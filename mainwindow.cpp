#include "mainwindow.h"
#include <QScreen>
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , workerDetection(nullptr)
    , workerDetectionThread(nullptr)
    , videoReader(nullptr)
    , videoThread(nullptr)
{
    ui->setupUi(this);

    setWindowTitle("Object Detection");

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui->imageLabel->setMaximumWidth(1920);
    ui->imageLabel->setMaximumHeight(1080);
    this->resize(1280, 720);

    // Initialize RTSP stream
    rtspStream = QSharedPointer<GStreamerRtsp>::create(this);
    connect(rtspStream.data(), &GStreamerRtsp::sendVideoFrame,
            this, &MainWindow::setVideoFrame);

    // Initialize worker and thread
    initializeWorker();

    QScreen* screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    int x = (screenGeometry.width() - this->width()) / 2;
    int y = (screenGeometry.height() - this->height()) / 2;
    move(x, y);
}

MainWindow::~MainWindow()
{
    cleanupWorker();   
    delete ui;
}

void MainWindow::initializeWorker()
{
    // Create worker and thread
    workerDetection = new DetectionWorker();
    workerDetectionThread = new QThread(this);
    workerDetection->moveToThread(workerDetectionThread);

    // Connect signals/slots
    connect(workerDetection, &DetectionWorker::detectionDone,
            this, &MainWindow::handleDetectionResult);
    connect(workerDetectionThread, &QThread::finished,
            workerDetection, &QObject::deleteLater);

    // Start the thread
    workerDetectionThread->start();

    videoThread = new QThread(this);
    videoReader = new VideoReader();
    videoReader->moveToThread(videoThread);

    connect(videoReader, &VideoReader::frameReady, this, &MainWindow::setVideoFrame);
    connect(videoReader, &VideoReader::finished, this, &MainWindow::handleVideoFinished);
    connect(videoThread, &QThread::finished, videoReader, &QObject::deleteLater);

    videoThread->start();
}

void MainWindow::cleanupWorker()
{
    if (workerDetectionThread) {
        workerDetectionThread->quit();
        workerDetectionThread->wait();
        delete workerDetection;
        workerDetection = nullptr;
        workerDetectionThread = nullptr;
    }

    if (videoThread) {
        videoThread->quit();
        videoThread->wait();
    }
}

void MainWindow::openFile()
{
    QString appDir = QString(EXPAND(PROJECT_PATH));
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Open File"),
                                                    appDir,
                                                    "Video Files (*.mp4 *.avi *.mkv);;All Files (*)");

    if (!fileName.isEmpty() && videoReader) {
        ui->lineUrl->setText(fileName);
        QMetaObject::invokeMethod(videoReader, "startReading", Qt::QueuedConnection, Q_ARG(QString, fileName));
    }
}

void MainWindow::setVideoFrame(const QImage &frame)
{
    QImage resizedFrame = frame.scaled(1280, 720, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (resizedFrame.isNull() || resizedFrame.width() == 0 || resizedFrame.height() == 0) {
        qDebug() << "Resized frame is invalid!";
        return;
    }

    if (shouldDetectObject()) {
        // Use QMetaObject::invokeMethod to safely call across threads
        QMetaObject::invokeMethod(workerDetection, "detectObject",
                                  Qt::QueuedConnection,
                                  Q_ARG(QImage, resizedFrame));
    } /*else {
        // Update UI directly if no detection needed
        QPixmap pixmap = QPixmap::fromImage(resizedFrame);
        ui->imageLabel->setPixmap(pixmap);
        ui->imageLabel->setScaledContents(true);
    }*/
}

void MainWindow::handleDetectionResult(const QImage &result)
{
    QPixmap pixmap = QPixmap::fromImage(result);
    ui->imageLabel->setPixmap(pixmap);
    ui->imageLabel->setScaledContents(true);
}

void MainWindow::handleVideoFinished() {
    qDebug() << "Video playback finished.";
    ui->openButton->setText("Open File");
    ui->lineUrl->setText("rtsp://192.168.1.249:554/stream1");
}

void MainWindow::updateImageLabel(const QImage &processedImage)
{
    QPixmap pixmap = QPixmap::fromImage(processedImage);
    ui->imageLabel->setPixmap(pixmap);
    ui->imageLabel->setScaledContents(true);
}

void MainWindow::handleError(const QString &errorMessage)
{
    qDebug() << "Error:" << errorMessage;
    QMessageBox::critical(this, tr("Error"), errorMessage);
}

bool MainWindow::shouldDetectObject()
{
    static int frameCounter = 0;
    frameCounter++;
    return (frameCounter % 2 == 0); // half frame 24 / 2 = 12
}

void MainWindow::on_playButton_clicked()
{
    auto cameraUrl = ui->lineUrl->text();

    if (ui->playButton->text() == "Play") {
        if (rtspStream && !rtspStream->isRunning()) {
            rtspStream->setUrl(cameraUrl);
            rtspStream->start();
        } else {
            qWarning() << "RTSP stream object is null for camera:" << cameraUrl;
        }
        ui->playButton->setText("Stop");
    } else {
        if (rtspStream && rtspStream->isRunning()) {
            rtspStream->stop();
            if (!rtspStream->wait(1000)) {
                rtspStream->terminate();
                rtspStream->wait();
            }
        }
        ui->playButton->setText("Play");
    }
}

void MainWindow::on_openButton_clicked() {
    if (ui->openButton->text() == "Open File") {
        openFile();
        ui->openButton->setText("Stop");
    } else {
        videoReader->stopReading();
        ui->lineUrl->setText("rtsp://192.168.1.249:554/stream1");
        ui->openButton->setText("Open File");
    }
}
