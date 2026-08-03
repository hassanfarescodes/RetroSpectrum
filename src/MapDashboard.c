/*
 * ============================================================================
 * File:            MapDashboard.c
 * Author:          Hassan Fares
 *
 * Description:     Dashboard and world-map/case rendering logic for
 * RetroSpectrum. This file was split out of RetroSpectrum.c so RetroSpectrum.c
 *                  can remain the top-level application coordinator.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SoapySDR/Device.h>

#include "CorrelationWorkstation.h"
#include "DataStore.h"
#include "GUIs.h"
#include "MapDashboard.h"

/*
 * world_map_bin_loader.c is intentionally included here, not compiled as a
 * separate translation unit. This preserves the original single-include design,
 * but keeps the map/dashboard implementation out of RetroSpectrum.c.
 */
#define WORLD_MAP_NO_DEMO
#include "world_map_bin_loader.c"

// ==============================
// Embedded Dashboard / Map Shell
// ==============================

typedef struct Type_Dashboard_Tab {
    SDL_Rect rect;
    const char *label;
    int event_id;
} Type_Dashboard_Tab;

typedef struct Type_Dashboard_Case_Info {
    char case_number[128];
    char description[512];
    SDL_Color color;
    int point_count;
} Type_Dashboard_Case_Info;

typedef struct Type_Dashboard_Case_Point {
    int case_index;
    char signal_name[256];
    char country[128];
    char notes[256];
    double latitude;
    double longitude;
} Type_Dashboard_Case_Point;

#define DASHBOARD_MAX_CASES 256
#define DASHBOARD_MAX_DOCUMENTS 512
#define DASHBOARD_MAX_CASE_POINTS 4096
#define DASHBOARD_CASE_DIR "Classification"
#define DASHBOARD_CASE_DESCRIPTION_CSV "Classification/CASE_DESCRIPTIONS.csv"
#define DASHBOARD_CASE_METADATA_PREFIX "__case_metadata_"

#ifndef DASHBOARD_EVENT_SDR_CHANGED
#define DASHBOARD_EVENT_SDR_CHANGED 1001
#endif

#define DASHBOARD_MAX_SDR_OPTIONS 16
#define DASHBOARD_SDR_BUTTON_W 330
#define DASHBOARD_SDR_BUTTON_H 28
#define DASHBOARD_SDR_ROW_H 30

typedef struct Type_Dashboard_SDR_Option {
    char label[256];
    char args[1024];
} Type_Dashboard_SDR_Option;

/* Implemented by RetroSpectrum.c. The dashboard owns only the selector UI;
 * the application coordinator owns the live SoapySDR device and stream. */
const char *RETROSPECTRUM_sdr_selected_label(void);
int RETROSPECTRUM_sdr_args_is_selected(const char *args);
int RETROSPECTRUM_select_sdr_args(const char *args, char *error, size_t error_size);

static Type_Dashboard_SDR_Option Global_Dashboard_SDR_Options[DASHBOARD_MAX_SDR_OPTIONS];
static int Global_Dashboard_SDR_Option_Count = 0;
static int Global_Dashboard_SDR_Menu_Open = 0;

static Type_Dashboard_Case_Info Global_Dashboard_Cases[DASHBOARD_MAX_CASES];
static Type_Dashboard_Case_Point Global_Dashboard_Case_Points[DASHBOARD_MAX_CASE_POINTS];
static int Global_Dashboard_Case_Count = 0;
static int Global_Dashboard_Case_Point_Count = 0;

#define DASHBOARD_MARGIN 20
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#define DASHBOARD_TOP_H RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define DASHBOARD_TAB_H 42
#define DASHBOARD_TAB_GAP 10
#define DASHBOARD_TAB_COUNT 7
#define DASHBOARD_CARD_H 0
#define DASHBOARD_MIN_MAP_H 280

static SDL_Color Dashboard_BG = {0, 0, 0, 255};
static SDL_Color Dashboard_Panel = {0, 12, 5, 255};
static SDL_Color Dashboard_Panel_2 = {0, 20, 8, 255};
static SDL_Color Dashboard_Grid = {0, 50, 20, 120};
static SDL_Color Dashboard_Border = {0, 150, 60, 255};
static SDL_Color Dashboard_Border_Hi = {0, 255, 90, 255};
static SDL_Color Dashboard_Text = {0, 255, 90, 255};
static SDL_Color Dashboard_Muted = {0, 155, 65, 255};
static SDL_Color Dashboard_Warn = {255, 180, 40, 255};

static int dashboard_point_in_rect(int x, int y, SDL_Rect r);

static void dashboard_sdr_selector_rects(int win_w, int win_h, SDL_Rect *label_rect, SDL_Rect *button_rect) {
    /*
        Purpose: Computes the bottom-right SDR selector rectangles
        Returns: No value
    */

    SDL_Rect button = {win_w - DASHBOARD_MARGIN - DASHBOARD_SDR_BUTTON_W, win_h - 48,
                       DASHBOARD_SDR_BUTTON_W, DASHBOARD_SDR_BUTTON_H};
    SDL_Rect label = {button.x - 112, button.y, 102, button.h};

    if (label_rect) {

        *label_rect = label;

    }

    if (button_rect) {

        *button_rect = button;

    }
}

static void dashboard_sdr_copy_truncated(TTF_Font *font, char *dst, size_t dst_size, const char *src,
                                         int max_width) {
    /*
        Purpose: Copies text while fitting it inside the SDR selector button
        Returns: No value
    */

    if (!dst || dst_size == 0) {

        return;

    }

    snprintf(dst, dst_size, "%s", src && src[0] ? src : "None");

    if (!font || max_width <= 0) {

        return;

    }

    int text_w = 0;
    int text_h = 0;

    if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_width) {

        return;

    }

    size_t length = strlen(dst);

    while (length > 3) {
        length--;
        dst[length] = '\0';

        if (length + 4 < dst_size) {

            strcat(dst, "...");

        }

        if (TTF_SizeText(font, dst, &text_w, &text_h) == 0 && text_w <= max_width) {

            return;

        }

        dst[length] = '\0';
    }
}

static void dashboard_refresh_sdr_options(void) {
    /*
        Purpose: Enumerates the currently detected SoapySDR devices
        Returns: No value
    */

    Global_Dashboard_SDR_Option_Count = 0;

    size_t device_count = 0;
    SoapySDRKwargs *devices = SoapySDRDevice_enumerate(NULL, &device_count);

    if (!devices) {

        return;

    }

    for (size_t index = 0; index < device_count &&
                           Global_Dashboard_SDR_Option_Count < DASHBOARD_MAX_SDR_OPTIONS;
         index++) {
        const char *label = SoapySDRKwargs_get(&devices[index], "label");
        const char *driver = SoapySDRKwargs_get(&devices[index], "driver");
        const char *serial = SoapySDRKwargs_get(&devices[index], "serial");
        char *serialized = SoapySDRKwargs_toString(&devices[index]);

        if (!serialized || !serialized[0]) {

            if (serialized) {

                SoapySDR_free(serialized);

            }
            continue;

        }

        Type_Dashboard_SDR_Option *option =
            &Global_Dashboard_SDR_Options[Global_Dashboard_SDR_Option_Count];

        snprintf(option->args, sizeof(option->args), "%s", serialized);

        if (label && label[0]) {

            snprintf(option->label, sizeof(option->label), "%s", label);

        }

        else if (driver && driver[0] && serial && serial[0]) {

            snprintf(option->label, sizeof(option->label), "%s [%s]", driver, serial);

        }

        else if (driver && driver[0]) {

            snprintf(option->label, sizeof(option->label), "%s", driver);

        }

        else {

            snprintf(option->label, sizeof(option->label), "SoapySDR Device %zu", index + 1);

        }

        Global_Dashboard_SDR_Option_Count++;
        SoapySDR_free(serialized);
    }

    SoapySDRKwargsList_clear(devices, device_count);
}

static void dashboard_draw_sdr_selector(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h,
                                        int mouse_x, int mouse_y) {
    /*
        Purpose: Draws the bottom-right selected SDR label and device menu
        Returns: No value
    */

    if (!renderer || !font) {

        return;

    }

    SDL_Rect label_rect;
    SDL_Rect button_rect;
    dashboard_sdr_selector_rects(win_w, win_h, &label_rect, &button_rect);

    draw_text(renderer, font, "Selected SDR", label_rect.x, label_rect.y + 6, Dashboard_Muted);

    int button_hovered = dashboard_point_in_rect(mouse_x, mouse_y, button_rect);
    SDL_Color button_fill = button_hovered || Global_Dashboard_SDR_Menu_Open ? Dashboard_Panel_2 : Dashboard_Panel;
    SDL_Color button_border = button_hovered || Global_Dashboard_SDR_Menu_Open ? Dashboard_Border_Hi : Dashboard_Border;

    draw_filled_rect(renderer, button_rect, button_fill);
    draw_outline_rect(renderer, button_rect, button_border);

    char selected_text[256];
    dashboard_sdr_copy_truncated(font, selected_text, sizeof(selected_text),
                                 RETROSPECTRUM_sdr_selected_label(), button_rect.w - 42);
    draw_text(renderer, font, selected_text, button_rect.x + 10, button_rect.y + 6, Dashboard_Text);

    int arrow_x = button_rect.x + button_rect.w - 18;
    int arrow_y = button_rect.y + button_rect.h / 2;
    SDL_SetRenderDrawColor(renderer, Dashboard_Text.r, Dashboard_Text.g, Dashboard_Text.b, Dashboard_Text.a);

    if (Global_Dashboard_SDR_Menu_Open) {

        SDL_RenderDrawLine(renderer, arrow_x - 5, arrow_y + 3, arrow_x, arrow_y - 2);
        SDL_RenderDrawLine(renderer, arrow_x, arrow_y - 2, arrow_x + 5, arrow_y + 3);

    }

    else {

        SDL_RenderDrawLine(renderer, arrow_x - 5, arrow_y - 2, arrow_x, arrow_y + 3);
        SDL_RenderDrawLine(renderer, arrow_x, arrow_y + 3, arrow_x + 5, arrow_y - 2);

    }

    if (!Global_Dashboard_SDR_Menu_Open) {

        return;

    }

    int row_count = Global_Dashboard_SDR_Option_Count > 0 ? Global_Dashboard_SDR_Option_Count : 1;
    SDL_Rect menu = {button_rect.x, button_rect.y - (row_count * DASHBOARD_SDR_ROW_H) - 4,
                     button_rect.w, row_count * DASHBOARD_SDR_ROW_H};

    draw_filled_rect(renderer, menu, (SDL_Color){0, 8, 4, 250});
    draw_outline_rect(renderer, menu, Dashboard_Border_Hi);

    for (int index = 0; index < row_count; index++) {
        SDL_Rect row = {menu.x, menu.y + index * DASHBOARD_SDR_ROW_H, menu.w, DASHBOARD_SDR_ROW_H};
        int hovered = dashboard_point_in_rect(mouse_x, mouse_y, row);

        if (hovered) {

            draw_filled_rect(renderer, row, Dashboard_Panel_2);

        }

        if (index > 0) {

            SDL_SetRenderDrawColor(renderer, Dashboard_Border.r, Dashboard_Border.g, Dashboard_Border.b, 150);
            SDL_RenderDrawLine(renderer, row.x, row.y, row.x + row.w, row.y);

        }

        if (Global_Dashboard_SDR_Option_Count == 0) {

            draw_text(renderer, font, "No SDRs detected", row.x + 10, row.y + 7, Dashboard_Warn);
            continue;

        }

        char option_text[256];
        dashboard_sdr_copy_truncated(font, option_text, sizeof(option_text),
                                     Global_Dashboard_SDR_Options[index].label, row.w - 38);
        SDL_Color text_color = RETROSPECTRUM_sdr_args_is_selected(
                                   Global_Dashboard_SDR_Options[index].args)
                                   ? Dashboard_Border_Hi
                                   : Dashboard_Text;

        if (hovered) {

            SDL_Color glow_color = {0, 255, 90, 95};
            draw_text(renderer, font, option_text, row.x + 9, row.y + 7, glow_color);
            draw_text(renderer, font, option_text, row.x + 11, row.y + 7, glow_color);
            draw_text(renderer, font, option_text, row.x + 10, row.y + 6, glow_color);
            draw_text(renderer, font, option_text, row.x + 10, row.y + 8, glow_color);
            text_color = (SDL_Color){210, 255, 225, 255};

        }

        draw_text(renderer, font, option_text, row.x + 10, row.y + 7, text_color);

        if (RETROSPECTRUM_sdr_args_is_selected(Global_Dashboard_SDR_Options[index].args)) {

            draw_text(renderer, font, "*", row.x + row.w - 20, row.y + 7, Dashboard_Border_Hi);

        }
    }
}

static int dashboard_handle_sdr_selector_event(Type_Dashboard_State *dashboard, const SDL_Event *event,
                                               int win_w, int win_h) {
    /*
        Purpose: Handles opening the SDR menu and switching the active device
        Returns: 0 not handled, 1 handled, 2 active SDR changed
    */

    if (!dashboard || !event) {

        return 0;

    }

    SDL_Rect label_rect;
    SDL_Rect button_rect;
    dashboard_sdr_selector_rects(win_w, win_h, &label_rect, &button_rect);
    (void)label_rect;

    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE &&
        Global_Dashboard_SDR_Menu_Open) {

        Global_Dashboard_SDR_Menu_Open = 0;
        return 1;

    }

    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT) {

        return 0;

    }

    if (dashboard_point_in_rect(event->button.x, event->button.y, button_rect)) {

        Global_Dashboard_SDR_Menu_Open = !Global_Dashboard_SDR_Menu_Open;

        if (Global_Dashboard_SDR_Menu_Open) {

            dashboard_refresh_sdr_options();

        }
        return 1;
    }

    if (!Global_Dashboard_SDR_Menu_Open) {

        return 0;

    }

    int row_count = Global_Dashboard_SDR_Option_Count > 0 ? Global_Dashboard_SDR_Option_Count : 1;
    SDL_Rect menu = {button_rect.x, button_rect.y - (row_count * DASHBOARD_SDR_ROW_H) - 4,
                     button_rect.w, row_count * DASHBOARD_SDR_ROW_H};

    if (dashboard_point_in_rect(event->button.x, event->button.y, menu)) {
        int selected_index = (event->button.y - menu.y) / DASHBOARD_SDR_ROW_H;

        if (Global_Dashboard_SDR_Option_Count == 0 || selected_index < 0 ||
            selected_index >= Global_Dashboard_SDR_Option_Count) {

            Global_Dashboard_SDR_Menu_Open = 0;
            return 1;

        }

        Type_Dashboard_SDR_Option *option = &Global_Dashboard_SDR_Options[selected_index];

        if (RETROSPECTRUM_sdr_args_is_selected(option->args)) {

            snprintf(dashboard->status, sizeof(dashboard->status), "Selected SDR unchanged: %s", option->label);
            Global_Dashboard_SDR_Menu_Open = 0;
            return 1;

        }

        char error[256] = "";

        if (!RETROSPECTRUM_select_sdr_args(option->args, error, sizeof(error))) {

            snprintf(dashboard->status, sizeof(dashboard->status), "SDR selection failed: %.210s",
                     error[0] ? error : "Unable to open the selected device");
            Global_Dashboard_SDR_Menu_Open = 0;
            return 1;

        }

        snprintf(dashboard->status, sizeof(dashboard->status), "Selected SDR: %s", option->label);
        Global_Dashboard_SDR_Menu_Open = 0;
        return 2;
    }

    Global_Dashboard_SDR_Menu_Open = 0;
    return 1;
}

static int dashboard_point_in_rect(int x, int y, SDL_Rect r) {
    /*
        Purpose: Checks whether a point lies inside a rectangle
        Returns: Boolean status
    */

    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void dashboard_update_timestamp(Type_Dashboard_State *dashboard) {
    time_t now;
    struct tm *local_time;

    if (dashboard == NULL) {

        return;

    }

    now = time(NULL);
    local_time = localtime(&now);

    if (local_time == NULL ||
        strftime(dashboard->status, sizeof(dashboard->status), "Updated at %I:%M:%S %p", local_time) == 0) {

        snprintf(dashboard->status, sizeof(dashboard->status), "Updated: Unknown");

    }
}

static void dashboard_copy_text(char *dst, size_t dst_size, const char *src) {
    /*
        Purpose: Copies the text
        Returns: No value
    */

    size_t i = 0;

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src) {

        src = "";

    }

    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

void dashboard_draw_text_centered(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect,
                                  SDL_Color color) {
    /*
        Purpose: Draws the text centered
        Returns: No value
    */

    int text_w = 0;
    int text_h = 0;

    if (!font || !text) {

        return;

    }

    if (TTF_SizeText(font, text, &text_w, &text_h) != 0) {

        text_w = 0;
        text_h = 0;

    }

    draw_text(renderer, font, text, rect.x + (rect.w - text_w) / 2, rect.y + (rect.h - text_h) / 2, color);
}

void dashboard_draw_grid(SDL_Renderer *renderer, int win_w, int win_h) {
    /*
        Purpose: Draws the grid
        Returns: No value
    */

    SDL_SetRenderDrawColor(renderer, Dashboard_Grid.r, Dashboard_Grid.g, Dashboard_Grid.b, Dashboard_Grid.a);

    for (int x = 0; x < win_w; x += 48) {
        SDL_RenderDrawLine(renderer, x, 0, x, win_h);
    }

    for (int y = 0; y < win_h; y += 48) {
        SDL_RenderDrawLine(renderer, 0, y, win_w, y);
    }

    SDL_SetRenderDrawColor(renderer, 0, 90, 35, 90);
    SDL_RenderDrawLine(renderer, 0, DASHBOARD_TOP_H, win_w, DASHBOARD_TOP_H);
    SDL_RenderDrawLine(renderer, 0, win_h - 58, win_w, win_h - 58);
}

static SDL_Rect dashboard_top_rect(int win_w) {
    /*
        Purpose: Computes the top rectangle
        Returns: Computed rectangle
    */

    SDL_Rect rect = {0, 0, win_w, DASHBOARD_TOP_H};
    return rect;
}

static SDL_Rect dashboard_content_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the content rectangle
        Returns: Computed rectangle
    */

    SDL_Rect rect = {DASHBOARD_MARGIN, DASHBOARD_TOP_H + DASHBOARD_MARGIN, win_w - 2 * DASHBOARD_MARGIN,
                     win_h - DASHBOARD_TOP_H - 2 * DASHBOARD_MARGIN - 40};

    if (rect.h < DASHBOARD_MIN_MAP_H) {

        rect.h = DASHBOARD_MIN_MAP_H;

    }
    return rect;
}

static void dashboard_make_tabs(int win_w, Type_Dashboard_Tab tabs[DASHBOARD_TAB_COUNT]) {
    /*
        Purpose: Builds the tabs
        Returns: No value
    */

    int total_gap = DASHBOARD_TAB_GAP * (DASHBOARD_TAB_COUNT - 1);
    int tab_w = (win_w - 2 * DASHBOARD_MARGIN - total_gap) / DASHBOARD_TAB_COUNT;
    int y = (DASHBOARD_TOP_H - DASHBOARD_TAB_H) / 2;
    int x = DASHBOARD_MARGIN;

    tabs[0].rect = (SDL_Rect){x, y, tab_w, DASHBOARD_TAB_H};
    tabs[0].label = "MAP";
    tabs[0].event_id = DASHBOARD_EVENT_MAP;

    x += tab_w + DASHBOARD_TAB_GAP;
    tabs[1].rect = (SDL_Rect){x, y, tab_w, DASHBOARD_TAB_H};
    tabs[1].label = "RETROSPECTRUM";
    tabs[1].event_id = DASHBOARD_EVENT_RETROSPECTRUM;

    x += tab_w + DASHBOARD_TAB_GAP;
    tabs[2].rect = (SDL_Rect){x, y, tab_w, DASHBOARD_TAB_H};
    tabs[2].label = "ANALYSIS";
    tabs[2].event_id = DASHBOARD_EVENT_ANALYSIS;

    x += tab_w + DASHBOARD_TAB_GAP;
    tabs[3].rect = (SDL_Rect){x, y, tab_w, DASHBOARD_TAB_H};
    tabs[3].label = "CLASSIFICATION";
    tabs[3].event_id = DASHBOARD_EVENT_CLASSIFICATION;

    x += tab_w + DASHBOARD_TAB_GAP;
    tabs[4].rect = (SDL_Rect){x, y, tab_w, DASHBOARD_TAB_H};
    tabs[4].label = "DECODE";
    tabs[4].event_id = DASHBOARD_EVENT_DECODE;

    x += tab_w + DASHBOARD_TAB_GAP;
    tabs[5].rect = (SDL_Rect){x, y, tab_w, DASHBOARD_TAB_H};
    tabs[5].label = "CASE MANAGEMENT";
    tabs[5].event_id = DASHBOARD_EVENT_CASE_MANAGEMENT;

    x += tab_w + DASHBOARD_TAB_GAP;
    tabs[6].rect = (SDL_Rect){x, y, tab_w, DASHBOARD_TAB_H};
    tabs[6].label = "CORRELATION";
    tabs[6].event_id = RETROSPECTRUM_DASHBOARD_EVENT_CORRELATION;
}

void dashboard_draw_tab(SDL_Renderer *renderer, TTF_Font *font, Type_Dashboard_Tab tab, int active, int hovered) {
    /*
        Purpose: Draws the tab
        Returns: No value
    */

    SDL_Color fill = active ? Dashboard_Panel_2 : Dashboard_Panel;
    SDL_Color border = (active || hovered) ? Dashboard_Border_Hi : Dashboard_Border;
    SDL_Color text = (active || hovered) ? Dashboard_Text : Dashboard_Muted;

    draw_filled_rect(renderer, tab.rect, fill);
    draw_outline_rect(renderer, tab.rect, border);

    if (active || hovered) {

        SDL_Rect inner = {tab.rect.x + 3, tab.rect.y + 3, tab.rect.w - 6, tab.rect.h - 6};
        draw_outline_rect(renderer, inner, border);

    }

    dashboard_draw_text_centered(renderer, font, tab.label, tab.rect, text);
}

void dashboard_draw_top_bar(SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium, int win_w, int mouse_x,
                            int mouse_y, int active_tab) {
    /*
        Purpose: Draws the top bar
        Returns: No value
    */

    (void)font_medium;

    SDL_Rect top = dashboard_top_rect(win_w);

    draw_filled_rect(renderer, top, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, top, Dashboard_Border);

    Type_Dashboard_Tab tabs[DASHBOARD_TAB_COUNT];
    dashboard_make_tabs(win_w, tabs);

    for (int i = 0; i < DASHBOARD_TAB_COUNT; i++) {
        dashboard_draw_tab(renderer, font_small, tabs[i], tabs[i].event_id == active_tab,
                           dashboard_point_in_rect(mouse_x, mouse_y, tabs[i].rect));
    }
}

void dashboard_draw_station_card(SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium, SDL_Rect rect,
                                 const char *title, const char *body, SDL_Color accent) {
    /*
        Purpose: Draws the station card
        Returns: No value
    */

    draw_filled_rect(renderer, rect, (SDL_Color){0, 10, 4, 240});
    draw_outline_rect(renderer, rect, accent);

    SDL_Rect stripe = {rect.x, rect.y, 5, rect.h};
    draw_filled_rect(renderer, stripe, accent);

    draw_text(renderer, font_medium, title, rect.x + 16, rect.y + 12, Dashboard_Text);
    draw_text(renderer, font_small, body, rect.x + 16, rect.y + 42, Dashboard_Muted);
}

static unsigned int dashboard_hash_string(const char *text) {
    /*
        Purpose: Hashes the string
        Returns: Success status
    */

    unsigned int h = 2166136261u;

    if (!text) {

        text = "";

    }
    while (*text) {
        h ^= (unsigned char)*text++;
        h *= 16777619u;
    }
    return h;
}

static int dashboard_ascii_equal_ci(const char *a, const char *b) {
    /*
        Purpose: Checks whether the ASCII case-insensitively values are equal
        Returns: Boolean status
    */

    if (!a) {

        a = "";

    }

    if (!b) {

        b = "";

    }

    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);

        if (ca != cb) {

            return 0;

        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static int dashboard_ascii_contains_ci(const char *haystack, const char *needle) {
    /*
        Purpose: Checks whether the ASCII case-insensitively contains a value
        Returns: Boolean status
    */

    if (!haystack) {

        haystack = "";

    }

    if (!needle || needle[0] == '\0') {

        return 0;

    }

    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < needle_len && p[i]) {
            int ca = tolower((unsigned char)p[i]);
            int cb = tolower((unsigned char)needle[i]);

            if (ca != cb) {

                break;

            }
            i++;
        }

        if (i == needle_len) {

            return 1;

        }
    }

    return 0;
}

static SDL_Color dashboard_case_color(const char *case_number) {
    /*
        Purpose: Gets the case color
        Returns: Computed color
    */

    static const SDL_Color palette[] = {
        {0, 170, 255, 255},  /* bright blue */
        {255, 150, 0, 255},  /* orange */
        {255, 55, 55, 255},  /* red */
        {0, 230, 120, 255},  /* green */
        {255, 235, 40, 255}, /* yellow */
        {255, 105, 90, 255}, /* coral */
        {150, 255, 45, 255}, /* lime */
        {80, 140, 255, 255}, /* soft blue */
        {255, 190, 80, 255}, /* amber */
        {255, 80, 180, 255}, /* pink */
        {80, 255, 220, 255}, /* cyan */
        {210, 120, 255, 255} /* violet */
    };

    unsigned int h = dashboard_hash_string(case_number);
    SDL_Color c = palette[h % (unsigned int)(sizeof(palette) / sizeof(palette[0]))];

    /* Slight deterministic brightness shift keeps repeated palette entries
     * distinct. */
    int shade = (int)((h >> 12) & 0x1F) - 10;
    int r = (int)c.r + shade;
    int g = (int)c.g + shade;
    int b = (int)c.b + shade;

    if (r < 0) {

        r = 0;

    }

    if (r > 255) {

        r = 255;

    }

    if (g < 0) {

        g = 0;

    }

    if (g > 255) {

        g = 255;

    }

    if (b < 0) {

        b = 0;

    }

    if (b > 255) {

        b = 255;

    }

    c.r = (Uint8)r;
    c.g = (Uint8)g;
    c.b = (Uint8)b;
    c.a = 255;
    return c;
}

static int dashboard_find_case_index(const char *case_number) {
    /*
        Purpose: Finds the case index
        Returns: Item index
    */

    if (!case_number || !case_number[0]) {

        case_number = "UNCASED";

    }

    for (int i = 0; i < Global_Dashboard_Case_Count; i++) {

        if (strcmp(Global_Dashboard_Cases[i].case_number, case_number) == 0) {

            return i;

        }
    }

    return -1;
}

static int dashboard_case_index_for(const char *case_number) {
    /*
        Purpose: Finds the index for a case
        Returns: Item index
    */

    if (!case_number || !case_number[0]) {

        case_number = "UNCASED";

    }

    {
        int existing = dashboard_find_case_index(case_number);

        if (existing >= 0) {

            return existing;

        }
    }

    if (Global_Dashboard_Case_Count >= DASHBOARD_MAX_CASES) {

        return -1;

    }

    int idx = Global_Dashboard_Case_Count++;
    snprintf(Global_Dashboard_Cases[idx].case_number, sizeof(Global_Dashboard_Cases[idx].case_number), "%s",
             case_number);
    Global_Dashboard_Cases[idx].description[0] = '\0';
    Global_Dashboard_Cases[idx].color = dashboard_case_color(case_number);
    Global_Dashboard_Cases[idx].point_count = 0;
    return idx;
}

static int dashboard_csv_parse_line(char *line, char fields[][512], int max_fields) {
    /*
        Purpose: Parses the CSV line
        Returns: Success status
    */

    int count = 0;
    char *p = line;

    while (*p && count < max_fields) {
        char *out = fields[count];
        int oi = 0;

        if (*p == '"') {

            p++;
            while (*p) {

                if (*p == '"') {

                    if (*(p + 1) == '"') {

                        if (oi < 511) {

                            out[oi++] = '"';

                        }
                        p += 2;
                        continue;

                    }
                    p++;
                    break;

                }

                if (oi < 511) {

                    out[oi++] = *p;

                }
                p++;
            }
            while (*p && *p != ',') {
                p++;
            }

            if (*p == ',') {

                p++;

            }

        }

        else {

            while (*p && *p != ',' && *p != '\n' && *p != '\r') {

                if (oi < 511) {

                    out[oi++] = *p;

                }
                p++;
            }

            if (*p == ',') {

                p++;

            }

        }

        out[oi] = '\0';
        count++;
    }

    return count;
}

static void dashboard_unescape_multiline_text(char *text) {
    /*
        Purpose: Unescapes the multiline text
        Returns: No value
    */

    char *r;
    char *w;

    if (!text) {

        return;

    }

    r = text;
    w = text;

    while (*r) {

        if (r[0] == '\\' && r[1] == 'n') {

            *w++ = '\n';
            r += 2;

        }

        else if (r[0] == '\\' && r[1] == 'r') {

            r += 2;

        }

        else {

            *w++ = *r++;

        }
    }

    *w = '\0';
}

static void dashboard_load_case_descriptions(void) {
    /*
        Purpose: Loads the case descriptions
        Returns: No value
    */

    FILE *fp = fopen(DASHBOARD_CASE_DESCRIPTION_CSV, "r");

    if (!fp) {

        return;

    }

    char line[2048];
    int first = 1;

    while (fgets(line, sizeof(line), fp)) {

        if (first) {

            first = 0;

            if (strstr(line, "case_number") && strstr(line, "description")) {

                continue;

            }

        }

        char fields[2][512];
        memset(fields, 0, sizeof(fields));

        if (dashboard_csv_parse_line(line, fields, 2) >= 1 && fields[0][0]) {

            int idx = dashboard_find_case_index(fields[0]);
            dashboard_unescape_multiline_text(fields[1]);

            if (idx >= 0) {

                snprintf(Global_Dashboard_Cases[idx].description, sizeof(Global_Dashboard_Cases[idx].description), "%s",
                         fields[1]);

            }

        }
    }

    fclose(fp);
}

static void dashboard_load_case_metadata(void) {
    /*
        Purpose: Loads case descriptions from case management metadata
        Returns: No value
    */

    static Type_DataStore_Document_Summary documents[DASHBOARD_MAX_DOCUMENTS];
    char database_error[256] = "";
    size_t document_count = 0;

    if (!DATASTORE_list_documents(DATASTORE_KIND_CASE_MANAGEMENT, documents, sizeof(documents) / sizeof(documents[0]),
                                  &document_count, database_error, sizeof(database_error))) {

        return;

    }

    for (size_t i = 0; i < document_count; i++) {
        unsigned char *content = NULL;
        size_t content_size = 0;
        int found = 0;
        char case_number[128] = "";
        const char *description = "";
        char *newline;
        int case_index;

        if (strncmp(documents[i].document_name, DASHBOARD_CASE_METADATA_PREFIX,
                    strlen(DASHBOARD_CASE_METADATA_PREFIX)) != 0) {

            continue;

        }

        if (!DATASTORE_load_content(DATASTORE_KIND_CASE_MANAGEMENT, documents[i].document_name, &content, &content_size,
                                    &found, database_error, sizeof(database_error)) ||
            !found || !content) {

            DATASTORE_free_content(content, content_size);
            continue;

        }

        newline = memchr(content, '\n', content_size);

        if (newline) {

            size_t case_size = (size_t)(newline - (char *)content);

            if (case_size >= sizeof(case_number)) {

                case_size = sizeof(case_number) - 1;

            }
            memcpy(case_number, content, case_size);
            case_number[case_size] = '\0';
            description = newline + 1;

        }

        else if (documents[i].case_number[0]) {

            dashboard_copy_text(case_number, sizeof(case_number), documents[i].case_number);

        }

        if (!case_number[0] && documents[i].case_number[0]) {

            dashboard_copy_text(case_number, sizeof(case_number), documents[i].case_number);

        }

        case_index = dashboard_case_index_for(case_number);

        if (case_index >= 0 && newline) {

            size_t description_size = content_size - (size_t)(description - (char *)content);

            if (description_size >= sizeof(Global_Dashboard_Cases[case_index].description)) {

                description_size = sizeof(Global_Dashboard_Cases[case_index].description) - 1;

            }
            memcpy(Global_Dashboard_Cases[case_index].description, description, description_size);
            Global_Dashboard_Cases[case_index].description[description_size] = '\0';

        }

        DATASTORE_free_content(content, content_size);
    }
}

static int dashboard_parse_coordinate(const char *text, double minimum, double maximum, double *value) {
    /*
        Purpose: Parses the coordinate
        Returns: Computed value
    */

    char *end = NULL;
    double parsed;

    if (!text || !text[0] || !value) {

        return 0;

    }

    errno = 0;
    parsed = strtod(text, &end);

    if (end == text || errno == ERANGE || !isfinite(parsed)) {

        return 0;

    }

    while (*end && isspace((unsigned char)*end)) {
        end++;
    }

    if (*end || parsed < minimum || parsed > maximum) {

        return 0;

    }

    *value = parsed;
    return 1;
}

static void dashboard_load_case_content(unsigned char *content, size_t content_size) {
    /*
        Purpose: Loads the case content
        Returns: No value
    */

    char *cursor;
    char *limit;
    int first = 1;

    if (!content || content_size == 0) {

        return;

    }

    cursor = (char *)content;
    limit = cursor + content_size;

    while (cursor < limit) {
        char *line = cursor;
        char *newline = memchr(cursor, '\n', (size_t)(limit - cursor));
        char saved = '\0';

        if (newline) {

            saved = *newline;
            *newline = '\0';
            cursor = newline + 1;

        }

        else {

            cursor = limit;

        }

        {
            size_t line_len = strlen(line);
            while (line_len > 0 && line[line_len - 1] == '\r') {
                line[--line_len] = '\0';
            }
        }

        if (first) {

            first = 0;

            if (strstr(line, "case_number") && strstr(line, "latitude")) {

                if (newline) {

                    *newline = saved;

                }
                continue;

            }

        }

        if (line[0]) {

            char fields[16][512];
            double latitude = 0.0;
            double longitude = 0.0;
            int field_count;

            memset(fields, 0, sizeof(fields));
            field_count = dashboard_csv_parse_line(line, fields, 16);

            if (field_count >= 12 && fields[0][0] && dashboard_parse_coordinate(fields[9], -90.0, 90.0, &latitude) &&
                dashboard_parse_coordinate(fields[10], -180.0, 180.0, &longitude) &&
                Global_Dashboard_Case_Point_Count < DASHBOARD_MAX_CASE_POINTS) {

                int case_index = dashboard_case_index_for(fields[0]);

                if (case_index >= 0) {

                    Type_Dashboard_Case_Point *point =
                        &Global_Dashboard_Case_Points[Global_Dashboard_Case_Point_Count++];

                    memset(point, 0, sizeof(*point));
                    point->case_index = case_index;
                    dashboard_copy_text(point->signal_name, sizeof(point->signal_name), fields[1]);
                    dashboard_copy_text(point->country, sizeof(point->country), fields[8]);
                    dashboard_copy_text(point->notes, sizeof(point->notes), fields[11]);
                    point->latitude = latitude;
                    point->longitude = longitude;
                    Global_Dashboard_Cases[case_index].point_count++;

                }

            }

        }

        if (newline) {

            *newline = saved;

        }
    }
}

static int dashboard_reload_cases(Type_Dashboard_State *dashboard) {
    /*
        Purpose: Reloads the cases
        Returns: Success status
    */

    static Type_DataStore_Document_Summary documents[DASHBOARD_MAX_DOCUMENTS];
    char selected_case_number[128] = "";
    char database_error[256] = "";
    size_t document_count = 0;

    if (dashboard && dashboard->selected_case >= 0 && dashboard->selected_case < Global_Dashboard_Case_Count) {

        dashboard_copy_text(selected_case_number, sizeof(selected_case_number),
                            Global_Dashboard_Cases[dashboard->selected_case].case_number);

    }

    if (!DATASTORE_list_documents(DATASTORE_KIND_CLASSIFICATION, documents, sizeof(documents) / sizeof(documents[0]),
                                  &document_count, database_error, sizeof(database_error))) {

        if (dashboard) {

            snprintf(dashboard->status, sizeof(dashboard->status), "Unable to load map cases from database: %.170s",
                     database_error);

        }
        return 0;

    }

    Global_Dashboard_Case_Count = 0;
    Global_Dashboard_Case_Point_Count = 0;

    for (size_t i = 0; i < document_count; i++) {
        unsigned char *content = NULL;
        size_t content_size = 0;
        int found = 0;

        database_error[0] = '\0';

        if (!DATASTORE_load_content(DATASTORE_KIND_CLASSIFICATION, documents[i].document_name, &content, &content_size,
                                    &found, database_error, sizeof(database_error))) {

            if (dashboard) {

                snprintf(dashboard->status, sizeof(dashboard->status), "Unable to load map case %.80s: %.120s",
                         documents[i].document_name, database_error);

            }
            continue;

        }

        if (found && content) {

            dashboard_load_case_content(content, content_size);

        }

        DATASTORE_free_content(content, content_size);
    }

    dashboard_load_case_descriptions();
    dashboard_load_case_metadata();

    if (dashboard) {

        dashboard->selected_case = selected_case_number[0] ? dashboard_find_case_index(selected_case_number) : -1;

        if (dashboard->selected_case < 0) {

            dashboard->case_desc_editing = 0;
            dashboard->case_desc_edit[0] = '\0';

        }

    }

    return 1;
}

static int dashboard_lonlat_to_screen(double lon, double lat, SDL_Rect map, int *sx, int *sy) {
    /*
        Purpose: Converts the longitude and latitude to the screen
        Returns: Success status
    */

    double min_lon = WM_VIEW.min_lon;
    double max_lon = WM_VIEW.max_lon;
    double min_lat = WM_VIEW.min_lat;
    double max_lat = WM_VIEW.max_lat;

    if (max_lon <= min_lon || max_lat <= min_lat) {

        return 0;

    }

    if (lon < min_lon || lon > max_lon || lat < min_lat || lat > max_lat) {

        return 0;

    }

    double xf = (lon - min_lon) / (max_lon - min_lon);
    double yf = (max_lat - lat) / (max_lat - min_lat);

    if (sx) {

        *sx = map.x + (int)(xf * (double)map.w + 0.5);

    }

    if (sy) {

        *sy = map.y + (int)(yf * (double)map.h + 0.5);

    }
    return 1;
}

void dashboard_draw_circle_outline(SDL_Renderer *renderer, int cx, int cy, int radius, int thickness) {
    /*
        Purpose: Draws the circle outline
        Returns: No value
    */

    if (!renderer || radius <= 0) {

        return;

    }

    int outer2 = radius * radius;
    int inner = radius - thickness;

    if (inner < 0) {

        inner = 0;

    }
    int inner2 = inner * inner;

    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int d2 = dx * dx + dy * dy;

            if (d2 <= outer2 && d2 >= inner2) {

                SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);

            }
        }
    }
}

void dashboard_draw_case_dot(SDL_Renderer *renderer, int x, int y, SDL_Color color, int enlarged, int search_match) {
    /*
        Purpose: Draws the case dot
        Returns: No value
    */

    int radius = enlarged ? 6 : 4;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (search_match) {

        SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
        dashboard_draw_circle_outline(renderer, x, y, radius + 5, 2);

    }

    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {

            if (dx * dx + dy * dy <= radius * radius) {

                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, enlarged ? 255 : 245);
                SDL_RenderDrawPoint(renderer, x + dx, y + dy);

            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static int dashboard_case_matches_search(Type_Dashboard_State *dashboard, int case_index) {
    /*
        Purpose: Checks whether the case matches the search
        Returns: Boolean status
    */

    if (!dashboard || dashboard->case_search_text[0] == '\0') {

        return 0;

    }

    if (case_index < 0 || case_index >= Global_Dashboard_Case_Count) {

        return 0;

    }
    return dashboard_ascii_contains_ci(Global_Dashboard_Cases[case_index].case_number, dashboard->case_search_text);
}

void dashboard_draw_case_points(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font, SDL_Rect map) {
    /*
        Purpose: Draws the case points
        Returns: No value
    */

    (void)font;

    if (!dashboard || !renderer) {

        return;

    }

    int drag_x = 0;
    int drag_y = 0;
    WORLD_MAP_get_drag_offset(&drag_x, &drag_y);

    for (int i = 0; i < Global_Dashboard_Case_Point_Count; i++) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];

        if (pt->case_index < 0 || pt->case_index >= Global_Dashboard_Case_Count) {

            continue;

        }

        int x = 0;
        int y = 0;

        if (!dashboard_lonlat_to_screen(pt->longitude, pt->latitude, map, &x, &y)) {

            continue;

        }

        x += drag_x;
        y += drag_y;

        if (!dashboard_point_in_rect(x, y, map)) {

            continue;

        }

        int selected = (dashboard->selected_case == pt->case_index);
        int search_match = dashboard_case_matches_search(dashboard, pt->case_index);
        dashboard_draw_case_dot(renderer, x, y, Global_Dashboard_Cases[pt->case_index].color, selected || search_match,
                                search_match);
    }
}

static int dashboard_select_case_at(Type_Dashboard_State *dashboard, SDL_Rect map, int x, int y) {
    /*
        Purpose: Selects the case at
        Returns: Success status
    */

    if (!dashboard) {

        return 0;

    }

    for (int i = Global_Dashboard_Case_Point_Count - 1; i >= 0; i--) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];
        int sx = 0;
        int sy = 0;

        if (!dashboard_lonlat_to_screen(pt->longitude, pt->latitude, map, &sx, &sy)) {

            continue;

        }

        int dx = x - sx;
        int dy = y - sy;

        if (dx * dx + dy * dy <= 100) {

            dashboard->selected_case = pt->case_index;
            dashboard->case_desc_editing = 0;

            if (pt->case_index >= 0 && pt->case_index < Global_Dashboard_Case_Count) {

                snprintf(dashboard->case_desc_edit, sizeof(dashboard->case_desc_edit), "%s",
                         Global_Dashboard_Cases[pt->case_index].description);

            }
            return 1;

        }
    }

    return 0;
}

static void dashboard_wrap_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect,
                                SDL_Color color) {
    /*
        Purpose: Wraps the text
        Returns: No value
    */

    if (!renderer || !font || !text) {

        return;

    }

    char line[512] = "";
    char word[128];
    int y = rect.y;
    int line_h = TTF_FontHeight(font) + 4;
    const char *p = text;

    while (*p && y + line_h <= rect.y + rect.h) {
        int wi = 0;
        while (*p == ' ') {
            p++;
        }
        while (*p && *p != ' ' && *p != '\n' && wi + 1 < (int)sizeof(word)) {
            word[wi++] = *p++;
        }
        word[wi] = '\0';

        char test[640];

        if (line[0]) {

            snprintf(test, sizeof(test), "%s %s", line, word);

        }

        else {

            snprintf(test, sizeof(test), "%s", word);

        }

        int tw = 0;
        int th = 0;
        TTF_SizeText(font, test, &tw, &th);

        if (tw > rect.w && line[0]) {

            draw_text(renderer, font, line, rect.x, y, color);
            y += line_h;
            dashboard_copy_text(line, sizeof(line), word);

        }

        else {

            dashboard_copy_text(line, sizeof(line), test);

        }

        if (*p == '\n') {

            if (line[0]) {

                draw_text(renderer, font, line, rect.x, y, color);

            }
            y += line_h;
            line[0] = '\0';
            p++;

        }
    }

    if (line[0] && y + line_h <= rect.y + rect.h) {

        draw_text(renderer, font, line, rect.x, y, color);

    }
}

static int dashboard_case_country_matches(const Type_Dashboard_Case_Point *pt, const WM_Country *country) {
    /*
        Purpose: Checks whether the case country matches the requested data
        Returns: Boolean status
    */

    if (!pt || !country || pt->country[0] == '\0') {

        return 0;

    }

    if (dashboard_ascii_equal_ci(pt->country, country->name)) {

        return 1;

    }

    if (country->alpha2 && country->alpha2[0] && dashboard_ascii_equal_ci(pt->country, country->alpha2)) {

        return 1;

    }
    return 0;
}

static int dashboard_collect_cases_for_country(const WM_Country *country, int *case_indices, int max_indices) {
    /*
        Purpose: Collects cases for a country
        Returns: Success status
    */

    int count = 0;

    if (!country || !case_indices || max_indices <= 0) {

        return 0;

    }

    for (int i = 0; i < Global_Dashboard_Case_Point_Count; i++) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];

        if (pt->case_index < 0 || pt->case_index >= Global_Dashboard_Case_Count) {

            continue;

        }

        if (!dashboard_case_country_matches(pt, country)) {

            continue;

        }

        int already = 0;
        for (int j = 0; j < count; j++) {

            if (case_indices[j] == pt->case_index) {

                already = 1;
                break;

            }
        }

        if (already) {

            continue;

        }

        if (count < max_indices) {

            case_indices[count] = pt->case_index;

        }
        count++;
    }

    return count;
}

void dashboard_draw_hover_country_cases(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font,
                                        SDL_Rect sidebar) {
    /*
        Purpose: Draws the hover country cases
        Returns: No value
    */

    if (!dashboard || !renderer || !font || dashboard->selected_case >= 0) {

        return;

    }

    if (dashboard->hover_country < 0 || dashboard->hover_country >= (int)WM_DATA.country_count) {

        return;

    }

    WM_Country *country = &WM_DATA.countries[dashboard->hover_country];
    int case_indices[DASHBOARD_MAX_CASES];
    int total_cases = dashboard_collect_cases_for_country(country, case_indices, DASHBOARD_MAX_CASES);

    int x = sidebar.x + 18;
    int y = sidebar.y + 352;
    int bottom = sidebar.y + sidebar.h - 18;

    if (y >= bottom) {

        return;

    }

    SDL_Rect panel = {x, y, sidebar.w - 36, bottom - y};
    draw_filled_rect(renderer, panel, (SDL_Color){0, 7, 3, 235});
    draw_outline_rect(renderer, panel, Dashboard_Border);

    char total_line[128];
    snprintf(total_line, sizeof(total_line), "Cases in country: %d", total_cases);
    draw_text(renderer, font, total_line, panel.x + 10, panel.y + 10, Dashboard_Text);

    int list_y = panel.y + 42;
    int list_h = panel.h - 52;
    int row_h = TTF_FontHeight(font) * 2 + 14;
    int visible_rows = row_h > 0 ? list_h / row_h : 0;

    if (visible_rows < 1) {

        visible_rows = 1;

    }

    if (dashboard->country_case_scroll < 0) {

        dashboard->country_case_scroll = 0;

    }
    int max_scroll = total_cases - visible_rows;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (dashboard->country_case_scroll > max_scroll) {

        dashboard->country_case_scroll = max_scroll;

    }

    if (total_cases <= 0) {

        dashboard_wrap_text(renderer, font, "No saved cases for this country.",
                            (SDL_Rect){panel.x + 10, list_y, panel.w - 20, list_h}, Dashboard_Muted);
        return;

    }

    for (int row = 0; row < visible_rows; row++) {
        int idx = dashboard->country_case_scroll + row;

        if (idx >= total_cases || idx >= DASHBOARD_MAX_CASES) {

            break;

        }
        int case_index = case_indices[idx];

        if (case_index < 0 || case_index >= Global_Dashboard_Case_Count) {

            continue;

        }

        SDL_Rect item = {panel.x + 10, list_y + row * row_h, panel.w - 20, row_h - 6};

        SDL_Color c = Global_Dashboard_Cases[case_index].color;
        SDL_Rect swatch = {item.x, item.y + 5, 8, 8};
        draw_filled_rect(renderer, swatch, c);

        char label[256];
        snprintf(label, sizeof(label), "%s  (%d signals)", Global_Dashboard_Cases[case_index].case_number,
                 Global_Dashboard_Cases[case_index].point_count);

        dashboard_wrap_text(renderer, font, label, (SDL_Rect){item.x + 16, item.y, item.w - 16, item.h},
                            Dashboard_Muted);
    }

    if (total_cases > visible_rows) {

        char scroll_line[96];
        snprintf(scroll_line, sizeof(scroll_line), "Scroll %d/%d", dashboard->country_case_scroll + 1, max_scroll + 1);
        draw_text(renderer, font, scroll_line, panel.x + 10, panel.y + panel.h - 22, (SDL_Color){120, 180, 140, 255});

    }
}

void dashboard_draw_case_search(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font,
                                SDL_Rect area) {
    /*
        Purpose: Draws the case search
        Returns: No value
    */

    if (!dashboard || !renderer || !font) {

        return;

    }

    draw_text(renderer, font, "Search by Case", area.x, area.y, Dashboard_Text);

    SDL_Rect input = {area.x, area.y + 24, area.w, 30};
    dashboard->case_search_rect = input;

    draw_filled_rect(renderer, input, (SDL_Color){0, 12, 5, 245});
    draw_outline_rect(renderer, input, dashboard->case_search_active ? Dashboard_Border_Hi : Dashboard_Border);

    const char *shown = dashboard->case_search_text[0] ? dashboard->case_search_text : "Search case";
    draw_text(renderer, font, shown, input.x + 8, input.y + 7,
              dashboard->case_search_text[0] ? Dashboard_Text : Dashboard_Muted);

    if (dashboard->case_search_active && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {

        int cursor = dashboard->case_search_cursor;
        int len = (int)strlen(dashboard->case_search_text);

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > len) {

            cursor = len;

        }

        int text_w = 0;
        int text_h = 0;

        if (cursor > 0) {

            char before[128];

            if (cursor >= (int)sizeof(before)) {

                cursor = (int)sizeof(before) - 1;

            }
            memcpy(before, dashboard->case_search_text, (size_t)cursor);
            before[cursor] = '\0';

            if (TTF_SizeText(font, before, &text_w, &text_h) != 0) {

                text_w = cursor * 8;

            }

        }

        int cx = input.x + 8 + text_w;
        int cy0 = input.y + 6;
        int cy1 = input.y + input.h - 6;

        if (cx < input.x + 8) {

            cx = input.x + 8;

        }

        if (cx > input.x + input.w - 8) {

            cx = input.x + input.w - 8;

        }

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
        SDL_RenderDrawLine(renderer, cx, cy0, cx, cy1);
        SDL_RenderDrawLine(renderer, cx + 1, cy0, cx + 1, cy1);

    }
}

void dashboard_draw_case_sidebar(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font,
                                 SDL_Rect sidebar) {
    /*
        Purpose: Draws the read-only case sidebar
        Returns: No value
    */

    if (!dashboard || dashboard->selected_case < 0 || dashboard->selected_case >= Global_Dashboard_Case_Count) {

        return;

    }

    Type_Dashboard_Case_Info *info = &Global_Dashboard_Cases[dashboard->selected_case];

    dashboard->case_desc_editing = 0;

    draw_filled_rect(renderer, sidebar, (SDL_Color){0, 5, 2, 248});
    draw_outline_rect(renderer, sidebar, info->color);

    draw_text(renderer, font, "CASE", sidebar.x + 16, sidebar.y + 18, Dashboard_Muted);
    draw_text(renderer, font, info->case_number, sidebar.x + 16, sidebar.y + 42, Dashboard_Text);

    char count_line[128];
    snprintf(count_line, sizeof(count_line), "Signals in case: %d", info->point_count);
    draw_text(renderer, font, count_line, sidebar.x + 16, sidebar.y + 72, Dashboard_Muted);

    draw_text(renderer, font, "Description", sidebar.x + 16, sidebar.y + 114, Dashboard_Text);

    dashboard->case_desc_rect = (SDL_Rect){sidebar.x + 16, sidebar.y + 142, sidebar.w - 32, 170};
    draw_filled_rect(renderer, dashboard->case_desc_rect, (SDL_Color){0, 9, 4, 255});
    draw_outline_rect(renderer, dashboard->case_desc_rect, Dashboard_Border);

    SDL_Rect desc_text_rect = {dashboard->case_desc_rect.x + 9, dashboard->case_desc_rect.y + 9,
                               dashboard->case_desc_rect.w - 18, dashboard->case_desc_rect.h - 18};
    const char *shown = info->description[0] ? info->description : "No case description.";
    dashboard_wrap_text(renderer, font, shown, desc_text_rect, Dashboard_Muted);

    draw_text(renderer, font, "Descriptions are edited in Case Management", sidebar.x + 16,
              dashboard->case_desc_rect.y + dashboard->case_desc_rect.h + 12, Dashboard_Muted);

    int y = dashboard->case_desc_rect.y + dashboard->case_desc_rect.h + 48;
    draw_text(renderer, font, "Signals", sidebar.x + 16, y, Dashboard_Text);
    y += 28;

    int shown_count = 0;
    for (int i = 0; i < Global_Dashboard_Case_Point_Count && shown_count < 7; i++) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];

        if (pt->case_index != dashboard->selected_case) {

            continue;

        }

        char line[256];
        snprintf(line, sizeof(line), "%s  %.4f, %.4f", pt->signal_name[0] ? pt->signal_name : "Unnamed signal",
                 pt->latitude, pt->longitude);
        draw_text(renderer, font, line, sidebar.x + 16, y, Dashboard_Muted);
        y += 24;

        if (pt->country[0]) {

            draw_text(renderer, font, pt->country, sidebar.x + 30, y, (SDL_Color){120, 180, 140, 255});
            y += 22;

        }

        shown_count++;
    }
}

static int dashboard_handle_case_search_event(Type_Dashboard_State *dashboard, const SDL_Event *event) {
    /*
        Purpose: Handles the case search event
        Returns: Handling status
    */

    if (!dashboard || !event) {

        return 0;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        if (dashboard_point_in_rect(event->button.x, event->button.y, dashboard->case_search_rect)) {

            dashboard->case_search_active = 1;
            dashboard->case_search_cursor = (int)strlen(dashboard->case_search_text);
            return 1;

        }
        dashboard->case_search_active = 0;
        return 0;

    }

    if (!dashboard->case_search_active) {

        return 0;

    }

    if (event->type == SDL_TEXTINPUT) {

        int len = (int)strlen(dashboard->case_search_text);
        int cursor = dashboard->case_search_cursor;

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > len) {

            cursor = len;

        }

        size_t add = strlen(event->text.text);

        if (add > 0 && len + (int)add < (int)sizeof(dashboard->case_search_text)) {

            memmove(dashboard->case_search_text + cursor + (int)add, dashboard->case_search_text + cursor,
                    (size_t)(len - cursor) + 1U);
            memcpy(dashboard->case_search_text + cursor, event->text.text, add);
            dashboard->case_search_cursor = cursor + (int)add;

        }
        return 1;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;
        int len = (int)strlen(dashboard->case_search_text);

        if (key == SDLK_LCTRL || key == SDLK_RCTRL) {

            return 0;

        }

        if (key == SDLK_LEFT) {

            if (dashboard->case_search_cursor > 0) {

                dashboard->case_search_cursor--;

            }
            return 1;

        }

        if (key == SDLK_RIGHT) {

            if (dashboard->case_search_cursor < len) {

                dashboard->case_search_cursor++;

            }
            return 1;

        }

        if (key == SDLK_HOME) {

            dashboard->case_search_cursor = 0;
            return 1;

        }

        if (key == SDLK_END) {

            dashboard->case_search_cursor = len;
            return 1;

        }

        if (key == SDLK_BACKSPACE) {

            int cursor = dashboard->case_search_cursor;

            if (cursor > 0 && len > 0) {

                memmove(dashboard->case_search_text + cursor - 1, dashboard->case_search_text + cursor,
                        (size_t)(len - cursor) + 1U);
                dashboard->case_search_cursor--;

            }
            return 1;

        }

        if (key == SDLK_DELETE) {

            int cursor = dashboard->case_search_cursor;

            if (cursor >= 0 && cursor < len) {

                memmove(dashboard->case_search_text + cursor, dashboard->case_search_text + cursor + 1,
                        (size_t)(len - cursor));

            }
            return 1;

        }

        if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            dashboard->case_search_active = 0;
            return 1;

        }
        return 1;

    }

    return 0;
}

static int dashboard_handle_case_sidebar_event(Type_Dashboard_State *dashboard, const SDL_Event *event) {
    /*
        Purpose: Keeps the world map case sidebar read only
        Returns: Handling status
    */

    (void)dashboard;
    (void)event;
    return 0;
}

int dashboard_handle_top_tab_event(Type_Dashboard_State *dashboard, const SDL_Event *event, int win_w,
                                   int text_entry_active) {
    /*
        Purpose: Handles the top tab event
        Returns: Handling status
    */

    if (!dashboard || !event) {

        return DASHBOARD_EVENT_NONE;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (!text_entry_active) {

            if (key == SDLK_1) {

                return DASHBOARD_EVENT_MAP;

            }

            if (key == SDLK_2) {

                return DASHBOARD_EVENT_RETROSPECTRUM;

            }

            if (key == SDLK_3) {

                return DASHBOARD_EVENT_ANALYSIS;

            }

            if (key == SDLK_4) {

                return DASHBOARD_EVENT_CLASSIFICATION;

            }

            if (key == SDLK_5) {

                return DASHBOARD_EVENT_DECODE;

            }

            if (key == SDLK_6) {

                return DASHBOARD_EVENT_CASE_MANAGEMENT;

            }

            if (key == SDLK_7) {

                return RETROSPECTRUM_DASHBOARD_EVENT_CORRELATION;

            }

        }

        if (key == SDLK_F1) {

            return DASHBOARD_EVENT_MAP;

        }

        if (key == SDLK_F2) {

            return DASHBOARD_EVENT_RETROSPECTRUM;

        }

        if (key == SDLK_F3) {

            return DASHBOARD_EVENT_ANALYSIS;

        }

        if (key == SDLK_F4) {

            return DASHBOARD_EVENT_CLASSIFICATION;

        }

        if (key == SDLK_F5) {

            return DASHBOARD_EVENT_DECODE;

        }

        if (key == SDLK_F6) {

            return DASHBOARD_EVENT_CASE_MANAGEMENT;

        }

        if (key == SDLK_F7) {

            return RETROSPECTRUM_DASHBOARD_EVENT_CORRELATION;

        }

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        Type_Dashboard_Tab tabs[DASHBOARD_TAB_COUNT];
        dashboard_make_tabs(win_w, tabs);
        for (int i = 0; i < DASHBOARD_TAB_COUNT; i++) {

            if (dashboard_point_in_rect(event->button.x, event->button.y, tabs[i].rect)) {

                return tabs[i].event_id;

            }
        }

    }

    return DASHBOARD_EVENT_NONE;
}

int dashboard_init(Type_Dashboard_State *dashboard, const char *map_bin_path) {
    /*
        Purpose: Initializes the map dashboard
        Returns: Success status
    */

    if (!dashboard) {

        return 0;

    }

    memset(dashboard, 0, sizeof(*dashboard));
    dashboard->enabled = 1;
    dashboard->current_tab = DASHBOARD_EVENT_MAP;
    dashboard->selected_case = -1;
    dashboard->case_desc_editing = 0;
    dashboard->country_case_scroll = 0;
    dashboard->hover_country = -1;
    dashboard->hover_mouse_x = 0;
    dashboard->hover_mouse_y = 0;
    dashboard->locked_country = -1;
    dashboard->locked_mouse_x = 0;
    dashboard->locked_mouse_y = 0;
    dashboard->case_search_active = 0;
    dashboard->case_search_cursor = 0;
    dashboard->case_desc_rect = (SDL_Rect){0, 0, 0, 0};
    dashboard->case_search_rect = (SDL_Rect){0, 0, 0, 0};
    dashboard->case_desc_edit[0] = '\0';
    dashboard->case_search_text[0] = '\0';
    dashboard->last_case_scan_ms = 0;

    if (!map_bin_path || map_bin_path[0] == '\0') {

        map_bin_path = "world_map.bin";

    }

    dashboard->map_loaded = WORLD_MAP_load(map_bin_path);

    if (dashboard->map_loaded) {

        snprintf(dashboard->status, sizeof(dashboard->status), "Loaded map data: %s", map_bin_path);

    }

    else {

        snprintf(dashboard->status, sizeof(dashboard->status),
                 "Map data not loaded. Put world_map.bin next to the executable.");

    }

    dashboard_reload_cases(dashboard);
    dashboard_refresh_sdr_options();
    Global_Dashboard_SDR_Menu_Open = 0;

    return dashboard->map_loaded;
}

void dashboard_shutdown(void) {
    /*
        Purpose: Shuts down the map dashboard
        Returns: No value
    */

    Global_Dashboard_SDR_Menu_Open = 0;
    Global_Dashboard_SDR_Option_Count = 0;
    WORLD_MAP_free();
}

static int dashboard_find_country_screen_point(SDL_Rect map, int country_index, int *out_x, int *out_y) {
    /*
        Purpose: Finds the country screen point
        Returns: Success status
    */

    if (country_index < 0 || !out_x || !out_y) {

        return 0;

    }

    if (dashboard_point_in_rect(*out_x, *out_y, map) && WM_country_at(map, *out_x, *out_y) == country_index) {

        return 1;

    }

    int step = 12;
    for (int y = map.y + step / 2; y < map.y + map.h; y += step) {
        for (int x = map.x + step / 2; x < map.x + map.w; x += step) {

            if (WM_country_at(map, x, y) == country_index) {

                *out_x = x;
                *out_y = y;
                return 1;

            }
        }
    }

    return 0;
}

int dashboard_handle_event(Type_Dashboard_State *dashboard, const SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the event
        Returns: Handling status
    */

    if (!dashboard || !event || !dashboard->enabled) {

        return DASHBOARD_EVENT_NONE;

    }

    if (event->type == SDL_QUIT) {

        return DASHBOARD_EVENT_QUIT;

    }

    int sdr_selector_result = dashboard_handle_sdr_selector_event(dashboard, event, win_w, win_h);

    if (sdr_selector_result == 2) {

        return DASHBOARD_EVENT_SDR_CHANGED;

    }

    if (sdr_selector_result == 1) {

        return DASHBOARD_EVENT_NONE;

    }

    if (dashboard_handle_case_sidebar_event(dashboard, event)) {

        return DASHBOARD_EVENT_NONE;

    }

    if (dashboard_handle_case_search_event(dashboard, event)) {

        return DASHBOARD_EVENT_NONE;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if ((key == SDLK_LCTRL || key == SDLK_RCTRL) && dashboard->selected_case >= 0) {

            dashboard->selected_case = -1;
            dashboard->case_desc_editing = 0;
            dashboard->case_desc_edit[0] = '\0';
            return DASHBOARD_EVENT_NONE;

        }

        if (key == SDLK_q) {

            return DASHBOARD_EVENT_QUIT;

        }

    }

    if (dashboard->current_tab == DASHBOARD_EVENT_MAP && dashboard->map_loaded) {

        int current_mouse_x = 0;
        int current_mouse_y = 0;
        SDL_GetMouseState(&current_mouse_x, &current_mouse_y);

        SDL_Rect content = dashboard_content_rect(win_w, win_h);
        int search_h = 64;
        int search_gap = 10;
        SDL_Rect map = {content.x + 12, content.y + 12, content.w - WORLD_MAP_SIDEBAR_W - 40, content.h - 24};

        if (map.h < DASHBOARD_MIN_MAP_H) {

            map.h = content.h - 24;

        }

        SDL_Rect sidebar = {content.x + content.w - WORLD_MAP_SIDEBAR_W, content.y + 12, WORLD_MAP_SIDEBAR_W,
                            content.h - 24 - search_h - search_gap};

        if (sidebar.h < 260) {

            sidebar.h = content.h - 24;

        }

        if (event->type == SDL_MOUSEWHEEL && dashboard->selected_case < 0 &&
            dashboard_point_in_rect(current_mouse_x, current_mouse_y, sidebar)) {

            dashboard->country_case_scroll -= event->wheel.y;

            if (dashboard->country_case_scroll < 0) {

                dashboard->country_case_scroll = 0;

            }
            return DASHBOARD_EVENT_NONE;

        }

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

            if (dashboard_select_case_at(dashboard, map, event->button.x, event->button.y)) {

                return DASHBOARD_EVENT_NONE;

            }

            /* Country hit-testing is deferred until a completed click. Performing
             * WM_country_at() here caused every drag to pause before it started. */

        }

        WORLD_MAP_handle_event(event, map);

        if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {

            int clicked_country = WORLD_MAP_take_clicked_country();

            if (clicked_country != -2) {

                if (clicked_country >= 0) {

                    if (clicked_country != dashboard->locked_country) {

                        dashboard->country_case_scroll = 0;

                    }
                    dashboard->locked_country = clicked_country;
                    dashboard->locked_mouse_x = event->button.x;
                    dashboard->locked_mouse_y = event->button.y;
                    dashboard->hover_country = clicked_country;
                    dashboard->hover_mouse_x = event->button.x;
                    dashboard->hover_mouse_y = event->button.y;

                }

                else {

                    dashboard->locked_country = -1;

                }

            }

        }

    }

    return DASHBOARD_EVENT_NONE;
}

void dashboard_draw(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font_small,
                    TTF_Font *font_medium, int win_w, int win_h, int mouse_x, int mouse_y) {
    /*
        Purpose: Draws the map dashboard
        Returns: No value
    */

    if (!dashboard || !renderer) {

        return;

    }

    SDL_SetRenderDrawColor(renderer, Dashboard_BG.r, Dashboard_BG.g, Dashboard_BG.b, Dashboard_BG.a);
    SDL_RenderClear(renderer);

    dashboard_draw_grid(renderer, win_w, win_h);

    Uint64 now_ms = SDL_GetTicks64();

    if (now_ms - dashboard->last_case_scan_ms > 15000) {

        dashboard_reload_cases(dashboard);
        dashboard->last_case_scan_ms = now_ms;

        dashboard_update_timestamp(dashboard);

    }

    SDL_Rect content = dashboard_content_rect(win_w, win_h);
    draw_filled_rect(renderer, content, (SDL_Color){0, 6, 3, 235});
    draw_outline_rect(renderer, content, Dashboard_Border);

    if (dashboard->map_loaded) {

        int search_h = 64;
        int search_gap = 10;
        SDL_Rect sidebar = {content.x + content.w - WORLD_MAP_SIDEBAR_W, content.y + 12, WORLD_MAP_SIDEBAR_W,
                            content.h - 24 - search_h - search_gap};

        if (sidebar.h < 260) {

            sidebar.h = content.h - 24;

        }
        SDL_Rect search_area = {content.x + content.w - WORLD_MAP_SIDEBAR_W, sidebar.y + sidebar.h + search_gap,
                                WORLD_MAP_SIDEBAR_W, search_h};
        SDL_Rect map = {content.x + 12, content.y + 12, content.w - WORLD_MAP_SIDEBAR_W - 40, content.h - 24};

        if (map.h < DASHBOARD_MIN_MAP_H) {

            map.h = content.h - 24;

        }

        int draw_mouse_x = mouse_x;
        int draw_mouse_y = mouse_y;
        int map_dragging = WORLD_MAP_is_dragging();

        if (map_dragging) {

            draw_mouse_x = dashboard->hover_mouse_x;
            draw_mouse_y = dashboard->hover_mouse_y;

        }

        else if (dashboard->locked_country >= 0 && dashboard->locked_country < (int)WM_DATA.country_count) {

            dashboard->hover_country = dashboard->locked_country;
            draw_mouse_x = dashboard->locked_mouse_x;
            draw_mouse_y = dashboard->locked_mouse_y;

            if (dashboard_find_country_screen_point(map, dashboard->locked_country, &draw_mouse_x, &draw_mouse_y)) {

                dashboard->locked_mouse_x = draw_mouse_x;
                dashboard->locked_mouse_y = draw_mouse_y;
                dashboard->hover_mouse_x = draw_mouse_x;
                dashboard->hover_mouse_y = draw_mouse_y;

            }

        }

        else if (dashboard_point_in_rect(mouse_x, mouse_y, map)) {

            int hovered = WM_country_at(map, mouse_x, mouse_y);

            if (hovered != dashboard->hover_country) {

                dashboard->country_case_scroll = 0;

            }
            dashboard->hover_country = hovered;

            if (hovered >= 0) {

                dashboard->hover_mouse_x = mouse_x;
                dashboard->hover_mouse_y = mouse_y;

            }

        }

        else if (dashboard_point_in_rect(mouse_x, mouse_y, sidebar) && dashboard->hover_country >= 0) {

            draw_mouse_x = dashboard->hover_mouse_x;
            draw_mouse_y = dashboard->hover_mouse_y;

        }

        else {

            dashboard->hover_country = -1;

        }

        WORLD_MAP_draw(renderer, font_small, map, sidebar, draw_mouse_x, draw_mouse_y, "flags");
        dashboard_draw_hover_country_cases(dashboard, renderer, font_small, sidebar);
        dashboard_draw_case_points(dashboard, renderer, font_small, map);
        dashboard_draw_case_search(dashboard, renderer, font_small, search_area);
        dashboard_draw_case_sidebar(dashboard, renderer, font_small, sidebar);

    }

    else {

        draw_text(renderer, font_medium, "world_map.bin was not loaded", content.x + 24, content.y + 26,
                  Dashboard_Warn);
        draw_text(renderer, font_small,
                  "Put world_map.bin in the working directory, or change the path "
                  "passed to dashboard_init().",
                  content.x + 24, content.y + 58, Dashboard_Muted);

    }

    draw_text(renderer, font_small, dashboard->status, DASHBOARD_MARGIN + 8, win_h - 38,
              dashboard->map_loaded ? Dashboard_Muted : Dashboard_Warn);

    dashboard_draw_sdr_selector(renderer, font_small, win_w, win_h, mouse_x, mouse_y);
}
