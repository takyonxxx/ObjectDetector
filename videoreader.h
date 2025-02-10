// videoreader.h
#ifndef VIDEOREADER_H
#define VIDEOREADER_H

#include <QObject>
#include <QThread>
#include <QImage>
#include <atomic>
#include <opencv2/opencv.hpp>

class VideoReader : public QObject
{
    Q_OBJECT

public:
    explicit VideoReader(QObject *parent = nullptr);
    ~VideoReader();

public slots:
    void startReading(const QString &filePath);
    void stopReading();

signals:
    void frameReady(const QImage &frame);
    void finished();

private:
     std::atomic<bool> m_stop;
};

#endif // VIDEOREADER_H
