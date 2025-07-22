#ifndef DETECTIONWORKER_H
#define DETECTIONWORKER_H

#include <QObject>
#include <QImage>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

class DetectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit DetectionWorker(QObject *parent = nullptr);

    // Performance tuning constants
    static constexpr int FRAME_SKIP = 2;              // Process every 2nd frame
    static constexpr int MAX_PROCESSING_WIDTH = 640;   // Max width for processing
    static constexpr int INPUT_SIZE = 416;             // YOLO input size
    static constexpr float CONFIDENCE_THRESHOLD = 0.5f;
    static constexpr float NMS_THRESHOLD = 0.4f;
    static constexpr float KNOWN_WIDTH = 0.60f;        // Average width of a person in meters
    static constexpr float FOCAL_LENGTH = 615.0f;      // Focal length (needs calibration)

public slots:
    void detectObject(const QImage &qImage);

signals:
    void detectionDone(const QImage &result);

private:
    // Core detection components
    cv::dnn::Net net;
    std::vector<std::string> classNames;
    std::vector<std::string> outputNames;

    // Pre-allocated memory for performance
    cv::Mat blob;
    std::vector<cv::Mat> detectionOutputs;
    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;
    std::vector<int> indices;

    // Performance tracking
    QElapsedTimer fpsTimer;
    float fps;
    int frameCount;
    int skipFrameCounter;
    double scaleFactor;

    // Helper methods
    cv::Mat qImageToCvMat(const QImage& qImage);
    QImage cvMatToQImage(const cv::Mat& mat);
    void processDetections(const cv::Mat& frame);
    void drawDetections(cv::Mat& frame);
    void drawPerformanceInfo(cv::Mat& frame, double processingTime);
    void loadClassNames();
    QString extractResource(const QString &resourcePath);
    std::vector<std::string> getOutputsNames(const cv::dnn::Net &net);
};

#endif // DETECTIONWORKER_H
