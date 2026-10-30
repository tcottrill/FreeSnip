#include "shortcuts.h"
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUuid>
struct Shortcut { QString id; QVariantMap options; };
Q_DECLARE_METATYPE(Shortcut)
Q_DECLARE_METATYPE(QList<Shortcut>)
QDBusArgument& operator<<(QDBusArgument& arg,const Shortcut& s) { arg.beginStructure(); arg<<s.id<<s.options; arg.endStructure(); return arg; }
const QDBusArgument& operator>>(const QDBusArgument& arg,Shortcut& s) { arg.beginStructure(); arg>>s.id>>s.options; arg.endStructure(); return arg; }
GlobalShortcut::GlobalShortcut(QObject* parent,QString service):QObject(parent),service_(std::move(service)) {
 qDBusRegisterMetaType<Shortcut>(); qDBusRegisterMetaType<QList<Shortcut>>();
 timer_.setSingleShot(true); timer_.setInterval(120000);
 connect(&timer_,&QTimer::timeout,this,[this]{ failure("Shortcut setup timed out. You can also assign the Snip launcher in desktop settings."); });
 QDBusConnection::sessionBus().connect(service_,"/org/freedesktop/portal/desktop","org.freedesktop.portal.GlobalShortcuts","Activated",this,SLOT(triggered(QDBusObjectPath,QString,qulonglong,QVariantMap)));
}
GlobalShortcut::~GlobalShortcut() { reset(); }
void GlobalShortcut::clearRequest() {
 timer_.stop();
 if(request_.isEmpty()) return;
 QDBusConnection::sessionBus().disconnect(service_,request_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)));
 request_.clear();
}
void GlobalShortcut::reset() {
 auto bus=QDBusConnection::sessionBus();
 if(!request_.isEmpty()) bus.asyncCall(QDBusMessage::createMethodCall(service_,request_,"org.freedesktop.portal.Request","Close"));
 clearRequest();
 if(!session_.isEmpty()) bus.asyncCall(QDBusMessage::createMethodCall(service_,session_,"org.freedesktop.portal.Session","Close"));
 session_.clear(); binding_=false;
}
void GlobalShortcut::failure(const QString& message) { reset(); emit status(message); }
void GlobalShortcut::setup() {
 if(!request_.isEmpty()) return;
 reset();
 request("CreateSession",{},{{"session_handle_token","snip"+QUuid::createUuid().toString(QUuid::Id128)}});
}
void GlobalShortcut::request(const QString& method,QVariantList args,QVariantMap options) {
 auto bus=QDBusConnection::sessionBus();
 QString token="snip"+QUuid::createUuid().toString(QUuid::Id128), sender=bus.baseService().mid(1); sender.replace('.','_');
 options.insert("handle_token",token);
 request_="/org/freedesktop/portal/desktop/request/"+sender+"/"+token;
 bus.connect(service_,request_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)));
 auto msg=QDBusMessage::createMethodCall(service_,"/org/freedesktop/portal/desktop","org.freedesktop.portal.GlobalShortcuts",method);
 args<<options; msg.setArguments(args); const auto expected=request_;
 auto* watcher=new QDBusPendingCallWatcher(bus.asyncCall(msg),this); timer_.start();
 connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,expected](QDBusPendingCallWatcher* w){
  QDBusPendingReply<QDBusObjectPath> reply=*w; w->deleteLater();
  if(request_!=expected) return;
  if(reply.isError()) { failure("Global shortcut setup is unavailable on this desktop. Assign the Snip launcher in desktop keyboard settings."); return; }
  if(reply.value().path()!=request_) {
   clearRequest(); request_=reply.value().path();
   QDBusConnection::sessionBus().connect(service_,request_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap))); timer_.start();
  }
 });
}
void GlobalShortcut::response(uint code,const QVariantMap& results) {
 if(request_.isEmpty()) return;
 clearRequest();
 if(code!=0) { failure(code==1?"Shortcut setup cancelled":"Desktop shortcut setup failed"); return; }
 if(!binding_) {
  session_=results.value("session_handle").toString();
  if(!session_.startsWith("/org/freedesktop/portal/desktop/session/")) { failure("Desktop returned an invalid shortcut session"); return; }
  binding_=true;
  QList<Shortcut> shortcuts{{"snip",{{"description","Take a new snip"},{"preferred_trigger","CTRL+ALT+s"}}}};
  request("BindShortcuts",{QVariant::fromValue(QDBusObjectPath(session_)),QVariant::fromValue(shortcuts),QString()},{});
 } else emit status("Global shortcut enabled. The desktop controls the assigned key.");
}
void GlobalShortcut::triggered(const QDBusObjectPath& session,const QString& id,qulonglong,const QVariantMap&) {
 if(!session_.isEmpty() && session.path()==session_ && id=="snip") emit activated();
}
