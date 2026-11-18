#pragma once

#include "RoadAudio.h"
#include "WorldSim.h"

#include <QElapsedTimer>
#include <QQuickFramebufferObject>
#include <QTimer>

class RoadView : public QQuickFramebufferObject {
    Q_OBJECT

    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool autoDrive READ autoDrive WRITE setAutoDrive NOTIFY stateChanged)
    Q_PROPERTY(bool tiltSteerEnabled READ tiltSteerEnabled WRITE setTiltSteerEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool invertTilt READ invertTilt WRITE setInvertTilt NOTIFY stateChanged)
    Q_PROPERTY(float steerSensitivity READ steerSensitivity WRITE setSteerSensitivity NOTIFY stateChanged)
    Q_PROPERTY(float cruiseSpeedKmh READ cruiseSpeedKmh WRITE setCruiseSpeedKmh NOTIFY stateChanged)

    Q_PROPERTY(int carIndex READ carIndex WRITE setCarIndex NOTIFY stateChanged)
    Q_PROPERTY(QString carName READ carName NOTIFY stateChanged)
    Q_PROPERTY(int cameraMode READ cameraMode WRITE setCameraMode NOTIFY stateChanged)
    Q_PROPERTY(int lockedBiome READ lockedBiome WRITE setLockedBiome NOTIFY stateChanged)
    Q_PROPERTY(int weatherMode READ weatherMode WRITE setWeatherMode NOTIFY stateChanged)
    Q_PROPERTY(float timeOfDay READ timeOfDay WRITE setTimeOfDay NOTIFY hudChanged)
    Q_PROPERTY(bool autoTimeCycle READ autoTimeCycle WRITE setAutoTimeCycle NOTIFY stateChanged)

    Q_PROPERTY(bool headlights READ headlights WRITE setHeadlights NOTIFY stateChanged)
    Q_PROPERTY(bool wipers READ wipers WRITE setWipers NOTIFY stateChanged)
    Q_PROPERTY(int radioStation READ radioStation WRITE setRadioStation NOTIFY stateChanged)
    Q_PROPERTY(QString radioStationName READ radioStationName NOTIFY stateChanged)
    Q_PROPERTY(bool engineSound READ engineSound WRITE setEngineSound NOTIFY stateChanged)

    // Live HUD / Telemetry properties
    Q_PROPERTY(int speedKmh READ speedKmh NOTIFY hudChanged)
    Q_PROPERTY(QString distanceText READ distanceText NOTIFY hudChanged)
    Q_PROPERTY(QString biomeTitle READ biomeTitle NOTIFY hudChanged)
    Q_PROPERTY(QString bannerText READ bannerText NOTIFY hudChanged)
    Q_PROPERTY(QString clockText READ clockText NOTIFY hudChanged)

public:
    explicit RoadView(QQuickItem* parent = nullptr);
    Renderer* createRenderer() const override;

    // Input invokables
    Q_INVOKABLE void setTiltGravity(float gx, float gy, float gz, bool rotated90);
    Q_INVOKABLE void calibrateTiltZero();
    Q_INVOKABLE void setTouchSteer(float val);
    Q_INVOKABLE void setGasHeld(bool held);
    Q_INVOKABLE void setBrakeHeld(bool held);
    Q_INVOKABLE void addViewDrag(float dxPx, float dyPx);
    Q_INVOKABLE void cycleCamera();
    Q_INVOKABLE void cycleRadio();
    Q_INVOKABLE void cycleCar();

    void syncToSnapshot(DriveSnapshot& dst) const;

    bool paused() const { return m_paused; }
    void setPaused(bool p);

    bool autoDrive() const { return m_sim.autoDrive(); }
    void setAutoDrive(bool on);

    bool tiltSteerEnabled() const { return m_tiltEnabled; }
    void setTiltSteerEnabled(bool on);

    bool invertTilt() const { return m_invertTilt; }
    void setInvertTilt(bool inv);

    float steerSensitivity() const { return m_steerSens; }
    void setSteerSensitivity(float s);

    float cruiseSpeedKmh() const { return m_sim.cruiseSpeedKmh(); }
    void setCruiseSpeedKmh(float kmh);

    int carIndex() const { return m_sim.carIndex(); }
    void setCarIndex(int idx);
    QString carName() const { return m_sim.carName(); }

    int cameraMode() const { return m_sim.cameraMode(); }
    void setCameraMode(int m);

    int lockedBiome() const { return m_sim.lockedBiome(); }
    void setLockedBiome(int b);

    int weatherMode() const { return m_sim.weatherMode(); }
    void setWeatherMode(int w);

    float timeOfDay() const { return m_sim.timeOfDay(); }
    void setTimeOfDay(float h);

    bool autoTimeCycle() const { return m_sim.autoTimeCycle(); }
    void setAutoTimeCycle(bool on);

    bool headlights() const { return m_sim.headlights(); }
    void setHeadlights(bool on);

    bool wipers() const { return m_sim.wipers(); }
    void setWipers(bool on);

    int radioStation() const { return m_radioStation; }
    void setRadioStation(int st);
    QString radioStationName() const;

    bool engineSound() const { return m_engineSound; }
    void setEngineSound(bool on);

    int speedKmh() const { return static_cast<int>(m_sim.speedKmh() + 0.5f); }
    QString distanceText() const;
    QString biomeTitle() const { return m_sim.currentBiomeTitle(); }
    QString bannerText() const { return m_sim.bannerText(); }
    QString clockText() const;

signals:
    void pausedChanged();
    void stateChanged();
    void hudChanged();

private slots:
    void onTick();

private:
    RoadAudio m_audio;
    WorldSim m_sim;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastNs = 0;

    bool m_paused = false;
    bool m_tiltEnabled = true;
    bool m_invertTilt = false;
    float m_steerSens = 1.0f;
    bool m_hasTiltSample = false;
    float m_fgx = 0.0f;
    float m_fgy = 0.0f;
    float m_zeroUx = 0.0f;
    float m_zeroUy = 1.0f;
    float m_tiltSteerVal = 0.0f;
    float m_touchSteerVal = 0.0f;
    bool m_gasHeld = false;
    bool m_brakeHeld = false;

    int m_radioStation = 1;
    bool m_engineSound = true;
};
