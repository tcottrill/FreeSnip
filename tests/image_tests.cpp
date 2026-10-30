#include "image_ops.h"
#include <QTest>
#include <QClipboard>
#include <QApplication>
class ImageTests : public QObject {
 Q_OBJECT
private slots:
 void mapping() {
  QCOMPARE(snip::imageSelection({20,30},{70,55},{10,5,100,50},{400,200}), QRect(40,100,200,100));
 }
 void reverse() {
  QCOMPARE(snip::imageSelection({70,55},{20,30},{10,5,100,50},{400,200}), QRect(40,100,200,100));
 }
 void clipped() {
  QCOMPARE(snip::imageSelection({-50,-50},{500,500},{10,5,100,50},{400,200}), QRect(0,0,400,200));
  QVERIFY(snip::imageSelection({-50,-50},{0,0},{10,5,100,50},{400,200}).isEmpty());
 }
 void degenerate() {
  QVERIFY(snip::imageSelection({20,30},{20,30},{10,5,100,50},{400,200}).isEmpty());
  QVERIFY(snip::imageSelection({0,0},{100,100},{},{400,200}).isEmpty());
 }
 void pngAndClipboard() {
  QImage im(301,151,QImage::Format_RGB32); im.fill(Qt::red); im.setPixelColor(300,150,Qt::blue);
  auto png = snip::pngBytes(im);
  QCOMPARE(QImage::fromData(png,"PNG"),im);
  auto mime = snip::imageMime(im); QVERIFY(mime); QVERIFY(mime->hasImage());
  QCOMPARE(mime->data("image/png"),png);
  QApplication::clipboard()->setMimeData(mime.release());
  QCOMPARE(QApplication::clipboard()->image(),im);
  QVERIFY(!snip::imageMime({}));
 }
};
QTEST_MAIN(ImageTests)
#include "image_tests.moc"
