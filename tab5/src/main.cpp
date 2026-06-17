#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>

#include "packet.h"

namespace {

// Tab5 SDIO2 pins (P4 -> C6 ESP-Hosted)
constexpr int kTab5SdioClk = 12;
constexpr int kTab5SdioCmd = 13;
constexpr int kTab5SdioD0  = 11;
constexpr int kTab5SdioD1  = 10;
constexpr int kTab5SdioD2  = 9;
constexpr int kTab5SdioD3  = 8;
constexpr int kTab5SdioRst = 15;

// SoftAP設定 (AtomS3がここに繋ぐ)
constexpr const char* kApSsid = "Tab5-IMU";
constexpr const char* kApPass = "12345678";

WiFiUDP udp;
ImuPacket rx_packet{};
volatile bool packet_updated = false;
IPAddress sender_ip;
uint16_t  sender_port = 0;
uint32_t  rx_count = 0;

void initTab5Wifi()
{
    WiFi.setPins(kTab5SdioClk, kTab5SdioCmd, kTab5SdioD0, kTab5SdioD1,
                 kTab5SdioD2, kTab5SdioD3, kTab5SdioRst);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPass);
    udp.begin(kUdpPort);
    Serial.printf("SoftAP: %s  IP: %s  UDP:%u\n",
                  kApSsid, WiFi.softAPIP().toString().c_str(), kUdpPort);
}

void drawHeader()
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.println("UDP IMU RX");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.printf("AP: %s\n", kApSsid);
    M5.Display.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
    M5.Display.println("waiting...");
}

void drawPacket(const ImuPacket& packet)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.println("UDP IMU RX");
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.printf("from %s\n", sender_ip.toString().c_str());
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.printf("seq %3u  cnt %lu\n\n", packet.seq, rx_count);
    M5.Display.printf("ax %7.3f\n", packet.accel_x);
    M5.Display.printf("ay %7.3f\n", packet.accel_y);
    M5.Display.printf("az %7.3f\n\n", packet.accel_z);
    M5.Display.printf("gx %7.3f\n", packet.gyro_x);
    M5.Display.printf("gy %7.3f\n", packet.gyro_y);
    M5.Display.printf("gz %7.3f\n\n", packet.gyro_z);
    M5.Display.printf("tx ms %lu\n", packet.uptime_ms);
}

}  // namespace

void setup()
{
    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(300);

    initTab5Wifi();
    drawHeader();

    Serial.println("Tab5 UDP receiver ready");
}

void loop()
{
    int pkt_size = udp.parsePacket();
    if (pkt_size == static_cast<int>(sizeof(ImuPacket))) {
        ImuPacket packet;
        udp.read(reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

        if (packet.magic == kPacketMagic) {
            sender_ip   = udp.remoteIP();
            sender_port = udp.remotePort();
            memcpy(&rx_packet, &packet, sizeof(packet));
            packet_updated = true;
            rx_count++;
        }
    }

    if (packet_updated) {
        packet_updated = false;
        ImuPacket packet;
        memcpy(&packet, &rx_packet, sizeof(packet));

        drawPacket(packet);

        Serial.printf("seq=%u ax=%.3f ay=%.3f az=%.3f gx=%.3f gy=%.3f gz=%.3f\n",
                      packet.seq,
                      packet.accel_x, packet.accel_y, packet.accel_z,
                      packet.gyro_x, packet.gyro_y, packet.gyro_z);
    }
}
