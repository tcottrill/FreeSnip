#include "portal.h"
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QUuid>
#include <QUrl>
PortalCapture::PortalCapture(QObject* parent,QString service,int timeoutMs):QObject(parent),service_(std::move(service)) {
 timer_.setInterval(timeoutMs); timer_.setSingleShot(true);
 connect(&timer_,&QTimer::timeout,this,[this]{ closeRequest(); fail("Screen capture timed out. Check your desktop's screenshot permission dialog and try again."); });
}
PortalCapture::~PortalCapture() { if(active_) closeRequest(); clear(); }
void PortalCapture::start(CaptureMode mode) {
 if(active_) return;
 auto bus=QDBusConnection::sessionBus();
 if(!bus.isConnected()) { emit failed("No desktop session bus is available."); return; }
 active_=true; const auto generation=++generation_; timer_.start();
 if(mode==CaptureMode::DesktopOptions) { requestScreenshot(mode,0,0); return; }
 auto properties=QDBusMessage::createMethodCall(service_,"/org/freedesktop/portal/desktop","org.freedesktop.DBus.Properties","GetAll");
 properties << QString("org.freedesktop.portal.Screenshot");
 auto* watcher=new QDBusPendingCallWatcher(bus.asyncCall(properties),this);
 connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,generation,mode](QDBusPendingCallWatcher* w){
  QDBusPendingReply<QVariantMap> reply=*w; w->deleteLater();
  if(!active_ || generation!=generation_) return;
  const auto values=reply.isError()?QVariantMap():reply.value();
  requestScreenshot(mode,values.value("version").toUInt(),values.value("AvailableTargets").toUInt());
 });
}
void PortalCapture::requestScreenshot(CaptureMode mode,uint version,uint targets) {
 if(mode==CaptureMode::ActiveWindow && (version<3 || !(targets&8))) {
  fail("This desktop does not advertise direct active-window capture. Use Capture > Desktop options to select a window with your desktop's picker."); return;
 }
 auto bus=QDBusConnection::sessionBus(); const auto generation=generation_;
 QString token="snip"+QUuid::createUuid().toString(QUuid::Id128);
 QString sender=bus.baseService().mid(1); sender.replace('.','_');
 path_="/org/freedesktop/portal/desktop/request/"+sender+"/"+token;
 if(!bus.connect(service_,path_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)))) {
  fail("Cannot listen for the desktop screenshot response."); return;
 }
 auto msg=QDBusMessage::createMethodCall(service_,"/org/freedesktop/portal/desktop","org.freedesktop.portal.Screenshot","Screenshot");
 QVariantMap options{{"handle_token",token},{"interactive",mode==CaptureMode::DesktopOptions},{"modal",false}};
 if(mode==CaptureMode::ActiveWindow) options.insert("target",uint(8));
 else if(mode!=CaptureMode::DesktopOptions && version>=3 && (targets&1)) options.insert("target",uint(1));
 msg << QString() << options;
 auto* watcher=new QDBusPendingCallWatcher(bus.asyncCall(msg),this);
 connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,generation](QDBusPendingCallWatcher* w){
  QDBusPendingReply<QDBusObjectPath> reply=*w; w->deleteLater();
  if(!active_ || generation!=generation_) return;
  if(reply.isError()) { fail("Desktop screenshot service unavailable: "+reply.error().message()+"\nCheck that your desktop's XDG screenshot portal is installed and running."); return; }
  if(reply.value().path()!=path_) {
   auto bus=QDBusConnection::sessionBus();
   bus.disconnect(service_,path_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)));
   path_=reply.value().path();
   if(!bus.connect(service_,path_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap))))
    fail("Cannot listen for the desktop screenshot response.");
  }
 });
}
void PortalCapture::cancel() { if(active_) { closeRequest(); clear(); emit cancelled(); } }
void PortalCapture::response(uint code,const QVariantMap& results) {
 if(!active_) return;
 clear();
 if(code==1) { emit cancelled(); return; }
 if(code!=0) { emit failed("The desktop could not capture a screenshot."); return; }
 QUrl uri(results.value("uri").toString());
 if(!uri.isLocalFile()) { emit failed("The desktop returned an invalid screenshot file."); return; }
 QImage image(uri.toLocalFile());
 if(image.isNull()) { emit failed("Cannot open the screenshot returned by the desktop."); return; }
 emit captured(image);
}
void PortalCapture::clear() {
 timer_.stop(); active_=false;
 QDBusConnection::sessionBus().disconnect(service_,path_,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap)));
 path_.clear();
}
void PortalCapture::fail(const QString& message) { clear(); emit failed(message); }
void PortalCapture::closeRequest() {
 if(path_.isEmpty()) return;
 QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(service_,path_,"org.freedesktop.portal.Request","Close"));
}
