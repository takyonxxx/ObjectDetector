#include "detectionworker.h"
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QTemporaryFile>
#include <QThread>

DetectionWorker::DetectionWorker(QObject *parent)
    : QObject(parent), fps(0.0f), frameCount(0), skipFrameCounter(0) {

    fpsTimer.start();
    QString modelPath = extractResource(":/models/yolov4-tiny.weights");
    QString configPath = extractResource(":/models/yolov4-tiny.cfg");

    if (modelPath.isEmpty() || configPath.isEmpty()) {
        qDebug() << "Error extracting YOLO model files.";
        return;
    }

    net = cv::dnn::readNet(modelPath.toStdString(), configPath.toStdString());

    if (net.empty()) {
        qDebug() << "Failed to load YOLOv4-Tiny!";
        return;
    }

    // Set backend and target for acceleration
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    // For GPU acceleration (if available):
    // net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    // net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);

    // Pre-allocate blob to avoid repeated allocations
    blob = cv::Mat();

    // Load class names once during initialization
    loadClassNames();

    // Pre-allocate detection vectors
    classIds.reserve(100);
    confidences.reserve(100);
    boxes.reserve(100);
    indices.reserve(100);
    detectionOutputs.reserve(3);

    // Cache output names
    outputNames = getOutputsNames(net);
}

void DetectionWorker::detectObject(const QImage &qImage) {
    if (net.empty()) {
        qDebug() << "Error: YOLOv4-Tiny model is not loaded!";
        return;
    }

    // Skip frames for performance (process every 2nd or 3rd frame)
    if (++skipFrameCounter % FRAME_SKIP != 0) {
        emit detectionDone(qImage); // Return original image
        return;
    }

    frameCount++;
    float elapsed = fpsTimer.elapsed() / 1000.0f;
    if (elapsed >= 1.0f) {
        fps = frameCount / elapsed;
        frameCount = 0;
        fpsTimer.restart();
    }

    QElapsedTimer frameTimer;
    frameTimer.start();

    // Convert QImage to cv::Mat more efficiently
    cv::Mat frame = qImageToCvMat(qImage);

    // Resize input if too large (major performance boost)
    cv::Mat processFrame;
    if (frame.cols > MAX_PROCESSING_WIDTH) {
        double scale = static_cast<double>(MAX_PROCESSING_WIDTH) / frame.cols;
        cv::resize(frame, processFrame, cv::Size(), scale, scale, cv::INTER_LINEAR);
        scaleFactor = 1.0 / scale;
    } else {
        processFrame = frame;
        scaleFactor = 1.0;
    }

    // Prepare input blob (reuse existing blob memory)
    cv::dnn::blobFromImage(processFrame, blob, 1.0/255.0, cv::Size(INPUT_SIZE, INPUT_SIZE),
                           cv::Scalar(0, 0, 0), true, false, CV_32F);
    net.setInput(blob);

    // Forward pass
    detectionOutputs.clear();
    net.forward(detectionOutputs, outputNames);

    // Clear vectors instead of recreating
    classIds.clear();
    confidences.clear();
    boxes.clear();

    // Process detections with early termination
    processDetections(processFrame);

    // Apply NMS
    indices.clear();
    if (!boxes.empty()) {
        cv::dnn::NMSBoxes(boxes, confidences, CONFIDENCE_THRESHOLD, NMS_THRESHOLD, indices);
    }

    // Draw detections on original frame
    drawDetections(frame);

    double processingTime = frameTimer.elapsed() / 1000.0;

    // Draw performance info
    drawPerformanceInfo(frame, processingTime);

    // Convert and emit result
    QImage processedImage = cvMatToQImage(frame);
    emit detectionDone(processedImage);
}

cv::Mat DetectionWorker::qImageToCvMat(const QImage& qImage) {
    // More efficient conversion without format change if possible
    if (qImage.format() == QImage::Format_RGB888) {
        cv::Mat mat(qImage.height(), qImage.width(), CV_8UC3,
                    const_cast<uchar*>(qImage.bits()), qImage.bytesPerLine());
        cv::Mat result;
        cv::cvtColor(mat, result, cv::COLOR_RGB2BGR);
        return result;
    } else {
        QImage convertedImage = qImage.convertToFormat(QImage::Format_RGB888);
        cv::Mat mat(convertedImage.height(), convertedImage.width(), CV_8UC3,
                    const_cast<uchar*>(convertedImage.bits()), convertedImage.bytesPerLine());
        cv::Mat result;
        cv::cvtColor(mat, result, cv::COLOR_RGB2BGR);
        return result;
    }
}

QImage DetectionWorker::cvMatToQImage(const cv::Mat& mat) {
    // More efficient conversion
    if (mat.type() == CV_8UC3) {
        cv::Mat rgbMat;
        cv::cvtColor(mat, rgbMat, cv::COLOR_BGR2RGB);
        return QImage(rgbMat.data, rgbMat.cols, rgbMat.rows,
                      rgbMat.step, QImage::Format_RGB888).copy();
    }
    return QImage();
}

void DetectionWorker::processDetections(const cv::Mat& frame) {
    for (const auto& output : detectionOutputs) {
        const float* data = reinterpret_cast<const float*>(output.data);

        for (int i = 0; i < output.rows; i++) {
            const float* detection = data + i * output.cols;

            // Quick confidence check before expensive operations
            float maxScore = 0.0f;
            int maxIndex = 0;
            for (int j = 5; j < output.cols; j++) {
                if (detection[j] > maxScore) {
                    maxScore = detection[j];
                    maxIndex = j - 5;
                }
            }

            if (maxScore > CONFIDENCE_THRESHOLD) {
                float centerX = detection[0] * frame.cols * scaleFactor;
                float centerY = detection[1] * frame.rows * scaleFactor;
                float width = detection[2] * frame.cols * scaleFactor;
                float height = detection[3] * frame.rows * scaleFactor;

                int left = static_cast<int>(centerX - width / 2);
                int top = static_cast<int>(centerY - height / 2);

                classIds.push_back(maxIndex);
                confidences.push_back(maxScore);
                boxes.push_back(cv::Rect(left, top, static_cast<int>(width), static_cast<int>(height)));
            }
        }
    }
}

void DetectionWorker::drawDetections(cv::Mat& frame) {
    // Pre-calculate common values
    static const std::vector<cv::Scalar> colors = {
        cv::Scalar(255, 0, 0),   // Blue
        cv::Scalar(0, 255, 0),   // Green
        cv::Scalar(0, 0, 255),   // Red
        cv::Scalar(255, 255, 0), // Cyan
        cv::Scalar(255, 0, 255)  // Magenta
    };

    for (size_t i = 0; i < indices.size(); ++i) {
        int idx = indices[i];
        const cv::Rect& box = boxes[idx];
        int classId = classIds[idx];
        float conf = confidences[idx];

        if (classId >= 0 && classId < static_cast<int>(classNames.size())) {
            cv::Scalar color = colors[classId % colors.size()];

            // Draw box
            cv::rectangle(frame, box, color, 2);

            // Calculate distance (simplified)
            float pixelWidth = static_cast<float>(box.width);
            float distanceToObject = (KNOWN_WIDTH * FOCAL_LENGTH) / pixelWidth;

            // Create label
            std::string label = classNames[classId] + ": " +
                                std::to_string(static_cast<int>(conf * 100)) + "% " +
                                "dist: " + std::to_string(distanceToObject).substr(0, 4) + "m";

            // Draw label with background
            int baseLine;
            cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            int labelTop = std::max(box.y, labelSize.height);

            cv::rectangle(frame,
                          cv::Point(box.x, labelTop - labelSize.height - 10),
                          cv::Point(box.x + labelSize.width, labelTop + baseLine - 10),
                          color, cv::FILLED);

            cv::putText(frame, label,
                        cv::Point(box.x, labelTop - 10),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        }
    }
}

void DetectionWorker::drawPerformanceInfo(cv::Mat& frame, double processingTime) {
    // Pre-format strings to avoid repeated string operations
    static std::string fpsText, timeText, dimensionsText;

    fpsText = "FPS: " + std::to_string(static_cast<int>(fps));
    timeText = "Time: " + std::to_string(processingTime).substr(0, 5) + "s";
    dimensionsText = "Size: " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows);

    // Draw background
    cv::rectangle(frame, cv::Point(10, 10), cv::Point(160, 80), cv::Scalar(0, 0, 0), cv::FILLED);

    // Draw text
    cv::putText(frame, fpsText, cv::Point(15, 30), cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 255), 2);
    cv::putText(frame, timeText, cv::Point(15, 50), cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 255), 2);
    cv::putText(frame, dimensionsText, cv::Point(15, 70), cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 255), 2);
}

void DetectionWorker::loadClassNames() {
    QString namesPath = extractResource(":/models/coco.names");
    QFile file(namesPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        classNames.reserve(80); // COCO has 80 classes
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty()) {
                classNames.push_back(line.toStdString());
            }
        }
        file.close();
    } else {
        qDebug() << "Failed to load class names from" << namesPath;
    }
}

QString DetectionWorker::extractResource(const QString &resourcePath) {
    QFile file(resourcePath);
    if (!file.exists()) {
        qDebug() << "Resource does not exist: " << resourcePath;
        return QString();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open resource file: " << resourcePath;
        return QString();
    }

    QString tempPath = QDir::tempPath() + "/" + QFileInfo(resourcePath).fileName();
    QFile extractedFile(tempPath);

    if (extractedFile.exists()) {
        return tempPath;
    }

    if (!extractedFile.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to create temp file: " << tempPath;
        return QString();
    }

    extractedFile.write(file.readAll());
    extractedFile.close();
    return tempPath;
}

std::vector<std::string> DetectionWorker::getOutputsNames(const cv::dnn::Net &net) {
    std::vector<int> outLayers = net.getUnconnectedOutLayers();
    std::vector<cv::String> layerNames = net.getLayerNames();
    std::vector<std::string> names;
    names.reserve(outLayers.size());

    for (size_t i = 0; i < outLayers.size(); ++i) {
        names.push_back(layerNames[outLayers[i] - 1]);
    }
    return names;
}
