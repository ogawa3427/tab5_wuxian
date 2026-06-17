#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <cmath>
#include <algorithm>
#include "packet.h"

namespace {

// ── WiFi / UDP ────────────────────────────────────────────────────────────
constexpr int kSdioClk=12, kSdioCmd=13;
constexpr int kSdioD0=11, kSdioD1=10, kSdioD2=9, kSdioD3=8, kSdioRst=15;
constexpr const char* kApSsid = "Tab5-IMU";
constexpr const char* kApPass = "12345678";

WiFiUDP   g_udp;
ImuPacket g_latest{};
bool      g_has_data   = false;
IPAddress g_sender_ip;
uint32_t  g_rx_count   = 0;
uint32_t  g_last_rx_ms = 0;

// ── Canvas (double buffer for flicker-free rendering) ─────────────────────
M5Canvas g_canvas(&M5.Display);

// ── 3D cube geometry ─────────────────────────────────────────────────────
struct Vec3 { float x, y, z; };
struct Pt   { int   x, y;    };
struct Mat3 { float m[3][3]; };

// 重力方向(モデル座標系)。これを画面下(0,+1,0)へ向ける回転で姿勢を表現する。
// AtomS3 IMU軸: X=右 / Y=上 / Z=画面手前、平置きで az≈+1g
//   → 画面はY軸下向きなので device(x,y,z) → model(x,-y,-z) で対応付ける。
Vec3     g_grav = {0, 1, 0};  // filtered gravity in model frame
uint32_t g_last_angle_ms = 0;

// 8 vertices of a unit cube
constexpr Vec3 kVerts[8] = {
    {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},  // back face  0-3
    {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1},  // front face 4-7
};

struct FaceDef { int v[4]; uint32_t color; };

// Vertex indices in CCW winding order (viewed from outside)
// 顔色: 红 绿 黄 蓝 青 品
// ※ 色は必ず RGB888 (0xRRGGBB) で指定する。M5GFX(LovyanGFX)の TFT_* マクロは
//    RGB565値だが int 型で、描画関数が uint32_t を RGB888 解釈するため
//    緑→青・赤→緑のように化ける。888で書けば正しく出る。
constexpr FaceDef kFaces[6] = {
    {{4,5,6,7}, 0xFF0000},  // +Z  front   红
    {{1,0,3,2}, 0x00FF00},  // -Z  back    绿
    {{1,2,6,5}, 0xFFFF00},  // +X  right   黄
    {{0,4,7,3}, 0x0000FF},  // -X  left    蓝
    {{3,7,6,2}, 0x00FFFF},  // +Y  top     青(cyan)
    {{0,1,5,4}, 0xFF00FF},  // -Y  bottom  品(magenta)
};

// Slightly darkened versions for edge outlines
constexpr uint32_t kEdgeColors[6] = {
    0x800000, 0x008000, 0x808000, 0x000080, 0x008080, 0x800080,
};

// ── 3D math ──────────────────────────────────────────────────────────────
Vec3 mul(const Mat3& R, const Vec3& p) {
    return { R.m[0][0]*p.x + R.m[0][1]*p.y + R.m[0][2]*p.z,
             R.m[1][0]*p.x + R.m[1][1]*p.y + R.m[1][2]*p.z,
             R.m[2][0]*p.x + R.m[2][1]*p.y + R.m[2][2]*p.z };
}

// 単位ベクトル a を b へ最短回転で重ねる行列 (Rodrigues)。
// これで「重力方向(a)を画面下(b)へ向ける」回転が一意に決まる(ヨーは出ない)。
Mat3 rotAtoB(const Vec3& a, const Vec3& b) {
    const Vec3 v = { a.y*b.z - a.z*b.y,
                     a.z*b.x - a.x*b.z,
                     a.x*b.y - a.y*b.x };           // a × b
    const float c = a.x*b.x + a.y*b.y + a.z*b.z;    // a · b

    if (c > 0.999999f) {                            // ほぼ同方向 → 恒等
        return {{ {1,0,0},{0,1,0},{0,0,1} }};
    }
    if (c < -0.999999f) {                           // ほぼ逆方向 → 180°回転
        return {{ {-1,0,0},{0,-1,0},{0,0,1} }};
    }
    const float k = 1.0f / (1.0f + c);
    return {{
        { 1 + k*(-(v.z*v.z + v.y*v.y)),     -v.z + k*(v.x*v.y),        v.y + k*(v.x*v.z) },
        {  v.z + k*(v.x*v.y),            1 + k*(-(v.z*v.z + v.x*v.x)), -v.x + k*(v.y*v.z) },
        { -v.y + k*(v.x*v.z),               v.x + k*(v.y*v.z),     1 + k*(-(v.y*v.y + v.x*v.x)) },
    }};
}

// Perspective projection
// fov: field-of-view scale, cd: camera distance (z offset)
Pt project(const Vec3& v, int cx, int cy, float fov, float cd) {
    float d = v.z + cd;
    float s = fov / d;
    return {cx + (int)(v.x * s), cy + (int)(v.y * s)};
}

// ── WiFi init ─────────────────────────────────────────────────────────────
void initWifi() {
    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPass);
    g_udp.begin(kUdpPort);
    Serial.printf("SoftAP: %s  IP: %s  UDP:%u\n",
                  kApSsid, WiFi.softAPIP().toString().c_str(), kUdpPort);
}

// ── UDP poll ──────────────────────────────────────────────────────────────
void pollUdp() {
    int sz = g_udp.parsePacket();
    if (sz != (int)sizeof(ImuPacket)) return;

    ImuPacket p;
    g_udp.read(reinterpret_cast<uint8_t*>(&p), sizeof(p));
    if (p.magic != kPacketMagic) return;

    g_latest     = p;
    g_sender_ip  = g_udp.remoteIP();
    g_rx_count++;
    g_last_rx_ms = millis();
    g_has_data   = true;
}

// ── Gravity update from accelerometer ─────────────────────────────────────
// accel の重力ベクトルをモデル座標系へ写し、低域通過フィルタで平滑化する。
// device(x,y,z) → model(x,-y,-z) : 画面Y軸が下向きなので Y/Z を反転。
// これで実機を傾けた向きと画面の立方体の向きが一致する。
// yaw は accel では不定 → このベクトル法では原理的に発生しない。
// データなし時は重力をゆっくり回して「生きている」表示にする。
void updateAnglesFromAccel() {
    g_last_angle_ms = millis();

    Vec3 target;
    if (g_has_data) {
        target = { g_latest.accel_x, -g_latest.accel_y, -g_latest.accel_z };
    } else {
        const float t = millis() * 0.0006f;
        target = { 0.5f * sinf(t), cosf(t * 0.7f), 0.5f * cosf(t) };
    }

    const float n = sqrtf(target.x*target.x + target.y*target.y + target.z*target.z);
    if (n < 1e-4f) return;                 // 自由落下/無重力相当 → 直前姿勢を保持
    target.x /= n; target.y /= n; target.z /= n;

    constexpr float kAlpha = 0.15f;        // 素早め追従
    g_grav.x += (target.x - g_grav.x) * kAlpha;
    g_grav.y += (target.y - g_grav.y) * kAlpha;
    g_grav.z += (target.z - g_grav.z) * kAlpha;

    const float gn = sqrtf(g_grav.x*g_grav.x + g_grav.y*g_grav.y + g_grav.z*g_grav.z);
    if (gn > 1e-4f) { g_grav.x /= gn; g_grav.y /= gn; g_grav.z /= gn; }
}

// ── Draw 3D cube ──────────────────────────────────────────────────────────
void drawCube(int cx, int cy) {
    constexpr float kFov = 1000.0f;  // perspective scale
    constexpr float kCd  = 8.0f;     // camera distance (z offset)
    // ※ cd を立方体の半径(√3≈1.73)に対して十分大きく取り、遠近の歪みを抑える。

    // 重力ベクトルを画面下(0,+1,0)へ向ける回転で姿勢行列を作る
    const Mat3 R = rotAtoB(g_grav, Vec3{0.0f, 1.0f, 0.0f});

    // Transform all 8 vertices
    Vec3 tv[8];
    for (int i = 0; i < 8; i++) {
        tv[i] = mul(R, kVerts[i]);
    }

    // Compute avg Z for each face (painter's algorithm sort key)
    float avg_z[6];
    for (int f = 0; f < 6; f++) {
        avg_z[f] = (tv[kFaces[f].v[0]].z + tv[kFaces[f].v[1]].z
                  + tv[kFaces[f].v[2]].z + tv[kFaces[f].v[3]].z) * 0.25f;
    }

    // Sort face indices back-to-front.
    // カメラは z=-kCd にあり +z が遠方。遠い面(z大)から先に描く。
    int order[6] = {0,1,2,3,4,5};
    std::sort(order, order+6, [&](int a, int b){ return avg_z[a] > avg_z[b]; });

    for (int fi = 0; fi < 6; fi++) {
        const int f = order[fi];

        // Project 4 vertices to screen
        Pt p[4];
        for (int k = 0; k < 4; k++) {
            p[k] = project(tv[kFaces[f].v[k]], cx, cy, kFov, kCd);
        }

        // Back-face culling は「透視投影後の画面座標」の符号付き面積で判定する。
        // 正射影の法線(tv)で判定すると遠近とズレ、シルエット際で
        // 表面が欠けて穴(透明)になったり裏面が漏れて見えたりする。
        // 画面Y軸は下向きなので、表向き(CW)の面は符号付き面積が負になる。
        long area2 = 0;
        for (int k = 0; k < 4; k++) {
            const int k2 = (k + 1) & 3;
            area2 += (long)p[k].x * p[k2].y - (long)p[k2].x * p[k].y;
        }
        if (area2 >= 0) continue;  // back face → skip

        // Fill quad as 2 triangles
        const uint32_t col = kFaces[f].color;
        g_canvas.fillTriangle(p[0].x,p[0].y, p[1].x,p[1].y, p[2].x,p[2].y, col);
        g_canvas.fillTriangle(p[0].x,p[0].y, p[2].x,p[2].y, p[3].x,p[3].y, col);

        // Quad outline (dark edge)
        const uint32_t ec = kEdgeColors[f];
        for (int k = 0; k < 4; k++) {
            const int k2 = (k + 1) & 3;
            g_canvas.drawLine(p[k].x,p[k].y, p[k2].x,p[k2].y, ec);
        }
    }
}

// ── Draw status bar ───────────────────────────────────────────────────────
void drawStatus() {
    const int W = g_canvas.width();
    const bool linked = g_has_data && (millis() - g_last_rx_ms < 2000);

    // Line 1: AP info
    g_canvas.setTextSize(2);
    g_canvas.setTextColor(0x00FFFF, TFT_BLACK);  // cyan (RGB888)
    g_canvas.setCursor(4, 4);
    g_canvas.printf("AP: %s   IP: %s", kApSsid, WiFi.softAPIP().toString().c_str());

    // Line 2: link status
    g_canvas.setCursor(4, 28);
    if (linked) {
        g_canvas.setTextColor(0x00FF00, TFT_BLACK);  // green (RGB888)
        g_canvas.setTextSize(3);
        g_canvas.printf("RX: %-5lu  from: %s  seq: %-3u",
                        g_rx_count,
                        g_sender_ip.toString().c_str(),
                        g_latest.seq);
    } else if (g_has_data) {
        g_canvas.setTextColor(0xFF0000, TFT_BLACK);  // red (RGB888)
        g_canvas.printf("LINK LOST  last: %lums ago", millis() - g_last_rx_ms);
    } else {
        g_canvas.setTextColor(0xFFFF00, TFT_BLACK);  // yellow (RGB888)
        g_canvas.print("Waiting for AtomS3...");
    }

    // Line 3: raw IMU values (small text)
    if (g_has_data) {
        g_canvas.setTextSize(1);
        g_canvas.setTextColor(0x808080, TFT_BLACK);  // grey (RGB888)
        g_canvas.setCursor(4, 56);
        g_canvas.printf("ax:%+.2f  ay:%+.2f  az:%+.2f     "
                        "gx:%+.1f  gy:%+.1f  gz:%+.1f",
                        g_latest.accel_x, g_latest.accel_y, g_latest.accel_z,
                        g_latest.gyro_x,  g_latest.gyro_y,  g_latest.gyro_z);
    }

    // Divider
    g_canvas.drawFastHLine(0, 72, W, 0x303030 /*dark grey (RGB888)*/);
}

// ── Main draw ─────────────────────────────────────────────────────────────
void drawFrame() {
    const int W  = g_canvas.width();
    const int H  = g_canvas.height();
    constexpr int kBarH = 74;

    g_canvas.fillScreen(TFT_BLACK);

    drawStatus();

    const int cube_cx = W / 2;
    const int cube_cy = kBarH + (H - kBarH) / 2;
    drawCube(cube_cx, cube_cy);

    g_canvas.pushSprite(0, 0);
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(300);

    // Allocate canvas (uses PSRAM on Tab5)
    const int W = M5.Display.width();
    const int H = M5.Display.height();
    if (!g_canvas.createSprite(W, H)) {
        Serial.printf("Canvas alloc failed (%dx%d). Falling back to direct draw.\n", W, H);
    } else {
        Serial.printf("Canvas: %dx%d\n", W, H);
    }

    initWifi();

    g_last_angle_ms = millis();
    Serial.println("Tab5 ready");
}

void loop() {
    M5.update();
    pollUdp();
    updateAnglesFromAccel();
    drawFrame();
}
