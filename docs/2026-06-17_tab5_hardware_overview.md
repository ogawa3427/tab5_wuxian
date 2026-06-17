# M5Stack Tab5 ハードウェア概要

日付: 2026-06-17

## メインSoC
- **ESP32-P4** (RISC-V 32bit デュアルコア 360MHz + LP シングルコア 40MHz)
- 16MB Flash、32MB PSRAM

## 無線
- **ESP32-C6-MINI-1U** モジュール（Wi-Fi 6対応）
- 内蔵3Dアンテナ ↔ MMCX外部アンテナポート 切り替え可能
- Thread・ZigBee 対応

## P4-C6間接続
- **SDIO**（SDIO2_D0〜D3、SDIO2_CMD、SDIO2_CK）
- P4がホスト（マスタ）、C6がスレーブ
- フレームワーク: **ESP-Hosted**（C6をネットワークコプロセッサとして扱う）
- アプリ側は `esp_wifi_remote` 経由で透過的にWi-Fi APIを使える
- SDIO採用理由: SPI-HDより高帯域、Wi-Fi 6のスループットに対応可能

```
ESP32-P4 (host)          ESP32-C6 (slave)
┌──────────────┐         ┌──────────────┐
│  SDIO2_CK   ├─────────┤  SDIO_CLK   │
│  SDIO2_CMD  ├─────────┤  SDIO_CMD   │
│  SDIO2_D0   ├─────────┤  SDIO_D0    │
│  SDIO2_D1   ├─────────┤  SDIO_D1    │
│  SDIO2_D2   ├─────────┤  SDIO_D2    │
│  SDIO2_D3   ├─────────┤  SDIO_D3    │
└──────────────┘         └──────────────┘
```

## ディスプレイ
- 5インチ 1280×720 IPS TFT
- MIPI-DSI 接続
- タッチ: GT911 マルチタッチコントローラ（I²C）

## カメラ
- MIPI-CSI 接続
- SC2356 2MP（1600×1200）

## 音声
- ES8388 コーデック + ES7210 AECフロントエンド
- デュアルマイクアレイ
- 3.5mm ジャック
- 1W スピーカー

## センサ
- BMI270 6軸 IMU（割り込みウェイクアップ対応）
- RX8130CE RTC

## I/O
- USB-A Host
- USB-C OTG
- RS-485（120Ω終端切替可能）
- Grove
- M5BUS
- GPIO_EXT
- microSD スロット
- STAMP パッド（Cat-M / NB-IoT / LoRaWAN）

## 電源
- 取り外し可能な NP-F550 バッテリ
- MP4560 buck-boost コンバータ
- IP2326 充電管理
- INA226 リアルタイム電力モニタリング
- バッテリなし版: C145、Kit版: K145
