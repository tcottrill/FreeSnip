#pragma once
#include <QObject>
#include <QDBusConnection>
#include <QImage>
#include <QTimer>
#include <QVariantMap>
enum class CaptureMode { Rectangle, FullDesktop, ActiveWindow, DesktopOptions };
class PortalCapture : public QObject {
 Q_OBJECT
public:
 explicit PortalCapture(QObject* parent=nullptr, QString service="org.freedesktop.portal.Desktop", int timeoutMs=120000);
 ~PortalCapture();
 void start(CaptureMode mode=CaptureMode::Rectangle);
 void cancel();
 bool busy() const { return active_; }
signals:
 void captured(QImage image);
 void cancelled();
 void failed(QString message);
private slots:
 void response(uint code, const QVariantMap& results);
private:
 QString service_, path_; QTimer timer_; bool active_=false; quint64 generation_=0;
 void clear();
 void fail(const QString& message);
 void closeRequest();
 void requestScreenshot(CaptureMode mode, uint version, uint targets);
};
