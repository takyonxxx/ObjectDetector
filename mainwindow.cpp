#include "mainwindow.h"
#include <QScreen>
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , worker(nullptr)
    , workerThread(nullptr)
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
    worker = new DetectionWorker();
    workerThread = new QThread(this);
    worker->moveToThread(workerThread);

    // Connect signals/slots
    connect(worker, &DetectionWorker::detectionDone,
            this, &MainWindow::handleDetectionResult);
    connect(workerThread, &QThread::finished,
            worker, &QObject::deleteLater);

    // Start the thread
    workerThread->start();
}

void MainWindow::cleanupWorker()
{
    if (workerThread) {
        workerThread->quit();
        workerThread->wait();
        delete worker;
        worker = nullptr;
        workerThread = nullptr;
    }
}

void MainWindow::openImage()
{
    QString appDir = QString(EXPAND(PROJECT_PATH));
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Open Image"),
                                                    appDir,
                                                    tr("Image Files (*.png *.jpg *.bmp);;All Files (*)"));

    if (!fileName.isEmpty()) {
        imagePath = fileName.toStdString();
        currentImage.load(fileName);
        ui->imageLabel->setPixmap(QPixmap::fromImage(currentImage));
    }
}

void MainWindow::setVideoFrame(const QImage &frame)
{
    QImage resizedFrame = frame.scaled(1280, 720, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (resizedFrame.isNull() || resizedFrame.width() == 0 || resizedFrame.height() == 0) {
        qDebug() << "Resized frame is invalid!";
        return;
    }

    if (shouldDetectFace()) {
        // Use QMetaObject::invokeMethod to safely call across threads
        QMetaObject::invokeMethod(worker, "detectObject",
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

bool MainWindow::shouldDetectFace()
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
