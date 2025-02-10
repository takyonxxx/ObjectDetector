#include "gstreamerrtsp.h"
#include <QCoreApplication>
#include <QUrl>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <gst/video/video.h>

GStreamerRtsp::GStreamerRtsp(QObject *parent)
    : QThread(parent)
    , m_outputFormat("avi"){

    QString appDir = QString(EXPAND(PROJECT_PATH));

    //QString exePath = QCoreApplication::applicationDirPath();
    QString binPath = appDir;
    QString pluginPath = appDir + "/gstreamer-1.0";

    // Add both paths to system PATH
    QString path = qgetenv("PATH");
    path = binPath + ";" + appDir + ";" + path;
    qputenv("PATH", path.toLocal8Bit());

    // Set GStreamer specific environment variables
    qputenv("GST_PLUGIN_PATH", pluginPath.toLocal8Bit());
    qputenv("GST_PLUGIN_SYSTEM_PATH", pluginPath.toLocal8Bit());
    qputenv("GST_PLUGIN_SCANNER_PATH", binPath.toLocal8Bit());

    gst_init(nullptr, nullptr);
    qDebug() << "GStreamer version:" << gst_version_string();
}

GStreamerRtsp::~GStreamerRtsp() {
    stop();
    wait();
    cleanup();
}

void GStreamerRtsp::setUrl(const QString &url) {
    m_inFilename = url;
}

QString GStreamerRtsp::getUrl() const {
    return m_inFilename;
}

bool GStreamerRtsp::isRunning() const {
    return m_isRunning.load(std::memory_order_acquire);
}

QString GStreamerRtsp::name() const {
    return m_name;
}

QString GStreamerRtsp::getInFilename() const {
    return m_inFilename;
}

quint16 GStreamerRtsp::clientPort() const {
    return m_clientPort;
}

quint16 GStreamerRtsp::serverPort() const {
    return m_serverPort;
}

QString GStreamerRtsp::clientIP() const {
    return m_clientIP;
}

QString GStreamerRtsp::serverIP() const {
    return m_serverIP;
}

bool GStreamerRtsp::initialize() {
    // Enable debug output
    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    m_pipeline = gst_pipeline_new("rtsp-player");

    // Create elements for the pipeline
    GstElement *source = gst_element_factory_make("rtspsrc", "source");
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    m_videoSink = gst_element_factory_make("appsink", "video-output");

    // Check if elements were created successfully
    if (!m_pipeline || !source || !convert || !m_videoSink) {
        qDebug() << "One or more elements could not be created";
        if (!source) qDebug() << "Failed to create source";
        if (!convert) qDebug() << "Failed to create convert";
        if (!m_videoSink) qDebug() << "Failed to create video sink";
        return false;
    }

    // Configure source element
    g_object_set(G_OBJECT(source),
                 "location", m_inFilename.toStdString().c_str(),
                 "protocols", (guint)0x4,  // Enable TCP
                 "latency", (guint)0,
                 "timeout", (guint64)5000000,
                 "tcp-timeout", (guint64)5000000,
                 "do-retransmission", TRUE,
                 "buffer-mode", 0,
                 "ntp-sync", TRUE,
                 "drop-on-latency", TRUE,
                 nullptr);

    // Configure appsink
    GstCaps *appsink_caps = gst_caps_new_simple("video/x-raw",
                                                "format", G_TYPE_STRING, "BGR",
                                                nullptr);
    gst_app_sink_set_caps(GST_APP_SINK(m_videoSink), appsink_caps);
    gst_caps_unref(appsink_caps);

    g_object_set(G_OBJECT(m_videoSink),
                 "emit-signals", TRUE,
                 "sync", FALSE,
                 "drop", TRUE,
                 "max-buffers", 1,
                 nullptr);

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(m_pipeline), source, convert, m_videoSink, nullptr);

    // We'll link elements dynamically in the pad-added callback
    // Only link convert -> videosink now
    if (!gst_element_link(convert, m_videoSink)) {
        qDebug() << "Failed to link convert -> videosink";
        return false;
    }

    // Store elements as member variables if needed later
    m_source = source;
    m_converter = convert;

    // Connect pad-added signal for dynamic linking
    g_signal_connect(source, "pad-added", G_CALLBACK(on_pad_added), this);

    // Connect new-sample signal for appsink
    g_signal_connect(m_videoSink, "new-sample", G_CALLBACK(cb_new_sample), this);

    qDebug() << "Pipeline initialized successfully";
    return true;
}

void GStreamerRtsp::startStreamer() {

    if (!initialize()) {
        qDebug() << "Failed to initialize GStreamer pipeline";
        return;
    }

    m_isRunning.store(true, std::memory_order_release);
    m_isStreaming.store(true, std::memory_order_release);

    qDebug() << "Setting pipeline to PLAYING state...";

    // First set to NULL state to ensure clean start
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    QThread::msleep(100);

    // Then set to READY state
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qDebug() << "Failed to set pipeline to READY";
        cleanup();
        m_isStreaming.store(false, std::memory_order_release);
        m_isRunning.store(false, std::memory_order_release);
        return;
    }

    QThread::msleep(100);

    // Finally set to PLAYING state
    ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

    // Wait for state change with longer timeout
    GstState current, pending;
    ret = gst_element_get_state(m_pipeline, &current, &pending, 5 * GST_SECOND);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        qDebug() << "Failed to reach PLAYING state";
        cleanup();
        m_isStreaming.store(false, std::memory_order_release);
        m_isRunning.store(false, std::memory_order_release);
        return;
    }

    qDebug() << "Pipeline is now playing";

    // Wait a bit for dimensions and fps if not available
    int timeout = 0;
    while ((m_width == 0 || m_height == 0 || m_fps == 0) && timeout < 50) {
        QThread::msleep(100);
        timeout++;
        qDebug() << "Waiting for video parameters..." << timeout
                 << "Current dimensions:" << m_width << "x" << m_height
                 << "FPS:" << m_fps;
    }

    // If we have all parameters after the loop, set the flag
    if (m_width > 0 && m_height > 0 && m_fps > 0) {
        m_parametersDetected.store(true, std::memory_order_release);
        qDebug() << "Video parameters locked";
    }

    m_lastFpsUpdateTime = std::chrono::steady_clock::now();
    m_frameCount = 0;

    // Main loop for handling messages
    GstBus* bus = gst_element_get_bus(m_pipeline);
    while (!m_stop.load(std::memory_order_acquire) && !m_stopUser.load(std::memory_order_acquire)) {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus,
                                                     GST_SECOND,
                                                     (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));

        if (msg != nullptr) {
            switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError* err = nullptr;
                gchar* debug_info = nullptr;
                gst_message_parse_error(msg, &err, &debug_info);
                qDebug() << "Error received from element" << GST_OBJECT_NAME(msg->src);
                qDebug() << "Error:" << err->message;
                if (debug_info) {
                    qDebug() << "Debug information:" << debug_info;
                }
                g_clear_error(&err);
                g_free(debug_info);
                m_stop.store(true, std::memory_order_release);
                break;
            }
            case GST_MESSAGE_EOS:
                qDebug() << "End of stream reached";
                m_stop.store(true, std::memory_order_release);
                break;
            case GST_MESSAGE_STATE_CHANGED:
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {
                    GstState old_state, new_state, pending;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
                    qDebug() << "Pipeline state changed from"
                             << gst_element_state_get_name(old_state) << "to"
                             << gst_element_state_get_name(new_state)
                             << "(pending:" << gst_element_state_get_name(pending) << ")";
                }
                break;
            default:
                break;
            }
            gst_message_unref(msg);
        }
    }

    gst_object_unref(bus);

    cleanup();
    m_isStreaming.store(false, std::memory_order_release);

    if (m_stop.load(std::memory_order_acquire) && !m_stopUser.load(std::memory_order_acquire)) {
        run();
    }
}

void GStreamerRtsp::on_pad_added(GstElement *src, GstPad *new_pad, gpointer user_data) {
    GStreamerRtsp *self = static_cast<GStreamerRtsp*>(user_data);
    GstCaps *new_pad_caps = gst_pad_get_current_caps(new_pad);
    if (!new_pad_caps) {
        new_pad_caps = gst_pad_query_caps(new_pad, nullptr);
    }

    GstStructure *str = gst_caps_get_structure(new_pad_caps, 0);
    const gchar *encoding_name = gst_structure_get_string(str, "encoding-name");

    // Only get parameters if they haven't been detected yet
    if (!self->m_parametersDetected.load(std::memory_order_acquire)) {
        // Try to get video dimensions and framerate from caps
        gint width, height;
        gint fps_n, fps_d;

        if (gst_structure_get_int(str, "width", &width) &&
            gst_structure_get_int(str, "height", &height)) {
            self->m_width = width;
            self->m_height = height;
            qDebug() << "Video dimensions detected:" << width << "x" << height;
        }

        if (gst_structure_get_fraction(str, "framerate", &fps_n, &fps_d)) {
            self->m_fps = (fps_d != 0) ? (fps_n / fps_d) : 25;
            qDebug() << "Video framerate detected:" << self->m_fps
                     << "(fraction:" << fps_n << "/" << fps_d << ")";
        }

        // If we have all parameters, set the flag
        if (self->m_width > 0 && self->m_height > 0 && self->m_fps > 0) {
            self->m_parametersDetected.store(true, std::memory_order_release);
            qDebug() << "All video parameters detected";
        }
    }

    qDebug() << "Detected encoding:" << encoding_name;

    if (g_str_equal(encoding_name, "PCMU") || g_str_equal(encoding_name, "PCMA")) {
        qDebug() << "Ignoring audio codec:" << encoding_name;
        gst_caps_unref(new_pad_caps);
        return;
    }

    // Create appropriate elements based on detected codec
    GstElement *depay = nullptr;
    GstElement *parse = nullptr;
    GstElement *decoder = nullptr;

    if (g_str_equal(encoding_name, "H264")) {
        depay = gst_element_factory_make("rtph264depay", "depay");
        parse = gst_element_factory_make("h264parse", "parse");
        decoder = gst_element_factory_make("avdec_h264", "decoder");
    } else if (g_str_equal(encoding_name, "H265")) {
        depay = gst_element_factory_make("rtph265depay", "depay");
        parse = gst_element_factory_make("h265parse", "parse");
        decoder = gst_element_factory_make("avdec_h265", "decoder");
    } else {
        qDebug() << "Unsupported codec:" << encoding_name;
        gst_caps_unref(new_pad_caps);
        return;
    }

    // Add and link the new elements
    gst_bin_add_many(GST_BIN(self->m_pipeline), depay, parse, decoder, nullptr);
    gst_element_link_many(depay, parse, decoder, self->m_converter, nullptr);
    gst_element_sync_state_with_parent(depay);
    gst_element_sync_state_with_parent(parse);
    gst_element_sync_state_with_parent(decoder);

    // Link the new pad to depay
    GstPad *sink_pad = gst_element_get_static_pad(depay, "sink");
    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        qDebug() << "Failed to link pads";
    } else {
        qDebug() << "Successfully linked pads for codec:" << encoding_name;
    }

    gst_object_unref(sink_pad);
    gst_caps_unref(new_pad_caps);
}

GstFlowReturn GStreamerRtsp::cb_new_sample(GstElement *sink, gpointer user_data) {
    GStreamerRtsp *self = static_cast<GStreamerRtsp*>(user_data);
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));

    if (!sample) {
        qDebug() << "Failed to pull sample from sink";
        return GST_FLOW_ERROR;
    }

    try {
        // Only get parameters if they haven't been detected yet
        if (!self->m_parametersDetected.load(std::memory_order_acquire)) {
            GstCaps *caps = gst_sample_get_caps(sample);
            if (caps) {
                GstStructure *str = gst_caps_get_structure(caps, 0);

                // Get dimensions
                gint width, height;
                if (gst_structure_get_int(str, "width", &width) &&
                    gst_structure_get_int(str, "height", &height)) {
                    self->m_width = width;
                    self->m_height = height;
                    qDebug() << "Video dimensions detected from sample:" << width << "x" << height;
                }

                // Get framerate
                gint fps_n, fps_d;
                if (gst_structure_get_fraction(str, "framerate", &fps_n, &fps_d)) {
                    self->m_fps = (fps_d != 0) ? (fps_n / fps_d) : 25;
                    qDebug() << "Video framerate detected from sample:" << self->m_fps
                             << "(fraction:" << fps_n << "/" << fps_d << ")";
                }

                // If we have all parameters, set the flag
                if (self->m_width > 0 && self->m_height > 0 && self->m_fps > 0) {
                    self->m_parametersDetected.store(true, std::memory_order_release);
                    qDebug() << "All video parameters detected from sample";
                }
            }
        }

        // Process the frame
        self->handleFrame(sample);

    } catch (const std::exception& e) {
        qCritical() << "Error processing sample:" << e.what();
    } catch (...) {
        qCritical() << "Unknown error processing sample";
    }

    // Release the sample
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

void GStreamerRtsp::handleFrame(GstSample *sample) {

    QImage image = convertFrameToImage(sample);
    if (image.isNull()) {
        qDebug() << "Failed to convert frame to QImage";
        return;
    }

    try {
        // Emit frame for display
        emit sendVideoFrame(image);
        // Update FPS counter
        m_frameCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFpsUpdateTime).count();
        if (elapsed >= 1000) {
            m_currentFps = static_cast<int>(m_frameCount * 1000.0 / elapsed);
            m_frameCount = 0;
            m_lastFpsUpdateTime = now;
        }

    } catch (const std::exception& e) {
        qCritical() << "Unhandled exception in frame processing:" << e.what();
    }
}

gboolean GStreamerRtsp::bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GStreamerRtsp *rtsp = static_cast<GStreamerRtsp*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);

        qDebug() << "Error received from element" << GST_OBJECT_NAME(msg->src);
        qDebug() << "Error:" << err->message;
        qDebug() << "Debug info:" << (debug ? debug : "none");
        qDebug() << "Error code:" << err->code;
        qDebug() << "Error domain:" << g_quark_to_string(err->domain);

        g_clear_error(&err);
        g_free(debug);

        // Try to recover
        if (rtsp->m_pipeline) {
            gst_element_set_state(rtsp->m_pipeline, GST_STATE_NULL);
            gst_element_set_state(rtsp->m_pipeline, GST_STATE_PLAYING);
        }
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_warning(msg, &err, &debug);
        qDebug() << "Warning received from element" << GST_OBJECT_NAME(msg->src);
        qDebug() << "Warning:" << err->message;
        qDebug() << "Debug info:" << (debug ? debug : "none");
        g_clear_error(&err);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(rtsp->m_pipeline)) {
            GstState old_state, new_state, pending;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
            qDebug() << "Pipeline state changed from"
                     << gst_element_state_get_name(old_state) << "to"
                     << gst_element_state_get_name(new_state)
                     << "(pending:" << gst_element_state_get_name(pending) << ")";
        }
        break;
    }
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream reached";
        break;
    case GST_MESSAGE_ELEMENT: {
        const GstStructure *s = gst_message_get_structure(msg);
        if (s) {
            qDebug() << "Element message:" << gst_structure_get_name(s);
        }
        break;
    }
    default:
        break;
    }

    return TRUE;
}

void GStreamerRtsp::cleanup() {

    // Clear frame queue
    {
        QMutexLocker locker(&m_queueMutex);
        m_frameQueue.clear();
    }

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
}

QImage GStreamerRtsp::convertFrameToImage(GstSample *sample) {
    GstCaps *caps = gst_sample_get_caps(sample);
    if (!caps) {
        qDebug() << "No caps in sample";
        return QImage();
    }

    GstStructure *str = gst_caps_get_structure(caps, 0);
    if (!gst_structure_get_int(str, "width", &m_width) ||
        !gst_structure_get_int(str, "height", &m_height)) {
        qDebug() << "Failed to get dimensions from caps";
        return QImage();
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        qDebug() << "No buffer in sample";
        return QImage();
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        qDebug() << "Failed to map buffer";
        return QImage();
    }

    // Create QImage and copy the data
    QImage image(
        map.data,
        m_width,
        m_height,
        QImage::Format_BGR888
        );

    // Make a deep copy before unmapping the buffer
    QImage copy = image.copy();

    gst_buffer_unmap(buffer, &map);

    return copy;
}

void GStreamerRtsp::stop() {
    m_stop.store(true, std::memory_order_release);
    m_stopUser.store(true, std::memory_order_release);
    m_isRunning.store(false, std::memory_order_release);

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
    }

    qDebug() << "GStreamer stopped:" << getUrl();
}

void GStreamerRtsp::run() {
    qDebug() << "GStreamer starting:" << getUrl();
    m_stop.store(false, std::memory_order_release);
    m_stopUser.store(false, std::memory_order_release);
    startStreamer();
}

QString GStreamerRtsp::modifyRtspUrl(const QString& inFilename) {
    QUrl url(inFilename);
    QString host = url.host();
    int port = url.port(554);  // Use 554 as default if port is not specified
    QString path = url.path();

    if (path.startsWith('/')) {
        path.remove(0, 1);
    }
    path.replace("/", "_");

    return QString("%1:%2_%3").arg(host).arg(port).arg(path);
}

void GStreamerRtsp::printParameters() {
    if (!m_pipeline) return;

    GstPad *pad = gst_element_get_static_pad(m_converter, "src");
    if (!pad) return;

    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (caps) {
        GstStructure *str = gst_caps_get_structure(caps, 0);
        gint width, height;
        gint fps_n, fps_d;

        if (gst_structure_get_int(str, "width", &width) &&
            gst_structure_get_int(str, "height", &height) &&
            gst_structure_get_fraction(str, "framerate", &fps_n, &fps_d)) {

            m_info = QString("Video Info: Width: %1, Height: %2, FPS: %3")
                         .arg(width)
                         .arg(height)
                         .arg(fps_n / (float)fps_d);
        }
        gst_caps_unref(caps);
    }
    gst_object_unref(pad);

    qDebug() << m_info;
}
