#include "detectionworker.h"
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QTemporaryFile>

DetectionWorker::DetectionWorker(QObject *parent) : QObject(parent), fps(0.0f), frameCount(0) {

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
    }
}

void DetectionWorker::detectObject(const QImage &qImage) {
    if (net.empty()) {
        qDebug() << "Error: YOLOv4-Tiny model is not loaded!";
        return;
    }

    frameCount++;
    float elapsed = fpsTimer.elapsed() / 1000.0f;
    if (elapsed >= 1.0f) {  // Update FPS every second
        fps = frameCount / elapsed;
        frameCount = 0;
        fpsTimer.restart();
    }

    // Convert QImage to cv::Mat
    cv::Mat frame;
    QImage convertedImage = qImage.convertToFormat(QImage::Format_RGB888);
    cv::cvtColor(QImageToCvMat(convertedImage), frame, cv::COLOR_RGB2BGR);

    QElapsedTimer frameTimer;
    frameTimer.start();

    // Prepare input blob
    cv::Mat blob;
    cv::dnn::blobFromImage(frame, blob, 1/255.0, cv::Size(416, 416),
                           cv::Scalar(0, 0, 0), true, false);
    net.setInput(blob);

    // Forward pass
    std::vector<cv::String> outputNames = getOutputsNames(net);
    std::vector<cv::Mat> detectionOutputs;
    net.forward(detectionOutputs, outputNames);

    // Process detections
    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;
    const float CONFIDENCE_THRESHOLD = 0.5f;
    const float NMS_THRESHOLD = 0.4f;

    // Constants for distance calculation
    const float KNOWN_WIDTH = 0.60f;  // Average width of a person in meters
    const float FOCAL_LENGTH = 615.0f;  // Focal length (needs calibration)

    // Load class names from resource file
    static std::vector<std::string> classNames;
    if (classNames.empty()) {
        QString namesPath = extractResource(":/models/coco.names");
        QFile file(namesPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (!line.isEmpty()) {
                    classNames.push_back(line.toStdString());
                }
            }
            file.close();
        } else {
            qDebug() << "Failed to load class names from" << namesPath;
            return;
        }
    }

    for (const auto& output : detectionOutputs) {
        for (int i = 0; i < output.rows; i++) {
            cv::Mat detection = output.row(i);
            cv::Mat scores = detection.colRange(5, detection.cols);
            cv::Point classIdPoint;
            double confidence;

            cv::minMaxLoc(scores, 0, &confidence, 0, &classIdPoint);

            if (confidence > CONFIDENCE_THRESHOLD) {
                int centerX = static_cast<int>(detection.at<float>(0) * frame.cols);
                int centerY = static_cast<int>(detection.at<float>(1) * frame.rows);
                int width = static_cast<int>(detection.at<float>(2) * frame.cols);
                int height = static_cast<int>(detection.at<float>(3) * frame.rows);

                int left = centerX - width / 2;
                int top = centerY - height / 2;

                classIds.push_back(classIdPoint.x);
                confidences.push_back(static_cast<float>(confidence));
                boxes.push_back(cv::Rect(left, top, width, height));
            }
        }
    }

    // Apply NMS
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, CONFIDENCE_THRESHOLD, NMS_THRESHOLD, indices);

    // Define colors
    std::vector<cv::Scalar> colors = {
        cv::Scalar(255, 0, 0),   // Blue
        cv::Scalar(0, 255, 0),   // Green
        cv::Scalar(0, 0, 255),   // Red
        cv::Scalar(255, 255, 0), // Cyan
        cv::Scalar(255, 0, 255)  // Magenta
    };

    // Draw detections
    for (size_t i = 0; i < indices.size(); ++i) {
        int idx = indices[i];
        cv::Rect box = boxes[idx];
        int classId = classIds[idx];
        float conf = confidences[idx];

        // Ensure classId is within bounds
        if (classId >= 0 && classId < classNames.size()) {
            // Get color
            cv::Scalar color = colors[classId % colors.size()];

            // Draw box
            cv::rectangle(frame, box, color, 2);

            // Calculate width and distance
            float pixelWidth = static_cast<float>(box.width);
            float distanceToObject = 0.0f;

            // Calculate distance for person class
            //if (classNames[classId] == "person") {
                distanceToObject = (KNOWN_WIDTH * FOCAL_LENGTH) / pixelWidth;
            //}

            // Create label with measurements
            std::ostringstream labelStream;
            labelStream << classNames[classId] << ": "
                        << static_cast<int>(conf * 100) << "% "
                       /* << "Width: " << std::fixed << std::setprecision(2)
                        << pixelWidth << "px"*/;

            if (distanceToObject > 0.0f) {
                labelStream << "dist: " << std::fixed << std::setprecision(2)
                            << distanceToObject << "m";
            }
            std::string label = labelStream.str();

            // Calculate label dimensions
            int baseLine;
            cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                                 0.5, 1, &baseLine);
            int labelTop = std::max(box.y, labelSize.height);

            // Draw label background
            cv::rectangle(frame,
                          cv::Point(box.x, labelTop - labelSize.height - 10),
                          cv::Point(box.x + labelSize.width, labelTop + baseLine - 10),
                          color, cv::FILLED);

            // Draw label
            cv::putText(frame, label,
                        cv::Point(box.x, labelTop - 10),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);

            // Log detection with measurements
            // qDebug() << "Detected" << QString::fromStdString(classNames[classId])
            //          << "with confidence:" << conf
            //          << "at position:" << box.x << box.y
            //          << "width:" << pixelWidth << "px"
            //          << "distance:" << distanceToObject << "m";
        }
    }

    double processingTime = frameTimer.elapsed() / 1000.0;
    int frameWidth = frame.cols;
    int frameHeight = frame.rows;

    // Draw FPS and processing time
    std::string fpsText = "FPS: " + std::to_string(static_cast<int>(fps));
    std::ostringstream timeStream;
    timeStream << std::fixed << std::setprecision(3) << processingTime;
    std::string timeText = "Time: " + timeStream.str() + "s";
    std::string dimensionsText = "Size: " + std::to_string(frameWidth) + "x" + std::to_string(frameHeight);

    // Draw background for FPS text
    cv::rectangle(frame,
                  cv::Point(10, 10),
                  cv::Point(160, 80),
                  cv::Scalar(0, 0, 0),
                  cv::FILLED);

    // Draw FPS and time text
    cv::putText(frame, fpsText,
                cv::Point(15, 30),
                cv::FONT_HERSHEY_SIMPLEX,
                0.55, cv::Scalar(0, 255, 255), 2);

    cv::putText(frame, timeText,
                cv::Point(15, 50),
                cv::FONT_HERSHEY_SIMPLEX,
                0.55, cv::Scalar(0, 255, 255), 2);

    cv::putText(frame, dimensionsText,
                cv::Point(15, 70),
                cv::FONT_HERSHEY_SIMPLEX,
                0.55, cv::Scalar(0, 255, 255), 2);

    // Convert and emit result
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    QImage processedImage = QImageFromCvMat(frame);
    emit detectionDone(processedImage);
}

cv::Mat DetectionWorker::QImageToCvMat(const QImage& qImage) {
    return cv::Mat(qImage.height(), qImage.width(), CV_8UC3,
                   const_cast<uchar*>(qImage.bits()), qImage.bytesPerLine());
}

QImage DetectionWorker::QImageFromCvMat(const cv::Mat& mat) {
    return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888).copy();
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

    // Extract to a known temp directory
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
    static std::vector<std::string> names;
    if (names.empty()) {
        std::vector<int> outLayers = net.getUnconnectedOutLayers();
        std::vector<cv::String> layerNames = net.getLayerNames();
        names.resize(outLayers.size());
        for (size_t i = 0; i < outLayers.size(); ++i) {
            names[i] = layerNames[outLayers[i] - 1];
        }
    }
    return names;
}

