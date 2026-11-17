#pragma once

#include <QAudioOutput>
#include <QIODevice>
#include <QMutex>
#include <QObject>
#include <atomic>
#include <cstdint>

class RoadAudioDevice : public QIODevice {
    Q_OBJECT
public:
    explicit RoadAudioDevice(QObject* parent = nullptr);

    void setPaused(bool p) { m_paused.store(p); }
    void setEngineEnabled(bool on) { m_engineEnabled.store(on); }
    void setRadioStation(int st) { m_radioStation.store(st); } // 0=Off, 1=Chillwave, 2=LoFi, 3=Ambient
    void setCarStyle(int car) { m_carStyle.store(car); }        // 0=Classic V8, 1=Comfy Wagon, 2=Fun Sport
    void updateTelemetry(float speedKmh, float rpmNorm, float throttle, float rainIntensity);
    void triggerHornOrChime(int kind);

protected:
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char* data, qint64 len) override;
    qint64 bytesAvailable() const override;

private:
    float nextNoise();
    float sampleEngineAndRoad(float dt);
    float sampleRadio(float dt, int station);

    QMutex m_mutex;
    uint32_t m_rng = 0x9E3779B9u;

    // Smoothed vehicle audio state
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_engineEnabled{true};
    std::atomic<int> m_radioStation{1};
    std::atomic<int> m_carStyle{0};
    std::atomic<float> m_speedKmh{60.0f};
    std::atomic<float> m_rpmNorm{0.35f};
    std::atomic<float> m_throttle{0.3f};
    std::atomic<float> m_rain{0.0f};

    float m_engPhase1 = 0.0f;
    float m_engPhase2 = 0.0f;
    float m_engSubPhase = 0.0f;
    float m_roadLp = 0.0f;
    float m_rainLp = 0.0f;

    // Radio synthesizer state
    double m_beatPos = 0.0;
    float m_padPhaseA = 0.0f;
    float m_padPhaseB = 0.0f;
    float m_padPhaseC = 0.0f;
    float m_arpPhase = 0.0f;
    float m_bassPhase = 0.0f;

    float m_chimeTimer = 0.0f;
    int m_chimeKind = 0;
    float m_chimePhase = 0.0f;
};

class RoadAudio : public QObject {
    Q_OBJECT
public:
    explicit RoadAudio(QObject* parent = nullptr);
    ~RoadAudio() override;

    void setPaused(bool p);
    void setEngineEnabled(bool on);
    void setRadioStation(int station);
    void setCarStyle(int car);
    void updateTelemetry(float speedKmh, float rpmNorm, float throttle, float rainIntensity);
    void triggerChime(int kind = 0);

private:
    QAudioOutput* m_output = nullptr;
    RoadAudioDevice* m_device = nullptr;
};
