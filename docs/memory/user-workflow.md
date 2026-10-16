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
