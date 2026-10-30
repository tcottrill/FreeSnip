#include "shortcuts.h"
#include <QTest>
#include <QSignalSpy>
#include <QDBusVirtualObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusArgument>

class FakeShortcuts : public QDBusVirtualObject {
public:
 bool cancelled=false; int binds=0; QString session;
 QString introspect(const QString&) const override { return {}; }
 bool handleMessage(const QDBusMessage& msg,const QDBusConnection& bus) override {
  if(msg.member()=="Close") { bus.send(msg.createReply()); return true; }
  if(msg.member()!="CreateSession" && msg.member()!="BindShortcuts") return false;
  const auto args=msg.arguments(); auto options=qdbus_cast<QVariantMap>(args.last());
  QString sender=msg.service().mid(1); sender.replace('.','_');
  QString path="/org/freedesktop/portal/desktop/request/"+sender+"/"+options.value("handle_token").toString();
  session="/org/freedesktop/portal/desktop/session/"+sender+"/test";
  bool bind=msg.member()=="BindShortcuts"; if(bind) ++binds;
  bus.send(msg.createReply(QVariant::fromValue(QDBusObjectPath(path))));
  QTimer::singleShot(10,this,[this,bus,path,bind]{
   auto response=QDBusMessage::createSignal(path,"org.freedesktop.portal.Request","Response");
   response << uint(cancelled?1:0) << (bind?QVariantMap():QVariantMap{{"session_handle",session}}); bus.send(response);
  });
  return true;
 }
 void activate(QString id) {
  auto signal=QDBusMessage::createSignal("/org/freedesktop/portal/desktop","org.freedesktop.portal.GlobalShortcuts","Activated");
  signal<<QVariant::fromValue(QDBusObjectPath(session))<<id<<qulonglong(123)<<QVariantMap(); QDBusConnection::sessionBus().send(signal);
 }
};
class ShortcutTests : public QObject {
 Q_OBJECT
 FakeShortcuts fake;
private slots:
 void initTestCase() {
  QVERIFY(QDBusConnection::sessionBus().registerService("io.github.tcottrill.Snip.ShortcutTests"));
  QVERIFY(QDBusConnection::sessionBus().registerVirtualObject("/org/freedesktop/portal/desktop",&fake,QDBusConnection::SubPath));
 }
 void bindAndActivate() {
  GlobalShortcut s(nullptr,"io.github.tcottrill.Snip.ShortcutTests");
  QSignalSpy status(&s,&GlobalShortcut::status), activated(&s,&GlobalShortcut::activated);
  s.setup(); QTRY_COMPARE(status.count(),1); QCOMPARE(fake.binds,1);
  QVERIFY(status[0][0].toString().startsWith("Global shortcut enabled"));
  fake.activate("other"); QTest::qWait(20); QCOMPARE(activated.count(),0);
  fake.activate("snip"); QTRY_COMPARE(activated.count(),1);
 }
 void cancel() {
  fake.cancelled=true; int binds=fake.binds; GlobalShortcut s(nullptr,"io.github.tcottrill.Snip.ShortcutTests");
  QSignalSpy status(&s,&GlobalShortcut::status); s.setup(); QTRY_COMPARE(status.count(),1);
  QCOMPARE(fake.binds,binds); QCOMPARE(status[0][0].toString(),QString("Shortcut setup cancelled"));
 }
 void unavailable() {
  GlobalShortcut s(nullptr,"io.github.tcottrill.Snip.NoService"); QSignalSpy status(&s,&GlobalShortcut::status);
  s.setup(); QTRY_COMPARE(status.count(),1); QVERIFY(status[0][0].toString().contains("unavailable"));
 }
};
QTEST_MAIN(ShortcutTests)
#include "shortcut_tests.moc"
