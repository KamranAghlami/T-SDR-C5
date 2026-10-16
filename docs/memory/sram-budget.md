---
name: sram-budget
description: Static SRAM on the C5 must stay below 0x40820000; measured budget and mitigations
metadata:
  type: project
---

The RF dump owns SRAM bank 1 (0x40820000–0x4083FFFF), so IRAM+.data+.bss must
fit bank 0 (128 KiB). Measured (feasibility CI, 2026-10-16): upstream esp-sdr
leaves 1.2 KiB; adding PSRAM+LCD overflows by 13.8 KiB; moving upstream
spectrum.c buffers (26.6 KiB) and Wi-Fi .bss to PSRAM leaves 30.8 KiB.

**Why:** a link failure "C5 RF dump overlaps BSS" is the symptom. **How to apply:**
allocate buffers at runtime; check `tools/headroom.py` output in CI summaries.
Related: [[hardware-findings]].
