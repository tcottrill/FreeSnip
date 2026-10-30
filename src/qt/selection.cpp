#include "selection.h"
#include "image_ops.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
Selection::Selection(QImage image, QWidget* parent) : QWidget(parent),image_(std::move(image)) {
 image_.setDevicePixelRatio(1); setWindowTitle("Snip - drag to crop");
 setCursor(Qt::CrossCursor); setFocusPolicy(Qt::StrongFocus);
}
QRectF Selection::imageRect() const {
 QSizeF size = image_.size(); size.scale(this->size(),Qt::KeepAspectRatio);
 return QRectF(QPointF((width()-size.width())/2,(height()-size.height())/2),size);
}
void Selection::paintEvent(QPaintEvent*) {
 QPainter p(this); p.fillRect(rect(),Qt::black); auto target = imageRect();
 p.drawImage(target,image_); p.fillRect(target,QColor(0,0,0,130));
 auto selected = QRectF(a_,b_).normalized().intersected(target);
 if (dragging_ && !selected.isEmpty()) {
  p.save(); p.setClipRect(selected); p.drawImage(target,image_); p.restore();
  p.setPen(QPen(QColor("#60a5fa"),2)); p.drawRect(selected);
 }
 auto pixels = snip::imageSelection(a_,b_,target,image_.size());
 QString text = dragging_ ? QString("%1 x %2 pixels").arg(pixels.width()).arg(pixels.height())
                          : (monitors_.isEmpty()?QString("Drag to snip    Enter: whole image    Esc / right-click: cancel")
                                                :QString("Drag to snip    Enter: this monitor    Esc / right-click: cancel"));
 auto box = p.fontMetrics().boundingRect(text).adjusted(-12,-8,12,8);
 box.moveTopLeft(QPoint(16,16)); p.fillRect(box,QColor(25,25,25)); p.setPen(Qt::white); p.drawText(box,Qt::AlignCenter,text);
}
void Selection::mousePressEvent(QMouseEvent* e) {
 if(e->button()==Qt::RightButton) { close(); return; }
 if(e->button()==Qt::LeftButton && imageRect().contains(e->position())) {
  a_=b_=e->position(); dragging_=true; update();
 }
}
void Selection::mouseMoveEvent(QMouseEvent* e) { if(dragging_) { b_=e->position(); update(); } }
void Selection::mouseReleaseEvent(QMouseEvent* e) {
 if(e->button()!=Qt::LeftButton || !dragging_) return;
 b_=e->position(); auto r=snip::imageSelection(a_,b_,imageRect(),image_.size());
 dragging_=false; if(r.width()>1 && r.height()>1) finish(r); else update();
}
void Selection::finish(QRect r) { finished_=true; emit selected(image_.copy(r)); close(); }
void Selection::keyPressEvent(QKeyEvent* e) {
 if(e->key()==Qt::Key_Escape) close();
 else if(e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter) {
  const QPointF pos=mapFromGlobal(QCursor::pos());
  auto point=snip::imageSelection(pos,pos+QPointF(1,1),imageRect(),image_.size()).topLeft();
  for(const auto& monitor:monitors_) if(monitor.contains(point)) { finish(monitor); return; }
  finish(image_.rect());
 }
}
void Selection::closeEvent(QCloseEvent* e) { if(!finished_) { finished_=true; emit cancelled(); } e->accept(); }
