#include "main_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QDir>
#include <QPainter>
#include <QTimer>
#include <QTextStream>

int main(int argc,char** argv) {
 QApplication app(argc,argv);
 app.setOrganizationName("tcottrill"); app.setApplicationName("Snip"); app.setApplicationVersion("1.1.0");
 app.setDesktopFileName("io.github.tcottrill.Snip"); app.setQuitOnLastWindowClosed(false);
 QCommandLineParser parser; parser.setApplicationDescription("Capture, crop, copy and save PNG screenshots.");
 parser.addHelpOption(); parser.addVersionOption();
 parser.addOption({"snip","Start a new snip (also activates a running instance)."});
 parser.addOption({"tray","Start in the tray, if available."});
 parser.addOption({"smoke-test","Show a synthetic preview and exit; does not capture the desktop."});
 parser.process(app); bool smoke=parser.isSet("smoke-test");
 QPixmap icon(64,64); icon.fill(Qt::transparent); QPainter p(&icon);
 p.setBrush(QColor("#2563eb")); p.setPen(Qt::NoPen); p.drawRoundedRect(0,0,64,64,12,12);
 p.setPen(QPen(Qt::white,5)); p.drawRect(17,17,30,30); p.end(); app.setWindowIcon(QIcon(icon));
 QString dir=QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/tcottrill-snip";
 if(!QDir().mkpath(dir)) { QTextStream(stderr)<<"Cannot create runtime directory\n"; return 1; }
 QString socket=dir+"/instance"; QLockFile lock(dir+"/instance.lock");
 QLocalServer server;
 if(!smoke) {
  if(!lock.tryLock(100)) {
   QLocalSocket other; other.connectToServer(socket);
   if(!other.waitForConnected(2000)) { QTextStream(stderr)<<"Snip is running but could not be contacted. Try again.\n"; return 1; }
   other.write(parser.isSet("tray")?"show\n":"snip\n");
   if(!other.waitForBytesWritten(2000)) return 1;
   if(!other.waitForReadyRead(2000) || other.readAll()!="ok\n") return 1;
   other.disconnectFromServer(); return 0;
  }
  QLocalServer::removeServer(socket); server.setSocketOptions(QLocalServer::UserAccessOption);
  if(!server.listen(socket)) { QTextStream(stderr)<<server.errorString()<<"\n"; return 1; }
 }
 MainWindow window(smoke);
 QObject::connect(&server,&QLocalServer::newConnection,&window,[&]{
  while(auto* client=server.nextPendingConnection()) {
   auto read=[client,&window]{
    if(client->bytesAvailable()>64) { client->disconnectFromServer(); return; }
    if(!client->canReadLine()) return;
    auto command=client->readLine().trimmed();
    if(command=="snip") window.newSnip(); else if(command=="show") window.showPreview();
    client->write("ok\n");
    client->disconnectFromServer();
   };
   QObject::connect(client,&QLocalSocket::readyRead,&window,read);
   QObject::connect(client,&QLocalSocket::disconnected,client,&QObject::deleteLater);
   QTimer::singleShot(3000,client,[client]{ client->disconnectFromServer(); }); read();
  }
 });
 if(smoke) {
  QImage image(301,151,QImage::Format_RGB32); image.fill(QColor("#2563eb")); window.showImage(image);
  QTimer::singleShot(700,&app,&QApplication::quit);
 } else if(parser.isSet("tray")) {
  if(!QSystemTrayIcon::isSystemTrayAvailable()) window.showPreview();
 } else { QTimer::singleShot(0,&window,&MainWindow::newSnip); }
 return app.exec();
}
