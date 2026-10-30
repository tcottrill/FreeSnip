#pragma once
#include <QImage>
#include <QMimeData>
#include <QRectF>
#include <memory>
namespace snip {
QRect imageSelection(QPointF start, QPointF end, QRectF displayed, QSize pixels);
QByteArray pngBytes(const QImage& image);
std::unique_ptr<QMimeData> imageMime(const QImage& image);
}
