# Attack Shark M87

*A custom QMK firmware port for the Attack Shark M87 (Xinmeng M87) mechanical keyboard.*

* **Keyboard Maintainer**: [HuuTinh (@qpneruy)](https://github.com/qpneruy)
* **Hardware Supported**: Attack Shark M87 / Xinmeng M87 (SONiX SN32F248BF / HFD2201KBA)
* **Hardware Availability**: [Attack Shark](https://attackshark.com)

Make example for this keyboard (after setting up your build environment):

    make attackshark_m87:default

Flashing example for this keyboard:

    make attackshark_m87:default:flash

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 2 ways:

* **Bootmagic reset**: Hold down the key at (0,0) in the matrix (`Escape`) and plug in the keyboard
* **Keycode in layout**: Press the key mapped to `QK_BOOT` (`Fn + Esc` on Layer 1)
