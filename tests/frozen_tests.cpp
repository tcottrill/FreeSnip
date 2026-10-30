#include "frozen_desktop.h"
#include <QTest>
#include <QSignalSpy>
class FrozenTests : public QObject {
 Q_OBJECT
private slots:
 void fullscreenDrag() {
  QImage im(800,600,QImage::Format_RGB32); im.fill(Qt::green); im.setPixelColor(200,100,Qt::red);
  FrozenDesktop d(im,{{QRect(0,0,800,600),nullptr}}); QVERIFY(d.valid());
  QSignalSpy chosen(&d,&FrozenDesktop::selected); d.show(); auto* w=d.surfaces().front();
  QVERIFY(w->isFullScreen()); QVERIFY(w->windowFlags().testFlag(Qt::FramelessWindowHint));
  QTest::mousePress(w,Qt::LeftButton,{},QPoint(100,50));
  QTest::mouseRelease(w,Qt::LeftButton,{},QPoint(301,151));
  QCOMPARE(chosen.count(),1); QCOMPARE(qvariant_cast<QImage>(chosen[0][0]),im.copy(100,50,201,101));
  QVERIFY(!w->isVisible());
 }
 void scaledNegativeMonitor() {
  QImage im(3200,1200,QImage::Format_RGB32); im.fill(Qt::blue);
  FrozenDesktop d(im,{{QRect(-800,0,800,600),nullptr},{QRect(0,0,800,600),nullptr}});
  QVERIFY(d.valid()); QSignalSpy chosen(&d,&FrozenDesktop::selected); d.show();
  auto* left=d.surfaces()[0];
  QTest::mousePress(left,Qt::LeftButton,{},QPoint(790,100));
  // Pointer coordinates continue relative to the pressed surface during an
  // implicit Wayland grab, including beyond its edge onto the next output.
  QTest::mouseRelease(left,Qt::LeftButton,{},QPoint(850,160));
  QCOMPARE(chosen.count(),1); QCOMPARE(qvariant_cast<QImage>(chosen[0][0]).size(),QSize(120,120));
 }
 void monitorAndCancel() {
  QImage im(1600,600,QImage::Format_RGB32); im.fill(Qt::red);
  FrozenDesktop d(im,{{QRect(-800,0,800,600),nullptr},{QRect(0,0,800,600),nullptr}});
  QSignalSpy chosen(&d,&FrozenDesktop::selected); d.show(); QTest::keyClick(d.surfaces()[1],Qt::Key_Return);
  QCOMPARE(chosen.count(),1); QCOMPARE(qvariant_cast<QImage>(chosen[0][0]).size(),QSize(800,600));
  FrozenDesktop c(im,{{QRect(0,0,1600,600),nullptr}}); QSignalSpy cancel(&c,&FrozenDesktop::cancelled);
  c.show(); QTest::keyClick(c.surfaces()[0],Qt::Key_Escape); QCOMPARE(cancel.count(),1);
  c.cancel(); QCOMPARE(cancel.count(),1);
 }
 void rejectWrongLayout() {
  FrozenDesktop d(QImage(640,480,QImage::Format_RGB32),{{QRect(0,0,1920,1080),nullptr}});
  QVERIFY(!d.valid());
 }
 void enterUsesPointerOutput() {
  FrozenDesktop d(QImage(1400,600,QImage::Format_RGB32),{{QRect(0,0,800,600),nullptr},{QRect(800,0,600,600),nullptr}});
  QSignalSpy chosen(&d,&FrozenDesktop::selected); d.show();
  QTest::mouseMove(d.surfaces()[1],QPoint(50,50));
  QTest::keyClick(d.surfaces()[0],Qt::Key_Return);
  QCOMPARE(chosen.count(),1); QCOMPARE(qvariant_cast<QImage>(chosen[0][0]).size(),QSize(600,600));
 }
 void rejectResizedSurface() {
  auto* screen=QGuiApplication::primaryScreen();
  FrozenDesktop d(QImage(screen->geometry().size(),QImage::Format_RGB32),{{screen->geometry(),screen}});
  QSignalSpy chosen(&d,&FrozenDesktop::selected), failed(&d,&FrozenDesktop::failed); d.show();
  QTest::qWait(30); auto* w=d.surfaces().first(); w->resize(100,100);
  QTest::mousePress(w,Qt::LeftButton,{},QPoint(10,10)); QTest::mouseRelease(w,Qt::LeftButton,{},QPoint(50,50));
  QCOMPARE(chosen.count(),0); QTRY_COMPARE(failed.count(),1);
 }
};
QTEST_MAIN(FrozenTests)
#include "frozen_tests.moc"
