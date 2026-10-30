#include "main_window.h"
#include "selection.h"
#include <QTest>
#include <QClipboard>
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QTemporaryDir>
#include <QSettings>
#include <QStatusBar>
#include <QLineEdit>
#include <QDBusVirtualObject>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QScreen>
#include <QUrl>
class PendingPortal : public QDBusVirtualObject {
public:
 QString imageUri; QVariantMap lastOptions;
 QString introspect(const QString&) const override { return {}; }
 bool handleMessage(const QDBusMessage& msg,const QDBusConnection& bus) override {
  if(msg.member()=="GetAll") {
   bus.send(msg.createReply(QVariantMap{{"version",uint(3)},{"AvailableTargets",uint(15)}})); return true;
  }
  if(msg.member()=="Close") { bus.send(msg.createReply()); return true; }
  if(msg.member()!="Screenshot") return false;
  auto options=qdbus_cast<QVariantMap>(msg.arguments().last());
  lastOptions=options;
  QString sender=msg.service().mid(1); sender.replace('.','_');
  auto path="/org/freedesktop/portal/desktop/request/"+sender+"/"+options.value("handle_token").toString();
  bus.send(msg.createReply(QVariant::fromValue(QDBusObjectPath(path))));
  if(!imageUri.isEmpty()) QTimer::singleShot(10,this,[this,path,bus]{
   auto response=QDBusMessage::createSignal(path,"org.freedesktop.portal.Request","Response");
   response << uint(0) << QVariantMap{{"uri",imageUri}}; bus.send(response);
  });
  return true;
 }
};
class UiTests : public QObject {
 Q_OBJECT
private slots:
 void frozenRectangleToClipboard() {
  PendingPortal portal; auto bus=QDBusConnection::sessionBus();
  QVERIFY(bus.registerService("io.github.tcottrill.Snip.FrozenTest"));
  QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop",&portal,QDBusConnection::SubPath));
  QRect bounds; for(auto* screen:QGuiApplication::screens()) bounds=bounds.united(screen->geometry());
  QImage desktop(bounds.size(),QImage::Format_RGB32);
  for(int y=0;y<desktop.height();++y) for(int x=0;x<desktop.width();++x) desktop.setPixelColor(x,y,QColor(x%256,y%256,(x+y)%256));
  QTemporaryDir files; QVERIFY(desktop.save(files.filePath("desktop.png")));
  portal.imageUri=QUrl::fromLocalFile(files.filePath("desktop.png")).toString();
  MainWindow w(true,"io.github.tcottrill.Snip.FrozenTest"); w.newSnip();
  auto frozenSurfaces=[] {
   QList<QWidget*> surfaces;
   for(auto* widget:QApplication::topLevelWidgets()) if(widget->objectName()=="frozenScreen" && widget->isVisible()) surfaces.append(widget);
   return surfaces;
  };
  QTRY_COMPARE(frozenSurfaces().size(),QGuiApplication::screens().size());
  auto* surface=frozenSurfaces().first(); QVERIFY(surface->isFullScreen());
  QVERIFY(QTest::qWaitForWindowExposed(surface));
  QTRY_COMPARE(surface->size(),surface->screen()->geometry().size());
  QVERIFY(!w.isVisible()); QVERIFY(!portal.lastOptions.value("interactive").toBool());
  QVERIFY(surface->grab().save("frozen-desktop-test.png"));
  const auto origin=surface->screen()->geometry().topLeft()-bounds.topLeft();
  QTest::mousePress(surface,Qt::LeftButton,{},QPoint(100,100));
  QTest::mouseMove(surface,QPoint(400,250));
  QTest::mouseRelease(surface,Qt::LeftButton,{},QPoint(400,250));
  QTRY_VERIFY(w.isVisible());
  QCOMPARE(QApplication::clipboard()->image(),desktop.copy(QRect(origin+QPoint(100,100),QSize(300,150))));
  QCOMPARE(w.windowTitle(),QString("Snip - 300 x 150"));
  QTRY_VERIFY(frozenSurfaces().isEmpty());
  // Full desktop, active-window and interactive options are not forced into
  // rectangle mode. Their exact returned image is immediately copied.
  for(auto mode:{CaptureMode::FullDesktop,CaptureMode::ActiveWindow,CaptureMode::DesktopOptions}) {
   w.startCapture(mode);
   QTRY_COMPARE(QApplication::clipboard()->image(),desktop);
   QVERIFY(frozenSurfaces().isEmpty());
   if(mode==CaptureMode::ActiveWindow) QCOMPARE(portal.lastOptions.value("target").toUInt(),uint(8));
   if(mode==CaptureMode::DesktopOptions) QVERIFY(portal.lastOptions.value("interactive").toBool());
   QApplication::clipboard()->clear();
  }
  bus.unregisterObject("/org/freedesktop/portal/desktop",QDBusConnection::UnregisterTree);
  bus.unregisterService("io.github.tcottrill.Snip.FrozenTest");
 }
 void pendingCaptureStaysHidden() {
  PendingPortal portal; auto bus=QDBusConnection::sessionBus();
  QVERIFY(bus.registerService("io.github.tcottrill.Snip.PendingTest"));
  QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop",&portal,QDBusConnection::SubPath));
  MainWindow w(true,"io.github.tcottrill.Snip.PendingTest"); w.showPreview(); w.newSnip();
  QTest::qWait(1100); QVERIFY(!w.isVisible());
  w.showPreview(); QVERIFY2(!w.isVisible(),"Tray/second-instance activation must not cover a pending capture");
  for(auto* action:w.findChildren<QAction*>()) if(action->text()=="Cancel capture") action->trigger();
  QVERIFY(w.isVisible());
  bus.unregisterObject("/org/freedesktop/portal/desktop",QDBusConnection::UnregisterTree);
  bus.unregisterService("io.github.tcottrill.Snip.PendingTest");
 }
 void previewCopyAndCancel() {
  MainWindow w(true); QImage im(301,151,QImage::Format_RGB32); im.fill(QColor("#2563eb"));
  im.setPixelColor(300,150,Qt::red); w.showImage(im); QTest::qWait(20);
  QCOMPARE(w.windowTitle(),QString("Snip - 301 x 151")); QCOMPARE(QApplication::clipboard()->image(),im);
  QApplication::clipboard()->clear();
  for(auto* action:w.findChildren<QAction*>()) if(action->text()=="Copy") action->trigger();
  QCOMPARE(QApplication::clipboard()->image(),im);
  // Cancelling the delay before capture must not overwrite the last image.
  w.newSnip();
  for(auto* action:w.findChildren<QAction*>()) if(action->text()=="Cancel capture") action->trigger();
  QTest::qWait(300); QCOMPARE(w.windowTitle(),QString("Snip - 301 x 151"));
  QCOMPARE(QApplication::clipboard()->image(),im); QVERIFY(w.isVisible());
  QTemporaryDir output; const QString path=output.filePath("saved");
  QTimer::singleShot(50,&w,[path]{
   for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QFileDialog*>(widget)) {
    dialog->setDirectory(QFileInfo(path).absolutePath());
    auto* name=dialog->findChild<QLineEdit*>("fileNameEdit"); QVERIFY(name);
    name->setText(QFileInfo(path).fileName()); QMetaObject::invokeMethod(dialog,"accept");
   }
  });
  for(auto* action:w.findChildren<QAction*>()) if(action->text()=="Save As...") action->trigger();
  QVERIFY2(QFileInfo::exists(path),"Save must use the exact filename authorized by the file chooser");
  QCOMPARE(QImage(path),im);
  QVERIFY(w.grab().save("preview-test.png"));
  Selection selection(im); selection.resize(800,450); selection.show(); QTest::qWait(20);
  QVERIFY(selection.grab().save("selection-test.png"));
 }
};
int main(int argc,char** argv) {
 QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
 QApplication app(argc,argv); QTemporaryDir settings;
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
 app.setOrganizationName("SnipTest"); app.setApplicationName("UiTest");
 UiTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "ui_tests.moc"
