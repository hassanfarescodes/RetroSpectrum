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
#include <limits.h>
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

/* Kept here so this source also builds when an older DataStore.h omits the
 * server-backed document deletion declaration. */
int DATASTORE_delete_content(const char *document_kind, const char *document_name,
                             int *deleted, char *error, size_t error_size);

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
    int has_valid_location;
    char signal_name[256];
    char frequency_mhz[64];
    char bandwidth[64];
    char start_time[64];
    char end_time[64];
    char calculated_modulation[128];
    char signal_class[128];
    char country[128];
    char latitude_text[64];
    char longitude_text[64];
    char notes[512];
    char file_name[512];
    char document_name[256];
    size_t document_row_index;
    double latitude;
    double longitude;
} Type_Dashboard_Case_Point;

#define DASHBOARD_MAX_CASES 256
#define DASHBOARD_MAX_DOCUMENTS 512
#define DASHBOARD_MAX_CASE_POINTS 4096
#define DASHBOARD_CASE_DIR "Classification"
#define DASHBOARD_CASE_DESCRIPTION_CSV "Classification/CASE_DESCRIPTIONS.csv"
#define DASHBOARD_CASE_METADATA_PREFIX "__case_metadata_"
#define DASHBOARD_CASE_IMAGE_KIND "case_image"
#define DASHBOARD_CASE_IMAGE_PREFIX "__case_image_"
#define DASHBOARD_CASE_IMAGE_PATH_MAX 1024
#define DASHBOARD_CASE_IMAGE_MAX_BYTES (16u * 1024u * 1024u)
#define DASHBOARD_CASE_COLOR_KIND "case_color"
#define DASHBOARD_CASE_COLOR_PREFIX "__case_color_"
#define DASHBOARD_CASE_COLOR_FIELD_COUNT 3
#define DASHBOARD_VISIBLE_SIGNAL_ROWS 3

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

static char Global_Dashboard_Case_Image_Path[DASHBOARD_CASE_IMAGE_PATH_MAX] = "";
static int Global_Dashboard_Case_Image_Path_Cursor = 0;
static int Global_Dashboard_Case_Image_Path_Active = 0;
static SDL_Rect Global_Dashboard_Case_Image_Preview_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Dashboard_Case_Image_Path_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Dashboard_Case_Image_Upload_Rect = {0, 0, 0, 0};
static SDL_Texture *Global_Dashboard_Case_Image_Texture = NULL;
static SDL_Renderer *Global_Dashboard_Case_Image_Texture_Renderer = NULL;
static int Global_Dashboard_Case_Image_Texture_W = 0;
static int Global_Dashboard_Case_Image_Texture_H = 0;
static int Global_Dashboard_Case_Image_Load_Attempted = 0;
static char Global_Dashboard_Case_Image_Loaded_Case[128] = "";

static char Global_Dashboard_Case_Color_Text[DASHBOARD_CASE_COLOR_FIELD_COUNT][4] = {"0", "0", "0"};
static int Global_Dashboard_Case_Color_Cursor[DASHBOARD_CASE_COLOR_FIELD_COUNT] = {1, 1, 1};
static int Global_Dashboard_Case_Color_Active = -1;
static SDL_Rect Global_Dashboard_Case_Color_Rect[DASHBOARD_CASE_COLOR_FIELD_COUNT];
static SDL_Rect Global_Dashboard_Case_Color_Apply_Rect = {0, 0, 0, 0};

static SDL_Rect Global_Dashboard_Signal_Row_Rect[DASHBOARD_VISIBLE_SIGNAL_ROWS];
static int Global_Dashboard_Signal_Row_Point_Index[DASHBOARD_VISIBLE_SIGNAL_ROWS] = {-1, -1, -1};
static int Global_Dashboard_Signal_Row_Count = 0;
static int Global_Dashboard_Selected_Signal = -1;
static int Global_Dashboard_Case_Description_Scroll = 0;
static int Global_Dashboard_Case_Description_Max_Scroll = 0;
static int Global_Dashboard_Case_Description_Selected_Case = -1;
static SDL_Rect Global_Dashboard_Signal_Popup_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Dashboard_Signal_Popup_Close_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Dashboard_Signal_Delete_Rect = {0, 0, 0, 0};

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
static void dashboard_case_image_clear_cache(void);

static void dashboard_sdr_selector_rects(int win_w, int win_h, SDL_Rect *label_rect, SDL_Rect *button_rect) {
    /*
        Purpose: Computes the bottom-right SDR selector rectangles
        Returns: No value
    */

    SDL_Rect button = {win_w - DASHBOARD_MARGIN - DASHBOARD_SDR_BUTTON_W, win_h - 48, DASHBOARD_SDR_BUTTON_W,
                       DASHBOARD_SDR_BUTTON_H};
    SDL_Rect label = {button.x - 112, button.y, 102, button.h};

    if (label_rect) {

        *label_rect = label;

    }

    if (button_rect) {

        *button_rect = button;

    }
}

static void dashboard_sdr_copy_truncated(TTF_Font *font, char *dst, size_t dst_size, const char *src, int max_width) {
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

    for (size_t index = 0; index < device_count && Global_Dashboard_SDR_Option_Count < DASHBOARD_MAX_SDR_OPTIONS;
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

        Type_Dashboard_SDR_Option *option = &Global_Dashboard_SDR_Options[Global_Dashboard_SDR_Option_Count];

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

static void dashboard_draw_sdr_selector(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h, int mouse_x,
                                        int mouse_y) {
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
    dashboard_sdr_copy_truncated(font, selected_text, sizeof(selected_text), RETROSPECTRUM_sdr_selected_label(),
                                 button_rect.w - 42);
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
    SDL_Rect menu = {button_rect.x, button_rect.y - (row_count * DASHBOARD_SDR_ROW_H) - 4, button_rect.w,
                     row_count * DASHBOARD_SDR_ROW_H};

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
        dashboard_sdr_copy_truncated(font, option_text, sizeof(option_text), Global_Dashboard_SDR_Options[index].label,
                                     row.w - 38);
        SDL_Color text_color = RETROSPECTRUM_sdr_args_is_selected(Global_Dashboard_SDR_Options[index].args)
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

static int dashboard_handle_sdr_selector_event(Type_Dashboard_State *dashboard, const SDL_Event *event, int win_w,
                                               int win_h) {
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

    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE && Global_Dashboard_SDR_Menu_Open) {

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
    SDL_Rect menu = {button_rect.x, button_rect.y - (row_count * DASHBOARD_SDR_ROW_H) - 4, button_rect.w,
                     row_count * DASHBOARD_SDR_ROW_H};

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

static void dashboard_case_color_document_name(const char *case_number, char *document_name,
                                               size_t document_name_size) {
    /*
        Purpose: Builds the deterministic server document name for a case color
        Returns: No value
    */

    if (!document_name || document_name_size == 0) {

        return;

    }

    snprintf(document_name, document_name_size, "%s%s", DASHBOARD_CASE_COLOR_PREFIX,
             case_number && case_number[0] ? case_number : "UNCASED");
}

static int dashboard_case_color_parse(const char *text, SDL_Color *color) {
    /*
        Purpose: Parses a stored RGB triplet
        Returns: Success status
    */

    int red;
    int green;
    int blue;
    char trailing;

    if (!text || !color) {

        return 0;

    }

    if (sscanf(text, " %d , %d , %d %c", &red, &green, &blue, &trailing) != 3 ||
        red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255) {

        return 0;

    }

    color->r = (Uint8)red;
    color->g = (Uint8)green;
    color->b = (Uint8)blue;
    color->a = 255;
    return 1;
}

static void dashboard_load_case_colors(void) {
    /*
        Purpose: Loads globally stored case colors from the encrypted server database
        Returns: No value
    */

    static Type_DataStore_Document_Summary documents[DASHBOARD_MAX_DOCUMENTS];
    char database_error[256] = "";
    size_t document_count = 0;

    if (!DATASTORE_list_documents(DASHBOARD_CASE_COLOR_KIND, documents,
                                  sizeof(documents) / sizeof(documents[0]),
                                  &document_count, database_error, sizeof(database_error))) {

        return;

    }

    for (size_t i = 0; i < document_count; i++) {
        unsigned char *content = NULL;
        size_t content_size = 0;
        int found = 0;
        int case_index;
        char text[32];
        SDL_Color color;

        if (strncmp(documents[i].document_name, DASHBOARD_CASE_COLOR_PREFIX,
                    strlen(DASHBOARD_CASE_COLOR_PREFIX)) != 0 ||
            !documents[i].case_number[0]) {

            continue;

        }

        case_index = dashboard_find_case_index(documents[i].case_number);

        if (case_index < 0) {

            continue;

        }

        if (!DATASTORE_load_content(DASHBOARD_CASE_COLOR_KIND, documents[i].document_name,
                                    &content, &content_size, &found,
                                    database_error, sizeof(database_error)) ||
            !found || !content || content_size == 0 || content_size >= sizeof(text)) {

            DATASTORE_free_content(content, content_size);
            continue;

        }

        memcpy(text, content, content_size);
        text[content_size] = '\0';

        if (dashboard_case_color_parse(text, &color)) {

            Global_Dashboard_Cases[case_index].color = color;

        }

        DATASTORE_free_content(content, content_size);
    }
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

static void dashboard_load_case_content(unsigned char *content, size_t content_size,
                                        const char *document_name) {
    /*
        Purpose: Loads the case content
        Returns: No value
    */

    char *cursor;
    char *limit;
    int first = 1;
    size_t data_row_index = 0;

    if (!content || content_size == 0) {

        return;

    }

    cursor = (char *)content;
    limit = cursor + content_size;

    while (cursor < limit) {
        char *line = cursor;
        char *newline = memchr(cursor, '\n', (size_t)(limit - cursor));
        char saved = '\0';
        int is_header = 0;

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

                is_header = 1;

            }

        }

        if (!is_header && line[0]) {

            char fields[16][512];
            double latitude = 0.0;
            double longitude = 0.0;
            int field_count;
            int has_valid_location = 0;
            size_t current_row_index = data_row_index++;

            memset(fields, 0, sizeof(fields));
            field_count = dashboard_csv_parse_line(line, fields, 16);

            if (field_count >= 12) {

                has_valid_location =
                    dashboard_parse_coordinate(fields[9], -90.0, 90.0, &latitude) &&
                    dashboard_parse_coordinate(fields[10], -180.0, 180.0, &longitude);

            }

            if (field_count >= 12 && fields[0][0] &&
                Global_Dashboard_Case_Point_Count < DASHBOARD_MAX_CASE_POINTS) {

                int case_index = dashboard_case_index_for(fields[0]);

                if (case_index >= 0) {

                    Type_Dashboard_Case_Point *point =
                        &Global_Dashboard_Case_Points[Global_Dashboard_Case_Point_Count++];

                    memset(point, 0, sizeof(*point));
                    point->case_index = case_index;
                    dashboard_copy_text(point->signal_name, sizeof(point->signal_name), fields[1]);
                    dashboard_copy_text(point->frequency_mhz, sizeof(point->frequency_mhz), fields[2]);
                    dashboard_copy_text(point->bandwidth, sizeof(point->bandwidth), fields[3]);
                    dashboard_copy_text(point->start_time, sizeof(point->start_time), fields[4]);
                    dashboard_copy_text(point->end_time, sizeof(point->end_time), fields[5]);
                    dashboard_copy_text(point->calculated_modulation, sizeof(point->calculated_modulation), fields[6]);
                    dashboard_copy_text(point->signal_class, sizeof(point->signal_class), fields[7]);
                    dashboard_copy_text(point->country, sizeof(point->country), fields[8]);
                    point->has_valid_location = has_valid_location;

                    if (has_valid_location) {

                        dashboard_copy_text(point->latitude_text, sizeof(point->latitude_text), fields[9]);
                        dashboard_copy_text(point->longitude_text, sizeof(point->longitude_text), fields[10]);

                    }

                    else {

                        dashboard_copy_text(point->latitude_text, sizeof(point->latitude_text), "Unknown Location");
                        dashboard_copy_text(point->longitude_text, sizeof(point->longitude_text), "Unknown Location");

                    }

                    dashboard_copy_text(point->notes, sizeof(point->notes), fields[11]);
                    dashboard_unescape_multiline_text(point->notes);

                    if (field_count >= 13) {

                        dashboard_copy_text(point->file_name, sizeof(point->file_name), fields[12]);

                    }

                    dashboard_copy_text(point->document_name, sizeof(point->document_name), document_name);
                    point->document_row_index = current_row_index;
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
    char selected_signal_document[256] = "";
    size_t selected_signal_row = 0;
    int restore_selected_signal = 0;
    char database_error[256] = "";
    size_t document_count = 0;

    if (Global_Dashboard_Selected_Signal >= 0 &&
        Global_Dashboard_Selected_Signal < Global_Dashboard_Case_Point_Count) {

        Type_Dashboard_Case_Point *selected_point =
            &Global_Dashboard_Case_Points[Global_Dashboard_Selected_Signal];
        dashboard_copy_text(selected_signal_document, sizeof(selected_signal_document),
                            selected_point->document_name);
        selected_signal_row = selected_point->document_row_index;
        restore_selected_signal = selected_signal_document[0] != '\0';

    }

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

            dashboard_load_case_content(content, content_size, documents[i].document_name);

        }

        DATASTORE_free_content(content, content_size);
    }

    dashboard_load_case_descriptions();
    dashboard_load_case_metadata();
    dashboard_load_case_colors();

    Global_Dashboard_Selected_Signal = -1;

    if (restore_selected_signal) {

        for (int i = 0; i < Global_Dashboard_Case_Point_Count; i++) {

            if (Global_Dashboard_Case_Points[i].document_row_index == selected_signal_row &&
                strcmp(Global_Dashboard_Case_Points[i].document_name, selected_signal_document) == 0) {

                Global_Dashboard_Selected_Signal = i;
                break;

            }
        }
    }

    if (dashboard) {

        dashboard->selected_case = selected_case_number[0] ? dashboard_find_case_index(selected_case_number) : -1;

        if (dashboard->selected_case < 0) {

            dashboard->case_desc_editing = 0;
            dashboard->case_desc_edit[0] = '\0';
            Global_Dashboard_Case_Image_Path_Active = 0;
            Global_Dashboard_Case_Image_Path[0] = '\0';
            Global_Dashboard_Case_Image_Path_Cursor = 0;
            Global_Dashboard_Case_Color_Active = -1;

        }

        dashboard_case_image_clear_cache();

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

        if (pt->case_index < 0 || pt->case_index >= Global_Dashboard_Case_Count ||
            !pt->has_valid_location) {

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

        if (!pt->has_valid_location ||
            !dashboard_lonlat_to_screen(pt->longitude, pt->latitude, map, &sx, &sy)) {

            continue;

        }

        int dx = x - sx;
        int dy = y - sy;

        if (dx * dx + dy * dy <= 100) {

            dashboard->selected_case = pt->case_index;
            dashboard->case_desc_editing = 0;
            Global_Dashboard_Case_Description_Scroll = 0;
            Global_Dashboard_Case_Description_Max_Scroll = 0;
            Global_Dashboard_Case_Description_Selected_Case = pt->case_index;
            Global_Dashboard_Case_Image_Path_Active = 0;
            Global_Dashboard_Case_Image_Path[0] = '\0';
            Global_Dashboard_Case_Image_Path_Cursor = 0;
            Global_Dashboard_Case_Color_Active = -1;
            Global_Dashboard_Selected_Signal = -1;

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

static int dashboard_description_text_width(TTF_Font *font, const char *text) {
    /*
        Purpose: Measures one description line while retaining a safe fallback
        Returns: Text width in pixels
    */

    int width = 0;
    int height = 0;

    if (!font || !text || text[0] == '\0') {

        return 0;

    }

    if (TTF_SizeText(font, text, &width, &height) != 0) {

        width = (int)strlen(text) * 8;

    }

    return width;
}

static void dashboard_description_emit_line(SDL_Renderer *renderer, TTF_Font *font,
                                            const char *line, SDL_Rect rect,
                                            SDL_Color color, int first_line,
                                            int visible_lines, int line_height,
                                            int *line_index) {
    /*
        Purpose: Counts and conditionally draws one wrapped description line
        Returns: No value
    */

    if (!line_index) {

        return;

    }

    if (renderer && font && line && *line_index >= first_line &&
        *line_index < first_line + visible_lines && line[0] != '\0') {

        draw_text(renderer, font, line, rect.x,
                  rect.y + (*line_index - first_line) * line_height, color);

    }

    (*line_index)++;
}

static int dashboard_wrap_description_text(SDL_Renderer *renderer, TTF_Font *font,
                                           const char *text, SDL_Rect rect,
                                           SDL_Color color, int first_line) {
    /*
        Purpose: Wraps, clips, and optionally scrolls the selected case description
        Returns: Total wrapped line count
    */

    char line[512] = "";
    int line_length = 0;
    int line_index = 0;
    int line_height;
    int visible_lines;
    SDL_Rect previous_clip = {0, 0, 0, 0};
    SDL_bool had_clip = SDL_FALSE;

    if (!font || !text || rect.w <= 0 || rect.h <= 0) {

        return 0;

    }

    line_height = TTF_FontHeight(font) + 4;

    if (line_height < 1) {

        line_height = 1;

    }

    visible_lines = rect.h / line_height;

    if (visible_lines < 1) {

        visible_lines = 1;

    }

    if (first_line < 0) {

        first_line = 0;

    }

    if (renderer) {

        had_clip = SDL_RenderIsClipEnabled(renderer);
        SDL_RenderGetClipRect(renderer, &previous_clip);
        SDL_RenderSetClipRect(renderer, &rect);

    }

    for (const unsigned char *cursor = (const unsigned char *)text; ; cursor++) {
        unsigned char character = *cursor;

        if (character == '\r') {

            continue;

        }

        if (character == '\n' || character == '\0') {

            while (line_length > 0 && line[line_length - 1] == ' ') {

                line[--line_length] = '\0';

            }

            if (line_length > 0 || character == '\n') {

                dashboard_description_emit_line(renderer, font, line, rect, color,
                                                first_line, visible_lines, line_height,
                                                &line_index);

            }

            line[0] = '\0';
            line_length = 0;

            if (character == '\0') {

                break;

            }

            continue;

        }

        if (character == '\t') {

            character = ' ';

        }

        if (character == ' ' && line_length == 0) {

            continue;

        }

        if (line_length + 1 >= (int)sizeof(line)) {

            dashboard_description_emit_line(renderer, font, line, rect, color,
                                            first_line, visible_lines, line_height,
                                            &line_index);
            line[0] = '\0';
            line_length = 0;

            if (character == ' ') {

                continue;

            }

        }

        line[line_length++] = (char)character;
        line[line_length] = '\0';

        while (line_length > 0 && dashboard_description_text_width(font, line) > rect.w) {
            int break_position = -1;

            for (int i = line_length - 1; i > 0; i--) {

                if (line[i] == ' ') {

                    break_position = i;
                    break;

                }
            }

            if (break_position > 0) {
                char remainder[512];
                int remainder_length;

                snprintf(remainder, sizeof(remainder), "%s", line + break_position + 1);
                line[break_position] = '\0';
                line_length = break_position;

                while (line_length > 0 && line[line_length - 1] == ' ') {

                    line[--line_length] = '\0';

                }

                dashboard_description_emit_line(renderer, font, line, rect, color,
                                                first_line, visible_lines, line_height,
                                                &line_index);

                snprintf(line, sizeof(line), "%s", remainder);
                line_length = (int)strlen(line);
                remainder_length = line_length;

                while (remainder_length > 0 && line[0] == ' ') {

                    memmove(line, line + 1, (size_t)remainder_length);
                    remainder_length--;

                }

                line_length = remainder_length;

            }

            else {
                char remainder[512];
                int fit_length = line_length - 1;

                while (fit_length > 1) {
                    char saved = line[fit_length];

                    line[fit_length] = '\0';

                    if (dashboard_description_text_width(font, line) <= rect.w) {

                        line[fit_length] = saved;
                        break;

                    }

                    line[fit_length] = saved;
                    fit_length--;
                }

                if (fit_length < 1) {

                    fit_length = 1;

                }

                snprintf(remainder, sizeof(remainder), "%s", line + fit_length);
                line[fit_length] = '\0';
                dashboard_description_emit_line(renderer, font, line, rect, color,
                                                first_line, visible_lines, line_height,
                                                &line_index);
                snprintf(line, sizeof(line), "%s", remainder);
                line_length = (int)strlen(line);

            }
        }
    }

    if (renderer) {

        SDL_RenderSetClipRect(renderer, had_clip ? &previous_clip : NULL);

    }

    return line_index;
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

static void dashboard_case_image_clear_cache(void) {
    /*
        Purpose: Releases the cached case image texture
        Returns: No value
    */

    if (Global_Dashboard_Case_Image_Texture) {

        SDL_DestroyTexture(Global_Dashboard_Case_Image_Texture);

    }

    Global_Dashboard_Case_Image_Texture = NULL;
    Global_Dashboard_Case_Image_Texture_Renderer = NULL;
    Global_Dashboard_Case_Image_Texture_W = 0;
    Global_Dashboard_Case_Image_Texture_H = 0;
    Global_Dashboard_Case_Image_Load_Attempted = 0;
    Global_Dashboard_Case_Image_Loaded_Case[0] = '\0';
}

static void dashboard_case_image_document_name(const char *case_number, char *document_name,
                                               size_t document_name_size) {
    /*
        Purpose: Builds the deterministic encrypted image document name for a case
        Returns: No value
    */

    if (!document_name || document_name_size == 0) {

        return;

    }

    snprintf(document_name, document_name_size, "%s%s", DASHBOARD_CASE_IMAGE_PREFIX,
             case_number && case_number[0] ? case_number : "UNCASED");
}

static void dashboard_case_image_trim_path(char *path) {
    /*
        Purpose: Trims surrounding whitespace from an entered path
        Returns: No value
    */

    char *start;
    size_t length;

    if (!path) {

        return;

    }

    start = path;

    while (*start && isspace((unsigned char)*start)) {

        start++;

    }

    if (start != path) {

        memmove(path, start, strlen(start) + 1U);

    }

    length = strlen(path);

    while (length > 0 && isspace((unsigned char)path[length - 1])) {

        path[--length] = '\0';

    }
}

static int dashboard_case_image_read_file(const char *path, unsigned char **content, size_t *content_size,
                                          char *error, size_t error_size) {
    /*
        Purpose: Reads a bounded local image file selected by path
        Returns: Success status
    */

    struct stat st;
    FILE *file = NULL;
    unsigned char *buffer = NULL;
    size_t size;

    if (content) {

        *content = NULL;

    }

    if (content_size) {

        *content_size = 0;

    }

    if (!path || !path[0] || !content || !content_size || stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0) {

        snprintf(error, error_size, "Image path does not identify a readable file.");
        return 0;

    }

    if ((uint64_t)st.st_size > DASHBOARD_CASE_IMAGE_MAX_BYTES) {

        snprintf(error, error_size, "Image exceeds the 16 MiB upload limit.");
        return 0;

    }

    size = (size_t)st.st_size;
    buffer = malloc(size);

    if (!buffer) {

        snprintf(error, error_size, "Unable to allocate memory for the image.");
        return 0;

    }

    file = fopen(path, "rb");

    if (!file || fread(buffer, 1, size, file) != size) {

        if (file) {

            fclose(file);

        }
        memset(buffer, 0, size);
        free(buffer);
        snprintf(error, error_size, "Unable to read the image file.");
        return 0;

    }

    if (fclose(file) != 0) {

        memset(buffer, 0, size);
        free(buffer);
        snprintf(error, error_size, "Unable to finish reading the image file.");
        return 0;

    }

    *content = buffer;
    *content_size = size;
    return 1;
}

static int dashboard_case_image_validate(const unsigned char *content, size_t content_size) {
    /*
        Purpose: Verifies that the selected bytes decode as an image
        Returns: Success status
    */

    SDL_RWops *source;
    SDL_Surface *surface;

    if (!content || content_size == 0 || content_size > INT32_MAX) {

        return 0;

    }

    source = SDL_RWFromConstMem(content, (int)content_size);

    if (!source) {

        return 0;

    }

    surface = IMG_Load_RW(source, 1);

    if (!surface) {

        return 0;

    }

    if (surface->w <= 0 || surface->h <= 0 || surface->w > 16384 || surface->h > 16384) {

        SDL_FreeSurface(surface);
        return 0;

    }

    SDL_FreeSurface(surface);
    return 1;
}

static int dashboard_case_image_upload(Type_Dashboard_State *dashboard) {
    /*
        Purpose: Uploads the entered image to the encrypted server database
        Returns: Success status
    */

    Type_Dashboard_Case_Info *info;
    unsigned char *content = NULL;
    size_t content_size = 0;
    char path[DASHBOARD_CASE_IMAGE_PATH_MAX];
    char document_name[256];
    char error[256] = "";

    if (!dashboard || dashboard->selected_case < 0 || dashboard->selected_case >= Global_Dashboard_Case_Count) {

        return 0;

    }

    info = &Global_Dashboard_Cases[dashboard->selected_case];
    dashboard_copy_text(path, sizeof(path), Global_Dashboard_Case_Image_Path);
    dashboard_case_image_trim_path(path);

    if (!path[0]) {

        snprintf(dashboard->status, sizeof(dashboard->status), "Enter the full path of an image.");
        return 0;

    }

    if (!dashboard_case_image_read_file(path, &content, &content_size, error, sizeof(error))) {

        snprintf(dashboard->status, sizeof(dashboard->status), "%s", error);
        return 0;

    }

    if (!dashboard_case_image_validate(content, content_size)) {

        memset(content, 0, content_size);
        free(content);
        snprintf(dashboard->status, sizeof(dashboard->status), "The selected file is not a supported image.");
        return 0;

    }

    dashboard_case_image_document_name(info->case_number, document_name, sizeof(document_name));

    if (!DATASTORE_save_content(DASHBOARD_CASE_IMAGE_KIND, document_name, info->case_number, content, content_size,
                                error, sizeof(error))) {

        memset(content, 0, content_size);
        free(content);
        snprintf(dashboard->status, sizeof(dashboard->status), "Unable to upload image: %.170s", error);
        return 0;

    }

    memset(content, 0, content_size);
    free(content);
    Global_Dashboard_Case_Image_Path[0] = '\0';
    Global_Dashboard_Case_Image_Path_Cursor = 0;
    Global_Dashboard_Case_Image_Path_Active = 0;
    dashboard->case_desc_editing = 0;
    dashboard_case_image_clear_cache();
    snprintf(dashboard->status, sizeof(dashboard->status), "Case image uploaded to the encrypted server database.");
    return 1;
}

static SDL_Texture *dashboard_case_image_load_texture(SDL_Renderer *renderer, const char *case_number) {
    /*
        Purpose: Loads the selected case image from encrypted storage into an SDL texture
        Returns: Texture or NULL
    */

    unsigned char *content = NULL;
    size_t content_size = 0;
    int found = 0;
    char document_name[256];
    char error[256] = "";
    SDL_RWops *source;

    if (!renderer || !case_number || !case_number[0]) {

        return NULL;

    }

    if (Global_Dashboard_Case_Image_Texture_Renderer != renderer ||
        strcmp(Global_Dashboard_Case_Image_Loaded_Case, case_number) != 0) {

        dashboard_case_image_clear_cache();
        Global_Dashboard_Case_Image_Texture_Renderer = renderer;
        dashboard_copy_text(Global_Dashboard_Case_Image_Loaded_Case,
                            sizeof(Global_Dashboard_Case_Image_Loaded_Case), case_number);

    }

    if (Global_Dashboard_Case_Image_Load_Attempted) {

        return Global_Dashboard_Case_Image_Texture;

    }

    Global_Dashboard_Case_Image_Load_Attempted = 1;
    dashboard_case_image_document_name(case_number, document_name, sizeof(document_name));

    if (!DATASTORE_load_content(DASHBOARD_CASE_IMAGE_KIND, document_name, &content, &content_size, &found, error,
                                sizeof(error)) ||
        !found || content_size == 0 || content_size > INT32_MAX) {

        DATASTORE_free_content(content, content_size);
        return NULL;

    }

    source = SDL_RWFromConstMem(content, (int)content_size);

    if (source) {

        Global_Dashboard_Case_Image_Texture = IMG_LoadTexture_RW(renderer, source, 1);

    }

    if (Global_Dashboard_Case_Image_Texture) {

        SDL_QueryTexture(Global_Dashboard_Case_Image_Texture, NULL, NULL, &Global_Dashboard_Case_Image_Texture_W,
                         &Global_Dashboard_Case_Image_Texture_H);

    }

    DATASTORE_free_content(content, content_size);
    return Global_Dashboard_Case_Image_Texture;
}

static void dashboard_case_image_draw_preview(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect,
                                              const char *case_number) {
    /*
        Purpose: Draws the case image or a grey missing-image placeholder
        Returns: No value
    */

    SDL_Texture *texture = dashboard_case_image_load_texture(renderer, case_number);

    (void)font;

    if (!texture || Global_Dashboard_Case_Image_Texture_W <= 0 || Global_Dashboard_Case_Image_Texture_H <= 0) {
        SDL_Color question_color = {220, 220, 220, 255};
        int available_w = rect.w - 20;
        int available_h = rect.h - 20;
        int question_h = available_h * 3 / 4;
        int question_w;
        int thickness;
        int question_x;
        int question_y;
        SDL_Rect top;
        SDL_Rect upper_left;
        SDL_Rect upper_right;
        SDL_Rect middle;
        SDL_Rect stem;
        SDL_Rect dot;

        draw_filled_rect(renderer, rect, (SDL_Color){72, 72, 72, 255});

        if (question_h > 110) {

            question_h = 110;

        }

        if (question_h < 44) {

            question_h = 44;

        }

        question_w = question_h * 3 / 5;

        if (question_w > available_w * 3 / 4) {

            question_w = available_w * 3 / 4;
            question_h = question_w * 5 / 3;

        }

        thickness = question_h / 10;

        if (thickness < 4) {

            thickness = 4;

        }

        if (thickness > 12) {

            thickness = 12;

        }

        question_x = rect.x + (rect.w - question_w) / 2;
        question_y = rect.y + (rect.h - question_h) / 2;

        top = (SDL_Rect){question_x + thickness, question_y, question_w - (thickness * 2), thickness};
        upper_left = (SDL_Rect){question_x, question_y + thickness, thickness, question_h / 5};
        upper_right = (SDL_Rect){question_x + question_w - thickness, question_y + thickness, thickness,
                                 question_h / 3};
        middle = (SDL_Rect){question_x + question_w / 2, question_y + question_h / 3,
                            question_w / 2, thickness};
        stem = (SDL_Rect){question_x + question_w / 2 - thickness / 2,
                          question_y + question_h / 3, thickness, question_h / 3};
        dot = (SDL_Rect){question_x + question_w / 2 - thickness / 2,
                         question_y + question_h - thickness, thickness, thickness};

        draw_filled_rect(renderer, top, question_color);
        draw_filled_rect(renderer, upper_left, question_color);
        draw_filled_rect(renderer, upper_right, question_color);
        draw_filled_rect(renderer, middle, question_color);
        draw_filled_rect(renderer, stem, question_color);
        draw_filled_rect(renderer, dot, question_color);
        draw_outline_rect(renderer, rect, Dashboard_Border);
        return;

    }

    {
        double scale_x = (double)(rect.w - 4) / (double)Global_Dashboard_Case_Image_Texture_W;
        double scale_y = (double)(rect.h - 4) / (double)Global_Dashboard_Case_Image_Texture_H;
        double scale = scale_x < scale_y ? scale_x : scale_y;
        SDL_Rect destination;

        destination.w = (int)((double)Global_Dashboard_Case_Image_Texture_W * scale);
        destination.h = (int)((double)Global_Dashboard_Case_Image_Texture_H * scale);
        destination.x = rect.x + (rect.w - destination.w) / 2;
        destination.y = rect.y + (rect.h - destination.h) / 2;
        SDL_RenderCopy(renderer, texture, NULL, &destination);
    }

    draw_outline_rect(renderer, rect, Dashboard_Border);
}

static void dashboard_case_image_insert_text(const char *text) {
    /*
        Purpose: Inserts text at the image path cursor
        Returns: No value
    */

    size_t length;
    size_t added;
    int cursor;

    if (!text || !text[0]) {

        return;

    }

    length = strlen(Global_Dashboard_Case_Image_Path);
    added = strlen(text);
    cursor = Global_Dashboard_Case_Image_Path_Cursor;

    if (cursor < 0) {

        cursor = 0;

    }

    if ((size_t)cursor > length) {

        cursor = (int)length;

    }

    if (length + added >= sizeof(Global_Dashboard_Case_Image_Path)) {

        added = sizeof(Global_Dashboard_Case_Image_Path) - length - 1U;

    }

    if (added == 0) {

        return;

    }

    memmove(Global_Dashboard_Case_Image_Path + cursor + (int)added,
            Global_Dashboard_Case_Image_Path + cursor, length - (size_t)cursor + 1U);
    memcpy(Global_Dashboard_Case_Image_Path + cursor, text, added);
    Global_Dashboard_Case_Image_Path_Cursor = cursor + (int)added;
}

static void dashboard_case_image_paste_path(void) {
    /*
        Purpose: Pastes a single-line path from the clipboard
        Returns: No value
    */

    char *clipboard = SDL_GetClipboardText();

    if (!clipboard) {

        return;

    }

    for (char *p = clipboard; *p; p++) {

        if (*p == '\r' || *p == '\n') {

            *p = '\0';
            break;

        }
    }

    dashboard_case_image_insert_text(clipboard);
    SDL_free(clipboard);
}

static void dashboard_case_image_draw_path_input(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect) {
    /*
        Purpose: Draws the image path input and cursor
        Returns: No value
    */

    const char *text = Global_Dashboard_Case_Image_Path;
    const char *shown = text[0] ? text : "Enter full image path";
    SDL_Color text_color = text[0] ? Dashboard_Text : Dashboard_Muted;
    int length = (int)strlen(text);
    int cursor = Global_Dashboard_Case_Image_Path_Cursor;
    int maximum_characters = (rect.w - 18) / 8;
    int start = 0;
    int end = length;
    int cursor_x = rect.x + 8;

    if (maximum_characters < 1) {

        maximum_characters = 1;

    }

    if (cursor < 0) {

        cursor = 0;

    }

    if (cursor > length) {

        cursor = length;

    }

    if (text[0]) {

        if (cursor > maximum_characters) {

            start = cursor - maximum_characters;

        }

        end = start + maximum_characters;

        if (end > length) {

            end = length;
            start = end - maximum_characters;

            if (start < 0) {

                start = 0;

            }

        }

    }

    draw_filled_rect(renderer, rect, (SDL_Color){0, 12, 5, 255});
    draw_outline_rect(renderer, rect,
                      Global_Dashboard_Case_Image_Path_Active ? Dashboard_Border_Hi : Dashboard_Border);

    SDL_RenderSetClipRect(renderer, &rect);

    if (text[0]) {
        char visible[DASHBOARD_CASE_IMAGE_PATH_MAX];
        int visible_length = end - start;

        if (visible_length < 0) {

            visible_length = 0;

        }

        memcpy(visible, text + start, (size_t)visible_length);
        visible[visible_length] = '\0';
        draw_text(renderer, font, visible, rect.x + 8, rect.y + 7, text_color);

        if (Global_Dashboard_Case_Image_Path_Active && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {
            char before[DASHBOARD_CASE_IMAGE_PATH_MAX];
            int before_length = cursor - start;
            int text_w = 0;
            int text_h = 0;

            if (before_length < 0) {

                before_length = 0;

            }

            if (before_length > visible_length) {

                before_length = visible_length;

            }

            memcpy(before, text + start, (size_t)before_length);
            before[before_length] = '\0';

            if (TTF_SizeText(font, before, &text_w, &text_h) != 0) {

                text_w = before_length * 8;

            }

            cursor_x += text_w;
            SDL_SetRenderDrawColor(renderer, Dashboard_Border_Hi.r, Dashboard_Border_Hi.g, Dashboard_Border_Hi.b,
                                   Dashboard_Border_Hi.a);
            SDL_RenderDrawLine(renderer, cursor_x, rect.y + 5, cursor_x, rect.y + rect.h - 5);
        }

    }

    else {

        draw_text(renderer, font, shown, rect.x + 8, rect.y + 7, text_color);

    }

    SDL_RenderSetClipRect(renderer, NULL);
}

static void dashboard_case_color_sync_inputs(SDL_Color color) {
    /*
        Purpose: Synchronizes the RGB editor with the selected case color
        Returns: No value
    */

    snprintf(Global_Dashboard_Case_Color_Text[0], sizeof(Global_Dashboard_Case_Color_Text[0]), "%u",
             (unsigned int)color.r);
    snprintf(Global_Dashboard_Case_Color_Text[1], sizeof(Global_Dashboard_Case_Color_Text[1]), "%u",
             (unsigned int)color.g);
    snprintf(Global_Dashboard_Case_Color_Text[2], sizeof(Global_Dashboard_Case_Color_Text[2]), "%u",
             (unsigned int)color.b);

    for (int i = 0; i < DASHBOARD_CASE_COLOR_FIELD_COUNT; i++) {

        Global_Dashboard_Case_Color_Cursor[i] = (int)strlen(Global_Dashboard_Case_Color_Text[i]);

    }
}

static void dashboard_case_color_insert_text(const char *text) {
    /*
        Purpose: Inserts numeric text into the active RGB field
        Returns: No value
    */

    char *field;
    int cursor;
    size_t length;

    if (Global_Dashboard_Case_Color_Active < 0 ||
        Global_Dashboard_Case_Color_Active >= DASHBOARD_CASE_COLOR_FIELD_COUNT ||
        !text || !text[0]) {

        return;

    }

    field = Global_Dashboard_Case_Color_Text[Global_Dashboard_Case_Color_Active];
    cursor = Global_Dashboard_Case_Color_Cursor[Global_Dashboard_Case_Color_Active];
    length = strlen(field);

    for (const char *p = text; *p; p++) {

        if (!isdigit((unsigned char)*p) || length >= sizeof(Global_Dashboard_Case_Color_Text[0]) - 1U) {

            continue;

        }

        if (cursor < 0) {

            cursor = 0;

        }

        if ((size_t)cursor > length) {

            cursor = (int)length;

        }

        memmove(field + cursor + 1, field + cursor, length - (size_t)cursor + 1U);
        field[cursor] = *p;
        cursor++;
        length++;
    }

    Global_Dashboard_Case_Color_Cursor[Global_Dashboard_Case_Color_Active] = cursor;
}

static int dashboard_case_color_save(Type_Dashboard_State *dashboard) {
    /*
        Purpose: Saves a selected case color to the encrypted server database
        Returns: Success status
    */

    Type_Dashboard_Case_Info *info;
    SDL_Color color;
    int values[DASHBOARD_CASE_COLOR_FIELD_COUNT];
    char content[32];
    char document_name[256];
    char database_error[256] = "";

    if (!dashboard || dashboard->selected_case < 0 ||
        dashboard->selected_case >= Global_Dashboard_Case_Count) {

        return 0;

    }

    for (int i = 0; i < DASHBOARD_CASE_COLOR_FIELD_COUNT; i++) {
        char *end = NULL;
        long value;

        if (!Global_Dashboard_Case_Color_Text[i][0]) {

            snprintf(dashboard->status, sizeof(dashboard->status), "RGB values must be between 0 and 255.");
            return 0;

        }

        errno = 0;
        value = strtol(Global_Dashboard_Case_Color_Text[i], &end, 10);

        if (errno == ERANGE || !end || *end != '\0' || value < 0 || value > 255) {

            snprintf(dashboard->status, sizeof(dashboard->status), "RGB values must be between 0 and 255.");
            return 0;

        }

        values[i] = (int)value;
    }

    info = &Global_Dashboard_Cases[dashboard->selected_case];
    color = (SDL_Color){(Uint8)values[0], (Uint8)values[1], (Uint8)values[2], 255};
    snprintf(content, sizeof(content), "%d,%d,%d", values[0], values[1], values[2]);
    dashboard_case_color_document_name(info->case_number, document_name, sizeof(document_name));

    if (!DATASTORE_save_content(DASHBOARD_CASE_COLOR_KIND, document_name, info->case_number,
                                content, strlen(content), database_error, sizeof(database_error))) {

        snprintf(dashboard->status, sizeof(dashboard->status),
                 "Unable to update case color: %.170s", database_error);
        return 0;

    }

    info->color = color;
    Global_Dashboard_Case_Color_Active = -1;
    dashboard->case_desc_editing = Global_Dashboard_Case_Image_Path_Active;
    dashboard_case_color_sync_inputs(color);
    snprintf(dashboard->status, sizeof(dashboard->status),
             "Case color updated for all connected users.");
    return 1;
}

static void dashboard_case_color_draw_input(SDL_Renderer *renderer, TTF_Font *font,
                                            SDL_Rect rect, int field_index) {
    /*
        Purpose: Draws one RGB channel input
        Returns: No value
    */

    const char *text = Global_Dashboard_Case_Color_Text[field_index];
    int text_w = 0;
    int text_h = 0;

    draw_filled_rect(renderer, rect, (SDL_Color){0, 9, 4, 255});
    draw_outline_rect(renderer, rect,
                      Global_Dashboard_Case_Color_Active == field_index
                          ? Dashboard_Border_Hi
                          : Dashboard_Border);

    if (TTF_SizeText(font, text, &text_w, &text_h) != 0) {

        text_w = (int)strlen(text) * 8;

    }

    draw_text(renderer, font, text, rect.x + (rect.w - text_w) / 2, rect.y + 7, Dashboard_Text);

    if (Global_Dashboard_Case_Color_Active == field_index &&
        ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {
        char before[4];
        int cursor = Global_Dashboard_Case_Color_Cursor[field_index];
        int cursor_w = 0;

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > (int)strlen(text)) {

            cursor = (int)strlen(text);

        }

        memcpy(before, text, (size_t)cursor);
        before[cursor] = '\0';

        if (TTF_SizeText(font, before, &cursor_w, &text_h) != 0) {

            cursor_w = cursor * 8;

        }

        SDL_SetRenderDrawColor(renderer, Dashboard_Border_Hi.r, Dashboard_Border_Hi.g,
                               Dashboard_Border_Hi.b, Dashboard_Border_Hi.a);
        SDL_RenderDrawLine(renderer, rect.x + (rect.w - text_w) / 2 + cursor_w,
                          rect.y + 5, rect.x + (rect.w - text_w) / 2 + cursor_w,
                          rect.y + rect.h - 5);
    }
}

void dashboard_draw_case_sidebar(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font,
                                 SDL_Rect sidebar) {
    /*
        Purpose: Draws the selected case details, image, and image upload controls
        Returns: No value
    */

    if (!dashboard || dashboard->selected_case < 0 || dashboard->selected_case >= Global_Dashboard_Case_Count) {

        return;

    }

    Type_Dashboard_Case_Info *info = &Global_Dashboard_Cases[dashboard->selected_case];
    int mouse_x = 0;
    int mouse_y = 0;
    int preview_h = sidebar.w * 9 / 16;
    int y;

    if (preview_h > 190) {

        preview_h = 190;

    }

    if (preview_h > sidebar.h - 360) {

        preview_h = sidebar.h - 360;

    }

    if (preview_h < 80) {

        preview_h = 80;

    }

    dashboard->case_desc_editing =
        Global_Dashboard_Case_Image_Path_Active || Global_Dashboard_Case_Color_Active >= 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    if (Global_Dashboard_Case_Color_Active < 0) {

        dashboard_case_color_sync_inputs(info->color);

    }

    draw_filled_rect(renderer, sidebar, (SDL_Color){0, 5, 2, 248});
    draw_outline_rect(renderer, sidebar, info->color);

    {
        const int field_w = 34;
        const int field_h = 30;
        const int field_gap = 5;
        const int apply_w = 44;
        const int group_w = field_w * 3 + field_gap * 3 + apply_w;
        const int group_x = sidebar.x + sidebar.w - group_w - 12;
        const int field_y = sidebar.y + 98;
        char displayed_case[128];

        draw_text(renderer, font, "CASE", sidebar.x + 16, sidebar.y + 18, Dashboard_Muted);
        dashboard_sdr_copy_truncated(font, displayed_case, sizeof(displayed_case),
                                     info->case_number, sidebar.w - 32);
        draw_text(renderer, font, displayed_case, sidebar.x + 16, sidebar.y + 42, Dashboard_Text);

        for (int i = 0; i < DASHBOARD_CASE_COLOR_FIELD_COUNT; i++) {
            char label[2] = {(char)("RGB"[i]), '\0'};

            Global_Dashboard_Case_Color_Rect[i] =
                (SDL_Rect){group_x + i * (field_w + field_gap), field_y, field_w, field_h};
            draw_text(renderer, font, label,
                      Global_Dashboard_Case_Color_Rect[i].x + field_w / 2 - 4,
                      field_y - 20, Dashboard_Muted);
            dashboard_case_color_draw_input(renderer, font,
                                            Global_Dashboard_Case_Color_Rect[i], i);
        }

        Global_Dashboard_Case_Color_Apply_Rect =
            (SDL_Rect){group_x + 3 * (field_w + field_gap), field_y, apply_w, field_h};
        draw_filled_rect(renderer, Global_Dashboard_Case_Color_Apply_Rect,
                         dashboard_point_in_rect(mouse_x, mouse_y,
                                                 Global_Dashboard_Case_Color_Apply_Rect)
                             ? (SDL_Color){0, 45, 18, 255}
                             : (SDL_Color){0, 20, 8, 255});
        draw_outline_rect(renderer, Global_Dashboard_Case_Color_Apply_Rect, Dashboard_Border);
        dashboard_draw_text_centered(renderer, font, "Set",
                                     Global_Dashboard_Case_Color_Apply_Rect, Dashboard_Text);
    }

    char count_line[128];
    snprintf(count_line, sizeof(count_line), "Signals in case: %d", info->point_count);
    draw_text(renderer, font, count_line, sidebar.x + 16, sidebar.y + 72, Dashboard_Muted);

    draw_text(renderer, font, "Case Image", sidebar.x + 16, sidebar.y + 104, Dashboard_Text);
    Global_Dashboard_Case_Image_Preview_Rect =
        (SDL_Rect){sidebar.x + 16, sidebar.y + 130, sidebar.w - 32, preview_h};
    dashboard_case_image_draw_preview(renderer, font, Global_Dashboard_Case_Image_Preview_Rect, info->case_number);

    Global_Dashboard_Case_Image_Path_Rect =
        (SDL_Rect){sidebar.x + 16, Global_Dashboard_Case_Image_Preview_Rect.y + preview_h + 10, sidebar.w - 32, 30};
    dashboard_case_image_draw_path_input(renderer, font, Global_Dashboard_Case_Image_Path_Rect);

    Global_Dashboard_Case_Image_Upload_Rect =
        (SDL_Rect){sidebar.x + 16, Global_Dashboard_Case_Image_Path_Rect.y + 38, sidebar.w - 32, 30};
    draw_filled_rect(renderer, Global_Dashboard_Case_Image_Upload_Rect,
                     dashboard_point_in_rect(mouse_x, mouse_y, Global_Dashboard_Case_Image_Upload_Rect)
                         ? (SDL_Color){0, 45, 18, 255}
                         : (SDL_Color){0, 20, 8, 255});
    draw_outline_rect(renderer, Global_Dashboard_Case_Image_Upload_Rect, Dashboard_Border);
    dashboard_draw_text_centered(renderer, font, "Upload / Replace Image", Global_Dashboard_Case_Image_Upload_Rect,
                                 Dashboard_Text);

    y = Global_Dashboard_Case_Image_Upload_Rect.y + 46;
    draw_text(renderer, font, "Description", sidebar.x + 16, y, Dashboard_Text);
    y += 26;

    {
        int line_height = TTF_FontHeight(font) + 4;
        int remaining = sidebar.y + sidebar.h - y - 86;
        int desired_height = 110 + line_height * 2;
        int description_h = remaining > desired_height ? desired_height : remaining;

        if (description_h < 54) {

            description_h = 54;

        }

        dashboard->case_desc_rect = (SDL_Rect){sidebar.x + 16, y, sidebar.w - 32, description_h};
    }

    draw_filled_rect(renderer, dashboard->case_desc_rect, (SDL_Color){0, 9, 4, 255});
    draw_outline_rect(renderer, dashboard->case_desc_rect, Dashboard_Border);

    if (Global_Dashboard_Case_Description_Selected_Case != dashboard->selected_case) {

        Global_Dashboard_Case_Description_Selected_Case = dashboard->selected_case;
        Global_Dashboard_Case_Description_Scroll = 0;
        Global_Dashboard_Case_Description_Max_Scroll = 0;

    }

    SDL_Rect desc_text_rect = {dashboard->case_desc_rect.x + 9, dashboard->case_desc_rect.y + 9,
                               dashboard->case_desc_rect.w - 28, dashboard->case_desc_rect.h - 18};
    const char *shown = info->description[0] ? info->description : "No case description.";
    int description_line_height = TTF_FontHeight(font) + 4;
    int visible_description_lines;
    int description_line_count;

    if (description_line_height < 1) {

        description_line_height = 1;

    }

    visible_description_lines = desc_text_rect.h / description_line_height;

    if (visible_description_lines < 1) {

        visible_description_lines = 1;

    }

    description_line_count = dashboard_wrap_description_text(NULL, font, shown,
                                                             desc_text_rect, Dashboard_Muted, 0);
    Global_Dashboard_Case_Description_Max_Scroll =
        description_line_count > visible_description_lines
            ? description_line_count - visible_description_lines
            : 0;

    if (Global_Dashboard_Case_Description_Scroll > Global_Dashboard_Case_Description_Max_Scroll) {

        Global_Dashboard_Case_Description_Scroll = Global_Dashboard_Case_Description_Max_Scroll;

    }

    dashboard_wrap_description_text(renderer, font, shown, desc_text_rect, Dashboard_Muted,
                                    Global_Dashboard_Case_Description_Scroll);

    if (Global_Dashboard_Case_Description_Max_Scroll > 0) {
        SDL_Rect scroll_track = {dashboard->case_desc_rect.x + dashboard->case_desc_rect.w - 8,
                                 dashboard->case_desc_rect.y + 8, 3,
                                 dashboard->case_desc_rect.h - 16};
        int thumb_height = scroll_track.h * visible_description_lines / description_line_count;
        int thumb_travel;
        int thumb_y;

        if (thumb_height < 12) {

            thumb_height = 12;

        }

        if (thumb_height > scroll_track.h) {

            thumb_height = scroll_track.h;

        }

        thumb_travel = scroll_track.h - thumb_height;
        thumb_y = scroll_track.y;

        if (Global_Dashboard_Case_Description_Max_Scroll > 0 && thumb_travel > 0) {

            thumb_y += thumb_travel * Global_Dashboard_Case_Description_Scroll /
                       Global_Dashboard_Case_Description_Max_Scroll;

        }

        draw_filled_rect(renderer, scroll_track, (SDL_Color){0, 35, 14, 255});
        draw_filled_rect(renderer,
                         (SDL_Rect){scroll_track.x, thumb_y, scroll_track.w, thumb_height},
                         Dashboard_Border_Hi);
    }

    y = dashboard->case_desc_rect.y + dashboard->case_desc_rect.h + 12;

    if (y + 24 < sidebar.y + sidebar.h) {

        draw_text(renderer, font, "Signals", sidebar.x + 16, y, Dashboard_Text);
        y += 26;

    }

    Global_Dashboard_Signal_Row_Count = 0;

    for (int i = 0; i < DASHBOARD_VISIBLE_SIGNAL_ROWS; i++) {

        Global_Dashboard_Signal_Row_Point_Index[i] = -1;
        Global_Dashboard_Signal_Row_Rect[i] = (SDL_Rect){0, 0, 0, 0};

    }

    int shown_count = 0;
    for (int i = 0; i < Global_Dashboard_Case_Point_Count && y + 22 < sidebar.y + sidebar.h; i++) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];

        if (pt->case_index != dashboard->selected_case) {

            continue;

        }

        SDL_Rect signal_row = {sidebar.x + 12, y - 3, sidebar.w - 24, 24};
        int hovered = dashboard_point_in_rect(mouse_x, mouse_y, signal_row);
        int selected = Global_Dashboard_Selected_Signal == i;
        char line[256];

        if (hovered || selected) {

            draw_filled_rect(renderer, signal_row,
                             selected ? (SDL_Color){0, 40, 16, 255}
                                      : (SDL_Color){0, 25, 10, 255});
            draw_outline_rect(renderer, signal_row,
                              selected ? Dashboard_Border_Hi : Dashboard_Border);

        }

        if (pt->has_valid_location) {

            snprintf(line, sizeof(line), "%s  %.4f, %.4f",
                     pt->signal_name[0] ? pt->signal_name : "Unnamed signal",
                     pt->latitude, pt->longitude);

        }

        else {

            snprintf(line, sizeof(line), "%s  Unknown Location",
                     pt->signal_name[0] ? pt->signal_name : "Unnamed signal");

        }
        draw_text(renderer, font, line, sidebar.x + 16, y,
                  hovered || selected ? Dashboard_Text : Dashboard_Muted);

        Global_Dashboard_Signal_Row_Rect[shown_count] = signal_row;
        Global_Dashboard_Signal_Row_Point_Index[shown_count] = i;
        Global_Dashboard_Signal_Row_Count = shown_count + 1;

        y += 24;
        shown_count++;

        if (shown_count >= DASHBOARD_VISIBLE_SIGNAL_ROWS) {

            break;

        }
    }
}


static int dashboard_classification_line_is_header(const unsigned char *line, size_t line_size) {
    /*
        Purpose: Checks whether a classification CSV line is the header
        Returns: Boolean status
    */

    char text[512];
    size_t copy_size;

    if (!line || line_size == 0) {

        return 0;

    }

    copy_size = line_size;

    if (copy_size >= sizeof(text)) {

        copy_size = sizeof(text) - 1;

    }

    memcpy(text, line, copy_size);
    text[copy_size] = '\0';

    return strstr(text, "case_number") != NULL && strstr(text, "latitude") != NULL;
}

static int dashboard_delete_selected_signal(Type_Dashboard_State *dashboard) {
    /*
        Purpose: Deletes one classification row and unlinks it from its case
        Returns: Success status
    */

    Type_Dashboard_Case_Point selected;
    unsigned char *content = NULL;
    unsigned char *updated = NULL;
    size_t content_size = 0;
    size_t updated_size = 0;
    size_t offset = 0;
    size_t data_row_index = 0;
    size_t remaining_rows = 0;
    int first = 1;
    int found = 0;
    int removed = 0;
    int deleted = 0;
    char case_number[128] = "";
    char database_error[256] = "";

    if (!dashboard || Global_Dashboard_Selected_Signal < 0 ||
        Global_Dashboard_Selected_Signal >= Global_Dashboard_Case_Point_Count) {

        return 0;

    }

    selected = Global_Dashboard_Case_Points[Global_Dashboard_Selected_Signal];

    if (!selected.document_name[0] || selected.case_index < 0 ||
        selected.case_index >= Global_Dashboard_Case_Count) {

        snprintf(dashboard->status, sizeof(dashboard->status),
                 "Unable to identify the selected classification record.");
        return 0;

    }

    dashboard_copy_text(case_number, sizeof(case_number),
                        Global_Dashboard_Cases[selected.case_index].case_number);

    if (!DATASTORE_load_content(DATASTORE_KIND_CLASSIFICATION, selected.document_name,
                                &content, &content_size, &found, database_error,
                                sizeof(database_error))) {

        snprintf(dashboard->status, sizeof(dashboard->status),
                 "Unable to load signal classification: %.170s", database_error);
        return 0;

    }

    if (!found || !content || content_size == 0) {

        DATASTORE_free_content(content, content_size);
        snprintf(dashboard->status, sizeof(dashboard->status),
                 "The selected classification no longer exists.");
        Global_Dashboard_Selected_Signal = -1;
        dashboard_reload_cases(dashboard);
        return 0;

    }

    updated = (unsigned char *)malloc(content_size + 1U);

    if (!updated) {

        DATASTORE_free_content(content, content_size);
        snprintf(dashboard->status, sizeof(dashboard->status),
                 "Unable to allocate memory for classification deletion.");
        return 0;

    }

    while (offset < content_size) {
        size_t line_start = offset;
        size_t line_end;
        size_t span_end;
        size_t trimmed_size;
        int is_header = 0;
        int skip_line = 0;

        while (offset < content_size && content[offset] != '\n') {
            offset++;
        }

        line_end = offset;

        if (offset < content_size && content[offset] == '\n') {

            offset++;

        }

        span_end = offset;
        trimmed_size = line_end - line_start;

        if (trimmed_size > 0 && content[line_start + trimmed_size - 1] == '\r') {

            trimmed_size--;

        }

        if (first) {

            first = 0;
            is_header = dashboard_classification_line_is_header(content + line_start,
                                                                 trimmed_size);

        }

        if (!is_header && trimmed_size > 0) {

            if (data_row_index == selected.document_row_index) {

                skip_line = 1;
                removed = 1;

            }

            else {

                remaining_rows++;

            }

            data_row_index++;

        }

        if (!skip_line) {
            size_t span_size = span_end - line_start;

            memcpy(updated + updated_size, content + line_start, span_size);
            updated_size += span_size;

        }
    }

    DATASTORE_free_content(content, content_size);
    content = NULL;

    if (!removed) {

        free(updated);
        snprintf(dashboard->status, sizeof(dashboard->status),
                 "The selected classification row could not be found.");
        Global_Dashboard_Selected_Signal = -1;
        dashboard_reload_cases(dashboard);
        return 0;

    }

    if (remaining_rows == 0) {

        if (!DATASTORE_delete_content(DATASTORE_KIND_CLASSIFICATION,
                                      selected.document_name, &deleted,
                                      database_error, sizeof(database_error)) || !deleted) {

            free(updated);
            snprintf(dashboard->status, sizeof(dashboard->status),
                     "Unable to delete signal classification: %.165s",
                     database_error[0] ? database_error : "record was not deleted");
            return 0;

        }

    }

    else if (!DATASTORE_save_content(DATASTORE_KIND_CLASSIFICATION,
                                     selected.document_name, case_number,
                                     updated, updated_size, database_error,
                                     sizeof(database_error))) {

        free(updated);
        snprintf(dashboard->status, sizeof(dashboard->status),
                 "Unable to update signal classification: %.170s", database_error);
        return 0;

    }

    free(updated);
    Global_Dashboard_Selected_Signal = -1;
    Global_Dashboard_Signal_Popup_Rect = (SDL_Rect){0, 0, 0, 0};
    Global_Dashboard_Signal_Popup_Close_Rect = (SDL_Rect){0, 0, 0, 0};
    Global_Dashboard_Signal_Delete_Rect = (SDL_Rect){0, 0, 0, 0};
    dashboard_reload_cases(dashboard);
    dashboard->last_case_scan_ms = SDL_GetTicks64();
    snprintf(dashboard->status, sizeof(dashboard->status),
             "Signal classification deleted.");
    return 1;
}

static void dashboard_draw_signal_field(SDL_Renderer *renderer, TTF_Font *font,
                                        const char *label, const char *value,
                                        SDL_Rect rect) {
    /*
        Purpose: Draws one read-only signal classification field
        Returns: No value
    */

    SDL_Rect value_rect = {rect.x, rect.y + 18, rect.w, rect.h - 18};
    SDL_Rect text_rect = {value_rect.x + 7, value_rect.y + 4,
                          value_rect.w - 14, value_rect.h - 8};
    const char *shown = value && value[0] ? value : "(not set)";

    draw_text(renderer, font, label, rect.x, rect.y, Dashboard_Muted);
    draw_filled_rect(renderer, value_rect, (SDL_Color){0, 10, 4, 255});
    draw_outline_rect(renderer, value_rect, Dashboard_Border);

    if (rect.h <= 50) {
        char fitted[512];
        int text_y;

        dashboard_sdr_copy_truncated(font, fitted, sizeof(fitted), shown,
                                     text_rect.w);
        text_y = value_rect.y + (value_rect.h - TTF_FontHeight(font)) / 2;

        if (text_y < value_rect.y + 2) {

            text_y = value_rect.y + 2;

        }

        draw_text(renderer, font, fitted, text_rect.x, text_y, Dashboard_Text);
        return;
    }

    {
        const char *cursor = shown;
        int y = text_rect.y;
        int line_h = TTF_FontHeight(font) + 4;

        while (*cursor && y + TTF_FontHeight(font) <= text_rect.y + text_rect.h) {
            char line[512];
            size_t line_length = 0;
            size_t last_break = 0;
            size_t consume_length;

            line[0] = '\0';

            while (cursor[line_length] && cursor[line_length] != '\n' &&
                   line_length + 1 < sizeof(line)) {
                int text_w = 0;
                int text_h = 0;

                line[line_length] = cursor[line_length];
                line[line_length + 1] = '\0';

                if (cursor[line_length] == ' ' || cursor[line_length] == '\t' ||
                    cursor[line_length] == '/' || cursor[line_length] == '_' ||
                    cursor[line_length] == '-' || cursor[line_length] == '.') {

                    last_break = line_length + 1;

                }

                if (TTF_SizeText(font, line, &text_w, &text_h) == 0 &&
                    text_w > text_rect.w) {

                    if (last_break > 0) {

                        line_length = last_break;

                    }

                    else if (line_length == 0) {

                        line_length = 1;

                    }

                    break;

                }

                line_length++;
            }

            if (line_length == 0 && *cursor != '\n') {

                line_length = 1;

            }

            consume_length = line_length;

            while (line_length > 0 &&
                   (cursor[line_length - 1] == ' ' || cursor[line_length - 1] == '\t')) {

                line_length--;

            }

            if (line_length >= sizeof(line)) {

                line_length = sizeof(line) - 1;

            }

            memcpy(line, cursor, line_length);
            line[line_length] = '\0';

            if (line[0]) {

                draw_text(renderer, font, line, text_rect.x, y, Dashboard_Text);

            }

            cursor += consume_length;

            if (*cursor == '\n') {

                cursor++;

            }

            while (*cursor == ' ' || *cursor == '\t') {

                cursor++;

            }

            y += line_h;
        }
    }
}

static void dashboard_draw_signal_popup(Type_Dashboard_State *dashboard,
                                        SDL_Renderer *renderer, TTF_Font *font,
                                        int win_w, int win_h, int mouse_x,
                                        int mouse_y) {
    /*
        Purpose: Draws the read-only signal classification popup
        Returns: No value
    */

    Type_Dashboard_Case_Point *point;
    Type_Dashboard_Case_Info *case_info;
    SDL_Rect popup;
    SDL_Rect overlay = {0, 0, win_w, win_h};
    int popup_w = win_w - 80;
    int popup_h = win_h - 80;
    int margin = 20;
    int gap = 12;
    int column_w;
    int field_h = 48;
    int y;
    char subtitle[512];
    char shown_signal[256];

    if (!dashboard || !renderer || !font || Global_Dashboard_Selected_Signal < 0 ||
        Global_Dashboard_Selected_Signal >= Global_Dashboard_Case_Point_Count) {

        return;

    }

    point = &Global_Dashboard_Case_Points[Global_Dashboard_Selected_Signal];

    if (point->case_index < 0 || point->case_index >= Global_Dashboard_Case_Count) {

        Global_Dashboard_Selected_Signal = -1;
        return;

    }

    case_info = &Global_Dashboard_Cases[point->case_index];

    if (popup_w > 760) {

        popup_w = 760;

    }

    if (popup_w < 420) {

        popup_w = win_w - 20;

    }

    if (popup_h > 600) {

        popup_h = 600;

    }

    if (popup_h < 520) {

        popup_h = win_h - 20;

    }

    popup = (SDL_Rect){(win_w - popup_w) / 2, (win_h - popup_h) / 2,
                       popup_w, popup_h};
    Global_Dashboard_Signal_Popup_Rect = popup;
    Global_Dashboard_Signal_Popup_Close_Rect =
        (SDL_Rect){popup.x + popup.w - 46, popup.y + 14, 30, 28};
    Global_Dashboard_Signal_Delete_Rect =
        (SDL_Rect){popup.x + popup.w - 176, popup.y + popup.h - 50, 156, 34};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, overlay, (SDL_Color){0, 0, 0, 185});
    draw_filled_rect(renderer, popup, (SDL_Color){0, 7, 3, 252});
    draw_outline_rect(renderer, popup, case_info->color);

    draw_text(renderer, font, "SIGNAL CLASSIFICATION DETAILS",
              popup.x + margin, popup.y + 18, Dashboard_Text);

    dashboard_sdr_copy_truncated(font, shown_signal, sizeof(shown_signal),
                                 point->signal_name[0] ? point->signal_name : "Unnamed signal",
                                 popup.w - 150);
    snprintf(subtitle, sizeof(subtitle), "%s  |  Case: %s",
             shown_signal, case_info->case_number);
    draw_text(renderer, font, subtitle, popup.x + margin, popup.y + 43,
              Dashboard_Muted);

    draw_filled_rect(renderer, Global_Dashboard_Signal_Popup_Close_Rect,
                     dashboard_point_in_rect(mouse_x, mouse_y,
                                             Global_Dashboard_Signal_Popup_Close_Rect)
                         ? (SDL_Color){0, 40, 16, 255}
                         : (SDL_Color){0, 18, 7, 255});
    draw_outline_rect(renderer, Global_Dashboard_Signal_Popup_Close_Rect,
                      Dashboard_Border);
    dashboard_draw_text_centered(renderer, font, "X",
                                 Global_Dashboard_Signal_Popup_Close_Rect,
                                 Dashboard_Text);

    column_w = (popup.w - margin * 2 - gap) / 2;
    y = popup.y + 72;

    dashboard_draw_signal_field(renderer, font, "Frequency (MHz)",
                                point->frequency_mhz,
                                (SDL_Rect){popup.x + margin, y, column_w, field_h});
    dashboard_draw_signal_field(renderer, font, "Bandwidth", point->bandwidth,
                                (SDL_Rect){popup.x + margin + column_w + gap, y,
                                           column_w, field_h});
    y += field_h + 7;

    dashboard_draw_signal_field(renderer, font, "Start Time", point->start_time,
                                (SDL_Rect){popup.x + margin, y, column_w, field_h});
    dashboard_draw_signal_field(renderer, font, "End Time", point->end_time,
                                (SDL_Rect){popup.x + margin + column_w + gap, y,
                                           column_w, field_h});
    y += field_h + 7;

    dashboard_draw_signal_field(renderer, font, "Calculated Modulation",
                                point->calculated_modulation,
                                (SDL_Rect){popup.x + margin, y, column_w, field_h});
    dashboard_draw_signal_field(renderer, font, "Signal Class", point->signal_class,
                                (SDL_Rect){popup.x + margin + column_w + gap, y,
                                           column_w, field_h});
    y += field_h + 7;

    dashboard_draw_signal_field(renderer, font, "Country", point->country,
                                (SDL_Rect){popup.x + margin, y, column_w, field_h});
    dashboard_draw_signal_field(renderer, font, "Latitude", point->latitude_text,
                                (SDL_Rect){popup.x + margin + column_w + gap, y,
                                           column_w, field_h});
    y += field_h + 7;

    dashboard_draw_signal_field(renderer, font, "Longitude", point->longitude_text,
                                (SDL_Rect){popup.x + margin, y,
                                           popup.w - margin * 2, field_h});
    y += field_h + 7;

    {
        const int filename_h = 84;
        int notes_h = Global_Dashboard_Signal_Delete_Rect.y - y - filename_h - 19;

        if (notes_h < 58) {

            notes_h = 58;

        }

        dashboard_draw_signal_field(renderer, font, "Notes", point->notes,
                                    (SDL_Rect){popup.x + margin, y,
                                               popup.w - margin * 2, notes_h});
        y += notes_h + 7;
        dashboard_draw_signal_field(renderer, font, "File Name", point->file_name,
                                    (SDL_Rect){popup.x + margin, y,
                                               popup.w - margin * 2, filename_h});
    }

    draw_filled_rect(renderer, Global_Dashboard_Signal_Delete_Rect,
                     dashboard_point_in_rect(mouse_x, mouse_y,
                                             Global_Dashboard_Signal_Delete_Rect)
                         ? (SDL_Color){90, 12, 12, 255}
                         : (SDL_Color){48, 7, 7, 255});
    draw_outline_rect(renderer, Global_Dashboard_Signal_Delete_Rect,
                      (SDL_Color){255, 80, 80, 255});
    dashboard_draw_text_centered(renderer, font, "Delete Signal",
                                 Global_Dashboard_Signal_Delete_Rect,
                                 (SDL_Color){255, 200, 200, 255});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static int dashboard_handle_signal_popup_event(Type_Dashboard_State *dashboard,
                                               const SDL_Event *event) {
    /*
        Purpose: Handles the signal detail popup and destructive delete button
        Returns: Handling status
    */

    if (!dashboard || !event || Global_Dashboard_Selected_Signal < 0) {

        return 0;

    }

    if (Global_Dashboard_Selected_Signal >= Global_Dashboard_Case_Point_Count) {

        Global_Dashboard_Selected_Signal = -1;
        return 0;

    }

    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) {

        Global_Dashboard_Selected_Signal = -1;
        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN &&
        event->button.button == SDL_BUTTON_LEFT) {

        if (dashboard_point_in_rect(event->button.x, event->button.y,
                                    Global_Dashboard_Signal_Delete_Rect)) {

            dashboard_delete_selected_signal(dashboard);
            return 1;

        }

        if (dashboard_point_in_rect(event->button.x, event->button.y,
                                    Global_Dashboard_Signal_Popup_Close_Rect)) {

            Global_Dashboard_Selected_Signal = -1;
            return 1;

        }

        if (!dashboard_point_in_rect(event->button.x, event->button.y,
                                     Global_Dashboard_Signal_Popup_Rect)) {

            Global_Dashboard_Selected_Signal = -1;
            return 1;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONUP || event->type == SDL_MOUSEMOTION ||
        event->type == SDL_MOUSEWHEEL || event->type == SDL_TEXTINPUT) {

        return 1;

    }

    return 0;
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
        Purpose: Handles case-image path entry and upload controls
        Returns: Handling status
    */

    if (!dashboard || !event || dashboard->selected_case < 0 ||
        dashboard->selected_case >= Global_Dashboard_Case_Count) {

        Global_Dashboard_Case_Image_Path_Active = 0;
        Global_Dashboard_Case_Color_Active = -1;
        return 0;

    }

    if (event->type == SDL_MOUSEWHEEL) {
        int mouse_x = 0;
        int mouse_y = 0;

        SDL_GetMouseState(&mouse_x, &mouse_y);

        if (dashboard->current_tab == DASHBOARD_EVENT_MAP &&
            dashboard_point_in_rect(mouse_x, mouse_y, dashboard->case_desc_rect)) {

            Global_Dashboard_Case_Description_Scroll -= event->wheel.y;

            if (Global_Dashboard_Case_Description_Scroll < 0) {

                Global_Dashboard_Case_Description_Scroll = 0;

            }

            if (Global_Dashboard_Case_Description_Scroll >
                Global_Dashboard_Case_Description_Max_Scroll) {

                Global_Dashboard_Case_Description_Scroll =
                    Global_Dashboard_Case_Description_Max_Scroll;

            }

            return 1;

        }
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        for (int i = 0; i < Global_Dashboard_Signal_Row_Count; i++) {

            if (Global_Dashboard_Signal_Row_Point_Index[i] >= 0 &&
                dashboard_point_in_rect(event->button.x, event->button.y,
                                        Global_Dashboard_Signal_Row_Rect[i])) {

                Global_Dashboard_Selected_Signal =
                    Global_Dashboard_Signal_Row_Point_Index[i];
                Global_Dashboard_Case_Image_Path_Active = 0;
                Global_Dashboard_Case_Color_Active = -1;
                dashboard->case_desc_editing = 0;
                return 1;

            }
        }

        for (int i = 0; i < DASHBOARD_CASE_COLOR_FIELD_COUNT; i++) {

            if (dashboard_point_in_rect(event->button.x, event->button.y,
                                        Global_Dashboard_Case_Color_Rect[i])) {

                Global_Dashboard_Case_Color_Active = i;
                Global_Dashboard_Case_Color_Cursor[i] =
                    (int)strlen(Global_Dashboard_Case_Color_Text[i]);
                Global_Dashboard_Case_Image_Path_Active = 0;
                dashboard->case_desc_editing = 1;
                SDL_StartTextInput();
                return 1;

            }
        }

        if (dashboard_point_in_rect(event->button.x, event->button.y,
                                    Global_Dashboard_Case_Color_Apply_Rect)) {

            dashboard_case_color_save(dashboard);
            return 1;

        }

        if (dashboard_point_in_rect(event->button.x, event->button.y, Global_Dashboard_Case_Image_Path_Rect)) {

            Global_Dashboard_Case_Image_Path_Active = 1;
            Global_Dashboard_Case_Image_Path_Cursor = (int)strlen(Global_Dashboard_Case_Image_Path);
            Global_Dashboard_Case_Color_Active = -1;
            dashboard->case_desc_editing = 1;
            SDL_StartTextInput();
            return 1;

        }

        if (dashboard_point_in_rect(event->button.x, event->button.y, Global_Dashboard_Case_Image_Upload_Rect)) {

            dashboard_case_image_upload(dashboard);
            return 1;

        }

        Global_Dashboard_Case_Image_Path_Active = 0;
        Global_Dashboard_Case_Color_Active = -1;
        dashboard->case_desc_editing = 0;
        return 0;

    }

    if (Global_Dashboard_Case_Color_Active >= 0) {
        int field_index = Global_Dashboard_Case_Color_Active;
        char *field = Global_Dashboard_Case_Color_Text[field_index];
        int *cursor = &Global_Dashboard_Case_Color_Cursor[field_index];
        int length = (int)strlen(field);

        dashboard->case_desc_editing = 1;

        if (event->type == SDL_TEXTINPUT) {

            dashboard_case_color_insert_text(event->text.text);
            return 1;

        }

        if (event->type == SDL_KEYDOWN) {
            SDL_Keycode key = event->key.keysym.sym;

            if (key == SDLK_BACKSPACE) {

                if (*cursor > 0 && length > 0) {

                    memmove(field + *cursor - 1, field + *cursor,
                            (size_t)(length - *cursor) + 1U);
                    (*cursor)--;

                }
                return 1;

            }

            if (key == SDLK_DELETE) {

                if (*cursor >= 0 && *cursor < length) {

                    memmove(field + *cursor, field + *cursor + 1,
                            (size_t)(length - *cursor));

                }
                return 1;

            }

            if (key == SDLK_LEFT) {

                if (*cursor > 0) {

                    (*cursor)--;

                }
                return 1;

            }

            if (key == SDLK_RIGHT) {

                if (*cursor < length) {

                    (*cursor)++;

                }
                return 1;

            }

            if (key == SDLK_HOME) {

                *cursor = 0;
                return 1;

            }

            if (key == SDLK_END) {

                *cursor = length;
                return 1;

            }

            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

                dashboard_case_color_save(dashboard);
                return 1;

            }

            if (key == SDLK_ESCAPE) {

                Global_Dashboard_Case_Color_Active = -1;
                dashboard_case_color_sync_inputs(
                    Global_Dashboard_Cases[dashboard->selected_case].color);
                dashboard->case_desc_editing = Global_Dashboard_Case_Image_Path_Active;
                return 1;

            }

            return 1;
        }

        return 0;
    }

    if (!Global_Dashboard_Case_Image_Path_Active) {

        return 0;

    }

    dashboard->case_desc_editing = 1;

    if (event->type == SDL_TEXTINPUT) {

        dashboard_case_image_insert_text(event->text.text);
        return 1;

    }

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        SDL_Keymod modifiers = SDL_GetModState();
        int length = (int)strlen(Global_Dashboard_Case_Image_Path);
        int cursor = Global_Dashboard_Case_Image_Path_Cursor;

        if ((modifiers & KMOD_CTRL) && key == SDLK_v) {

            dashboard_case_image_paste_path();
            return 1;

        }

        if (key == SDLK_BACKSPACE) {

            if (cursor > 0 && length > 0) {

                memmove(Global_Dashboard_Case_Image_Path + cursor - 1,
                        Global_Dashboard_Case_Image_Path + cursor, (size_t)(length - cursor) + 1U);
                Global_Dashboard_Case_Image_Path_Cursor--;

            }
            return 1;

        }

        if (key == SDLK_DELETE) {

            if (cursor >= 0 && cursor < length) {

                memmove(Global_Dashboard_Case_Image_Path + cursor,
                        Global_Dashboard_Case_Image_Path + cursor + 1, (size_t)(length - cursor));

            }
            return 1;

        }

        if (key == SDLK_LEFT) {

            if (Global_Dashboard_Case_Image_Path_Cursor > 0) {

                Global_Dashboard_Case_Image_Path_Cursor--;

            }
            return 1;

        }

        if (key == SDLK_RIGHT) {

            if (Global_Dashboard_Case_Image_Path_Cursor < length) {

                Global_Dashboard_Case_Image_Path_Cursor++;

            }
            return 1;

        }

        if (key == SDLK_HOME) {

            Global_Dashboard_Case_Image_Path_Cursor = 0;
            return 1;

        }

        if (key == SDLK_END) {

            Global_Dashboard_Case_Image_Path_Cursor = length;
            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            dashboard_case_image_upload(dashboard);
            return 1;

        }

        if (key == SDLK_ESCAPE) {

            Global_Dashboard_Case_Image_Path_Active = 0;
            dashboard->case_desc_editing = 0;
            return 1;

        }

        return 1;

    }

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
    Global_Dashboard_Case_Image_Path[0] = '\0';
    Global_Dashboard_Case_Image_Path_Cursor = 0;
    Global_Dashboard_Case_Image_Path_Active = 0;
    Global_Dashboard_Case_Color_Active = -1;
    Global_Dashboard_Selected_Signal = -1;
    Global_Dashboard_Signal_Row_Count = 0;
    Global_Dashboard_Case_Description_Scroll = 0;
    Global_Dashboard_Case_Description_Max_Scroll = 0;
    Global_Dashboard_Case_Description_Selected_Case = -1;
    dashboard_case_image_clear_cache();

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
    Global_Dashboard_Case_Image_Path_Active = 0;
    Global_Dashboard_Case_Color_Active = -1;
    Global_Dashboard_Selected_Signal = -1;
    Global_Dashboard_Signal_Row_Count = 0;
    dashboard_case_image_clear_cache();
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

    if (dashboard_handle_signal_popup_event(dashboard, event)) {

        return DASHBOARD_EVENT_NONE;

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
            Global_Dashboard_Case_Description_Scroll = 0;
            Global_Dashboard_Case_Description_Max_Scroll = 0;
            Global_Dashboard_Case_Description_Selected_Case = -1;
            Global_Dashboard_Case_Image_Path_Active = 0;
            Global_Dashboard_Case_Image_Path[0] = '\0';
            Global_Dashboard_Case_Image_Path_Cursor = 0;
            Global_Dashboard_Case_Color_Active = -1;
            Global_Dashboard_Selected_Signal = -1;
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
    dashboard_draw_signal_popup(dashboard, renderer, font_small, win_w, win_h, mouse_x, mouse_y);
}
