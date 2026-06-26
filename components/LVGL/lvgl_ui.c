/**
 * @file lvgl_ui.c
 * @brief LVGL v9.5 sensor dashboard UI for agricultural IoT gateway
 *
 * 3-tab layout (320x240 ILI9341):
 *   Tab "Environ" — SHT30 temp/humidity + BH1750 lux + sub-node data
 *   Tab "Soil" — Soil sensor 8-parameter grid (2x4)
 *   Tab "Status" — System status (WiFi/MQTT/LoRa/OTA/IP/FW)
 */

#include "lvgl_ui.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ========== Colours & theme ========== */
#define COL_BG          lv_color_hex(0x1A1A2E)   /* deep navy background */
#define COL_PANEL       lv_color_hex(0x16213E)   /* panel card bg */
#define COL_ACCENT      lv_color_hex(0x0F3460)   /* accent / header bar */
#define COL_GREEN       lv_color_hex(0x4ECB71)   /* OK / temperature */
#define COL_BLUE        lv_color_hex(0x3B82F6)   /* humidity */
#define COL_ORANGE      lv_color_hex(0xF59E0B)   /* lux / light */
#define COL_RED         lv_color_hex(0xEF4444)   /* error / offline */
#define COL_GREY        lv_color_hex(0x9CA3AF)   /* no-data placeholder */
#define COL_WHITE       lv_color_hex(0xF1F5F9)   /* text on dark bg */
#define COL_TAB_ACTIVE  lv_color_hex(0x3B82F6)
#define COL_TAB_INACTIVE lv_color_hex(0x334155)

/* ========== Global UI object references ========== */

/* -- Tab 1: Environment -- */
static lv_obj_t *g_label_env_temp;    /* e.g. "25.3 C" */
static lv_obj_t *g_bar_env_humi;      /* humidity bar */
static lv_obj_t *g_label_env_humi;    /* e.g. "65 %" */
static lv_obj_t *g_label_env_lux;     /* e.g. "1234 Lux" */
static lv_obj_t *g_label_sub_temp;    /* sub-node temperature */
static lv_obj_t *g_label_sub_humi;    /* sub-node humidity */
static lv_obj_t *g_label_sub_lux;     /* sub-node light */

/* -- Tab 2: Soil -- */
static lv_obj_t *g_label_soil_temp;
static lv_obj_t *g_label_soil_humi;
static lv_obj_t *g_label_soil_ec;
static lv_obj_t *g_label_soil_ph;
static lv_obj_t *g_label_soil_n;
static lv_obj_t *g_label_soil_p;
static lv_obj_t *g_label_soil_k;
static lv_obj_t *g_label_soil_sal;

/* -- Tab 3: System Status -- */
static lv_obj_t *g_led_wifi;
static lv_obj_t *g_label_wifi;
static lv_obj_t *g_led_mqtt;
static lv_obj_t *g_label_mqtt;
static lv_obj_t *g_led_lora;
static lv_obj_t *g_label_lora;
static lv_obj_t *g_led_ota;
static lv_obj_t *g_label_ota;
static lv_obj_t *g_label_ip;
static lv_obj_t *g_label_fw;

/* ========== Helper: create a small coloured circle (LED indicator) ========== */
static lv_obj_t *create_led(lv_obj_t *parent, lv_color_t color) {
    lv_obj_t *led = lv_obj_create(parent);
    lv_obj_set_size(led, 14, 14);
    lv_obj_set_style_radius(led, 7, 0);
    lv_obj_set_style_border_width(led, 0, 0);
    lv_obj_set_style_bg_color(led, color, 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
    return led;
}

static void led_set_color(lv_obj_t *led, lv_color_t color) {
    lv_obj_set_style_bg_color(led, color, 0);
}

/* ========== Style helpers ========== */
static void apply_card_style(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 6, 0);
}

static lv_obj_t *make_title(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, COL_GREY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    return label;
}

static lv_obj_t *make_value(lv_obj_t *parent, const char *text, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    return label;
}


/* ========== Tab 1: Environment ========== */
static void build_env_tab(lv_obj_t *tab) {
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);

    /* -- Temperature card (top, full width) -- */
    lv_obj_t *card_temp = lv_obj_create(tab);
    lv_obj_set_size(card_temp, 296, 68);
    lv_obj_align(card_temp, LV_ALIGN_TOP_MID, 0, 4);
    apply_card_style(card_temp);
    lv_obj_set_style_pad_all(card_temp, 8, 0);

    lv_obj_t *tt = make_title(card_temp, "SHT30 Temp");
    lv_obj_align(tt, LV_ALIGN_TOP_LEFT, 2, 2);
    g_label_env_temp = make_value(card_temp, "--.- C", COL_GREEN);
    lv_obj_align(g_label_env_temp, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    lv_obj_t *t_humi = make_title(card_temp, "Humidity");
    lv_obj_align(t_humi, LV_ALIGN_TOP_MID, 30, 2);
    g_bar_env_humi = lv_bar_create(card_temp);
    lv_obj_set_size(g_bar_env_humi, 80, 10);
    lv_obj_align(g_bar_env_humi, LV_ALIGN_CENTER, 30, 0);
    lv_obj_set_style_bg_color(g_bar_env_humi, COL_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_bar_env_humi, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar_env_humi, COL_BLUE, LV_PART_INDICATOR);
    lv_bar_set_range(g_bar_env_humi, 0, 100);
    lv_bar_set_value(g_bar_env_humi, 0, LV_ANIM_OFF);

    g_label_env_humi = make_value(card_temp, "-- %", COL_BLUE);
    lv_obj_set_style_text_font(g_label_env_humi, &lv_font_montserrat_14, 0);
    lv_obj_align(g_label_env_humi, LV_ALIGN_BOTTOM_MID, 30, -2);

    /* -- Light card (bottom left) -- */
    lv_obj_t *card_lux = lv_obj_create(tab);
    lv_obj_set_size(card_lux, 144, 52);
    lv_obj_align(card_lux, LV_ALIGN_TOP_MID, -78, 78);
    apply_card_style(card_lux);
    lv_obj_set_style_pad_all(card_lux, 8, 0);

    lv_obj_t *tl = make_title(card_lux, "BH1750 Light");
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 2, 2);
    g_label_env_lux = make_value(card_lux, "--- Lux", COL_ORANGE);
    lv_obj_set_style_text_font(g_label_env_lux, &lv_font_montserrat_14, 0);
    lv_obj_align(g_label_env_lux, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    /* -- Sub-node card (bottom right) -- */
    lv_obj_t *card_sub = lv_obj_create(tab);
    lv_obj_set_size(card_sub, 144, 52);
    lv_obj_align(card_sub, LV_ALIGN_TOP_MID, 78, 78);
    apply_card_style(card_sub);
    lv_obj_set_style_pad_all(card_sub, 8, 0);

    lv_obj_t *ts = make_title(card_sub, "Node D001");
    lv_obj_align(ts, LV_ALIGN_TOP_LEFT, 2, 2);

    g_label_sub_temp = lv_label_create(card_sub);
    lv_label_set_text(g_label_sub_temp, "--.- C");
    lv_obj_set_style_text_color(g_label_sub_temp, COL_GREEN, 0);
    lv_obj_set_style_text_font(g_label_sub_temp, &lv_font_montserrat_10, 0);
    lv_obj_align(g_label_sub_temp, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    g_label_sub_humi = lv_label_create(card_sub);
    lv_label_set_text(g_label_sub_humi, "-- %");
    lv_obj_set_style_text_color(g_label_sub_humi, COL_BLUE, 0);
    lv_obj_set_style_text_font(g_label_sub_humi, &lv_font_montserrat_10, 0);
    lv_obj_align(g_label_sub_humi, LV_ALIGN_BOTTOM_MID, -8, -2);

    g_label_sub_lux = lv_label_create(card_sub);
    lv_label_set_text(g_label_sub_lux, "--- Lx");
    lv_obj_set_style_text_color(g_label_sub_lux, COL_ORANGE, 0);
    lv_obj_set_style_text_font(g_label_sub_lux, &lv_font_montserrat_10, 0);
    lv_obj_align(g_label_sub_lux, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
}

/* ========== Tab 2: Soil ========== */
static lv_obj_t *make_soil_cell(lv_obj_t *parent, const char *title,
                                const char *unit, lv_color_t color,
                                int x, int y) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 140, 44);
    lv_obj_set_pos(card, x, y);
    apply_card_style(card);
    lv_obj_set_style_pad_all(card, 6, 0);

    lv_obj_t *t = make_title(card, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 2, 2);

    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_color(v, color, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    lv_obj_t *u = lv_label_create(card);
    lv_label_set_text(u, unit);
    lv_obj_set_style_text_color(u, COL_GREY, 0);
    lv_obj_set_style_text_font(u, &lv_font_montserrat_10, 0);
    lv_obj_align(u, LV_ALIGN_BOTTOM_RIGHT, -2, -2);

    return v; /* return the value label for runtime updates */
}

static void build_soil_tab(lv_obj_t *tab) {
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);

    /* 2-column x 4-row grid of parameter cards */
    int x0 = 4, x1 = 152;
    int y_step = 50;

    g_label_soil_temp = make_soil_cell(tab, "Temp", "C", COL_GREEN,   x0, 4);
    g_label_soil_humi = make_soil_cell(tab, "Moisture", "%", COL_BLUE,    x1, 4);
    g_label_soil_ec   = make_soil_cell(tab, "EC", "us/cm", COL_ORANGE, x0, 4 + y_step);
    g_label_soil_ph   = make_soil_cell(tab, "pH", "", COL_ORANGE,     x1, 4 + y_step);
    g_label_soil_n    = make_soil_cell(tab, "N", "mg/kg", COL_GREEN,   x0, 4 + y_step * 2);
    g_label_soil_p    = make_soil_cell(tab, "P", "mg/kg", COL_BLUE,    x1, 4 + y_step * 2);
    g_label_soil_k    = make_soil_cell(tab, "K", "mg/kg", COL_ORANGE,  x0, 4 + y_step * 3);
    g_label_soil_sal  = make_soil_cell(tab, "Salinity", "mg/L", COL_RED,      x1, 4 + y_step * 3);
}

/* ========== Tab 3: System Status ========== */
static lv_obj_t *make_status_row(lv_obj_t *parent, const char *name,
                                  lv_obj_t **led, lv_obj_t **label,
                                  int y) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 296, 28);
    lv_obj_set_pos(card, 4, y);
    apply_card_style(card);
    lv_obj_set_style_pad_all(card, 4, 0);

    *led = create_led(card, COL_RED);
    lv_obj_align(*led, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *name_label = lv_label_create(card);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, COL_WHITE, 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 24, 0);

    *label = lv_label_create(card);
    lv_label_set_text(*label, "Waiting");
    lv_obj_set_style_text_color(*label, COL_GREY, 0);
    lv_obj_set_style_text_font(*label, &lv_font_montserrat_10, 0);
    lv_obj_align(*label, LV_ALIGN_RIGHT_MID, -4, 0);

    return card;
}

static void build_sys_tab(lv_obj_t *tab) {
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);

    make_status_row(tab, "WiFi",    &g_led_wifi, &g_label_wifi, 4);
    make_status_row(tab, "MQTT",    &g_led_mqtt, &g_label_mqtt, 36);
    make_status_row(tab, "LoRa",    &g_led_lora, &g_label_lora, 68);
    make_status_row(tab, "OTA Update", &g_led_ota, &g_label_ota, 100);

    /* IP address */
    lv_obj_t *card_ip = lv_obj_create(tab);
    lv_obj_set_size(card_ip, 296, 28);
    lv_obj_set_pos(card_ip, 4, 132);
    apply_card_style(card_ip);
    lv_obj_set_style_pad_all(card_ip, 4, 0);

    lv_obj_t *ip_title = make_title(card_ip, "IP Addr");
    lv_obj_align(ip_title, LV_ALIGN_LEFT_MID, 4, 0);
    g_label_ip = lv_label_create(card_ip);
    lv_label_set_text(g_label_ip, "---");
    lv_obj_set_style_text_color(g_label_ip, COL_WHITE, 0);
    lv_obj_set_style_text_font(g_label_ip, &lv_font_montserrat_10, 0);
    lv_obj_align(g_label_ip, LV_ALIGN_RIGHT_MID, -4, 0);

    /* FW version */
    lv_obj_t *card_fw = lv_obj_create(tab);
    lv_obj_set_size(card_fw, 296, 28);
    lv_obj_set_pos(card_fw, 4, 164);
    apply_card_style(card_fw);
    lv_obj_set_style_pad_all(card_fw, 4, 0);

    lv_obj_t *fw_title = make_title(card_fw, "Firmware");
    lv_obj_align(fw_title, LV_ALIGN_LEFT_MID, 4, 0);
    g_label_fw = lv_label_create(card_fw);
    lv_label_set_text(g_label_fw, "---");
    lv_obj_set_style_text_color(g_label_fw, COL_GREY, 0);
    lv_obj_set_style_text_font(g_label_fw, &lv_font_montserrat_10, 0);
    lv_obj_align(g_label_fw, LV_ALIGN_RIGHT_MID, -4, 0);
}

/* ========== Public API ========== */

void lvgl_ui_create(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *tv = lv_tabview_create(scr);
    lv_obj_set_size(tv, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(tv, COL_BG, 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);

    /* Style the tab bar */
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(tab_bar, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_pad_all(tab_bar, 2, 0);
    lv_obj_set_style_text_color(tab_bar, COL_WHITE, 0);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_14, 0);

    /* Build the three tabs */
    lv_obj_t *tab_env = lv_tabview_add_tab(tv, "Environ");
    build_env_tab(tab_env);

    lv_obj_t *tab_soil = lv_tabview_add_tab(tv, "Soil");
    build_soil_tab(tab_soil);

    lv_obj_t *tab_sys = lv_tabview_add_tab(tv, "Status");
    build_sys_tab(tab_sys);
}

/* -- Helper to format a float value with a label -- */
static void fmt_float_label(lv_obj_t *label, float val, const char *unit,
                             lv_color_t ok_color) {
    if (val <= -99.0f) {
        lv_label_set_text_fmt(label, "-- %s", unit);
        lv_obj_set_style_text_color(label, COL_GREY, 0);
    } else {
        if (val < 100.0f) {
            lv_label_set_text_fmt(label, "%.1f %s", val, unit);
        } else {
            lv_label_set_text_fmt(label, "%.0f %s", val, unit);
        }
        lv_obj_set_style_text_color(label, ok_color, 0);
    }
}

void lvgl_ui_update(float env_temp, float env_humi, float env_lux,
                    float soil_temp, float soil_humi, float soil_ec,
                    float soil_ph, float soil_n, float soil_p,
                    float soil_k, float soil_sal) {
    /* -- Tab 1: Environment -- */
    fmt_float_label(g_label_env_temp, env_temp, "C", COL_GREEN);
    fmt_float_label(g_label_env_humi, env_humi, "%", COL_BLUE);
    fmt_float_label(g_label_env_lux, env_lux, "Lux", COL_ORANGE);

    /* Update humidity bar */
    if (env_humi > -99.0f) {
        lv_bar_set_value(g_bar_env_humi, (int32_t)env_humi, LV_ANIM_ON);
    } else {
        lv_bar_set_value(g_bar_env_humi, 0, LV_ANIM_OFF);
    }

    /* Sub-node data (dummy for now - LoRa disabled) */
    fmt_float_label(g_label_sub_temp, -99.0f, "C", COL_GREY);
    fmt_float_label(g_label_sub_humi, -99.0f, "%", COL_GREY);
    fmt_float_label(g_label_sub_lux, -99.0f, "Lux", COL_GREY);

    /* -- Tab 2: Soil -- */
    fmt_float_label(g_label_soil_temp, soil_temp, "C",   COL_GREEN);
    fmt_float_label(g_label_soil_humi, soil_humi, "%",   COL_BLUE);
    fmt_float_label(g_label_soil_ec,   soil_ec,   "uS/cm", COL_ORANGE);
    fmt_float_label(g_label_soil_ph,   soil_ph,   "",    COL_ORANGE);
    fmt_float_label(g_label_soil_n,    soil_n,    "mg/kg", COL_GREEN);
    fmt_float_label(g_label_soil_p,    soil_p,    "mg/kg", COL_BLUE);
    fmt_float_label(g_label_soil_k,    soil_k,    "mg/kg", COL_ORANGE);
    fmt_float_label(g_label_soil_sal,  soil_sal,  "mg/L", COL_RED);
}

void lvgl_ui_update_sys(bool wifi_ok, bool mqtt_ok, bool lora_ok,
                        bool ota_active, const char *ip_addr,
                        const char *fw_ver) {
    /* WiFi */
    led_set_color(g_led_wifi, wifi_ok ? COL_GREEN : COL_RED);
    lv_label_set_text(g_label_wifi, wifi_ok ? "Online" : "Offline");
    lv_obj_set_style_text_color(g_label_wifi, wifi_ok ? COL_GREEN : COL_RED, 0);

    /* MQTT */
    led_set_color(g_led_mqtt, mqtt_ok ? COL_GREEN : COL_RED);
    lv_label_set_text(g_label_mqtt, mqtt_ok ? "Online" : "Offline");
    lv_obj_set_style_text_color(g_label_mqtt, mqtt_ok ? COL_GREEN : COL_RED, 0);

    /* LoRa */
    led_set_color(g_led_lora, lora_ok ? COL_GREEN : COL_RED);
    lv_label_set_text(g_label_lora, lora_ok ? "Online" : "Offline");
    lv_obj_set_style_text_color(g_label_lora, lora_ok ? COL_GREEN : COL_RED, 0);

    /* OTA */
    led_set_color(g_led_ota, ota_active ? COL_ORANGE : COL_GREY);
    lv_label_set_text(g_label_ota, ota_active ? "Updating..." : "Idle");
    lv_obj_set_style_text_color(g_label_ota, ota_active ? COL_ORANGE : COL_GREY, 0);

    /* IP */
    if (ip_addr && ip_addr[0]) {
        lv_label_set_text(g_label_ip, ip_addr);
    }

    /* FW */
    if (fw_ver && fw_ver[0]) {
        lv_label_set_text_fmt(g_label_fw, "v%s", fw_ver);
    }
}
