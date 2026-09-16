#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <vector>
#include <cstring>

// Audio arrives on a GStreamer streaming thread and is consumed on the QQuick render
// thread, so it has to cross threads. A small mutex-guarded ring is enough: the critical
// section is a memcpy of a few hundred floats, and the render thread never blocks long.
//
// projectM only ever wants "the most recent N samples", so old data is simply overwritten.
class AudioRingBuffer
{
public:
    static constexpr int kChannels = 2;
    static constexpr int kCapacityFrames = 8192;

    void write(const float *interleaved, int frames)
    {
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < frames; ++i) {
            m_data[m_writePos * kChannels] = interleaved[i * kChannels];
            m_data[m_writePos * kChannels + 1] = interleaved[i * kChannels + 1];
            m_writePos = (m_writePos + 1) % kCapacityFrames;
        }
        m_written += frames;
    }

    // Copies the newest `frames` frames. Returns false when nothing has arrived recently,
    // which is the cue to fall back to silence rather than replay stale audio.
    bool readLatest(float *out, int frames)
    {
        QMutexLocker lock(&m_mutex);
        if (m_written < frames)
            return false;
        int pos = (m_writePos - frames + kCapacityFrames) % kCapacityFrames;
        for (int i = 0; i < frames; ++i) {
            out[i * kChannels] = m_data[pos * kChannels];
            out[i * kChannels + 1] = m_data[pos * kChannels + 1];
            pos = (pos + 1) % kCapacityFrames;
        }
        return true;
    }

    void reset()
    {
        QMutexLocker lock(&m_mutex);
        std::memset(m_data.data(), 0, m_data.size() * sizeof(float));
        m_writePos = 0;
        m_written = 0;
    }

private:
    QMutex m_mutex;
    std::vector<float> m_data = std::vector<float>(kCapacityFrames * kChannels, 0.0f);
    int m_writePos = 0;
    qint64 m_written = 0;
};
