#pragma once

#include "wey_config.h"
#include <cstdint>
#include <string>
#include <functional>

struct ChipVersionInfo {
    uint8_t  chip_index = 0;
    uint16_t fw_version = 0;   // e.g. 17 -> V1.7
    uint16_t icsp_version = 0; // e.g. 14 -> V1.4
    std::string name;          // MAIN, USB 1, USB 2, USB 3, USB 4
};

struct SwitchDeviceInfo {
    uint16_t product_id = 0;
    uint8_t  bus_number = 0;
    uint8_t  device_address = 0;
    ChipVersionInfo chips[5];
};

enum class UsbPresenceStatus {
    NotConnected,
    ConnectedNoPermission,
    ConnectedReady
};

struct UsbStatusInfo {
    UsbPresenceStatus status = UsbPresenceStatus::NotConnected;
    uint8_t bus = 0;
    uint8_t addr = 0;
    std::string dev_path;
};

using ProgressCallback = std::function<void(double fraction, const std::string& stage)>;

// Non-intrusive check for 1b07:0010 presence and R/W permission
UsbStatusInfo CheckUsbDeviceStatus();

// Uses pkexec to install /etc/udev/rules.d/99-wey-deskswitch.rules and chmod 0666 the device node
bool InstallUdevRuleAndFixPermissions(std::string& err_msg);

// High-level operations (NOTE: Download = Switch -> PC; Upload = PC -> Switch)
bool DownloadConfigFromSwitch(T_Config11& out_cfg, std::string& msg, ProgressCallback progress = nullptr);
bool UploadConfigToSwitch(const T_Config11& in_cfg, std::string& msg, ProgressCallback progress = nullptr);
bool ResetSwitchToDefaultSettings(T_Config11& out_cfg, std::string& msg, ProgressCallback progress = nullptr);
bool ReadSwitchDeviceInfo(SwitchDeviceInfo& out_info, std::string& msg, ProgressCallback progress = nullptr);
bool UpdateSwitchFirmware(const FirmwarePackage& pkg, bool apply_default_settings, std::string& msg, ProgressCallback progress = nullptr);
