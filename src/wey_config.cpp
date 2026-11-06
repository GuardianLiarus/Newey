#include "wey_config.h"
#include "wey_resources.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

static const uint8_t BIT_TABLE[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

std::string GetShortString(const uint8_t s[51]) {
    uint8_t len = s[0];
    if (len > 50) len = 50;
    std::string res;
    res.reserve(len);
    for (uint8_t i = 1; i <= len; ++i) {
        if (s[i] == 0 || s[i] == 0xFF) break;
        res.push_back(static_cast<char>(s[i]));
    }
    return res;
}

void SetShortString(uint8_t s[51], const std::string& val) {
    std::memset(s, 0, 51);
    size_t len = std::min<size_t>(val.size(), 50);
    s[0] = static_cast<uint8_t>(len);
    if (len > 0) {
        std::memcpy(&s[1], val.data(), len);
    }
}

std::string GetKeyName(uint8_t hid_code) {
    if (hid_code < 232) {
        return KEY_TABLE[hid_code];
    }
    return "none";
}

// Exact port of Unit_Settings_Config.DefaultValues (0x4d7728)
void DefaultValues(T_Config11& cfg) {
    std::memset(&cfg, 0, sizeof(T_Config11));
    cfg.Version = 11; // 0x000B

    SetShortString(cfg.Configuration, "");
    SetShortString(cfg.Client, "");
    SetShortString(cfg.Project, "");

    cfg.Start_Source = 1;

    for (int i = 0; i < 4; ++i) {
        cfg.MouseJiggle[i].Enable = 0;
        cfg.MouseJiggle[i].Pad = 0;
        cfg.MouseJiggle[i].Interval = 240;
    }

    for (int i = 0; i < 4; ++i) {
        cfg.Event[i].Active = 1;
        cfg.Event[i].Key1   = 0xE7; // Right GUI (Win)
        cfg.Event[i].Key2   = static_cast<uint8_t>(0x3A + i); // F1..F4
        cfg.Event[i].Key3   = 0x00; // none
        cfg.Event[i].Source = static_cast<uint8_t>(1 + i); // Source 1..4
    }
    for (int i = 4; i < 16; ++i) {
        cfg.Event[i].Active = 0;
        cfg.Event[i].Key1   = 0x00;
        cfg.Event[i].Key2   = 0x00;
        cfg.Event[i].Key3   = 0x00;
        cfg.Event[i].Source = 0xFF; // '-' (No Change)
    }

    cfg.MouseSwitch_Version = 0; // Mouseswitch disabled by default
    cfg.Switch_Over = 0x03;      // Horizontal (bit 0) + Vertical (bit 1)

    for (int i = 0; i < 4; ++i) {
        cfg.MousePointer_Leave[i] = 0x55;     // 0b01010101 -> all 4 monitors "move to 0/0" (1)
        cfg.MousePointer_Leave_Man[i] = 0x55; // 0b01010101 -> all 4 monitors "move to 0/0" (1)
    }
    for (int i = 0; i < 2; ++i) {
        cfg.MousePointer_Enter[i] = 0xFF;     // all 16 monitors "latest position" (1)
        cfg.MousePointer_Enter_Man[i] = 0xFF; // all 16 monitors "latest position" (1)
    }
    cfg.MousePointer_Dummy = 0x00;

    for (int i = 0; i < 128; ++i) {
        cfg.Monitor[i].Source = 0;
        cfg.Monitor[i].Pad    = 0;
        cfg.Monitor[i].Width  = 0;
        cfg.Monitor[i].Height = 0;
    }
}

// Exact port of LoadFileV10 (0x4d7b04) & LoadFileV11 (0x4d7d14)
bool LoadConfigFile(const std::string& path, T_Config11& cfg, std::string& err_msg) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        err_msg = "Cannot open file: " + path;
        return false;
    }
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    if (sz < 2) {
        err_msg = "File is too small to be a valid .Wey22411 configuration.";
        return false;
    }

    uint16_t ver = 0;
    f.read(reinterpret_cast<char*>(&ver), 2);
    f.seekg(0, std::ios::beg);

    if (ver == 11) {
        if (sz < static_cast<std::streamsize>(sizeof(T_Config11))) {
            err_msg = "Invalid V1.1 .Wey22411 file size.";
            return false;
        }
        DefaultValues(cfg);
        f.read(reinterpret_cast<char*>(&cfg), sizeof(T_Config11));
        return true;
    } else if (ver == 10) {
        if (sz < static_cast<std::streamsize>(sizeof(T_Config10))) {
            err_msg = "Invalid V1.0 .Wey22411 file size.";
            return false;
        }
        T_Config10 cfg10{};
        f.read(reinterpret_cast<char*>(&cfg10), sizeof(T_Config10));
        DefaultValues(cfg);
        cfg.Version = 11;
        std::memcpy(cfg.Configuration, cfg10.Configuration, 51);
        std::memcpy(cfg.Client, cfg10.Client, 51);
        std::memcpy(cfg.Project, cfg10.Project, 51);
        cfg.Start_Source = cfg10.Start_Source;
        std::memcpy(cfg.Event, cfg10.Event, sizeof(cfg.Event));
        cfg.MouseSwitch_Version = cfg10.MouseSwitch_Version;
        cfg.Switch_Over = cfg10.Switch_Over;
        std::memcpy(cfg.MousePointer_Leave, cfg10.MousePointer_Leave, 4);
        std::memcpy(cfg.MousePointer_Enter, cfg10.MousePointer_Enter, 2);
        std::memcpy(cfg.MousePointer_Leave_Man, cfg10.MousePointer_Leave_Man, 4);
        std::memcpy(cfg.MousePointer_Enter_Man, cfg10.MousePointer_Enter_Man, 2);
        cfg.MousePointer_Dummy = cfg10.MousePointer_Dummy;
        std::memcpy(cfg.Monitor, cfg10.Monitor, sizeof(cfg.Monitor));
        return true;
    } else {
        err_msg = "Unsupported configuration file version (" + std::to_string(ver) + "). Expected 10 or 11.";
        return false;
    }
}

// Exact port of SaveFileV11 (0x4d7e10)
bool SaveConfigFile(const std::string& path, const T_Config11& cfg, std::string& err_msg) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        err_msg = "Cannot write file: " + path;
        return false;
    }
    T_Config11 out = cfg;
    out.Version = 11;
    f.write(reinterpret_cast<const char*>(&out), sizeof(T_Config11));
    if (!f.good()) {
        err_msg = "Error writing configuration data to " + path;
        return false;
    }
    return true;
}

static void WritePaddedStringToPIC(const uint8_t short_str[51], uint8_t* pic_dst) {
    std::memset(pic_dst, 0xFF, 51);
    std::string s = GetShortString(short_str);
    size_t len = std::min<size_t>(s.size(), 50);
    for (size_t i = 0; i < len; ++i) {
        pic_dst[i] = static_cast<uint8_t>(s[i]);
    }
}

static void ReadPaddedStringFromPIC(const uint8_t* pic_src, uint8_t short_str[51]) {
    std::string s;
    for (int i = 0; i < 50; ++i) {
        uint8_t c = pic_src[i];
        if (c == 0xFF || c == 0x00) break;
        s.push_back(static_cast<char>(c));
    }
    SetShortString(short_str, s);
}

// Exact port of Config11_to_PIC11 (0x4de638)
// Populates 2048 bytes corresponding to PIC32 DataFlash[0x7e000..0x7e7ff]
void Config11_to_PIC11(const T_Config11& cfg, uint8_t flash[2048]) {
    std::memset(flash, 0xFF, 2048);

    // +0x000 (0x7e000): Version = 11 (0x000B)
    flash[0x000] = 0x0B;
    flash[0x001] = 0x00;

    // +0x002..0x09A: Configuration, Client, Project (51B each, 0xFF padded)
    WritePaddedStringToPIC(cfg.Configuration, &flash[0x002]);
    WritePaddedStringToPIC(cfg.Client,        &flash[0x035]);
    WritePaddedStringToPIC(cfg.Project,       &flash[0x068]);

    // +0x09B (0x7e09b): Start_Source
    flash[0x09B] = cfg.Start_Source;

    // +0x09C..0x0A7 (0x7e09c..0x7e0a7): MouseJiggle[0..3] (4 * 3 bytes = 12B)
    for (int i = 0; i < 4; ++i) {
        int off = 0x09C + i * 3;
        flash[off + 0] = cfg.MouseJiggle[i].Enable;
        flash[off + 1] = static_cast<uint8_t>(cfg.MouseJiggle[i].Interval & 0xFF);
        flash[off + 2] = static_cast<uint8_t>((cfg.MouseJiggle[i].Interval >> 8) & 0xFF);
    }

    // +0x0A8..0x0F7 (0x7e0a8..0x7e0f7): Event[0..15] (16 * 5 bytes = 80B)
    for (int i = 0; i < 16; ++i) {
        int off = 0x0A8 + i * 5;
        flash[off + 0] = cfg.Event[i].Active;
        flash[off + 1] = cfg.Event[i].Key1;
        flash[off + 2] = cfg.Event[i].Key2;
        flash[off + 3] = cfg.Event[i].Key3;
        flash[off + 4] = cfg.Event[i].Source;
    }

    // +0x0F8..0x0F9 (0x7e0f8): MouseSwitch_Version
    flash[0x0F8] = static_cast<uint8_t>(cfg.MouseSwitch_Version & 0xFF);
    flash[0x0F9] = static_cast<uint8_t>((cfg.MouseSwitch_Version >> 8) & 0xFF);

    // +0x0FA (0x7e0fa): Switch_Over
    flash[0x0FA] = cfg.Switch_Over;

    // +0x0FB..0x0FE (0x7e0fb): MousePointer_Leave[0..3]
    for (int i = 0; i < 4; ++i) flash[0x0FB + i] = cfg.MousePointer_Leave[i];

    // +0x0FF..0x100 (0x7e0ff): MousePointer_Enter[0..1]
    for (int i = 0; i < 2; ++i) flash[0x0FF + i] = cfg.MousePointer_Enter[i];

    // +0x101..0x104 (0x7e101): MousePointer_Leave_Man[0..3]
    for (int i = 0; i < 4; ++i) flash[0x101 + i] = cfg.MousePointer_Leave_Man[i];

    // +0x105..0x106 (0x7e105): MousePointer_Enter_Man[0..1]
    for (int i = 0; i < 2; ++i) flash[0x105 + i] = cfg.MousePointer_Enter_Man[i];

    // +0x107 (0x7e107): MousePointer_Dummy
    flash[0x107] = cfg.MousePointer_Dummy;

    // +0x108..0x407 (0x7e108..0x7e407): Monitor[0..127] (128 * 6 bytes = 768B)
    for (int i = 0; i < 128; ++i) {
        int off = 0x108 + i * 6;
        flash[off + 0] = cfg.Monitor[i].Source;
        flash[off + 1] = 0x00;
        flash[off + 2] = static_cast<uint8_t>(cfg.Monitor[i].Width & 0xFF);
        flash[off + 3] = static_cast<uint8_t>((cfg.Monitor[i].Width >> 8) & 0xFF);
        flash[off + 4] = static_cast<uint8_t>(cfg.Monitor[i].Height & 0xFF);
        flash[off + 5] = static_cast<uint8_t>((cfg.Monitor[i].Height >> 8) & 0xFF);
    }
}

// Exact port of PIC11_to_Config11 (0x4de250)
void PIC11_to_Config11(const uint8_t flash[2048], T_Config11& cfg) {
    DefaultValues(cfg);
    cfg.Version = 11;

    ReadPaddedStringFromPIC(&flash[0x002], cfg.Configuration);
    ReadPaddedStringFromPIC(&flash[0x035], cfg.Client);
    ReadPaddedStringFromPIC(&flash[0x068], cfg.Project);

    cfg.Start_Source = flash[0x09B];

    for (int i = 0; i < 4; ++i) {
        int off = 0x09C + i * 3;
        cfg.MouseJiggle[i].Enable   = flash[off + 0];
        cfg.MouseJiggle[i].Pad      = 0;
        cfg.MouseJiggle[i].Interval = static_cast<uint16_t>(flash[off + 1] | (flash[off + 2] << 8));
        if (cfg.MouseJiggle[i].Interval < 5) cfg.MouseJiggle[i].Interval = 240;
    }

    for (int i = 0; i < 16; ++i) {
        int off = 0x0A8 + i * 5;
        cfg.Event[i].Active = flash[off + 0];
        cfg.Event[i].Key1   = flash[off + 1];
        cfg.Event[i].Key2   = flash[off + 2];
        cfg.Event[i].Key3   = flash[off + 3];
        cfg.Event[i].Source = flash[off + 4];
    }

    cfg.MouseSwitch_Version = static_cast<uint16_t>(flash[0x0F8] | (flash[0x0F9] << 8));
    cfg.Switch_Over         = flash[0x0FA];

    for (int i = 0; i < 4; ++i) cfg.MousePointer_Leave[i]     = flash[0x0FB + i];
    for (int i = 0; i < 2; ++i) cfg.MousePointer_Enter[i]     = flash[0x0FF + i];
    for (int i = 0; i < 4; ++i) cfg.MousePointer_Leave_Man[i] = flash[0x101 + i];
    for (int i = 0; i < 2; ++i) cfg.MousePointer_Enter_Man[i] = flash[0x105 + i];
    cfg.MousePointer_Dummy = flash[0x107];

    for (int i = 0; i < 128; ++i) {
        int off = 0x108 + i * 6;
        cfg.Monitor[i].Source = flash[off + 0];
        cfg.Monitor[i].Pad    = 0;
        cfg.Monitor[i].Width  = static_cast<uint16_t>(flash[off + 2] | (flash[off + 3] << 8));
        cfg.Monitor[i].Height = static_cast<uint16_t>(flash[off + 4] | (flash[off + 5] << 8));
    }
}

// Exact port of PIC10_to_Config11 (0x4dddf0)
void PIC10_to_Config11(const uint8_t flash[2048], T_Config11& cfg) {
    DefaultValues(cfg);
    cfg.Version = 11;

    ReadPaddedStringFromPIC(&flash[0x002], cfg.Configuration);
    ReadPaddedStringFromPIC(&flash[0x035], cfg.Client);
    ReadPaddedStringFromPIC(&flash[0x068], cfg.Project);

    cfg.Start_Source = flash[0x09B];

    // V1.0 PIC layout has no 12B MouseJiggle; Event starts at 0x09C
    for (int i = 0; i < 16; ++i) {
        int off = 0x09C + i * 5;
        cfg.Event[i].Active = flash[off + 0];
        cfg.Event[i].Key1   = flash[off + 1];
        cfg.Event[i].Key2   = flash[off + 2];
        cfg.Event[i].Key3   = flash[off + 3];
        cfg.Event[i].Source = flash[off + 4];
    }

    cfg.MouseSwitch_Version = static_cast<uint16_t>(flash[0x0EC] | (flash[0x0ED] << 8));
    cfg.Switch_Over         = flash[0x0EE];

    for (int i = 0; i < 4; ++i) cfg.MousePointer_Leave[i]     = flash[0x0EF + i];
    for (int i = 0; i < 2; ++i) cfg.MousePointer_Enter[i]     = flash[0x0F3 + i];
    for (int i = 0; i < 4; ++i) cfg.MousePointer_Leave_Man[i] = flash[0x0F5 + i];
    for (int i = 0; i < 2; ++i) cfg.MousePointer_Enter_Man[i] = flash[0x0F9 + i];
    cfg.MousePointer_Dummy = flash[0x0FB];

    for (int i = 0; i < 128; ++i) {
        int off = 0x0FC + i * 6;
        cfg.Monitor[i].Source = flash[off + 0];
        cfg.Monitor[i].Pad    = 0;
        cfg.Monitor[i].Width  = static_cast<uint16_t>(flash[off + 2] | (flash[off + 3] << 8));
        cfg.Monitor[i].Height = static_cast<uint16_t>(flash[off + 4] | (flash[off + 5] << 8));
    }
}

// Exact port of Unit_Settings_Config_Monitor.CheckMonitorPosition (0x4d17ec)
bool CheckMonitorPosition(const T_Config11& cfg) {
    bool ws_present[4] = {false, false, false, false};

    for (int i = 0; i < 128; ++i) {
        uint8_t src = cfg.Monitor[i].Source & 0x07; // 1..4 (strips +8 hatch bit)
        if (src == 0) continue;
        if (src >= 1 && src <= 4) {
            ws_present[src - 1] = true;
        }

        bool has_neighbor = false;
        int col = i % 16;
        int row = i / 16;

        // Left neighbor
        if (col > 0) {
            uint8_t nsrc = cfg.Monitor[i - 1].Source & 0x07;
            if (nsrc != 0 && nsrc != src) has_neighbor = true;
        }
        // Right neighbor
        if (col < 15) {
            uint8_t nsrc = cfg.Monitor[i + 1].Source & 0x07;
            if (nsrc != 0 && nsrc != src) has_neighbor = true;
        }
        // Up neighbor
        if (row > 0) {
            uint8_t nsrc = cfg.Monitor[i - 16].Source & 0x07;
            if (nsrc != 0 && nsrc != src) has_neighbor = true;
        }
        // Down neighbor
        if (row < 7) {
            uint8_t nsrc = cfg.Monitor[i + 16].Source & 0x07;
            if (nsrc != 0 && nsrc != src) has_neighbor = true;
        }

        if (!has_neighbor) {
            return false;
        }
    }

    int ws_count = 0;
    for (int w = 0; w < 4; ++w) {
        if (ws_present[w]) ws_count++;
    }
    return ws_count >= 2;
}

uint8_t GetMouseLeave(const uint8_t arr[4], int ws, int mon) {
    if (ws < 0 || ws > 3 || mon < 0 || mon > 3) return 0;
    return (arr[ws] >> (mon * 2)) & 0x03;
}

void SetMouseLeave(uint8_t arr[4], int ws, int mon, uint8_t val) {
    if (ws < 0 || ws > 3 || mon < 0 || mon > 3) return;
    int shift = mon * 2;
    arr[ws] = static_cast<uint8_t>((arr[ws] & ~(0x03 << shift)) | ((val & 0x03) << shift));
}

uint8_t GetMouseEnter(const uint8_t arr[2], int ws, int mon) {
    if (ws < 0 || ws > 3 || mon < 0 || mon > 3) return 0;
    int bit_idx = (ws % 2) * 4 + mon;
    int byte_idx = ws / 2;
    return (arr[byte_idx] & BIT_TABLE[bit_idx]) ? 1 : 0;
}

void SetMouseEnter(uint8_t arr[2], int ws, int mon, uint8_t val) {
    if (ws < 0 || ws > 3 || mon < 0 || mon > 3) return;
    int bit_idx = (ws % 2) * 4 + mon;
    int byte_idx = ws / 2;
    if (val) {
        arr[byte_idx] |= BIT_TABLE[bit_idx];
    } else {
        arr[byte_idx] &= ~BIT_TABLE[bit_idx];
    }
}

uint16_t CalcCRC16_CCITT(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = static_cast<uint16_t>((crc << 8) | ((crc >> 8) & 0xFF));
        crc ^= data[i];
        crc ^= static_cast<uint16_t>((crc & 0xFF) >> 4);
        crc ^= static_cast<uint16_t>(crc << 12);
        crc ^= static_cast<uint16_t>((crc & 0xFF) << 5);
    }
    return crc;
}

static void InitPIC18Image(PIC18Image& img) {
    img.version = 0;
    std::memset(img.data_flash, 0xFF, sizeof(img.data_flash));
    std::memset(img.id_flash, 0xFF, sizeof(img.id_flash));
    std::memset(img.config_flash, 0x00, sizeof(img.config_flash));
}

static void InitPIC32Image(PIC32Image& img) {
    img.version = 0;
    std::memset(img.data_flash, 0xFF, sizeof(img.data_flash));
    std::memset(img.boot_flash, 0xFF, sizeof(img.boot_flash));
    std::memset(img.config_flash, 0x00, sizeof(img.config_flash));
}

// Exact port of Unit_Package.ReadPakage (0x4df3f8)
bool LoadFirmwarePackage(const std::string& path, FirmwarePackage& pkg, std::string& err_msg) {
    std::ifstream f(path);
    if (!f.is_open()) {
        err_msg = "Cannot open firmware package: " + path;
        return false;
    }

    InitPIC18Image(pkg.icsp);
    InitPIC32Image(pkg.main_chip);
    for (int i = 0; i < 4; ++i) InitPIC18Image(pkg.devices[i]);

    auto read_line = [&](std::string& line) -> bool {
        if (!std::getline(f, line)) return false;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
            line.pop_back();
        }
        return true;
    };

    std::string line;
    if (!read_line(line) || line != "Wey Technology AG") {
        err_msg = "Invalid firmware header (expected 'Wey Technology AG').";
        return false;
    }
    if (!read_line(line) || line != "USB Deskswitch II 1to4 22411") {
        err_msg = "Invalid product header (expected 'USB Deskswitch II 1to4 22411').";
        return false;
    }

    const char* expected_sections[6] = {
        "[PIC18F4550_ICSP]",
        "[PIC32MX675F512H_MAIN]",
        "[PIC18F4550_DEVICE1]",
        "[PIC18F4550_DEVICE2]",
        "[PIC18F4550_DEVICE3]",
        "[PIC18F4550_DEVICE4]"
    };

    for (int sec = 0; sec < 6; ++sec) {
        while (read_line(line)) {
            if (!line.empty() && line[0] != ';') break;
        }
        if (line != expected_sections[sec]) {
            err_msg = std::string("Expected section ") + expected_sections[sec] + ", got: " + line;
            return false;
        }
        if (!read_line(line)) {
            err_msg = "Unexpected EOF reading version for " + std::string(expected_sections[sec]);
            return false;
        }
        uint16_t ver = static_cast<uint16_t>(std::stoi(line));
        if (sec == 0) pkg.icsp.version = ver;
        else if (sec == 1) pkg.main_chip.version = ver;
        else pkg.devices[sec - 2].version = ver;

        uint32_t upper_addr = 0;
        while (read_line(line)) {
            if (line == ":00000001FF") break;
            if (line.empty() || line[0] == ';') continue;
            if (line[0] != ':' || line.size() < 11) {
                err_msg = "Malformed Intel HEX line in " + std::string(expected_sections[sec]);
                return false;
            }
            uint8_t byte_count = static_cast<uint8_t>(std::stoul(line.substr(1, 2), nullptr, 16));
            uint16_t addr16    = static_cast<uint16_t>(std::stoul(line.substr(3, 4), nullptr, 16));
            uint8_t rec_type   = static_cast<uint8_t>(std::stoul(line.substr(7, 2), nullptr, 16));

            if (rec_type == 0x04) {
                upper_addr = static_cast<uint32_t>(std::stoul(line.substr(9, 4), nullptr, 16)) << 16;
            } else if (rec_type == 0x00) {
                uint32_t full_addr = upper_addr + addr16;
                for (uint8_t b = 0; b < byte_count; ++b) {
                    uint8_t val = static_cast<uint8_t>(std::stoul(line.substr(9 + b * 2, 2), nullptr, 16));
                    uint32_t cur = full_addr + b;
                    if (sec == 1) {
                        // PIC32MX675F512H
                        if (cur >= 0x1D000000 && cur <= 0x1D07FFFF) {
                            pkg.main_chip.data_flash[cur - 0x1D000000] = val;
                        } else if (cur >= 0x1FC00000 && cur <= 0x1FC02FFF) {
                            pkg.main_chip.boot_flash[cur - 0x1FC00000] = val;
                            if (cur >= 0x1FC02FF0 && cur <= 0x1FC02FFF) {
                                pkg.main_chip.config_flash[cur - 0x1FC02FF0] = val;
                            }
                        }
                    } else {
                        // PIC18F4550
                        PIC18Image& img = (sec == 0) ? pkg.icsp : pkg.devices[sec - 2];
                        if (cur < 0x8000) {
                            img.data_flash[cur] = val;
                        } else if (cur >= 0x200000 && cur <= 0x200007) {
                            img.id_flash[cur - 0x200000] = val;
                        } else if (cur >= 0x300000 && cur <= 0x30000D) {
                            img.config_flash[cur - 0x300000] = val;
                        }
                    }
                }
            }
        }
    }
    return true;
}
