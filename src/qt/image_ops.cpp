#include "image_ops.h"
#include <QBuffer>
#include <cmath>
namespace snip {
QRect imageSelection(QPointF a, QPointF b, QRectF displayed, QSize pixels) {
 if (displayed.isEmpty() || pixels.isEmpty()) return {};
 const QRectF r = QRectF(a,b).normalized().intersected(displayed);
 if (r.isEmpty()) return {};
 const double sx = pixels.width()/displayed.width(), sy = pixels.height()/displayed.height();
 const int x = int(std::floor((r.left()-displayed.left())*sx));
 const int y = int(std::floor((r.top()-displayed.top())*sy));
 const int right = int(std::ceil((r.right()-displayed.left())*sx));
 const int bottom = int(std::ceil((r.bottom()-displayed.top())*sy));
 return QRect(x,y,right-x,bottom-y).intersected(QRect(QPoint(),pixels));
}
QByteArray pngBytes(const QImage& image) {
 QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
 if (!image.save(&buffer,"PNG")) return {};
 return bytes;
}
std::unique_ptr<QMimeData> imageMime(const QImage& image) {
 if (image.isNull()) return {};
 auto bytes = pngBytes(image); if (bytes.isEmpty()) return {};
 auto mime = std::make_unique<QMimeData>();
 mime->setImageData(image); mime->setData("image/png",bytes); return mime;
}
}
