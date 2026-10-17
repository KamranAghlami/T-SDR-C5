# t-sdr-c5 — Specification (v0.3)

A pocket spectrum/waterfall gadget for the **LilyGO T-Display-C5**. It uses the
ESP32-C5's own Wi-Fi radio as a receive-only SDR (the ESP-SDR raw-IQ debug
path), computes FFTs on the chip and shows a live spectrum and waterfall on the
1.9" LCD: around any frequency from 2130–2730 or 4790–5990 MHz, or a whole range
at once. Two buttons tune and drive a small settings menu.

Status: decisions D1–D7 (§12) are implemented; v0.1.0 and v0.2.x are released and
verified on hardware. §1 is the original feasibility study, kept as a record.

---

## 1. Feasibility verdict

**Feasible** (2026-10-16). No blocker was found; the open hardware questions were
answered by the feasibility spike on the user's board (§11). Figures below are
from that study; current measurements are in §5.2.

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
| LCD SCK / MOSI / DC / CS / RST | 7 / 9 / 8 / 26 / 23 | ST7789, 170×320 IPS, used landscape 320×170, **RGB** element order (LilyGO's sample code says BGR, which is wrong for esp_lcd), inverted, gap (0, 35) |
| LCD backlight | 25 | LEDC PWM for brightness |
| Button **A** "USER" | 0 | active low, internal pull-up |
| Button **B** "BOOT" | 28 | active low, strapping pin (fine as input after boot) |
| I²C SDA / SCL | 2 / 3 | AXP2602 fuel gauge (battery %), CST816S touch on touch variant (unused) |
| AXP2602 INT | 10 | optional |
| UART0 TX / RX | 11 / 12 | left free (the ESP-SDR UART bridge is disabled) |
| Flash / PSRAM | — | 16 MB / 8 MB quad PSRAM |
| USB | native USB-Serial/JTAG | flashing, logs |

## 3. Functional specification

### 3.1 Tuning ranges

* **2.4G range: 2130–2730 MHz**, **5G range: 4790–5990 MHz**: the receive LO's exact-tuning
  limits as measured by ESPsoup (CW generator, several boards). They hold Wi-Fi, the 13 cm
  amateur band, 5.8 GHz FPV and more. Below about 2.1 GHz the LO doesn't follow.
* Tuning is by **frequency in whole MHz** (D6). Short A/B tune by the **Step** (1 / 5 / 10 /
  20 MHz, default 5), wrapping at the range ends; each range remembers its own frequency.
* Above 3 GHz the PLL is programmed through the Wi-Fi driver path (ESPsoup's `TUNEM 2`);
  the stock path is about 20 dB less sensitive above about 5745 MHz.
* The receiver is **receive-only**; the firmware never transmits on purpose (the stock PHY
  calibration can emit short internal signals, as on any ESP32 Wi-Fi firmware).

### 3.2 Views

**Channel view.** Centred on the tuned frequency:

* Capture: 80 MS/s, widest analog bandwidth (PHY mode 1, ≈48 MHz), 1024-point FFT, Hann
  window, mean removed per capture (DC/LO leakage).
* Displayed span 20 / 40 (default) / 80 MHz; the kept bins are max-combined into 320 px.
  At 80 MHz the analog roll-off beyond about ±24 MHz is visible.
* Per waterfall line (40 ms): as many snapshots as fit (≈25 measured), combined as **peak**
  (default, best for bursty signals) or **mean**.

**Band view** (D7). The whole range in one line:

* Stitched sweep in 40 MHz steps (15 for the 2.4G range, 30 for 5G); each step retunes
  without a DC measurement (≈1 ms), takes at least one snapshot and keeps the flat ±20 MHz.
* The receiver's passband shape (noise strongest at a capture's centre) is learned per bin
  offset from the quietest step of each line, smoothed over ≈16 lines and divided out
  (≤ ±12 dB). The 5 bins around each step's centre (DC/LO leakage) are skipped and their
  pixels filled from the neighbours.
* Short A/B move a cursor by the Step; the axis bar shows the span the channel view would
  display there, and switching View back to Channel opens it.

### 3.3 Screen layout (320 × 170, landscape)

```
┌──────────────────────────────────────────────┐  y
│ 2.4G 2437 MHz                  40MHz PEAK    │  0–15   range + frequency | span (BAND) + detector
│                            25/s A26 87%      │         lines/s, gain mode+index, power
├──────────────────────────────────────────────┤
│        ╱╲    ___                               │ 16–55   spectrum: mean (cyan) + decaying peak hold
│ ______╱  ╲__╱   ╲_______                       │
├──────────────────────────────────────────────┤ 56–67   axis: bar (20 MHz / band: the span), MHz labels,
│ ▔▔▔▔▔▔ 2430  2440  2450                       │         ticks below the labels
├──────────────────────────────────────────────┤
│▓▓▒▒░░  ░░▒▒▓▓█████▓▓▒░░                      │ 68–169  waterfall, 102 lines, newest on top
└──────────────────────────────────────────────┘
```

Colours (turbo or grey) are relative to the tracked noise floor (20th percentile, smoothed):
40 dB from floor to top. The status bar turns orange while frozen, red without captures.
The menu (8 items) covers rows 16–169 while open.

### 3.4 Buttons (A = IO0 "USER", B = IO28 "BOOT")

| Gesture | Normal | In menu |
|---|---|---|
| A short | tune down by the step (band view: move the cursor) | change the value |
| B short | tune up by the step (band view: move the cursor) | next item |
| A long (≥ 700 ms) | switch range 2.4G ↔ 5G | close |
| B long (≥ 700 ms) | open the menu | close |
| A + B together | freeze / unfreeze (restarts the line) | close |

No auto-repeat (a hold is a long press). Menu items: View, Span, Step, Detector, Gain
(Auto / HW AGC / manual 8, 16, …), Colours, Bright, About (version).

### 3.5 Persistence and boot

View, range, frequency per range, span, step, detector, gain, colour map and brightness
are stored in NVS (layout v4), written 2 s after the last change to limit flash wear.
Older layouts are read (v1–3 stored a Wi-Fi channel index, converted to its frequency).
Boot: the panel is cleared with the backlight off while the radio initialises (≈1 s, no
splash); the version is in the boot log and the menu's About item.

### 3.6 Gain

Default **auto-level**: a manual gain index adjusted between lines, never within one. If a
line's largest raw sample reaches 480/511 the gain drops by 4 at once; after 2 s with every
sample below 128 it rises by 2. Unlike the chip's hardware AGC it doesn't change gain inside
packets, so the waterfall doesn't pump. HW AGC and manual gain remain menu options; the
status bar shows the mode letter (A/H/G) and the index in use.

## 4. Non-goals (v1)

Demodulation/audio (the board has no speaker), transmitting, packet decoding,
continuous I/Q streaming to a PC or a USB SDR mode (D3: flash ESP-SDR for that), touch
input, Wi-Fi/BLE connectivity (the radio is busy being an SDR).

## 5. Signal chain and performance

### 5.1 Pipeline (single core, two tasks)

```
fast snapshot (80 MS/s × 1024 → SRAM bank 1, register-level engine)
→ prepare_iq: mean removal, Hann window, block scale (fills ±16384), bit-reversed order
→ FFT 1024 (Q15, no saturation): five radix-4 passes, the first multiply-free
→ |X|² normalised to a common scale → accumulate peak/mean
   channel view: until the 40 ms line deadline; band view: per sweep step
→ bins → 320 px, log (0.5 dB codes) → frame queue
UI task (higher priority, blocks on SPI): waterfall ring (PSRAM) → 2 × 320×16 DMA bands
```

### 5.2 Targets and measurements (hardware, 2026-10-16/17)

| Metric | Target | Measured |
|---|---|---|
| Channel-view line rate | ≥ 20 lines/s | 25–26 |
| Snapshots per line | ≥ 30 | 24–25 (channel view), ≈35 (2.4G band, older 3-step plan) |
| Capture / DSP per snapshot | — | ≈70 µs / ≈1.0 ms with the radix-2 stages (1.45 ms before the saturation-free FFT); radix-4 expected ≈0.83 ms |
| Band-view retune | — | ≈1.0 ms per step |
| Band-view line rate | — | 24 (3 steps), 19 (19 steps); 15/30-step ranges ≈15–25 expected |
| UI CPU per frame | — | ≈3.5 ms |
| Noise-floor accuracy of the fixed-point path | — | within 0.02 dB of double precision, 0.5–50 LSB rms (host test) |

The snapshot target is missed by a little. Per 1024-point capture the DSP was ≈210k
instructions (FFT 148k, `prepare_iq` 47k, accumulation 15k); the radix-4 passes cut the FFT
to ≈111k (RISC-V GCC: 92 instructions per 4 points over two stages instead of 4 × 32),
about 17 % of the total, so ≈28–30 snapshots per line are expected. Still to measure on
hardware.

### 5.3 Memory plan

* Bank 1 (128 KiB): owned by the RF dump; never heap. Capture data is read only after
  ownership returns.
* Internal SRAM: DMA bands 20 KiB, FFT work buffers, stacks, Wi-Fi; ≈60–70 KiB stays free.
* PSRAM: waterfall history (320 × 102 B), axis cache, Wi-Fi/lwip `.bss`.
* Static sections: ≈30 KiB headroom below the dump bank; CI fails below 8 KiB.

### 5.4 Limitations (also in the README)

* **Sampling, not gapless.** Each 40 ms line looks at well under 1 ms of RF.
  Busy channels and beacons show clearly, but a single short burst can be missed.
* Uncalibrated dB. Residual DC/LO leakage at the centre (mean removal, not perfect);
  image/IQ imbalance is visible on strong signals.
* Passband flat to about ±20 MHz; the 80 MHz span shows the roll-off.
* The tuning limits are ESPsoup's boards' figures; individual chips can differ near the ends,
  and board-specific dead spots are possible (ESPsoup saw 4960–5020 MHz deaf on one).

## 6. Software architecture

```
t-sdr-c5/
├─ third_party/esp-sdr/        git submodule, pinned (ESPARGOS/esp-sdr)
├─ components/
│  ├─ c5_radio/                C: tune (normal + fast), gain, bandwidth, DC recal, capture
│  │                           (upstream calibration sources from the submodule, ported
│  │                           ESPsoup engine & TUNEM; linker guard + --wrap options)
│  ├─ core/                    C++23, no ESP-IDF APIs: state/tuning, gestures, FFT,
│  │                           spectrum line, band survey, menu, gain, colour maps,
│  │                           settings layout (main/settings.cpp only does NVS I/O)
│  ├─ board/                   C++: LCD (esp_lcd ST7789), backlight, buttons, battery
│  └─ ui/                      C++: renderer (status, spectrum, axis, waterfall, menu)
├─ main/                       control/radio loop, UI task, settings (NVS)
├─ tests/host/                 host unit tests of core (ASan/UBSan)
├─ tools/                      headroom check, release packaging, flasher site
├─ web/                        browser flasher page (esptool-js)
├─ docs/                       SPEC.md, CLAUDE.md, memory/
├─ idf-ref                     pinned ESP-IDF commit (single source of truth)
└─ .github/workflows/ci.yml    tests, firmware, release, Pages
```

* ESP-IDF at the pinned commit; `MINIMAL_BUILD`; component manager off (reproducible).
* No LVGL: a purpose-built renderer is lighter on SRAM and faster for a waterfall.
* App in C++, radio layer in C (upstream-derived).

## 7. Build, CI and releases (GitHub Actions, `ci.yml`)

* Every push/PR: host tests → firmware at the pinned IDF (cached `~/.espressif`) in two
  variants, `release` and `stats` (per-second statistics log, version suffixed `-stats`) →
  SRAM headroom check → packaged artifacts. A drift check fails if `idf-ref` ≠ the ESP-SDR
  C5 pin in the submodule.
* Tag `v*`: GitHub Release with `t-sdr-c5-<ver>-merged.bin` (flash at `0x0`), the separate
  images + `flash_args` + `offsets.json` (tar.gz), `SHA256SUMS`, generated notes. Never tag a
  `[skip ci]` commit (GitHub skips the tag's run too).
* Then the `pages` job publishes the browser flasher (`web/index.html`, esptool-js 0.7.0)
  with every release; a manual run on a branch redeploys it.
* Version = `git describe`, shown in the boot log and the menu's About item.

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
| M3 | Fast engine, `TUNEM`, auto-level gain, menu, brightness, battery % — **done, verified on HW** | `v0.2.0` ✅ |
| M4 | Browser flasher (GitHub Pages; esptool-js, version selector, serial console, settings-preserving updates) — **done**; band survey; tuning by frequency — **on `main`, awaiting HW test** | `v0.3.0` |

## 10. Testing

* Host (`tests/host`, ASan/UBSan): FFT against a double-precision DFT (sizes 4–1024, odd and
  even stage counts), settings layout (byte-identical to the original struct, versions 1–4,
  rejection, sanitising), noise-floor accuracy
  of the fixed-point path, block-scale normalisation, power codes, spectrum orientation,
  line accumulation, survey plans and pixel mapping, passband flattening, tuning and
  wrap-around, gestures, menu, auto-level, colour maps.
* Device (the user, with the board): per release: boot, both views on both ranges,
  button gestures, menu, settings restore, the stats build's log (lines/s, timing).

## 11. Risks (all retired)

| Risk | Outcome |
|---|---|
| SPI DMA / PSRAM disturb captures (or vice versa) | Spike + firmware: 0 capture failures with the display running |
| Static SRAM headroom too small | ≈30 KiB headroom; CI gate at 8 KiB |
| Wi-Fi `.bss` in PSRAM breaks the PHY debug path | Works on hardware |
| Fast-engine port regressions | 0 failures on hardware; stock `adctrig` kept behind `CONFIG_C5_RADIO_STOCK_CAPTURE` |
| Panel at 80 MHz SPI | Not needed: 40 MHz meets the targets |

## 12. Decisions

| # | Decision | Choice |
|---|---|---|
| D1 | Radio code base | **Upstream ESP-SDR as a pinned submodule + ported ESPsoup fast capture engine and `TUNEM`** (attributed, GPL-3.0) |
| D2 | Buttons | **Proposed scheme** of §3.4 (short A/B = channel −/+, now frequency steps per D6, long A = band, long B = menu, A+B = freeze); auto-repeat dropped (it conflicted with long press); in the menu B walks items and A changes values (user request after HW test: fewer hand movements) |
| D3 | USB host mode (ESPARGOS viewer compatible) | **Dropped** (2026-10-16): anyone who wants a USB SDR can flash ESP-SDR itself |
| D4 | Language | **C++23 app/UI/DSP, C radio layer** |
| D5 | Default view | Channel view, 80 MS/s capture, 40 MHz displayed span, peak detector |
| D6 | Tuning (2026-10-16) | **By frequency, not Wi-Fi channel**: ranges 2130–2730 / 4790–5990 MHz (ESPsoup's exact-tuning limits), Step 1/5/10/20 MHz in the menu; band view = whole range, cursor moves by the step; settings v4 migrates saved channels |
| D7 | Band survey | Menu item View (A+B stays freeze); stitched 40 MHz steps, passband shape learned from the quietest step per offset and removed; centre bins blanked |
