#ifndef DETECTIONWORKER_H
#define DETECTIONWORKER_H

#include <QObject>
#include <QImage>
#include <QElapsedTimer>
#include <QThread>

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

class DetectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit DetectionWorker(QObject *parent = nullptr);

public slots:
    void detectObject(const QImage &qImage);

signals:
    void detectionDone(const QImage &result);

private:
    cv::dnn::Net net;  // YOLOv4 Model
    cv::VideoCapture cap;
    QString extractResource(const QString &resourcePath);
    std::vector<std::string> getOutputsNames(const cv::dnn::Net &net);
    cv::Mat QImageToCvMat(const QImage& qImage);
    QImage QImageFromCvMat(const cv::Mat& mat);

    QElapsedTimer fpsTimer;
    float fps;
    int frameCount;
};

#endif // DETECTIONWORKER_H
