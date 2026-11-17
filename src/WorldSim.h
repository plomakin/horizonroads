#pragma once

#include "RoadAudio.h"
#include <QString>
#include <cstdint>
#include <vector>

enum class BiomeId : int {
    PacificRedwoods = 0,
    HeartlandFarms  = 1,
    SonoranCanyon   = 2,
    QuietSuburbia   = 3,
    DowntownSkyline = 4,
    AlpineSummit    = 5
};

enum class WeatherKind : int {
    Clear     = 0,
    Rain      = 1,
    MistyFog  = 2,
    Snowfall  = 3
};

enum class PropKind : int {
    RedwoodTree = 0,
    MapleTree,
    SaguaroCactus,
    RedBarnSilo,
    Windmill,
    SuburbanHouse,
    Skyscraper,
    DesertMesa,
    Streetlight,
    HighwaySign,
    SnowBoulder,
    FenceRow
};

struct Color3 {
    float r, g, b;
};

struct BiomePalette {
    Color3 grassNear;
    Color3 grassFar;
    Color3 dirtShoulder;
    Color3 mountainCol;
    float treeDensity;
    float hillScale;
    float mountainHeight;
};

struct TrafficCar {
    double s = 0.0;        // Distance along road (m)
    float laneOffset = 0.0f;
    float speedMs = 22.0f; // Positive = same dir, Negative = oncoming
    int bodyType = 0;      // 0=Sedan, 1=Van/SUV, 2=Truck
    Color3 color{0.8f, 0.2f, 0.2f};
};

struct WeatherDrop {
    float x, y, z; // Camera-local offset
    float speed;
};

struct DriveSnapshot {
    double playerS = 0.0;
    float playerLat = 2.6f;     // Right-hand lane (+2.6 m)
    float playerHeading = 0.0f; // Relative to road tangent (rad)
    float speedKmh = 85.0f;
    float rpmNorm = 0.4f;
    float steerWheelAngleDeg = 0.0f; // -420 .. +420 deg visual steering wheel rotation
    float bodyRollRad = 0.0f;
    float bodyPitchRad = 0.0f;
    float suspBobY = 0.0f;

    int carIndex = 0;           // 0=Classic '68, 1=Comfy '94 Wagon, 2=Fun Apex GT-RS
    int cameraMode = 0;         // 0=Cockpit Wheel, 1=3rd Person Chase, 2=Bonnet
    float freeLookYaw = 0.0f;
    float freeLookPitch = 0.0f;

    bool headlightsOn = true;
    bool wipersOn = false;
    float wiperPhase = 0.0f;
    bool autoDrive = true;

    // Environment & Sky
    float timeOfDay = 17.6f;    // 0..24 hours (17.6 = Golden Sunset!)
    WeatherKind weather = WeatherKind::Clear;
    float rainAmount = 0.0f;
    float snowAmount = 0.0f;
    float fogDensity = 0.012f;

    BiomeId biomeA = BiomeId::PacificRedwoods;
    BiomeId biomeB = BiomeId::PacificRedwoods;
    float biomeBlend = 0.0f;
    BiomePalette blendedPalette{};

    Color3 skyZenith{0.12f, 0.24f, 0.55f};
    Color3 skyHorizon{0.95f, 0.55f, 0.30f};
    Color3 sunColor{1.0f, 0.85f, 0.65f};
    float sunDirX = 0.3f, sunDirY = 0.25f, sunDirZ = 0.9f;
    float nightFactor = 0.0f;   // 0=Day, 1=Night
    float worldTimeSec = 0.0f;

    std::vector<TrafficCar> traffic;
    std::vector<WeatherDrop> precip;
};

class WorldSim {
public:
    explicit WorldSim(RoadAudio* audio);

    void update(float dt, float steerInput, float throttleInput, float brakeInput);

    // Deterministic road spline & terrain queries (used by both physics & 3D renderer!)
    static float roadCenterX(double s);
    static float roadSlopeDxDs(double s);
    static float roadElevationY(double s);
    static float roadSlopeDyDs(double s);
    static BiomeId biomeAtDistance(double s, int lockedBiome, float* outBlend = nullptr, BiomeId* outNext = nullptr);
    static BiomePalette paletteForBiome(BiomeId b);
    static float terrainSideProfile(BiomeId b, double s, float absLat);

    void buildSnapshot(DriveSnapshot& out) const;

    // Controls & Configuration
    void setAutoDrive(bool on) { m_autoDrive = on; }
    bool autoDrive() const { return m_autoDrive; }

    void setCruiseSpeedKmh(float kmh) { m_cruiseTargetKmh = kmh; }
    float cruiseSpeedKmh() const { return m_cruiseTargetKmh; }

    void setCarIndex(int idx);
    int carIndex() const { return m_carIndex; }
    QString carName() const;

    void setCameraMode(int mode) { m_cameraMode = mode % 3; }
    int cameraMode() const { return m_cameraMode; }
    void cycleCamera() { m_cameraMode = (m_cameraMode + 1) % 3; }

    void setLockedBiome(int b) { m_lockedBiome = b; } // -1 = Auto Road-Trip across states!
    int lockedBiome() const { return m_lockedBiome; }
    QString currentBiomeTitle() const { return m_currentBiomeTitle; }
    QString bannerText() const { return m_bannerTimer > 0.0f ? m_bannerText : QString(); }

    void setTimeOfDay(float hours);
    float timeOfDay() const { return m_timeOfDay; }
    void setAutoTimeCycle(bool on) { m_autoTime = on; }
    bool autoTimeCycle() const { return m_autoTime; }

    void setWeatherMode(int w); // -1 = Auto Weather, 0=Clear, 1=Rain, 2=Fog, 3=Snow
    int weatherMode() const { return m_weatherMode; }

    void setHeadlights(bool on) { m_headlights = on; }
    bool headlights() const { return m_headlights; }

    void setWipers(bool on) { m_wipersManual = on; }
    bool wipers() const { return m_wipersManual || m_rainAmount > 0.25f; }

    void addFreeLook(float dYawRad, float dPitchRad);
    void recenterFreeLook() { m_freeLookYaw = 0.0f; m_freeLookPitch = 0.0f; }

    float speedKmh() const { return m_speedMs * 3.6f; }
    double distanceKm() const { return m_s * 0.001; }

private:
    void initTraffic();
    void updateEnvironment(float dt);
    static QString titleForBiome(BiomeId b);

    RoadAudio* m_audio = nullptr;

    double m_s = 0.0;
    float m_lat = 2.65f;
    float m_heading = 0.0f;
    float m_speedMs = 24.0f;
    float m_steerSmoothed = 0.0f;
    float m_bodyRoll = 0.0f;
    float m_bodyPitch = 0.0f;
    float m_worldTime = 0.0f;

    bool m_autoDrive = true;
    float m_cruiseTargetKmh = 95.0f;
    int m_carIndex = 0;
    int m_cameraMode = 0; // Default: Cockpit behind steering wheel!

    int m_lockedBiome = -1;
    BiomeId m_lastAnnouncedBiome = BiomeId::PacificRedwoods;
    QString m_currentBiomeTitle;
    QString m_bannerText;
    float m_bannerTimer = 0.0f;

    float m_timeOfDay = 17.4f; // Start at breathtaking Golden Sunset!
    bool m_autoTime = true;
    int m_weatherMode = -1;    // Auto weather
    WeatherKind m_activeWeather = WeatherKind::Clear;
    float m_rainAmount = 0.0f;
    float m_snowAmount = 0.0f;
    float m_fogDensity = 0.010f;

    bool m_headlights = true;
    bool m_wipersManual = false;
    float m_wiperPhase = 0.0f;

    float m_freeLookYaw = 0.0f;
    float m_freeLookPitch = 0.0f;
    float m_freeLookIdleTimer = 0.0f;

    std::vector<TrafficCar> m_traffic;
    std::vector<WeatherDrop> m_precip;
    uint32_t m_rng = 0xA53C9E17u;
    float rand01();
};
