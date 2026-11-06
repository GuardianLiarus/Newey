#include "wey_config.h"
#include "wey_usb.h"
#include "wey_resources.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <cairo.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

// ============================================================================
// Helper: Load embedded WEY_LOGO_PNG as GdkPixbuf
// ============================================================================
static GdkPixbuf* LoadWeyLogoPixbuf() {
    GError* err = nullptr;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_type("png", &err);
    if (!loader) {
        if (err) g_error_free(err);
        return nullptr;
    }
    gdk_pixbuf_loader_write(loader, WEY_LOGO_PNG, sizeof(WEY_LOGO_PNG), &err);
    gdk_pixbuf_loader_close(loader, nullptr);
    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pixbuf) g_object_ref(pixbuf);
    g_object_unref(loader);
    return pixbuf;
}

// Helper: Locate bundled FRW22411_V1.7.hex relative to executable or AppDir
static std::string FindBundledFirmwareHex() {
    std::vector<std::string> candidates;
    const char* appdir = std::getenv("APPDIR");
    if (appdir && *appdir) {
        candidates.push_back(std::string(appdir) + "/usr/share/newey/FRW22411_V1.7.hex");
        candidates.push_back(std::string(appdir) + "/usr/share/wey-deskswitch/FRW22411_V1.7.hex");
        candidates.push_back(std::string(appdir) + "/FRW22411_V1.7.hex");
    }
    char exe_buf[PATH_MAX] = {0};
    ssize_t n = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (n > 0) {
        std::string exe_path(exe_buf, static_cast<size_t>(n));
        size_t slash = exe_path.find_last_of('/');
        if (slash != std::string::npos) {
            std::string dir = exe_path.substr(0, slash);
            candidates.push_back(dir + "/FRW22411_V1.7.hex");
            candidates.push_back(dir + "/Wei/FRW22411_V1.7.hex");
            candidates.push_back(dir + "/../share/newey/FRW22411_V1.7.hex");
            candidates.push_back(dir + "/../share/wey-deskswitch/FRW22411_V1.7.hex");
            candidates.push_back(dir + "/../../Wei/FRW22411_V1.7.hex");
        }
    }
    candidates.push_back("Wei/FRW22411_V1.7.hex");
    candidates.push_back("FRW22411_V1.7.hex");

    for (const auto& c : candidates) {
        struct stat st;
        if (stat(c.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return c;
        }
    }
    return "";
}

// ============================================================================
// Modal Message Helpers
// ============================================================================
static void ShowMessage(GtkWindow* parent, GtkMessageType type, const std::string& title, const std::string& text) {
    GtkWidget* dlg = gtk_message_dialog_new(
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        type,
        GTK_BUTTONS_OK,
        "%s", title.c_str()
    );
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", text.c_str());
    gtk_window_set_title(GTK_WINDOW(dlg), "Newey — USB-Deskswitch II 1:4");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static bool AskConfirm(GtkWindow* parent, const std::string& title, const std::string& text) {
    GtkWidget* dlg = gtk_message_dialog_new(
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s", title.c_str()
    );
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", text.c_str());
    gtk_window_set_title(GTK_WINDOW(dlg), "Newey — USB-Deskswitch II 1:4");
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    return (resp == GTK_RESPONSE_YES);
}

// ============================================================================
// Dialog 1: Mouse Jiggle Dialog (TForm_Settings_Config_MouseJiggle)
// ============================================================================
static bool RunMouseJiggleDialog(GtkWindow* parent, T_Config11& cfg) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Mouse Jiggle Configuration",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK,
        nullptr
    );
    gtk_window_set_default_size(GTK_WINDOW(dlg), 380, 260);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    GtkWidget* chk_enable[4];
    GtkWidget* spin_interval[4];

    for (int i = 0; i < 4; ++i) {
        std::string frame_title = "Source " + std::to_string(i + 1);
        GtkWidget* frame = gtk_frame_new(frame_title.c_str());
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 8);
        gtk_container_add(GTK_CONTAINER(frame), hbox);

        chk_enable[i] = gtk_check_button_new_with_label("Enable");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_enable[i]), cfg.MouseJiggle[i].Enable != 0);
        gtk_box_pack_start(GTK_BOX(hbox), chk_enable[i], FALSE, FALSE, 0);

        GtkWidget* lbl = gtk_label_new("Interval:");
        gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);

        uint16_t init_val = cfg.MouseJiggle[i].Interval;
        if (init_val < 5) init_val = 240;
        spin_interval[i] = gtk_spin_button_new_with_range(5.0, 65535.0, 1.0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_interval[i]), static_cast<double>(init_val));
        gtk_box_pack_start(GTK_BOX(hbox), spin_interval[i], FALSE, FALSE, 0);

        GtkWidget* unit_lbl = gtk_label_new("5..65535 [s]");
        gtk_box_pack_start(GTK_BOX(hbox), unit_lbl, FALSE, FALSE, 0);

        gtk_grid_attach(GTK_GRID(grid), frame, 0, i, 1, 1);
    }

    gtk_widget_show_all(dlg);
    bool accepted = false;
    while (true) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        if (resp == GTK_RESPONSE_OK) {
            for (int i = 0; i < 4; ++i) {
                gtk_spin_button_update(GTK_SPIN_BUTTON(spin_interval[i]));
                int val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_interval[i]));
                if (val < 5 || val > 65535) {
                    ShowMessage(GTK_WINDOW(dlg), GTK_MESSAGE_WARNING, "Invalid Interval",
                                "Interval for Source " + std::to_string(i + 1) + " must be between 5 and 65535 seconds.");
                    continue;
                }
                cfg.MouseJiggle[i].Enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_enable[i])) ? 1 : 0;
                cfg.MouseJiggle[i].Pad = 0;
                cfg.MouseJiggle[i].Interval = static_cast<uint16_t>(val);
            }
            accepted = true;
            break;
        } else {
            break;
        }
    }

    gtk_widget_destroy(dlg);
    return accepted;
}

// ============================================================================
// Dialog 2: Visual USB Keyboard Selector (TForm_USBKeyboard)
// ============================================================================
struct UsbKeyboardDialogState {
    uint8_t keys[3] = {0, 0, 0};
    GtkWidget* drawing_area = nullptr;
    GtkWidget* status_label = nullptr;

    void UpdateLabel() {
        if (!status_label) return;
        std::string txt = "Selected Keys (up to 3):  [1] " + GetKeyName(keys[0]) +
                          "    [2] " + GetKeyName(keys[1]) +
                          "    [3] " + GetKeyName(keys[2]);
        gtk_label_set_text(GTK_LABEL(status_label), txt.c_str());
    }

    bool IsSelected(uint8_t id) const {
        if (id == 0) return false;
        return (keys[0] == id || keys[1] == id || keys[2] == id);
    }

    // Exact logic of TForm_USBKeyboard.KeyClick (0x4d99d4)
    void ToggleKey(uint8_t id) {
        if (id == 0) return;
        for (int i = 0; i < 3; ++i) {
            if (keys[i] == id) {
                keys[i] = 0;
                UpdateLabel();
                if (drawing_area) gtk_widget_queue_draw(drawing_area);
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (keys[i] == 0) {
                keys[i] = id;
                UpdateLabel();
                if (drawing_area) gtk_widget_queue_draw(drawing_area);
                return;
            }
        }
    }
};

static gboolean OnKeyboardDraw(GtkWidget* /*widget*/, cairo_t* cr, gpointer user_data) {
    auto* st = static_cast<UsbKeyboardDialogState*>(user_data);

    // Background
    cairo_set_source_rgb(cr, 0.93, 0.93, 0.93);
    cairo_paint(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

    for (size_t i = 0; i < 105; ++i) {
        const KeyRect& kr = KEYBOARD_KEYS[i];
        bool sel = st->IsSelected(kr.id);

        // Key shadow/border
        cairo_rectangle(cr, kr.x, kr.y, kr.w, kr.h);
        if (sel) {
            // Maroon (#800000) when selected, matching 0x4d991c
            cairo_set_source_rgb(cr, 0.55, 0.0, 0.0);
        } else {
            cairo_set_source_rgb(cr, 0.98, 0.98, 0.98);
        }
        cairo_fill_preserve(cr);

        cairo_set_line_width(cr, sel ? 2.0 : 1.0);
        cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
        cairo_stroke(cr);

        // Key caption
        std::string label = GetKeyName(kr.id);
        if (label == "none") label = "";
        if ((kr.id >= 83 && kr.id <= 99) && kr.id != 83) {
            // Numpad keys: keep concise label
        }

        if (sel) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        } else {
            cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        }

        double font_size = (label.size() > 6) ? 8.5 : (label.size() > 3 ? 9.5 : 11.0);
        cairo_set_font_size(cr, font_size);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, label.c_str(), &ext);
        double tx = kr.x + (kr.w - ext.width) / 2.0 - ext.x_bearing;
        double ty = kr.y + (kr.h - ext.height) / 2.0 - ext.y_bearing;
        if (tx < kr.x + 2) tx = kr.x + 2;

        cairo_save(cr);
        cairo_rectangle(cr, kr.x + 1, kr.y + 1, kr.w - 2, kr.h - 2);
        cairo_clip(cr);
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, label.c_str());
        cairo_restore(cr);
    }

    return FALSE;
}

static gboolean OnKeyboardMousePress(GtkWidget* /*widget*/, GdkEventButton* ev, gpointer user_data) {
    if (ev->button != 1) return FALSE;
    auto* st = static_cast<UsbKeyboardDialogState*>(user_data);
    int mx = static_cast<int>(ev->x);
    int my = static_cast<int>(ev->y);

    for (size_t i = 0; i < 105; ++i) {
        const KeyRect& kr = KEYBOARD_KEYS[i];
        if (mx >= kr.x && mx <= kr.x + kr.w && my >= kr.y && my <= kr.y + kr.h) {
            st->ToggleKey(kr.id);
            break;
        }
    }
    return TRUE;
}

static void OnKeyboardClearClicked(GtkWidget* /*btn*/, gpointer user_data) {
    auto* st = static_cast<UsbKeyboardDialogState*>(user_data);
    st->keys[0] = st->keys[1] = st->keys[2] = 0;
    st->UpdateLabel();
    if (st->drawing_area) gtk_widget_queue_draw(st->drawing_area);
}

static bool RunUsbKeyboardDialog(GtkWindow* parent, uint8_t& k1, uint8_t& k2, uint8_t& k3) {
    UsbKeyboardDialogState st;
    st.keys[0] = k1;
    st.keys[1] = k2;
    st.keys[2] = k3;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "USB Keyboard - Select up to 3 Hotkeys",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK,
        nullptr
    );

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    gtk_box_set_spacing(GTK_BOX(content), 8);

    GtkWidget* top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    st.status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(st.status_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(top_bar), st.status_label, TRUE, TRUE, 0);

    GtkWidget* btn_clear = gtk_button_new_with_label("Clear Keys");
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(OnKeyboardClearClicked), &st);
    gtk_box_pack_end(GTK_BOX(top_bar), btn_clear, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), top_bar, FALSE, FALSE, 0);

    st.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(st.drawing_area, 940, 270);
    gtk_widget_add_events(st.drawing_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(st.drawing_area, "draw", G_CALLBACK(OnKeyboardDraw), &st);
    g_signal_connect(st.drawing_area, "button-press-event", G_CALLBACK(OnKeyboardMousePress), &st);
    gtk_box_pack_start(GTK_BOX(content), st.drawing_area, TRUE, TRUE, 0);

    st.UpdateLabel();
    gtk_widget_show_all(dlg);

    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    bool ok = (resp == GTK_RESPONSE_OK);
    if (ok) {
        // Pack non-zero keys into k1, k2, k3 in order
        uint8_t out[3] = {0, 0, 0};
        int idx = 0;
        for (int i = 0; i < 3; ++i) {
            if (st.keys[i] != 0) out[idx++] = st.keys[i];
        }
        k1 = out[0];
        k2 = out[1];
        k3 = out[2];
    }

    gtk_widget_destroy(dlg);
    return ok;
}

// ============================================================================
// Dialog 3: Event & Startup Source Dialog (TForm_Settings_Config_Event)
// ============================================================================
struct EventDialogState {
    GtkWindow* dlg_win = nullptr;
    T_Config11 temp_cfg;
    GtkWidget* combo_start_source = nullptr;
    GtkWidget* rows_grid = nullptr;
    GtkWidget* btn_add_event = nullptr;

    struct RowWidgets {
        GtkWidget* btn_del;
        GtkWidget* btn_kbd;
        GtkWidget* lbl_k1;
        GtkWidget* lbl_k2;
        GtkWidget* lbl_k3;
        GtkWidget* combo_source;
    };
    RowWidgets rows[16];
    bool updating_ui = false;

    static int SourceByteToComboIdx(uint8_t src) {
        if (src == 0xFF) return 0; // '-'
        if (src <= 4) return static_cast<int>(src) + 1; // '0'..'4' -> 1..5
        return 0;
    }

    static uint8_t ComboIdxToSourceByte(int idx) {
        if (idx <= 0) return 0xFF;
        if (idx >= 1 && idx <= 5) return static_cast<uint8_t>(idx - 1);
        return 0xFF;
    }

    void RefreshRows() {
        updating_ui = true;
        int active_count = 0;
        for (int i = 0; i < 16; ++i) {
            bool act = (temp_cfg.Event[i].Active == 1);
            if (act) active_count++;
            gtk_widget_set_visible(rows[i].btn_del, act);
            gtk_widget_set_visible(rows[i].btn_kbd, act);
            gtk_widget_set_visible(rows[i].lbl_k1, act);
            gtk_widget_set_visible(rows[i].lbl_k2, act);
            gtk_widget_set_visible(rows[i].lbl_k3, act);
            gtk_widget_set_visible(rows[i].combo_source, act);

            if (act) {
                gtk_label_set_text(GTK_LABEL(rows[i].lbl_k1), GetKeyName(temp_cfg.Event[i].Key1).c_str());
                gtk_label_set_text(GTK_LABEL(rows[i].lbl_k2), GetKeyName(temp_cfg.Event[i].Key2).c_str());
                gtk_label_set_text(GTK_LABEL(rows[i].lbl_k3), GetKeyName(temp_cfg.Event[i].Key3).c_str());
                gtk_combo_box_set_active(GTK_COMBO_BOX(rows[i].combo_source),
                                         SourceByteToComboIdx(temp_cfg.Event[i].Source));
            }
        }
        gtk_widget_set_sensitive(btn_add_event, active_count < 16);
        updating_ui = false;
    }

    // Exact logic of DeleteEvent (0x4d8dc8)
    void DeleteEventAt(int idx) {
        if (idx < 0 || idx >= 16) return;
        for (int i = idx; i < 15; ++i) {
            temp_cfg.Event[i] = temp_cfg.Event[i + 1];
        }
        temp_cfg.Event[15].Active = 0;
        temp_cfg.Event[15].Key1 = 0;
        temp_cfg.Event[15].Key2 = 0;
        temp_cfg.Event[15].Key3 = 0;
        temp_cfg.Event[15].Source = 0xFF;
        RefreshRows();
    }

    void AddEvent() {
        for (int i = 0; i < 16; ++i) {
            if (temp_cfg.Event[i].Active == 0) {
                temp_cfg.Event[i].Active = 1;
                temp_cfg.Event[i].Key1 = 0;
                temp_cfg.Event[i].Key2 = 0;
                temp_cfg.Event[i].Key3 = 0;
                temp_cfg.Event[i].Source = 0xFF;
                break;
            }
        }
        RefreshRows();
    }
};

static void OnEventDelClicked(GtkWidget* btn, gpointer user_data) {
    auto* st = static_cast<EventDialogState*>(user_data);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "row-index"));
    st->DeleteEventAt(idx);
}

static void OnEventKbdClicked(GtkWidget* btn, gpointer user_data) {
    auto* st = static_cast<EventDialogState*>(user_data);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "row-index"));
    if (idx < 0 || idx >= 16) return;
    uint8_t k1 = st->temp_cfg.Event[idx].Key1;
    uint8_t k2 = st->temp_cfg.Event[idx].Key2;
    uint8_t k3 = st->temp_cfg.Event[idx].Key3;
    if (RunUsbKeyboardDialog(st->dlg_win, k1, k2, k3)) {
        st->temp_cfg.Event[idx].Key1 = k1;
        st->temp_cfg.Event[idx].Key2 = k2;
        st->temp_cfg.Event[idx].Key3 = k3;
        st->RefreshRows();
    }
}

static void OnEventSourceChanged(GtkComboBox* combo, gpointer user_data) {
    auto* st = static_cast<EventDialogState*>(user_data);
    if (st->updating_ui) return;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo), "row-index"));
    if (idx < 0 || idx >= 16) return;
    int active = gtk_combo_box_get_active(combo);
    st->temp_cfg.Event[idx].Source = EventDialogState::ComboIdxToSourceByte(active);
}

static void OnAddEventClicked(GtkWidget* /*btn*/, gpointer user_data) {
    auto* st = static_cast<EventDialogState*>(user_data);
    st->AddEvent();
}

static bool RunEventDialog(GtkWindow* parent, T_Config11& cfg) {
    EventDialogState st;
    st.temp_cfg = cfg;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Event & Startup Configuration",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK,
        nullptr
    );
    st.dlg_win = GTK_WINDOW(dlg);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 660, 560);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    // Top row: Startup Source + Switch options legend
    GtkWidget* top_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_pack_start(GTK_BOX(content), top_hbox, FALSE, FALSE, 0);

    GtkWidget* frame_startup = gtk_frame_new("Startup Source");
    GtkWidget* startup_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(startup_box), 8);
    gtk_container_add(GTK_CONTAINER(frame_startup), startup_box);

    gtk_box_pack_start(GTK_BOX(startup_box), gtk_label_new("Keyboard / Mouse:"), FALSE, FALSE, 0);
    st.combo_start_source = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_start_source), "none");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_start_source), "1");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_start_source), "2");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_start_source), "3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_start_source), "4");
    int init_start = (st.temp_cfg.Start_Source <= 4) ? st.temp_cfg.Start_Source : 0;
    gtk_combo_box_set_active(GTK_COMBO_BOX(st.combo_start_source), init_start);
    gtk_box_pack_start(GTK_BOX(startup_box), st.combo_start_source, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top_hbox), frame_startup, FALSE, FALSE, 0);

    GtkWidget* frame_legend = gtk_frame_new("Switch options");
    GtkWidget* legend_lbl = gtk_label_new("  -    = No Change\n  0    = Switch Off\n1 - 4 = Switch to Source");
    gtk_widget_set_margin_start(legend_lbl, 10);
    gtk_widget_set_margin_end(legend_lbl, 10);
    gtk_widget_set_margin_top(legend_lbl, 4);
    gtk_widget_set_margin_bottom(legend_lbl, 4);
    gtk_container_add(GTK_CONTAINER(frame_legend), legend_lbl);
    gtk_box_pack_start(GTK_BOX(top_hbox), frame_legend, TRUE, TRUE, 0);

    // Events Frame
    GtkWidget* frame_events = gtk_frame_new("Hotkey Events (up to 16)");
    GtkWidget* events_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(events_vbox), 8);
    gtk_container_add(GTK_CONTAINER(frame_events), events_vbox);
    gtk_box_pack_start(GTK_BOX(content), frame_events, TRUE, TRUE, 0);

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(events_vbox), scrolled, TRUE, TRUE, 0);

    st.rows_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(st.rows_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(st.rows_grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(st.rows_grid), 6);
    gtk_container_add(GTK_CONTAINER(scrolled), st.rows_grid);

    // Header labels
    gtk_grid_attach(GTK_GRID(st.rows_grid), gtk_label_new("Action"), 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(st.rows_grid), gtk_label_new("Key 1"), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(st.rows_grid), gtk_label_new("Key 2"), 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(st.rows_grid), gtk_label_new("Key 3"), 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(st.rows_grid), gtk_label_new("Switched to Source (Keyboard/Mouse)"), 5, 0, 1, 1);

    for (int i = 0; i < 16; ++i) {
        st.rows[i].btn_del = gtk_button_new_with_label("Del");
        g_object_set_data(G_OBJECT(st.rows[i].btn_del), "row-index", GINT_TO_POINTER(i));
        g_signal_connect(st.rows[i].btn_del, "clicked", G_CALLBACK(OnEventDelClicked), &st);

        st.rows[i].btn_kbd = gtk_button_new_with_label("Keyboard");
        g_object_set_data(G_OBJECT(st.rows[i].btn_kbd), "row-index", GINT_TO_POINTER(i));
        g_signal_connect(st.rows[i].btn_kbd, "clicked", G_CALLBACK(OnEventKbdClicked), &st);

        st.rows[i].lbl_k1 = gtk_label_new("none");
        gtk_widget_set_size_request(st.rows[i].lbl_k1, 95, -1);
        st.rows[i].lbl_k2 = gtk_label_new("none");
        gtk_widget_set_size_request(st.rows[i].lbl_k2, 95, -1);
        st.rows[i].lbl_k3 = gtk_label_new("none");
        gtk_widget_set_size_request(st.rows[i].lbl_k3, 95, -1);

        st.rows[i].combo_source = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.rows[i].combo_source), "-");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.rows[i].combo_source), "0");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.rows[i].combo_source), "1");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.rows[i].combo_source), "2");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.rows[i].combo_source), "3");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.rows[i].combo_source), "4");
        g_object_set_data(G_OBJECT(st.rows[i].combo_source), "row-index", GINT_TO_POINTER(i));
        g_signal_connect(st.rows[i].combo_source, "changed", G_CALLBACK(OnEventSourceChanged), &st);

        int r = i + 1;
        gtk_grid_attach(GTK_GRID(st.rows_grid), st.rows[i].btn_del, 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(st.rows_grid), st.rows[i].btn_kbd, 1, r, 1, 1);
        gtk_grid_attach(GTK_GRID(st.rows_grid), st.rows[i].lbl_k1, 2, r, 1, 1);
        gtk_grid_attach(GTK_GRID(st.rows_grid), st.rows[i].lbl_k2, 3, r, 1, 1);
        gtk_grid_attach(GTK_GRID(st.rows_grid), st.rows[i].lbl_k3, 4, r, 1, 1);
        gtk_grid_attach(GTK_GRID(st.rows_grid), st.rows[i].combo_source, 5, r, 1, 1);
    }

    st.btn_add_event = gtk_button_new_with_label("Add Event");
    g_signal_connect(st.btn_add_event, "clicked", G_CALLBACK(OnAddEventClicked), &st);
    gtk_box_pack_start(GTK_BOX(events_vbox), st.btn_add_event, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);
    st.RefreshRows();

    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    bool ok = (resp == GTK_RESPONSE_OK);
    if (ok) {
        int s = gtk_combo_box_get_active(GTK_COMBO_BOX(st.combo_start_source));
        st.temp_cfg.Start_Source = (s >= 0 && s <= 4) ? static_cast<uint8_t>(s) : 0;
        cfg = st.temp_cfg;
    }

    gtk_widget_destroy(dlg);
    return ok;
}

// ============================================================================
// Dialog 4: Mouseswitch / Monitor Arrangement Dialog (TForm_Settings_Config_Monitor)
// ============================================================================
struct ResolutionPreset {
    const char* label;
    uint16_t w;
    uint16_t h;
};

static const ResolutionPreset RES_PRESETS[] = {
    {"640 x 480",   640,  480},
    {"800 x 600",   800,  600},
    {"1024 x 768",  1024, 768},
    {"1152 x 864",  1152, 864},
    {"1280 x 600",  1280, 600},
    {"1280 x 720",  1280, 720},
    {"1280 x 768",  1280, 768},
    {"1280 x 800",  1280, 800},
    {"1280 x 960",  1280, 960},
    {"1280 x 1024", 1280, 1024},
    {"1360 x 768",  1360, 768},
    {"1366 x 768",  1366, 768},
    {"1400 x 1050", 1400, 1050},
    {"1440 x 900",  1440, 900},
    {"1600 x 900",  1600, 900},
    {"1600 x 1200", 1600, 1200},
    {"1680 x 1050", 1680, 1050},
    {"1920 x 1080", 1920, 1080},
    {"1920 x 1200", 1920, 1200},
    {"2048 x 1152", 2048, 1152},
    {"2560 x 1440", 2560, 1440},
    {"2560 x 1600", 2560, 1600},
    {"3840 x 2160", 3840, 2160},
};
static constexpr size_t NUM_RES_PRESETS = sizeof(RES_PRESETS) / sizeof(RES_PRESETS[0]);

static bool RunCustomResolutionDialog(GtkWindow* parent, uint16_t& w, uint16_t& h) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Custom Resolution",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK,
        nullptr
    );
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Size X (Width):"), 0, 0, 1, 1);
    GtkWidget* spin_w = gtk_spin_button_new_with_range(320.0, 16384.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_w), w);
    gtk_grid_attach(GTK_GRID(grid), spin_w, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Size Y (Height):"), 0, 1, 1, 1);
    GtkWidget* spin_h = gtk_spin_button_new_with_range(200.0, 16384.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_h), h);
    gtk_grid_attach(GTK_GRID(grid), spin_h, 1, 1, 1, 1);

    gtk_widget_show_all(dlg);
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    bool ok = (resp == GTK_RESPONSE_OK);
    if (ok) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(spin_w));
        gtk_spin_button_update(GTK_SPIN_BUTTON(spin_h));
        w = static_cast<uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_w)));
        h = static_cast<uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_h)));
    }
    gtk_widget_destroy(dlg);
    return ok;
}

struct MonitorDialogState {
    GtkWindow* dlg_win = nullptr;
    T_Config11 temp_cfg;
    GtkWidget* grid_area = nullptr;
    GtkWidget* chk_horiz = nullptr;
    GtkWidget* chk_vert = nullptr;
    GtkWidget* combo_res = nullptr;
    GtkWidget* lbl_custom_res = nullptr;
    GtkWidget* radio_ws[4] = {nullptr, nullptr, nullptr, nullptr};

    // Per-WS (0..3), Per-Monitor (0..3) combos:
    GtkWidget* combo_ms_leave[4][4];
    GtkWidget* combo_ms_enter[4][4];
    GtkWidget* combo_man_leave[4][4];
    GtkWidget* combo_man_enter[4][4];

    int selected_ws = 1; // 1..4
    uint16_t current_w = 1920;
    uint16_t current_h = 1080;

    int CountMonitorsForWS(int ws) const {
        int count = 0;
        for (int i = 0; i < 128; ++i) {
            uint8_t s = temp_cfg.Monitor[i].Source;
            if (s == ws || s == ws + 8) count++;
        }
        return count;
    }

    int GetMonitorOrdinal(int cell_idx) const {
        uint8_t target_ws = temp_cfg.Monitor[cell_idx].Source;
        if (target_ws > 8) target_ws -= 8;
        if (target_ws < 1 || target_ws > 4) return 0;
        int ord = 0;
        for (int i = 0; i <= cell_idx; ++i) {
            uint8_t s = temp_cfg.Monitor[i].Source;
            if (s > 8) s -= 8;
            if (s == target_ws) ord++;
        }
        return ord;
    }
};

static gboolean OnMonitorGridDraw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    auto* st = static_cast<MonitorDialogState*>(user_data);
    int alloc_w = gtk_widget_get_allocated_width(widget);
    int alloc_h = gtk_widget_get_allocated_height(widget);

    double cell_w = alloc_w / 16.0;
    double cell_h = alloc_h / 8.0;

    // White background
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 16; ++c) {
            int idx = r * 16 + c;
            const T_Monitor& m = st->temp_cfg.Monitor[idx];
            double x = c * cell_w;
            double y = r * cell_h;

            uint8_t raw_src = m.Source;
            bool hatched = (raw_src > 8);
            uint8_t ws = hatched ? (raw_src - 8) : raw_src;

            if (ws >= 1 && ws <= 4) {
                // Exact colors from ColorTable (0x4fa66c):
                // WS1 = Red (#FF0000), WS2 = Lime (#00FF00), WS3 = Yellow (#FFFF00), WS4 = Aqua (#00FFFF)
                if (ws == 1) cairo_set_source_rgb(cr, 1.0, 0.22, 0.22);
                else if (ws == 2) cairo_set_source_rgb(cr, 0.15, 0.95, 0.15);
                else if (ws == 3) cairo_set_source_rgb(cr, 1.0, 1.0, 0.15);
                else if (ws == 4) cairo_set_source_rgb(cr, 0.15, 0.95, 0.95);

                cairo_rectangle(cr, x + 1, y + 1, cell_w - 2, cell_h - 2);
                cairo_fill(cr);

                if (hatched) {
                    // Cross-hatch lines (bsDiagCross in Delphi)
                    cairo_save(cr);
                    cairo_rectangle(cr, x + 1, y + 1, cell_w - 2, cell_h - 2);
                    cairo_clip(cr);
                    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
                    cairo_set_line_width(cr, 1.0);
                    for (double d = -cell_h; d < cell_w + cell_h; d += 8.0) {
                        cairo_move_to(cr, x + d, y);
                        cairo_line_to(cr, x + d + cell_h, y + cell_h);
                        cairo_move_to(cr, x + d + cell_h, y);
                        cairo_line_to(cr, x + d, y + cell_h);
                    }
                    cairo_stroke(cr);
                    cairo_restore(cr);
                }

                // Draw "WS<n> - M<m>" and "<W>x<H>"
                int ord = st->GetMonitorOrdinal(idx);
                char line1[32], line2[32];
                std::snprintf(line1, sizeof(line1), "WS%d - M%d", ws, ord);
                std::snprintf(line2, sizeof(line2), "%ux%u", m.Width, m.Height);

                cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
                cairo_set_font_size(cr, 8.5);

                cairo_text_extents_t ext1, ext2;
                cairo_text_extents(cr, line1, &ext1);
                cairo_text_extents(cr, line2, &ext2);

                cairo_move_to(cr, x + (cell_w - ext1.width) / 2.0 - ext1.x_bearing, y + cell_h * 0.42);
                cairo_show_text(cr, line1);

                cairo_move_to(cr, x + (cell_w - ext2.width) / 2.0 - ext2.x_bearing, y + cell_h * 0.80);
                cairo_show_text(cr, line2);
            }

            // Grid cell border
            cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
            cairo_set_line_width(cr, 1.0);
            cairo_rectangle(cr, x, y, cell_w, cell_h);
            cairo_stroke(cr);
        }
    }

    return FALSE;
}

// Exact port of TForm_Settings_Config_Monitor.ImageMouseDown (0x4dab2c)
static gboolean OnMonitorGridMousePress(GtkWidget* widget, GdkEventButton* ev, gpointer user_data) {
    auto* st = static_cast<MonitorDialogState*>(user_data);
    int alloc_w = gtk_widget_get_allocated_width(widget);
    int alloc_h = gtk_widget_get_allocated_height(widget);
    if (alloc_w <= 0 || alloc_h <= 0) return FALSE;

    int col = static_cast<int>((ev->x / alloc_w) * 16.0);
    int row = static_cast<int>((ev->y / alloc_h) * 8.0);
    if (col < 0 || col >= 16 || row < 0 || row >= 8) return FALSE;

    int idx = row * 16 + col;
    T_Monitor& cell = st->temp_cfg.Monitor[idx];

    if (ev->button == 1) {
        // Left-click: toggle empty <-> selected workstation (max 4 monitors per WS)
        if (cell.Source == 0) {
            if (st->CountMonitorsForWS(st->selected_ws) >= 4) {
                ShowMessage(st->dlg_win, GTK_MESSAGE_INFO, "Maximum Monitors Reached",
                            "Workstation WS" + std::to_string(st->selected_ws) + " already has the maximum of 4 monitors.");
                return TRUE;
            }
            cell.Source = static_cast<uint8_t>(st->selected_ws);
            cell.Pad = 0;
            cell.Width = st->current_w;
            cell.Height = st->current_h;
        } else {
            cell.Source = 0;
            cell.Pad = 0;
            cell.Width = 0;
            cell.Height = 0;
        }
        gtk_widget_queue_draw(widget);
    } else if (ev->button == 3) {
        // Right-click: toggle solid (1..4) <-> cross-hatched (9..12)
        if (cell.Source >= 1 && cell.Source <= 4) {
            cell.Source += 8;
            gtk_widget_queue_draw(widget);
        } else if (cell.Source >= 9 && cell.Source <= 12) {
            cell.Source -= 8;
            gtk_widget_queue_draw(widget);
        }
    }
    return TRUE;
}

static void OnMonitorResComboChanged(GtkComboBox* combo, gpointer user_data) {
    auto* st = static_cast<MonitorDialogState*>(user_data);
    int idx = gtk_combo_box_get_active(combo);
    if (idx >= 0 && static_cast<size_t>(idx) < NUM_RES_PRESETS) {
        st->current_w = RES_PRESETS[idx].w;
        st->current_h = RES_PRESETS[idx].h;
    } else if (static_cast<size_t>(idx) == NUM_RES_PRESETS) {
        // "Custom..."
        uint16_t cw = st->current_w;
        uint16_t ch = st->current_h;
        if (RunCustomResolutionDialog(st->dlg_win, cw, ch)) {
            st->current_w = cw;
            st->current_h = ch;
        }
    }
    if (st->lbl_custom_res) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Active Size: %u x %u", st->current_w, st->current_h);
        gtk_label_set_text(GTK_LABEL(st->lbl_custom_res), buf);
    }
}

static void OnMonitorWsToggled(GtkToggleButton* btn, gpointer user_data) {
    if (!gtk_toggle_button_get_active(btn)) return;
    auto* st = static_cast<MonitorDialogState*>(user_data);
    int ws = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "ws-id"));
    if (ws >= 1 && ws <= 4) st->selected_ws = ws;
}

static void OnMonitorClearAllClicked(GtkWidget* /*btn*/, gpointer user_data) {
    auto* st = static_cast<MonitorDialogState*>(user_data);
    for (int i = 0; i < 128; ++i) {
        st->temp_cfg.Monitor[i] = {0, 0, 0, 0};
    }
    if (st->grid_area) gtk_widget_queue_draw(st->grid_area);
}

static bool RunMouseswitchDialog(GtkWindow* parent, T_Config11& cfg) {
    MonitorDialogState st;
    st.temp_cfg = cfg;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Mouseswitch & Monitor Arrangement",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK,
        nullptr
    );
    st.dlg_win = GTK_WINDOW(dlg);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 1120, 720);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    // Upper area: 8x16 Grid on the left, Controls on the right
    GtkWidget* upper_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(content), upper_hbox, TRUE, TRUE, 0);

    GtkWidget* grid_frame = gtk_frame_new("Monitor Grid (8 rows x 16 cols — Left-click: Add/Remove Monitor, Right-click: Toggle Hatched Mode)");
    GtkWidget* grid_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(grid_vbox), 6);
    gtk_container_add(GTK_CONTAINER(grid_frame), grid_vbox);

    st.grid_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(st.grid_area, 860, 340);
    gtk_widget_add_events(st.grid_area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(st.grid_area, "draw", G_CALLBACK(OnMonitorGridDraw), &st);
    g_signal_connect(st.grid_area, "button-press-event", G_CALLBACK(OnMonitorGridMousePress), &st);
    gtk_box_pack_start(GTK_BOX(grid_vbox), st.grid_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(upper_hbox), grid_frame, TRUE, TRUE, 0);

    // Right-hand side control panel
    GtkWidget* right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(upper_hbox), right_vbox, FALSE, FALSE, 0);

    // Switch over checkboxes
    GtkWidget* frame_switchover = gtk_frame_new("Wrap-Around Switch Over");
    GtkWidget* so_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(so_vbox), 8);
    gtk_container_add(GTK_CONTAINER(frame_switchover), so_vbox);

    st.chk_horiz = gtk_check_button_new_with_label("Horizontal switch over");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st.chk_horiz), (st.temp_cfg.Switch_Over & 1) != 0);
    gtk_box_pack_start(GTK_BOX(so_vbox), st.chk_horiz, FALSE, FALSE, 0);

    st.chk_vert = gtk_check_button_new_with_label("Vertical switch over");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st.chk_vert), (st.temp_cfg.Switch_Over & 2) != 0);
    gtk_box_pack_start(GTK_BOX(so_vbox), st.chk_vert, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_vbox), frame_switchover, FALSE, FALSE, 0);

    // Resolution selector
    GtkWidget* frame_res = gtk_frame_new("Resolution");
    GtkWidget* res_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(res_vbox), 8);
    gtk_container_add(GTK_CONTAINER(frame_res), res_vbox);

    st.combo_res = gtk_combo_box_text_new();
    int default_res_idx = 17; // 1920 x 1080
    for (size_t i = 0; i < NUM_RES_PRESETS; ++i) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_res), RES_PRESETS[i].label);
    }
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_res), "Custom...");
    gtk_combo_box_set_active(GTK_COMBO_BOX(st.combo_res), default_res_idx);
    g_signal_connect(st.combo_res, "changed", G_CALLBACK(OnMonitorResComboChanged), &st);
    gtk_box_pack_start(GTK_BOX(res_vbox), st.combo_res, FALSE, FALSE, 0);

    st.lbl_custom_res = gtk_label_new("Active Size: 1920 x 1080");
    gtk_box_pack_start(GTK_BOX(res_vbox), st.lbl_custom_res, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_vbox), frame_res, FALSE, FALSE, 0);

    // Workstation Selector
    GtkWidget* frame_ws = gtk_frame_new("Select Workstation to Place");
    GtkWidget* ws_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(ws_vbox), 8);
    gtk_container_add(GTK_CONTAINER(frame_ws), ws_vbox);

    const char* ws_labels[4] = {
        "WS1 (Red — Source 1)",
        "WS2 (Green — Source 2)",
        "WS3 (Yellow — Source 3)",
        "WS4 (Aqua — Source 4)"
    };
    for (int i = 0; i < 4; ++i) {
        if (i == 0) {
            st.radio_ws[i] = gtk_radio_button_new_with_label(nullptr, ws_labels[i]);
        } else {
            st.radio_ws[i] = gtk_radio_button_new_with_label_from_widget(
                GTK_RADIO_BUTTON(st.radio_ws[0]), ws_labels[i]);
        }
        g_object_set_data(G_OBJECT(st.radio_ws[i]), "ws-id", GINT_TO_POINTER(i + 1));
        g_signal_connect(st.radio_ws[i], "toggled", G_CALLBACK(OnMonitorWsToggled), &st);
        gtk_box_pack_start(GTK_BOX(ws_vbox), st.radio_ws[i], FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(right_vbox), frame_ws, FALSE, FALSE, 0);

    GtkWidget* btn_clear_grid = gtk_button_new_with_label("Clear All Monitors");
    g_signal_connect(btn_clear_grid, "clicked", G_CALLBACK(OnMonitorClearAllClicked), &st);
    gtk_box_pack_start(GTK_BOX(right_vbox), btn_clear_grid, FALSE, FALSE, 0);

    // Lower area: 4 Workstation frames (WS1..WS4), each with a 4-tab notebook (Monitor 1..4)
    GtkWidget* lower_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(content), lower_hbox, FALSE, FALSE, 0);

    for (int ws = 0; ws < 4; ++ws) {
        std::string ws_title = "WS" + std::to_string(ws + 1);
        GtkWidget* ws_frame = gtk_frame_new(ws_title.c_str());
        gtk_box_pack_start(GTK_BOX(lower_hbox), ws_frame, TRUE, TRUE, 0);

        GtkWidget* nb = gtk_notebook_new();
        gtk_container_add(GTK_CONTAINER(ws_frame), nb);

        for (int mon = 0; mon < 4; ++mon) {
            GtkWidget* page_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_container_set_border_width(GTK_CONTAINER(page_vbox), 6);

            // Mouseswitch sub-frame
            GtkWidget* fr_ms = gtk_frame_new("Mouseswitch");
            GtkWidget* gr_ms = gtk_grid_new();
            gtk_grid_set_row_spacing(GTK_GRID(gr_ms), 4);
            gtk_grid_set_column_spacing(GTK_GRID(gr_ms), 6);
            gtk_container_set_border_width(GTK_CONTAINER(gr_ms), 6);
            gtk_container_add(GTK_CONTAINER(fr_ms), gr_ms);

            gtk_grid_attach(GTK_GRID(gr_ms), gtk_label_new("Mouse on leave:"), 0, 0, 1, 1);
            st.combo_ms_leave[ws][mon] = gtk_combo_box_text_new();
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_ms_leave[ws][mon]), "not move");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_ms_leave[ws][mon]), "move to 0/0");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_ms_leave[ws][mon]), "move to M/M");
            gtk_combo_box_set_active(GTK_COMBO_BOX(st.combo_ms_leave[ws][mon]),
                                     GetMouseLeave(st.temp_cfg.MousePointer_Leave, ws, mon));
            gtk_grid_attach(GTK_GRID(gr_ms), st.combo_ms_leave[ws][mon], 1, 0, 1, 1);

            gtk_grid_attach(GTK_GRID(gr_ms), gtk_label_new("Mouse on enter:"), 0, 1, 1, 1);
            st.combo_ms_enter[ws][mon] = gtk_combo_box_text_new();
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_ms_enter[ws][mon]), "no change");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_ms_enter[ws][mon]), "latest position");
            gtk_combo_box_set_active(GTK_COMBO_BOX(st.combo_ms_enter[ws][mon]),
                                     GetMouseEnter(st.temp_cfg.MousePointer_Enter, ws, mon));
            gtk_grid_attach(GTK_GRID(gr_ms), st.combo_ms_enter[ws][mon], 1, 1, 1, 1);

            gtk_box_pack_start(GTK_BOX(page_vbox), fr_ms, FALSE, FALSE, 0);

            // Switch manually sub-frame
            GtkWidget* fr_man = gtk_frame_new("Switch manually");
            GtkWidget* gr_man = gtk_grid_new();
            gtk_grid_set_row_spacing(GTK_GRID(gr_man), 4);
            gtk_grid_set_column_spacing(GTK_GRID(gr_man), 6);
            gtk_container_set_border_width(GTK_CONTAINER(gr_man), 6);
            gtk_container_add(GTK_CONTAINER(fr_man), gr_man);

            gtk_grid_attach(GTK_GRID(gr_man), gtk_label_new("Mouse on leave:"), 0, 0, 1, 1);
            st.combo_man_leave[ws][mon] = gtk_combo_box_text_new();
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_man_leave[ws][mon]), "not move");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_man_leave[ws][mon]), "move to 0/0");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_man_leave[ws][mon]), "move to M/M");
            gtk_combo_box_set_active(GTK_COMBO_BOX(st.combo_man_leave[ws][mon]),
                                     GetMouseLeave(st.temp_cfg.MousePointer_Leave_Man, ws, mon));
            gtk_grid_attach(GTK_GRID(gr_man), st.combo_man_leave[ws][mon], 1, 0, 1, 1);

            gtk_grid_attach(GTK_GRID(gr_man), gtk_label_new("Mouse on enter:"), 0, 1, 1, 1);
            st.combo_man_enter[ws][mon] = gtk_combo_box_text_new();
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_man_enter[ws][mon]), "no change");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st.combo_man_enter[ws][mon]), "latest position");
            gtk_combo_box_set_active(GTK_COMBO_BOX(st.combo_man_enter[ws][mon]),
                                     GetMouseEnter(st.temp_cfg.MousePointer_Enter_Man, ws, mon));
            gtk_grid_attach(GTK_GRID(gr_man), st.combo_man_enter[ws][mon], 1, 1, 1, 1);

            gtk_box_pack_start(GTK_BOX(page_vbox), fr_man, FALSE, FALSE, 0);

            std::string tab_lbl = "Mon " + std::to_string(mon + 1);
            gtk_notebook_append_page(GTK_NOTEBOOK(nb), page_vbox, gtk_label_new(tab_lbl.c_str()));
        }
    }

    gtk_widget_show_all(dlg);

    bool accepted = false;
    while (true) {
        gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
        if (resp == GTK_RESPONSE_OK) {
            uint8_t so = 0;
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st.chk_horiz))) so |= 1;
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st.chk_vert)))  so |= 2;
            st.temp_cfg.Switch_Over = so;

            for (int ws = 0; ws < 4; ++ws) {
                for (int mon = 0; mon < 4; ++mon) {
                    int ml = gtk_combo_box_get_active(GTK_COMBO_BOX(st.combo_ms_leave[ws][mon]));
                    int me = gtk_combo_box_get_active(GTK_COMBO_BOX(st.combo_ms_enter[ws][mon]));
                    int man_l = gtk_combo_box_get_active(GTK_COMBO_BOX(st.combo_man_leave[ws][mon]));
                    int man_e = gtk_combo_box_get_active(GTK_COMBO_BOX(st.combo_man_enter[ws][mon]));

                    SetMouseLeave(st.temp_cfg.MousePointer_Leave, ws, mon, ml >= 0 ? static_cast<uint8_t>(ml) : 0);
                    SetMouseEnter(st.temp_cfg.MousePointer_Enter, ws, mon, me >= 0 ? static_cast<uint8_t>(me) : 0);
                    SetMouseLeave(st.temp_cfg.MousePointer_Leave_Man, ws, mon, man_l >= 0 ? static_cast<uint8_t>(man_l) : 0);
                    SetMouseEnter(st.temp_cfg.MousePointer_Enter_Man, ws, mon, man_e >= 0 ? static_cast<uint8_t>(man_e) : 0);
                }
            }

            // Validate monitor position using exact Delphi CheckMonitorPosition
            if (!CheckMonitorPosition(st.temp_cfg)) {
                ShowMessage(GTK_WINDOW(dlg), GTK_MESSAGE_WARNING, "Illegal Monitor Position",
                            "Illegal Monitor Position!\n\n"
                            "Please make sure:\n"
                            "• At least one monitor is placed in the grid.\n"
                            "• Monitors are contiguously connected horizontally/vertically.\n"
                            "•Monitors of the same Workstation (WS) form a contiguous group.");
                continue;
            }

            cfg = st.temp_cfg;
            accepted = true;
            break;
        } else {
            break;
        }
    }

    gtk_widget_destroy(dlg);
    return accepted;
}

// ============================================================================
// Dialog 5: History Dialog (TForm_History)
// ============================================================================
static void RunHistoryDialog(GtkWindow* parent) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Version History (HIS22411.txt)",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Close", GTK_RESPONSE_CLOSE,
        nullptr
    );
    gtk_window_set_default_size(GTK_WINDOW(dlg), 540, 440);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);

    GtkWidget* tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), HIS22411_TXT, -1);
    gtk_container_add(GTK_CONTAINER(scrolled), tv);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ============================================================================
// Dialog 6: About & Live Hardware Info Dialog (TForm_About)
// ============================================================================
struct AboutDialogState {
    GtkWindow* dlg_win = nullptr;
    GtkWidget* lbl_hw_info = nullptr;
    GtkWidget* prog_bar = nullptr;
};

static std::string FormatVer(uint16_t v) {
    if (v == 0 || v == 0xFFFF) return "N/A";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "V%u.%u", v / 10, v % 10);
    return buf;
}

static void OnAboutReadInfoClicked(GtkWidget* btn, gpointer user_data) {
    auto* st = static_cast<AboutDialogState*>(user_data);
    gtk_widget_set_sensitive(btn, FALSE);
    gtk_widget_set_visible(st->prog_bar, TRUE);

    SwitchDeviceInfo info;
    std::string msg;
    bool ok = ReadSwitchDeviceInfo(info, msg, [&](double frac, const std::string& stage) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->prog_bar), frac);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(st->prog_bar), stage.c_str());
        while (gtk_events_pending()) gtk_main_iteration();
    });

    gtk_widget_set_sensitive(btn, TRUE);
    if (ok) {
        std::ostringstream oss;
        oss << "Device Product ID: " << info.product_id << " (0x" << std::hex << std::uppercase << info.product_id << std::dec << ")"
            << " — Bus " << static_cast<int>(info.bus_number)
            << ", Addr " << static_cast<int>(info.device_address) << "\n\n"
            << "• Device:    " << info.chips[0].name << "\n"
            << "• Firmware:  " << FormatVer(info.chips[0].fw_version) << "\n"
            << "• ICSP:      " << FormatVer(info.chips[0].icsp_version);
        gtk_label_set_text(GTK_LABEL(st->lbl_hw_info), oss.str().c_str());
    } else {
        ShowMessage(st->dlg_win, GTK_MESSAGE_ERROR, "Read Device Info Failed", msg);
    }
}

static const char* const NEWEY_LICENSE_TEXT =
    "Newey — Configuration & Firmware Utility for the USB-Deskswitch II 1:4\n"
    "SPDX-License-Identifier: GPL-3.0-or-later WITH Commons-Clause-1.0\n\n"
    "LICENSE SUMMARY:\n"
    "1. Open Source Copyleft (Forks Must Be Open Source):\n"
    "   Any fork, modification, or derivative work of Newey that is distributed\n"
    "   must be licensed under these same terms and accompanied by its complete\n"
    "   corresponding source code (GNU General Public License v3.0).\n\n"
    "2. Commercial Usage Permitted:\n"
    "   You are free to run, use, and deploy Newey for personal, internal business,\n"
    "   and commercial operations without restriction.\n\n"
    "3. Resale Prohibited (Commons Clause v1.0):\n"
    "   You may NOT sell, resell, rent, or charge a fee for this software, any\n"
    "   fork or derivative of this software, or bundle it for sale.\n\n"
    "-----------------------------------------------------------------------\n"
    "COMMONS CLAUSE LICENSE CONDITION v1.0\n"
    "-----------------------------------------------------------------------\n"
    "The Software is provided to you by the Licensor under the GNU General Public\n"
    "License v3.0 (the \"License\"), coupled with the following Commons Clause\n"
    "restriction:\n\n"
    "Without limiting other conditions in the License, the grant of rights under\n"
    "the License will not include, and the License does not grant to you, the\n"
    "right to Sell the Software.\n\n"
    "For purposes of the foregoing, \"Sell\" means practicing any or all of the\n"
    "rights granted to you under the License to provide to third parties, for a\n"
    "fee or other consideration, a product or service whose value derives,\n"
    "entirely or substantially, from the functionality of the Software. Any\n"
    "license notice or attribution required by the License must also include\n"
    "this Commons Clause License Condition notice.\n";

static void RunLicenseDialog(GtkWindow* parent) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Newey License (GPL-3.0 WITH Commons-Clause-1.0)",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Close", GTK_RESPONSE_CLOSE,
        nullptr
    );
    gtk_window_set_default_size(GTK_WINDOW(dlg), 600, 440);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);

    GtkWidget* tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), NEWEY_LICENSE_TEXT, -1);
    gtk_container_add(GTK_CONTAINER(scrolled), tv);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void OnAboutViewLicenseClicked(GtkWidget* /*btn*/, gpointer user_data) {
    auto* st = static_cast<AboutDialogState*>(user_data);
    RunLicenseDialog(st->dlg_win);
}

static void RunAboutDialog(GtkWindow* parent) {
    AboutDialogState st;
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "About Newey — USB-Deskswitch II 1:4",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Close", GTK_RESPONSE_CLOSE,
        nullptr
    );
    st.dlg_win = GTK_WINDOW(dlg);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 410);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 14);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    GdkPixbuf* logo = LoadWeyLogoPixbuf();
    if (logo) {
        GtkWidget* img = gtk_image_new_from_pixbuf(logo);
        gtk_box_pack_start(GTK_BOX(content), img, FALSE, FALSE, 0);
        g_object_unref(logo);
    }

    GtkWidget* title_lbl = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title_lbl),
        "<span size='large' weight='bold'>Newey</span>\n"
        "Configuration &amp; Firmware Utility for <b>USB-Deskswitch II 1:4</b> (22411R / 22411S)\n"
        "Software Version: <b>V1.7</b>\n"
        "<small>License: <b>GPL-3.0-or-later WITH Commons-Clause-1.0</b>\n"
        "(Forks must be Open Source • Commercial Use Allowed • Resale Prohibited)</small>");
    gtk_label_set_justify(GTK_LABEL(title_lbl), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(content), title_lbl, FALSE, FALSE, 0);

    GtkWidget* btn_license = gtk_button_new_with_label("View Open Source Non-Resale License...");
    g_signal_connect(btn_license, "clicked", G_CALLBACK(OnAboutViewLicenseClicked), &st);
    gtk_box_pack_start(GTK_BOX(content), btn_license, FALSE, FALSE, 0);

    GtkWidget* frame_hw = gtk_frame_new("Hardware Firmware & ICSP Versions");
    GtkWidget* hw_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hw_vbox), 10);
    gtk_container_add(GTK_CONTAINER(frame_hw), hw_vbox);

    st.lbl_hw_info = gtk_label_new("Click 'Read Info from Switch' to query the connected USB-Deskswitch II 1:4.");
    gtk_label_set_selectable(GTK_LABEL(st.lbl_hw_info), TRUE);
    gtk_box_pack_start(GTK_BOX(hw_vbox), st.lbl_hw_info, FALSE, FALSE, 0);

    st.prog_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(st.prog_bar), TRUE);
    gtk_box_pack_start(GTK_BOX(hw_vbox), st.prog_bar, FALSE, FALSE, 0);

    GtkWidget* btn_read = gtk_button_new_with_label("Read Info from Switch");
    g_signal_connect(btn_read, "clicked", G_CALLBACK(OnAboutReadInfoClicked), &st);
    gtk_box_pack_start(GTK_BOX(hw_vbox), btn_read, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), frame_hw, TRUE, TRUE, 0);

    gtk_widget_show_all(dlg);
    gtk_widget_set_visible(st.prog_bar, FALSE);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ============================================================================
// Dialog 7: Firmware Update Dialog (TForm_Firmware_Update)
// ============================================================================
struct FirmwareDialogState {
    GtkWindow* dlg_win = nullptr;
    GtkWidget* entry_path = nullptr;
    GtkWidget* lbl_summary = nullptr;
    GtkWidget* chk_default_settings = nullptr;
    GtkWidget* prog_bar = nullptr;
    FirmwarePackage pkg;
    bool pkg_loaded = false;

    void TryLoadFile(const std::string& path) {
        std::string err;
        if (LoadFirmwarePackage(path, pkg, err)) {
            pkg_loaded = true;
            std::ostringstream oss;
            oss << "Loaded Firmware Package:\n"
                << "  • MAIN (PIC32MX675F512H): " << FormatVer(pkg.main_chip.version) << "\n"
                << "  • ICSP (PIC18F4550):      " << FormatVer(pkg.icsp.version) << "\n"
                << "  • USB 1..4 (PIC18F4550):  " << FormatVer(pkg.devices[0].version);
            gtk_label_set_text(GTK_LABEL(lbl_summary), oss.str().c_str());
        } else {
            pkg_loaded = false;
            gtk_label_set_text(GTK_LABEL(lbl_summary), ("Could not load firmware file: " + err).c_str());
        }
    }
};

static void OnFwBrowseClicked(GtkWidget* /*btn*/, gpointer user_data) {
    auto* st = static_cast<FirmwareDialogState*>(user_data);
    GtkWidget* fc = gtk_file_chooser_dialog_new(
        "Select Firmware Package (FRW22411_V1.7.hex)",
        st->dlg_win,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        nullptr
    );
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "WEY Firmware Files (*.hex)");
    gtk_file_filter_add_pattern(filter, "*.hex");
    gtk_file_filter_add_pattern(filter, "*.HEX");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), filter);

    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        if (fn) {
            gtk_entry_set_text(GTK_ENTRY(st->entry_path), fn);
            st->TryLoadFile(fn);
            g_free(fn);
        }
    }
    gtk_widget_destroy(fc);
}

static void OnFwStartUpdateClicked(GtkWidget* btn, gpointer user_data) {
    auto* st = static_cast<FirmwareDialogState*>(user_data);
    if (!st->pkg_loaded) {
        ShowMessage(st->dlg_win, GTK_MESSAGE_WARNING, "No Firmware Loaded",
                    "Please select a valid FRW22411_V1.7.hex firmware file first.");
        return;
    }
    if (!AskConfirm(st->dlg_win, "Confirm Firmware Update",
                    "Are you sure you want to check and update the firmware on the connected WEY Deskswitch II?\n\n"
                    "Do not disconnect the USB cable during programming.")) {
        return;
    }

    bool apply_defaults = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->chk_default_settings));
    gtk_widget_set_sensitive(btn, FALSE);

    std::string msg;
    bool ok = UpdateSwitchFirmware(st->pkg, apply_defaults, msg, [&](double frac, const std::string& stage) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->prog_bar), frac);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(st->prog_bar), stage.c_str());
        while (gtk_events_pending()) gtk_main_iteration();
    });

    gtk_widget_set_sensitive(btn, TRUE);
    ShowMessage(st->dlg_win, ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
                ok ? "Firmware Update Complete" : "Firmware Update Failed", msg);
}

static void RunFirmwareUpdateDialog(GtkWindow* parent) {
    FirmwareDialogState st;
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Update Firmware (FRW22411)",
        parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Close", GTK_RESPONSE_CLOSE,
        nullptr
    );
    st.dlg_win = GTK_WINDOW(dlg);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 520, 320);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 10);

    GtkWidget* path_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX( path_hbox), gtk_label_new("Firmware File:"), FALSE, FALSE, 0);
    st.entry_path = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(st.entry_path), FALSE);
    gtk_box_pack_start(GTK_BOX(path_hbox), st.entry_path, TRUE, TRUE, 0);
    GtkWidget* btn_browse = gtk_button_new_with_label("Browse...");
    g_signal_connect(btn_browse, "clicked", G_CALLBACK(OnFwBrowseClicked), &st);
    gtk_box_pack_start(GTK_BOX(path_hbox), btn_browse, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), path_hbox, FALSE, FALSE, 0);

    st.lbl_summary = gtk_label_new("No firmware loaded.");
    gtk_label_set_xalign(GTK_LABEL(st.lbl_summary), 0.0f);
    gtk_box_pack_start(GTK_BOX(content), st.lbl_summary, FALSE, FALSE, 0);

    st.chk_default_settings = gtk_check_button_new_with_label("Default Settings (reset configuration to factory defaults)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st.chk_default_settings), FALSE);
    gtk_box_pack_start(GTK_BOX(content), st.chk_default_settings, FALSE, FALSE, 0);

    st.prog_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(st.prog_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(st.prog_bar), "Ready");
    gtk_box_pack_start(GTK_BOX(content), st.prog_bar, FALSE, FALSE, 0);

    GtkWidget* btn_update = gtk_button_new_with_label("Start Firmware Update");
    g_signal_connect(btn_update, "clicked", G_CALLBACK(OnFwStartUpdateClicked), &st);
    gtk_box_pack_start(GTK_BOX(content), btn_update, FALSE, FALSE, 0);

    std::string bundled = FindBundledFirmwareHex();
    if (!bundled.empty()) {
        gtk_entry_set_text(GTK_ENTRY(st.entry_path), bundled.c_str());
        st.TryLoadFile(bundled);
    }

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ============================================================================
// Main Application Window (TForm_Main)
// ============================================================================
struct MainAppState {
    GtkWindow* main_win = nullptr;
    T_Config11 cfg;
    std::string current_filepath;

    GtkWidget* lbl_file_status = nullptr;
    GtkWidget* entry_configuration = nullptr;
    GtkWidget* entry_client = nullptr;
    GtkWidget* entry_project = nullptr;
    GtkWidget* chk_mouseswitch_enabled = nullptr;
    GtkWidget* lbl_summary_info = nullptr;

    GtkWidget* btn_download_from_switch = nullptr;
    GtkWidget* btn_upload_to_switch = nullptr;

    GtkWidget* lbl_usb_status = nullptr;
    GtkWidget* btn_grant_usb = nullptr;
    GtkWidget* progress_bar = nullptr;

    bool updating_ui = false;

    void LoadConfigIntoUI() {
        updating_ui = true;
        gtk_entry_set_text(GTK_ENTRY(entry_configuration), GetShortString(cfg.Configuration).c_str());
        gtk_entry_set_text(GTK_ENTRY(entry_client), GetShortString(cfg.Client).c_str());
        gtk_entry_set_text(GTK_ENTRY(entry_project), GetShortString(cfg.Project).c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_mouseswitch_enabled), cfg.MouseSwitch_Version >= 10);
        UpdateSummaryLabel();
        UpdateFileStatusLabel();
        updating_ui = false;
    }

    void SaveUIIntoConfig() {
        SetShortString(cfg.Configuration, gtk_entry_get_text(GTK_ENTRY(entry_configuration)));
        SetShortString(cfg.Client, gtk_entry_get_text(GTK_ENTRY(entry_client)));
        SetShortString(cfg.Project, gtk_entry_get_text(GTK_ENTRY(entry_project)));
        cfg.MouseSwitch_Version = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_mouseswitch_enabled)) ? 10 : 0;
        cfg.Version = 11;
    }

    void UpdateFileStatusLabel() {
        if (!lbl_file_status) return;
        std::string txt = current_filepath.empty()
            ? "File: [Unsaved / In-Memory Configuration]"
            : ("File: " + current_filepath);
        gtk_label_set_text(GTK_LABEL(lbl_file_status), txt.c_str());
    }

    void UpdateSummaryLabel() {
        if (!lbl_summary_info) return;
        int active_events = 0;
        for (int i = 0; i < 16; ++i) {
            if (cfg.Event[i].Active == 1) active_events++;
        }
        int active_jiggle = 0;
        for (int i = 0; i < 4; ++i) {
            if (cfg.MouseJiggle[i].Enable != 0) active_jiggle++;
        }
        int active_monitors = 0;
        for (int i = 0; i < 128; ++i) {
            if (cfg.Monitor[i].Source != 0) active_monitors++;
        }
        std::ostringstream oss;
        oss << "Startup Source: " << (cfg.Start_Source == 0 ? "none" : std::to_string(cfg.Start_Source))
            << "   |   Mouse Jiggle: " << active_jiggle << "/4 active"
            << "   |   Hotkey Events: " << active_events << "/16"
            << "   |   Mouseswitch Monitors: " << active_monitors
            << (cfg.MouseSwitch_Version >= 10 ? " (Enabled)" : " (Disabled)");
        gtk_label_set_text(GTK_LABEL(lbl_summary_info), oss.str().c_str());
    }

    void RefreshUsbStatus() {
        UsbStatusInfo st = CheckUsbDeviceStatus();
        if (st.status == UsbPresenceStatus::ConnectedReady) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "<span foreground='#1b7e20'><b>● USB Connected &amp; Ready</b> — WEY Deskswitch II (1b07:0010 at %s)</span>",
                st.dev_path.c_str());
            gtk_label_set_markup(GTK_LABEL(lbl_usb_status), buf);
            gtk_widget_set_visible(btn_grant_usb, FALSE);
        } else if (st.status == UsbPresenceStatus::ConnectedNoPermission) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "<span foreground='#b55b00'><b>● USB Connected (Permission Needed)</b> — WEY Deskswitch II at %s</span>",
                st.dev_path.c_str());
            gtk_label_set_markup(GTK_LABEL(lbl_usb_status), buf);
            gtk_widget_set_visible(btn_grant_usb, TRUE);
        } else {
            gtk_label_set_markup(GTK_LABEL(lbl_usb_status),
                "<span foreground='#9e1c1c'><b>○ USB Not Connected</b> — Plug in WEY USB-Deskswitch II (1b07:0010)</span>");
            gtk_widget_set_visible(btn_grant_usb, FALSE);
        }
    }

    bool EnsureUsbPermissionInteractive() {
        UsbStatusInfo st = CheckUsbDeviceStatus();
        if (st.status == UsbPresenceStatus::ConnectedNoPermission) {
            if (AskConfirm(main_win, "USB Permission Required",
                           "The WEY Deskswitch II is connected (" + st.dev_path + "), "
                           "but your Linux user does not yet have write permission to it.\n\n"
                           "Would you like to install the udev rule (/etc/udev/rules.d/99-wey-deskswitch.rules) "
                           "and grant access now?")) {
                std::string err;
                if (!InstallUdevRuleAndFixPermissions(err)) {
                    ShowMessage(main_win, GTK_MESSAGE_ERROR, "Failed to Grant USB Permission", err);
                    return false;
                }
                RefreshUsbStatus();
            } else {
                return false;
            }
        }
        return true;
    }
};

static gboolean OnUsbStatusTimer(gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    app->RefreshUsbStatus();
    return TRUE;
}

static void OnGrantUsbClicked(GtkWidget* /*btn*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    std::string err;
    if (InstallUdevRuleAndFixPermissions(err)) {
        app->RefreshUsbStatus();
        ShowMessage(app->main_win, GTK_MESSAGE_INFO, "USB Access Granted",
                    "Installed /etc/udev/rules.d/99-wey-deskswitch.rules and enabled read/write access to the WEY Deskswitch II.");
    } else {
        ShowMessage(app->main_win, GTK_MESSAGE_ERROR, "Permission Setup Failed", err);
    }
}

static void OnFileNew(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    DefaultValues(app->cfg);
    app->current_filepath.clear();
    app->LoadConfigIntoUI();
}

static void OnFileOpen(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    GtkWidget* fc = gtk_file_chooser_dialog_new(
        "Open .Wey22411 Configuration File",
        app->main_win,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        nullptr
    );
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "WEY Deskswitch II Config (*.Wey22411)");
    gtk_file_filter_add_pattern(filter, "*.Wey22411");
    gtk_file_filter_add_pattern(filter, "*.wey22411");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), filter);

    GtkFileFilter* all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Files (*)");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), all_filter);

    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        if (fn) {
            T_Config11 loaded;
            std::string err;
            if (LoadConfigFile(fn, loaded, err)) {
                app->cfg = loaded;
                app->current_filepath = fn;
                app->LoadConfigIntoUI();
            } else {
                ShowMessage(app->main_win, GTK_MESSAGE_ERROR, "Error Opening File", err);
            }
            g_free(fn);
        }
    }
    gtk_widget_destroy(fc);
}

static void DoFileSaveAs(MainAppState* app) {
    app->SaveUIIntoConfig();
    GtkWidget* fc = gtk_file_chooser_dialog_new(
        "Save .Wey22411 Configuration File",
        app->main_win,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        nullptr
    );
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "WEY Deskswitch II Config (*.Wey22411)");
    gtk_file_filter_add_pattern(filter, "*.Wey22411");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), filter);

    if (!app->current_filepath.empty()) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(fc), app->current_filepath.c_str());
    } else {
        std::string def_name = GetShortString(app->cfg.Configuration);
        if (def_name.empty()) def_name = "config";
        def_name += ".Wey22411";
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), def_name.c_str());
    }

    if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        if (fn) {
            std::string path(fn);
            g_free(fn);
            if (path.size() < 9 || (path.substr(path.size() - 9) != ".Wey22411" && path.substr(path.size() - 9) != ".wey22411")) {
                path += ".Wey22411";
            }
            std::string err;
            if (SaveConfigFile(path, app->cfg, err)) {
                app->current_filepath = path;
                app->UpdateFileStatusLabel();
            } else {
                ShowMessage(app->main_win, GTK_MESSAGE_ERROR, "Error Saving File", err);
            }
        }
    }
    gtk_widget_destroy(fc);
}

static void OnFileSave(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    app->SaveUIIntoConfig();
    if (app->current_filepath.empty()) {
        DoFileSaveAs(app);
    } else {
        std::string err;
        if (!SaveConfigFile(app->current_filepath, app->cfg, err)) {
            ShowMessage(app->main_win, GTK_MESSAGE_ERROR, "Error Saving File", err);
        }
    }
}

static void OnFileSaveAs(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    DoFileSaveAs(app);
}

static void OnDefaultConfigClicked(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    DefaultValues(app->cfg);
    app->LoadConfigIntoUI();
}

static void OnOpenMouseJiggleClicked(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    app->SaveUIIntoConfig();
    if (RunMouseJiggleDialog(app->main_win, app->cfg)) {
        app->UpdateSummaryLabel();
    }
}

static void OnOpenEventClicked(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    app->SaveUIIntoConfig();
    if (RunEventDialog(app->main_win, app->cfg)) {
        app->UpdateSummaryLabel();
    }
}

static void OnOpenMouseswitchClicked(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    app->SaveUIIntoConfig();
    if (RunMouseswitchDialog(app->main_win, app->cfg)) {
        app->UpdateSummaryLabel();
    }
}

static void OnMouseswitchEnabledToggled(GtkToggleButton* btn, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    if (app->updating_ui) return;
    app->cfg.MouseSwitch_Version = gtk_toggle_button_get_active(btn) ? 10 : 0;
    app->UpdateSummaryLabel();
}

// ============================================================================
// REVERSED BUTTONS (User Request):
// - "Download" = Download configuration FROM the Switch TO the PC
// - "Upload"   = Upload configuration FROM the PC TO the Switch
// ============================================================================
static void OnDownloadFromSwitchClicked(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    if (!app->EnsureUsbPermissionInteractive()) return;

    gtk_widget_set_sensitive(app->btn_download_from_switch, FALSE);
    gtk_widget_set_sensitive(app->btn_upload_to_switch, FALSE);

    T_Config11 downloaded;
    std::string msg;
    bool ok = DownloadConfigFromSwitch(downloaded, msg, [&](double frac, const std::string& stage) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), frac);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), stage.c_str());
        while (gtk_events_pending()) gtk_main_iteration();
    });

    gtk_widget_set_sensitive(app->btn_download_from_switch, TRUE);
    gtk_widget_set_sensitive(app->btn_upload_to_switch, TRUE);
    app->RefreshUsbStatus();

    if (ok) {
        app->cfg = downloaded;
        app->LoadConfigIntoUI();
        ShowMessage(app->main_win, GTK_MESSAGE_INFO, "Download from Switch Complete", msg);
    } else {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), "Download failed");
        ShowMessage(app->main_win, GTK_MESSAGE_ERROR, "Download from Switch Failed", msg);
    }
}

static void OnUploadToSwitchClicked(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    app->SaveUIIntoConfig();

    // If Mouseswitch is enabled, validate monitor positions first
    if (app->cfg.MouseSwitch_Version >= 10 && !CheckMonitorPosition(app->cfg)) {
        ShowMessage(app->main_win, GTK_MESSAGE_WARNING, "Illegal Monitor Position",
                    "Mouseswitch is enabled, but the Monitor Arrangement is empty or invalid.\n\n"
                    "Please open 'Mouseswitch...' to configure valid monitor positions, or uncheck 'Enabled'.");
        return;
    }

    if (!app->EnsureUsbPermissionInteractive()) return;

    gtk_widget_set_sensitive(app->btn_download_from_switch, FALSE);
    gtk_widget_set_sensitive(app->btn_upload_to_switch, FALSE);

    std::string msg;
    bool ok = UploadConfigToSwitch(app->cfg, msg, [&](double frac, const std::string& stage) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), frac);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), stage.c_str());
        while (gtk_events_pending()) gtk_main_iteration();
    });

    gtk_widget_set_sensitive(app->btn_download_from_switch, TRUE);
    gtk_widget_set_sensitive(app->btn_upload_to_switch, TRUE);
    app->RefreshUsbStatus();

    if (ok) {
        ShowMessage(app->main_win, GTK_MESSAGE_INFO, "Upload to Switch Complete", msg);
    } else {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), "Upload failed");
        ShowMessage(app->main_win, GTK_MESSAGE_ERROR, "Upload to Switch Failed", msg);
    }
}

static void OnResetSwitchDefaultsMenu(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    if (!AskConfirm(app->main_win, "Reset Switch to Default Settings",
                    "Do you want to reset the connected WEY Deskswitch II hardware and editor to factory default settings?")) {
        return;
    }
    if (!app->EnsureUsbPermissionInteractive()) return;

    T_Config11 def_cfg;
    std::string msg;
    bool ok = ResetSwitchToDefaultSettings(def_cfg, msg, [&](double frac, const std::string& stage) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), frac);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), stage.c_str());
        while (gtk_events_pending()) gtk_main_iteration();
    });

    app->RefreshUsbStatus();
    if (ok) {
        app->cfg = def_cfg;
        app->LoadConfigIntoUI();
        ShowMessage(app->main_win, GTK_MESSAGE_INFO, "Default Settings Restored", msg);
    } else {
        ShowMessage(app->main_win, GTK_MESSAGE_ERROR, "Reset Failed", msg);
    }
}

static void OnFirmwareUpdateMenu(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    RunFirmwareUpdateDialog(app->main_win);
    app->RefreshUsbStatus();
}

static void OnHistoryMenu(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    RunHistoryDialog(app->main_win);
}

static void OnLicenseMenu(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    RunLicenseDialog(app->main_win);
}

static void OnAboutMenu(GtkWidget* /*w*/, gpointer user_data) {
    auto* app = static_cast<MainAppState*>(user_data);
    RunAboutDialog(app->main_win);
    app->RefreshUsbStatus();
}

// ============================================================================
// CLI Support (for scripting, diagnostics, and verification)
// ============================================================================
static void PrintCliHelp(const char* prog) {
    std::printf(
        "Newey — Configuration & Firmware Utility for USB-Deskswitch II 1:4 (22411R / 22411S)\n"
        "License: GPL-3.0-or-later WITH Commons-Clause-1.0 (Open Source Copyleft, Commercial Use Allowed, Resale Prohibited)\n\n"
        "Usage:\n"
        "  %s [config.Wey22411]              Launch Newey GUI (optionally loading a .Wey22411 file)\n"
        "  %s --download <out.Wey22411>      Download config FROM switch TO PC file\n"
        "  %s --upload <in.Wey22411>         Upload config FROM PC file TO switch\n"
        "  %s --read-info                    Query switch firmware & ICSP versions\n"
        "  %s --status                       Check USB connection & permission status\n"
        "  %s --dump-config <in.Wey22411>    Inspect contents of a .Wey22411 file\n"
        "  %s --version                      Print version and license information\n"
        "  %s --help                         Show this help message\n",
        prog, prog, prog, prog, prog, prog, prog, prog
    );
}

static int DumpConfigCli(const std::string& path) {
    T_Config11 cfg;
    std::string err;
    if (!LoadConfigFile(path, cfg, err)) {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }
    std::printf("File: %s (Version %u.%u)\n", path.c_str(), cfg.Version / 10, cfg.Version % 10);
    std::printf("  Configuration: '%s'\n", GetShortString(cfg.Configuration).c_str());
    std::printf("  Client:        '%s'\n", GetShortString(cfg.Client).c_str());
    std::printf("  Project:       '%s'\n", GetShortString(cfg.Project).c_str());
    std::printf("  Start_Source:  %u\n", cfg.Start_Source);
    for (int i = 0; i < 4; ++i) {
        std::printf("  MouseJiggle[%d]: Enable=%u, Interval=%u s\n",
                    i + 1, cfg.MouseJiggle[i].Enable, cfg.MouseJiggle[i].Interval);
    }
    for (int i = 0; i < 16; ++i) {
        if (cfg.Event[i].Active) {
            std::printf("  Event[%d]: Keys=[%s, %s, %s] -> Source=%d\n",
                        i + 1,
                        GetKeyName(cfg.Event[i].Key1).c_str(),
                        GetKeyName(cfg.Event[i].Key2).c_str(),
                        GetKeyName(cfg.Event[i].Key3).c_str(),
                        cfg.Event[i].Source == 0xFF ? -1 : cfg.Event[i].Source);
        }
    }
    std::printf("  Mouseswitch:   %s (Switch_Over=0x%02X)\n",
                cfg.MouseSwitch_Version >= 10 ? "Enabled" : "Disabled", cfg.Switch_Over);
    return 0;
}

int main(int argc, char* argv[]) {
    std::string initial_file;

    // Handle CLI arguments first so headless usage works without X11/Wayland
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintCliHelp(argv[0]);
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::printf("Newey 1.7 — Configuration & Firmware Utility for USB-Deskswitch II 1:4\n"
                        "License: GPL-3.0-or-later WITH Commons-Clause-1.0\n");
            return 0;
        }
        if (arg == "--status") {
            UsbStatusInfo st = CheckUsbDeviceStatus();
            if (st.status == UsbPresenceStatus::ConnectedReady) {
                std::printf("CONNECTED_READY (%s)\n", st.dev_path.c_str());
                return 0;
            } else if (st.status == UsbPresenceStatus::ConnectedNoPermission) {
                std::printf("CONNECTED_NO_PERMISSION (%s)\n", st.dev_path.c_str());
                return 2;
            } else {
                std::printf("NOT_CONNECTED\n");
                return 1;
            }
        }
        if (arg == "--read-info") {
            SwitchDeviceInfo info;
            std::string msg;
            if (!ReadSwitchDeviceInfo(info, msg, [](double f, const std::string& s) {
                std::fprintf(stderr, "\r[%3.0f%%] %s   ", f * 100.0, s.c_str());
            })) {
                std::fprintf(stderr, "\nError: %s\n", msg.c_str());
                return 1;
            }
            std::fprintf(stderr, "\n");
            std::printf("Product ID: %u / 0x%04X (Bus %u, Device %u)\n",
                        info.product_id, info.product_id, info.bus_number, info.device_address);
            std::printf("  %-24s  FW: %-6s  ICSP: %-6s\n",
                        info.chips[0].name.c_str(),
                        FormatVer(info.chips[0].fw_version).c_str(),
                        FormatVer(info.chips[0].icsp_version).c_str());
            return 0;
        }
        if (arg == "--download" && i + 1 < argc) {
            std::string out_path = argv[++i];
            T_Config11 cfg;
            std::string msg;
            if (!DownloadConfigFromSwitch(cfg, msg, [](double f, const std::string& s) {
                std::fprintf(stderr, "\r[%3.0f%%] %s   ", f * 100.0, s.c_str());
            })) {
                std::fprintf(stderr, "\nError: %s\n", msg.c_str());
                return 1;
            }
            std::fprintf(stderr, "\n");
            std::string err;
            if (!SaveConfigFile(out_path, cfg, err)) {
                std::fprintf(stderr, "Error saving file: %s\n", err.c_str());
                return 1;
            }
            std::printf("Successfully downloaded configuration from switch to '%s'.\n", out_path.c_str());
            return 0;
        }
        if (arg == "--upload" && i + 1 < argc) {
            std::string in_path = argv[++i];
            T_Config11 cfg;
            std::string err;
            if (!LoadConfigFile(in_path, cfg, err)) {
                std::fprintf(stderr, "Error loading file: %s\n", err.c_str());
                return 1;
            }
            std::string msg;
            if (!UploadConfigToSwitch(cfg, msg, [](double f, const std::string& s) {
                std::fprintf(stderr, "\r[%3.0f%%] %s   ", f * 100.0, s.c_str());
            })) {
                std::fprintf(stderr, "\nError: %s\n", msg.c_str());
                return 1;
            }
            std::fprintf(stderr, "\n");
            std::printf("Successfully uploaded configuration '%s' to switch.\n", in_path.c_str());
            return 0;
        }
        if (arg == "--dump-config" && i + 1 < argc) {
            return DumpConfigCli(argv[++i]);
        }
        if (!arg.empty() && arg[0] != '-') {
            initial_file = arg;
        }
    }

    gtk_init(&argc, &argv);

    MainAppState app;
    DefaultValues(app.cfg);

    if (!initial_file.empty()) {
        T_Config11 loaded;
        std::string err;
        if (LoadConfigFile(initial_file, loaded, err)) {
            app.cfg = loaded;
            app.current_filepath = initial_file;
        }
    }

    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app.main_win = GTK_WINDOW(win);
    gtk_window_set_title(GTK_WINDOW(win), "Newey — USB-Deskswitch II 1:4");
    gtk_window_set_default_size(GTK_WINDOW(win), 620, 520);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    GdkPixbuf* logo_icon = LoadWeyLogoPixbuf();
    if (logo_icon) {
        gtk_window_set_icon(GTK_WINDOW(win), logo_icon);
    }

    GtkWidget* root_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(win), root_vbox);

    // ========================================================================
    // Menu Bar (matches TForm_Main MainMenu1)
    // ========================================================================
    GtkWidget* menubar = gtk_menu_bar_new();
    gtk_box_pack_start(GTK_BOX(root_vbox), menubar, FALSE, FALSE, 0);

    // File Menu
    GtkWidget* menu_file = gtk_menu_new();
    GtkWidget* mi_file = gtk_menu_item_new_with_mnemonic("_File");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_file), menu_file);

    GtkWidget* mi_new = gtk_menu_item_new_with_mnemonic("_New");
    g_signal_connect(mi_new, "activate", G_CALLBACK(OnFileNew), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), mi_new);

    GtkWidget* mi_open = gtk_menu_item_new_with_mnemonic("_Open (.Wey22411)...");
    g_signal_connect(mi_open, "activate", G_CALLBACK(OnFileOpen), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), mi_open);

    GtkWidget* mi_save = gtk_menu_item_new_with_mnemonic("_Save");
    g_signal_connect(mi_save, "activate", G_CALLBACK(OnFileSave), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), mi_save);

    GtkWidget* mi_save_as = gtk_menu_item_new_with_mnemonic("Save _As...");
    g_signal_connect(mi_save_as, "activate", G_CALLBACK(OnFileSaveAs), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), mi_save_as);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), gtk_separator_menu_item_new());

    GtkWidget* mi_exit = gtk_menu_item_new_with_mnemonic("E_xit");
    g_signal_connect(mi_exit, "activate", G_CALLBACK(gtk_main_quit), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), mi_exit);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_file);

    // Firmware Menu
    GtkWidget* menu_fw = gtk_menu_new();
    GtkWidget* mi_fw = gtk_menu_item_new_with_mnemonic("F_irmware");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_fw), menu_fw);

    GtkWidget* mi_fw_update = gtk_menu_item_new_with_mnemonic("_Update Firmware...");
    g_signal_connect(mi_fw_update, "activate", G_CALLBACK(OnFirmwareUpdateMenu), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_fw), mi_fw_update);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_fw);

    // Settings Menu
    GtkWidget* menu_settings = gtk_menu_new();
    GtkWidget* mi_settings = gtk_menu_item_new_with_mnemonic("_Settings");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_settings), menu_settings);

    GtkWidget* mi_cfg_sub = gtk_menu_item_new_with_mnemonic("_Configuration");
    GtkWidget* menu_cfg_sub = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_cfg_sub), menu_cfg_sub);

    GtkWidget* mi_jiggle = gtk_menu_item_new_with_mnemonic("_Mouse Jiggle...");
    g_signal_connect(mi_jiggle, "activate", G_CALLBACK(OnOpenMouseJiggleClicked), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_cfg_sub), mi_jiggle);

    GtkWidget* mi_event = gtk_menu_item_new_with_mnemonic("_Event (Hotkeys & Startup)...");
    g_signal_connect(mi_event, "activate", G_CALLBACK(OnOpenEventClicked), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_cfg_sub), mi_event);

    GtkWidget* mi_ms = gtk_menu_item_new_with_mnemonic("M_ouseswitch (Monitor Layout)...");
    g_signal_connect(mi_ms, "activate", G_CALLBACK(OnOpenMouseswitchClicked), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_cfg_sub), mi_ms);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu_settings), mi_cfg_sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_settings), gtk_separator_menu_item_new());

    GtkWidget* mi_def_switch = gtk_menu_item_new_with_mnemonic("_Default Settings (Reset Switch)...");
    g_signal_connect(mi_def_switch, "activate", G_CALLBACK(OnResetSwitchDefaultsMenu), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_settings), mi_def_switch);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_settings);

    // Info Menu
    GtkWidget* menu_info = gtk_menu_new();
    GtkWidget* mi_info = gtk_menu_item_new_with_mnemonic("_Info");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_info), menu_info);

    GtkWidget* mi_history = gtk_menu_item_new_with_mnemonic("_History...");
    g_signal_connect(mi_history, "activate", G_CALLBACK(OnHistoryMenu), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_info), mi_history);

    GtkWidget* mi_license = gtk_menu_item_new_with_mnemonic("_License...");
    g_signal_connect(mi_license, "activate", G_CALLBACK(OnLicenseMenu), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_info), mi_license);

    GtkWidget* mi_about = gtk_menu_item_new_with_mnemonic("_About Newey / Read Device Info...");
    g_signal_connect(mi_about, "activate", G_CALLBACK(OnAboutMenu), &app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_info), mi_about);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_info);

    // ========================================================================
    // Main Body Content
    // ========================================================================
    GtkWidget* body_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(body_vbox), 14);
    gtk_box_pack_start(GTK_BOX(root_vbox), body_vbox, TRUE, TRUE, 0);

    // Header Banner with WEY Logo + Quick File Buttons
    GtkWidget* header_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    if (logo_icon) {
        GtkWidget* logo_img = gtk_image_new_from_pixbuf(logo_icon);
        gtk_box_pack_start(GTK_BOX(header_hbox), logo_img, FALSE, FALSE, 0);
        g_object_unref(logo_icon);
    }

    GtkWidget* header_text_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* title_lbl = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title_lbl),
        "<span size='x-large' weight='bold'>Newey</span>\n"
        "<span size='small'>Configuration &amp; Firmware Utility for <b>USB-Deskswitch II 1:4</b> (22411R / 22411S)</span>");
    gtk_label_set_xalign(GTK_LABEL(title_lbl), 0.0f);
    gtk_box_pack_start(GTK_BOX(header_text_vbox), title_lbl, FALSE, FALSE, 0);

    app.lbl_file_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app.lbl_file_status), 0.0f);
    gtk_box_pack_start(GTK_BOX(header_text_vbox), app.lbl_file_status, FALSE, FALSE, 0);

    GtkWidget* file_btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* btn_open_file = gtk_button_new_with_label("Open File...");
    g_signal_connect(btn_open_file, "clicked", G_CALLBACK(OnFileOpen), &app);
    gtk_box_pack_start(GTK_BOX(file_btn_hbox), btn_open_file, FALSE, FALSE, 0);

    GtkWidget* btn_save_file = gtk_button_new_with_label("Save File");
    g_signal_connect(btn_save_file, "clicked", G_CALLBACK(OnFileSave), &app);
    gtk_box_pack_start(GTK_BOX(file_btn_hbox), btn_save_file, FALSE, FALSE, 0);

    GtkWidget* btn_save_as_file = gtk_button_new_with_label("Save As...");
    g_signal_connect(btn_save_as_file, "clicked", G_CALLBACK(OnFileSaveAs), &app);
    gtk_box_pack_start(GTK_BOX(file_btn_hbox), btn_save_as_file, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header_text_vbox), file_btn_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_hbox), header_text_vbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(body_vbox), header_hbox, FALSE, FALSE, 0);

    // ========================================================================
    // Configuration Frame (matches Panel_Config)
    // ========================================================================
    GtkWidget* frame_cfg = gtk_frame_new("Configuration");
    GtkWidget* cfg_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(cfg_vbox), 12);
    gtk_container_add(GTK_CONTAINER(frame_cfg), cfg_vbox);
    gtk_box_pack_start(GTK_BOX(body_vbox), frame_cfg, TRUE, TRUE, 0);

    GtkWidget* fields_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(fields_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(fields_grid), 12);
    gtk_box_pack_start(GTK_BOX(cfg_vbox), fields_grid, FALSE, FALSE, 0);

    GtkWidget* lbl_c1 = gtk_label_new("Configuration:");
    gtk_label_set_xalign(GTK_LABEL(lbl_c1), 0.0f);
    gtk_grid_attach(GTK_GRID(fields_grid), lbl_c1, 0, 0, 1, 1);
    app.entry_configuration = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(app.entry_configuration), 50);
    gtk_widget_set_hexpand(app.entry_configuration, TRUE);
    gtk_grid_attach(GTK_GRID(fields_grid), app.entry_configuration, 1, 0, 1, 1);

    GtkWidget* btn_default = gtk_button_new_with_label("Default");
    gtk_widget_set_tooltip_text(btn_default, "Reset configuration fields in the editor to factory default values");
    g_signal_connect(btn_default, "clicked", G_CALLBACK(OnDefaultConfigClicked), &app);
    gtk_grid_attach(GTK_GRID(fields_grid), btn_default, 2, 0, 1, 1);

    GtkWidget* lbl_c2 = gtk_label_new("Client:");
    gtk_label_set_xalign(GTK_LABEL(lbl_c2), 0.0f);
    gtk_grid_attach(GTK_GRID(fields_grid), lbl_c2, 0, 1, 1, 1);
    app.entry_client = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(app.entry_client), 50);
    gtk_widget_set_hexpand(app.entry_client, TRUE);
    gtk_grid_attach(GTK_GRID(fields_grid), app.entry_client, 1, 1, 2, 1);

    GtkWidget* lbl_c3 = gtk_label_new("Project:");
    gtk_label_set_xalign(GTK_LABEL(lbl_c3), 0.0f);
    gtk_grid_attach(GTK_GRID(fields_grid), lbl_c3, 0, 2, 1, 1);
    app.entry_project = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(app.entry_project), 50);
    gtk_widget_set_hexpand(app.entry_project, TRUE);
    gtk_grid_attach(GTK_GRID(fields_grid), app.entry_project, 1, 2, 2, 1);

    gtk_box_pack_start(GTK_BOX(cfg_vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 2);

    // Sub-dialog Buttons (Mouse Jiggle, Event, Mouseswitch + Enabled)
    GtkWidget* sub_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(sub_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(sub_grid), 12);
    gtk_box_pack_start(GTK_BOX(cfg_vbox), sub_grid, FALSE, FALSE, 0);

    GtkWidget* btn_jiggle = gtk_button_new_with_label("Mouse Jiggle...");
    gtk_widget_set_size_request(btn_jiggle, 180, 34);
    g_signal_connect(btn_jiggle, "clicked", G_CALLBACK(OnOpenMouseJiggleClicked), &app);
    gtk_grid_attach(GTK_GRID(sub_grid), btn_jiggle, 0, 0, 1, 1);

    GtkWidget* btn_event = gtk_button_new_with_label("Event (Hotkeys)...");
    gtk_widget_set_size_request(btn_event, 180, 34);
    g_signal_connect(btn_event, "clicked", G_CALLBACK(OnOpenEventClicked), &app);
    gtk_grid_attach(GTK_GRID(sub_grid), btn_event, 0, 1, 1, 1);

    GtkWidget* btn_mouseswitch = gtk_button_new_with_label("Mouseswitch...");
    gtk_widget_set_size_request(btn_mouseswitch, 180, 34);
    g_signal_connect(btn_mouseswitch, "clicked", G_CALLBACK(OnOpenMouseswitchClicked), &app);
    gtk_grid_attach(GTK_GRID(sub_grid), btn_mouseswitch, 0, 2, 1, 1);

    app.chk_mouseswitch_enabled = gtk_check_button_new_with_label("Enabled (seamless screen-edge mouse switching)");
    g_signal_connect(app.chk_mouseswitch_enabled, "toggled", G_CALLBACK(OnMouseswitchEnabledToggled), &app);
    gtk_grid_attach(GTK_GRID(sub_grid), app.chk_mouseswitch_enabled, 1, 2, 1, 1);

    app.lbl_summary_info = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app.lbl_summary_info), 0.0f);
    gtk_box_pack_start(GTK_BOX(cfg_vbox), app.lbl_summary_info, FALSE, FALSE, 2);

    // ========================================================================
    // Switch Transfer Frame (Reversed Download / Upload as requested!)
    // ========================================================================
    GtkWidget* frame_xfer = gtk_frame_new("Deskswitch Transfer (USB)");
    GtkWidget* xfer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(xfer_vbox), 10);
    gtk_container_add(GTK_CONTAINER(frame_xfer), xfer_vbox);
    gtk_box_pack_start(GTK_BOX(body_vbox), frame_xfer, FALSE, FALSE, 0);

    GtkWidget* xfer_btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_set_homogeneous(GTK_BOX(xfer_btn_hbox), TRUE);
    gtk_box_pack_start(GTK_BOX(xfer_vbox), xfer_btn_hbox, FALSE, FALSE, 0);

    // Download = Switch -> PC
    app.btn_download_from_switch = gtk_button_new_with_label("⬇  Download (from Switch to PC)");
    gtk_widget_set_size_request(app.btn_download_from_switch, -1, 40);
    gtk_widget_set_tooltip_text(app.btn_download_from_switch,
        "Download the active configuration from the connected WEY Deskswitch II into this PC application");
    g_signal_connect(app.btn_download_from_switch, "clicked", G_CALLBACK(OnDownloadFromSwitchClicked), &app);
    gtk_box_pack_start(GTK_BOX(xfer_btn_hbox), app.btn_download_from_switch, TRUE, TRUE, 0);

    // Upload = PC -> Switch
    app.btn_upload_to_switch = gtk_button_new_with_label("⬆  Upload (from PC to Switch)");
    gtk_widget_set_size_request(app.btn_upload_to_switch, -1, 40);
    gtk_widget_set_tooltip_text(app.btn_upload_to_switch,
        "Upload the current configuration from this PC application to the connected WEY Deskswitch II");
    g_signal_connect(app.btn_upload_to_switch, "clicked", G_CALLBACK(OnUploadToSwitchClicked), &app);
    gtk_box_pack_start(GTK_BOX(xfer_btn_hbox), app.btn_upload_to_switch, TRUE, TRUE, 0);

    app.progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app.progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app.progress_bar), "Ready");
    gtk_box_pack_start(GTK_BOX(xfer_vbox), app.progress_bar, FALSE, FALSE, 0);

    // ========================================================================
    // Bottom USB Connection Status Bar
    // ========================================================================
    GtkWidget* status_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(body_vbox), status_hbox, FALSE, FALSE, 0);

    app.lbl_usb_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app.lbl_usb_status), 0.0f);
    gtk_box_pack_start(GTK_BOX(status_hbox), app.lbl_usb_status, TRUE, TRUE, 0);

    app.btn_grant_usb = gtk_button_new_with_label("Grant USB Access (udev)");
    g_signal_connect(app.btn_grant_usb, "clicked", G_CALLBACK(OnGrantUsbClicked), &app);
    gtk_box_pack_end(GTK_BOX(status_hbox), app.btn_grant_usb, FALSE, FALSE, 0);

    app.LoadConfigIntoUI();
    gtk_widget_show_all(win);
    app.RefreshUsbStatus();

    g_timeout_add(1500, OnUsbStatusTimer, &app);
    gtk_main();

    return 0;
}
