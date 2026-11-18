#include "RoadView.h"

#include <GLES2/gl2.h>
#include <QMatrix4x4>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QQuickWindow>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr float kPi = 3.14159265359f;

struct Vert3D {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b;
    float emissive;
};

struct Vert2D {
    float x, y;
    float r, g, b, a;
};

const char* kSkyVS = R"(
attribute vec2 a_pos;
uniform mat4 u_invViewProj;
varying vec3 v_ray;
void main() {
    vec4 farPt = u_invViewProj * vec4(a_pos, 1.0, 1.0);
    v_ray = farPt.xyz / max(0.0001, farPt.w);
    gl_Position = vec4(a_pos, 0.999, 1.0);
}
)";

const char* kSkyFS = R"(
precision mediump float;
varying vec3 v_ray;
uniform vec3 u_zenith;
uniform vec3 u_horizon;
uniform vec3 u_sunDir;
uniform vec3 u_sunCol;
uniform float u_night;
uniform float u_time;

float starHash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    vec3 dir = normalize(v_ray);
    float h = clamp(dir.y, 0.0, 1.0);
    vec3 col = mix(u_horizon, u_zenith, pow(h, 0.52));

    // Sun disk & warm atmospheric Mie scatter glow
    float sunDot = max(dot(dir, normalize(u_sunDir)), 0.0);
    col += u_sunCol * pow(sunDot, 7.0) * 0.42 * (1.0 - u_night * 0.75);
    if (sunDot > 0.9985 && u_sunDir.y > -0.08) {
        col = mix(col, vec3(1.0, 0.98, 0.90), 0.95);
    } else if (sunDot > 0.994 && u_sunDir.y > -0.10) {
        col += u_sunCol * 0.85;
    }

    // Moon & Starfield at night
    if (u_night > 0.05 && dir.y > 0.02) {
        vec3 moonDir = normalize(vec3(-u_sunDir.x, max(0.28, -u_sunDir.y), u_sunDir.z));
        float mDot = max(dot(dir, moonDir), 0.0);
        col += vec3(0.45, 0.55, 0.80) * pow(mDot, 28.0) * 0.35 * u_night;
        if (mDot > 0.9988) {
            col = mix(col, vec3(0.92, 0.95, 1.0), u_night);
        }

        vec2 skyUv = dir.xz / (dir.y + 0.35) * 42.0;
        vec2 cell = floor(skyUv);
        float sh = starHash(cell);
        if (sh > 0.986) {
            vec2 f = fract(skyUv) - 0.5;
            float d = length(f);
            float twinkle = 0.65 + 0.35 * sin(u_time * 2.5 + sh * 90.0);
            float star = smoothstep(0.22, 0.02, d) * twinkle * u_night * smoothstep(0.02, 0.25, dir.y);
            col += vec3(0.88, 0.92, 1.0) * star;
        }
    }

    // Soft high-altitude drifting clouds
    if (dir.y > 0.04) {
        vec2 cuv = dir.xz / (dir.y + 0.22) * 1.4 + vec2(u_time * 0.012, u_time * 0.006);
        float c1 = sin(cuv.x * 2.3) * cos(cuv.y * 2.1) + 0.5 * sin(cuv.x * 5.1 - cuv.y * 3.7);
        float cloud = smoothstep(0.35, 1.25, c1) * smoothstep(0.04, 0.28, dir.y) * 0.24 * (1.0 - u_night * 0.65);
        col = mix(col, mix(u_horizon, vec3(1.0, 0.97, 0.94), 0.55), cloud);
    }

    gl_FragColor = vec4(col, 1.0);
}
)";

const char* kWorldVS = R"(
attribute vec3 a_pos;
attribute vec3 a_norm;
attribute vec3 a_col;
attribute float a_emissive;

uniform mat4 u_mvp;

varying vec3 v_pos;
varying vec3 v_norm;
varying vec3 v_col;
varying float v_emissive;

void main() {
    v_pos = a_pos;
    v_norm = a_norm;
    v_col = a_col;
    v_emissive = a_emissive;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

const char* kWorldFS = R"(
precision mediump float;

varying vec3 v_pos;
varying vec3 v_norm;
varying vec3 v_col;
varying float v_emissive;

uniform vec3 u_sunDir;
uniform vec3 u_sunCol;
uniform vec3 u_skyZenith;
uniform vec3 u_skyHorizon;
uniform float u_night;
uniform float u_fogDensity;
uniform float u_headlightOn;
uniform vec3 u_beamOrigin;
uniform vec3 u_beamDir;
uniform float u_rain;

void main() {
    vec3 N = normalize(v_norm);
    float ndl = max(dot(N, normalize(u_sunDir)), 0.0);
    float dayScale = 1.0 - u_night * 0.82;

    vec3 hemiAmb = mix(vec3(0.16, 0.15, 0.20), u_skyZenith * 0.65 + u_skyHorizon * 0.35, 0.5 + 0.5 * N.y);
    hemiAmb = mix(hemiAmb, vec3(0.055, 0.068, 0.125), u_night * 0.78);

    vec3 lit = v_col * (hemiAmb + u_sunCol * ndl * 0.82 * dayScale);

    // Player twin headlights illuminating the 3D highway & roadside props ahead!
    if (u_headlightOn > 0.5) {
        vec3 toFrag = v_pos - u_beamOrigin;
        float distB = length(toFrag);
        if (distB > 0.5 && distB < 68.0) {
            vec3 dirB = toFrag / distB;
            float cone = dot(dirB, normalize(u_beamDir));
            float spot = smoothstep(0.84, 0.965, cone);
            float att = clamp(1.0 - distB / 68.0, 0.0, 1.0);
            lit += v_col * vec3(1.15, 1.08, 0.92) * spot * att * att * 1.45;
        }
    }

    // Wet asphalt sky reflection shimmer during rainstorms
    if (u_rain > 0.05 && N.y > 0.92 && v_emissive < 0.05) {
        lit = mix(lit, u_skyHorizon * 0.65, u_rain * 0.28);
    }

    // Emissive windows, streetlights, headlights, taillights & cat's-eyes!
    lit = mix(lit, v_col * 1.28, clamp(v_emissive, 0.0, 1.0));

    float dist = length(v_pos);
    float fog = 1.0 - exp(-pow(dist * u_fogDensity, 1.55));
    fog = clamp(fog, 0.0, 1.0);
    gl_FragColor = vec4(mix(lit, u_skyHorizon, fog * (1.0 - v_emissive * 0.35)), 1.0);
}
)";

const char* kUiVS = R"(
attribute vec2 a_pos;
attribute vec4 a_col;
varying vec4 v_col;
void main() {
    v_col = a_col;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

const char* kUiFS = R"(
precision mediump float;
varying vec4 v_col;
void main() {
    gl_FragColor = v_col;
}
)";

GLuint makeProgram(const char* vsSrc, const char* fsSrc) {
    auto comp = [](GLenum t, const char* s) {
        GLuint id = glCreateShader(t);
        glShaderSource(id, 1, &s, nullptr);
        glCompileShader(id);
        return id;
    };
    GLuint vs = comp(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = comp(GL_FRAGMENT_SHADER, fsSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

uint32_t sliceHash(int64_t a, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(a) * 2654435761u ^ (salt * 2246822519u);
    h = (h ^ (h >> 13)) * 3266489917u;
    return h ^ (h >> 16);
}

float hashFloat(int64_t a, uint32_t salt) {
    return static_cast<float>(sliceHash(a, salt) & 0xFFFFu) / 65535.0f;
}

void pushTri3D(std::vector<Vert3D>& v,
               float x0, float y0, float z0,
               float x1, float y1, float z1,
               float x2, float y2, float z2,
               float nx, float ny, float nz,
               const Color3& c, float emissive = 0.0f) {
    v.push_back({x0, y0, z0, nx, ny, nz, c.r, c.g, c.b, emissive});
    v.push_back({x1, y1, z1, nx, ny, nz, c.r, c.g, c.b, emissive});
    v.push_back({x2, y2, z2, nx, ny, nz, c.r, c.g, c.b, emissive});
}

void pushQuad3D(std::vector<Vert3D>& v,
                float x0, float y0, float z0,
                float x1, float y1, float z1,
                float x2, float y2, float z2,
                float x3, float y3, float z3,
                float nx, float ny, float nz,
                const Color3& c, float emissive = 0.0f) {
    pushTri3D(v, x0, y0, z0, x1, y1, z1, x2, y2, z2, nx, ny, nz, c, emissive);
    pushTri3D(v, x0, y0, z0, x2, y2, z2, x3, y3, z3, nx, ny, nz, c, emissive);
}

void pushBox3D(std::vector<Vert3D>& v,
               float cx, float yBottom, float cz,
               float sx, float sy, float sz,
               const Color3& c, float emissive = 0.0f) {
    const float x0 = cx - sx * 0.5f, x1 = cx + sx * 0.5f;
    const float y0 = yBottom,        y1 = yBottom + sy;
    const float z0 = cz - sz * 0.5f, z1 = cz + sz * 0.5f;
    // Front (-Z facing camera), Back (+Z), Left (-X), Right (+X), Top (+Y)
    pushQuad3D(v, x0, y0, z0, x1, y0, z0, x1, y1, z0, x0, y1, z0,  0, 0, -1, c, emissive);
    pushQuad3D(v, x1, y0, z1, x0, y0, z1, x0, y1, z1, x1, y1, z1,  0, 0,  1, c, emissive);
    pushQuad3D(v, x0, y0, z1, x0, y0, z0, x0, y1, z0, x0, y1, z1, -1, 0,  0, c, emissive);
    pushQuad3D(v, x1, y0, z0, x1, y0, z1, x1, y1, z1, x1, y1, z0,  1, 0,  0, c, emissive);
    pushQuad3D(v, x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1,  0, 1,  0, c, emissive);
}

void pushPyramidCone(std::vector<Vert3D>& v,
                     float cx, float yBase, float cz,
                     float radius, float height, const Color3& c) {
    const float x0 = cx - radius, x1 = cx + radius;
    const float z0 = cz - radius, z1 = cz + radius;
    const float yt = yBase + height;
    pushTri3D(v, x0, yBase, z0, x1, yBase, z0, cx, yt, cz,  0.0f, 0.5f, -0.86f, c);
    pushTri3D(v, x1, yBase, z0, x1, yBase, z1, cx, yt, cz,  0.86f, 0.5f, 0.0f, c);
    pushTri3D(v, x1, yBase, z1, x0, yBase, z1, cx, yt, cz,  0.0f, 0.5f,  0.86f, c);
    pushTri3D(v, x0, yBase, z1, x0, yBase, z0, cx, yt, cz, -0.86f, 0.5f, 0.0f, c);
}

// 2D Cockpit Vector Helpers
void pushTri2D(std::vector<Vert2D>& v, float x0, float y0, float x1, float y1, float x2, float y2,
               float r, float g, float b, float a = 1.0f) {
    v.push_back({x0, y0, r, g, b, a});
    v.push_back({x1, y1, r, g, b, a});
    v.push_back({x2, y2, r, g, b, a});
}

void pushQuad2D(std::vector<Vert2D>& v,
                float x0, float y0, float x1, float y1,
                float x2, float y2, float x3, float y3,
                float r, float g, float b, float a = 1.0f) {
    pushTri2D(v, x0, y0, x1, y1, x2, y2, r, g, b, a);
    pushTri2D(v, x0, y0, x2, y2, x3, y3, r, g, b, a);
}

void pushLine2D(std::vector<Vert2D>& v,
                float x0, float y0, float x1, float y1, float halfThick, float invAspect,
                float r, float g, float b, float a = 1.0f) {
    float dx = (x1 - x0);
    float dy = (y1 - y0);
    float len = std::hypot(dx, dy);
    if (len < 1e-5f) return;
    float nx = (-dy / len) * halfThick * invAspect;
    float ny = (dx / len) * halfThick;
    pushQuad2D(v, x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny, x0 + nx, y0 + ny, r, g, b, a);
}

void pushDisc2D(std::vector<Vert2D>& v, float cx, float cy, float rx, float ry, int segs,
                float r, float g, float b, float a = 1.0f) {
    for (int i = 0; i < segs; ++i) {
        float a0 = (2.0f * kPi * i) / segs;
        float a1 = (2.0f * kPi * (i + 1)) / segs;
        pushTri2D(v, cx, cy,
                  cx + std::cos(a0) * rx, cy + std::sin(a0) * ry,
                  cx + std::cos(a1) * rx, cy + std::sin(a1) * ry,
                  r, g, b, a);
    }
}

void pushRing2D(std::vector<Vert2D>& v, float cx, float cy,
                float rxIn, float ryIn, float rxOut, float ryOut, int segs,
                float r, float g, float b, float a = 1.0f) {
    for (int i = 0; i < segs; ++i) {
        float a0 = (2.0f * kPi * i) / segs;
        float a1 = (2.0f * kPi * (i + 1)) / segs;
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);
        pushQuad2D(v,
                   cx + c0 * rxIn,  cy + s0 * ryIn,
                   cx + c0 * rxOut, cy + s0 * ryOut,
                   cx + c1 * rxOut, cy + s1 * ryOut,
                   cx + c1 * rxIn,  cy + s1 * ryIn,
                   r, g, b, a);
    }
}

class HorizonRenderer : public QQuickFramebufferObject::Renderer {
public:
    ~HorizonRenderer() override {
        if (m_skyProg) glDeleteProgram(m_skyProg);
        if (m_worldProg) glDeleteProgram(m_worldProg);
        if (m_uiProg) glDeleteProgram(m_uiProg);
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QSize target = size;
        const int maxDim = std::max(size.width(), size.height());
        if (maxDim > 1120) {
            float sc = 1120.0f / static_cast<float>(maxDim);
            target = QSize(std::max(64, int(size.width() * sc)), std::max(64, int(size.height() * sc)));
        }
        m_fbSize = target;
        return new QOpenGLFramebufferObject(target, fmt);
    }

    void synchronize(QQuickFramebufferObject* item) override {
        auto* rv = static_cast<RoadView*>(item);
        m_window = rv->window();
        rv->syncToSnapshot(m_snap);
    }

    void render() override {
        if (!QOpenGLContext::currentContext()) return;
        ensurePrograms();

        const QSize curSz = framebufferObject() ? framebufferObject()->size() : m_fbSize;
        const int w = std::max(1, curSz.width());
        const int h = std::max(1, curSz.height());
        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        glViewport(0, 0, w, h);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glClearColor(m_snap.skyHorizon.r, m_snap.skyHorizon.g, m_snap.skyHorizon.b, 1.0f);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Compute origin (player's world road position) so all floats stay near (0,0,0)
        const double s0 = m_snap.playerS;
        const float roadX0 = WorldSim::roadCenterX(s0);
        const float roadY0 = WorldSim::roadElevationY(s0);
        const float roadYaw = std::atan(WorldSim::roadSlopeDxDs(s0));
        const float roadPitch = std::atan(WorldSim::roadSlopeDyDs(s0));

        const float carX = roadX0 + m_snap.playerLat;
        const float carY = roadY0 + m_snap.suspBobY;
        const float totalYaw = roadYaw + m_snap.playerHeading;

        // Camera placement relative to (carX, carY, s0)
        const float viewYaw = (m_snap.cameraMode == 1) ? (totalYaw + m_snap.freeLookYaw) : totalYaw;
        float camOffX = 0.0f, camOffY = 1.24f, camOffZ = 0.15f;
        if (m_snap.cameraMode == 0) {
            // Cockpit Driver Seat (slightly left-of-center LHD behind steering wheel!)
            camOffX = -0.36f * std::cos(totalYaw);
            camOffY = (m_snap.carIndex == 2) ? 1.12f : 1.28f;
            camOffZ = -0.15f;
        } else if (m_snap.cameraMode == 1) {
            // 3rd-Person Telephoto Chase Camera (orbits cleanly around car with freeLookYaw!)
            camOffX = -std::sin(viewYaw) * 7.2f;
            camOffY = 2.18f + std::sin(roadPitch) * 1.2f;
            camOffZ = -std::cos(viewYaw) * 7.2f;
        } else {
            // Panoramic Hood / Bonnet Camera
            camOffX = std::sin(totalYaw) * 0.8f;
            camOffY = 1.18f;
            camOffZ = std::cos(totalYaw) * 0.8f;
        }

        // Aspect-aware horizontal FOV (prevents wide-angle fisheye stretching on 20:9 phone screens!)
        QMatrix4x4 proj;
        const float targetHFovDeg = (m_snap.cameraMode == 1)
                                        ? (78.0f + std::min(7.0f, m_snap.speedKmh * 0.03f))
                                        : 86.0f;
        const float vFovRad = 2.0f * std::atan(std::tan(targetHFovDeg * 0.5f * (kPi / 180.0f)) / std::max(0.75f, aspect));
        const float vFovDeg = std::clamp(vFovRad * (180.0f / kPi), 36.0f, 70.0f);
        proj.perspective(vFovDeg, aspect, 0.20f, 340.0f);

        QMatrix4x4 rotOnly;
        const float camRoll = (m_snap.cameraMode == 1) ? (m_snap.bodyRollRad * 0.25f) : m_snap.bodyRollRad;
        rotOnly.rotate(camRoll * 57.2958f, 0.0f, 0.0f, 1.0f);
        rotOnly.rotate(-(roadPitch + (m_snap.cameraMode == 1 ? 0.0f : m_snap.bodyPitchRad) + m_snap.freeLookPitch) * 57.2958f +
                           (m_snap.cameraMode == 1 ? 8.2f : 1.5f),
                       1.0f, 0.0f, 0.0f);
        rotOnly.rotate((totalYaw + m_snap.freeLookYaw) * 57.2958f, 0.0f, 1.0f, 0.0f);

        QMatrix4x4 view = rotOnly;
        view.translate(-camOffX, -camOffY, camOffZ); // Note: glZ = -relS -> camGlZ = -camOffZ

        // 1) Render Procedural Atmospheric Sky Dome
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        renderSky(proj * rotOnly);

        // 2) Build & Render 3D Highway, Rolling Biomes, Mountains, Buildings, Trees & Cars!
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);

        m_verts3D.clear();
        buildWorldGeometry(s0, carX, carY, totalYaw);
        renderWorld3D(proj * view, totalYaw);

        // 3) Render 3D Rain / Snow Streaks + Cockpit Dashboard & Rotating Steering Wheel!
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        m_verts2D.clear();
        buildPrecipitationOverlay(aspect);
        if (m_snap.cameraMode == 0) {
            buildCockpitInterior(aspect);
        }
        renderOverlay2D();

        glDepthMask(GL_TRUE);
        if (m_window) m_window->resetOpenGLState();
        update();
    }

private:
    void ensurePrograms() {
        if (m_worldProg != 0) return;
        m_skyProg = makeProgram(kSkyVS, kSkyFS);
        m_aSkyPos = glGetAttribLocation(m_skyProg, "a_pos");
        m_uSkyInvVP = glGetUniformLocation(m_skyProg, "u_invViewProj");
        m_uSkyZenith = glGetUniformLocation(m_skyProg, "u_zenith");
        m_uSkyHorizon = glGetUniformLocation(m_skyProg, "u_horizon");
        m_uSkySunDir = glGetUniformLocation(m_skyProg, "u_sunDir");
        m_uSkySunCol = glGetUniformLocation(m_skyProg, "u_sunCol");
        m_uSkyNight = glGetUniformLocation(m_skyProg, "u_night");
        m_uSkyTime = glGetUniformLocation(m_skyProg, "u_time");

        m_worldProg = makeProgram(kWorldVS, kWorldFS);
        m_aPos = glGetAttribLocation(m_worldProg, "a_pos");
        m_aNorm = glGetAttribLocation(m_worldProg, "a_norm");
        m_aCol = glGetAttribLocation(m_worldProg, "a_col");
        m_aEmis = glGetAttribLocation(m_worldProg, "a_emissive");
        m_uMvp = glGetUniformLocation(m_worldProg, "u_mvp");
        m_uSunDir = glGetUniformLocation(m_worldProg, "u_sunDir");
        m_uSunCol = glGetUniformLocation(m_worldProg, "u_sunCol");
        m_uZenith = glGetUniformLocation(m_worldProg, "u_skyZenith");
        m_uHorizon = glGetUniformLocation(m_worldProg, "u_skyHorizon");
        m_uNight = glGetUniformLocation(m_worldProg, "u_night");
        m_uFog = glGetUniformLocation(m_worldProg, "u_fogDensity");
        m_uHeadOn = glGetUniformLocation(m_worldProg, "u_headlightOn");
        m_uBeamOrig = glGetUniformLocation(m_worldProg, "u_beamOrigin");
        m_uBeamDir = glGetUniformLocation(m_worldProg, "u_beamDir");
        m_uRain = glGetUniformLocation(m_worldProg, "u_rain");

        m_uiProg = makeProgram(kUiVS, kUiFS);
        m_aUiPos = glGetAttribLocation(m_uiProg, "a_pos");
        m_aUiCol = glGetAttribLocation(m_uiProg, "a_col");
    }

    void renderSky(const QMatrix4x4& rotVP) {
        glUseProgram(m_skyProg);
        bool invOk = false;
        QMatrix4x4 invVP = rotVP.inverted(&invOk);
        if (!invOk) invVP.setToIdentity();

        glUniformMatrix4fv(m_uSkyInvVP, 1, GL_FALSE, invVP.constData());
        glUniform3f(m_uSkyZenith, m_snap.skyZenith.r, m_snap.skyZenith.g, m_snap.skyZenith.b);
        glUniform3f(m_uSkyHorizon, m_snap.skyHorizon.r, m_snap.skyHorizon.g, m_snap.skyHorizon.b);
        // Forward in GL space is -Z!
        glUniform3f(m_uSkySunDir, m_snap.sunDirX, m_snap.sunDirY, -m_snap.sunDirZ);
        glUniform3f(m_uSkySunCol, m_snap.sunColor.r, m_snap.sunColor.g, m_snap.sunColor.b);
        glUniform1f(m_uSkyNight, m_snap.nightFactor);
        glUniform1f(m_uSkyTime, m_snap.worldTimeSec);

        static const float quad[12] = {
            -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
        };
        glEnableVertexAttribArray(m_aSkyPos);
        glVertexAttribPointer(m_aSkyPos, 2, GL_FLOAT, GL_FALSE, 0, quad);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(m_aSkyPos);
    }

    void renderWorld3D(const QMatrix4x4& mvp, float totalYaw) {
        if (m_verts3D.empty()) return;
        glUseProgram(m_worldProg);
        glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, mvp.constData());
        glUniform3f(m_uSunDir, m_snap.sunDirX, m_snap.sunDirY, -m_snap.sunDirZ);
        glUniform3f(m_uSunCol, m_snap.sunColor.r, m_snap.sunColor.g, m_snap.sunColor.b);
        glUniform3f(m_uZenith, m_snap.skyZenith.r, m_snap.skyZenith.g, m_snap.skyZenith.b);
        glUniform3f(m_uHorizon, m_snap.skyHorizon.r, m_snap.skyHorizon.g, m_snap.skyHorizon.b);
        glUniform1f(m_uNight, m_snap.nightFactor);
        glUniform1f(m_uFog, m_snap.fogDensity);
        glUniform1f(m_uHeadOn, m_snap.headlightsOn ? 1.0f : 0.0f);
        glUniform3f(m_uBeamOrig, 0.0f, 0.85f, -1.8f);
        glUniform3f(m_uBeamDir, std::sin(totalYaw), -0.06f, -std::cos(totalYaw));
        glUniform1f(m_uRain, m_snap.rainAmount);

        const char* base = reinterpret_cast<const char*>(m_verts3D.data());
        const GLsizei stride = sizeof(Vert3D);
        glEnableVertexAttribArray(m_aPos);
        glEnableVertexAttribArray(m_aNorm);
        glEnableVertexAttribArray(m_aCol);
        glEnableVertexAttribArray(m_aEmis);
        glVertexAttribPointer(m_aPos, 3, GL_FLOAT, GL_FALSE, stride, base + 0);
        glVertexAttribPointer(m_aNorm, 3, GL_FLOAT, GL_FALSE, stride, base + 3 * sizeof(float));
        glVertexAttribPointer(m_aCol, 3, GL_FLOAT, GL_FALSE, stride, base + 6 * sizeof(float));
        glVertexAttribPointer(m_aEmis, 1, GL_FLOAT, GL_FALSE, stride, base + 9 * sizeof(float));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts3D.size()));
        glDisableVertexAttribArray(m_aPos);
        glDisableVertexAttribArray(m_aNorm);
        glDisableVertexAttribArray(m_aCol);
        glDisableVertexAttribArray(m_aEmis);
    }

    void renderOverlay2D() {
        if (m_verts2D.empty()) return;
        glUseProgram(m_uiProg);
        const char* base = reinterpret_cast<const char*>(m_verts2D.data());
        const GLsizei stride = sizeof(Vert2D);
        glEnableVertexAttribArray(m_aUiPos);
        glEnableVertexAttribArray(m_aUiCol);
        glVertexAttribPointer(m_aUiPos, 2, GL_FLOAT, GL_FALSE, stride, base + 0);
        glVertexAttribPointer(m_aUiCol, 4, GL_FLOAT, GL_FALSE, stride, base + 2 * sizeof(float));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_verts2D.size()));
        glDisableVertexAttribArray(m_aUiPos);
        glDisableVertexAttribArray(m_aUiCol);
    }

    void buildVehicleMesh(float x, float y, float z,
                          float yawRad, float pitchRad, float rollRad,
                          int style, const Color3& bodyCol, bool isPlayer) {
        // True SO(3) rigid-body 3D oriented box builder — rotates all 8 corners & 6 face normals
        // cleanly by (roll -> pitch -> yaw) so car proportions stay 100% locked at every road angle!
        const float cy = std::cos(yawRad),   sy = std::sin(yawRad);
        const float cp = std::cos(pitchRad), sp = std::sin(pitchRad);
        const float cr = std::cos(rollRad),  sr = std::sin(rollRad);

        auto xformVec = [&](float vx, float vy, float vz, float& ox, float& oy, float& oz) {
            // 1. Roll around longitudinal Z
            float x1 = vx * cr - vy * sr;
            float y1 = vx * sr + vy * cr;
            float z1 = vz;
            // 2. Pitch around lateral X (positive pitch lifts nose at vz < 0)
            float x2 = x1;
            float y2 = y1 * cp - z1 * sp;
            float z2 = y1 * sp + z1 * cp;
            // 3. Yaw around vertical Y (positive yaw turns nose at vz < 0 toward +X)
            ox = x2 * cy - z2 * sy;
            oy = y2;
            oz = x2 * sy + z2 * cy;
        };

        auto addPart = [&](float lx, float lyBottom, float lz,
                           float wx, float wy, float wz,
                           const Color3& c, float emis = 0.0f) {
            const float hx = wx * 0.5f;
            const float hz = wz * 0.5f;
            const float y0 = lyBottom;
            const float y1 = lyBottom + wy;

            float P[8][3];
            const float localPts[8][3] = {
                {lx - hx, y0, lz - hz}, // 0: front-bottom-left
                {lx + hx, y0, lz - hz}, // 1: front-bottom-right
                {lx + hx, y1, lz - hz}, // 2: front-top-right
                {lx - hx, y1, lz - hz}, // 3: front-top-left
                {lx - hx, y0, lz + hz}, // 4: rear-bottom-left
                {lx + hx, y0, lz + hz}, // 5: rear-bottom-right
                {lx + hx, y1, lz + hz}, // 6: rear-top-right
                {lx - hx, y1, lz + hz}  // 7: rear-top-left
            };
            for (int i = 0; i < 8; ++i) {
                float rx, ry, rz;
                xformVec(localPts[i][0], localPts[i][1], localPts[i][2], rx, ry, rz);
                P[i][0] = x + rx;
                P[i][1] = y + ry;
                P[i][2] = z + rz;
            }

            auto emitFace = [&](int a, int b, int d, int e, float lnx, float lny, float lnz) {
                float nx, ny, nz;
                xformVec(lnx, lny, lnz, nx, ny, nz);
                pushQuad3D(m_verts3D,
                           P[a][0], P[a][1], P[a][2],
                           P[b][0], P[b][1], P[b][2],
                           P[d][0], P[d][1], P[d][2],
                           P[e][0], P[e][1], P[e][2],
                           nx, ny, nz, c, emis);
            };

            emitFace(0, 1, 2, 3,  0.0f,  0.0f, -1.0f); // Front nose (-Z)
            emitFace(5, 4, 7, 6,  0.0f,  0.0f,  1.0f); // Rear tail (+Z)
            emitFace(4, 0, 3, 7, -1.0f,  0.0f,  0.0f); // Left side (-X)
            emitFace(1, 5, 6, 2,  1.0f,  0.0f,  0.0f); // Right side (+X)
            emitFace(3, 2, 6, 7,  0.0f,  1.0f,  0.0f); // Top (+Y)
        };

        const Color3 darkTrim{0.11f, 0.12f, 0.15f};
        const Color3 glassCol{0.17f, 0.24f, 0.33f};
        const Color3 chrome{0.82f, 0.85f, 0.90f};
        const Color3 headGlow{1.0f, 0.96f, 0.82f};

        auto addWheels = [&](float trackX, float wbFrontZ, float wbRearZ, float tireDiam) {
            for (float wx : {-trackX, trackX}) {
                for (float wz : {wbFrontZ, wbRearZ}) {
                    addPart(wx, 0.02f, wz, 0.26f, tireDiam, tireDiam, darkTrim);
                    float hubX = wx + (wx < 0.0f ? -0.02f : 0.02f);
                    addPart(hubX, 0.02f + tireDiam * 0.22f, wz, 0.25f, tireDiam * 0.56f, tireDiam * 0.56f, chrome);
                }
            }
        };

        if (isPlayer) {
            if (m_snap.carIndex == 0) {
                // '68 Canyon Cruiser (Cherry-Red Vintage Fastback Coupe)
                Color3 cherry{0.78f, 0.12f, 0.14f};
                addPart(0.0f, 0.28f,  0.0f,  1.86f, 0.52f, 4.56f, cherry);       // Sculpted lower body
                addPart(0.0f, 0.80f, -1.05f, 0.86f, 0.09f, 1.55f, cherry);       // Classic V8 hood power-bulge
                addPart(0.0f, 0.80f,  0.22f, 1.54f, 0.44f, 1.95f, glassCol);     // Cabin greenhouse
                addPart(0.0f, 1.21f,  0.26f, 1.48f, 0.07f, 1.65f, cherry);       // Fastback roof
                addPart(0.0f, 0.25f,  2.31f, 1.92f, 0.15f, 0.14f, chrome);       // Rear chrome bumper
                addPart(0.0f, 0.25f, -2.31f, 1.92f, 0.15f, 0.14f, chrome);       // Front chrome bumper
                addPart(-0.96f, 0.76f, -0.55f, 0.14f, 0.10f, 0.14f, chrome);     // Chrome side mirrors
                addPart( 0.96f, 0.76f, -0.55f, 0.14f, 0.10f, 0.14f, chrome);
                for (float tx : {-0.68f, -0.44f, 0.44f, 0.68f}) {
                    addPart(tx, 0.50f, 2.29f, 0.15f, 0.14f, 0.06f, {1.0f, 0.12f, 0.10f}, 0.95f);
                }
                addPart(-0.66f, 0.48f, -2.29f, 0.24f, 0.18f, 0.06f, headGlow, m_snap.headlightsOn ? 0.98f : 0.2f);
                addPart( 0.66f, 0.48f, -2.29f, 0.24f, 0.18f, 0.06f, headGlow, m_snap.headlightsOn ? 0.98f : 0.2f);
                addWheels(0.88f, -1.38f, 1.38f, 0.64f);
            } else if (m_snap.carIndex == 1) {
                // '94 Nomad Wagon (Sage-Green Comfy Touring Estate + Roof Box)
                Color3 sage{0.26f, 0.48f, 0.38f};
                addPart(0.0f, 0.28f, 0.0f,  1.84f, 0.54f, 4.66f, sage);
                addPart(0.0f, 0.24f, 0.0f,  1.88f, 0.16f, 4.72f, darkTrim);      // Rugged touring lower cladding
                addPart(0.0f, 0.82f, 0.40f, 1.62f, 0.50f, 2.88f, glassCol);      // Long wagon greenhouse
                addPart(0.0f, 1.30f, 0.40f, 1.58f, 0.07f, 2.78f, sage);          // Wagon roof
                addPart(0.0f, 1.38f, 0.46f, 0.86f, 0.24f, 1.80f, {0.76f, 0.79f, 0.83f}); // Rooftop cargo box
                addPart(-0.73f, 0.48f, 2.34f, 0.26f, 0.38f, 0.06f, {1.0f, 0.16f, 0.08f}, 0.95f);
                addPart( 0.73f, 0.48f, 2.34f, 0.26f, 0.38f, 0.06f, {1.0f, 0.16f, 0.08f}, 0.95f);
                addPart(-0.66f, 0.50f, -2.34f, 0.32f, 0.18f, 0.06f, headGlow, m_snap.headlightsOn ? 0.98f : 0.2f);
                addPart( 0.66f, 0.50f, -2.34f, 0.32f, 0.18f, 0.06f, headGlow, m_snap.headlightsOn ? 0.98f : 0.2f);
                addWheels(0.87f, -1.40f, 1.40f, 0.64f);
            } else {
                // Apex GT-RS (Sunburst Gold Mid-Engine Sport Coupe + GT Wing)
                Color3 gold{0.96f, 0.72f, 0.10f};
                addPart(0.0f, 0.22f,  0.0f,  1.96f, 0.44f, 4.38f, gold);
                addPart(0.0f, 0.66f,  0.08f, 1.48f, 0.38f, 1.82f, darkTrim);     // Sleek teardrop canopy
                addPart(0.0f, 1.02f,  0.12f, 1.36f, 0.05f, 1.35f, gold);         // Coupe roof panel
                addPart(-0.60f, 0.66f, 1.92f, 0.08f, 0.32f, 0.22f, darkTrim);    // GT wing uprights
                addPart( 0.60f, 0.66f, 1.92f, 0.08f, 0.32f, 0.22f, darkTrim);
                addPart(0.0f,   0.96f, 1.96f, 1.88f, 0.06f, 0.30f, darkTrim);    // Carbon GT wing blade
                addPart(0.0f,   0.46f, 2.20f, 1.74f, 0.09f, 0.06f, {1.0f, 0.10f, 0.22f}, 1.0f);
                addPart(-0.68f, 0.42f, -2.20f, 0.30f, 0.12f, 0.06f, headGlow, m_snap.headlightsOn ? 0.98f : 0.2f);
                addPart( 0.68f, 0.42f, -2.20f, 0.30f, 0.12f, 0.06f, headGlow, m_snap.headlightsOn ? 0.98f : 0.2f);
                addWheels(0.92f, -1.32f, 1.32f, 0.62f);
            }
            return;
        }

        // Ambient AI Traffic Vehicles (fully oriented in 3D along road yaw & slope!)
        const float len = (style == 2) ? 6.6f : 4.45f;
        const float bodyH = (style == 2) ? 2.25f : (style == 1 ? 1.25f : 0.56f);
        addPart(0.0f, 0.28f, 0.0f, 1.86f, bodyH, len, bodyCol);
        if (style == 0) {
            addPart(0.0f, 0.84f, 0.15f, 1.56f, 0.44f, 2.05f, glassCol);
            addPart(0.0f, 1.26f, 0.18f, 1.50f, 0.06f, 1.75f, bodyCol);
        } else if (style == 1) {
            addPart(0.0f, 0.84f, -0.35f, 1.66f, 0.52f, 2.95f, glassCol);
        }
        // Front Headlights (-Z in vehicle space) & Rear Taillights (+Z in vehicle space)
        const float zFront = -len * 0.505f;
        const float zRear  =  len * 0.505f;
        addPart(-0.66f, 0.50f, zFront, 0.26f, 0.16f, 0.06f, headGlow, 0.98f);
        addPart( 0.66f, 0.50f, zFront, 0.26f, 0.16f, 0.06f, headGlow, 0.98f);
        addPart(-0.66f, 0.52f, zRear,  0.26f, 0.16f, 0.06f, {1.0f, 0.12f, 0.10f}, 0.98f);
        addPart( 0.66f, 0.52f, zRear,  0.26f, 0.16f, 0.06f, {1.0f, 0.12f, 0.10f}, 0.98f);
        addWheels(0.86f, -len * 0.31f, len * 0.31f, 0.62f);
    }

    void buildPropAt(PropKind kind, float x, float y, float z, int64_t seed) {
        const float nightGlow = (m_snap.nightFactor > 0.22f) ? 0.95f : 0.0f;
        const float scaleVar = 0.82f + hashFloat(seed, 71u) * 0.45f;

        switch (kind) {
            case PropKind::RedwoodTree: {
                float th = 9.5f * scaleVar;
                pushBox3D(m_verts3D, x, y, z, 0.75f, th * 0.38f, 0.75f, {0.34f, 0.20f, 0.12f});
                Color3 pine1{0.10f, 0.30f, 0.15f};
                Color3 pine2{0.13f, 0.36f, 0.18f};
                if (m_snap.biomeA == BiomeId::AlpineSummit || m_snap.snowAmount > 0.3f) {
                    pine1 = {0.65f, 0.76f, 0.82f};
                    pine2 = {0.82f, 0.88f, 0.94f};
                }
                pushPyramidCone(m_verts3D, x, y + th * 0.25f, z, 2.6f * scaleVar, th * 0.40f, pine1);
                pushPyramidCone(m_verts3D, x, y + th * 0.50f, z, 2.0f * scaleVar, th * 0.36f, pine2);
                pushPyramidCone(m_verts3D, x, y + th * 0.72f, z, 1.3f * scaleVar, th * 0.32f, pine1);
                break;
            }
            case PropKind::MapleTree: {
                pushBox3D(m_verts3D, x, y, z, 0.52f, 2.6f * scaleVar, 0.52f, {0.36f, 0.24f, 0.15f});
                float pick = hashFloat(seed, 19u);
                Color3 leaf = (pick < 0.35f) ? Color3{0.88f, 0.42f, 0.14f} : // Autumn orange!
                              (pick < 0.62f) ? Color3{0.85f, 0.66f, 0.16f} : // Golden maple!
                                               Color3{0.22f, 0.52f, 0.22f};  // Lush green!
                pushBox3D(m_verts3D, x, y + 2.2f * scaleVar, z, 3.4f * scaleVar, 2.9f * scaleVar, 3.4f * scaleVar, leaf);
                pushBox3D(m_verts3D, x, y + 4.4f * scaleVar, z, 2.2f * scaleVar, 1.4f * scaleVar, 2.2f * scaleVar, leaf);
                break;
            }
            case PropKind::SaguaroCactus: {
                Color3 cac{0.22f, 0.48f, 0.24f};
                float h = 4.2f * scaleVar;
                pushBox3D(m_verts3D, x, y, z, 0.58f, h, 0.58f, cac);
                pushBox3D(m_verts3D, x - 0.85f, y + h * 0.45f, z, 1.15f, 0.42f, 0.42f, cac);
                pushBox3D(m_verts3D, x - 1.25f, y + h * 0.45f, z, 0.44f, h * 0.40f, 0.44f, cac);
                pushBox3D(m_verts3D, x + 0.80f, y + h * 0.58f, z, 1.05f, 0.42f, 0.42f, cac);
                pushBox3D(m_verts3D, x + 1.15f, y + h * 0.58f, z, 0.44f, h * 0.34f, 0.44f, cac);
                break;
            }
            case PropKind::RedBarnSilo: {
                // Classic Midwest Red Barn + White Doors + Pitched Roof + Grain Silo!
                pushBox3D(m_verts3D, x, y, z, 8.5f, 5.2f, 10.5f, {0.72f, 0.14f, 0.13f});
                pushPyramidCone(m_verts3D, x, y + 5.2f, z, 5.8f, 3.4f, {0.26f, 0.22f, 0.20f});
                pushBox3D(m_verts3D, x, y + 0.2f, z + 5.3f, 3.2f, 3.4f, 0.12f, {0.92f, 0.92f, 0.90f});
                pushBox3D(m_verts3D, x, y + 3.9f, z + 5.3f, 1.4f, 1.1f, 0.14f, {1.0f, 0.82f, 0.42f}, nightGlow);
                // Tall Silver Silo right beside barn
                pushBox3D(m_verts3D, x + 6.2f, y, z - 1.5f, 2.9f, 10.5f, 2.9f, {0.72f, 0.76f, 0.80f});
                pushPyramidCone(m_verts3D, x + 6.2f, y + 10.5f, z - 1.5f, 1.7f, 1.8f, {0.84f, 0.86f, 0.90f});
                break;
            }
            case PropKind::Windmill: {
                pushBox3D(m_verts3D, x, y, z, 0.85f, 13.5f, 0.85f, {0.85f, 0.87f, 0.90f});
                float ang = m_snap.worldTimeSec * 1.4f + static_cast<float>(seed % 7);
                for (int b = 0; b < 3; ++b) {
                    float a = ang + b * (2.0f * kPi / 3.0f);
                    float bx = x + std::cos(a) * 3.2f;
                    float by = y + 13.2f + std::sin(a) * 3.2f;
                    pushTri3D(m_verts3D, x, y + 13.2f, z + 0.55f,
                              bx, by, z + 0.55f,
                              x + std::cos(a + 0.14f) * 1.6f, y + 13.2f + std::sin(a + 0.14f) * 1.6f, z + 0.55f,
                              0, 0, 1, {0.95f, 0.96f, 0.98f});
                }
                break;
            }
            case PropKind::SuburbanHouse: {
                static const Color3 walls[4] = {
                    {0.88f, 0.85f, 0.76f}, {0.68f, 0.76f, 0.84f},
                    {0.76f, 0.48f, 0.40f}, {0.76f, 0.82f, 0.72f}
                };
                Color3 wc = walls[seed & 3];
                pushBox3D(m_verts3D, x, y, z, 7.6f, 4.6f, 7.2f, wc);
                pushPyramidCone(m_verts3D, x, y + 4.6f, z, 4.6f, 2.7f, {0.26f, 0.24f, 0.27f});
                // Glowing warm living-room windows!
                Color3 winCol = (nightGlow > 0.1f) ? Color3{1.0f, 0.80f, 0.42f} : Color3{0.25f, 0.36f, 0.48f};
                float faceZ = (x < 0.0f) ? (z + 0.0f) : z;
                float sideX = (x < 0.0f) ? (x + 3.85f) : (x - 3.85f);
                pushBox3D(m_verts3D, sideX, y + 1.6f, faceZ - 1.6f, 0.12f, 1.3f, 1.6f, winCol, nightGlow);
                pushBox3D(m_verts3D, sideX, y + 1.6f, faceZ + 1.6f, 0.12f, 1.3f, 1.6f, winCol, nightGlow);
                break;
            }
            case PropKind::Skyscraper: {
                float h = 22.0f + hashFloat(seed, 99u) * 48.0f;
                float w = 9.0f + hashFloat(seed, 101u) * 6.5f;
                Color3 glassTower = ((seed & 1) == 0) ? Color3{0.18f, 0.24f, 0.34f} : Color3{0.28f, 0.28f, 0.32f};
                pushBox3D(m_verts3D, x, y, z, w, h, w, glassTower);
                // Illuminated vertical & horizontal office/apartment window bands!
                float faceX = (x < 0.0f) ? (x + w * 0.51f) : (x - w * 0.51f);
                Color3 litWin = ((seed % 3) == 0) ? Color3{0.45f, 0.88f, 1.0f} : Color3{1.0f, 0.84f, 0.52f};
                float winEmis = 0.35f + nightGlow * 0.65f;
                for (float wy = 4.0f; wy < h - 3.0f; wy += 5.5f) {
                    pushBox3D(m_verts3D, faceX, y + wy, z, 0.14f, 2.1f, w * 0.76f, litWin, winEmis);
                }
                // Red rooftop aircraft warning beacon!
                pushBox3D(m_verts3D, x, y + h, z, 0.65f, 0.65f, 0.65f, {1.0f, 0.15f, 0.18f}, 0.95f);
                break;
            }
            case PropKind::DesertMesa: {
                float s = 1.0f + hashFloat(seed, 55u) * 1.4f;
                pushBox3D(m_verts3D, x, y, z, 18.0f * s, 9.0f * s, 16.0f * s, {0.72f, 0.32f, 0.19f});
                pushBox3D(m_verts3D, x, y + 9.0f * s, z, 12.0f * s, 7.5f * s, 11.0f * s, {0.80f, 0.39f, 0.22f});
                pushBox3D(m_verts3D, x, y + 16.5f * s, z, 7.5f * s, 5.0f * s, 7.0f * s, {0.86f, 0.48f, 0.26f});
                break;
            }
            case PropKind::Streetlight: {
                float armDir = (x < 0.0f) ? 1.0f : -1.0f;
                pushBox3D(m_verts3D, x, y, z, 0.22f, 7.2f, 0.22f, {0.35f, 0.38f, 0.42f});
                pushBox3D(m_verts3D, x + armDir * 1.1f, y + 7.1f, z, 2.3f, 0.16f, 0.20f, {0.35f, 0.38f, 0.42f});
                pushBox3D(m_verts3D, x + armDir * 2.0f, y + 6.90f, z, 0.55f, 0.22f, 0.35f,
                          {1.0f, 0.85f, 0.52f}, (m_snap.nightFactor > 0.15f) ? 1.0f : 0.2f);
                break;
            }
            case PropKind::HighwaySign: {
                // Classic Overhead Green Interstate Highway Gantry!
                pushBox3D(m_verts3D, x - 7.0f, y, z, 0.35f, 6.8f, 0.35f, {0.62f, 0.66f, 0.70f});
                pushBox3D(m_verts3D, x + 7.0f, y, z, 0.35f, 6.8f, 0.35f, {0.62f, 0.66f, 0.70f});
                pushBox3D(m_verts3D, x, y + 6.4f, z, 14.4f, 0.28f, 0.28f, {0.62f, 0.66f, 0.70f});
                pushBox3D(m_verts3D, x, y + 5.1f, z + 0.15f, 9.2f, 2.3f, 0.18f, {0.06f, 0.44f, 0.26f}, 0.25f);
                pushBox3D(m_verts3D, x, y + 6.0f, z + 0.26f, 7.4f, 0.35f, 0.06f, {0.95f, 0.97f, 0.96f}, 0.65f);
                pushBox3D(m_verts3D, x, y + 5.4f, z + 0.26f, 5.2f, 0.28f, 0.06f, {0.95f, 0.97f, 0.96f}, 0.65f);
                break;
            }
            case PropKind::SnowBoulder: {
                pushBox3D(m_verts3D, x, y, z, 3.2f * scaleVar, 2.2f * scaleVar, 2.8f * scaleVar, {0.46f, 0.50f, 0.56f});
                pushBox3D(m_verts3D, x, y + 2.0f * scaleVar, z, 2.6f * scaleVar, 0.45f, 2.3f * scaleVar, {0.90f, 0.94f, 0.98f});
                break;
            }
            case PropKind::FenceRow: {
                Color3 wood{0.48f, 0.34f, 0.20f};
                pushBox3D(m_verts3D, x, y, z, 0.18f, 1.15f, 0.18f, wood);
                pushBox3D(m_verts3D, x, y + 0.85f, z - 3.0f, 0.10f, 0.12f, 6.2f, wood);
                pushBox3D(m_verts3D, x, y + 0.48f, z - 3.0f, 0.10f, 0.12f, 6.2f, wood);
                break;
            }
        }
    }

    void buildWorldGeometry(double s0, float originX, float originY, float playerTotalYaw) {
        const float roadHalfW = 5.4f;
        const float shoulderW = 6.8f;
        const float midBandW  = 34.0f;
        const float farBandW  = 175.0f;
        const auto& pal = m_snap.blendedPalette;

        // 1) Continuous 3D Highway + Multi-Band Rolling Terrain Ribbon (-16m behind .. +236m ahead)
        constexpr int kNumSlices = 63;
        constexpr double kStepS  = 4.0;
        const double startS = std::floor((s0 - 16.0) / kStepS) * kStepS;

        struct RingPts {
            float glZ;
            float cx, cy;
            float hL2, hL1, hR1, hR2;
        };
        std::vector<RingPts> rings(kNumSlices + 1);
        for (int i = 0; i <= kNumSlices; ++i) {
            double s = startS + i * kStepS;
            float rx = WorldSim::roadCenterX(s) - originX;
            float ry = WorldSim::roadElevationY(s) - originY;
            float glZ = -static_cast<float>(s - s0);

            float hL1 = ry + WorldSim::terrainSideProfile(m_snap.biomeA, s, midBandW) * (1.0f - m_snap.biomeBlend) +
                             WorldSim::terrainSideProfile(m_snap.biomeB, s, midBandW) * m_snap.biomeBlend;
            float hR1 = ry + WorldSim::terrainSideProfile(m_snap.biomeA, s + 37.0, midBandW) * (1.0f - m_snap.biomeBlend) +
                             WorldSim::terrainSideProfile(m_snap.biomeB, s + 37.0, midBandW) * m_snap.biomeBlend;
            float hL2 = ry + WorldSim::terrainSideProfile(m_snap.biomeA, s, farBandW) * (1.0f - m_snap.biomeBlend) +
                             WorldSim::terrainSideProfile(m_snap.biomeB, s, farBandW) * m_snap.biomeBlend;
            float hR2 = ry + WorldSim::terrainSideProfile(m_snap.biomeA, s + 71.0, farBandW) * (1.0f - m_snap.biomeBlend) +
                             WorldSim::terrainSideProfile(m_snap.biomeB, s + 71.0, farBandW) * m_snap.biomeBlend;
            rings[i] = {glZ, rx, ry, hL2, hL1, hR1, hR2};
        }

        const Color3 asphalt{0.18f, 0.19f, 0.22f};
        const Color3 edgeLine{0.90f, 0.92f, 0.94f};
        const Color3 centerLine = (m_snap.biomeA == BiomeId::DowntownSkyline)
                                      ? Color3{0.92f, 0.94f, 0.96f}
                                      : Color3{0.95f, 0.76f, 0.18f};

        for (int i = 0; i < kNumSlices; ++i) {
            const auto& r0 = rings[i];
            const auto& r1 = rings[i + 1];
            const int64_t segIdx = static_cast<int64_t>(std::floor((startS + i * kStepS) / kStepS));

            // Far Terrain Skirts
            pushQuad3D(m_verts3D,
                       r0.cx - farBandW, r0.hL2, r0.glZ, r0.cx - midBandW, r0.hL1, r0.glZ,
                       r1.cx - midBandW, r1.hL1, r1.glZ, r1.cx - farBandW, r1.hL2, r1.glZ,
                       0.2f, 0.95f, 0.0f, pal.grassFar);
            pushQuad3D(m_verts3D,
                       r0.cx + midBandW, r0.hR1, r0.glZ, r0.cx + farBandW, r0.hR2, r0.glZ,
                       r1.cx + farBandW, r1.hR2, r1.glZ, r1.cx + midBandW, r1.hR1, r1.glZ,
                       -0.2f, 0.95f, 0.0f, pal.grassFar);

            // Near Terrain Bands
            pushQuad3D(m_verts3D,
                       r0.cx - midBandW, r0.hL1, r0.glZ, r0.cx - shoulderW, r0.cy - 0.04f, r0.glZ,
                       r1.cx - shoulderW, r1.cy - 0.04f, r1.glZ, r1.cx - midBandW, r1.hL1, r1.glZ,
                       0.15f, 0.98f, 0.0f, pal.grassNear);
            pushQuad3D(m_verts3D,
                       r0.cx + shoulderW, r0.cy - 0.04f, r0.glZ, r0.cx + midBandW, r0.hR1, r0.glZ,
                       r1.cx + midBandW, r1.hR1, r1.glZ, r1.cx + shoulderW, r1.cy - 0.04f, r1.glZ,
                       -0.15f, 0.98f, 0.0f, pal.grassNear);

            // Gravel / Dirt / Concrete Shoulders
            pushQuad3D(m_verts3D,
                       r0.cx - shoulderW, r0.cy - 0.04f, r0.glZ, r0.cx - roadHalfW, r0.cy, r0.glZ,
                       r1.cx - roadHalfW, r1.cy, r1.glZ, r1.cx - shoulderW, r1.cy - 0.04f, r1.glZ,
                       0, 1, 0, pal.dirtShoulder);
            pushQuad3D(m_verts3D,
                       r0.cx + roadHalfW, r0.cy, r0.glZ, r0.cx + shoulderW, r0.cy - 0.04f, r0.glZ,
                       r1.cx + shoulderW, r1.cy - 0.04f, r1.glZ, r1.cx + roadHalfW, r1.cy, r1.glZ,
                       0, 1, 0, pal.dirtShoulder);

            // Main Two-Lane Asphalt Highway Surface
            pushQuad3D(m_verts3D,
                       r0.cx - roadHalfW, r0.cy, r0.glZ, r0.cx + roadHalfW, r0.cy, r0.glZ,
                       r1.cx + roadHalfW, r1.cy, r1.glZ, r1.cx - roadHalfW, r1.cy, r1.glZ,
                       0, 1, 0, asphalt);

            // Crisp White Outer Fog-Lines
            const float yMark0 = r0.cy + 0.018f;
            const float yMark1 = r1.cy + 0.018f;
            for (float side : {-1.0f, 1.0f}) {
                float lx = side * (roadHalfW - 0.28f);
                pushQuad3D(m_verts3D,
                           r0.cx + lx - 0.09f, yMark0, r0.glZ, r0.cx + lx + 0.09f, yMark0, r0.glZ,
                           r1.cx + lx + 0.09f, yMark1, r1.glZ, r1.cx + lx - 0.09f, yMark1, r1.glZ,
                           0, 1, 0, edgeLine, 0.12f);
            }

            // Dashed Center Highway Line + Night Reflective Cat's-Eye Studs!
            if ((segIdx & 1) == 0) {
                pushQuad3D(m_verts3D,
                           r0.cx - 0.10f, yMark0, r0.glZ, r0.cx + 0.10f, yMark0, r0.glZ,
                           r1.cx + 0.10f, yMark1, r1.glZ, r1.cx - 0.10f, yMark1, r1.glZ,
                           0, 1, 0, centerLine, 0.20f);
            } else if (m_snap.nightFactor > 0.25f) {
                pushBox3D(m_verts3D, r0.cx, yMark0, r0.glZ, 0.14f, 0.05f, 0.18f, {1.0f, 0.78f, 0.22f}, 0.95f);
            }
        }

        // 2) Distant Horizon 3D Mountain Range Silhouette Crown
        const float mtnDist = 235.0f;
        const float fwdCenterX = WorldSim::roadCenterX(s0 + mtnDist) - originX;
        const float fwdBaseY = WorldSim::roadElevationY(s0 + mtnDist) - originY - 8.0f;
        for (int m = -7; m <= 7; ++m) {
            float mx = fwdCenterX + m * 38.0f;
            float mh = pal.mountainHeight * (0.65f + 0.7f * std::abs(std::sin(m * 1.7f + 0.4f)));
            if (mh > 4.0f) {
                pushPyramidCone(m_verts3D, mx, fwdBaseY, -mtnDist, 32.0f, mh, pal.mountainCol);
            }
        }

        // 3) Deterministic Biome Scenery Props Along Road Slices!
        constexpr double kPropSpacing = 12.0;
        const int64_t firstSlice = static_cast<int64_t>(std::floor((s0 - 12.0) / kPropSpacing));
        const int64_t lastSlice  = static_cast<int64_t>(std::floor((s0 + 215.0) / kPropSpacing));

        for (int64_t sl = firstSlice; sl <= lastSlice; ++sl) {
            const double ps = sl * kPropSpacing;
            const float glZ = -static_cast<float>(ps - s0);
            const float rcx = WorldSim::roadCenterX(ps) - originX;
            const float rcy = WorldSim::roadElevationY(ps) - originY;
            const BiomeId b = WorldSim::biomeAtDistance(ps, m_snap.biomeA == m_snap.biomeB && m_snap.biomeBlend == 0.0f ? static_cast<int>(m_snap.biomeA) : -1);

            // Overhead Interstate Welcome Sign at Biome Borders!
            if (sl > 0 && (sl % static_cast<int64_t>(1500.0 / kPropSpacing)) == 0) {
                buildPropAt(PropKind::HighwaySign, rcx, rcy, glZ, sl);
            }

            for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
                const float side = (sideIdx == 0) ? -1.0f : 1.0f;
                const uint32_t salt = sideIdx * 977u + 13u;
                const float rPick = hashFloat(sl, salt);
                const float rDist = hashFloat(sl, salt + 31u);

                auto groundY = [&](float absOff) {
                    return rcy + WorldSim::terrainSideProfile(b, ps + sideIdx * 37.0, absOff);
                };

                if (b == BiomeId::PacificRedwoods) {
                    float lat = 9.5f + rDist * 26.0f;
                    buildPropAt(PropKind::RedwoodTree, rcx + side * lat, groundY(lat) - 0.3f, glZ, sl * 3 + sideIdx);
                    if (rPick > 0.35f) {
                        float lat2 = 24.0f + hashFloat(sl, salt + 7u) * 32.0f;
                        buildPropAt(PropKind::RedwoodTree, rcx + side * lat2, groundY(lat2) - 0.3f, glZ - 4.0f, sl * 7 + sideIdx);
                    }
                } else if (b == BiomeId::HeartlandFarms) {
                    // Continuous split-rail farm fences along both shoulders!
                    if ((sl & 1) == 0) {
                        buildPropAt(PropKind::FenceRow, rcx + side * 7.6f, rcy, glZ, sl);
                    }
                    if (rPick < 0.16f) {
                        float lat = 22.0f + rDist * 16.0f;
                        buildPropAt(PropKind::RedBarnSilo, rcx + side * lat, groundY(lat) - 0.3f, glZ, sl + sideIdx);
                    } else if (rPick < 0.28f) {
                        float lat = 19.0f + rDist * 22.0f;
                        buildPropAt(PropKind::Windmill, rcx + side * lat, groundY(lat) - 0.2f, glZ, sl + sideIdx);
                    } else if (rPick > 0.72f) {
                        float lat = 14.0f + rDist * 24.0f;
                        buildPropAt(PropKind::MapleTree, rcx + side * lat, groundY(lat) - 0.2f, glZ, sl * 5 + sideIdx);
                    }
                } else if (b == BiomeId::SonoranCanyon) {
                    if (rPick < 0.55f) {
                        float lat = 10.0f + rDist * 24.0f;
                        buildPropAt(PropKind::SaguaroCactus, rcx + side * lat, groundY(lat) - 0.2f, glZ, sl * 3 + sideIdx);
                    } else if (rPick > 0.80f) {
                        float lat = 44.0f + rDist * 38.0f;
                        buildPropAt(PropKind::DesertMesa, rcx + side * lat, groundY(lat) - 1.0f, glZ, sl + sideIdx);
                    }
                } else if (b == BiomeId::QuietSuburbia) {
                    if ((sl % 2) == 0 && sideIdx == 1) {
                        buildPropAt(PropKind::Streetlight, rcx + side * 6.9f, rcy, glZ, sl);
                    }
                    if ((sl % 2) == 0) {
                        float lat = 16.5f + rDist * 5.0f;
                        buildPropAt(PropKind::SuburbanHouse, rcx + side * lat, groundY(lat) - 0.2f, glZ, sl * 3 + sideIdx);
                    }
                    if (rPick > 0.35f) {
                        float lat = 9.8f + rDist * 4.0f;
                        buildPropAt(PropKind::MapleTree, rcx + side * lat, groundY(lat) - 0.2f, glZ - 3.5f, sl * 11 + sideIdx);
                    }
                } else if (b == BiomeId::DowntownSkyline) {
                    if ((sl % 2) == 0) {
                        buildPropAt(PropKind::Streetlight, rcx + side * 6.6f, rcy, glZ, sl);
                    }
                    float lat = 15.5f + rDist * 14.0f;
                    buildPropAt(PropKind::Skyscraper, rcx + side * lat, 0.0f + rcy, glZ, sl * 5 + sideIdx);
                    if (rPick > 0.45f) {
                        float lat2 = 34.0f + rDist * 18.0f;
                        buildPropAt(PropKind::Skyscraper, rcx + side * lat2, 0.0f + rcy, glZ - 5.0f, sl * 13 + sideIdx);
                    }
                } else if (b == BiomeId::AlpineSummit) {
                    float lat = 10.0f + rDist * 24.0f;
                    if (rPick < 0.68f) {
                        buildPropAt(PropKind::RedwoodTree, rcx + side * lat, groundY(lat) - 0.3f, glZ, sl * 3 + sideIdx);
                    } else {
                        buildPropAt(PropKind::SnowBoulder, rcx + side * (9.0f + rDist * 10.0f), groundY(9.0f + rDist * 10.0f) - 0.3f, glZ, sl);
                    }
                }
            }
        }

        // 4) Ambient AI Traffic Cars
        for (const auto& tc : m_snap.traffic) {
            if (tc.s < s0 - 15.0 || tc.s > s0 + 220.0) continue;
            float tx = WorldSim::roadCenterX(tc.s) + tc.laneOffset - originX;
            float ty = WorldSim::roadElevationY(tc.s) - originY;
            float tGlZ = -static_cast<float>(tc.s - s0);
            float tRoadYaw = std::atan(WorldSim::roadSlopeDxDs(tc.s));
            float tRoadPitch = std::atan(WorldSim::roadSlopeDyDs(tc.s));
            float vehYaw = (tc.speedMs >= 0.0f) ? tRoadYaw : (tRoadYaw + kPi);
            float vehPitch = (tc.speedMs >= 0.0f) ? tRoadPitch : -tRoadPitch;
            buildVehicleMesh(tx, ty, tGlZ, vehYaw, vehPitch, 0.0f, tc.bodyType, tc.color, false);
        }

        // 5) 3rd-Person Player Car Mesh (rigidly aligned with road slope + body pitch & cornering roll!)
        if (m_snap.cameraMode == 1) {
            const float pPitch = std::atan(WorldSim::roadSlopeDyDs(s0)) + m_snap.bodyPitchRad;
            buildVehicleMesh(0.0f, 0.0f, 0.0f, playerTotalYaw, pPitch, m_snap.bodyRollRad, 0, {1, 1, 1}, true);
        }
    }

    void buildPrecipitationOverlay(float aspect) {
        if (m_snap.rainAmount < 0.03f && m_snap.snowAmount < 0.03f) return;
        const bool isSnow = (m_snap.snowAmount > m_snap.rainAmount);
        const float alpha = isSnow ? (m_snap.snowAmount * 0.72f) : (m_snap.rainAmount * 0.42f);
        const float invA = 1.0f / std::max(0.5f, aspect);

        for (const auto& d : m_snap.precip) {
            float invZ = 1.0f / std::max(1.2f, d.z);
            float sx = (d.x * invZ) * 1.4f * invA;
            float sy = ((d.y - 2.5f) * invZ) * 1.4f;
            if (sx < -1.1f || sx > 1.1f || sy < -1.1f || sy > 1.1f) continue;

            if (isSnow) {
                float r = 0.014f * (8.0f * invZ);
                pushDisc2D(m_verts2D, sx, sy, r * invA, r, 6, 0.94f, 0.97f, 1.0f, alpha);
            } else {
                pushLine2D(m_verts2D, sx, sy, sx - 0.015f, sy - 0.095f, 0.0032f, invA, 0.78f, 0.86f, 0.98f, alpha);
            }
        }
    }

    void buildCockpitInterior(float aspect) {
        const float invA = 1.0f / std::max(0.55f, aspect);
        // Subtle parallax shift when free-looking around cabin!
        const float px = -m_snap.freeLookYaw * 0.55f;
        const float py = -m_snap.freeLookPitch * 0.55f;
        const float ambientDim = 1.0f - m_snap.nightFactor * 0.62f;

        auto lit = [&](float r, float g, float b) -> Color3 {
            return {r * ambientDim, g * ambientDim, b * ambientDim};
        };

        // 1) Sculpted Car Hood visible just outside the windshield!
        Color3 hoodCol = (m_snap.carIndex == 0) ? lit(0.78f, 0.12f, 0.14f) :
                         (m_snap.carIndex == 1) ? lit(0.25f, 0.46f, 0.36f) :
                                                  lit(0.95f, 0.70f, 0.10f);
        pushQuad2D(m_verts2D,
                   -0.78f + px, -0.44f + py,  0.86f + px, -0.44f + py,
                    0.56f + px, -0.25f + py, -0.50f + px, -0.25f + py,
                   hoodCol.r, hoodCol.g, hoodCol.b, 1.0f);

        // 2) Animated Windshield Wipers!
        if (m_snap.wipersOn || m_snap.wiperPhase > 0.04f) {
            float sweep = (0.5f - 0.5f * std::cos(m_snap.wiperPhase)) * 1.95f + 0.35f; // rad from right horizontal
            float len = 0.64f;
            Color3 blade = lit(0.10f, 0.11f, 0.13f);
            for (float pivotX : {-0.46f, 0.14f}) {
                float x0 = pivotX + px;
                float y0 = -0.42f + py;
                float x1 = x0 - std::cos(sweep) * len * invA;
                float y1 = y0 + std::sin(sweep) * len;
                pushLine2D(m_verts2D, x0, y0, x1, y1, 0.009f, invA, blade.r, blade.g, blade.b, 0.95f);
            }
        }

        // 3) Cabin Roof Headliner, Left & Right A-Pillars & Center Rear-View Mirror!
        Color3 pillar = lit(0.13f, 0.13f, 0.16f);
        // Top headliner visor bar
        pushQuad2D(m_verts2D, -1.2f, 0.88f + py, 1.2f, 0.88f + py, 1.2f, 1.15f, -1.2f, 1.15f, pillar.r, pillar.g, pillar.b);
        // Left A-pillar
        pushQuad2D(m_verts2D, -1.20f, -0.46f + py, -0.88f + px, -0.46f + py, -0.74f + px, 0.92f + py, -1.20f, 0.92f + py, pillar.r, pillar.g, pillar.b);
        // Right A-pillar
        pushQuad2D(m_verts2D,  0.90f + px, -0.46f + py,  1.20f, -0.46f + py,  1.20f, 0.92f + py,  0.76f + px, 0.92f + py, pillar.r, pillar.g, pillar.b);

        // Center Rear-View Mirror (reflecting horizon sky tint!)
        pushQuad2D(m_verts2D, -0.02f + px, 0.78f + py, 0.02f + px, 0.78f + py, 0.02f + px, 0.92f + py, -0.02f + px, 0.92f + py, 0.08f, 0.08f, 0.10f);
        pushQuad2D(m_verts2D, -0.25f + px, 0.65f + py, 0.25f + px, 0.65f + py, 0.23f + px, 0.80f + py, -0.23f + px, 0.80f + py, 0.07f, 0.08f, 0.10f);
        pushQuad2D(m_verts2D, -0.23f + px, 0.67f + py, 0.23f + px, 0.67f + py, 0.21f + px, 0.78f + py, -0.21f + px, 0.78f + py,
                   m_snap.skyHorizon.r * 0.75f, m_snap.skyHorizon.g * 0.75f, m_snap.skyHorizon.b * 0.75f);

        // 4) Dashboard Upper Cowl & Car-Specific Fascia!
        Color3 dashTop = (m_snap.carIndex == 0) ? lit(0.16f, 0.12f, 0.10f) :
                         (m_snap.carIndex == 1) ? lit(0.18f, 0.19f, 0.21f) :
                                                  lit(0.11f, 0.11f, 0.14f);
        Color3 dashFace = (m_snap.carIndex == 0) ? lit(0.46f, 0.24f, 0.11f) : // Rich Walnut Woodgrain!
                          (m_snap.carIndex == 1) ? lit(0.26f, 0.24f, 0.22f) : // Cozy Touring Charcoal/Tan!
                                                   lit(0.14f, 0.15f, 0.19f);  // Matte Carbon / Alcantara!

        // Upper dashboard top pad + raised driver gauge binnacle arch
        pushQuad2D(m_verts2D, -1.25f, -1.15f, 1.25f, -1.15f, 1.25f, -0.43f + py, -1.25f, -0.43f + py, dashTop.r, dashTop.g, dashTop.b);
        pushDisc2D(m_verts2D, -0.30f + px, -0.46f + py, 0.36f * invA * 1.4f, 0.15f, 20, dashTop.r * 0.9f, dashTop.g * 0.9f, dashTop.b * 0.9f);

        // Main fascia trim panel
        pushQuad2D(m_verts2D, -1.20f, -0.98f, 1.20f, -0.98f, 1.20f, -0.50f + py, -1.20f, -0.50f + py, dashFace.r, dashFace.g, dashFace.b);
        if (m_snap.carIndex == 0) {
            // Classic '68 Chrome horizontal trim molding!
            Color3 chr = lit(0.85f, 0.87f, 0.90f);
            pushQuad2D(m_verts2D, -1.20f, -0.51f + py, 1.20f, -0.51f + py, 1.20f, -0.495f + py, -1.20f, -0.495f + py, chr.r, chr.g, chr.b);
        } else if (m_snap.carIndex == 2) {
            // Apex GT-RS Neon Cyan/Magenta accent stitch line!
            pushQuad2D(m_verts2D, -1.20f, -0.51f + py, 1.20f, -0.51f + py, 1.20f, -0.498f + py, -1.20f, -0.498f + py, 0.15f, 0.90f, 1.0f);
        }

        // 5) Working Instrument Cluster Gauges (Illuminated at Dusk/Night!)
        const float gx = -0.30f + px;
        const float gy = -0.58f + py;

        // Blinking Green Dashboard Turn-Signal Indicators during lane changes / Zen overtakes!
        const bool blinkLit = (std::fmod(m_snap.worldTimeSec * 2.4f, 1.0f) < 0.52f);
        for (int dir : {-1, 1}) {
            bool active = (m_snap.turnSignal == dir) && blinkLit;
            float ax = gx + dir * 0.24f * invA * 1.45f;
            float ay = gy + 0.115f;
            float cr = active ? 0.18f : 0.06f;
            float cg = active ? 0.98f : 0.18f;
            float cb = active ? 0.38f : 0.09f;
            pushTri2D(m_verts2D,
                      ax + dir * 0.032f * invA, ay,
                      ax - dir * 0.012f * invA, ay + 0.024f,
                      ax - dir * 0.012f * invA, ay - 0.024f,
                      cr, cg, cb, 0.95f);
        }

        if (m_snap.carIndex == 0 || m_snap.carIndex == 1) {
            // Twin Round Analog Speedometer & Tachometer Dials!
            Color3 dialGlow = (m_snap.carIndex == 0)
                                  ? Color3{0.98f, 0.76f, 0.42f}  // Warm Vintage Amber
                                  : Color3{0.35f, 0.95f, 0.58f}; // Cozy '90s Emerald Backlight!
            float faceBright = (m_snap.nightFactor > 0.2f) ? 0.24f : 0.10f;
            for (int d = 0; d < 2; ++d) {
                float dx = gx + (d == 0 ? -0.14f : 0.14f) * invA * 1.45f;
                float drx = 0.115f * invA * 1.45f;
                float dry = 0.115f;
                // Chrome / Matte Bezel + Illuminated Dial Face
                pushDisc2D(m_verts2D, dx, gy, drx * 1.12f, dry * 1.12f, 20, 0.55f, 0.58f, 0.62f);
                pushDisc2D(m_verts2D, dx, gy, drx, dry, 20,
                           dialGlow.r * faceBright, dialGlow.g * faceBright, dialGlow.b * faceBright);
                // Tick marks around dial arc
                for (int t = 0; t <= 6; ++t) {
                    float a = 3.8f - (t / 6.0f) * 4.4f;
                    pushLine2D(m_verts2D,
                               dx + std::cos(a) * drx * 0.76f, gy + std::sin(a) * dry * 0.76f,
                               dx + std::cos(a) * drx * 0.92f, gy + std::sin(a) * dry * 0.92f,
                               0.004f, invA, dialGlow.r, dialGlow.g, dialGlow.b);
                }
                // Live Sweeping Needle (Left = Speed 0..200 km/h, Right = RPM)
                float normVal = (d == 0) ? std::clamp(m_snap.speedKmh / 180.0f, 0.0f, 1.0f) : m_snap.rpmNorm;
                float needleAng = 3.8f - normVal * 4.4f;
                pushLine2D(m_verts2D, dx, gy,
                           dx + std::cos(needleAng) * drx * 0.84f,
                           gy + std::sin(needleAng) * dry * 0.84f,
                           0.0065f, invA, 1.0f, 0.28f, 0.20f);
            }

            if (m_snap.carIndex == 1) {
                // Cozy '94 Wagon Bonus: Travel Coffee Cup on the right center console!
                float cupX = 0.34f + px;
                float cupY = -0.52f + py;
                Color3 mug = lit(0.88f, 0.84f, 0.76f);
                pushQuad2D(m_verts2D, cupX - 0.045f * invA, cupY - 0.09f, cupX + 0.045f * invA, cupY - 0.09f,
                           cupX + 0.055f * invA, cupY + 0.05f, cupX - 0.055f * invA, cupY + 0.05f, mug.r, mug.g, mug.b);
                pushQuad2D(m_verts2D, cupX - 0.060f * invA, cupY + 0.05f, cupX + 0.060f * invA, cupY + 0.05f,
                           cupX + 0.056f * invA, cupY + 0.075f, cupX - 0.056f * invA, cupY + 0.075f, 0.18f, 0.18f, 0.20f);
            }
        } else {
            // Apex GT-RS Digital Race Cluster + RPM Bar Graph + Shift LEDs!
            float cw = 0.30f * invA * 1.45f;
            pushQuad2D(m_verts2D, gx - cw, gy - 0.09f, gx + cw, gy - 0.09f, gx + cw, gy + 0.09f, gx - cw, gy + 0.09f, 0.05f, 0.08f, 0.14f);
            int litBars = static_cast<int>(m_snap.rpmNorm * 12.0f);
            for (int b = 0; b < 12; ++b) {
                float bx = gx - cw * 0.86f + b * (cw * 1.72f / 12.0f);
                bool on = (b <= litBars);
                Color3 bc = (b < 7) ? Color3{0.15f, 0.92f, 1.0f} : (b < 10 ? Color3{1.0f, 0.75f, 0.15f} : Color3{1.0f, 0.18f, 0.38f});
                float dim = on ? 1.0f : 0.16f;
                pushQuad2D(m_verts2D, bx, gy - 0.04f, bx + cw * 0.10f, gy - 0.04f,
                           bx + cw * 0.10f, gy + 0.055f, bx, gy + 0.055f, bc.r * dim, bc.g * dim, bc.b * dim);
            }
        }

        // Center Console Glowing Infotainment / Radio Deck!
        float radX = 0.16f + px;
        float radY = -0.64f + py;
        pushQuad2D(m_verts2D, radX - 0.13f * invA * 1.4f, radY - 0.055f, radX + 0.13f * invA * 1.4f, radY - 0.055f,
                   radX + 0.13f * invA * 1.4f, radY + 0.055f, radX - 0.13f * invA * 1.4f, radY + 0.055f,
                   0.06f, 0.09f, 0.13f);
        pushQuad2D(m_verts2D, radX - 0.11f * invA * 1.4f, radY - 0.015f, radX + 0.11f * invA * 1.4f, radY - 0.015f,
                   radX + 0.11f * invA * 1.4f, radY + 0.025f, radX - 0.11f * invA * 1.4f, radY + 0.025f,
                   0.18f, 0.85f, 0.95f, 0.85f);

        // 6) TACTILE ROTATING STEERING WHEEL!
        const float wx = -0.30f + px * 1.18f;
        const float wy = -0.82f + py * 1.10f;
        const float wrOutY = 0.44f;
        const float wrInY  = 0.365f;
        const float wrOutX = wrOutY * invA;
        const float wrInX  = wrInY * invA;
        const float rotRad = -m_snap.steerWheelAngleDeg * (kPi / 180.0f);

        Color3 rimCol = (m_snap.carIndex == 0) ? lit(0.54f, 0.25f, 0.10f) : // Classic Mahogany Wood Rim!
                        (m_snap.carIndex == 1) ? lit(0.18f, 0.19f, 0.22f) : // Comfy Leather Rim!
                                                 lit(0.12f, 0.13f, 0.16f);  // Sport Alcantara Rim!
        Color3 spokeCol = (m_snap.carIndex == 0) ? lit(0.84f, 0.86f, 0.90f) : // Brushed Chrome Spokes!
                          (m_snap.carIndex == 1) ? lit(0.22f, 0.23f, 0.26f) :
                                                   lit(0.28f, 0.30f, 0.35f);

        // Spokes rotating with wheel!
        const float spokeAngles[3] = {
            rotRad + kPi,                  // Left spoke (9 o'clock)
            rotRad + 0.0f,                 // Right spoke (3 o'clock)
            rotRad - kPi * 0.5f            // Bottom spoke (6 o'clock)
        };
        float spokeThick = (m_snap.carIndex == 0) ? 0.018f : 0.032f;
        for (int s = 0; s < 3; ++s) {
            float a = spokeAngles[s];
            pushLine2D(m_verts2D, wx, wy,
                       wx + std::cos(a) * wrInX * 1.02f,
                       wy + std::sin(a) * wrInY * 1.02f,
                       spokeThick, invA, spokeCol.r, spokeCol.g, spokeCol.b);
        }

        // Outer Steering Wheel Rim
        pushRing2D(m_verts2D, wx, wy, wrInX, wrInY, wrOutX, wrOutY, 36, rimCol.r, rimCol.g, rimCol.b);

        if (m_snap.carIndex == 2) {
            // Apex GT-RS Red 12-o'clock Top Center Stripe rotating with the wheel!
            float topA = rotRad + kPi * 0.5f;
            float a0 = topA - 0.07f, a1 = topA + 0.07f;
            pushQuad2D(m_verts2D,
                       wx + std::cos(a0) * wrInX,  wy + std::sin(a0) * wrInY,
                       wx + std::cos(a0) * wrOutX, wy + std::sin(a0) * wrOutY,
                       wx + std::cos(a1) * wrOutX, wy + std::sin(a1) * wrOutY,
                       wx + std::cos(a1) * wrInX,  wy + std::sin(a1) * wrInY,
                       0.95f, 0.14f, 0.20f);
        }

        // Center Horn Boss + Emblem
        pushDisc2D(m_verts2D, wx, wy, 0.115f * invA, 0.115f, 20, spokeCol.r * 0.85f, spokeCol.g * 0.85f, spokeCol.b * 0.85f);
        Color3 badge = (m_snap.carIndex == 0) ? Color3{0.85f, 0.68f, 0.22f} :
                       (m_snap.carIndex == 1) ? Color3{0.25f, 0.65f, 0.48f} :
                                                Color3{0.18f, 0.88f, 1.0f};
        pushDisc2D(m_verts2D, wx, wy, 0.038f * invA, 0.038f, 14, badge.r, badge.g, badge.b);
    }

    QQuickWindow* m_window = nullptr;
    QSize m_fbSize{800, 480};
    DriveSnapshot m_snap;
    std::vector<Vert3D> m_verts3D;
    std::vector<Vert2D> m_verts2D;

    GLuint m_skyProg = 0;
    GLint m_aSkyPos = -1, m_uSkyInvVP = -1, m_uSkyZenith = -1, m_uSkyHorizon = -1;
    GLint m_uSkySunDir = -1, m_uSkySunCol = -1, m_uSkyNight = -1, m_uSkyTime = -1;

    GLuint m_worldProg = 0;
    GLint m_aPos = -1, m_aNorm = -1, m_aCol = -1, m_aEmis = -1;
    GLint m_uMvp = -1, m_uSunDir = -1, m_uSunCol = -1, m_uZenith = -1, m_uHorizon = -1;
    GLint m_uNight = -1, m_uFog = -1, m_uHeadOn = -1, m_uBeamOrig = -1, m_uBeamDir = -1, m_uRain = -1;

    GLuint m_uiProg = 0;
    GLint m_aUiPos = -1, m_aUiCol = -1;
};

} // namespace

RoadView::RoadView(QQuickItem* parent)
    : QQuickFramebufferObject(parent),
      m_audio(this),
      m_sim(&m_audio) {
    setMirrorVertically(true);
    m_clock.start();
    m_lastNs = m_clock.nsecsElapsed();

    connect(&m_timer, &QTimer::timeout, this, &RoadView::onTick);
    m_timer.start(16);
}

QQuickFramebufferObject::Renderer* RoadView::createRenderer() const {
    return new HorizonRenderer();
}

void RoadView::setTiltGravity(float gx, float gy, float /*gz*/, bool /*rotated90*/) {
    // Smooth low-pass filter on 2D screen-plane gravity vector (gx, gy)
    if (!m_hasTiltSample) {
        m_fgx = gx;
        m_fgy = gy;
        // Snap initial "12 o'clock" reference to nearest cardinal screen axis
        if (std::abs(gx) > std::abs(gy)) {
            m_zeroUx = (gx >= 0.0f) ? 1.0f : -1.0f;
            m_zeroUy = 0.0f;
        } else {
            m_zeroUx = 0.0f;
            m_zeroUy = (gy >= 0.0f) ? 1.0f : -1.0f;
        }
        m_hasTiltSample = true;
    } else {
        m_fgx += 0.24f * (gx - m_fgx);
        m_fgy += 0.24f * (gy - m_fgy);
    }

    const float planeMag = std::hypot(m_fgx, m_fgy);
    if (planeMag < 1.2f) {
        // Phone is lying almost completely flat on a table
        m_tiltSteerVal = 0.0f;
        return;
    }

    const float ux = m_fgx / planeMag;
    const float uy = m_fgy / planeMag;

    // If user physically rotated the phone by ~90/180 deg to switch portrait <-> landscape,
    // automatically re-anchor neutral "up" to the new cardinal axis!
    const float cosDelta = m_zeroUx * ux + m_zeroUy * uy;
    if (cosDelta < 0.25f) {
        if (std::abs(ux) > std::abs(uy)) {
            m_zeroUx = (ux >= 0.0f) ? 1.0f : -1.0f;
            m_zeroUy = 0.0f;
        } else {
            m_zeroUx = 0.0f;
            m_zeroUy = (uy >= 0.0f) ? 1.0f : -1.0f;
        }
    }

    // 2D cross product gives exact sin(wheelAngle) across ALL phone orientations & couch recline angles!
    const float sinAngle = (m_zeroUx * uy - m_zeroUy * ux) * (m_invertTilt ? -1.0f : 1.0f);
    // Attenuate smoothly only if phone is nearly flat (< 2.5 m/s^2 in screen plane)
    const float flatFade = std::clamp((planeMag - 1.2f) / 1.8f, 0.0f, 1.0f);

    constexpr float kDeadzone = 0.026f; // ~1.5 deg calm center
    constexpr float kFullLockSin = 0.56f; // ~34 deg comfortable wheel full-lock
    const float absS = std::abs(sinAngle);
    if (absS <= kDeadzone) {
        m_tiltSteerVal = 0.0f;
        return;
    }
    const float sign = (sinAngle >= 0.0f) ? 1.0f : -1.0f;
    const float norm = std::clamp((absS - kDeadzone) / (kFullLockSin - kDeadzone), 0.0f, 1.0f);
    const float curved = std::pow(norm, 1.25f) * 1.05f * m_steerSens * flatFade;
    m_tiltSteerVal = std::clamp(sign * curved, -1.0f, 1.0f);
}

void RoadView::calibrateTiltZero() {
    const float len = std::hypot(m_fgx, m_fgy);
    if (m_hasTiltSample && len > 1.2f) {
        m_zeroUx = m_fgx / len;
        m_zeroUy = m_fgy / len;
    }
    m_tiltSteerVal = 0.0f;
    m_audio.triggerChime(0);
}

void RoadView::setTouchSteer(float val) {
    m_touchSteerVal = std::clamp(val, -1.0f, 1.0f);
}

void RoadView::setGasHeld(bool held) { m_gasHeld = held; }
void RoadView::setBrakeHeld(bool held) { m_brakeHeld = held; }

void RoadView::addViewDrag(float dxPx, float dyPx) {
    m_sim.addFreeLook(dxPx * 0.0038f, -dyPx * 0.0030f);
}

void RoadView::cycleCamera() {
    m_sim.cycleCamera();
    m_sim.recenterFreeLook();
    m_audio.triggerChime(1);
    emit stateChanged();
}

void RoadView::cycleRadio() {
    setRadioStation((m_radioStation + 1) % 4);
}

void RoadView::cycleCar() {
    setCarIndex((m_sim.carIndex() + 1) % 3);
}

void RoadView::syncToSnapshot(DriveSnapshot& dst) const {
    m_sim.buildSnapshot(dst);
}

void RoadView::setPaused(bool p) {
    if (m_paused == p) return;
    m_paused = p;
    m_audio.setPaused(p);
    if (!p) m_lastNs = m_clock.nsecsElapsed();
    emit pausedChanged();
}

void RoadView::setAutoDrive(bool on) {
    m_sim.setAutoDrive(on);
    m_audio.triggerChime(0);
    emit stateChanged();
}

void RoadView::setTiltSteerEnabled(bool on) {
    m_tiltEnabled = on;
    emit stateChanged();
}

void RoadView::setInvertTilt(bool inv) {
    m_invertTilt = inv;
    emit stateChanged();
}

void RoadView::setSteerSensitivity(float s) {
    m_steerSens = std::clamp(s, 0.25f, 2.2f);
    emit stateChanged();
}

void RoadView::setCruiseSpeedKmh(float kmh) {
    m_sim.setCruiseSpeedKmh(std::clamp(kmh, 45.0f, 210.0f));
    emit stateChanged();
}

void RoadView::setCarIndex(int idx) {
    m_sim.setCarIndex(idx);
    m_audio.triggerChime(0);
    emit stateChanged();
}

void RoadView::setCameraMode(int m) {
    m_sim.setCameraMode(m);
    emit stateChanged();
}

void RoadView::setLockedBiome(int b) {
    m_sim.setLockedBiome(b);
    emit stateChanged();
    emit hudChanged();
}

void RoadView::setWeatherMode(int w) {
    m_sim.setWeatherMode(w);
    emit stateChanged();
}

void RoadView::setTimeOfDay(float h) {
    m_sim.setTimeOfDay(h);
    emit hudChanged();
}

void RoadView::setAutoTimeCycle(bool on) {
    m_sim.setAutoTimeCycle(on);
    emit stateChanged();
}

void RoadView::setHeadlights(bool on) {
    m_sim.setHeadlights(on);
    emit stateChanged();
}

void RoadView::setWipers(bool on) {
    m_sim.setWipers(on);
    emit stateChanged();
}

void RoadView::setRadioStation(int st) {
    m_radioStation = std::clamp(st, 0, 3);
    m_audio.setRadioStation(m_radioStation);
    m_audio.triggerChime(1);
    emit stateChanged();
}

QString RoadView::radioStationName() const {
    switch (m_radioStation) {
        case 0: return QStringLiteral("Radio OFF");
        case 1: return QStringLiteral("92.5 Sunset Chill");
        case 2: return QStringLiteral("101.3 Lo-Fi Keys");
        case 3: return QStringLiteral("107.9 Horizon Pad");
    }
    return QStringLiteral("FM");
}

void RoadView::setEngineSound(bool on) {
    m_engineSound = on;
    m_audio.setEngineEnabled(on);
    emit stateChanged();
}

QString RoadView::distanceText() const {
    return QStringLiteral("%1 km").arg(m_sim.distanceKm(), 0, 'f', 1);
}

QString RoadView::clockText() const {
    int totalMin = static_cast<int>(m_sim.timeOfDay() * 60.0f) % (24 * 60);
    int hh = totalMin / 60;
    int mm = totalMin % 60;
    return QStringLiteral("%1:%2")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'));
}

void RoadView::onTick() {
    const qint64 nowNs = m_clock.nsecsElapsed();
    const float dt = std::clamp(static_cast<float>(nowNs - m_lastNs) * 1e-9f, 0.001f, 0.05f);
    m_lastNs = nowNs;

    if (!m_paused) {
        float steer = (std::abs(m_touchSteerVal) > 0.04f)
                          ? m_touchSteerVal
                          : (m_tiltEnabled ? m_tiltSteerVal : 0.0f);
        float thr = m_gasHeld ? 1.0f : 0.0f;
        float brk = m_brakeHeld ? 1.0f : 0.0f;
        m_sim.update(dt, steer, thr, brk);
        emit hudChanged();
        update();
    }
}
