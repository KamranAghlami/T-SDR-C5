---
name: hardware-findings
description: T-Display-C5 facts verified on the user's board with the feasibility spike (2026-10-16)
metadata:
  type: project
---

Verified on hardware (spike photo, 2026-10-16):

- ST7789 panel is **RGB** element order (`LCD_RGB_ELEMENT_ORDER_RGB`). LilyGO's
  factory code says BGR via an old API field; that is wrong for esp_lcd in IDF 6.
  Landscape: swap_xy, mirror(true,false), invert, gap(0,35).
- 40 MHz SPI gives ~53 full 320x16 band updates per second with DMA.
- PSRAM 8 MB works with `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` (Wi-Fi .bss in PSRAM), 0 errors.
- Buttons GPIO0 / GPIO28, active low. ESP-SDR's `burst_gpio_init()` (called from
  `burst_serial_init()`) disables every unreserved GPIO — the spike's buttons
  read "DOWN" for that reason. We do not link burst_gpio.
- Rows written must cover all 170 lines, otherwise stale panel RAM shows as noise.

Verified with M1 firmware (2026-10-16): colours, spectrum orientation (Q negated,
see core::prepare_iq), 25-26 lines/s, NVS restore all correct. Vendor adctrig()
logs `I phy: 0x3ff,0x40000,...` on every capture; silence the "phy" tag. M1 got
11 captures/line with that logging on.

- Battery: AXP2602 is gauge-only (no presence/charger bit); TP4065 CHRG drives an
  LED only. With USB and no cell the charger holds VBAT ~4.2 V, so voltage can't
  detect a missing cell. Decide by cell current (|I| < 5 mA => external power).
- M3 timing on HW: capture 70 us, DSP 1446 us (FFT dominated: saturation
  branches), UI 3.5 ms/frame; 25 captures/line.

- Band survey on hardware (2026-10-16/17): fast retune ~1.0 ms/step; every 40 MHz
  capture shows a centre-heavy noise hump (passband) and residual DC at its centre,
  hence the learned passband correction and centre-bin blanking. 5 GHz (incl.
  UNII-3) receives fine through the Wi-Fi-driver PLL path.

**Why:** these cost a hardware round-trip to learn. **How to apply:** keep them
when touching board/ code. See [[sram-budget]].
