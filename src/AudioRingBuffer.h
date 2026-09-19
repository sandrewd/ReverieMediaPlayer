#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <vector>
#include <cstring>

// Audio arrives on a GStreamer streaming thread and is consumed on the QQuick render
// thread, so it has to cross threads. A small mutex-guarded ring is enough: the critical
// section is a memcpy of a few hundred floats, and the render thread never blocks long.
//
// The consumer reads *continuously* rather than taking the newest N frames each time. That
// distinction turned out to matter: reading "the latest 512" once per rendered frame discards
// the audio in between - 5.1 ms of every 16.7 ms at 60fps - and joins what remains. Measured on
// real music, the step across each join is 6.9 times a normal sample-to-sample step, sixty times
// a second. A discontinuity at that rate is broadband energy in every frame's spectrum, which is
// what a beat detector responds to, so beat-triggered presets retriggered constantly.
class AudioRingBuffer
{
public:
    static constexpr int kChannels = 2;
    static constexpr int kCapacityFrames = 8192;

    void write(const float *interleaved, int frames)
    {
        write(interleaved, frames, kChannels);
    }

    // Takes audio at its native channel count and stores stereo, so the playback path never has
    // to be forced to two channels just to feed the visualiser. Mono is duplicated; anything
    // wider keeps the front pair, which is what a 2D waveform can represent anyway.
    void write(const float *interleaved, int frames, int channels)
    {
        if (!interleaved || frames <= 0 || channels <= 0)
            return;
        const int right = channels > 1 ? 1 : 0;
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < frames; ++i) {
            m_data[m_writePos * kChannels] = interleaved[i * channels];
            m_data[m_writePos * kChannels + 1] = interleaved[i * channels + right];
            m_writePos = (m_writePos + 1) % kCapacityFrames;
        }
        m_written += frames;
        m_available += frames;
        // A consumer that has fallen more than the whole buffer behind can only be given what
        // is still here; anything older has already been overwritten.
        if (m_available > kCapacityFrames)
            m_available = kCapacityFrames;
    }

    // Copies every frame written since the last call, up to `maxFrames`, and returns how many.
    // Zero means nothing new has arrived - the cue to feed projectM nothing at all rather than
    // replay stale audio, which would be another discontinuity.
    //
    // Continuity is the whole point: consecutive calls return consecutive samples, so the
    // waveform projectM analyses is the one that was played.
    int readContinuous(float *out, int maxFrames)
    {
        QMutexLocker lock(&m_mutex);
        // Behind by more than one read? Drop the oldest and stay current. This is the only
        // place a discontinuity can now occur, and it happens when the renderer has genuinely
        // stalled - not on every single frame, which was the bug.
        if (m_available > maxFrames)
            m_available = maxFrames;
        const int frames = m_available;
        if (frames <= 0)
            return 0;
        int pos = (m_writePos - m_available + kCapacityFrames * 2) % kCapacityFrames;
        for (int i = 0; i < frames; ++i) {
            out[i * kChannels] = m_data[pos * kChannels];
            out[i * kChannels + 1] = m_data[pos * kChannels + 1];
            pos = (pos + 1) % kCapacityFrames;
        }
        m_available -= frames;
        return frames;
    }

    void reset()
    {
        QMutexLocker lock(&m_mutex);
        std::memset(m_data.data(), 0, m_data.size() * sizeof(float));
        m_writePos = 0;
        m_written = 0;
        m_available = 0;
    }

private:
    QMutex m_mutex;
    std::vector<float> m_data = std::vector<float>(kCapacityFrames * kChannels, 0.0f);
    int m_writePos = 0;
    qint64 m_written = 0;
    // Frames written but not yet handed to the consumer.
    int m_available = 0;
};
