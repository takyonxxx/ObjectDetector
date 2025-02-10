#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSharedPointer>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include "gstreamerrtsp.h"
#include "detectionworker.h"

#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void setVideoFrame(const QImage &frame);
    void updateImageLabel(const QImage &processedImage);
    void handleError(const QString &errorMessage);
    void on_playButton_clicked();
    void handleDetectionResult(const QImage &result);

private:
    bool shouldDetectFace();
    void openImage();
    void initializeWorker();
    void cleanupWorker();

private:
    Ui::MainWindow *ui;
    QImage currentImage;
    std::string imagePath;
    QSharedPointer<GStreamerRtsp> rtspStream;
    DetectionWorker *worker;
    QThread *workerThread;
};

#endif // MAINWINDOW_H
