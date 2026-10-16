# t-sdr-c5 — notes for Claude

Handheld Wi-Fi waterfall for the LilyGO T-Display-C5 (ESP32-C5), receive-only
SDR built on ESPARGOS/esp-sdr. Specification: [SPEC.md](SPEC.md).

## Layout

- `components/c5_radio` (C): tune/gain/bandwidth/capture; compiles upstream
  calibration sources straight from `third_party/esp-sdr` (pinned submodule).
- `components/core` (C++23, no ESP-IDF APIs): channels, gestures, FFT,
  spectrum line, colour maps. Also built by `tests/host`.
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
