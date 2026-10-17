# T-SDR-C5 — notes for Claude

Handheld spectrum/waterfall for the LilyGO T-Display-C5 (ESP32-C5): a receive-only
SDR on ESPARGOS/esp-sdr, tuned by frequency over 2130–2730 and 4790–5990 MHz,
with a detail view (around the tuned frequency) and a band-survey view. Specification: [SPEC.md](SPEC.md).

## Layout

- `components/c5_radio` (C): tune/gain/bandwidth/capture; compiles upstream
  calibration sources straight from `third_party/esp-sdr` (pinned submodule).
- `components/core` (C++23, no ESP-IDF APIs): state/tuning ranges, gestures,
  FFT (radix-4), spectrum line, band survey (passband learning), menu, gain,
  colour maps, settings blob layout (`settings_codec`: append-only, versioned).
  Also built by `tests/host` (ASan/UBSan) — test changes there first.
- `components/board` (C++): LCD, backlight, buttons. `components/ui`: renderer.
- `main`: control/radio loop (prio 4) + UI task (prio 5, blocks on SPI).

## Rules

- Build only with the ESP-IDF commit in `idf-ref`; it must equal ESP-SDR's C5
  pin (CI enforces). The receiver patches private libphy ABI offsets.
- No large static buffers: bank 0 (0x40800000–0x4081FFFF) holds all static
  sections and is nearly full. Use heap (internal for DMA/hot data, PSRAM for
  bulk). CI fails below 8 KiB headroom (`tools/headroom.py`).
- Never read the capture buffer (SRAM bank 1) while the modem owns it.
- This machine has one core: build firmware in GitHub Actions, not locally.
  Host tests are cheap: `cmake -S tests/host -B <dir> && cmake --build <dir> && <dir>/core_tests`.
- Never transmit. Python only in a venv; never touch the system Python.
- Releases: tag `vX.Y.Z` on a commit **without** `[skip ci]` in its message —
  GitHub skips the tag push's workflow too. If it happens:
  `gh workflow run ci.yml --ref vX.Y.Z` (the release job keys off refs/tags/v*).
- Browser flasher: `web/index.html` + `tools/web_site.py`, deployed to GitHub
  Pages by the `pages` job on `v*` tags, or by hand (`gh workflow run ci.yml
  --ref main`). It offers every published release (version selector,
  `?version=` preselects) and flashes the separate images so NVS survives.
- The flasher page drives esptool-js 0.7.0 (unpkg, pinned) directly: >= 0.7.0
  is needed to detect ESP32-C5 rev 1.2. ESP Web Tools was dropped: its dialog
  bundles an older esptool-js (10.4.0) and mixes the console into the install flow.
- Logs: release builds are WARN level. For per-second measurements (lines/s,
  captures/line, per-phase timing, battery) build with
  `CONFIG_T_SDR_C5_STATS_LOG=y` and `CONFIG_LOG_DEFAULT_LEVEL_INFO=y` (the
  release WARN level compiles INFO out). CI's `firmware-stats` artifact is that build.
- The NVS namespace is lowercase `t-sdr-c5`; never change it (that orphans every
  board's saved settings). Releases up to v0.2.2 ship `t-sdr-c5-<tag>.tar.gz`
  archives with `t_sdr_c5.bin`; the flasher site reads both naming schemes.
