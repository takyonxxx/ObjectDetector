#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSharedPointer>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include "gstreamerrtsp.h"
#include "detectionworker.h"
#include "videoreader.h"

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
    void openFile();
    void setVideoFrame(const QImage &frame);
    void updateImageLabel(const QImage &processedImage);
    void handleError(const QString &errorMessage);
    void handleDetectionResult(const QImage &result);
    void handleVideoFinished();
    void on_playButton_clicked();    
    void on_openButton_clicked();

private:
    bool shouldDetectObject();
    void initializeWorker();
    void cleanupWorker();

private:
    Ui::MainWindow *ui;
    QSharedPointer<GStreamerRtsp> rtspStream;
    DetectionWorker *workerDetection;
    QThread *workerDetectionThread;
    VideoReader *videoReader;
    QThread *videoThread;
};

#endif // MAINWINDOW_H
