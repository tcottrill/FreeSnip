#include "portal.h"
#include <QTest>
#include <QSignalSpy>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QTemporaryDir>
#include <QUrl>
class FakePortal : public QObject, protected QDBusContext {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")
 Q_PROPERTY(uint version MEMBER version)
 Q_PROPERTY(uint AvailableTargets MEMBER targets)
public:
 uint version=2, targets=15;
 uint code=0; QString uri; bool respond=true; QVariantMap lastOptions;
public slots:
 QDBusObjectPath Screenshot(const QString&,const QVariantMap& options) {
  lastOptions=options;
  QString sender=message().service().mid(1); sender.replace('.','_');
  QString path="/org/freedesktop/portal/desktop/request/"+sender+"/"+options.value("handle_token").toString();
  if(respond) QTimer::singleShot(10,this,[this,path]{
   auto msg=QDBusMessage::createSignal(path,"org.freedesktop.portal.Request","Response");
   msg << code << QVariantMap{{"uri",uri}}; QDBusConnection::sessionBus().send(msg);
  });
  return QDBusObjectPath(path);
 }
};
class PortalTests : public QObject {
 Q_OBJECT
 FakePortal fake; QTemporaryDir tmp;
private slots:
 void initTestCase() {
  QVERIFY(QDBusConnection::sessionBus().registerService("io.github.tcottrill.Snip.TestPortal"));
  QVERIFY(QDBusConnection::sessionBus().registerObject("/org/freedesktop/portal/desktop",&fake,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllProperties));
  QImage im(123,45,QImage::Format_RGB32); im.fill(Qt::green); QVERIFY(im.save(tmp.filePath("test.png")));
 }
 void init() { fake.code=0; fake.version=2; fake.targets=15; fake.lastOptions.clear(); fake.respond=true; fake.uri=QUrl::fromLocalFile(tmp.filePath("test.png")).toString(); }
 void success() {
  PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000); QSignalSpy got(&p,&PortalCapture::captured);
  p.start(); QTRY_COMPARE(got.count(),1); QCOMPARE(qvariant_cast<QImage>(got[0][0]).size(),QSize(123,45)); QVERIFY(!p.busy());
 }
 void defaultCaptureMustNotOpenPicker() {
  PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000); QSignalSpy got(&p,&PortalCapture::captured);
  p.start(); QTRY_COMPARE(got.count(),1); QCOMPARE(fake.lastOptions.value("interactive").toBool(),false);
 }
 void cancelled() {
  fake.code=1; PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000);
  QSignalSpy cancel(&p,&PortalCapture::cancelled), got(&p,&PortalCapture::captured);
  p.start(); QTRY_COMPARE(cancel.count(),1); QCOMPARE(got.count(),0); QVERIFY(!p.busy());
 }
 void desktopOptionsRetained() {
  PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000); QSignalSpy got(&p,&PortalCapture::captured);
  p.start(CaptureMode::DesktopOptions); QTRY_COMPARE(got.count(),1); QVERIFY(fake.lastOptions.value("interactive").toBool());
  QVERIFY(!fake.lastOptions.contains("target"));
 }
 void fullScreenTarget() {
  fake.version=3; PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000); QSignalSpy got(&p,&PortalCapture::captured);
  p.start(CaptureMode::FullDesktop); QTRY_COMPARE(got.count(),1); QCOMPARE(fake.lastOptions.value("target").toUInt(),uint(1));
  QVERIFY(!fake.lastOptions.value("interactive").toBool());
 }
 void activeWindowTarget() {
  fake.version=3; PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000); QSignalSpy got(&p,&PortalCapture::captured);
  p.start(CaptureMode::ActiveWindow); QTRY_COMPARE(got.count(),1); QCOMPARE(fake.lastOptions.value("target").toUInt(),uint(8));
 }
 void activeWindowUnsupported() {
  PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000); QSignalSpy error(&p,&PortalCapture::failed);
  p.start(CaptureMode::ActiveWindow); QTRY_COMPARE(error.count(),1); QVERIFY(fake.lastOptions.isEmpty());
 }
 void invalidUri() {
  fake.uri="https://example.com/no.png"; PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000);
  QSignalSpy error(&p,&PortalCapture::failed); p.start(); QTRY_COMPARE(error.count(),1);
 }
 void timeout() {
  fake.respond=false; PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",40);
  QSignalSpy error(&p,&PortalCapture::failed); p.start(); QTRY_COMPARE(error.count(),1); QVERIFY(!p.busy());
 }
 void explicitCancel() {
  fake.respond=false; PortalCapture p(nullptr,"io.github.tcottrill.Snip.TestPortal",1000);
  QSignalSpy cancel(&p,&PortalCapture::cancelled); p.start(); p.cancel(); QCOMPARE(cancel.count(),1); QVERIFY(!p.busy());
 }
 void missingService() {
  PortalCapture p(nullptr,"io.github.tcottrill.Snip.NoSuchService",1000);
  QSignalSpy error(&p,&PortalCapture::failed); p.start(); QTRY_COMPARE(error.count(),1);
 }
};
QTEST_MAIN(PortalTests)
#include "portal_tests.moc"
