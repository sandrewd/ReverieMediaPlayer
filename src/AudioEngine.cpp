#include "AudioEngine.h"
#include <cmath>
#include <cstring>
#include <QSettings>

#include <QDebug>
#include <QFileInfo>
#include <QLocale>
#include <QDir>
#include <QUrl>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/streamvolume.h>
#include <gst/video/video.h>

#include "VideoItem.h"

namespace {


// Called on a GStreamer streaming thread, watching the audio on its way to the speakers.
//
// This replaced a tee with a second appsink branch. A tee's branches negotiate independently, so
// a failure on one is silent on the other: the visualiser branch ran happily while the speaker
// branch failed to negotiate and no error was ever posted. A probe cannot fail that way - there
// is only one chain, and if it does not negotiate nothing plays and GStreamer says so. Strawberry
// takes the same approach for its analyser.
GstPadProbeReturn onAudioProbe(GstPad *pad, GstPadProbeInfo *info, gpointer userData)
{
    auto *ring = static_cast<AudioRingBuffer *>(userData);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!ring || !buffer)
        return GST_PAD_PROBE_OK;

    // The format is pinned to F32LE by a capsfilter; the channel count is whatever the track has,
    // deliberately, so watching the audio never reshapes it.
    int channels = 0;
    if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
        gst_structure_get_int(gst_caps_get_structure(caps, 0), "channels", &channels);
        gst_caps_unref(caps);
    }
    if (channels <= 0)
        return GST_PAD_PROBE_OK;

    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        const int frames = static_cast<int>(map.size / (sizeof(float) * channels));
        if (frames > 0)
            ring->write(reinterpret_cast<const float *>(map.data), frames, channels);
        gst_buffer_unmap(buffer, &map);
    }
    return GST_PAD_PROBE_OK;
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
        // Subtitles default OFF, which is deliberately *not* what playbin3 does - left alone it
        // turns the first text stream on. Someone watching in their own language did not ask for
        // them, and they are one button away. Once on, the preferred language decides which
        // track, and the choice persists.
        m_subtitlesWanted = settings.value(QStringLiteral("video/subtitlesEnabled"), false).toBool();
        m_preferredAudioLanguage =
            settings.value(QStringLiteral("audio/preferredLanguage")).toString();
        m_preferredSubtitleLanguage =
            settings.value(QStringLiteral("video/preferredSubtitleLanguage")).toString();
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

    // Hardware decode is autoplugged by playbin3 when the machine has a VA driver, and is
    // otherwise silently absent - the va plugin ships in gstreamer1.0-plugins-bad, which is
    // already a hard dependency, and registers no elements at all without a capable device.
    //
    // That silence is the problem: there is no way to tell from the outside whether a file is
    // being decoded on the GPU or on four software threads. So the pipeline says which decoder
    // it actually chose. §6 made the same argument about frame rate - the application has to
    // report what it is doing, because watching it cannot tell you.
    g_signal_connect(m_pipeline, "deep-element-added",
                     G_CALLBACK(+[](GstBin *, GstBin *, GstElement *element, gpointer) {
                         GstElementFactory *factory = gst_element_get_factory(element);
                         if (!factory)
                             return;
                         const gchar *klass =
                             gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS);
                         if (!klass || !strstr(klass, "Decoder"))
                             return;
                         const gchar *name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
                         if (!name)
                             return;
                         // Named so the answer is greppable in a bug report.
                         qInfo("decoder: %s (%s)%s", name, klass,
                               (g_str_has_prefix(name, "va") || strstr(klass, "Hardware"))
                                   ? "  [hardware]" : "");
                     }),
                     nullptr);

    // An escape hatch, because VA drivers are the least reliable part of this stack and a
    // machine that cannot play video is worse than one that decodes in software. Demoting the
    // rank leaves the elements present but never autoplugged.
    if (qEnvironmentVariableIsSet("PLAYER_NO_HW_DECODE")) {
        GstRegistry *registry = gst_registry_get();
        GList *features = gst_registry_get_feature_list(registry, GST_TYPE_ELEMENT_FACTORY);
        int demoted = 0;
        for (GList *item = features; item; item = item->next) {
            auto *feature = GST_PLUGIN_FEATURE(item->data);
            const gchar *name = gst_plugin_feature_get_name(feature);
            const gchar *klass = gst_element_factory_get_metadata(GST_ELEMENT_FACTORY(feature),
                                                                 GST_ELEMENT_METADATA_KLASS);
            if (name && klass && strstr(klass, "Decoder")
                && (g_str_has_prefix(name, "va") || g_str_has_prefix(name, "nv")
                    || g_str_has_prefix(name, "msdk"))) {
                gst_plugin_feature_set_rank(feature, GST_RANK_NONE);
                ++demoted;
            }
        }
        gst_plugin_feature_list_free(features);
        qInfo("decoder: hardware decoding disabled by PLAYER_NO_HW_DECODE (%d demoted)", demoted);
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

    // Pins the format the probe reads, and nothing else. Channels and rate are left alone on
    // purpose: the visualiser must not reshape the audio on its way to the speakers, and forcing
    // stereo here would quietly downmix multichannel video soundtracks.
    GstElement *pcmCaps = gst_element_factory_make("capsfilter", nullptr);
    // Always convert and resample immediately before the sink. equalizer-10bands can emit F64LE
    // and pulsesink cannot accept it; without these the sink simply fails to link, and
    // autoaudiosink answers that by silently substituting a fake sink.
    GstElement *playConvert = gst_element_factory_make("audioconvert", nullptr);
    GstElement *playResample = gst_element_factory_make("audioresample", nullptr);

    // The equaliser is optional: if the plugin is missing the player still plays, it just has no
    // tone controls. Failing the whole output bin over it would be the wrong trade.
    const bool haveEqualiser = m_equaliser && m_makeupGain;
    if (!haveEqualiser) {
        qWarning("equalizer-10bands unavailable; tone controls disabled");
        if (m_equaliser) { gst_object_unref(m_equaliser); m_equaliser = nullptr; }
        if (m_makeupGain) { gst_object_unref(m_makeupGain); m_makeupGain = nullptr; }
    }

    if (!bin || !convert || !sink || !pcmCaps || !playConvert || !playResample) {
        emit errorOccurred(QStringLiteral("Failed to create the audio output bin"));
        return;
    }

    GstCaps *caps = gst_caps_new_simple("audio/x-raw",
                                        "format", G_TYPE_STRING, "F32LE",
                                        "layout", G_TYPE_STRING, "interleaved",
                                        nullptr);
    g_object_set(pcmCaps, "caps", caps, nullptr);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(bin), convert, pcmCaps, playConvert, playResample, sink, nullptr);

    // autoaudiosink falls back to a *fake* sink when it cannot open a real device, and says
    // nothing. Everything then behaves perfectly - the pipeline plays, the position advances, the
    // visualiser runs - while no audio reaches the sound server at all. That is the worst failure
    // a media player can have, and it is exactly what one machine was doing.
    if (sink && GST_IS_BIN(sink)) {
        g_signal_connect(sink, "element-added",
                         G_CALLBACK(+[](GstBin *, GstElement *element, gpointer data) {
                             auto *self = static_cast<AudioEngine *>(data);
                             const gchar *name = gst_element_get_name(element);
                             qInfo("audio: output sink is %s", name);
                             if (!name || !strstr(name, "fake"))
                                 return;
                             // Decided on the GUI thread, where the playback state can be read
                             // safely - and where it means something. autoaudiosink also swaps
                             // its fake sink in while the pipeline is torn down at the end of a
                             // track, which raised the banner every time a track finished. It is
                             // only a real failure if we still believe we are playing.
                             QMetaObject::invokeMethod(
                                 self, [self]() {
                                     if (self->m_state != Playing && self->m_state != Buffering)
                                         return;
                                     qWarning("audio: no real output device could be opened - "
                                              "autoaudiosink fell back to a fake sink, so there "
                                              "will be no sound");
                                     emit self->errorOccurred(
                                         tr("No audio output could be opened, so there is no "
                                            "sound. Check the system's sound settings."));
                                 }, Qt::QueuedConnection);
                         }), this);
    }

    // One chain, so there is no branch that can fail quietly. The equaliser is spliced in when it
    // is available and skipped when it is not; either way the link is checked, because silence
    // with no explanation is far worse than having no tone controls.
    bool linked = false;
    if (haveEqualiser) {
        gst_bin_add_many(GST_BIN(bin), m_equaliser, m_makeupGain, nullptr);
        linked = gst_element_link_many(convert, m_equaliser, m_makeupGain, pcmCaps,
                                       playConvert, playResample, sink, nullptr);
        if (linked) {
            applyEqualiserToPipeline();
            qInfo("audio: equaliser in the chain");
        } else {
            qWarning("audio: could not link the equaliser; continuing without tone controls");
            gst_element_unlink_many(convert, m_equaliser, m_makeupGain, pcmCaps, nullptr);
            gst_bin_remove(GST_BIN(bin), m_equaliser);
            gst_bin_remove(GST_BIN(bin), m_makeupGain);
            m_equaliser = nullptr;
            m_makeupGain = nullptr;
        }
    }
    if (!linked) {
        linked = gst_element_link_many(convert, pcmCaps, playConvert, playResample, sink, nullptr);
        qInfo("audio: %s", linked ? "equaliser bypassed" : "output bin did not link");
    }
    if (!linked)
        qWarning("audio: the output chain could not be linked - there will be no sound");

    // Watch the audio on its way past, rather than splitting it off. See onAudioProbe.
    if (GstPad *pcmPad = gst_element_get_static_pad(pcmCaps, "src")) {
        gst_pad_add_probe(pcmPad, GST_PAD_PROBE_TYPE_BUFFER, onAudioProbe, m_ring.get(), nullptr);
        gst_object_unref(pcmPad);
    }

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

        // The dimensions come from the caps and the bytes come from the buffer, and nothing
        // guarantees the two agree. A file is untrusted input by definition here, so a stream
        // whose caps claim more than the buffer holds must not be handed to QImage - it would
        // read past the mapping. Cheap to check, and the alternative is an out-of-bounds read on
        // a malformed or hostile video.
        const bool sane = width > 0 && height > 0 && stride > 0
                          && map.size >= static_cast<gsize>(height) * static_cast<gsize>(stride);
        if (sane) {
            // copy() detaches from the mapped buffer, which is unmapped the moment we return.
            const QImage frame = QImage(map.data, width, height, stride,
                                        QImage::Format_RGBA8888).copy();
            gst_buffer_unmap(buffer, &map);
            engine->deliverVideoFrame(frame);
        } else {
            qWarning("video: frame %dx%d stride %d does not fit in %zu bytes; dropped",
                     width, height, stride, static_cast<size_t>(map.size));
            gst_buffer_unmap(buffer, &map);
        }
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

    // Subtitle state belongs to the old file. suburi in particular must be cleared explicitly:
    // left set, it would attach the previous track's subtitle file to this one.
    m_subtitles.clear();
    m_videoStreamIds.clear();
    m_audioStreams.clear();
    m_audioTrack = -1;
    m_subtitleTrack = -1;
    m_selectNewSubtitle = false;
    m_subtitleFile = sidecarSubtitleFor(uri);
    g_object_set(m_pipeline, "suburi",
                 m_subtitleFile.isEmpty()
                     ? nullptr
                     : QUrl::fromLocalFile(m_subtitleFile).toString().toUtf8().constData(),
                 nullptr);
    if (!m_subtitleFile.isEmpty())
        qInfo("subtitles: found %s alongside the media",
              qUtf8Printable(QFileInfo(m_subtitleFile).fileName()));
    emit subtitlesChanged();
    emit audioTracksChanged();

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

// Subtitles
//
// Nothing here renders text. playbin3's playsink inserts subtitleoverlay ahead of whatever video
// sink it is given, including our appsink, so subtitle pixels are already in the RGBA frames by
// the time VideoItem sees them. Measured against a two-track file before any of this was written.
//
// What playbin3 does *not* do is let the user choose. It selects the first text stream it finds
// and renders it, which meant Reverie had been burning in subtitles with no way to turn them off.

void AudioEngine::rebuildSubtitleTracks(GstStreamCollection *collection)
{
    const QString previousId =
        (m_subtitleTrack >= 0 && m_subtitleTrack < m_subtitles.size())
            ? m_subtitles.at(m_subtitleTrack).id
            : QString();

    QVector<SubtitleTrack> found;
    QStringList videoIds;
    const guint count = gst_stream_collection_get_size(collection);
    for (guint i = 0; i < count; ++i) {
        GstStream *stream = gst_stream_collection_get_stream(collection, i);
        if (!stream)
            continue;
        const char *id = gst_stream_get_stream_id(stream);
        if (!id)
            continue;
        if (gst_stream_get_stream_type(stream) & GST_STREAM_TYPE_VIDEO) {
            videoIds << QString::fromUtf8(id);
            continue;
        }
        if (!(gst_stream_get_stream_type(stream) & GST_STREAM_TYPE_TEXT))
            continue;
        SubtitleTrack track;
        track.id = QString::fromUtf8(id);
        if (GstTagList *tags = gst_stream_get_tags(stream)) {
            gchar *value = nullptr;
            if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &value)) {
                track.language = QString::fromUtf8(value);
                g_free(value);
            }
            value = nullptr;
            if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &value)) {
                track.title = QString::fromUtf8(value);
                g_free(value);
            }
            gst_tag_list_unref(tags);
        }
        // An external file carries no language tag, which is how it is told apart from an
        // embedded track for labelling purposes.
        track.external = track.language.isEmpty() && !m_subtitleFile.isEmpty();
        found << track;
    }

    const bool sameTracks = (found.size() == m_subtitles.size()) &&
        [&] {
            for (int i = 0; i < found.size(); ++i)
                if (found.at(i).id != m_subtitles.at(i).id)
                    return false;
            return true;
        }();

    m_subtitles = found;
    m_videoStreamIds = videoIds;

    int wanted = -1;
    if (m_selectNewSubtitle && !m_subtitles.isEmpty()) {
        // The stream the freshly attached file added is the one that was not there before.
        wanted = m_subtitles.size() - 1;
        m_selectNewSubtitle = false;
        m_subtitlesWanted = true;
    } else if (!previousId.isEmpty()) {
        for (int i = 0; i < m_subtitles.size(); ++i)
            if (m_subtitles.at(i).id == previousId)
                wanted = i;
    }
    if (wanted < 0 && m_subtitlesWanted && !m_subtitles.isEmpty()) {
        // Prefer the language the user asked for; fall back to the first track, which is what
        // playbin3 would have done unaided.
        wanted = trackForLanguage(m_subtitles, m_preferredSubtitleLanguage);
        if (wanted < 0)
            wanted = 0;
    }

    const bool changed = !sameTracks || wanted != m_subtitleTrack;
    m_subtitleTrack = wanted;
    applySubtitleSelection();
    if (changed) {
        if (!m_subtitles.isEmpty())
            qInfo("subtitles: %d track(s), showing %s",
                  int(m_subtitles.size()),
                  wanted < 0 ? "none"
                             : qUtf8Printable(m_subtitles.at(wanted).language.isEmpty()
                                                  ? QStringLiteral("track 1")
                                                  : m_subtitles.at(wanted).language));
        emit subtitlesChanged();
    }
}

void AudioEngine::applySubtitleSelection()
{
    if (!m_pipeline || (m_videoStreamIds.isEmpty() && m_audioStreams.isEmpty()))
        return;
    // select-streams replaces the entire selection, so every stream that should keep playing has
    // to be named again each time. Leaving one out silently stops it rather than reporting
    // anything - the same quiet failure the tee used to produce on the speaker branch.
    GList *ids = nullptr;
    for (const QString &id : m_videoStreamIds)
        ids = g_list_append(ids, g_strdup(id.toUtf8().constData()));
    if (m_audioTrack >= 0 && m_audioTrack < m_audioStreams.size())
        ids = g_list_append(ids, g_strdup(m_audioStreams.at(m_audioTrack).id.toUtf8().constData()));
    if (m_subtitleTrack >= 0 && m_subtitleTrack < m_subtitles.size())
        ids = g_list_append(ids, g_strdup(m_subtitles.at(m_subtitleTrack).id.toUtf8().constData()));
    if (ids) {
        gst_element_send_event(m_pipeline, gst_event_new_select_streams(ids));
        g_list_free_full(ids, g_free);
    }
}

// Audio tracks. Same shape as subtitles, with one difference that matters: there is no "off".
// A file always plays some audio track, so the only questions are which one and how it is chosen
// when the user has not said.
void AudioEngine::rebuildAudioTracks(GstStreamCollection *collection)
{
    const QString previousId = (m_audioTrack >= 0 && m_audioTrack < m_audioStreams.size())
                                   ? m_audioStreams.at(m_audioTrack).id
                                   : QString();
    QVector<SubtitleTrack> found;
    const guint count = gst_stream_collection_get_size(collection);
    for (guint i = 0; i < count; ++i) {
        GstStream *stream = gst_stream_collection_get_stream(collection, i);
        if (!stream || !(gst_stream_get_stream_type(stream) & GST_STREAM_TYPE_AUDIO))
            continue;
        const char *id = gst_stream_get_stream_id(stream);
        if (!id)
            continue;
        SubtitleTrack track;
        track.id = QString::fromUtf8(id);
        if (GstTagList *tags = gst_stream_get_tags(stream)) {
            gchar *value = nullptr;
            if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &value)) {
                track.language = QString::fromUtf8(value);
                g_free(value);
            }
            value = nullptr;
            if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &value)) {
                track.title = QString::fromUtf8(value);
                g_free(value);
            }
            gst_tag_list_unref(tags);
        }
        found << track;
    }

    const bool same = (found.size() == m_audioStreams.size()) && [&] {
        for (int i = 0; i < found.size(); ++i)
            if (found.at(i).id != m_audioStreams.at(i).id)
                return false;
        return true;
    }();
    m_audioStreams = found;

    int wanted = -1;
    if (!previousId.isEmpty())
        for (int i = 0; i < m_audioStreams.size(); ++i)
            if (m_audioStreams.at(i).id == previousId)
                wanted = i;
    // The preference only decides anything when there is a choice to make. With one track it is
    // irrelevant, and forcing a mismatch would mean silence on a file whose only track is in
    // another language.
    if (wanted < 0 && m_audioStreams.size() > 1)
        wanted = trackForLanguage(m_audioStreams, effectiveAudioLanguage());
    if (wanted < 0 && !m_audioStreams.isEmpty())
        wanted = 0;

    const bool changed = !same || wanted != m_audioTrack;
    m_audioTrack = wanted;
    if (changed) {
        if (m_audioStreams.size() > 1 && wanted >= 0)
            qInfo("audio: %d tracks, playing %d (%s); preference is %s",
                  int(m_audioStreams.size()), wanted,
                  qUtf8Printable(m_audioStreams.at(wanted).language.isEmpty()
                                     ? QStringLiteral("no language tag")
                                     : m_audioStreams.at(wanted).language),
                  qUtf8Printable(effectiveAudioLanguage()));
        emit audioTracksChanged();
    }
}

QString AudioEngine::effectiveAudioLanguage() const
{
    return m_preferredAudioLanguage.isEmpty()
               ? QLocale::system().name().section(QLatin1Char('_'), 0, 0)
               : m_preferredAudioLanguage;
}

QVariantList AudioEngine::audioTracks() const
{
    QVariantList out;
    for (int i = 0; i < m_audioStreams.size(); ++i) {
        const SubtitleTrack &track = m_audioStreams.at(i);
        QString label;
        if (!track.language.isEmpty()) {
            const QLocale locale(track.language);
            const QString name = QLocale::languageToString(locale.language());
            label = (locale.language() == QLocale::C || name.isEmpty()) ? track.language : name;
        }
        if (label.isEmpty() && !track.title.isEmpty())
            label = track.title;
        if (label.isEmpty())
            label = tr("Track %1").arg(i + 1);
        else if (!track.title.isEmpty() && !track.language.isEmpty()
                 && track.title.compare(label, Qt::CaseInsensitive) != 0)
            label = tr("%1 - %2").arg(label, track.title);
        QVariantMap entry;
        entry.insert(QStringLiteral("label"), label);
        entry.insert(QStringLiteral("language"), track.language);
        out.append(entry);
    }
    return out;
}

void AudioEngine::setAudioTrack(int index)
{
    if (index < 0 || index >= m_audioStreams.size())
        return;
    m_audioTrack = index;
    applySubtitleSelection();
    qInfo("audio: switched to track %d (%s) on request", index,
          qUtf8Printable(m_audioStreams.at(index).language.isEmpty()
                             ? QStringLiteral("no language tag")
                             : m_audioStreams.at(index).language));
    emit audioTracksChanged();
}

void AudioEngine::setPreferredAudioLanguage(const QString &code)
{
    if (code == m_preferredAudioLanguage) {
        emit preferredAudioLanguageChanged();
        return;
    }
    m_preferredAudioLanguage = code;
    QSettings().setValue(QStringLiteral("audio/preferredLanguage"), code);
    emit preferredAudioLanguageChanged();
    // Anything already playing keeps the track it has; changing the preference mid-file and
    // having the dialogue switch language underneath you would be worse than waiting.
}

void AudioEngine::setPreferredSubtitleLanguage(const QString &code)
{
    if (code == m_preferredSubtitleLanguage) {
        emit preferredSubtitleLanguageChanged();
        return;
    }
    m_preferredSubtitleLanguage = code;
    QSettings().setValue(QStringLiteral("video/preferredSubtitleLanguage"), code);
    emit preferredSubtitleLanguageChanged();
}

QVariantList AudioEngine::languageChoices(bool subtitles) const
{
    // Deliberately a short list. The target user is not choosing from every ISO 639 code, and
    // a file whose language is not here still plays - the preference only breaks ties.
    static const char *codes[] = {"en", "es", "fr", "de", "it", "pt", "nl", "pl",
                                  "ru", "ja", "ko", "zh", "ar", "hi", "sv", "tr"};
    QVariantList out;
    QVariantMap first;
    first.insert(QStringLiteral("code"), QString());
    // For audio the fallback is the system locale, because something must play. For subtitles
    // it is simply the first track the file offers, since there is nothing to match against
    // until the user names a language.
    first.insert(QStringLiteral("label"),
                 subtitles ? tr("First available")
                           : tr("Match the system (%1)")
                                 .arg(QLocale::languageToString(QLocale::system().language())));
    out.append(first);
    for (const char *code : codes) {
        QVariantMap entry;
        entry.insert(QStringLiteral("code"), QString::fromLatin1(code));
        entry.insert(QStringLiteral("label"),
                     QLocale::languageToString(QLocale(QString::fromLatin1(code)).language()));
        out.append(entry);
    }
    return out;
}

QVariantList AudioEngine::subtitleTracks() const
{
    QVariantList out;
    for (int i = 0; i < m_subtitles.size(); ++i) {
        const SubtitleTrack &track = m_subtitles.at(i);
        QString label;
        if (!track.language.isEmpty()) {
            const QLocale locale(track.language);
            const QString name = QLocale::languageToString(locale.language());
            // An unrecognised code is more useful shown raw than as "C" or "Default".
            label = (locale.language() == QLocale::C || name.isEmpty())
                        ? track.language
                        : name;
        }
        if (label.isEmpty() && !track.title.isEmpty())
            label = track.title;
        if (label.isEmpty())
            label = track.external ? tr("Subtitle file") : tr("Track %1").arg(i + 1);
        // A title alongside a language distinguishes two tracks in the same one, which is
        // exactly the case a bare language label cannot.
        else if (!track.title.isEmpty() && !track.language.isEmpty()
                 && track.title.compare(label, Qt::CaseInsensitive) != 0)
            label = tr("%1 - %2").arg(label, track.title);

        QVariantMap entry;
        entry.insert(QStringLiteral("label"), label);
        entry.insert(QStringLiteral("language"), track.language);
        out.append(entry);
    }
    return out;
}

void AudioEngine::setSubtitleTrack(int index)
{
    if (index < -1 || index >= m_subtitles.size())
        index = -1;
    // The preference is what survives to the next file; the index does not, because which
    // track is number 0 is a property of the file rather than of the user's choice.
    if (m_subtitlesWanted != (index >= 0)) {
        m_subtitlesWanted = index >= 0;
        QSettings().setValue(QStringLiteral("video/subtitlesEnabled"), m_subtitlesWanted);
    }
    if (index == m_subtitleTrack) {
        emit subtitlesChanged();
        return;
    }
    m_subtitleTrack = index;
    applySubtitleSelection();
    emit subtitlesChanged();
}

int AudioEngine::trackForLanguage(const QVector<SubtitleTrack> &tracks, const QString &code) const
{
    if (code.isEmpty())
        return -1;
    // Codes arrive as two or three letters depending on the container, so they are compared
    // through QLocale rather than as strings: "fr" and "fra" are the same language.
    const QLocale::Language want = QLocale(code).language();
    if (want == QLocale::C)
        return -1;
    for (int i = 0; i < tracks.size(); ++i) {
        const QString lang = tracks.at(i).language;
        if (!lang.isEmpty() && QLocale(lang).language() == want)
            return i;
    }
    return -1;
}

void AudioEngine::setSubtitlesEnabled(bool on)
{
    if (!on) {
        setSubtitleTrack(-1);
        return;
    }
    int wanted = trackForLanguage(m_subtitles, m_preferredSubtitleLanguage);
    if (wanted < 0 && !m_subtitles.isEmpty())
        wanted = 0;
    if (wanted < 0) {
        // Nothing loaded that has subtitles. Record the wish so the next file honours it,
        // rather than making the button look broken.
        if (!m_subtitlesWanted) {
            m_subtitlesWanted = true;
            QSettings().setValue(QStringLiteral("video/subtitlesEnabled"), true);
        }
        emit subtitlesChanged();
        return;
    }
    setSubtitleTrack(wanted);
}

QString AudioEngine::sidecarSubtitleFor(const QString &uri) const
{
    const QUrl url(uri);
    if (!url.isLocalFile())
        return QString();
    const QFileInfo info(url.toLocalFile());
    const QDir dir = info.dir();
    const QString base = info.completeBaseName();
    // "clip.srt" first, then "clip.en.srt" and friends - a language-suffixed sidecar is the
    // common shape and matching only the exact stem would miss all of them.
    static const QStringList suffixes{QStringLiteral("srt"), QStringLiteral("ass"),
                                      QStringLiteral("ssa"), QStringLiteral("vtt"),
                                      QStringLiteral("sub")};
    for (const QString &suffix : suffixes) {
        const QString exact = dir.filePath(base + QLatin1Char('.') + suffix);
        if (QFileInfo::exists(exact))
            return exact;
    }
    const QFileInfoList candidates =
        dir.entryInfoList(QStringList{base + QStringLiteral(".*")}, QDir::Files);
    for (const QFileInfo &candidate : candidates)
        if (suffixes.contains(candidate.suffix().toLower()))
            return candidate.absoluteFilePath();
    return QString();
}

bool AudioEngine::addSubtitleFile(const QString &path)
{
    if (!m_pipeline || m_source.isEmpty() || path.isEmpty())
        return false;
    // The file dialog hands back a file:// URL; the command line and tests hand back a path.
    const QString local = path.startsWith(QStringLiteral("file://"))
                              ? QUrl(path).toLocalFile()
                              : path;
    const QString absolute = QFileInfo(local).absoluteFilePath();
    if (!QFileInfo::exists(absolute))
        return false;

    const QString uri = QUrl::fromLocalFile(absolute).toString();
    const bool wasPlaying = (m_state == Playing || m_state == Buffering);

    gint64 position = 0;
    if (!gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &position))
        position = 0;

    // playbin3 accepts suburi only below PAUSED. Setting it while playing is not an error and
    // not a warning - the property simply reads back as unset afterwards.
    gst_element_set_state(m_pipeline, GST_STATE_READY);
    gst_element_get_state(m_pipeline, nullptr, nullptr, 5 * GST_SECOND);
    g_object_set(m_pipeline, "suburi", uri.toUtf8().constData(), nullptr);
    m_subtitleFile = absolute;
    m_selectNewSubtitle = true;

    gst_element_set_state(m_pipeline, wasPlaying ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    gst_element_get_state(m_pipeline, nullptr, nullptr, 5 * GST_SECOND);
    bool sought = true;
    if (position > 0)
        sought = gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
                                         GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                                         position);
    applyOutputLevels();
    qInfo("subtitles: attached %s (was %s at %.1fs, seek %s)",
          qUtf8Printable(QFileInfo(absolute).fileName()),
          wasPlaying ? "playing" : "not playing",
          position / double(GST_SECOND), sought ? "ok" : "REFUSED");
    return true;
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
            // It arrives more than once - an external subtitle file produces a second, larger
            // collection - so this rebuilds rather than accumulates.
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
                rebuildAudioTracks(collection);
                rebuildSubtitleTracks(collection);
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
                    // Trimmed, as TagLib's path already was. A container whose title tag holds
                    // nothing but whitespace is not untagged as far as GStreamer is concerned,
                    // and an untrimmed blank propagated all the way to the window title, which
                    // read " - Reverie" with nothing before the dash.
                    const QString value = QString::fromUtf8(title).trimmed();
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
                    const QString value = QString::fromUtf8(artist).trimmed();
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
