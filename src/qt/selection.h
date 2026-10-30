#pragma once
#include <QWidget>
#include <QImage>
class Selection : public QWidget {
 Q_OBJECT
public:
 explicit Selection(QImage image, QWidget* parent = nullptr);
 QRectF imageRect() const;
 void setMonitorRects(QList<QRect> rects) { monitors_=std::move(rects); }
signals:
 void selected(QImage image);
 void cancelled();
protected:
 void paintEvent(QPaintEvent*) override;
 void mousePressEvent(QMouseEvent*) override;
 void mouseMoveEvent(QMouseEvent*) override;
 void mouseReleaseEvent(QMouseEvent*) override;
 void keyPressEvent(QKeyEvent*) override;
 void closeEvent(QCloseEvent*) override;
private:
 QImage image_; QPointF a_, b_; bool dragging_ = false, finished_ = false;
 QList<QRect> monitors_;
 void finish(QRect pixels);
};
