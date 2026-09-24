# Embed Time Tracker

An Arduino/PlatformIO time tracker for the LilyGO T-Embed CC1101. Select activities with the rotary encoder, store start/end events on a microSD card, and download the CSV over Wi-Fi. The interface uses LVGL 8 and TFT_eSPI.

## Hardware target

The repository targets the **T-Embed CC1101 / PN532 variant**: its environment is `lilygo-t-embed-cc1101`, its custom board is `T_Embed_PN532`, and its GPIO assignments match LilyGO's CC1101 pinout. This identification is based on the code, not inspection of a physical board. Do not assume the original T-Embed or other variants use the same wiring.

| Component | Board specification |
| --- | --- |
| MCU | ESP32-S3, dual-core LX7, 240 MHz |
| Memory | 16 MB flash, 8 MB PSRAM |
| Display | 1.9-inch ST7789V IPS TFT, 320 × 170 pixels |
| Network | 2.4 GHz 802.11 b/g/n Wi-Fi; Bluetooth 5.0 LE |
| Other radios | CC1101 sub-GHz transceiver; PN532 NFC |
| Controls/storage | Rotary encoder with push button, user button, microSD/TF slot |
| Battery | 3.7 V, 1300 mAh; BQ25896 charger and BQ27220 fuel gauge |
| Other peripherals | USB-C, infrared, microphone, speaker, eight WS2812 LEDs, QWIIC expansion |

Specifications: [LilyGO CC1101 documentation](https://wiki.lilygo.cc/products/t-embed-series/t-embed-cc1101/). Pin assignments: [official CC1101 repository](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101#4%EF%B8%8F%E2%83%A3-pins-). Checked September 22, 2026.

This firmware uses the display, controls, SD card, fuel gauge, Wi-Fi, and PN532 NFC reader. BLE, CC1101, infrared, audio, and RGB LEDs are not implemented here.

### GPIO assignments used by the firmware

| Function | GPIO |
| --- | --- |
| Encoder A / B / push (BOOT) | 4 / 5 / 0 |
| BACK/user button | 6 |
| Peripheral power enable | 15, driven HIGH at startup |
| TFT backlight / CS / DC | 21 / 41 / 16 |
| Shared TFT/SD SCLK / MOSI / MISO | 11 / 9 / 10 |
| SD chip select | 13 |
| Shared fuel gauge/NFC I2C SDA / SCL | 8 / 18 |
| PN532 reset / IRQ | 45 / 17 |

The TFT reset setting is `-1` (no dedicated reset GPIO). The display is configured as 170 × 320 and rotated with `setRotation(3)` for a 320 × 170 landscape interface. SD reuses `tft.getSPIinstance()`; it must not start a conflicting SPI bus. The fuel gauge is read at I2C address `0x55`.

## Build and upload

Use PlatformIO Core or the PlatformIO extension for VS Code, a USB data cable, and a FAT32 microSD card for persistent tracking.

1. Open this repository as the PlatformIO project.
2. Create `.env` in the project root with your network details:

   ```dotenv
   WIFI_SSID=your-network-name
   WIFI_PASSWORD=your-network-password
   ```

   Use plain values without surrounding quotes, `export`, or inline comments. The loader strips leading/trailing whitespace and does not expand environment variables. It generates `include/wifi_secrets.h` on each build. Both files are ignored by Git; do not edit the generated header. Missing keys become empty strings, so the firmware can build but Wi-Fi will not connect. Credentials are compiled into the firmware.

3. Build, connect the board, upload, and open the serial monitor:

   ```sh
   pio run -e lilygo-t-embed-cc1101
   pio run -e lilygo-t-embed-cc1101 -t upload
   pio device monitor -b 115200
   ```

   If necessary, add `--upload-port COM5` to the upload command and `-p COM5` to the monitor command, replacing `COM5` with your device port. Run these commands from a PlatformIO terminal if `pio` is not on your PATH.

The project pins `espressif32@6.13.0`, uses the Arduino framework, and requests `TFT_eSPI@^2.5.43`, `lvgl@^8.3.11`, `Adafruit PN532@1.3.4` (with BusIO), and `SD`. WiFi, WebServer, Wire, and SPI come from the ESP32 Arduino core. Upload and serial monitoring are configured for 115200 baud. The custom board selects `default_16MB.csv`, QIO flash, and `qio_opi` memory mode; its display name says QSPI PSRAM, but the effective configuration is OPI.

## Using the tracker

Rotate the encoder to move focus and press it to select. A short BACK press closes a category popup or returns to the parent menu. On the main menu, a short BACK press enters deep sleep. Holding BACK for **1.5 seconds** also enters deep sleep; release it to complete sleep entry, then press BACK to wake.

| Menu | Behavior |
| --- | --- |
| Time | Choose a category, then an activity to begin tracking |
| Time → Clear | End the active activity; keep all log history |
| Notes | Placeholder |
| Settings → Sync with PC | Connect to Wi-Fi and serve the SD log over HTTP |
| Settings → Sync Time | Connect to Wi-Fi and request time from `pool.ntp.org` |

Selecting an activity ends any current activity and starts the selected one, even when selecting the same activity again. The current activity and elapsed duration appear on the main and Time screens. The main screen also shows local time and battery percentage (`?%` on a failed gauge read).

Activities are defined in [include/TaskDefs.h](include/TaskDefs.h):

| Category | Activities |
| --- | --- |
| Body | Sleep, Hygiene, Eating |
| Mind | Working, University, Reflection |
| Relax | Phone, Gaming, Movies, Reading |
| Home | Cooking, Cleaning |
| Street | Social, Commute, Sport, Walking |
| Misc | Errand |

Edit this table and rebuild to customize activities. The combined `Category: Activity` name must fit in 31 bytes plus its null terminator. Avoid commas and newlines because the CSV writer does not escape fields.

### NFC reading on the main screen

Open the serial monitor at **115200 baud**, stay on the main menu, and hold an ISO14443A tag near the board's NFC antenna. The reader follows the known-good IRQ-driven PN532 sequence from the local `temporary/test_pn532` firmware and prints tag details to serial.

A held tag prints once; remove it briefly to scan it again. The reader prints UID, type, card ID for 4-byte UIDs, and tag text when present. The reader does not authenticate, dump full memory, write tag contents, or write task logs. The shared `Wire` bus uses the board's PN532 address `0x24`, 100 kHz I2C, and a short bus timeout. NFC work only runs while the main screen is active; leaving the main screen holds PN532 reset LOW.

### Clock and sleep

After a cold boot, use **Settings → Sync Time** before starting an activity. Startup applies the timezone but does not automatically set or synchronize the clock. `RtcClock` wraps the ESP32 system clock; it does not drive an external RTC.

The configured POSIX timezone is `EET-2EEST,M3.5.0/3,M10.5.0/4`, with UTC+2 standard time and UTC+3 daylight time. Change `lib/RtcClock/RtcClock.cpp` to use another timezone. NTP uses a completion callback and a roughly five-second connection/synchronization budget, then disconnects Wi-Fi.

Sleep does not end the active task. On wake/boot, the last log event restores its name and start epoch. Restoring the task does not restore a valid wall clock after power loss. Elapsed duration is calculated from system time, so a clock correction can change it; negative values display as zero.

## Log format and download

The SD file is `/log.csv`, created with this header:

```csv
timestamp,epoch,event,task
```

Each activity change appends `end` for the previous activity (if any), then `start` for the new activity. `timestamp` is local time formatted as `YYYY-MM-DD HH:MM:SS`; `epoch` is Unix seconds; `task` is the combined category/activity name. Durations are derived from events, not stored as separate rows. Boot scans the entire file and restores an active task only when the final nonempty data line is a `start` event.

To download:

1. Open **Settings → Sync with PC** and wait for the displayed URL.
2. Connect your computer to a network that can reach the board.
3. Open `http://<device-ip>/log.csv` in a browser, or use the Python helper:

   ```sh
   python src/sync_log.py 192.168.1.42
   python src/sync_log.py 192.168.1.42 --output activity.csv --timestamped
   ```

4. Press BACK on the board to stop the server and disconnect Wi-Fi.

The helper uses only the Python standard library. Use Python 3.9+ for `--timestamped` (`Path.with_stem`). By default it overwrites `./log.csv`; timestamped output adds a suffix. The output directory must already exist. Downloads copy the full file and do not merge or delete device records.

### HTTP interface

The server runs on port 80 while the sync screen is active.

| Method/path | Result |
| --- | --- |
| `GET /` | Plain-text usage hint |
| `GET /log.csv` | CSV download; 404 if the file cannot be opened |
| `DELETE /log.csv` | Remove history, recreate the CSV header, and signal the UI to clear its active task |

There is no authentication or TLS. Anyone who can reach the board while this server runs can download or delete the log. Use a trusted network. `DELETE` is destructive and is not used by the download helper.

## Code map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Hardware startup, LVGL screens/focus groups, encoder ISR, BACK navigation, active task state, sleep, main loop |
| `include/TaskDefs.h` | Category and activity definitions |
| `include/lv_conf.h` | LVGL configuration: 16-bit color, 128 KiB heap, manual tick updates |
| `lib/TaskLog/` | Shared SPI SD initialization, CSV append, last-event recovery |
| `lib/RtcClock/` | System time helpers, timezone, Wi-Fi/SNTP synchronization |
| `lib/SyncServer/` | Wi-Fi connection and CSV HTTP endpoints |
| `lib/BatteryGauge/` | BQ27220 state-of-charge reads over I2C |
| `lib/NfcReader/` | Main-screen PN532 ISO14443A UID and text detection |
| `scripts/load_env.py` | PlatformIO pre-build credential header generation |
| `src/sync_log.py` | Desktop CSV downloader; not firmware code |
| `platformio.ini`, `boards/` | Toolchain, dependencies, display pins, custom board settings |

## Current limitations and verification

- Task callbacks update RAM even when SD logging fails. A displayed active task therefore does not guarantee that an event was saved; inspect serial SD messages.
- Switching activities writes two separate rows, with no transaction or power-loss recovery. Recovery reads only the final data line and scans the whole file, so large logs increase startup time.
- Wi-Fi connection, NTP waiting, and HTTP file streaming are synchronous and can pause UI processing.
- The delete handler signals the UI to reset its active task even if recreating the log fails. Check the HTTP result and retain a backup before deleting history.
- Deep sleep turns off the display/backlight but does not explicitly disable the peripheral power rail. Battery current and runtime have not been characterized here.
- NFC hardware behavior must be checked on the board; a successful build does not verify physical NFC communication.

For firmware changes, build with PlatformIO and check on the board: display/encoder navigation, start/switch/Clear log ordering, active-task recovery after wake, NTP success/failure, CSV download, server shutdown on BACK, and battery display. For NFC, check main-screen ISO14443A UID/text detection, repeated presentation after removal, and normal responsiveness when navigating away from the main screen. Test log deletion only with disposable data. See [AGENTS.md](AGENTS.md) for development constraints.
