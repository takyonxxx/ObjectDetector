#ifndef GSTREAMERTSP_H
#define GSTREAMERTSP_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QThreadPool>
#include <QImage>
#include <QSharedPointer>
#include <atomic>
#include <gst/gst.h>
#include <gst/gstpad.h>
#include <gst/app/gstappsink.h>
#include <opencv2/opencv.hpp>

#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)

class GStreamerRtsp : public QThread {
    Q_OBJECT
public:
    explicit GStreamerRtsp(QObject *parent = nullptr);
    ~GStreamerRtsp() override;

    void setUrl(const QString &url);
    QString getUrl() const;
    bool isRunning() const;
    QString name() const;
    QString getInFilename() const;
    quint16 clientPort() const;
    quint16 serverPort() const;
    QString clientIP() const;
    QString serverIP() const;

public slots:
    void stop();
    void startStreamer();

signals:
    void sendVideoFrame(const QImage &frame);
    void sendConnectionStatus(GStreamerRtsp* rtsp, bool status);

protected:
    void run() override;

private:
    bool initialize();
    void cleanup();
    void printParameters();

    QString modifyRtspUrl(const QString& inFilename);
    QImage convertFrameToImage(GstSample *sample);
    void handleFrame(GstSample *sample);

    static GstFlowReturn cb_new_sample(GstElement *sink, gpointer user_data);
    static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);
    static void on_pad_added(GstElement *element, GstPad *pad, gpointer data);

    GstElement *m_pipeline = nullptr;
    GstElement *m_source = nullptr;
    GstElement *m_depay = nullptr;
    GstElement *m_decoder = nullptr;
    GstElement *m_converter = nullptr;
    GstElement *m_videoSink = nullptr;
    GstElement *m_tee = nullptr;

    QString m_inFilename;
    QString m_name;
    QString m_info;
    QString m_outputFormat;
    quint16 m_clientPort = 0, m_serverPort = 0;
    QString m_clientIP, m_serverIP;

    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_isStreaming{false};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_stopUser{false};  

    std::chrono::steady_clock::time_point m_lastFpsUpdateTime;
    int m_frameCount = 0;
    int m_currentFps = 0;
    int m_width = 0;
    int m_height = 0;
    int m_fps = 0;

    QMutex m_mutex;  
    std::atomic<bool> m_parametersDetected{false};

    QQueue<cv::Mat> m_frameQueue;
    QMutex m_queueMutex;
    QWaitCondition m_queueCondition;
};

#endif // GSTREAMERTSP_H
