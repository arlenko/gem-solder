# Gem Firmware
This firmware is derived from [IronOS](https://github.com/Ralim/IronOS)
(v2.23 commit [543fcd0](https://github.com/Ralim/IronOS/commit/543fcd0be6f28daac2f69c1caa4cce1047e1be24)),
originally targeting the Miniware TS101. Due to significant hardware differences between the TS101 and Gem maintaining a separate copy became necessary. Upstream IronOS changes are not automatically merged; this fork is maintained independently.

## Hardware

- **MCU**: STM32F103C8T6 (Cortex-M3, 64K flash, 20K RAM)
- **Display**: 128x32 OLED. SSD1306 display driver
- **Power**: DC and USB-C PD. STUSB4500 PD controller

## Building

1. Enter the source directory:
```
cd source
```

2. Create a Python virtual environment:
```
python -m venv ironos-venv
```

3. Activate virtual environment:
```
source ./ironos-venv/bin/activate # or activate.fish for the fish shell
```

4. Install dependencies:
```
pip install bdflib pyyaml
```

5. Build the firmware:
```
make -j$(nproc)
```
If you need SWD enabled for debugging:
```
make -j$(nproc) swd_enable=1
```

For all subsequent builds in a new terminal session consider activating a Python virtual environment before building the firmware.