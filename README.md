# Newey

**Newey** is a native Linux configuration and firmware utility for the **USB-Deskswitch II 1:4** (`22411R` / `22411S`) KVM switch.

## Key Features
- **Intuitive Switch Transfer Directions**:
  - **⬇ Download (from Switch to PC)**: Reads the active configuration from the connected USB-Deskswitch II 1:4 into Newey.
  - **⬆ Upload (from PC to Switch)**: Validates and writes the configuration from Newey to the connected USB-Deskswitch II 1:4.
- **Full `.Wey22411` Configuration Support**: Opens and saves `.Wey22411` configuration files (`V1.0` and `V1.1`).
- **Mouse Jiggle**: Per-source (`1`–`4`) enable and interval (`5..65535 s`).
- **Events & Hotkeys**: Startup source selection and up to 16 hotkey events with an interactive 105-key visual USB keyboard selector (up to 3 simultaneous keys per event).
- **Mouseswitch & 8×16 Monitor Grid**: Interactive 8×16 workstation monitor arrangement (`WS1`–`WS4`), custom resolutions, wrap-around switch-over, and per-monitor mouse enter/leave rules.
- **Firmware Update & Live Hardware Info**: Queries live firmware (`V1.7`) and ICSP (`V1.0`) versions and supports full firmware programming (`FRW22411_V1.7.hex`).
- **One-Click Linux USB Permissions**: Built-in `udev` rule installer (`/etc/udev/rules.d/99-wey-deskswitch.rules`) for non-root USB access (`1b07:0010`).

## Usage

```bash
# Launch the Newey GUI
./Newey-x86_64.AppImage

# Query connected USB-Deskswitch II 1:4 hardware info
./Newey-x86_64.AppImage --read-info

# Download configuration from Switch to PC file
./Newey-x86_64.AppImage --download my_config.Wey22411

# Upload configuration from PC file to Switch
./Newey-x86_64.AppImage --upload my_config.Wey22411
```

## License

**Newey** is licensed under **GNU General Public License v3.0 or later with the Commons Clause v1.0** (`GPL-3.0-or-later WITH Commons-Clause-1.0`).

- **Forks Must Remain Open Source (Copyleft)**: Any fork, modification, or derivative work must be distributed as open source under the same license terms with full source code.
- **Commercial Usage Allowed**: Businesses and organizations are free to use Newey internally and in commercial environments.
- **Resale Prohibited**: Selling, reselling, or charging a fee for Newey (or any fork/derivative thereof) is strictly prohibited.

See [LICENSE](./LICENSE) for the complete license text.
