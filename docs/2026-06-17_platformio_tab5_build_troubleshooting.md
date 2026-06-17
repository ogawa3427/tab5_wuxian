# Tab5 PlatformIO ビルド環境構築 トラブルシューティング

日付: 2026-06-17

## 発生した問題と解決の流れ

### 1. click APIバージョン非互換
**症状**: `TypeError: ParamType.get_metavar() missing 1 required positional argument: 'ctx'`
**原因**: click 8.2.0 で `get_metavar()` のシグネチャが変更。PlatformIO同梱の古いesptoolが死ぬ。
**対処**: PlatformIOのpenvでclickをダウングレード
```bash
~/.platformio/penv/bin/python -m ensurepip --upgrade
~/.platformio/penv/bin/python -m pip install "click<8.2"
```

### 2. penvにpipが存在しない
**症状**: `/Users/ogawa/.platformio/penv/bin/python: No module named pip`
**原因**: PlatformIO内部のPython仮想環境にpipが入っていない
**対処**:
```bash
~/.platformio/penv/bin/python -m ensurepip --upgrade
```

### 3. `idf_tools.py installation failed` (複数回)
**症状**: ビルド開始時に6回前後出る
**原因**: pioarduino `#54.03.21` の `tl-install` パッケージに `tools.json` が欠如していた。JSONパースエラーで常時失敗。
**対処**: 
- `rm -rf ~/.platformio/packages && rm -rf ~/.platformio/platforms && rm -rf ~/.local/share/esp32 && rm -rf .pio`
- platformio.ini を `stable` zip に戻す（開発タグ `#54.03.21` は broken だった）

### 4. `xesppie` 拡張未サポートエラー
**症状**: `riscv32-esp-elf-g++: error: '-march=rv32imafc_zicsr_zifencei_xesppie': extension 'xesppie' starts with 'x' but is unsupported`
**原因**: 
- `framework-arduinoespressif32-libs @ 5.4.0` はGCC 15.2.0でビルドされており、`-march`に`xesppie`が含まれる
- pioarduino stableのToolchain (`toolchain-riscv32-esp @ 14.2.0`) は`xesppie`を知らない
- `idf_tools.py`が正しいToolchainをインストールできないせいで古いものがフォールバックとして使われていた
**対処**: 環境リセット後、pioarduino stableが正しくToolchainをセットアップすることで解決

## 使用した platformio.ini (動作確認済み)

```ini
[env:esp32p4_pioarduino]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
framework = arduino
board = esp32-p4-evboard
board_build.mcu = esp32p4
board_build.flash_mode = qio
upload_speed = 1500000
monitor_speed = 115200
build_type = debug
lib_deps =
    https://github.com/M5Stack/M5Unified.git
build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
```

## WiFi接続コード (Tab5固有のSDIOピン設定)

```cpp
#include <WiFi.h>

// Tab5固有: デフォルトはP4 EvalBoard用で異なる
// ESP32-P4 (host) <-SDIO2-> ESP32-C6 (slave, ESP-Hosted)
WiFi.setPins(12, 13, 11, 10, 9, 8, 15); // CLK, CMD, D0, D1, D2, D3, RST
WiFi.begin(SSID, PASSWORD);
```

参考: [arduino-esp32 issue #11404](https://github.com/espressif/arduino-esp32/issues/11404)

## 環境リセット手順

```bash
rm -rf ~/.platformio/packages
rm -rf ~/.platformio/platforms
rm -rf ~/.local/share/esp32
rm -rf .pio
pio run
```
