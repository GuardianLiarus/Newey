#include "wey_usb.h"

#include <libusb-1.0/libusb.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sstream>
#include <iomanip>

static constexpr uint16_t WEY_VID = 0x1B07;
static constexpr uint16_t WEY_PID = 0x0010;
static constexpr uint8_t  EP1_OUT = 0x01;
static constexpr uint8_t  EP1_IN  = 0x81;

UsbStatusInfo CheckUsbDeviceStatus() {
    UsbStatusInfo info;
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) != 0) {
        return info;
    }

    libusb_device** list = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device* dev = list[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) == 0) {
            if (desc.idVendor == WEY_VID && desc.idProduct == WEY_PID) {
                info.bus = libusb_get_bus_number(dev);
                info.addr = libusb_get_device_address(dev);
                char path_buf[128];
                std::snprintf(path_buf, sizeof(path_buf), "/dev/bus/usb/%03u/%03u", info.bus, info.addr);
                info.dev_path = path_buf;

                if (access(path_buf, R_OK | W_OK) == 0) {
                    info.status = UsbPresenceStatus::ConnectedReady;
                } else {
                    info.status = UsbPresenceStatus::ConnectedNoPermission;
                }
                break;
            }
        }
    }

    if (list) libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return info;
}

bool InstallUdevRuleAndFixPermissions(std::string& err_msg) {
    // Use pkexec to install /etc/udev/rules.d/99-wey-deskswitch.rules, reload udev, and chmod 0666 the device node
    const char* cmd =
        "pkexec /bin/sh -c '"
        "printf \"SUBSYSTEM==\\\"usb\\\", ATTR{idVendor}==\\\"1b07\\\", ATTR{idProduct}==\\\"0010\\\", MODE=\\\"0666\\\", TAG+=\\\"uaccess\\\"\\n\" "
        "> /etc/udev/rules.d/99-wey-deskswitch.rules && "
        "udevadm control --reload-rules && "
        "udevadm trigger --attr-match=idVendor=1b07 && "
        "for d in /sys/bus/usb/devices/*; do "
        "  if [ -f \"$d/idVendor\" ] && [ \"$(cat \"$d/idVendor\" 2>/dev/null)\" = \"1b07\" ]; then "
        "    bus=$(cat \"$d/busnum\" 2>/dev/null); "
        "    dev=$(cat \"$d/devnum\" 2>/dev/null); "
        "    if [ -n \"$bus\" ] && [ -n \"$dev\" ]; then "
        "      chmod 0666 $(printf \"/dev/bus/usb/%03d/%03d\" \"$bus\" \"$dev\"); "
        "    fi; "
        "  fi; "
        "done'";

    int ret = std::system(cmd);
    if (ret == 0) {
        return true;
    }
    err_msg = "Failed or cancelled while granting USB permissions via pkexec.";
    return false;
}

class WeyUsbSession {
public:
    libusb_context* ctx = nullptr;
    libusb_device_handle* handle = nullptr;
    bool claimed = false;
    bool in_prog_mode = false;
    uint16_t product_id = 0;

    ~WeyUsbSession() {
        Close();
    }

    bool Open(std::string& err) {
        if (libusb_init(&ctx) != 0) {
            err = "Failed to initialize libusb-1.0.";
            return false;
        }

        libusb_device** list = nullptr;
        ssize_t cnt = libusb_get_device_list(ctx, &list);
        libusb_device* found = nullptr;
        for (ssize_t i = 0; i < cnt; ++i) {
            libusb_device_descriptor desc{};
            if (libusb_get_device_descriptor(list[i], &desc) == 0) {
                if (desc.idVendor == WEY_VID && desc.idProduct == WEY_PID) {
                    found = list[i];
                    break;
                }
            }
        }

        if (!found) {
            if (list) libusb_free_device_list(list, 1);
            err = "Cannot find any WEY Deskswitch II USB-Device (1b07:0010)!";
            return false;
        }

        int r = libusb_open(found, &handle);
        libusb_free_device_list(list, 1);
        if (r != 0 || !handle) {
            if (r == LIBUSB_ERROR_ACCESS) {
                err = "USB Permission Denied (LIBUSB_ERROR_ACCESS). Click 'Grant USB Access' to install the udev rule.";
            } else {
                err = std::string("Cannot open WEY USB-Device: ") + libusb_error_name(r);
            }
            return false;
        }

        libusb_set_auto_detach_kernel_driver(handle, 1);
        r = libusb_claim_interface(handle, 0);
        if (r != 0) {
            err = std::string("Cannot claim USB interface 0: ") + libusb_error_name(r);
            return false;
        }
        claimed = true;

        // OpenDevice handshake (0x4ce4ec): send [0x01], read 5 bytes [0x01, b1, b2, prod_lo, prod_hi]
        uint8_t cmd = 0x01;
        if (!WriteEP1(&cmd, 1, 2000, err)) {
            return false;
        }
        uint8_t resp[64] = {0};
        int actual = 0;
        if (!ReadEP1(resp, sizeof(resp), actual, 2000, err)) {
            return false;
        }
        if (actual < 5 || resp[0] != 0x01) {
            err = "Unexpected identification response from WEY USB-Device.";
            return false;
        }
        product_id = static_cast<uint16_t>(resp[3] | (resp[4] << 8));
        if (product_id != 1160 && product_id != 1) {
            err = "Connected WEY device has unexpected Product ID (" + std::to_string(product_id) + ", expected 1160).";
            return false;
        }
        return true;
    }

    void Close() {
        if (handle && in_prog_mode) {
            std::string dummy;
            LeaveProgrammingMode(dummy);
        }
        if (handle && claimed) {
            libusb_release_interface(handle, 0);
            claimed = false;
        }
        if (handle) {
            libusb_close(handle);
            handle = nullptr;
        }
        if (ctx) {
            libusb_exit(ctx);
            ctx = nullptr;
        }
    }

    bool WriteEP1(const uint8_t* data, int len, unsigned int timeout_ms, std::string& err) {
        int transferred = 0;
        int r = libusb_bulk_transfer(handle, EP1_OUT, const_cast<uint8_t*>(data), len, &transferred, timeout_ms);
        if (r != 0 || transferred != len) {
            err = std::string("Error writing to USB-Device (EP1 OUT): ") + libusb_error_name(r);
            return false;
        }
        return true;
    }

    bool ReadEP1(uint8_t* data, int max_len, int& actual_len, unsigned int timeout_ms, std::string& err) {
        actual_len = 0;
        int r = libusb_bulk_transfer(handle, EP1_IN, data, max_len, &actual_len, timeout_ms);
        if (r != 0) {
            err = std::string("Error reading from USB-Device (EP1 IN): ") + libusb_error_name(r);
            return false;
        }
        return true;
    }

    bool ReadEP1Exact(uint8_t* data, int expected_len, unsigned int timeout_ms, std::string& err) {
        int total = 0;
        while (total < expected_len) {
            int got = 0;
            int r = libusb_bulk_transfer(handle, EP1_IN, data + total, expected_len - total, &got, timeout_ms);
            if (r != 0 || got <= 0) {
                err = std::string("Error reading flash data from USB-Device: ") + libusb_error_name(r);
                return false;
            }
            total += got;
        }
        return true;
    }

    // EnterProgrammingMode (0x4e15c0): [0x03]
    bool EnterProgrammingMode(std::string& err) {
        uint8_t cmd = 0x03;
        if (!WriteEP1(&cmd, 1, 2000, err)) return false;
        in_prog_mode = true;
        return true;
    }

    // LeaveProgrammingMode (0x4e1648): [0x04]
    bool LeaveProgrammingMode(std::string& err) {
        uint8_t cmd = 0x04;
        bool ok = WriteEP1(&cmd, 1, 2000, err);
        in_prog_mode = false;
        return ok;
    }

    // SelectChip (0x4e16d0): [0x0B, chip_index]
    bool SelectChip(uint8_t chip_index, std::string& err) {
        uint8_t cmd[2] = {0x0B, chip_index};
        return WriteEP1(cmd, 2, 2000, err);
    }

    // GetFirmwareVersion (0x4e175c): [0x0C] -> [0x0C, icsp_lo, icsp_hi, fw_lo, fw_hi]
    bool GetFirmwareVersion(uint16_t& fw_ver, uint16_t& icsp_ver, std::string& err) {
        uint8_t cmd = 0x0C;
        if (!WriteEP1(&cmd, 1, 2000, err)) return false;
        uint8_t resp[64] = {0};
        int actual = 0;
        if (!ReadEP1(resp, sizeof(resp), actual, 3000, err)) return false;
        if (actual < 5) {
            err = "Incomplete response reading firmware version.";
            return false;
        }
        icsp_ver = static_cast<uint16_t>(resp[1] | (resp[2] << 8));
        fw_ver   = static_cast<uint16_t>(resp[3] | (resp[4] << 8));
        return true;
    }

    // SaveFirmwareVersion (0x4e1898): [0x0D, ver_lo, ver_hi]
    bool SaveFirmwareVersion(uint16_t ver, std::string& err) {
        uint8_t cmd[3] = {
            0x0D,
            static_cast<uint8_t>(ver & 0xFF),
            static_cast<uint8_t>((ver >> 8) & 0xFF)
        };
        return WriteEP1(cmd, 3, 2000, err);
    }

    // PIC32 ReadDataFlash1024 (0x4cfbe8): [0x32, addr_le32(addr + 0x1D000000)] -> 1024B
    bool PIC32_ReadDataFlash1024(uint32_t offset, uint8_t out_buf[1024], std::string& err) {
        uint32_t phys = offset + 0x1D000000u;
        uint8_t cmd[5] = {
            0x32,
            static_cast<uint8_t>(offset & 0xFF),
            static_cast<uint8_t>((offset >> 8) & 0xFF),
            static_cast<uint8_t>((phys >> 16) & 0xFF),
            static_cast<uint8_t>((phys >> 24) & 0xFF)
        };
        if (!WriteEP1(cmd, 5, 2000, err)) return false;
        return ReadEP1Exact(out_buf, 1024, 3000, err);
    }

    // PIC32 WriteDataFlash1024 (0x4cf97c): [0x31, addr_le32(addr + 0x1D000000)] + 1024B
    bool PIC32_WriteDataFlash1024(uint32_t offset, const uint8_t in_buf[1024], std::string& err) {
        bool all_ff = true;
        for (int i = 0; i < 1024; ++i) {
            if (in_buf[i] != 0xFF) {
                all_ff = false;
                break;
            }
        }
        if (all_ff) return true;

        uint32_t phys = offset + 0x1D000000u;
        uint8_t cmd[5] = {
            0x31,
            static_cast<uint8_t>(offset & 0xFF),
            static_cast<uint8_t>((offset >> 8) & 0xFF),
            static_cast<uint8_t>((phys >> 16) & 0xFF),
            static_cast<uint8_t>((phys >> 24) & 0xFF)
        };
        if (!WriteEP1(cmd, 5, 2000, err)) return false;
        return WriteEP1(in_buf, 1024, 3000, err);
    }

    // PIC32 WriteBootFlash1024 (0x4cfb18): [0x31, addr_le32(addr + 0x1FC00000)] + 1024B
    bool PIC32_WriteBootFlash1024(uint32_t offset, const uint8_t in_buf[1024], std::string& err) {
        bool all_ff = true;
        for (int i = 0; i < 1024; ++i) {
            if (in_buf[i] != 0xFF) {
                all_ff = false;
                break;
            }
        }
        if (all_ff) return true;

        uint32_t phys = offset + 0x1FC00000u;
        uint8_t cmd[5] = {
            0x31,
            static_cast<uint8_t>(offset & 0xFF),
            static_cast<uint8_t>((offset >> 8) & 0xFF),
            static_cast<uint8_t>((phys >> 16) & 0xFF),
            static_cast<uint8_t>((phys >> 24) & 0xFF)
        };
        if (!WriteEP1(cmd, 5, 2000, err)) return false;
        return WriteEP1(in_buf, 1024, 3000, err);
    }

    // PIC32 BulkErase (0x4cfd8c): [0x33]
    bool PIC32_BulkErase(std::string& err) {
        uint8_t cmd = 0x33;
        return WriteEP1(&cmd, 1, 5000, err);
    }

    // PIC32 PageErase4096 (0x4cfdb4): [0x34, addr_le32(addr + 0x1D000000)]
    bool PIC32_PageErase4096(uint32_t offset, std::string& err) {
        uint32_t phys = offset + 0x1D000000u;
        uint8_t cmd[5] = {
            0x34,
            static_cast<uint8_t>(offset & 0xFF),
            static_cast<uint8_t>((offset >> 8) & 0xFF),
            static_cast<uint8_t>((phys >> 16) & 0xFF),
            static_cast<uint8_t>((phys >> 24) & 0xFF)
        };
        return WriteEP1(cmd, 5, 3000, err);
    }

    // PIC32 GetCRC (0x4cfe08): [0x35, start_le32, len_le32] -> [0x35, crc_lo, crc_hi]
    bool PIC32_GetCRC(uint32_t start_addr, uint32_t length, uint16_t& out_crc, std::string& err) {
        uint8_t cmd[9] = {
            0x35,
            static_cast<uint8_t>(start_addr & 0xFF),
            static_cast<uint8_t>((start_addr >> 8) & 0xFF),
            static_cast<uint8_t>((start_addr >> 16) & 0xFF),
            static_cast<uint8_t>((start_addr >> 24) & 0xFF),
            static_cast<uint8_t>(length & 0xFF),
            static_cast<uint8_t>((length >> 8) & 0xFF),
            static_cast<uint8_t>((length >> 16) & 0xFF),
            static_cast<uint8_t>((length >> 24) & 0xFF),
        };
        if (!WriteEP1(cmd, 9, 2000, err)) return false;
        uint8_t resp[64] = {0};
        int actual = 0;
        if (!ReadEP1(resp, sizeof(resp), actual, 3000, err)) return false;
        if (actual < 3) {
            err = "Incomplete CRC response from PIC32.";
            return false;
        }
        out_crc = static_cast<uint16_t>(resp[1] | (resp[2] << 8));
        return true;
    }

    // PIC18 BulkErase (0x4cf22c): [0x14]
    bool PIC18_BulkErase(std::string& err) {
        uint8_t cmd = 0x14;
        return WriteEP1(&cmd, 1, 3000, err);
    }

    // PIC18 WriteDataFlash1024 (0x4cef94): [0x11, addr_le32] + 1024B
    bool PIC18_WriteDataFlash1024(uint32_t offset, const uint8_t in_buf[1024], std::string& err) {
        bool all_ff = true;
        for (int i = 0; i < 1024; ++i) {
            if (in_buf[i] != 0xFF) {
                all_ff = false;
                break;
            }
        }
        if (all_ff) return true;

        uint8_t cmd[5] = {
            0x11,
            static_cast<uint8_t>(offset & 0xFF),
            static_cast<uint8_t>((offset >> 8) & 0xFF),
            static_cast<uint8_t>((offset >> 16) & 0xFF),
            static_cast<uint8_t>((offset >> 24) & 0xFF)
        };
        if (!WriteEP1(cmd, 5, 2000, err)) return false;
        return WriteEP1(in_buf, 1024, 3000, err);
    }

    // PIC18 WriteConfigFlash14 (0x4cf11c): [0x12, 14B]
    bool PIC18_WriteConfigFlash14(const uint8_t cfg14[14], std::string& err) {
        uint8_t cmd[15];
        cmd[0] = 0x12;
        std::memcpy(&cmd[1], cfg14, 14);
        return WriteEP1(cmd, 15, 2000, err);
    }
};

// ============================================================================
// High-Level Operations (REVERSED Download / Upload as requested by User!)
// ============================================================================

// DownloadConfigFromSwitch: Reads configuration from the switch into the PC
// (Corresponds to legacy Device_Upload at 0x4de9b8)
bool DownloadConfigFromSwitch(T_Config11& out_cfg, std::string& msg, ProgressCallback progress) {
    if (progress) progress(0.1, "Opening USB communication...");
    WeyUsbSession usb;
    if (!usb.Open(msg)) return false;

    if (progress) progress(0.25, "Entering programming mode...");
    if (!usb.EnterProgrammingMode(msg)) return false;

    if (progress) progress(0.4, "Selecting MAIN controller (Chip 0)...");
    if (!usb.SelectChip(0, msg)) return false;

    uint8_t flash_buf[2048];
    std::memset(flash_buf, 0xFF, sizeof(flash_buf));

    if (progress) progress(0.6, "Reading configuration block 1/2 (0x7E000)...");
    if (!usb.PIC32_ReadDataFlash1024(0x7E000, &flash_buf[0], msg)) return false;

    if (progress) progress(0.8, "Reading configuration block 2/2 (0x7E400)...");
    if (!usb.PIC32_ReadDataFlash1024(0x7E400, &flash_buf[1024], msg)) return false;

    if (progress) progress(0.95, "Leaving programming mode...");
    if (!usb.LeaveProgrammingMode(msg)) return false;
    usb.Close();

    uint16_t ver = static_cast<uint16_t>(flash_buf[0] | (flash_buf[1] << 8));
    if (ver == 0x000B) {
        PIC11_to_Config11(flash_buf, out_cfg);
        msg = "Successfully downloaded configuration (V1.1) from WEY Deskswitch II.";
    } else if (ver == 0x000A) {
        PIC10_to_Config11(flash_buf, out_cfg);
        msg = "Successfully downloaded configuration (V1.0 -> upgraded to V1.1) from WEY Deskswitch II.";
    } else if (ver == 0xFFFF) {
        DefaultValues(out_cfg);
        msg = "Switch configuration area is blank (0xFFFF); loaded factory default settings.";
    } else {
        msg = "The switch configuration version (" + std::to_string(ver) +
              ") is not compatible with this software version.";
        return false;
    }

    if (progress) progress(1.0, "Download complete.");
    return true;
}

// UploadConfigToSwitch: Writes configuration from the PC to the switch
// (Corresponds to legacy Device_Download at 0x4deb68)
bool UploadConfigToSwitch(const T_Config11& in_cfg, std::string& msg, ProgressCallback progress) {
    if (progress) progress(0.1, "Opening USB communication...");
    WeyUsbSession usb;
    if (!usb.Open(msg)) return false;

    if (progress) progress(0.2, "Entering programming mode...");
    if (!usb.EnterProgrammingMode(msg)) return false;

    if (progress) progress(0.3, "Selecting MAIN controller (Chip 0)...");
    if (!usb.SelectChip(0, msg)) return false;

    if (progress) progress(0.45, "Checking firmware compatibility (0x7F000)...");
    uint8_t check_buf[1024];
    if (!usb.PIC32_ReadDataFlash1024(0x7F000, check_buf, msg)) return false;

    uint16_t fw_cfg_ver = static_cast<uint16_t>(check_buf[0] | (check_buf[1] << 8));
    if (fw_cfg_ver != 0x000B) {
        std::string dummy;
        usb.LeaveProgrammingMode(dummy);
        usb.Close();
        msg = "The switch firmware config version (0x" +
              ([&]() { std::ostringstream ss; ss << std::hex << fw_cfg_ver; return ss.str(); })() +
              ") is not compatible with V1.1 (expected 0x000B). Please update firmware first.";
        return false;
    }

    uint8_t flash_buf[2048];
    Config11_to_PIC11(in_cfg, flash_buf);

    if (progress) progress(0.6, "Erasing configuration page (0x7E000)...");
    if (!usb.PIC32_PageErase4096(0x7E000, msg)) return false;

    if (progress) progress(0.75, "Writing configuration block 1/2 (0x7E000)...");
    if (!usb.PIC32_WriteDataFlash1024(0x7E000, &flash_buf[0], msg)) return false;

    if (progress) progress(0.9, "Writing configuration block 2/2 (0x7E400)...");
    if (!usb.PIC32_WriteDataFlash1024(0x7E400, &flash_buf[1024], msg)) return false;

    if (progress) progress(0.95, "Leaving programming mode...");
    if (!usb.LeaveProgrammingMode(msg)) return false;
    usb.Close();

    if (progress) progress(1.0, "Upload complete.");
    msg = "Successfully uploaded configuration from PC to WEY Deskswitch II.";
    return true;
}

// ResetSwitchToDefaultSettings: Resets configuration on the switch to factory defaults
// (Corresponds to Unit_PRG.DefaultSettings at 0x4e09c8)
bool ResetSwitchToDefaultSettings(T_Config11& out_cfg, std::string& msg, ProgressCallback progress) {
    DefaultValues(out_cfg);
    if (!UploadConfigToSwitch(out_cfg, msg, progress)) {
        return false;
    }
    msg = "Successfully reset WEY Deskswitch II to factory default settings.";
    return true;
}

// ReadSwitchDeviceInfo: Reads firmware & ICSP versions from the switch
bool ReadSwitchDeviceInfo(SwitchDeviceInfo& out_info, std::string& msg, ProgressCallback progress) {
    if (progress) progress(0.2, "Opening USB communication...");
    WeyUsbSession usb;
    if (!usb.Open(msg)) return false;

    out_info.product_id = usb.product_id;
    UsbStatusInfo st = CheckUsbDeviceStatus();
    out_info.bus_number = st.bus;
    out_info.device_address = st.addr;

    if (progress) progress(0.6, "Reading Firmware & ICSP versions...");
    uint16_t fw = 0, icsp = 0;
    if (!usb.GetFirmwareVersion(fw, icsp, msg)) return false;

    out_info.chips[0].chip_index = 0;
    out_info.chips[0].fw_version = fw;
    out_info.chips[0].icsp_version = icsp;
    out_info.chips[0].name = "USB Deskswitch II 1to4";

    usb.Close();
    if (progress) progress(1.0, "Done.");
    msg = "Successfully read device information.";
    return true;
}

// UpdateSwitchFirmware: Full firmware update matching Unit_PRG.UpdateFirmware (0x4e05a0)
bool UpdateSwitchFirmware(const FirmwarePackage& pkg, bool apply_default_settings, std::string& msg, ProgressCallback progress) {
    WeyUsbSession usb;
    if (progress) progress(0.02, "Opening USB communication...");
    if (!usb.Open(msg)) return false;

    if (progress) progress(0.04, "Entering programming mode...");
    if (!usb.EnterProgrammingMode(msg)) return false;

    uint16_t cur_fw = 0, cur_icsp = 0;
    if (!usb.GetFirmwareVersion(cur_fw, cur_icsp, msg)) return false;

    // Backup existing config if not applying default settings
    uint8_t backup_config[2048];
    std::memset(backup_config, 0xFF, sizeof(backup_config));
    if (!apply_default_settings) {
        if (progress) progress(0.06, "Backing up existing configuration...");
        if (!usb.SelectChip(0, msg)) return false;
        if (!usb.PIC32_ReadDataFlash1024(0x7E000, &backup_config[0], msg)) return false;
        if (!usb.PIC32_ReadDataFlash1024(0x7E400, &backup_config[1024], msg)) return false;

        uint16_t ver = static_cast<uint16_t>(backup_config[0] | (backup_config[1] << 8));
        if (ver == 0xFFFF || ver == 0x0000) {
            T_Config11 def_cfg;
            DefaultValues(def_cfg);
            Config11_to_PIC11(def_cfg, backup_config);
        } else if (ver == 0x000A) {
            T_Config11 upg_cfg;
            PIC10_to_Config11(backup_config, upg_cfg);
            Config11_to_PIC11(upg_cfg, backup_config);
        }
    } else {
        T_Config11 def_cfg;
        DefaultValues(def_cfg);
        Config11_to_PIC11(def_cfg, backup_config);
    }

    if (!usb.SaveFirmwareVersion(0xFFFF, msg)) return false;

    // 1. Program MAIN (PIC32MX675F512H, Chip 0)
    if (progress) progress(0.08, "Erasing MAIN controller flash...");
    if (!usb.SelectChip(0, msg)) return false;
    if (!usb.PIC32_BulkErase(msg)) return false;

    // Write 12 x 1KB BootFlash blocks
    for (uint32_t b = 0; b < 12; ++b) {
        if (progress) {
            progress(0.10 + (0.05 * b / 12.0), "Programming MAIN Boot Flash (" + std::to_string(b + 1) + "/12)...");
        }
        if (!usb.PIC32_WriteBootFlash1024(b * 1024, &pkg.main_chip.boot_flash[b * 1024], msg)) return false;
    }

    // Prepare DataFlash copy (with default config if apply_default_settings is true, else 0xFF at 0x7E000)
    std::vector<uint8_t> main_data(pkg.main_chip.data_flash, pkg.main_chip.data_flash + 0x80000);
    if (apply_default_settings) {
        std::memcpy(&main_data[0x7E000], backup_config, 2048);
    } else {
        std::memset(&main_data[0x7E000], 0xFF, 2048);
    }

    // Write 512 x 1KB DataFlash blocks
    for (uint32_t b = 0; b < 512; ++b) {
        if (progress && (b % 16 == 0)) {
            progress(0.15 + (0.45 * b / 512.0), "Programming MAIN Data Flash (" + std::to_string(b + 1) + "/512)...");
        }
        if (!usb.PIC32_WriteDataFlash1024(b * 1024, &main_data[b * 1024], msg)) return false;
    }

    // Verify MAIN DataFlash & BootFlash CRCs
    if (progress) progress(0.62, "Verifying MAIN Flash CRC...");
    uint16_t dev_data_crc = 0, dev_boot_crc = 0;
    if (!usb.PIC32_GetCRC(0x1D000000u, 0x7FFFFu, dev_data_crc, msg)) return false;
    uint16_t exp_data_crc = CalcCRC16_CCITT(main_data.data(), 0x80000);
    if (dev_data_crc != exp_data_crc) {
        msg = "Error verifying MAIN program flash CRC!";
        return false;
    }

    if (!usb.PIC32_GetCRC(0x1FC00000u, 0x2FFFu, dev_boot_crc, msg)) return false;
    uint16_t exp_boot_crc = CalcCRC16_CCITT(pkg.main_chip.boot_flash, 0x3000);
    if (dev_boot_crc != exp_boot_crc) {
        msg = "Error verifying MAIN boot flash CRC!";
        return false;
    }

    // 2. Program USB Devices 1..4 (PIC18F4550)
    for (int d = 0; d < 4; ++d) {
        uint8_t chip_id = static_cast<uint8_t>(d + 1);
        double base_p = 0.65 + d * 0.07;
        if (progress) progress(base_p, "Programming USB Device " + std::to_string(chip_id) + "...");
        if (!usb.SelectChip(chip_id, msg)) return false;
        if (!usb.PIC18_BulkErase(msg)) return false;

        for (uint32_t b = 0; b < 32; ++b) {
            if (!usb.PIC18_WriteDataFlash1024(b * 1024, &pkg.devices[d].data_flash[b * 1024], msg)) return false;
        }
        if (!usb.PIC18_WriteConfigFlash14(pkg.devices[d].config_flash, msg)) return false;
    }

    // 3. Restore backed up config if !apply_default_settings
    if (!apply_default_settings) {
        if (progress) progress(0.94, "Restoring user configuration...");
        if (!usb.SelectChip(0, msg)) return false;
        if (!usb.PIC32_PageErase4096(0x7E000, msg)) return false;
        if (!usb.PIC32_WriteDataFlash1024(0x7E000, &backup_config[0], msg)) return false;
        if (!usb.PIC32_WriteDataFlash1024(0x7E400, &backup_config[1024], msg)) return false;
    }

    // 4. Save Firmware Version = 17 (V1.7)
    if (progress) progress(0.98, "Finalizing firmware version V1.7...");
    if (!usb.SaveFirmwareVersion(17, msg)) return false;

    if (!usb.LeaveProgrammingMode(msg)) return false;
    usb.Close();

    if (progress) progress(1.0, "Firmware update complete.");
    msg = "Firmware update to V1.7 completed successfully!";
    return true;
}
