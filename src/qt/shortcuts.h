#pragma once
#include <QObject>
#include <QVariantMap>
#include <QDBusObjectPath>
#include <QTimer>
class GlobalShortcut : public QObject {
 Q_OBJECT
public:
 explicit GlobalShortcut(QObject* parent=nullptr, QString service="org.freedesktop.portal.Desktop");
 ~GlobalShortcut();
 void setup();
signals:
 void activated();
 void status(QString text);
private slots:
 void response(uint code,const QVariantMap& results);
 void triggered(const QDBusObjectPath& session,const QString& id,qulonglong timestamp,const QVariantMap& options);
private:
 QString service_, request_, session_; bool binding_=false; QTimer timer_;
 void request(const QString& method, QVariantList args, QVariantMap options);
 void clearRequest();
 void reset();
 void failure(const QString& message);
};
