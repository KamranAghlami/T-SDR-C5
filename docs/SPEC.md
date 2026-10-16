# t-sdr-c5 — Specification (v0.2, decisions recorded)

A pocket spectrum/waterfall gadget for the **LilyGO T-Display-C5**. It uses the
ESP32-C5's own Wi-Fi radio as a receive-only SDR (the ESP-SDR raw-IQ debug
path), computes FFTs on the chip and shows a live waterfall of a 2.4 GHz or 5 GHz
Wi-Fi channel on the 1.9" LCD. Two buttons switch channels and bands.

Status: **v0.2. Design decisions recorded (§12)**, pending final sign-off
and the on-hardware spike check. The flashable spike image
(`feasibility-tdisplay` artifact, see `spike/README.md`) answers the remaining hardware questions.

---

## 1. Feasibility verdict

**Feasible.** No blocker found. The risks that remain need a hardware check on
your board (§11), and the spike image in the `feasibility` branch exists to answer them.

| # | Question | Finding | Evidence | Verdict |
|---|---|---|---|---|
| F1 | Can the C5 capture raw I/Q? | Yes. Snapshot captures of up to 16,380 complex samples at 80/40/20/10/8/4 MS/s, 10-bit I/Q, written by the modem into SRAM bank 1 (`0x40820000–0x4083FFFF`). | ESP-SDR `main/families/c5_c6_c61/receiver.c`; C5 is a supported, CI-built target upstream | ✅ |
| F2 | 2.4 **and** 5 GHz? | Yes. Upstream tunes 100–6000 MHz (attempts) and switches to the 5 GHz RF path above 3000 MHz. ESPsoup measured exact tuning at 2130–2730 and 4790–5745 MHz on the stock path, and up to ≥5990 MHz via the Wi-Fi-driver path (`TUNEM`). On the stock path UNII-2e/UNII-3 (ch 100–165) is about 20 dB less sensitive. | ESP-SDR README; ESPsoup `docs/espsoup-c5.md` §Tuning | ✅ (port `TUNEM` for ch ≥ 100) |
| F3 | Antenna | The board ships with a dual-band IPEX antenna (module is ESP32-C5-WROOM-1**U**-N16R8). | LilyGO repo, CNX review | ✅ |
| F4 | Static SRAM fits with LCD + PSRAM? | All IRAM/.data/.bss must end below `0x40820000` (128 KiB bank 0), enforced by upstream's linker guard. Upstream has **only 1.2 KiB left**, and adding the PSRAM and LCD drivers naively **overflows by 13.8 KiB**. Moving upstream's 26.6 KiB spectrum workspace and the Wi-Fi `.bss` to PSRAM leaves **30.8 KiB of headroom** with the guard enforced. See §1.1. | `feasibility` CI runs 38914731477, 38915580492 | ✅ with mitigations |
| F5 | Display concurrent with captures | SPI2 + GDMA reads from banks 0/2 and never touches bank 1. ESPsoup found CPU/GDMA reads of bank 1 return garbage *while the modem owns it*, so all display buffers must stay out of bank 1 (they do, because the heap never maps it). | ESPsoup hardware findings | ✅ by design, ⚠️ to confirm on HW (spike) |
| F6 | CPU budget (single 240 MHz RISC-V core) | 1024-pt Q15 FFT ≈ 0.5 ms. At 25 waterfall lines/s about 40–50 FFTs fit per line, plus rendering. The SPI DMA (≈13 ms per full waterfall redraw at 40 MHz) runs while the CPU works. | Estimate from ESPsoup cycle counts (radix-2 ≈ 115 cyc/sample @ nfft 64) | ✅ |
| F7 | Capture overhead | Stock vendor `adctrig()` adds **≈1.7 ms per snapshot** (a bus-stalling register RMW). ESPsoup's register-level engine removes it (80 MS/s: 1886 → 206 µs). Without the port, the frame budget allows roughly 15 captures per line; with it, about 45. | ESPsoup §Measurements | ✅ (port the engine) |
| F8 | Gapless capture? | Not at waterfall bandwidths. Dump-path continuous mode reaches 91–96 % coverage only at 4–10 MS/s, and only 16–46 % at 20 MS/s. The gapless 40 MS/s PARLIO path is 4+4-bit and needs 8 free GPIO pads toggling at 40 MHz. **Out of scope**: this is a *sampling* spectrum analyser, see §5.4. | ESPsoup §Continuous engine, §PARLIO | ⚠️ limitation, documented |
| F9 | Toolchain | Must use the **exact ESP-IDF commit ESP-SDR pins for C5** (`25fe69f9…`, a 6.2-dev master commit). The receiver patches private `libphy` ABI offsets, so any other IDF (e.g. the `release-v6.1` Docker image) is unsafe. | `firmware-targets.json`, `rx_recalibration.c` | ✅ (pin + CI drift check) |
| F10 | Licence | ESP-SDR is GPL-3.0-or-later. ESPsoup parts are GPL-3.0-or-later, with some possibly GPL-3.0-only (C5VRX-inspired). Distributing **t-sdr-c5 under GPL-3.0** is compatible. | NOTICE files | ✅ |

### 1.1 Memory headroom (CI, ESP-IDF `25fe69f9`)

Bank 0 is `0x40800000–0x4081FFFF`. Every static section has to end below the dump bank.

| Build | `_iram_end` | `_data_end` | `_bss_end` | Headroom |
|---|---|---|---|---|
| ESP-SDR C5, unmodified | `0x4080f380` | `0x4081378e` | `0x4081fb10` | **1.2 KiB** |
| ESPsoup C5, unmodified | `0x40810f00` | `0x4081535e` | `0x4081f120` | 3.8 KiB |
| ESP-SDR + PSRAM + LCD + spike (guard removed) | `0x40812980` | `0x4081721a` | `0x40823618` | **−13.8 KiB (overflow)** |
| … + spectrum workspace & Wi-Fi `.bss` → PSRAM (`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`) | `0x40812980` | `0x40817262` | `0x408187b8` | **+30.8 KiB** ✅ |

Where the space goes:

* **Upstream `spectrum.c`:** 26.6 KiB of static FFT buffers (`fft_data`, `powers`, `window`, `twiddles`, `frame`).
  t-sdr-c5 doesn't link it and keeps its own DSP buffers on the heap.
* **PSRAM driver + SPI master ISR + cache/MMU code** add about 13.8 KiB of IRAM. Possible further
  savings: `CONFIG_SPI_MASTER_ISR_IN_IRAM=n` (≈2.6 KiB), dropping the UART bridge.
* Wi-Fi `.bss` (`g_cnxMgr` 5.9 KiB, NAN/BTWT/NVS state) moves to PSRAM automatically with the option above.

Design rules that follow: **no large static buffers**; DMA bands and FFT work buffers are
`heap_caps_malloc(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)` in bank 2; bulk state goes in PSRAM;
CI fails if headroom drops below 8 KiB.

### 1.2 What other ESP32 SDR projects taught us

| Project | Takeaway for this gadget |
|---|---|
| [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr) | The C5 receiver, DC recalibration, gain and bandwidth tables, and fixed-point FFT/log helpers we build on. |
| [pit711/espsoup-firmware](https://github.com/pit711/espsoup-firmware) | C5-specific fork: fast capture engine (no 1.7 ms stall), `TUNEM` for upper 5 GHz, hardware DC cal, SWEEP panorama (100–6000 MHz in 0.69 s), radix-8/4 FFT (46 vs 115 cyc/sample). We port the engine and `TUNEM`; the rest is optional later. |
| [cyber-murmel/esp-sdr-zephyr](https://github.com/cyber-murmel/esp-sdr-zephyr), [Twotoz/C5VRX](https://github.com/Twotoz/C5VRX) | Gapless 40 MS/s via MODEM_DIAG → GPIO → PARLIO (4+4 bit). Not used, because it needs 8 free toggling pads. A possible future experiment. |
| [z1000biker/esp32c3rxtx](https://github.com/z1000biker/esp32c3rxtx) | UI ideas: turbo colormap, DC notch, offset tuning to dodge the LO spike, blind IQ-balance correction (later). |
| [h0m3us3r/eSpDR](https://github.com/h0m3us3r/eSpDR), [jochenhammes/esp32-sdr-trx](https://github.com/jochenhammes/esp32-sdr-trx) | S3-only (FPGA / dual core), so not applicable. Confirms the zero-IF DC spike and the non-flat passband edges we must label. |
| [phodara/LilygoTDisplayC5](https://github.com/phodara/LilygoTDisplayC5) | Same board: Wi-Fi/BLE scanner UI, case with LiPo. Validates the display/button pin map. |

---

## 2. Hardware (LilyGO T-Display-C5)

| Function | GPIO | Notes |
|---|---|---|
| LCD SCK / MOSI / DC / CS / RST | 7 / 9 / 8 / 26 / 23 | ST7789, 170×320 IPS, used landscape 320×170, BGR, inverted, gap (0, 35) |
| LCD backlight | 25 | LEDC PWM for brightness |
| Button **A** "USER" | 0 | active low, internal pull-up |
| Button **B** "BOOT" | 28 | active low, strapping pin (fine as input after boot) |
| I²C SDA / SCL | 2 / 3 | AXP2602 fuel gauge (battery %), CST816S touch on touch variant (unused) |
| AXP2602 INT | 10 | optional |
| UART0 TX / RX | 11 / 12 | left free (the ESP-SDR UART bridge is disabled) |
| Flash / PSRAM | — | 16 MB / 8 MB quad PSRAM |
| USB | native USB-Serial/JTAG | flashing, logs, (later) host mode |

## 3. Functional specification

### 3.1 Bands and channels

* **2.4 GHz:** ch 1–13 (2412–2472 MHz, 5 MHz raster) and ch 14 (2484 MHz).
* **5 GHz (20 MHz channels):** 36–64 (UNII-1/2), 100–144 (UNII-2e), 149–165 (UNII-3),
  169–177 (UNII-4). Ch ≥ 100 use the `TUNEM` Wi-Fi-driver tuning path.
* The receiver is **receive-only**. The firmware never transmits. No region restrictions apply.

### 3.2 Views

**Channel view (MVP).** Centred on the selected channel:

* Capture: 80 MS/s, analog bandwidth 48 MHz (widest), 1024-pt FFT, Hann window.
* Displayed span: the **centre 40 MHz** (512 bins → 320 px, max-combined) so
  neighbouring/overlapping 2.4 GHz channels are visible. Spans of 20 / 40 / 80 MHz can be picked
  in the menu (80 MHz shows the full capture with the analog roll-off beyond ±24 MHz greyed).
* Per waterfall line: as many snapshots as fit the frame budget (target ≥ 40),
  combined as **peak** (default, best for bursty Wi-Fi) or **mean**.
* DC spike at the centre suppressed (ESP-SDR `spectrum_dc_apply`).

**Band survey (v1.1, stretch).** A stitched sweep of the whole band, one line per sweep:
2.4 GHz (2400–2500 MHz, 3 steps) about 20 lines/s; 5 GHz (5150–5895 MHz, 19 steps)
about 5–8 lines/s, with channel markers on the axis.

### 3.3 Screen layout (320 × 170, landscape)

```
┌──────────────────────────────────────────────┐  y
│ 2.4G  CH 6  2437 MHz   40M  PK  G:A  25fps ▮▮ │  0–15   status bar
├──────────────────────────────────────────────┤
│        ╱╲    ___                               │ 16–55   live spectrum (mean) + decaying peak trace
│ ______╱  ╲__╱   ╲_______                       │
├──┬─────┬─────┬─────┬─────┬────────────────────┤ 56–67   axis: MHz ticks + channel markers (current highlighted)
│ 2420  2430  2440  2450   │ 4  5 [6] 7  8       │
├──────────────────────────────────────────────┤
│▓▓▒▒░░  ░░▒▒▓▓█████▓▓▒░░                      │ 68–169  waterfall, 102 lines (≈4 s at 25 lines/s), newest on top
│ ...                                          │
└──────────────────────────────────────────────┘
```

Colour map: turbo (default) or grayscale/inferno. Auto-ranged dB scale
(noise floor tracked per band, configurable offset).

### 3.4 Buttons (A = IO0 "USER", B = IO28 "BOOT")

| Gesture | Normal | In menu |
|---|---|---|
| A short | previous channel (wraps within band) | next item |
| B short | next channel (wraps) | change value |
| A long (≥ 700 ms) | toggle band 2.4 ↔ 5 GHz (remembers the last channel per band) | — |
| B long (≥ 700 ms) | open menu | close menu (saves) |
| A + B together | freeze/unfreeze waterfall | — |

No auto-repeat: a hold is a long press. Channel steps wrap in both directions, so
stepping by clicks is quick. Until the menu lands (M3), B long cycles the span 20/40/80 MHz.
Menu items: span, detector (peak/mean), gain (auto-level / HW AGC / manual 0…max),
colour map, brightness, view (channel/band survey), about (version).

### 3.5 Persistence and boot

Band, channel per band, span, detector, gain, colour map and brightness are
stored in NVS (written after 2 s without changes, to limit flash wear). Boot: the panel is
cleared while the radio initialises (backlight off), then the last channel shows. No splash
screen (dropped after the first hardware test); the version is in the boot log and the
menu's About item. A cold boot to the first waterfall takes under 2 s.

### 3.6 Gain

Default **auto-level**: a manual gain index adjusted between lines, never within one. If a
line's largest raw sample reaches 480/511 the gain drops by 4 at once; after 2 s with every
sample below 128 it rises by 2. This doesn't pump the way hardware AGC does on bursty packets,
and it needs no blocking sweep on retune. (The earlier draft planned a sweep; the continuous
version is simpler and keeps adapting.) HW AGC and manual gain remain menu options. The gain
mode and index are shown in the status bar.

## 4. Non-goals (v1)

Demodulation/audio (the board has no speaker), transmitting, packet decoding,
continuous I/Q streaming to a PC, touch input, Wi-Fi/BLE connectivity
(the radio is busy being an SDR).

## 5. Signal chain and performance

### 5.1 Pipeline (single core, cooperative)

```
fast snapshot (80 MS/s × 1024, bank 1) → copy/convert to Q15 + window → FFT 1024 (Q15, radix-2;
radix-4/8 later) → |X|² → DC fix → accumulate peak/mean (512 centre bins)
   … repeat until line deadline …
→ bin→pixel max-combine (512→320) → log (spectrum_power_code, 0.5 dB steps) → line ring (PSRAM)
→ render status/spectrum/axis/waterfall into 2 × 320×16 DMA bands → SPI DMA (overlaps next captures)
```

### 5.2 Targets

| Metric | Target |
|---|---|
| Waterfall line rate | ≥ 20 lines/s (goal 25) |
| Button → retuned display | ≤ 150 ms (incl. DC recalibration) |
| Snapshots per line | ≥ 30 (goal 45) |
| RF time coverage | ≈ 1–3 % (sampling analyser, §5.4) |
| Frequency resolution | 78 kHz/bin, 125 kHz/px at 40 MHz span |
| SPI clock | 40 MHz (80 MHz if the panel tolerates it) |

### 5.3 Memory plan

* Bank 1 (128 KiB): owned by the RF dump; never heap. Capture data is read
  only after ownership returns.
* Internal SRAM (banks 0/2): DMA bands 20 KiB, FFT work ≈ 14 KiB, stacks, Wi-Fi.
* PSRAM: waterfall history (320 × 102 × 1 B), peak traces, UI state.

### 5.4 Honest limitations (go into the README)

* **Sampling, not gapless.** Each line looks at ≈0.5–1 ms of RF out of 40 ms.
  Busy channels and beacons show clearly, but a single short burst can be missed.
* Uncalibrated dB. Zero-IF DC spike at the centre (suppressed, not perfect).
  Image/IQ imbalance is visible on strong signals.
* Passband is flat to about ±20 MHz; edges beyond are attenuated and labelled.
* Stock tuning path is weak above about 5745 MHz (fixed by `TUNEM`). Board-to-board dead
  spots are possible (ESPsoup saw 4960–5020 MHz deaf on one board).

## 6. Software architecture

```
t-sdr-c5/
├─ third_party/esp-sdr/        git submodule, pinned (ESPARGOS/esp-sdr)
├─ components/
│  ├─ c5_radio/                C: tune, gain, bandwidth, DC recal, fast capture
│  │                           (builds upstream common/*.c from the submodule +
│  │                           ported ESPsoup engine & TUNEM; linker guard + --wrap opts)
│  ├─ board/                   C++: LCD (esp_lcd ST7789), backlight, buttons, battery
│  ├─ dsp/                     C++: window, FFT, accumulators, bin mapping, dB/colour LUT
│  └─ ui/                      C++: renderer (status, spectrum, axis, waterfall), menu
├─ main/                       app: state machine, settings (NVS), pipeline loop
├─ tests/host/                 host unit tests (channel tables, bin mapping, dB LUT)
├─ docs/                       SPEC.md, CLAUDE.md, memory/, hardware notes
├─ idf-ref                     pinned ESP-IDF commit (single source of truth)
└─ .github/workflows/          ci.yml, release.yml
```

* ESP-IDF at the pinned commit; `MINIMAL_BUILD`; component manager off (no
  registry downloads, reproducible builds); the esp-dsp submodule comes from upstream.
* No LVGL: a purpose-built renderer is lighter on SRAM and faster for a waterfall.
* App in C++ (matching your `t_display_c5` template), radio layer in C (upstream-derived).

## 7. Build, CI and releases (GitHub Actions)

* **ci.yml** (push/PR): host unit tests → firmware build at the pinned IDF (cached
  `~/.espressif`, shallow IDF checkout) → size/headroom report in the job summary →
  artifact. A drift check fails if `idf-ref` ≠ upstream's C5 pin in the submodule.
* **release.yml** (tag `v*`): same build, then a GitHub Release with
  `t-sdr-c5-<ver>-merged.bin` (flash at `0x0`), bootloader/partition/app bins,
  `flash_args`, `offsets.json`, `SHA256SUMS`, auto-generated notes. Uses `GITHUB_TOKEN`.
* Version = `git describe`, shown in the boot log and the menu About item.

## 8. README outline

Hero image/GIF · what it is · features · hardware & antenna · flash in 1 minute
(esptool command, offsets) · button cheat-sheet · screen legend · how it works
(diagram) · limitations · build from source / CI · credits (ESPARGOS, ESPsoup, …)
· GPL-3.0 licence.

## 9. Milestones

| M | Deliverable | Tag |
|---|---|---|
| M0 | Feasibility spike + HW check — **done** | — |
| M1 | Repo skeleton, CI, release pipeline, board bring-up, radio capture → one spectrum line — **done** | — |
| M2 | Channel view MVP: waterfall, spectrum, status, channel/band buttons, NVS — **done, verified on HW** | `v0.1.0` ✅ |
| M3 | Fast engine, `TUNEM`, auto-level gain, menu, brightness, battery % — **on `main`, awaiting HW test** | `v0.2.0` |
| M4 | Stretch: band survey, USB host mode (ESPARGOS viewer compatible), web flasher | `v0.3.0` |

## 10. Testing

* Host: unit tests for channel↔frequency tables, bin→pixel mapping, dB/colour LUTs,
  button gesture state machine (fed synthetic timings).
* Device (you, with the board): checklist per release: boot, every channel on both
  bands shows the expected traffic (e.g. your AP's channel), button gestures,
  NVS restore, ≥ 20 lines/s shown in the status bar.

## 11. Open risks and how we retire them

| Risk | Retired by |
|---|---|
| SPI DMA / PSRAM disturb captures (or vice versa) | Spike: viewer CRC errors and screen artefacts while animating |
| Static SRAM headroom too small | Measured: 30.8 KiB after mitigations (§1.1); CI gate at 8 KiB |
| Wi-Fi `.bss` in PSRAM breaks the PHY debug path | Spike boots with it enabled; fallback: keep Wi-Fi `.bss` internal (we still have ≈ 20 KiB) |
| Fast-engine port regressions | Keep stock `adctrig` as a compile-time fallback |
| Panel at 80 MHz SPI | Try at bring-up; 40 MHz meets the targets anyway |

## 12. Decisions (2026-10-16)

| # | Decision | Choice |
|---|---|---|
| D1 | Radio code base | **Upstream ESP-SDR as a pinned submodule + ported ESPsoup fast capture engine and `TUNEM`** (attributed, GPL-3.0) |
| D2 | Buttons | **Proposed scheme** of §3.4 (short A/B = channel −/+, long A = band, long B = menu, A+B = freeze); auto-repeat dropped (it conflicted with long press) |
| D3 | USB host mode (ESPARGOS viewer compatible) | **Later, in M4** |
| D4 | Language | **C++23 app/UI/DSP, C radio layer** |
| D5 | Default view | Channel view, 80 MS/s capture, 40 MHz displayed span, peak detector |
