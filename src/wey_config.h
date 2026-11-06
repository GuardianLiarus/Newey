#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#pragma pack(push, 1)

struct T_MouseJiggle {
    uint8_t  Enable;    // 0 or 1
    uint8_t  Pad;       // alignment byte in Delphi record (0)
    uint16_t Interval;  // 5..65535 [s], default 240
};

struct T_Event {
    uint8_t Active;     // 0 or 1
    uint8_t Key1;       // USB HID keycode (0..0xE7)
    uint8_t Key2;       // USB HID keycode (0..0xE7)
    uint8_t Key3;       // USB HID keycode (0..0xE7)
    uint8_t Source;     // 0xFF = '-', 0 = '0' (Switch Off), 1..4 = '1'..'4'
};

struct T_Monitor {
    uint8_t  Source;    // 0 = empty, 1..4 = WS1..WS4 solid, 9..12 = WS1..WS4 hatched (Source + 8)
    uint8_t  Pad;       // alignment byte in Delphi record (0)
    uint16_t Width;     // e.g. 1920
    uint16_t Height;    // e.g. 1080
};

// Exact binary layout of V1.1 .Wey22411 file (1036 bytes = 0x40C)
struct T_Config11 {
    uint16_t      Version;                // +0x000 (2B): 11 (0x000B)
    uint8_t       Configuration[51];      // +0x002 (51B): Delphi ShortString[50] (len + 50 chars)
    uint8_t       Client[51];             // +0x035 (51B): Delphi ShortString[50]
    uint8_t       Project[51];            // +0x068 (51B): Delphi ShortString[50]
    uint8_t       Start_Source;           // +0x09B (1B): 0 = none, 1..4 = Source 1..4
    T_MouseJiggle MouseJiggle[4];         // +0x09C (16B): Sources 1..4
    T_Event       Event[16];              // +0x0AC (80B): 16 hotkey events
    uint16_t      MouseSwitch_Version;    // +0x0FC (2B): 10 if enabled, 0 if disabled
    uint8_t       Switch_Over;            // +0x0FE (1B): bit 0 = Horizontal, bit 1 = Vertical
    uint8_t       MousePointer_Leave[4];  // +0x0FF (4B): 2 bits per monitor (0=not move, 1=0/0, 2=M/M)
    uint8_t       MousePointer_Enter[2];  // +0x103 (2B): 1 bit per monitor (0=no change, 1=latest pos)
    uint8_t       MousePointer_Leave_Man[4]; // +0x105 (4B): manual switch leave
    uint8_t       MousePointer_Enter_Man[2]; // +0x109 (2B): manual switch enter
    uint8_t       MousePointer_Dummy;     // +0x10B (1B): 0
    T_Monitor     Monitor[128];           // +0x10C (768B): 8 rows x 16 cols grid
};

// Exact binary layout of V1.0 .Wey22411 file (1020 bytes = 0x3FC, no MouseJiggle)
struct T_Config10 {
    uint16_t  Version;                    // +0x000 (2B): 10 (0x000A)
    uint8_t   Configuration[51];          // +0x002 (51B)
    uint8_t   Client[51];                 // +0x035 (51B)
    uint8_t   Project[51];                // +0x068 (51B)
    uint8_t   Start_Source;               // +0x09B (1B)
    T_Event   Event[16];                  // +0x09C (80B)
    uint16_t  MouseSwitch_Version;        // +0x0EC (2B)
    uint8_t   Switch_Over;                // +0x0EE (1B)
    uint8_t   MousePointer_Leave[4];      // +0x0EF (4B)
    uint8_t   MousePointer_Enter[2];      // +0x0F3 (2B)
    uint8_t   MousePointer_Leave_Man[4];  // +0x0F5 (4B)
    uint8_t   MousePointer_Enter_Man[2];  // +0x0F9 (2B)
    uint8_t   MousePointer_Dummy;         // +0x0FB (1B)
    T_Monitor Monitor[128];               // +0x0FC (768B)
};

#pragma pack(pop)

static_assert(sizeof(T_Config11) == 0x40C, "T_Config11 must be exactly 1036 bytes");
static_assert(sizeof(T_Config10) == 0x3FC, "T_Config10 must be exactly 1020 bytes");

// ShortString helpers
std::string GetShortString(const uint8_t s[51]);
void SetShortString(uint8_t s[51], const std::string& val);

// Config initialization and file I/O
void DefaultValues(T_Config11& cfg);
bool LoadConfigFile(const std::string& path, T_Config11& cfg, std::string& err_msg);
bool SaveConfigFile(const std::string& path, const T_Config11& cfg, std::string& err_msg);

// PIC32 DataFlash (0x7e000..0x7e7ff = 2048 bytes) serialization
void Config11_to_PIC11(const T_Config11& cfg, uint8_t flash_7e000[2048]);
void PIC11_to_Config11(const uint8_t flash_7e000[2048], T_Config11& cfg);
void PIC10_to_Config11(const uint8_t flash_7e000[2048], T_Config11& cfg);

// Monitor grid validation (exact port of Unit_Settings_Config_Monitor.CheckMonitorPosition)
bool CheckMonitorPosition(const T_Config11& cfg);

// Mouse Leave/Enter bitfield helpers (ws = 0..3 for WS1..WS4, mon = 0..3 for Monitor 1..4)
uint8_t GetMouseLeave(const uint8_t arr[4], int ws, int mon);
void SetMouseLeave(uint8_t arr[4], int ws, int mon, uint8_t val);
uint8_t GetMouseEnter(const uint8_t arr[2], int ws, int mon);
void SetMouseEnter(uint8_t arr[2], int ws, int mon, uint8_t val);

// Key name helper
std::string GetKeyName(uint8_t hid_code);

// Firmware package (FRW22411_V1.7.hex) structures
struct PIC18Image {
    uint16_t version = 0;
    uint8_t  data_flash[0x8000];   // 32 KB
    uint8_t  id_flash[8];          // 0x200000..0x200007
    uint8_t  config_flash[14];     // 0x300000..0x30000D
};

struct PIC32Image {
    uint16_t version = 0;
    uint8_t  data_flash[0x80000];  // 512 KB (0x1D000000..0x1D07FFFF)
    uint8_t  boot_flash[0x3000];   // 12 KB  (0x1FC00000..0x1FC02FFF)
    uint8_t  config_flash[16];     // 16 B   (0x1FC02FF0..0x1FC02FFF)
};

struct FirmwarePackage {
    PIC18Image icsp;
    PIC32Image main_chip;
    PIC18Image devices[4];
};

bool LoadFirmwarePackage(const std::string& path, FirmwarePackage& pkg, std::string& err_msg);
uint16_t CalcCRC16_CCITT(const uint8_t* data, size_t len);
