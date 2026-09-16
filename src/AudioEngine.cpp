#include "AudioEngine.h"

#include <QDebug>
#include <QFileInfo>
#include <QUrl>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/streamvolume.h>

namespace {

constexpr int kPcmRate = 44100;

// Called on a GStreamer streaming thread.
GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData)
{
    auto *ring = static_cast<AudioRingBuffer *>(userData);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
        return GST_FLOW_OK;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        const int frames = static_cast<int>(map.size / (sizeof(float) * AudioRingBuffer::kChannels));
        if (frames > 0)
            ring->write(reinterpret_cast<const float *>(map.data), frames);
        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

} // namespace

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , m_ring(std::make_unique<AudioRingBuffer>())
{
    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);

    buildPipeline();

    // Polling the bus avoids assuming a GLib main loop is driving Qt's event loop.
    m_busTimer.setInterval(50);
    connect(&m_busTimer, &QTimer::timeout, this, &AudioEngine::pollBus);
    m_busTimer.start();

    m_positionTimer.setInterval(100);
    connect(&m_positionTimer, &QTimer::timeout, this, &AudioEngine::pollPosition);
}

AudioEngine::~AudioEngine()
{
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
    }
}

void AudioEngine::buildPipeline()
{
    m_pipeline = gst_element_factory_make("playbin3", "player");
    if (!m_pipeline) {
        emit errorOccurred(QStringLiteral("GStreamer playbin3 is not available"));
        return;
    }

    // The audio sink is a bin that tees the decoded stream: one branch to the speakers,
    // one to an appsink delivering float32 stereo for the visualiser. This is the shape
    // the brief settled on, and it is why no second media engine is needed.
    GstElement *bin = gst_bin_new("audio-output");
    GstElement *convert = gst_element_factory_make("audioconvert", nullptr);
    GstElement *tee = gst_element_factory_make("tee", nullptr);
    GstElement *playQueue = gst_element_factory_make("queue", nullptr);
    // Test seam: automated runs need the pipeline to decode normally without putting sound
    // through the speakers. Overriding the sink element is honest about what it changes,
    // unlike re-ranking plugins globally, which also perturbs decoder autoplugging.
    const QByteArray sinkOverride = qgetenv("PLAYER_AUDIO_SINK");
    GstElement *sink = gst_element_factory_make(
        sinkOverride.isEmpty() ? "autoaudiosink" : sinkOverride.constData(), nullptr);
    GstElement *pcmQueue = gst_element_factory_make("queue", nullptr);
    GstElement *pcmConvert = gst_element_factory_make("audioconvert", nullptr);
    GstElement *pcmResample = gst_element_factory_make("audioresample", nullptr);
    m_appsink = gst_element_factory_make("appsink", nullptr);

    if (!bin || !convert || !tee || !playQueue || !sink || !pcmQueue || !pcmConvert
        || !pcmResample || !m_appsink) {
        emit errorOccurred(QStringLiteral("Failed to create the audio output bin"));
        return;
    }

    GstCaps *caps = gst_caps_new_simple("audio/x-raw",
                                        "format", G_TYPE_STRING, "F32LE",
                                        "channels", G_TYPE_INT, AudioRingBuffer::kChannels,
                                        "rate", G_TYPE_INT, kPcmRate,
                                        "layout", G_TYPE_STRING, "interleaved",
                                        nullptr);
    gst_app_sink_set_caps(GST_APP_SINK(m_appsink), caps);
    gst_caps_unref(caps);

    // Never let the visualiser branch stall playback: drop rather than block.
    g_object_set(m_appsink, "emit-signals", FALSE, "sync", FALSE, "max-buffers", 4,
                 "drop", TRUE, nullptr);
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(m_appsink), &callbacks, m_ring.get(), nullptr);

    g_object_set(pcmQueue, "leaky", 2 /* downstream */, "max-size-buffers", 8, nullptr);

    gst_bin_add_many(GST_BIN(bin), convert, tee, playQueue, sink, pcmQueue, pcmConvert,
                     pcmResample, m_appsink, nullptr);
    gst_element_link(convert, tee);
    gst_element_link_many(playQueue, sink, nullptr);
    gst_element_link_many(pcmQueue, pcmConvert, pcmResample, m_appsink, nullptr);

    GstPad *teePlay = gst_element_request_pad_simple(tee, "src_%u");
    GstPad *teePcm = gst_element_request_pad_simple(tee, "src_%u");
    GstPad *playIn = gst_element_get_static_pad(playQueue, "sink");
    GstPad *pcmIn = gst_element_get_static_pad(pcmQueue, "sink");
    gst_pad_link(teePlay, playIn);
    gst_pad_link(teePcm, pcmIn);
    gst_object_unref(playIn);
    gst_object_unref(pcmIn);

    GstPad *binSink = gst_element_get_static_pad(convert, "sink");
    gst_element_add_pad(bin, gst_ghost_pad_new("sink", binSink));
    gst_object_unref(binSink);

    g_object_set(m_pipeline, "audio-sink", bin, nullptr);

    gst_stream_volume_set_volume(GST_STREAM_VOLUME(m_pipeline),
                                 GST_STREAM_VOLUME_FORMAT_CUBIC, m_volume);
}

void AudioEngine::setSource(const QString &uriOrPath)
{
    if (!m_pipeline)
        return;

    QString uri = uriOrPath;
    if (!uri.contains(QStringLiteral("://")))
        uri = QUrl::fromLocalFile(QFileInfo(uriOrPath).absoluteFilePath()).toString();

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    m_ring->reset();
    g_object_set(m_pipeline, "uri", uri.toUtf8().constData(), nullptr);

    m_source = uri;
    m_position = 0;
    m_duration = 0;
    m_streamTitle.clear();
    emit sourceChanged();
    emit positionChanged();
    emit durationChanged();
    emit streamTitleChanged();
    setState(Stopped);
}

void AudioEngine::play()
{
    if (!m_pipeline || m_source.isEmpty())
        return;
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    m_positionTimer.start();
    setState(Playing);
}

void AudioEngine::pause()
{
    if (!m_pipeline)
        return;
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    m_positionTimer.stop();
    setState(Paused);
}

void AudioEngine::togglePlayPause()
{
    m_state == Playing ? pause() : play();
}

void AudioEngine::stop()
{
    if (!m_pipeline)
        return;
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    m_positionTimer.stop();
    m_ring->reset();
    m_position = 0;
    emit positionChanged();
    setState(Stopped);
}

void AudioEngine::seek(qint64 milliseconds)
{
    if (!m_pipeline || !m_seekable)
        return;
    gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
                            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                            milliseconds * GST_MSECOND);
    m_position = milliseconds;
    emit positionChanged();
}

void AudioEngine::setVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(volume, m_volume))
        return;
    m_volume = volume;
    if (m_pipeline) {
        // Cubic is the perceptual curve: a linear slider should sound linear.
        gst_stream_volume_set_volume(GST_STREAM_VOLUME(m_pipeline),
                                     GST_STREAM_VOLUME_FORMAT_CUBIC, m_volume);
    }
    emit volumeChanged();
}

void AudioEngine::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    if (m_pipeline)
        gst_stream_volume_set_mute(GST_STREAM_VOLUME(m_pipeline), muted);
    emit mutedChanged();
}

void AudioEngine::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void AudioEngine::updateDuration()
{
    gint64 duration = 0;
    if (gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &duration) && duration > 0) {
        const qint64 ms = duration / GST_MSECOND;
        if (ms != m_duration) {
            m_duration = ms;
            emit durationChanged();
        }
    }

    GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
    if (gst_element_query(m_pipeline, query)) {
        gboolean seekable = FALSE;
        gst_query_parse_seeking(query, nullptr, &seekable, nullptr, nullptr);
        if (bool(seekable) != m_seekable) {
            m_seekable = seekable;
            emit seekableChanged();
        }
    }
    gst_query_unref(query);
}

void AudioEngine::pollPosition()
{
    if (!m_pipeline)
        return;
    gint64 position = 0;
    if (gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &position)) {
        const qint64 ms = position / GST_MSECOND;
        if (ms != m_position) {
            m_position = ms;
            emit positionChanged();
        }
    }
    if (m_duration <= 0)
        updateDuration();
}

void AudioEngine::pollBus()
{
    if (!m_pipeline)
        return;
    GstBus *bus = gst_element_get_bus(m_pipeline);
    if (!bus)
        return;

    while (GstMessage *msg = gst_bus_pop(bus)) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            m_positionTimer.stop();
            setState(Stopped);
            emit endOfStream();
            break;
        case GST_MESSAGE_ERROR: {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(msg, &error, &debug);
            emit errorOccurred(QString::fromUtf8(error ? error->message : "unknown error"));
            if (error)
                g_error_free(error);
            g_free(debug);
            m_positionTimer.stop();
            setState(Stopped);
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED:
            updateDuration();
            break;
        case GST_MESSAGE_ASYNC_DONE:
            updateDuration();
            break;
        case GST_MESSAGE_TAG: {
            GstTagList *tags = nullptr;
            gst_message_parse_tag(msg, &tags);
            if (tags) {
                gchar *title = nullptr;
                if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &title) && title) {
                    const QString value = QString::fromUtf8(title);
                    if (value != m_streamTitle) {
                        m_streamTitle = value;
                        emit streamTitleChanged();
                    }
                    g_free(title);
                }
                gst_tag_list_unref(tags);
            }
            break;
        }
        case GST_MESSAGE_BUFFERING: {
            gint percent = 0;
            gst_message_parse_buffering(msg, &percent);
            if (percent < 100 && m_state == Playing)
                setState(Buffering);
            else if (percent >= 100 && m_state == Buffering)
                setState(Playing);
            break;
        }
        default:
            break;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}
