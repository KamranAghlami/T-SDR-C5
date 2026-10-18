---
name: s3-findings
description: T-Display-S3 port facts verified on hardware (2026-10-19)
metadata:
  type: project
---

Verified on a T-Display-S3 (ESP32-S3R8 rev, 16 MB flash, 8 MB octal PSRAM):

- RF dump: vendor S3 aperture at 0x3fcd0000 (64 KiB, IRAM alias 0x403c0000),
  owner register 0x600c101c, dump control 0x60033d5c (bit 15 = 40 MS/s,
  bit 16 = 16 MS/s, bit 18 done, bit 19 trigger). Word layout matches the C5
  (I bits 0..9, Q 10..19).
- **Read the owner register back** (after `memw`) once the aperture is returned
  to the CPU. Without it ~99 % of snapshots still held sentinels when checked
  (about 11000 failures/s against 120 good captures); with it, 0 failures.
  The C5 engine does the same with `fence` + read-back.
- Timing at 240 MHz: capture 77 us, DSP 806 us per 1024-point snapshot; detail
  view 25 lines/s with 44 captures/line; band view 21 lines/s (tune_fast ~1.9 ms
  per 40 MHz step, of which 1 ms is the settle delay).
- Panel: ST7789 on 8-bit i80 (D0-D7 = GPIO39-42, 45-48; WR 8, RD 9 held high,
  DC 7, CS 6, RST 5, backlight 38, panel power GPIO15 must be high on battery).
  Same orientation/gap as the C5 (swap_xy, mirror(true,false), invert, gap(0,35)),
  RGB order, 20 MHz pixel clock.
- Buttons GPIO0 (BOOT) and GPIO14, active low. Battery: ADC1 ch3 (GPIO4), 1:2
  divider; reads ~4.9 V on USB without a cell.

Not yet verified: spectrum orientation against a known tone (core::prepare_iq
negates Q for the C5), and tuning above ~2650 MHz.

**Why:** the read-back cost a debugging round on hardware. **How to apply:**
keep it when touching s3_radio capture code. Related: [[hardware-findings]].
