#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>

#include <cmath>

#include "packet.h"

namespace {

constexpr const char* kApSsid = "Tab5-IMU";
constexpr const char* kApPass = "12345678";
constexpr uint32_t kSendIntervalMs = 100;
constexpr uint32_t kWifiTimeoutMs = 15000;

WiFiUDP udp;
IPAddress dest_ip;
bool wifi_ready = false;
bool imu_available = false;
uint8_t seq = 0;

bool connectToTab5Ap()
{
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

    dest_ip = WiFi.gatewayIP();
    Serial.printf("\nConnected. IP=%s dest=%s:%u\n",
                  WiFi.localIP().toString().c_str(),
                  dest_ip.toString().c_str(),
                  kUdpPort);
    return true;
}

void fillSyntheticImu(ImuPacket& packet)
{
    const float t = millis() * 0.001f;
    packet.accel_x = sinf(t * 1.7f) * 0.4f;
    packet.accel_y = cosf(t * 1.3f) * 0.4f;
    packet.accel_z = 1.0f + sinf(t * 0.9f) * 0.1f;
    packet.gyro_x = sinf(t * 2.1f) * 30.0f;
    packet.gyro_y = cosf(t * 1.9f) * 30.0f;
    packet.gyro_z = sinf(t * 1.5f) * 15.0f;
}

bool fillImuPacket(ImuPacket& packet)
{
    packet.magic = kPacketMagic;
    packet.seq = seq++;
    packet.uptime_ms = millis();

    if (imu_available && M5.Imu.update()) {
        auto data = M5.Imu.getImuData();
        packet.accel_x = data.accel.x;
        packet.accel_y = data.accel.y;
        packet.accel_z = data.accel.z;
        packet.gyro_x = data.gyro.x;
        packet.gyro_y = data.gyro.y;
        packet.gyro_z = data.gyro.z;
        return true;
    }

    fillSyntheticImu(packet);
    return false;
}

void blinkTxLed()
{
    if (!M5.Led.isEnabled()) {
        return;
    }
    M5.Led.setAllColor(0, 32, 0);
    M5.Led.display();
    delay(20);
    M5.Led.setAllColor(0, 0, 0);
    M5.Led.display();
}

void setStatusLed(uint8_t r, uint8_t g, uint8_t b)
{
    if (!M5.Led.isEnabled()) {
        return;
    }
    M5.Led.setAllColor(r, g, b);
    M5.Led.display();
}

bool sendUdpPacket(const ImuPacket& packet)
{
    if (!udp.beginPacket(dest_ip, kUdpPort)) {
        return false;
    }
    const size_t written = udp.write(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
    return written == sizeof(packet) && udp.endPacket();
}

}  // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(300);

    imu_available = M5.Imu.isEnabled();
    if (!imu_available) {
        Serial.println("IMU not found. Using synthetic values.");
    } else {
        Serial.println("IMU ready.");
    }

    wifi_ready = connectToTab5Ap();
    if (!wifi_ready) {
        setStatusLed(32, 0, 0);
        return;
    }

    Serial.println("AtomS3 UDP transmitter ready");
    setStatusLed(0, 0, 32);
}

void loop()
{
    if (!wifi_ready) {
        delay(1000);
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected, reconnecting...");
        wifi_ready = connectToTab5Ap();
        if (!wifi_ready) {
            setStatusLed(32, 0, 0);
            delay(1000);
            return;
        }
    }

    ImuPacket packet{};
    const bool from_imu = fillImuPacket(packet);

    if (sendUdpPacket(packet)) {
        Serial.printf("tx seq=%u src=%s ax=%.3f ay=%.3f az=%.3f\n",
                      packet.seq,
                      from_imu ? "imu" : "fake",
                      packet.accel_x, packet.accel_y, packet.accel_z);
        blinkTxLed();
    } else {
        Serial.println("udp send failed");
    }

    delay(kSendIntervalMs);
}
