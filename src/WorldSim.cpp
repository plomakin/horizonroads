#include "WorldSim.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kBiomeLengthMeters = 1500.0;
constexpr double kTransitionMeters  = 240.0;

inline Color3 lerpColor(const Color3& a, const Color3& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
} // namespace

WorldSim::WorldSim(RoadAudio* audio) : m_audio(audio) {
    m_currentBiomeTitle = titleForBiome(BiomeId::PacificRedwoods);
    m_bannerText = QStringLiteral("ENTERING OREGON COAST · PACIFIC REDWOODS");
    m_bannerTimer = 4.0f;
    initTraffic();

    m_precip.resize(180);
    for (auto& d : m_precip) {
        d.x = (rand01() * 2.0f - 1.0f) * 14.0f;
        d.y = rand01() * 9.0f;
        d.z = 2.0f + rand01() * 32.0f;
        d.speed = 0.8f + rand01() * 0.5f;
    }
}

float WorldSim::rand01() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xFFFFu) / 65535.0f;
}

float WorldSim::roadCenterX(double s) {
    return static_cast<float>(
        38.0 * std::sin(s * 0.0042) +
        19.0 * std::sin(s * 0.0107 + 1.3) +
        7.5  * std::cos(s * 0.0230 - 0.7)
    );
}

float WorldSim::roadSlopeDxDs(double s) {
    return static_cast<float>(
        38.0 * 0.0042 * std::cos(s * 0.0042) +
        19.0 * 0.0107 * std::cos(s * 0.0107 + 1.3) -
        7.5  * 0.0230 * std::sin(s * 0.0230 - 0.7)
    );
}

float WorldSim::roadElevationY(double s) {
    return static_cast<float>(
        9.5 * std::sin(s * 0.0055) +
        4.2 * std::cos(s * 0.0135 + 0.8)
    );
}

float WorldSim::roadSlopeDyDs(double s) {
    return static_cast<float>(
        9.5 * 0.0055 * std::cos(s * 0.0055) -
        4.2 * 0.0135 * std::sin(s * 0.0135 + 0.8)
    );
}

BiomeId WorldSim::biomeAtDistance(double s, int lockedBiome, float* outBlend, BiomeId* outNext) {
    if (lockedBiome >= 0 && lockedBiome <= 5) {
        BiomeId b = static_cast<BiomeId>(lockedBiome);
        if (outBlend) *outBlend = 0.0f;
        if (outNext) *outNext = b;
        return b;
    }
    const double pos = std::max(0.0, s);
    const int seg = static_cast<int>(std::floor(pos / kBiomeLengthMeters));
    const double inSeg = pos - seg * kBiomeLengthMeters;
    const BiomeId cur = static_cast<BiomeId>(seg % 6);
    const BiomeId nxt = static_cast<BiomeId>((seg + 1) % 6);
    float blend = 0.0f;
    if (inSeg > (kBiomeLengthMeters - kTransitionMeters)) {
        blend = static_cast<float>((inSeg - (kBiomeLengthMeters - kTransitionMeters)) / kTransitionMeters);
    }
    if (outBlend) *outBlend = std::clamp(blend, 0.0f, 1.0f);
    if (outNext) *outNext = nxt;
    return cur;
}

BiomePalette WorldSim::paletteForBiome(BiomeId b) {
    switch (b) {
        case BiomeId::PacificRedwoods:
            return {{0.14f, 0.34f, 0.18f}, {0.10f, 0.24f, 0.17f}, {0.34f, 0.28f, 0.22f}, {0.16f, 0.25f, 0.32f}, 1.0f, 1.15f, 34.0f};
        case BiomeId::HeartlandFarms:
            return {{0.62f, 0.56f, 0.22f}, {0.36f, 0.48f, 0.20f}, {0.48f, 0.39f, 0.26f}, {0.35f, 0.42f, 0.36f}, 0.45f, 0.55f, 14.0f};
        case BiomeId::SonoranCanyon:
            return {{0.76f, 0.45f, 0.25f}, {0.64f, 0.33f, 0.20f}, {0.80f, 0.54f, 0.32f}, {0.68f, 0.29f, 0.18f}, 0.50f, 0.90f, 40.0f};
        case BiomeId::QuietSuburbia:
            return {{0.24f, 0.50f, 0.23f}, {0.20f, 0.40f, 0.21f}, {0.58f, 0.58f, 0.60f}, {0.28f, 0.35f, 0.40f}, 0.80f, 0.35f, 12.0f};
        case BiomeId::DowntownSkyline:
            return {{0.26f, 0.28f, 0.32f}, {0.20f, 0.22f, 0.27f}, {0.42f, 0.44f, 0.48f}, {0.20f, 0.22f, 0.30f}, 0.95f, 0.12f, 8.0f};
        case BiomeId::AlpineSummit:
            return {{0.82f, 0.87f, 0.93f}, {0.68f, 0.76f, 0.86f}, {0.48f, 0.52f, 0.58f}, {0.75f, 0.82f, 0.92f}, 0.85f, 1.55f, 52.0f};
    }
    return {{0.2f, 0.4f, 0.2f}, {0.15f, 0.3f, 0.18f}, {0.4f, 0.35f, 0.3f}, {0.25f, 0.3f, 0.35f}, 0.7f, 1.0f, 25.0f};
}

float WorldSim::terrainSideProfile(BiomeId b, double s, float absLat) {
    if (absLat <= 6.8f) return 0.0f;
    const float d = absLat - 6.8f;
    const float wave = static_cast<float>(std::sin(s * 0.031 + absLat * 0.06) * 0.5 + 0.5);
    switch (b) {
        case BiomeId::PacificRedwoods:
            return std::min(32.0f, d * 0.26f * (0.55f + 0.65f * wave));
        case BiomeId::HeartlandFarms:
            return std::sin(d * 0.045f + static_cast<float>(s * 0.012)) * 3.2f + d * 0.03f;
        case BiomeId::SonoranCanyon: {
            if (d < 22.0f) return std::sin(d * 0.09f) * 0.8f;
            float terrace = std::floor((d - 22.0f) / 14.0f) * 5.5f;
            return std::min(28.0f, terrace + wave * 3.5f);
        }
        case BiomeId::QuietSuburbia:
            return std::min(5.0f, d * 0.04f + wave * 0.8f);
        case BiomeId::DowntownSkyline:
            return 0.15f; // Flat city blocks alongside paved shoulders!
        case BiomeId::AlpineSummit:
            return std::min(46.0f, d * 0.38f * (0.65f + 0.55f * wave));
    }
    return 0.0f;
}

QString WorldSim::titleForBiome(BiomeId b) {
    switch (b) {
        case BiomeId::PacificRedwoods: return QStringLiteral("Oregon · Pacific Redwoods");
        case BiomeId::HeartlandFarms:  return QStringLiteral("Iowa · Heartland Farms");
        case BiomeId::SonoranCanyon:   return QStringLiteral("Arizona · Sonoran Red Rocks");
        case BiomeId::QuietSuburbia:   return QStringLiteral("Vermont · Maple Suburbs");
        case BiomeId::DowntownSkyline: return QStringLiteral("Metro · Downtown Skyline");
        case BiomeId::AlpineSummit:    return QStringLiteral("Colorado · Alpine Pass");
    }
    return QStringLiteral("Open Highway");
}

void WorldSim::initTraffic() {
    m_traffic.clear();
    static const Color3 palette[6] = {
        {0.82f, 0.22f, 0.20f}, {0.20f, 0.48f, 0.78f}, {0.88f, 0.86f, 0.82f},
        {0.25f, 0.58f, 0.38f}, {0.85f, 0.62f, 0.18f}, {0.32f, 0.34f, 0.40f}
    };
    // 3 well-spaced same-way cars (+2.70m right lane) + 3 well-spaced oncoming cars (-2.70m left lane)
    for (int i = 0; i < 6; ++i) {
        TrafficCar tc{};
        bool oncoming = (i % 2 == 1);
        tc.s = 70.0 + i * 92.0;
        tc.laneOffset = oncoming ? -2.70f : 2.70f;
        tc.speedMs = oncoming ? (-20.0f - (i % 3) * 1.6f) : (16.5f + (i % 3) * 1.5f);
        tc.bodyType = i % 3;
        tc.color = palette[i % 6];
        m_traffic.push_back(tc);
    }
}

void WorldSim::setCarIndex(int idx) {
    m_carIndex = std::clamp(idx, 0, 2);
    if (m_audio) m_audio->setCarStyle(m_carIndex);
}

QString WorldSim::carName() const {
    switch (m_carIndex) {
        case 0: return QStringLiteral("'68 Canyon Cruiser (Classic V8)");
        case 1: return QStringLiteral("'94 Nomad Wagon (Comfy Cruiser)");
        case 2: return QStringLiteral("Apex GT-RS (Fun Sport)");
    }
    return QString();
}

void WorldSim::setTimeOfDay(float hours) {
    while (hours < 0.0f) hours += 24.0f;
    while (hours >= 24.0f) hours -= 24.0f;
    m_timeOfDay = hours;
}

void WorldSim::setWeatherMode(int w) {
    m_weatherMode = std::clamp(w, -1, 3);
}

void WorldSim::addFreeLook(float dYawRad, float dPitchRad) {
    m_freeLookYaw = std::clamp(m_freeLookYaw + dYawRad, -1.35f, 1.35f);
    m_freeLookPitch = std::clamp(m_freeLookPitch + dPitchRad, -0.55f, 0.55f);
    m_freeLookIdleTimer = 2.0f;
}

void WorldSim::updateEnvironment(float dt) {
    // 1 full 24h day-night cycle every ~6 minutes when Auto Time is ON!
    if (m_autoTime) {
        setTimeOfDay(m_timeOfDay + dt * (24.0f / 360.0f));
    }

    float blend = 0.0f;
    BiomeId nextB = BiomeId::PacificRedwoods;
    BiomeId curB = biomeAtDistance(m_s, m_lockedBiome, &blend, &nextB);
    m_currentBiomeTitle = titleForBiome(curB);
    if (curB != m_lastAnnouncedBiome) {
        m_lastAnnouncedBiome = curB;
        m_bannerText = QStringLiteral("WELCOME TO %1").arg(m_currentBiomeTitle.toUpper());
        m_bannerTimer = 4.2f;
        if (m_audio) m_audio->triggerChime(0);
    }
    if (m_bannerTimer > 0.0f) m_bannerTimer -= dt;

    // Weather selection (Auto picks biome-fitting atmospheric weather periodically!)
    if (m_weatherMode >= 0) {
        m_activeWeather = static_cast<WeatherKind>(m_weatherMode);
    } else {
        int cycle = static_cast<int>(m_s / 950.0) % 5;
        if (curB == BiomeId::AlpineSummit) m_activeWeather = WeatherKind::Snowfall;
        else if (curB == BiomeId::SonoranCanyon) m_activeWeather = WeatherKind::Clear;
        else if (cycle == 2) m_activeWeather = WeatherKind::Rain;
        else if (cycle == 4 && curB == BiomeId::PacificRedwoods) m_activeWeather = WeatherKind::MistyFog;
        else m_activeWeather = WeatherKind::Clear;
    }

    const float targetRain = (m_activeWeather == WeatherKind::Rain) ? 1.0f : 0.0f;
    const float targetSnow = (m_activeWeather == WeatherKind::Snowfall) ? 1.0f : 0.0f;
    const float targetFog  = (m_activeWeather == WeatherKind::MistyFog) ? 0.032f :
                             (m_activeWeather == WeatherKind::Rain)     ? 0.018f :
                             (m_activeWeather == WeatherKind::Snowfall) ? 0.020f : 0.0085f;

    m_rainAmount += (targetRain - m_rainAmount) * std::min(1.0f, dt * 0.8f);
    m_snowAmount += (targetSnow - m_snowAmount) * std::min(1.0f, dt * 0.8f);
    m_fogDensity += (targetFog - m_fogDensity) * std::min(1.0f, dt * 0.8f);

    // Animate wipers when raining or manually switched on
    if (wipers()) {
        m_wiperPhase += dt * 3.8f;
        if (m_wiperPhase > 6.283185f) m_wiperPhase -= 6.283185f;
    } else if (m_wiperPhase > 0.05f) {
        m_wiperPhase = std::max(0.0f, m_wiperPhase - dt * 4.0f);
    }

    // Update falling rain/snow particles in camera space
    if (m_rainAmount > 0.02f || m_snowAmount > 0.02f) {
        const bool isSnow = (m_snowAmount > m_rainAmount);
        const float fallSpd = isSnow ? 3.8f : 19.5f;
        for (auto& d : m_precip) {
            d.y -= fallSpd * d.speed * dt;
            d.z -= (m_speedMs * 0.65f) * dt;
            if (isSnow) d.x += std::sin(m_worldTime * 2.0f + d.z) * 0.8f * dt;
            if (d.y < -1.5f || d.z < 0.8f) {
                d.x = (rand01() * 2.0f - 1.0f) * 14.0f;
                d.y = 6.5f + rand01() * 4.0f;
                d.z = 6.0f + rand01() * 28.0f;
            }
        }
    }
}

void WorldSim::update(float dt, float steerInput, float throttleInput, float brakeInput) {
    dt = std::min(dt, 0.05f);
    m_worldTime += dt;
    updateEnvironment(dt);

    // Gentle auto-return of free-look when finger isn't dragging
    if (m_freeLookIdleTimer > 0.0f) {
        m_freeLookIdleTimer -= dt;
    } else {
        m_freeLookYaw *= std::exp(-3.2f * dt);
        m_freeLookPitch *= std::exp(-3.2f * dt);
    }

    // Car specs: 0=Classic '68, 1=Comfy '94 Wagon, 2=Apex GT-RS
    const float maxSpdKmh = (m_carIndex == 0) ? 148.0f : (m_carIndex == 1 ? 162.0f : 228.0f);
    const float accelRate = (m_carIndex == 0) ? 7.8f   : (m_carIndex == 1 ? 7.2f   : 13.5f);
    const float steerResp = (m_carIndex == 0) ? 5.2f   : (m_carIndex == 1 ? 6.5f   : 9.8f);
    const float rollCompliance = (m_carIndex == 0) ? 0.095f : (m_carIndex == 1 ? 0.065f : 0.032f);

    // Compute road curvature d2x/ds2 so turns feel real in manual steering!
    const float slopeNow  = roadSlopeDxDs(m_s);
    const float slopeNext = roadSlopeDxDs(m_s + 4.0);
    const float roadCurv  = (slopeNext - slopeNow) * 0.25f;

    float effectiveThrottle = throttleInput;
    float targetSteer = std::clamp(steerInput, -1.0f, 1.0f);
    m_turnSignal = 0;

    if (m_autoDrive) {
        // Predictive Kinematic Zen Auto-Drive:
        //  - Computes exact time-to-pass vs oncoming time-to-arrival before pulling out
        //  - Emergency bail-out steers back into the right lane whenever oncoming traffic closes in!
        const TrafficCar* leadCar = nullptr;
        double bestLeadDist = 1e9;
        for (const auto& tc : m_traffic) {
            if (tc.speedMs > 0.0f && std::abs(tc.laneOffset - 2.70f) < 1.6f) {
                double relS = tc.s - m_s;
                if (relS > -10.5 && relS < 52.0 && relS < bestLeadDist) {
                    bestLeadDist = relS;
                    leadCar = &tc;
                }
            }
        }

        float targetMs = m_cruiseTargetKmh / 3.6f;
        if (throttleInput > 0.1f) targetMs = maxSpdKmh / 3.6f;
        if (brakeInput > 0.1f) targetMs = 8.0f;

        // Compute nearest oncoming vehicle arrival time (seconds) in the left lane
        float minOncomingSec = 999.0f;
        const float estPassSpeedMs = (leadCar != nullptr)
            ? std::min(maxSpdKmh / 3.6f, std::max(targetMs + 4.0f, leadCar->speedMs + 9.5f))
            : std::max(18.0f, m_speedMs);

        for (const auto& oc : m_traffic) {
            if (oc.speedMs < 0.0f && oc.s > m_s - 6.0) {
                float distAhead = static_cast<float>(oc.s - m_s);
                float closingSpd = std::max(12.0f, estPassSpeedMs + std::abs(oc.speedMs));
                float tArr = distAhead / closingSpd;
                minOncomingSec = std::min(minOncomingSec, tArr);
            }
        }

        bool emergencyAbort = false;
        if (leadCar != nullptr) {
            const float relPassSpd = std::max(5.5f, estPassSpeedMs - leadCar->speedMs);
            const float distToClear = std::max(0.0f, static_cast<float>(bestLeadDist + 10.5));
            const float secNeededToPass = (distToClear / relPassSpd) + 1.8f;

            if (!m_zenPassing) {
                // Only initiate pass from the 24m pocket when oncoming traffic leaves plenty of margin!
                if (bestLeadDist <= 24.0 &&
                    (targetMs > leadCar->speedMs + 1.0f) &&
                    minOncomingSec > (secNeededToPass + 2.0f)) {
                    m_zenPassing = true;
                }
            } else {
                // Already passing: abort back to our lane if an oncoming car closes within danger threshold!
                if (minOncomingSec < 3.0f && minOncomingSec < (secNeededToPass + 0.6f)) {
                    m_zenPassing = false;
                    emergencyAbort = true;
                }
            }
        } else {
            m_zenPassing = false;
        }

        float desiredLane = m_zenPassing ? -2.55f : 2.65f;
        float steerUrgency = 1.0f;

        if (!m_zenPassing && m_lat < 1.8f) {
            // Returning to right lane: make sure right lane isn't currently occupied right beside our door!
            const bool besideLeadCar = (leadCar != nullptr && bestLeadDist > -5.2 && bestLeadDist < 5.4);
            if (besideLeadCar) {
                if (bestLeadDist < -1.2) {
                    // Almost clear ahead of leadCar — sprint clear then merge right!
                    desiredLane = -2.2f;
                    targetMs = maxSpdKmh / 3.6f;
                } else {
                    // Alongside/slightly behind leadCar while oncoming approaches — brake behind bumper then merge right!
                    desiredLane = -1.35f;
                    targetMs = std::max(5.0f, leadCar->speedMs - 9.5f);
                    brakeInput = 0.95f;
                }
            } else {
                desiredLane = 2.65f;
                if (minOncomingSec < 3.6f || emergencyAbort) {
                    steerUrgency = 1.65f; // Crisp evasive return to own lane!
                }
            }
        }

        // Turn signal indicator while changing lanes!
        if (desiredLane < 0.0f && m_lat > -1.7f) m_turnSignal = -1;
        else if (desiredLane > 0.0f && m_lat < 1.7f) m_turnSignal = 1;

        if (m_zenPassing && leadCar != nullptr) {
            // Brisk passing speed surge (+34 km/h over overtaken car) so time in oncoming lane stays short!
            targetMs = estPassSpeedMs;
        } else if (!m_zenPassing && leadCar != nullptr && bestLeadDist > 0.0 && bestLeadDist < 36.0 && m_lat > 0.5f) {
            // Smooth adaptive cruise spacing (~15m cushion) in right lane while waiting for oncoming traffic to clear!
            float gapErr = static_cast<float>(bestLeadDist - 15.0);
            float followMs = std::max(0.0f, leadCar->speedMs + gapErr * 0.48f);
            targetMs = std::min(targetMs, followMs);
        }

        float latErr = desiredLane - m_lat;
        float headTarget = std::clamp(latErr * 0.21f * steerUrgency, -0.46f, 0.46f);
        float steerCmd = (headTarget - m_heading) * (3.4f * steerUrgency);
        targetSteer = std::clamp(steerCmd, -1.0f, 1.0f);

        float spdErr = targetMs - m_speedMs;
        effectiveThrottle = std::clamp(spdErr * 0.45f, 0.0f, 1.0f);
        if (spdErr < -0.8f) {
            brakeInput = std::max(brakeInput, std::min(0.95f, -spdErr * 0.28f));
        }
    } else {
        m_zenPassing = false;
        if (steerInput < -0.22f) m_turnSignal = -1;
        else if (steerInput > 0.22f) m_turnSignal = 1;

        // Manual mode: gentle coasting floor (~58 km/h) when neither pedal is held
        if (throttleInput <= 0.02f && brakeInput <= 0.02f) {
            if (m_speedMs * 3.6f < 58.0f) effectiveThrottle = 0.32f;
        }
    }

    // Longitudinal dynamics
    const float drag = 0.0065f * m_speedMs * m_speedMs + 0.35f;
    const float grade = roadSlopeDyDs(m_s);
    float accel = effectiveThrottle * accelRate - brakeInput * 16.5f - drag * 0.18f - grade * 3.2f;
    // Soft off-road grass drag only outside the 6.8m paved/gravel shoulder
    if (std::abs(m_lat) > 6.8f) {
        accel -= m_speedMs * 0.65f;
    }
    m_speedMs = std::clamp(m_speedMs + accel * dt, 0.0f, maxSpdKmh / 3.6f);

    // Lateral / Steering dynamics
    m_steerSmoothed += (targetSteer - m_steerSmoothed) * std::min(1.0f, dt * steerResp);
    const float speedTurnFactor = std::min(1.0f, m_speedMs / 10.0f);
    const float curveDrift = m_autoDrive ? 0.0f : (roadCurv * m_speedMs * 0.24f);
    m_heading += (m_steerSmoothed * 1.25f * speedTurnFactor - curveDrift) * dt;
    m_heading *= std::exp(-1.7f * dt);
    m_heading = std::clamp(m_heading, -0.75f, 0.75f);

    m_s += static_cast<double>(m_speedMs * std::cos(m_heading) * dt);
    m_lat += m_speedMs * std::sin(m_heading) * dt;

    // Gentle guardrail boundary at +/- 11.5m so player never falls out of the world
    if (m_lat > 11.5f)  { m_lat = 11.5f;  m_heading = std::min(0.0f, m_heading); }
    if (m_lat < -11.5f) { m_lat = -11.5f; m_heading = std::max(0.0f, m_heading); }

    // Suspension roll & pitch feel
    float targetRoll = -m_steerSmoothed * (m_speedMs / 28.0f) * rollCompliance;
    float targetPitch = -accel * 0.0055f;
    m_bodyRoll += (targetRoll - m_bodyRoll) * std::min(1.0f, dt * 7.5f);
    m_bodyPitch += (targetPitch - m_bodyPitch) * std::min(1.0f, dt * 7.5f);

    // Update ambient AI traffic & recycle far beyond fog horizon (390..540m) so cars never pop in mid-pass!
    for (size_t i = 0; i < m_traffic.size(); ++i) {
        auto& tc = m_traffic[i];
        tc.s += static_cast<double>(tc.speedMs * dt);
        if (tc.s < m_s - 55.0 || tc.s > m_s + 580.0) {
            double candidateS = m_s + 390.0 + rand01() * 140.0;
            for (size_t j = 0; j < m_traffic.size(); ++j) {
                if (i == j) continue;
                if (std::abs(m_traffic[j].laneOffset - tc.laneOffset) < 1.5f &&
                    std::abs(m_traffic[j].s - candidateS) < 110.0) {
                    candidateS = std::max(candidateS, m_traffic[j].s + 115.0);
                }
            }
            tc.s = candidateS;
        }

        // Physical collision deflection: oncoming cars always deflect player back toward right lane (+X)!
        const double relS = tc.s - m_s;
        const float latDiff = m_lat - tc.laneOffset;
        if (std::abs(relS) < 4.0 && std::abs(latDiff) < 1.58f) {
            float slipDir = 1.0f;
            if (tc.speedMs < 0.0f) {
                // Oncoming vehicle in left lane: push decisively back toward right lane!
                slipDir = 1.0f;
                m_heading = std::max(m_heading, 0.18f);
            } else {
                slipDir = (latDiff < 0.35f) ? -1.0f : 1.0f;
            }
            m_lat += slipDir * 11.0f * dt;
            if (relS > 0.0 && tc.speedMs > 0.0f && std::abs(latDiff) < 0.95f) {
                m_speedMs = std::min(m_speedMs, tc.speedMs + 2.0f);
            }
        }
    }

    // Simulated 5-speed automatic gear curve for realistic engine RPM note & gauge needle!
    const float spdNorm = std::clamp((m_speedMs * 3.6f) / maxSpdKmh, 0.0f, 1.0f);
    const float gearSaw = std::fmod(spdNorm * 4.4f, 1.0f);
    const float rpmNorm = std::clamp(0.18f + gearSaw * 0.58f + spdNorm * 0.24f, 0.14f, 0.98f);

    if (m_audio) {
        m_audio->updateTelemetry(m_speedMs * 3.6f, rpmNorm, effectiveThrottle, m_rainAmount);
    }
}

void WorldSim::buildSnapshot(DriveSnapshot& out) const {
    out.playerS = m_s;
    out.playerLat = m_lat;
    out.playerHeading = m_heading;
    out.speedKmh = m_speedMs * 3.6f;

    const float maxSpdKmh = (m_carIndex == 0) ? 148.0f : (m_carIndex == 1 ? 162.0f : 228.0f);
    const float spdNorm = std::clamp(out.speedKmh / maxSpdKmh, 0.0f, 1.0f);
    out.rpmNorm = std::clamp(0.18f + std::fmod(spdNorm * 4.4f, 1.0f) * 0.58f + spdNorm * 0.24f, 0.14f, 0.98f);
    out.steerWheelAngleDeg = m_steerSmoothed * 135.0f;
    out.bodyRollRad = m_bodyRoll;
    out.bodyPitchRad = m_bodyPitch;
    out.suspBobY = std::sin(static_cast<float>(m_s * 0.9)) * 0.008f * std::min(1.0f, m_speedMs / 20.0f) +
                   ((std::abs(m_lat) > 5.5f) ? std::sin(static_cast<float>(m_s * 4.5)) * 0.035f : 0.0f);

    out.carIndex = m_carIndex;
    out.cameraMode = m_cameraMode;
    out.freeLookYaw = m_freeLookYaw;
    out.freeLookPitch = m_freeLookPitch;

    out.wipersOn = wipers();
    out.wiperPhase = m_wiperPhase;
    out.autoDrive = m_autoDrive;
    out.turnSignal = m_turnSignal;

    out.timeOfDay = m_timeOfDay;
    out.weather = m_activeWeather;
    out.rainAmount = m_rainAmount;
    out.snowAmount = m_snowAmount;
    out.fogDensity = m_fogDensity;
    out.worldTimeSec = m_worldTime;

    out.biomeA = biomeAtDistance(m_s, m_lockedBiome, &out.biomeBlend, &out.biomeB);
    BiomePalette pA = paletteForBiome(out.biomeA);
    BiomePalette pB = paletteForBiome(out.biomeB);
    out.blendedPalette.grassNear     = lerpColor(pA.grassNear, pB.grassNear, out.biomeBlend);
    out.blendedPalette.grassFar      = lerpColor(pA.grassFar, pB.grassFar, out.biomeBlend);
    out.blendedPalette.dirtShoulder  = lerpColor(pA.dirtShoulder, pB.dirtShoulder, out.biomeBlend);
    out.blendedPalette.mountainCol   = lerpColor(pA.mountainCol, pB.mountainCol, out.biomeBlend);
    out.blendedPalette.treeDensity   = pA.treeDensity + (pB.treeDensity - pA.treeDensity) * out.biomeBlend;
    out.blendedPalette.hillScale     = pA.hillScale + (pB.hillScale - pA.hillScale) * out.biomeBlend;
    out.blendedPalette.mountainHeight= pA.mountainHeight + (pB.mountainHeight - pA.mountainHeight) * out.biomeBlend;

    // Compute Sun elevation & atmospheric Dawn / Noon / Golden Hour / Night sky gradients!
    const float dayAngle = ((m_timeOfDay - 6.0f) / 24.0f) * 6.2831853f;
    const float sunElev = std::sin(dayAngle); // -1 midnight .. 0 sunrise/sunset .. +1 noon
    out.sunDirX = std::cos(dayAngle) * 0.55f;
    out.sunDirY = sunElev;
    out.sunDirZ = 0.82f;
    const float sLen = std::sqrt(out.sunDirX * out.sunDirX + out.sunDirY * out.sunDirY + out.sunDirZ * out.sunDirZ);
    out.sunDirX /= sLen; out.sunDirY /= sLen; out.sunDirZ /= sLen;

    out.nightFactor = std::clamp((-sunElev + 0.12f) / 0.42f, 0.0f, 1.0f);
    const float sunsetBand = std::exp(-std::pow(sunElev * 4.2f, 2.0f)); // Peaks right around 6:00 & 18:00!

    Color3 dayZenith{0.18f, 0.44f, 0.84f};
    Color3 dayHorizon{0.62f, 0.80f, 0.96f};
    Color3 nightZenith{0.02f, 0.03f, 0.09f};
    Color3 nightHorizon{0.06f, 0.08f, 0.18f};
    Color3 sunsetZenith{0.24f, 0.18f, 0.48f};
    Color3 sunsetHorizon{0.98f, 0.46f, 0.24f};

    Color3 baseZ = lerpColor(dayZenith, nightZenith, out.nightFactor);
    Color3 baseH = lerpColor(dayHorizon, nightHorizon, out.nightFactor);
    out.skyZenith  = lerpColor(baseZ, sunsetZenith, sunsetBand * 0.82f);
    out.skyHorizon = lerpColor(baseH, sunsetHorizon, sunsetBand * 0.92f);

    // Overcast desaturation during Rain / Fog / Snow
    const float overcast = std::max({m_rainAmount * 0.75f, m_snowAmount * 0.55f, m_activeWeather == WeatherKind::MistyFog ? 0.8f : 0.0f});
    if (overcast > 0.01f) {
        Color3 greySky = lerpColor({0.46f, 0.50f, 0.56f}, {0.10f, 0.12f, 0.16f}, out.nightFactor);
        out.skyZenith  = lerpColor(out.skyZenith, greySky, overcast * 0.85f);
        out.skyHorizon = lerpColor(out.skyHorizon, greySky, overcast * 0.85f);
    }

    out.sunColor = lerpColor(
        lerpColor({1.0f, 0.96f, 0.90f}, {0.22f, 0.28f, 0.48f}, out.nightFactor),
        {1.0f, 0.58f, 0.28f},
        sunsetBand
    );

    out.headlightsOn = m_headlights && (out.nightFactor > 0.25f || overcast > 0.45f || !m_autoTime);
    out.traffic = m_traffic;
    out.precip = m_precip;
}
