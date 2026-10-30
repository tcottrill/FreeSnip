#include "selection.h"
#include <QTest>
#include <QSignalSpy>
class SelectionTests : public QObject {
 Q_OBJECT
private slots:
 void drag() {
  QImage im(400,200,QImage::Format_RGB32); im.fill(Qt::red);
  Selection s(im); s.resize(200,100); s.show();
  QSignalSpy selected(&s,&Selection::selected);
  QTest::mousePress(&s,Qt::LeftButton,{},QPoint(10,10));
  QTest::mouseRelease(&s,Qt::LeftButton,{},QPoint(110,60));
  QCOMPARE(selected.count(),1);
  QCOMPARE(qvariant_cast<QImage>(selected[0][0]).size(),QSize(200,100));
 }
 void cancel() {
  Selection s(QImage(400,200,QImage::Format_RGB32)); s.show();
  QSignalSpy cancelled(&s,&Selection::cancelled), selected(&s,&Selection::selected);
  QTest::keyClick(&s,Qt::Key_Escape); QCOMPARE(cancelled.count(),1); QCOMPARE(selected.count(),0);
 }
 void whole() {
  Selection s(QImage(400,200,QImage::Format_RGB32)); s.show();
  QSignalSpy selected(&s,&Selection::selected); QTest::keyClick(&s,Qt::Key_Return);
  QCOMPARE(selected.count(),1); QCOMPARE(qvariant_cast<QImage>(selected[0][0]).size(),QSize(400,200));
 }
 void monitor() {
  Selection s(QImage(400,200,QImage::Format_RGB32)); s.resize(400,200);
  s.setMonitorRects({QRect(0,0,150,200),QRect(150,0,250,200)}); s.show();
  QCursor::setPos(s.mapToGlobal(QPoint(250,50)));
  QSignalSpy selected(&s,&Selection::selected); QTest::keyClick(&s,Qt::Key_Return);
  QCOMPARE(selected.count(),1); QCOMPARE(qvariant_cast<QImage>(selected[0][0]).size(),QSize(250,200));
 }
};
QTEST_MAIN(SelectionTests)
#include "selection_tests.moc"
