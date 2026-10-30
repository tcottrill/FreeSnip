#pragma once
#include <QObject>
#include <QImage>
#include <QPointer>
#include <QScreen>
#include <QWidget>

// All surfaces share selection coordinates in the logical desktop. No surface
// uses compositor-controlled global window placement to locate its pixels.
class FrozenDesktop : public QObject {
 Q_OBJECT
public:
 struct Output { QRect geometry; QPointer<QScreen> screen; };
 FrozenDesktop(QImage image, QList<Output> outputs, QObject* parent=nullptr);
 ~FrozenDesktop();
 bool valid() const;
 void show();
 void cancel();
 QList<QWidget*> surfaces() const;
signals:
 void selected(QImage image);
 void cancelled();
 void failed(QString message);
private:
 class Surface;
 friend class Surface;
 QImage image_; QList<Output> outputs_; QRect bounds_;
 QList<Surface*> surfaces_; QPointF a_, b_;
 QPointF pointer_; bool havePointer_=false;
 bool dragging_=false, finished_=false, ready_=false;
 QRect pixels(QRectF logical) const;
 void redraw();
 void finish(QRect pixels);
 void dismiss();
 bool validateSurfaces();
};
