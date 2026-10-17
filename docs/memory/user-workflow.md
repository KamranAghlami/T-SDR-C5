---
name: user-workflow
description: How the user works on t-sdr-c5 and tests firmware
metadata:
  type: user
---

The user flashes CI artifacts with esptool-js (browser) on another machine and
reports back with photos of the screen and boot logs; this dev VM has one core
and no USB device. They wanted a feasibility test and an approved spec before
building, all project docs/memory in the repo, CI as the build machine, and
releases on tags.

**How to apply:** make every hardware-facing change easy to verify from a photo
(on-screen diagnostics), give exact flash steps, and push small testable steps.

User preferences seen so far: no splash screen (it flashed by and served no
purpose); prefers keeping the device UI minimal and to the point.

Scope decisions: USB host mode (ESPARGOS viewer compatibility) dropped on
2026-10-16 — "people can use their firmware if they want to".

Product direction (2026-10-16): the gadget is a general receiver, not a Wi-Fi tool —
tune by frequency (whole MHz, user step), full chip ranges, no channel numbers in the
UI. Keep UI text minimal; the user notices cluttered labels and unthemed web UI.
