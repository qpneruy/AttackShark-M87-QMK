## Xinmeng M87 (Attack Shark M87) Tri-Mode QMK/VIA Firmware v2.0.0

Stable release of QMK/VIA firmware for the Xinmeng M87 (Attack Shark M87) keyboard powered by the Sonix SN32F248BF (ARM Cortex-M0) and WCH CH582F wireless controller.

---

### Maintenance Notice
* This repository is in stable maintenance mode. No new features are planned unless upstream [SonixQMK](https://github.com/SonixQMK/qmk_firmware) introduces critical bug fixes or core platform updates.

---

### Changelog & Features:

1. **In-RAM RGB Tuning & Flash Protection (`_noeeprom`):**
   * Real-time RGB adjustments (mode, speed, hue, brightness) run in RAM to prevent Flash wear and eliminate corruption during low battery conditions.
   * Press **`Fn + /` (`M87_RGB_SAVE`)** to manually save current RGB and LED strip settings to Flash/EEPROM.

2. **Smooth Lighting Transitions:**
   * Seamless sweep transition when switching RGB modes (`RM_NEXT` / `RM_PREV`).
   * Eliminates matrix frame tearing and flicker during mode changes.

3. **Input Damping & Debounce:**
   * Added cooldown timers for lighting controls (260ms for mode change, 110ms for color/speed steps) to prevent accidental double-skips.
   * Smooth inertia interpolation for hue, brightness, and speed adjustments.

4. **Performance & Math Optimizations:**
   * Optimized math operations and lookup tables for smoother animation playback with reduced MCU overhead.

5. **Tri-Mode Connectivity:**
   * Seamless switching between 2.4GHz RF, Bluetooth 5.3 (3 profiles), and Type-C Wired.
   * Polling rates: 1000Hz NKRO in wired mode; 6KRO optimized for wireless battery life.
   * Battery level display on number row (`1`..`0`) via **`Fn + B`** or **`Fn + Alt`**.
   * Soft Power-Off via **`M87_OFF`**.
   * Independent toggle controls for Keylight, Sidelight, and Logo lightbar.
   * Full VIA v3 web support via [usevia.app](https://usevia.app).

---

### Assets:
* **`xinmeng_m87_default.bin`**: Default QMK firmware binary.
* **`xinmeng_m87_via.bin`**: VIA v3 enabled firmware binary.
* **`xinmeng_m87_via.json`**: VIA v3 keymap definition file for web configurator.
* **`sonixflasher`**: Standalone Linux x64 flashing utility.
