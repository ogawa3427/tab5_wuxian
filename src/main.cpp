#include <M5Unified.h>
#include <WiFi.h>

#include "sec.h"

void setup()
{
    auto cfg = M5.config();
    cfg.output_power = true;

    M5.begin(cfg);

    Serial.begin(115200);
    delay(500);

    M5.Display.fillScreen(TFT_GREEN);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    // WiFi接続開始
    // P4 -> C6 (SDIO) -> ESP-Hosted -> esp_wifi_remote 経由で透過的に動く
    Serial.printf("Connecting to %s\n", WIFI_SSID);
    M5.Display.printf("Connecting to %s\n", WIFI_SSID);

    // Tab5固有のSDIO2ピン設定（デフォルトはP4 EvalBoard用で異なる）
    // CLK=12, CMD=13, D0=11, D1=10, D2=9, D3=8, RST=15
    WiFi.setPins(12, 13, 11, 10, 9, 8, 15);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000) {
            Serial.println("WiFi timeout");
            M5.Display.println("WiFi timeout");
            return;
        }
        delay(500);
        Serial.print(".");
        M5.Display.print(".");
    }

    Serial.println("\nConnected!");
    M5.Display.println("\nConnected!");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    M5.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    M5.Display.printf("RSSI: %d dBm\n", WiFi.RSSI());
}

void loop()
{
    delay(1000);
}
