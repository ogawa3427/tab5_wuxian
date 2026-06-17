#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <cmath>
#include "packet.h"

namespace {

constexpr const char* kApSsid        = "Tab5-IMU";
constexpr const char* kApPass        = "12345678";
constexpr uint32_t   kSendIntervalMs = 100;
constexpr uint32_t   kWifiTimeoutMs  = 15000;
constexpr uint32_t   kDrawIntervalMs = 200;  // display refresh ~5fps

// ── state ─────────────────────────────────────────────────────────────────
WiFiUDP   g_udp;
IPAddress g_dest_ip;
bool      g_wifi_ready    = false;
bool      g_imu_available = false;
uint8_t   g_seq           = 0;
uint32_t  g_tx_count      = 0;
uint32_t  g_tx_fail_count = 0;
ImuPacket g_last_packet{};
uint32_t  g_last_draw_ms  = 0;
uint32_t  g_last_send_ms  = 0;

// ── LED: ノンブロッキング点滅 ─────────────────────────────────────────────
uint32_t g_led_restore_ms = 0;   // 0 = no pending restore
uint8_t  g_idle_r = 0, g_idle_g = 0, g_idle_b = 32;  // blue = standby

void setLed(uint8_t r, uint8_t g, uint8_t b) {
    if (!M5.Led.isEnabled()) return;
    M5.Led.setAllColor(r, g, b);
    M5.Led.display();
}

void blinkLed(uint8_t r, uint8_t g, uint8_t b, uint32_t dur_ms = 40) {
    setLed(r, g, b);
    g_led_restore_ms = millis() + dur_ms;
}

void updateLed() {
    if (g_led_restore_ms && millis() >= g_led_restore_ms) {
        g_led_restore_ms = 0;
        setLed(g_idle_r, g_idle_g, g_idle_b);
    }
}

// ── Canvas (sprite) ──────────────────────────────────────────────────────
M5Canvas g_canvas(&M5.Display);

// ── WiFi ─────────────────────────────────────────────────────────────────
bool connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(kApSsid, kApPass);
    Serial.printf("Connecting to %s", kApSsid);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > kWifiTimeoutMs) {
            Serial.println("\nWiFi timeout");
            return false;
        }
        delay(300);
        Serial.print(".");
    }
    g_dest_ip = WiFi.gatewayIP();
    Serial.printf("\nConnected. IP=%s  dest=%s:%u\n",
                  WiFi.localIP().toString().c_str(),
                  g_dest_ip.toString().c_str(), kUdpPort);
    return true;
}

// ── IMU ──────────────────────────────────────────────────────────────────
void fillSyntheticImu(ImuPacket& p) {
    const float t = millis() * 0.001f;
    p.accel_x = sinf(t * 1.7f) * 0.4f;
    p.accel_y = cosf(t * 1.3f) * 0.4f;
    p.accel_z = 1.0f + sinf(t * 0.9f) * 0.1f;
    p.gyro_x  = sinf(t * 2.1f) * 30.0f;
    p.gyro_y  = cosf(t * 1.9f) * 30.0f;
    p.gyro_z  = sinf(t * 1.5f) * 15.0f;
}

bool fillPacket(ImuPacket& p) {
    p.magic     = kPacketMagic;
    p.seq       = g_seq++;
    p.uptime_ms = millis();
    if (g_imu_available && M5.Imu.update()) {
        auto d  = M5.Imu.getImuData();
        p.accel_x = d.accel.x; p.accel_y = d.accel.y; p.accel_z = d.accel.z;
        p.gyro_x  = d.gyro.x;  p.gyro_y  = d.gyro.y;  p.gyro_z  = d.gyro.z;
        return true;
    }
    fillSyntheticImu(p);
    return false;
}

// ── Display ───────────────────────────────────────────────────────────────
// AtomS3: 128x128, text size 1 = 6x8px → 21 chars wide, 16 rows tall
void drawDisplay() {
    const bool wifi_ok = g_wifi_ready && (WiFi.status() == WL_CONNECTED);

    g_canvas.fillScreen(TFT_BLACK);
    g_canvas.setTextSize(1);
    g_canvas.setCursor(0, 0);

    // ── Row 0: WiFi ──────────────────────────────────────────────────────
    g_canvas.setTextColor(wifi_ok ? TFT_GREEN : TFT_RED);
    g_canvas.printf("WiFi %-3s  ", wifi_ok ? "OK" : "NG");
    g_canvas.setTextColor(g_imu_available ? TFT_CYAN : TFT_DARKGREY);
    g_canvas.println(g_imu_available ? "IMU real" : "IMU fake");

    // ── Row 1-3: Accel ───────────────────────────────────────────────────
    g_canvas.setTextColor(TFT_WHITE);
    g_canvas.printf("Ax %+7.3f\n", g_last_packet.accel_x);
    g_canvas.printf("Ay %+7.3f\n", g_last_packet.accel_y);
    g_canvas.printf("Az %+7.3f\n", g_last_packet.accel_z);

    // ── Row 4-6: Gyro ────────────────────────────────────────────────────
    g_canvas.setTextColor(TFT_YELLOW);
    g_canvas.printf("Gx %+7.2f\n", g_last_packet.gyro_x);
    g_canvas.printf("Gy %+7.2f\n", g_last_packet.gyro_y);
    g_canvas.printf("Gz %+7.2f\n", g_last_packet.gyro_z);

    // ── Divider ──────────────────────────────────────────────────────────
    g_canvas.drawFastHLine(0, 104, 128, TFT_DARKGREY);

    // ── Row 7: TX stats ──────────────────────────────────────────────────
    g_canvas.setCursor(0, 108);
    g_canvas.setTextColor(TFT_DARKGREY);
    g_canvas.printf("tx:%-5lu  fail:%-3lu", g_tx_count, g_tx_fail_count);

    // ── Row 8: seq ───────────────────────────────────────────────────────
    g_canvas.setCursor(0, 120);
    if (wifi_ok) {
        g_canvas.setTextColor(TFT_GREEN);
        g_canvas.printf("seq:%-3u  ", g_last_packet.seq);
    } else {
        g_canvas.setTextColor(TFT_RED);
        g_canvas.print("no link  ");
    }
    g_canvas.setTextColor(TFT_DARKGREY);
    g_canvas.printf("%lus", millis() / 1000);

    g_canvas.pushSprite(0, 0);
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(300);

    g_canvas.createSprite(128, 128);

    g_imu_available = M5.Imu.isEnabled();
    Serial.println(g_imu_available ? "IMU ready." : "IMU not found. Using synthetic values.");

    // Connecting 表示
    g_idle_r = 0; g_idle_g = 8; g_idle_b = 32;
    setLed(g_idle_r, g_idle_g, g_idle_b);
    g_canvas.fillScreen(TFT_BLACK);
    g_canvas.setTextColor(TFT_YELLOW);
    g_canvas.setTextSize(1);
    g_canvas.setCursor(4, 56);
    g_canvas.println("Connecting...");
    g_canvas.pushSprite(0, 0);

    g_wifi_ready = connectWifi();

    if (g_wifi_ready) {
        g_udp.begin(kUdpPort);
        g_idle_r = 0; g_idle_g = 0; g_idle_b = 32;
        setLed(g_idle_r, g_idle_g, g_idle_b);
        Serial.println("AtomS3 UDP transmitter ready");
    } else {
        g_idle_r = 32; g_idle_g = 0; g_idle_b = 0;
        setLed(g_idle_r, g_idle_g, g_idle_b);
    }

    g_last_send_ms = millis();
    g_last_draw_ms = millis();
}

void loop() {
    M5.update();
    updateLed();

    if (!g_wifi_ready) {
        delay(1000);
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost. Reconnecting...");
        g_idle_r = 32; g_idle_g = 0; g_idle_b = 0;
        setLed(g_idle_r, g_idle_g, g_idle_b);
        g_udp.stop();  // 古いソケットを閉じる
        g_wifi_ready = connectWifi();
        if (!g_wifi_ready) { delay(1000); return; }
        g_udp.begin(kUdpPort);  // 再接続後に再bind
        g_idle_r = 0; g_idle_g = 0; g_idle_b = 32;
        setLed(g_idle_r, g_idle_g, g_idle_b);
    }

    // ── IMU read & send at kSendIntervalMs ────────────────────────────────
    if (millis() - g_last_send_ms >= kSendIntervalMs) {
        g_last_send_ms = millis();

        ImuPacket p{};
        fillPacket(p);
        g_last_packet = p;

        if (g_udp.beginPacket(g_dest_ip, kUdpPort)) {
            g_udp.write(reinterpret_cast<const uint8_t*>(&p), sizeof(p));
            if (g_udp.endPacket()) {
                g_tx_count++;
                blinkLed(0, 32, 0);  // green flash
            } else {
                g_tx_fail_count++;
                blinkLed(32, 0, 0);
            }
        } else {
            g_tx_fail_count++;
        }

        // Serial.printf("tx seq=%u ax=%.3f ay=%.3f az=%.3f gx=%.1f gy=%.1f gz=%.1f\n",
        //               p.seq, p.accel_x, p.accel_y, p.accel_z,
        //               p.gyro_x, p.gyro_y, p.gyro_z);
    }

    // ── Display refresh at kDrawIntervalMs ────────────────────────────────
    if (millis() - g_last_draw_ms >= kDrawIntervalMs) {
        g_last_draw_ms = millis();
        drawDisplay();
    }

    // WiFiスタックとFreeRTOSタスクに処理時間を渡す
    // ビジーループのままだとWiFiスタックが詰まってクラッシュの原因になる
    delay(20);
}
