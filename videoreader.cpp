// videoreader.cpp
#include "videoreader.h"
#include <QDebug>

VideoReader::VideoReader(QObject *parent) : QObject(parent), m_stop(false) {}

VideoReader::~VideoReader() {
    stopReading();
}

void VideoReader::startReading(const QString &filePath) {
    m_stop = false;

    cv::VideoCapture videoCapture(filePath.toStdString());
    if (!videoCapture.isOpened()) {
        qDebug() << "Failed to open video file:" << filePath;
        emit finished();
        return;
    }

    cv::Mat frame;
    while (videoCapture.read(frame) && !m_stop) {
        // Convert OpenCV Mat to QImage
        QImage qImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888);

        // Emit the frame to the main thread
        emit frameReady(qImage);

        // Add a delay to control the frame rate (e.g., 30 FPS)
        QThread::msleep(33); // 1000 ms / 30 FPS ≈ 33 ms per frame
    }

    videoCapture.release();
    emit finished();
}

void VideoReader::stopReading() {
    m_stop = true; // Set the stop flag to true
}
