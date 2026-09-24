# Repository guidance

## Scope and project

This file applies to the entire repository. This is an ESP32-S3 Arduino application built with PlatformIO for the LilyGO T-Embed CC1101/PN532 variant. Read `README.md`, `platformio.ini`, and the affected source before changing behavior. Keep documentation aligned with implemented features: Notes is a placeholder; wireless features comprise Wi-Fi NTP, HTTP CSV transfer, and main-screen NFC reads to serial.

## Where to work

- `src/main.cpp`: setup/loop, LVGL screens and encoder focus groups, input handling, active task state, deep sleep.
- `include/TaskDefs.h`: the single table of categories and activities. Prefer edits here for task customization.
- `lib/TaskLog/`: SD mount, append-only event log, boot recovery.
- `lib/RtcClock/`: timezone, system clock helpers, NTP synchronization.
- `lib/SyncServer/`: HTTP server lifecycle, download and destructive delete handlers.
- `lib/BatteryGauge/`: I2C fuel gauge access.
- `lib/NfcReader/`: PN532 lifecycle, tag detection, incremental read-only memory dumps to serial.
- `include/lv_conf.h`: LVGL settings; `boards/T_Embed_PN532.json` and `platformio.ini`: board/build settings.
- `scripts/load_env.py`: SCons pre-build script; `src/sync_log.py`: standalone desktop helper.

Keep changes focused and follow nearby C++ style (two-space indentation, existing function naming and module interfaces). Preserve unrelated working-tree edits. Do not modify generated dependencies or build output under `.pio/`.

## Build and checks

Run from the repository root:

```sh
pio run -e lilygo-t-embed-cc1101
python src/sync_log.py --help
```

The first command is the firmware compile check; the second is a CLI smoke check when changing the downloader. Use a PlatformIO terminal if `pio` is not on PATH. `scripts/load_env.py` depends on SCons `Import("env")`; do not run it directly as an ordinary Python script.

NFC host regression tests live in `tests/nfc/`; compile/run them with the C++17 commands in README when changing the reader. They use simulated hardware, not actual NFC communication. There is no CI. For code changes, run relevant checks and report exactly what ran. If the toolchain or physical board is unavailable, state that limitation instead of claiming build or hardware success. Documentation-only changes need source/link consistency and whitespace checks, not a firmware rebuild. Do not flash a device as a substitute for a compile check.

For hardware validation relevant to a change, check:

- Encoder selection, category popup focus, BACK navigation, and main-menu sleep.
- Start, switch, same-task reselection, and Clear; verify CSV event order and active-task recovery.
- Display/SD coexistence, missing SD handling, and fuel-gauge failure display.
- NTP success/timeout, sync-server start/download/stop, sleep/wake with an active task.
- Remote deletion only with disposable or backed-up logs, including failure handling.

## Hardware and UI constraints

- Preserve the CC1101 variant's pin mapping documented in README. The original T-Embed has different hardware. Check [LilyGO's CC1101 pinout](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101#4%EF%B8%8F%E2%83%A3-pins-) before changing pins.
- Assert peripheral power GPIO 15 before initializing peripherals. BACK is GPIO 6; the encoder push/BOOT input is GPIO 0.
- Initialize TFT before SD. SD must reuse `tft.getSPIinstance()` with CS 13; do not create a competing bus on GPIO 9/10/11.
- Keep TFT, SD, and LVGL operations out of the encoder ISR. If adding tasks or concurrency, explicitly synchronize shared SPI and input state.
- Maintain landscape rotation before calculating display dimensions. LVGL is version 8 with 16-bit color, a `320 * 20` pixel buffer, and manual `lv_tick_inc()` in `loop()`. Do not introduce LVGL 9 APIs or a second tick source without a deliberate migration.
- Associate each selectable screen/popup with the correct encoder group. Restore the parent group when closing a popup; delete temporary objects/groups without leaving dangling references. Sync screens intentionally have no selectable group.
- BACK sleep uses `LONG_PRESS_MS = 1500`; an older comment says three seconds and is stale. Wait for button release before enabling active-low ext0 wake to avoid immediate wakeup.
- Prefer bounded/nonblocking additions to `loop()`. Existing Wi-Fi waits and HTTP transfers already block UI processing.
- NFC shares the fuel gauge's `Wire` bus on GPIO 8/18 (address `0x24`, reset 45, IRQ 17). Initialize it after the gauge has configured Wire. Keep reads on the main screen, one memory transaction per loop, and hold PN532 reset LOW when leaving or sleeping. Preserve bounded polling, UID/frame-length checks, and held-tag duplicate suppression. The Adafruit UID helper does not expose SAK; the reader validates the raw selection response to classify tags. Do not infer tag type from UID length or introduce tag writes into this read-only feature.

## Data and time contracts

- Preserve `/log.csv` and its `timestamp,epoch,event,task` schema unless the task explicitly calls for a format migration. Coordinate writer, recovery, HTTP endpoints, downloader, and docs for format changes.
- Ordinary task events append; Time → Clear appends an `end` event and does not erase history. HTTP `DELETE /log.csv` is a separate destructive operation.
- A switch emits old-task `end` followed by new-task `start`. Selecting the same activity also restarts it. Boot restores state from the final nonempty data line, not a separate state store.
- `ActiveTaskState.task_name` and the selection buffer are 32 bytes. Keep `Category: Activity` within 31 bytes; avoid commas/newlines and preserve the `: ` separator used by UI highlighting, unless implementing proper encoding and matching changes.
- Logging return values currently are ignored by task callbacks. Do not assume displayed state proves persistence. Any reliability fix should address RAM/disk consistency and the two-write switch sequence explicitly.
- Use Unix epoch seconds for event time and local time only for presentation. Startup applies a POSIX timezone but does not synchronize time. `RtcClock` is a wrapper for system time, not an external RTC.
- Keep NTP success tied to its completion callback, rather than treating an already plausible clock as proof of a new sync. Changing the timezone requires reviewing both local formatting and time-setting helpers.
- Sleep does not end an activity. Do not promise correct elapsed time after power loss without a valid clock source.

## Credentials and network behavior

- `.env` and generated `include/wifi_secrets.h` are ignored and contain credentials. Do not print, commit, or copy their real values into documentation, logs, or examples.
- Update `scripts/load_env.py` for credential-generation changes; do not hand-edit its generated header. Its parser reads literal `KEY=value` lines, not full dotenv syntax or shell environment variables.
- Keep the server lifecycle tied to the sync screen and service it through `sync_server_handle()` in the main loop. Returning with BACK stops the server and Wi-Fi.
- The existing HTTP server is unauthenticated on port 80. Do not describe it as secure, cloud synchronization, or BLE transfer. Preserve the distinction between downloading and deleting data.
- The desktop downloader uses Python's standard library and overwrites its destination unless `--timestamped` is given. Keep its CLI documented if changed.

## Documentation and dependencies

Use official manufacturer documentation for hardware facts and source code for implemented behavior. Keep board capabilities separate from enabled firmware features. The local board name says QSPI PSRAM, but `memory_type = qio_opi` is the effective configuration. Do not silently replace board definitions or upgrade the pinned platform/library major versions as part of unrelated work.

When finishing, summarize changed behavior, checks performed, and any remaining limitations. Do not claim physical validation from code inspection alone.
