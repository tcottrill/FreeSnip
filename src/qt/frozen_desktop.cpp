#include "frozen_desktop.h"
#include "image_ops.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QWindow>
#include <QTimer>
#include <cmath>

class FrozenDesktop::Surface : public QWidget {
public:
 Surface(FrozenDesktop* owner,Output output):owner_(owner),output_(output),physical_(bool(output.screen)) {
  setObjectName("frozenScreen"); setWindowTitle("Snip - frozen screen");
  setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
  setCursor(Qt::CrossCursor); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
  resize(output.geometry.size());
  if(output.screen) {
   winId(); windowHandle()->setScreen(output.screen);
   connect(windowHandle(),&QWindow::screenChanged,this,[this]{ scheduleValidation(); });
  }
 }
 bool matchesOutput() const {
  return !physical_ || (output_.screen && windowHandle() && windowHandle()->isExposed() && isFullScreen()
   && windowHandle()->screen()==output_.screen && size()==output_.geometry.size()
   && output_.screen->geometry()==output_.geometry);
 }
 QString placementDescription() const {
  return QString("expected=%1x%2 actual=%3x%4 fullscreen=%5 exposed=%6 sameOutput=%7")
   .arg(output_.geometry.width()).arg(output_.geometry.height()).arg(width()).arg(height())
   .arg(isFullScreen()).arg(windowHandle() && windowHandle()->isExposed())
   .arg(windowHandle() && windowHandle()->screen()==output_.screen);
 }
protected:
 void enterEvent(QEnterEvent* e) override { trackPointer(e->position()); }
 void resizeEvent(QResizeEvent*) override { scheduleValidation(); }
 void changeEvent(QEvent* e) override { if(e->type()==QEvent::WindowStateChange) scheduleValidation(); }
 void paintEvent(QPaintEvent*) override {
  QPainter p(this); p.fillRect(rect(),Qt::black);
  p.translate(-output_.geometry.topLeft());
  p.drawImage(QRectF(owner_->bounds_),owner_->image_);
  p.fillRect(output_.geometry,QColor(0,0,0,125));
  QRectF selection=QRectF(owner_->a_,owner_->b_).normalized().intersected(owner_->bounds_);
  if(owner_->dragging_ && !selection.isEmpty()) {
   p.save(); p.setClipRect(selection); p.drawImage(QRectF(owner_->bounds_),owner_->image_); p.restore();
   p.setPen(QPen(QColor("#60a5fa"),2)); p.drawRect(selection);
  }
  p.resetTransform(); const auto pixels=owner_->pixels(selection);
  const QString text=owner_->dragging_?QString("%1 x %2 pixels").arg(pixels.width()).arg(pixels.height())
   :QString("Drag to snip    Enter: this screen    Esc / right-click: cancel");
  QRect box=p.fontMetrics().boundingRect(text).adjusted(-12,-8,12,8); box.moveTopLeft({16,16});
  p.fillRect(box,QColor(25,25,25)); p.setPen(Qt::white); p.drawText(box,Qt::AlignCenter,text);
 }
 void mousePressEvent(QMouseEvent* e) override {
  if(e->button()==Qt::RightButton) { owner_->cancel(); return; }
  if(e->button()!=Qt::LeftButton || !owner_->validateSurfaces()) return;
  trackPointer(e->position());
  owner_->a_=owner_->b_=e->position()+output_.geometry.topLeft(); owner_->dragging_=true; owner_->redraw();
 }
 void mouseMoveEvent(QMouseEvent* e) override {
  trackPointer(e->position());
  if(owner_->dragging_) { owner_->b_=e->position()+output_.geometry.topLeft(); owner_->redraw(); }
 }
 void mouseReleaseEvent(QMouseEvent* e) override {
  if(e->button()!=Qt::LeftButton || !owner_->dragging_ || !owner_->validateSurfaces()) return;
  trackPointer(e->position());
  owner_->b_=e->position()+output_.geometry.topLeft(); owner_->dragging_=false;
  auto r=owner_->pixels(QRectF(owner_->a_,owner_->b_).normalized());
  if(r.width()>1 && r.height()>1) owner_->finish(r); else owner_->redraw();
 }
 void keyPressEvent(QKeyEvent* e) override {
  if(e->key()==Qt::Key_Escape) owner_->cancel();
  else if(e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter) {
   if(!owner_->validateSurfaces()) return;
   QRect output=output_.geometry;
   if(owner_->havePointer_) for(const auto& candidate:owner_->outputs_)
    if(QRectF(candidate.geometry).contains(owner_->pointer_)) { output=candidate.geometry; break; }
   owner_->finish(owner_->pixels(output));
  }
 }
 void closeEvent(QCloseEvent* e) override { e->ignore(); owner_->cancel(); }
private:
 void trackPointer(QPointF local) { owner_->pointer_=local+output_.geometry.topLeft(); owner_->havePointer_=true; }
 void scheduleValidation() {
  if(owner_->ready_) QTimer::singleShot(100,this,[this]{ owner_->validateSurfaces(); });
 }
 FrozenDesktop* owner_; Output output_; bool physical_;
};

FrozenDesktop::FrozenDesktop(QImage image,QList<Output> outputs,QObject* parent)
 :QObject(parent),image_(std::move(image)),outputs_(std::move(outputs)) {
 image_.setDevicePixelRatio(1);
 for(const auto& output:outputs_) bounds_=bounds_.united(output.geometry);
}
FrozenDesktop::~FrozenDesktop() { qDeleteAll(surfaces_); }
bool FrozenDesktop::valid() const {
 if(image_.isNull() || bounds_.isEmpty() || outputs_.isEmpty()) return false;
 const double sx=double(image_.width())/bounds_.width(), sy=double(image_.height())/bounds_.height();
 return std::abs(sx-sy)<=1.0/qMin(bounds_.width(),bounds_.height());
}
QRect FrozenDesktop::pixels(QRectF r) const { return snip::imageSelection(r.topLeft(),r.bottomRight(),bounds_,image_.size()); }
void FrozenDesktop::show() {
 if(!valid() || !surfaces_.isEmpty() || finished_) return;
 for(const auto& output:outputs_) {
  auto* surface=new Surface(this,output); surfaces_.append(surface); surface->showFullScreen();
 }
 if(!surfaces_.isEmpty()) { surfaces_.first()->activateWindow(); surfaces_.first()->setFocus(); }
 QTimer::singleShot(1500,this,[this]{ validateSurfaces(); });
}
bool FrozenDesktop::validateSurfaces() {
 if(finished_) return false;
 for(auto* surface:surfaces_) if(!surface->matchesOutput()) {
  finished_=true; dismiss();
  qWarning().noquote()<<"Frozen screen placement rejected:"<<surface->placementDescription();
  emit failed("The desktop did not place a fullscreen overlay on the expected screen. Capture cancelled to avoid a misaligned snip. Use Capture > Desktop options or retry.");
  return false;
 }
 ready_=true; return true;
}
QList<QWidget*> FrozenDesktop::surfaces() const { QList<QWidget*> list; for(auto* s:surfaces_) list.append(s); return list; }
void FrozenDesktop::redraw() { for(auto* surface:surfaces_) surface->update(); }
void FrozenDesktop::dismiss() { for(auto* surface:surfaces_) surface->hide(); }
void FrozenDesktop::cancel() { if(finished_) return; finished_=true; dismiss(); emit cancelled(); }
void FrozenDesktop::finish(QRect r) {
 if(finished_ || r.isEmpty()) return;
 finished_=true; dismiss(); emit selected(image_.copy(r));
}
