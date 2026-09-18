#include "AudioEngine.h"
#include <cmath>
#include <cstring>
#include <QSettings>

#include <QDebug>
#include <QFileInfo>
#include <QUrl>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/streamvolume.h>
#include <gst/video/video.h>

#include "VideoItem.h"

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

    // Load the stored curve before the pipeline is built, so the equaliser starts configured
    // rather than flat-then-corrected - the latter is audible on the first second of a track.
    {
        QSettings settings;
        m_equaliserGains.clear();
        const QStringList stored =
            settings.value(QStringLiteral("audio/equaliserBands")).toString().split(
                QLatin1Char(','), Qt::SkipEmptyParts);
        for (int band = 0; band < kEqualiserBands; ++band)
            m_equaliserGains.append(band < stored.size() ? stored.at(band).toDouble() : 0.0);
        m_equaliserPreset = settings.value(QStringLiteral("audio/equaliserPreset")).toString();
        m_equaliserEnabled = settings.value(QStringLiteral("audio/equaliserEnabled"), false).toBool();
    }

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
    // Ahead of the tee on purpose, so the visualiser is driven by the signal the listener hears
    // rather than the one before the equaliser touched it. equalizer-10bands is in
    // gstreamer1.0-plugins-good, which is already a dependency, and costs about 0.2% of a core.
    m_equaliser = gst_element_factory_make("equalizer-10bands", nullptr);
    m_makeupGain = gst_element_factory_make("volume", nullptr);
    GstElement *tee = gst_element_factory_make("tee", nullptr);
    GstElement *playQueue = gst_element_factory_make("queue", nullptr);
    // Test seam: automated runs need the pipeline to decode normally without putting sound
    // through the speakers. Overriding the sink element is honest about what it changes,
    // unlike re-ranking plugins globally, which also perturbs decoder autoplugging.
    const QByteArray sinkOverride = qgetenv("PLAYER_AUDIO_SINK");
    GstElement *sink = gst_element_factory_make(
        sinkOverride.isEmpty() ? "autoaudiosink" : sinkOverride.constData(), nullptr);

    // A substituted sink must still honour the clock. fakesink defaults to sync=false, which
    // makes the pipeline race through a track in a second or two - so anything timed looks
    // wrong under test for reasons that have nothing to do with the code being tested.
    if (sink && !sinkOverride.isEmpty()
        && g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "sync")) {
        g_object_set(sink, "sync", TRUE, nullptr);
    }
    // The speaker branch needs its own conversion. It had none: the tee fed autoaudiosink
    // directly, so whatever the equaliser negotiated had to be something the device accepted
    // as-is. equalizer-10bands can output F64LE and pulsesink cannot take it - and when the sink
    // fails to negotiate, autoaudiosink silently substitutes a fake sink and the machine goes
    // quiet with everything else looking perfect. The visualiser branch always had converters;
    // the branch that actually feeds the speakers did not.
    GstElement *playConvert = gst_element_factory_make("audioconvert", nullptr);
    GstElement *playResample = gst_element_factory_make("audioresample", nullptr);
    GstElement *pcmQueue = gst_element_factory_make("queue", nullptr);
    GstElement *pcmConvert = gst_element_factory_make("audioconvert", nullptr);
    GstElement *pcmResample = gst_element_factory_make("audioresample", nullptr);
    m_appsink = gst_element_factory_make("appsink", nullptr);

    // The equaliser is optional: if the plugin is missing the player still plays, it just has no
    // tone controls. Failing the whole output bin over it would be the wrong trade.
    const bool haveEqualiser = m_equaliser && m_makeupGain;
    if (!haveEqualiser) {
        qWarning("equalizer-10bands unavailable; tone controls disabled");
        if (m_equaliser) { gst_object_unref(m_equaliser); m_equaliser = nullptr; }
        if (m_makeupGain) { gst_object_unref(m_makeupGain); m_makeupGain = nullptr; }
    }

    if (!bin || !convert || !tee || !playQueue || !sink || !playConvert || !playResample
        || !pcmQueue || !pcmConvert || !pcmResample || !m_appsink) {
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

    gst_bin_add_many(GST_BIN(bin), convert, tee, playQueue, playConvert, playResample, sink,
                     pcmQueue, pcmConvert, pcmResample, m_appsink, nullptr);
    // The link is checked. If the equaliser cannot sit in the chain on this machine, falling
    // back to a direct connection keeps the player playing: silence with no explanation is a far
    // worse failure than having no tone controls.
    bool equaliserLinked = false;
    if (haveEqualiser) {
        gst_bin_add_many(GST_BIN(bin), m_equaliser, m_makeupGain, nullptr);
        equaliserLinked = gst_element_link_many(convert, m_equaliser, m_makeupGain, tee, nullptr);
        if (equaliserLinked) {
            applyEqualiserToPipeline();
            qInfo("audio: equaliser in the chain");
        } else {
            qWarning("audio: could not link the equaliser; continuing without tone controls");
            gst_element_unlink_many(convert, m_equaliser, m_makeupGain, tee, nullptr);
            gst_bin_remove(GST_BIN(bin), m_equaliser);
            gst_bin_remove(GST_BIN(bin), m_makeupGain);
            m_equaliser = nullptr;
            m_makeupGain = nullptr;
        }
    }
    // autoaudiosink falls back to a *fake* sink when it cannot open a real device, and says
    // nothing. Everything then behaves perfectly - the pipeline plays, the position advances, the
    // visualiser runs off the tee - while no audio ever reaches the sound server. That is the
    // worst possible failure for a media player, and it is exactly what one machine was doing.
    //
    // So the fallback is detected and reported rather than accepted in silence.
    if (sink && GST_IS_BIN(sink)) {
        g_signal_connect(sink, "element-added",
                         G_CALLBACK(+[](GstBin *, GstElement *element, gpointer data) {
                             auto *self = static_cast<AudioEngine *>(data);
                             const gchar *name = gst_element_get_name(element);
                             qInfo("audio: output sink is %s", name);
                             if (name && strstr(name, "fake")) {
                                 qWarning("audio: no real output device could be opened - "
                                          "autoaudiosink fell back to a fake sink, so there "
                                          "will be no sound");
                                 QMetaObject::invokeMethod(
                                     self, [self]() {
                                         emit self->errorOccurred(
                                             tr("No audio output could be opened, so there is no "
                                                "sound. Check the system's sound settings."));
                                     }, Qt::QueuedConnection);
                             }
                         }), this);
    }

    if (!equaliserLinked) {
        if (!gst_element_link(convert, tee))
            qWarning("audio: could not link the output bin at all - there will be no sound");
        else
            qInfo("audio: equaliser bypassed");
    }
    if (!gst_element_link_many(playQueue, playConvert, playResample, sink, nullptr))
        qWarning("audio: could not link the speaker branch");
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

    buildVideoSink();

    gst_stream_volume_set_volume(GST_STREAM_VOLUME(m_pipeline),
                                 GST_STREAM_VOLUME_FORMAT_CUBIC, m_volume);
}

namespace {

// Called on a GStreamer streaming thread.
GstFlowReturn onNewVideoSample(GstAppSink *sink, gpointer userData)
{
    auto *engine = static_cast<AudioEngine *>(userData);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
        return GST_FLOW_OK;

    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstVideoInfo info;
    GstMapInfo map;

    if (caps && buffer && gst_video_info_from_caps(&info, caps)
        && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        const int width = GST_VIDEO_INFO_WIDTH(&info);
        const int height = GST_VIDEO_INFO_HEIGHT(&info);
        const int stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);

        // copy() detaches from the mapped buffer, which is unmapped the moment we return.
        const QImage frame = QImage(map.data, width, height, stride,
                                    QImage::Format_RGBA8888).copy();
        gst_buffer_unmap(buffer, &map);
        engine->deliverVideoFrame(frame);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

} // namespace

void AudioEngine::buildVideoSink()
{
    // Frames come back as plain RGBA and are drawn by VideoItem. See VideoItem.h for why the
    // GL sink is not used.
    GstElement *bin = gst_bin_new("video-output");
    GstElement *convert = gst_element_factory_make("videoconvert", nullptr);
    m_videoSink = gst_element_factory_make("appsink", nullptr);
    if (!bin || !convert || !m_videoSink) {
        qWarning("could not build the video output bin");
        return;
    }

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGBA",
                                        nullptr);
    gst_app_sink_set_caps(GST_APP_SINK(m_videoSink), caps);
    gst_caps_unref(caps);

    // sync=true so video is presented on the clock rather than as fast as it decodes; that is
    // what keeps it with the audio. Dropping is allowed because a late frame is worth less
    // than a stalled pipeline.
    g_object_set(m_videoSink, "emit-signals", FALSE, "sync", TRUE, "max-buffers", 2,
                 "drop", TRUE, nullptr);
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onNewVideoSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(m_videoSink), &callbacks, this, nullptr);

    gst_bin_add_many(GST_BIN(bin), convert, m_videoSink, nullptr);
    gst_element_link(convert, m_videoSink);

    GstPad *binSink = gst_element_get_static_pad(convert, "sink");
    gst_element_add_pad(bin, gst_ghost_pad_new("sink", binSink));
    gst_object_unref(binSink);

    g_object_set(m_pipeline, "video-sink", bin, nullptr);
}

void AudioEngine::setVideoItem(QQuickItem *item)
{
    m_videoItem = qobject_cast<VideoItem *>(item);
    if (!m_videoItem && item)
        qWarning("video surface is not a VideoItem; video will not be shown");
}

void AudioEngine::deliverVideoFrame(const QImage &frame)
{
    if (!m_videoItem)
        return;
    QMetaObject::invokeMethod(m_videoItem, "presentFrame", Qt::QueuedConnection,
                              Q_ARG(QImage, frame));
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
    if (!m_tagTitle.isEmpty() || !m_tagArtist.isEmpty()) {
        m_tagTitle.clear();
        m_tagArtist.clear();
        emit tagsChanged();
    }
    m_streamStation.clear();
    m_bufferPercent = 100;
    m_sinceProgress.invalidate();
    if (m_hasVideo) {
        m_hasVideo = false;
        emit hasVideoChanged();
    }
    if (m_videoItem)
        QMetaObject::invokeMethod(m_videoItem, "clear", Qt::QueuedConnection);
    if (m_buffering) {
        m_buffering = false;
        emit bufferingChanged();
    }
    emit streamStationChanged();
    emit sourceChanged();
    emit positionChanged();
    emit durationChanged();
    emit streamTitleChanged();
    setState(Stopped);
}

void AudioEngine::applyOutputLevels()
{
    if (!m_pipeline)
        return;
    gst_stream_volume_set_volume(GST_STREAM_VOLUME(m_pipeline),
                                 GST_STREAM_VOLUME_FORMAT_CUBIC, m_volume);
    gst_stream_volume_set_mute(GST_STREAM_VOLUME(m_pipeline), m_muted);
}

void AudioEngine::play()
{
    if (!m_pipeline || m_source.isEmpty())
        return;
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    // After the state change, so the sink exists and the value reaches the stream rather than
    // being overwritten by whatever the audio server restored for this application.
    applyOutputLevels();
    m_positionTimer.start();
    m_sinceProgress.restart();
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

void AudioEngine::updateBuffering()
{
    // "Buffering" should mean playback has stalled, not merely that a queue is below full.
    // A live stream delivered at exactly playback rate sits under 100% forever while playing
    // perfectly well, and reporting that as buffering is just wrong.
    const bool stalled = m_bufferPercent < 100 && m_state == Playing
        && m_sinceProgress.isValid() && m_sinceProgress.elapsed() > 1000;
    if (stalled != m_buffering) {
        m_buffering = stalled;
        emit bufferingChanged();
    }
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
            m_sinceProgress.restart();
            emit positionChanged();
        }
    }
    updateBuffering();
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
        if (qEnvironmentVariableIsSet("PLAYER_BUS_TRACE")) {
            qInfo("bus: %s from %s", GST_MESSAGE_TYPE_NAME(msg),
                  GST_MESSAGE_SRC_NAME(msg) ? GST_MESSAGE_SRC_NAME(msg) : "?");
        }
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
        // Warnings were being dropped on the floor. A pipeline that fails to negotiate, or a
        // sink that cannot open its device, often reports it this way rather than as an error -
        // so the player went quiet with nothing said anywhere.
        case GST_MESSAGE_WARNING: {
            GError *warning = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_warning(msg, &warning, &debug);
            qWarning("gstreamer warning from %s: %s (%s)",
                     GST_OBJECT_NAME(msg->src),
                     warning ? warning->message : "unknown",
                     debug ? debug : "no detail");
            if (warning)
                g_error_free(warning);
            g_free(debug);
            break;
        }
        case GST_MESSAGE_STREAM_COLLECTION: {
            // playbin3 reports what it found this way rather than through an n-video property.
            GstStreamCollection *collection = nullptr;
            gst_message_parse_stream_collection(msg, &collection);
            if (collection) {
                bool video = false;
                const guint count = gst_stream_collection_get_size(collection);
                for (guint i = 0; i < count && !video; ++i) {
                    GstStream *stream = gst_stream_collection_get_stream(collection, i);
                    if (stream && (gst_stream_get_stream_type(stream) & GST_STREAM_TYPE_VIDEO))
                        video = true;
                }
                gst_object_unref(collection);
                if (video != m_hasVideo) {
                    m_hasVideo = video;
                    emit hasVideoChanged();
                }
            }
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED:
            updateDuration();
            break;
        case GST_MESSAGE_ASYNC_DONE:
            updateDuration();
            // Only now does the sink exist, so only now can our mute and volume reach the
            // stream. Setting them right after set_state(PLAYING) is too early - the state
            // change is asynchronous, and the audio server applies its remembered
            // per-application values when the stream is actually created.
            applyOutputLevels();
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
                    if (value != m_tagTitle) {
                        m_tagTitle = value;
                        emit tagsChanged();
                    }
                    g_free(title);
                }
                gchar *artist = nullptr;
                if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &artist) && artist) {
                    const QString value = QString::fromUtf8(artist);
                    if (value != m_tagArtist) {
                        m_tagArtist = value;
                        emit tagsChanged();
                    }
                    g_free(artist);
                }
                // Icecast sends the station name as the organization tag.
                gchar *organization = nullptr;
                if (gst_tag_list_get_string(tags, GST_TAG_ORGANIZATION, &organization)
                    && organization) {
                    const QString value = QString::fromUtf8(organization);
                    if (value != m_streamStation) {
                        m_streamStation = value;
                        emit streamStationChanged();
                    }
                    g_free(organization);
                }
                gst_tag_list_unref(tags);
            }
            break;
        }
        case GST_MESSAGE_BUFFERING: {
            gint percent = 0;
            gst_message_parse_buffering(msg, &percent);
            m_bufferPercent = percent;
            updateBuffering();
            break;
        }
        default:
            break;
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}

// --- equaliser -------------------------------------------------------------------------------
//
// Ten fixed bands, the centres equalizer-10bands defines. The curves are our own renditions of
// what these names usually mean, not reproductions of anybody's published settings - the same
// honesty the colour palettes carry.
//
// Boosts are kept modest on purpose. Every positive decibel here is one the makeup stage has to
// take back out to avoid clipping, so a curve full of +12s would simply be a quieter track.
namespace {

struct EqPreset { const char *name; double gains[AudioEngine::kEqualiserBands]; };

const EqPreset kEqPresets[] = {
    { "Flat",        {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 } },
    { "Acoustic",    {  4,  4,  3,  1,  2,  2,  3,  3,  2,  1 } },
    { "Bass boost",  {  6,  5,  4,  2,  0,  0,  0,  0,  0,  0 } },
    { "Classical",   {  4,  3,  2,  0, -1, -1,  0,  2,  3,  3 } },
    { "Country",     {  3,  4,  2,  0, -1,  0,  2,  3,  3,  2 } },
    { "Electronic",  {  5,  4,  1, -1, -2,  1,  2,  3,  4,  4 } },
    { "Hip-hop",     {  6,  5,  3,  1, -1, -1,  1,  2,  3,  2 } },
    { "Jazz",        {  4,  3,  1,  1, -1, -1,  0,  2,  3,  3 } },
    { "Pop",         { -1,  0,  2,  3,  4,  3,  1,  0, -1, -1 } },
    { "Rock",        {  5,  4,  2, -1, -2, -1,  2,  3,  4,  4 } },
    { "Treble boost",{  0,  0,  0,  0,  0,  1,  2,  4,  5,  6 } },
    { "Vocal",       { -3, -2,  0,  2,  4,  4,  3,  1,  0, -1 } },
};

const char *kBandLabels[AudioEngine::kEqualiserBands] = {
    "29", "59", "119", "237", "474", "947", "1.9k", "3.8k", "7.5k", "15k"
};

} // namespace

QStringList AudioEngine::equaliserPresetNames() const
{
    QStringList names;
    for (const EqPreset &preset : kEqPresets)
        names.append(QString::fromLatin1(preset.name));
    return names;
}

QStringList AudioEngine::equaliserBandLabels() const
{
    QStringList labels;
    for (const char *label : kBandLabels)
        labels.append(QString::fromLatin1(label));
    return labels;
}

QVariantList AudioEngine::equaliserBands() const
{
    QVariantList bands;
    for (qreal gain : m_equaliserGains)
        bands.append(gain);
    return bands;
}

void AudioEngine::applyEqualiserPreset(const QString &name)
{
    for (const EqPreset &preset : kEqPresets) {
        if (name.compare(QString::fromLatin1(preset.name), Qt::CaseInsensitive) != 0)
            continue;
        m_equaliserGains.clear();
        for (double gain : preset.gains)
            m_equaliserGains.append(gain);
        m_equaliserPreset = QString::fromLatin1(preset.name);
        // Choosing a curve is choosing to hear it; leaving the equaliser off after picking one
        // looks exactly like the picking not having worked.
        m_equaliserEnabled = true;
        applyEqualiserToPipeline();
        saveEqualiser();
        emit equaliserChanged();
        return;
    }
}

void AudioEngine::setEqualiserBand(int band, qreal gainDb)
{
    if (band < 0 || band >= kEqualiserBands)
        return;
    gainDb = qBound(-24.0, static_cast<double>(gainDb), 12.0);
    if (qFuzzyCompare(m_equaliserGains.at(band), gainDb))
        return;
    m_equaliserGains[band] = gainDb;
    // Editing a band means this is no longer whichever preset it started from. Saying so is what
    // stops the menu claiming "Rock" while the curve is something else.
    m_equaliserPreset.clear();
    m_equaliserEnabled = true;
    applyEqualiserToPipeline();
    saveEqualiser();
    emit equaliserChanged();
}

void AudioEngine::setEqualiserEnabled(bool enabled)
{
    if (enabled == m_equaliserEnabled)
        return;
    m_equaliserEnabled = enabled;
    applyEqualiserToPipeline();
    saveEqualiser();
    emit equaliserChanged();
}

void AudioEngine::chooseNoEqualiser()
{
    m_equaliserEnabled = false;
    applyEqualiserToPipeline();
    saveEqualiser();
    emit equaliserChanged();
}

void AudioEngine::applyCustomEqualiser()
{
    // The stored curve is already whatever was last edited; this marks it as the chosen one and
    // turns the equaliser on, so picking "Custom" is a real selection rather than just a way to
    // open the editor.
    m_equaliserPreset.clear();
    m_equaliserEnabled = true;
    applyEqualiserToPipeline();
    saveEqualiser();
    emit equaliserChanged();
}

void AudioEngine::flattenCustomEqualiser()
{
    for (int band = 0; band < kEqualiserBands; ++band)
        m_equaliserGains[band] = 0.0;
    m_equaliserPreset.clear();
    m_equaliserEnabled = true;
    applyEqualiserToPipeline();
    saveEqualiser();
    emit equaliserChanged();
}

void AudioEngine::resetEqualiser()
{
    applyEqualiserPreset(QStringLiteral("Flat"));
    setEqualiserEnabled(false);
}

void AudioEngine::applyEqualiserToPipeline()
{
    if (!m_equaliser || !m_makeupGain)
        return;
    double highestBoost = 0.0;
    for (int band = 0; band < kEqualiserBands; ++band) {
        const double gain = m_equaliserEnabled ? m_equaliserGains.value(band, 0.0) : 0.0;
        highestBoost = qMax(highestBoost, gain);
        g_object_set(m_equaliser, QByteArray("band" + QByteArray::number(band)).constData(),
                     gain, nullptr);
    }
    // Take back exactly what the loudest band added. Without this a boosted curve on material
    // already near full scale clips, which sounds like distortion rather than like tone control.
    g_object_set(m_makeupGain, "volume", std::pow(10.0, -highestBoost / 20.0), nullptr);
}

void AudioEngine::saveEqualiser()
{
    QSettings settings;
    settings.setValue(QStringLiteral("audio/equaliserEnabled"), m_equaliserEnabled);
    settings.setValue(QStringLiteral("audio/equaliserPreset"), m_equaliserPreset);
    QStringList gains;
    for (qreal gain : m_equaliserGains)
        gains.append(QString::number(gain, 'f', 1));
    settings.setValue(QStringLiteral("audio/equaliserBands"), gains.join(QLatin1Char(',')));
}
