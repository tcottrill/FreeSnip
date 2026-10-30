#include "main_window.h"
#include "selection.h"
#include "image_ops.h"
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QToolBar>
#include <QToolButton>
#include <QStatusBar>
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QScreen>
#include <QPainter>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QSaveFile>

MainWindow::MainWindow(bool smoke,QString portalService):tray_(this),portal_(this,std::move(portalService)),shortcut_(this),smoke_(smoke) {
 setWindowTitle("Snip"); resize(760,500);
 preview_=new QLabel("New snip captures your screen.\nDrag a rectangle, then paste or save the result.",this);
 preview_->setAlignment(Qt::AlignCenter); preview_->setMinimumSize(100,80); setCentralWidget(preview_);
 auto* bar=addToolBar("Snip"); bar->setMovable(false); bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
 fresh_=bar->addAction("New",this,&MainWindow::newSnip); fresh_->setShortcut(QKeySequence("Ctrl+N"));
 auto* captureMenu=new QMenu("Capture",this);
 captureMenu->addAction("Rectangle",this,&MainWindow::newSnip);
 captureMenu->addAction("Full desktop",this,[this]{ startCapture(CaptureMode::FullDesktop); });
 captureMenu->addAction("Active window",this,[this]{ startCapture(CaptureMode::ActiveWindow); });
 captureMenu->addAction("Desktop options...",this,[this]{ startCapture(CaptureMode::DesktopOptions); });
 auto* captureAction=bar->addAction("Capture"); captureAction->setMenu(captureMenu);
 if(auto* button=qobject_cast<QToolButton*>(bar->widgetForAction(captureAction))) button->setPopupMode(QToolButton::InstantPopup);
 copy_=bar->addAction("Copy",this,&MainWindow::copyImage); copy_->setShortcut(QKeySequence::Copy);
 save_=bar->addAction("Save As...",this,&MainWindow::saveImage); save_->setShortcut(QKeySequence::Save);
 copy_->setEnabled(false); save_->setEnabled(false);
 cancel_=bar->addAction("Cancel capture",this,[this]{
  ++captureGeneration_; pending_=false; portal_.cancel(); if(selection_) selection_->close(); if(frozen_) frozen_->cancel(); restore();
 }); cancel_->setEnabled(false);
 bar->addAction("Shortcut...",&shortcut_,&GlobalShortcut::setup);
 connect(&shortcut_,&GlobalShortcut::activated,this,&MainWindow::newSnip);
 connect(&shortcut_,&GlobalShortcut::status,this,[this](const QString& text){ statusBar()->showMessage(text); });
 auto* quit=bar->addAction("Quit",qApp,&QApplication::quit); quit->setShortcut(QKeySequence("Ctrl+Q"));
 auto* closeAction=new QAction(this); closeAction->setShortcut(QKeySequence(Qt::Key_Escape));
 connect(closeAction,&QAction::triggered,this,&MainWindow::close); addAction(closeAction);
 auto* menu=new QMenu(this); menu->addAction("New snip",this,&MainWindow::newSnip);
 menu->addMenu(captureMenu);
 menu->addAction("Show Snip",this,&MainWindow::showPreview); menu->addAction(cancel_); menu->addSeparator(); menu->addAction("Exit",qApp,&QApplication::quit);
 tray_.setContextMenu(menu); tray_.setIcon(windowIcon()); tray_.setToolTip("Snip - click to capture");
 connect(&tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
  if(reason==QSystemTrayIcon::Trigger) newSnip();
 });
 if(!smoke_ && QSystemTrayIcon::isSystemTrayAvailable()) tray_.show();
 connect(&portal_,&PortalCapture::captured,this,[this](QImage image){
  if(mode_==CaptureMode::Rectangle) freezeDesktop(std::move(image)); else showImage(std::move(image));
 });
 connect(&portal_,&PortalCapture::cancelled,this,&MainWindow::restore);
 connect(&portal_,&PortalCapture::failed,this,[this](const QString& message){ restore(); QMessageBox::warning(this,"Screen capture",message); });
 statusBar()->showMessage("Ready");
 auto changed=[this]{
  if(!pending_ && !frozen_ && !selection_) return;
  ++captureGeneration_; pending_=false; portal_.cancel();
  if(frozen_) frozen_->cancel();
  if(selection_) selection_->close();
  restore();
  statusBar()->showMessage("Display layout changed. Start a new snip.");
 };
 auto watch=[this,changed](QScreen* screen){ connect(screen,&QScreen::geometryChanged,this,changed); };
 for(auto* screen:QGuiApplication::screens()) watch(screen);
 connect(qApp,&QGuiApplication::screenAdded,this,[watch,changed](QScreen* screen){ changed(); watch(screen); });
 connect(qApp,&QGuiApplication::screenRemoved,this,changed);
}
void MainWindow::showPreview() {
 if(pending_ || portal_.busy() || frozen_) return;
 show(); raise(); activateWindow();
}
void MainWindow::restore() {
 pending_=false; fresh_->setEnabled(true); cancel_->setEnabled(false);
 statusBar()->showMessage(image_.isNull()?"Ready":"Last snip kept"); showPreview();
}
void MainWindow::newSnip() { startCapture(CaptureMode::Rectangle); }
void MainWindow::startCapture(CaptureMode mode) {
 if(pending_ || portal_.busy() || selection_ || frozen_) return;
 mode_=mode;
 pending_=true; fresh_->setEnabled(false); cancel_->setEnabled(true); hide();
 const auto generation=++captureGeneration_;
 QTimer::singleShot(250,this,[this,generation]{ if(generation==captureGeneration_) capture(); });
}
void MainWindow::capture() {
 captureOutputs_.clear();
 for(auto* screen:QGuiApplication::screens()) captureOutputs_.append({screen->geometry(),screen});
 // XWayland cannot capture the Wayland desktop. Use the portal whenever a
 // Wayland session is detected, even if Qt was explicitly started with xcb.
 const bool wayland=QGuiApplication::platformName().contains("wayland") ||
   qEnvironmentVariable("XDG_SESSION_TYPE")=="wayland" || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
 if(wayland || QGuiApplication::platformName()!="xcb" || mode_==CaptureMode::ActiveWindow || mode_==CaptureMode::DesktopOptions) {
  portal_.start(mode_);
  // Keep our own window hidden until the desktop finishes capturing. The
  // portal dialog and tray menu provide cancellation while it is pending.
  return;
 }
 QRect desktop; qreal scale=1;
 const auto screens=QGuiApplication::screens();
 for(auto* screen:screens) { desktop=desktop.united(screen->geometry()); scale=qMax(scale,screen->devicePixelRatio()); }
 QImage composed(QSize(qCeil(desktop.width()*scale),qCeil(desktop.height()*scale)),QImage::Format_RGB32);
 if(composed.isNull()) { restore(); QMessageBox::warning(this,"Screen capture","Cannot allocate a desktop image."); return; }
 composed.fill(Qt::black); QPainter painter(&composed);
 for(auto* screen:screens) {
  auto shot=screen->grabWindow(0).toImage();
  if(shot.isNull()) { painter.end(); restore(); QMessageBox::warning(this,"Screen capture","The display server did not return a screen image."); return; }
  shot.setDevicePixelRatio(1); QRect g=screen->geometry();
  painter.drawImage(QRectF((g.x()-desktop.x())*scale,(g.y()-desktop.y())*scale,g.width()*scale,g.height()*scale),shot);
 }
 painter.end();
 if(mode_==CaptureMode::FullDesktop) showImage(std::move(composed)); else select(std::move(composed),desktop);
}
void MainWindow::freezeDesktop(QImage image) {
 hide(); auto* frozen=new FrozenDesktop(std::move(image),captureOutputs_,this);
 if(!frozen->valid()) {
  delete frozen; restore();
  QMessageBox::warning(this,"Screen layout","The desktop screenshot does not match the monitor layout. Use Capture > Desktop options for this layout, or retry after checking display scaling."); return;
 }
 frozen_=frozen;
 connect(frozen,&FrozenDesktop::selected,this,[this,frozen](QImage im){ frozen_=nullptr; frozen->deleteLater(); showImage(std::move(im)); });
 connect(frozen,&FrozenDesktop::cancelled,this,[this,frozen]{ frozen_=nullptr; frozen->deleteLater(); restore(); });
 connect(frozen,&FrozenDesktop::failed,this,[this,frozen](const QString& message){
  frozen_=nullptr; frozen->deleteLater(); restore();
  if(smoke_) { qWarning().noquote()<<message; statusBar()->showMessage(message); }
  else QMessageBox::warning(this,"Screen layout",message);
 });
 frozen->show();
}
void MainWindow::select(QImage image,QRect desktop) {
 hide(); auto* crop=new Selection(std::move(image)); selection_=crop; crop->setAttribute(Qt::WA_DeleteOnClose);
 connect(crop,&Selection::selected,this,[this](QImage im){ showImage(std::move(im)); });
 connect(crop,&Selection::cancelled,this,&MainWindow::restore);
 if(!desktop.isEmpty()) {
  qreal scale=1; for(auto* screen:QGuiApplication::screens()) scale=qMax(scale,screen->devicePixelRatio());
  QList<QRect> monitors;
  for(auto* screen:QGuiApplication::screens()) {
   const auto g=screen->geometry();
   monitors.append(QRect(qRound((g.x()-desktop.x())*scale),qRound((g.y()-desktop.y())*scale),qRound(g.width()*scale),qRound(g.height()*scale)));
  }
  crop->setMonitorRects(monitors);
  crop->setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint); crop->setGeometry(desktop); crop->show();
 }
 else { crop->resize(1100,750); crop->showMaximized(); }
 crop->raise(); crop->activateWindow(); crop->setFocus();
}
void MainWindow::showImage(QImage image) {
 if(image.isNull()) return;
 image_=std::move(image); image_.setDevicePixelRatio(1);
 pending_=false; fresh_->setEnabled(true); cancel_->setEnabled(false); copy_->setEnabled(true); save_->setEnabled(true);
 setWindowTitle(QString("Snip - %1 x %2").arg(image_.width()).arg(image_.height()));
 updatePreview(); showPreview(); copyImage();
}
void MainWindow::copyImage() {
 auto mime=snip::imageMime(image_);
 if(!mime) { statusBar()->showMessage("Cannot encode this image for the clipboard"); return; }
 QApplication::clipboard()->setMimeData(mime.release()); statusBar()->showMessage("Copied to clipboard - paste into your app");
}
void MainWindow::saveImage() {
 QSettings settings; QString dir=settings.value("saveDirectory",QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
 QString path=QFileDialog::getSaveFileName(this,"Save snip",dir+"/Snip-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+".png","PNG images (*.png)");
 if(path.isEmpty()) return;
 // A portal-returned path grants access to that exact filename. Never append
 // a suffix after authorization; PNG is the encoding regardless of its name.
 QSaveFile file(path);
 if(!file.open(QIODevice::WriteOnly) || !image_.save(&file,"PNG") || !file.commit()) {
  QMessageBox::warning(this,"Save failed","Could not save the PNG: "+file.errorString()); return;
 }
 settings.setValue("saveDirectory",QFileInfo(path).absolutePath()); statusBar()->showMessage("Saved "+path);
}
void MainWindow::updatePreview() {
 if(!image_.isNull()) preview_->setPixmap(QPixmap::fromImage(image_).scaled(preview_->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
}
void MainWindow::resizeEvent(QResizeEvent* e) { QMainWindow::resizeEvent(e); updatePreview(); }
void MainWindow::closeEvent(QCloseEvent* e) {
 if(portal_.busy() || pending_) { e->ignore(); return; }
 if(tray_.isVisible() && QSystemTrayIcon::isSystemTrayAvailable()) { hide(); e->ignore(); }
 else { e->accept(); qApp->quit(); }
}
