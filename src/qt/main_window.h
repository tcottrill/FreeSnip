#pragma once
#include "portal.h"
#include "shortcuts.h"
#include "frozen_desktop.h"
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>
class QLabel;
class QAction;
class Selection;
class MainWindow : public QMainWindow {
 Q_OBJECT
public:
 explicit MainWindow(bool smoke=false, QString portalService="org.freedesktop.portal.Desktop");
 void newSnip();
 void startCapture(CaptureMode mode);
 void showImage(QImage image);
 void showPreview();
protected:
 void closeEvent(QCloseEvent*) override;
 void resizeEvent(QResizeEvent*) override;
private:
 QImage image_; QLabel* preview_; QAction *copy_, *save_, *fresh_, *cancel_;
 QSystemTrayIcon tray_; PortalCapture portal_; GlobalShortcut shortcut_; QPointer<Selection> selection_;
 QPointer<FrozenDesktop> frozen_;
 QList<FrozenDesktop::Output> captureOutputs_;
 CaptureMode mode_=CaptureMode::Rectangle;
 bool pending_=false; bool smoke_=false; quint64 captureGeneration_=0;
 void select(QImage image, QRect desktop={});
 void capture();
 void copyImage();
 void saveImage();
 void updatePreview();
 void restore();
 void freezeDesktop(QImage image);
};
