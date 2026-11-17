#include "RoadAudio.h"

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int kSampleRate = 44100;
constexpr float kTwoPi = 6.28318530718f;

inline float softTri(float phase) {
    float p = phase / kTwoPi;
    p -= std::floor(p);
    return 1.0f - 4.0f * std::abs(p - 0.5f);
}

inline float softSaw(float phase) {
    float p = phase / kTwoPi;
    p -= std::floor(p);
    return 2.0f * p - 1.0f;
}
} // namespace

RoadAudioDevice::RoadAudioDevice(QObject* parent) : QIODevice(parent) {
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

void RoadAudioDevice::updateTelemetry(float speedKmh, float rpmNorm, float throttle, float rainIntensity) {
    m_speedKmh.store(speedKmh);
    m_rpmNorm.store(rpmNorm);
    m_throttle.store(throttle);
    m_rain.store(rainIntensity);
}

void RoadAudioDevice::triggerHornOrChime(int kind) {
    QMutexLocker lock(&m_mutex);
    m_chimeKind = kind;
    m_chimeTimer = 0.28f;
    m_chimePhase = 0.0f;
}

float RoadAudioDevice::nextNoise() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return (static_cast<float>(m_rng & 0xFFFFu) / 32768.0f) - 1.0f;
}

float RoadAudioDevice::sampleEngineAndRoad(float dt) {
    const int car = m_carStyle.load();
    const float spd = std::clamp(m_speedKmh.load(), 0.0f, 240.0f);
    const float rpm = std::clamp(m_rpmNorm.load(), 0.08f, 1.0f);
    const float thr = std::clamp(m_throttle.load(), 0.0f, 1.0f);
    const float rain = std::clamp(m_rain.load(), 0.0f, 1.0f);

    // Distinct engine firing frequencies for the 3 cars:
    // 0: '68 Classic V8 (deep warm burble, 36..125 Hz)
    // 1: '94 Comfy Wagon (hushed smooth touring purr, 44..135 Hz)
    // 2: Apex Sport GT-RS (crisp flat-6 sing, 58..230 Hz)
    float baseHz = 45.0f;
    float gain = 0.12f;
    if (car == 0) {
        baseHz = 34.0f + rpm * 88.0f;
        gain = 0.13f + thr * 0.05f;
    } else if (car == 1) {
        baseHz = 42.0f + rpm * 82.0f;
        gain = 0.085f + thr * 0.035f;
    } else {
        baseHz = 56.0f + rpm * 168.0f;
        gain = 0.125f + thr * 0.065f;
    }

    m_engPhase1 += kTwoPi * baseHz * dt;
    m_engPhase2 += kTwoPi * (baseHz * (car == 0 ? 1.505f : 2.0f)) * dt;
    m_engSubPhase += kTwoPi * (baseHz * 0.5f) * dt;
    if (m_engPhase1 > kTwoPi) m_engPhase1 -= kTwoPi;
    if (m_engPhase2 > kTwoPi) m_engPhase2 -= kTwoPi;
    if (m_engSubPhase > kTwoPi) m_engSubPhase -= kTwoPi;

    float eng = 0.0f;
    if (car == 0) {
        // Warm V8 cross-plane lope
        float lope = 0.82f + 0.18f * std::sin(m_engSubPhase);
        eng = (0.62f * std::sin(m_engPhase1) + 0.28f * softTri(m_engPhase2) + 0.18f * std::sin(m_engSubPhase)) * lope * gain;
    } else if (car == 1) {
        // Soft insulated touring cabin hum
        eng = (0.72f * std::sin(m_engPhase1) + 0.22f * std::sin(m_engPhase2)) * gain;
    } else {
        // Sport flat-6 harmonic rasp
        eng = (0.45f * softSaw(m_engPhase1) + 0.38f * softTri(m_engPhase2) + 0.25f * std::sin(m_engPhase1)) * gain * 0.85f;
    }

    // Tire-on-asphalt + wind cabin whoosh (low-pass filtered pink/brown noise)
    float nz = nextNoise();
    float spdNorm = spd / 160.0f;
    m_roadLp += 0.065f * (nz - m_roadLp);
    float roadWhoosh = m_roadLp * std::min(0.18f, spdNorm * 0.13f);

    // Cozy rain-on-windshield patter
    float rainOut = 0.0f;
    if (rain > 0.02f) {
        m_rainLp += 0.24f * (nz - m_rainLp);
        float dropPop = ((m_rng & 0x3FFu) == 0u) ? (nz * 0.18f) : 0.0f;
        rainOut = (m_rainLp * 0.11f + dropPop) * rain;
    }

    return eng + roadWhoosh + rainOut;
}

float RoadAudioDevice::sampleRadio(float dt, int station) {
    if (station <= 0) return 0.0f;

    const float bpm = (station == 1) ? 92.0f : (station == 2 ? 74.0f : 60.0f);
    const double stepRate = (bpm / 60.0) * 4.0;
    m_beatPos += stepRate * dt;
    if (m_beatPos >= 64.0) m_beatPos -= 64.0;

    const int step = static_cast<int>(m_beatPos) & 63;
    const int chordIdx = (step / 16) & 3;
    const float stepFrac = static_cast<float>(m_beatPos - std::floor(m_beatPos));

    // Lush nostalgic road-trip chord progressions (Fmaj7 -> G6 -> Em7 -> Am9)
    static const float roots[4]  = {174.61f, 196.00f, 164.81f, 220.00f};
    static const float thirds[4] = {220.00f, 246.94f, 196.00f, 261.63f};
    static const float fifths[4] = {261.63f, 293.66f, 246.94f, 329.63f};

    m_padPhaseA += kTwoPi * roots[chordIdx] * dt;
    m_padPhaseB += kTwoPi * thirds[chordIdx] * dt;
    m_padPhaseC += kTwoPi * fifths[chordIdx] * dt;
    if (m_padPhaseA > kTwoPi) m_padPhaseA -= kTwoPi;
    if (m_padPhaseB > kTwoPi) m_padPhaseB -= kTwoPi;
    if (m_padPhaseC > kTwoPi) m_padPhaseC -= kTwoPi;

    float pad = (std::sin(m_padPhaseA) + 0.8f * std::sin(m_padPhaseB) + 0.7f * softTri(m_padPhaseC)) * 0.052f;

    if (station == 3) {
        // Station 3: Pure Horizon Ambient Warm Pad
        return pad * 1.25f;
    }

    // Gentle Rhodes / Chillwave arpeggio + warm sub-bass
    static const int arpPick[8] = {0, 1, 2, 1, 2, 1, 0, 2};
    const float notes[3] = {roots[chordIdx] * 2.0f, thirds[chordIdx] * 2.0f, fifths[chordIdx] * 2.0f};
    float arpFreq = notes[arpPick[step & 7]];
    m_arpPhase += kTwoPi * arpFreq * dt;
    if (m_arpPhase > kTwoPi) m_arpPhase -= kTwoPi;

    float bellEnv = std::exp(-3.4f * stepFrac);
    float arp = (station == 2)
                    ? (std::sin(m_arpPhase) * bellEnv * 0.065f) // Lo-Fi Rhodes electric piano
                    : (softTri(m_arpPhase) * bellEnv * 0.060f); // Sunset Chillwave pluck

    m_bassPhase += kTwoPi * (roots[chordIdx] * 0.5f) * dt;
    if (m_bassPhase > kTwoPi) m_bassPhase -= kTwoPi;
    float bass = std::sin(m_bassPhase) * 0.075f;

    // Soft brushed lo-fi beat on Stations 1 & 2
    float perc = 0.0f;
    if ((step % 8) == 0) {
        perc += std::sin(stepFrac * 55.0f) * std::exp(-9.0f * stepFrac) * 0.09f;
    } else if ((step % 8) == 4) {
        perc += nextNoise() * std::exp(-11.0f * stepFrac) * 0.038f;
    }

    return pad + arp + bass + perc;
}

qint64 RoadAudioDevice::readData(char* data, qint64 maxlen) {
    const int samples = static_cast<int>(maxlen / sizeof(int16_t));
    int16_t* out = reinterpret_cast<int16_t*>(data);

    if (m_paused.load()) {
        std::memset(data, 0, static_cast<size_t>(samples) * sizeof(int16_t));
        return static_cast<qint64>(samples) * sizeof(int16_t);
    }

    const float dt = 1.0f / static_cast<float>(kSampleRate);
    const bool engOn = m_engineEnabled.load();
    const int station = m_radioStation.load();

    QMutexLocker lock(&m_mutex);
    for (int i = 0; i < samples; ++i) {
        float mix = 0.0f;
        if (engOn) mix += sampleEngineAndRoad(dt);
        if (station > 0) mix += sampleRadio(dt, station);

        if (m_chimeTimer > 0.0f) {
            m_chimeTimer -= dt;
            float cf = (m_chimeKind == 0) ? 523.25f : 392.0f;
            m_chimePhase += kTwoPi * cf * dt;
            mix += std::sin(m_chimePhase) * (m_chimeTimer / 0.28f) * 0.14f;
        }

        mix = std::tanh(mix * 0.92f);
        out[i] = static_cast<int16_t>(std::clamp(mix * 30000.0f, -32700.0f, 32700.0f));
    }
    return static_cast<qint64>(samples) * sizeof(int16_t);
}

qint64 RoadAudioDevice::writeData(const char*, qint64) { return 0; }
qint64 RoadAudioDevice::bytesAvailable() const { return 4096 + QIODevice::bytesAvailable(); }

RoadAudio::RoadAudio(QObject* parent) : QObject(parent) {
    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(1);
    fmt.setSampleSize(16);
    fmt.setCodec("audio/pcm");
    fmt.setByteOrder(QAudioFormat::LittleEndian);
    fmt.setSampleType(QAudioFormat::SignedInt);

    m_device = new RoadAudioDevice(this);
    QAudioDeviceInfo chosen;
    const auto devices = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const auto& dev : devices) {
        if (dev.deviceName().contains(QStringLiteral("pulse"), Qt::CaseInsensitive) ||
            dev.deviceName() == QStringLiteral("default")) {
            if (dev.isFormatSupported(fmt)) {
                chosen = dev;
                break;
            }
        }
    }
    if (chosen.isNull()) {
        QAudioDeviceInfo def = QAudioDeviceInfo::defaultOutputDevice();
        if (!def.isNull() && def.isFormatSupported(fmt)) chosen = def;
    }
    if (!chosen.isNull()) {
        m_output = new QAudioOutput(chosen, fmt, this);
        m_output->setBufferSize(4096);
        m_output->start(m_device);
    }
}

RoadAudio::~RoadAudio() {
    if (m_output) m_output->stop();
}

void RoadAudio::setPaused(bool p) { if (m_device) m_device->setPaused(p); }
void RoadAudio::setEngineEnabled(bool on) { if (m_device) m_device->setEngineEnabled(on); }
void RoadAudio::setRadioStation(int station) { if (m_device) m_device->setRadioStation(station); }
void RoadAudio::setCarStyle(int car) { if (m_device) m_device->setCarStyle(car); }
void RoadAudio::updateTelemetry(float speedKmh, float rpmNorm, float throttle, float rainIntensity) {
    if (m_device) m_device->updateTelemetry(speedKmh, rpmNorm, throttle, rainIntensity);
}
void RoadAudio::triggerChime(int kind) { if (m_device) m_device->triggerHornOrChime(kind); }
