/*
 * ============================================================================
 * File:            RetroSpectrum.c
 * Author:          Hassan Fares
 *
 * Confidential:    No
 *
 * Description:     Main logic for the RetroSpectrum application
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 *                                                                   05/04/2026
 * ============================================================================
 */

// =========
// Libraries
// =========

// Standard Libraries
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// HackRF Library
#include <libhackrf/hackrf.h>

// FFT Library
#include <fftw3.h>

// SDL (GUI) Library
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

// ============
// Header Files
// ============

// Responsible for GUI objects
#include "GUIs.h"

// Responsible for IQ objects
#include "IQs.h"

// Responsible for the ClassificationWorkstation
#include "ClassificationWorkstation.h"

// Responsible for the AnalysisWorkstation
#include "AnalysisWorkstation.h"

// Embedded binary world map loader used directly by RetroSpectrum's dashboard.
#define WORLD_MAP_NO_DEMO
#include "world_map_bin_loader.c"

// GUI functions implemented in GUIs.c and called from this main logic file

void add_fft_line_to_waterfall(uint32_t *pixels, int tex_w, int tex_h, double *db);

int ANALYSIS_export_classification_fields(char *file_name,
                                          size_t file_name_size,
                                          double *frequency_mhz,
                                          double *bandwidth_khz,
                                          double *start_time,
                                          double *end_time);

void CLASSIFICATION_prefill_from_analysis_selection(const char *file_name,
                                                    double frequency_mhz,
                                                    double bandwidth_khz,
                                                    double start_time,
                                                    double end_time);

int CLASSIFICATION_is_text_entry_active(void);
int ANALYSIS_is_text_entry_active(void);


// ==============================
// Embedded Dashboard / Map Shell
// ==============================

typedef enum Type_Dashboard_Event {
    DASHBOARD_EVENT_NONE = 0,
    DASHBOARD_EVENT_QUIT,
    DASHBOARD_EVENT_RETROSPECTRUM,
    DASHBOARD_EVENT_ANALYSIS,
    DASHBOARD_EVENT_CLASSIFICATION,
    DASHBOARD_EVENT_MAP
} Type_Dashboard_Event;

typedef struct Type_Dashboard_State {
    int enabled;
    int map_loaded;
    int current_tab;
    int selected_case;
    int case_desc_editing;
    int country_case_scroll;
    int hover_country;
    int hover_mouse_x;
    int hover_mouse_y;
    int locked_country;
    int locked_mouse_x;
    int locked_mouse_y;
    int case_search_active;
    int case_search_cursor;
    Uint64 last_case_scan_ms;
    SDL_Rect case_desc_rect;
    SDL_Rect case_search_rect;
    char case_desc_edit[512];
    char case_search_text[128];
    char status[256];
} Type_Dashboard_State;

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

#define DASHBOARD_MAX_CASES             256
#define DASHBOARD_MAX_CASE_POINTS       4096
#define DASHBOARD_CASE_DIR              "Classification"
#define DASHBOARD_CASE_DESCRIPTION_CSV  "Classification/CASE_DESCRIPTIONS.csv"

static Type_Dashboard_Case_Info Global_Dashboard_Cases[DASHBOARD_MAX_CASES];
static Type_Dashboard_Case_Point Global_Dashboard_Case_Points[DASHBOARD_MAX_CASE_POINTS];
static int Global_Dashboard_Case_Count = 0;
static int Global_Dashboard_Case_Point_Count = 0;


#define DASHBOARD_MARGIN              20
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#define DASHBOARD_TOP_H               RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define DASHBOARD_TAB_H               42
#define DASHBOARD_TAB_GAP             10
#define DASHBOARD_CARD_H              0
#define DASHBOARD_MIN_MAP_H           280

static SDL_Color Dashboard_BG         = {0,   0,   0,   255};
static SDL_Color Dashboard_Panel      = {0,   12,  5,   255};
static SDL_Color Dashboard_Panel_2    = {0,   20,  8,   255};
static SDL_Color Dashboard_Grid       = {0,   50,  20,  120};
static SDL_Color Dashboard_Border     = {0,   150, 60,  255};
static SDL_Color Dashboard_Border_Hi  = {0,   255, 90,  255};
static SDL_Color Dashboard_Text       = {0,   255, 90,  255};
static SDL_Color Dashboard_Muted      = {0,   155, 65,  255};
static SDL_Color Dashboard_Warn       = {255, 180, 40,  255};
static SDL_Color Dashboard_Red        = {255, 70,  55,  255};
static SDL_Color Dashboard_Blue       = {0,   190, 255, 255};

static int dashboard_point_in_rect(int x, int y, SDL_Rect r){
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void dashboard_draw_text_centered(SDL_Renderer *renderer,
                                         TTF_Font *font,
                                         const char *text,
                                         SDL_Rect rect,
                                         SDL_Color color){
    int text_w = 0;
    int text_h = 0;

    if (!font || !text) return;

    if (TTF_SizeText(font, text, &text_w, &text_h) != 0) {
        text_w = 0;
        text_h = 0;
    }

    draw_text(renderer,
              font,
              text,
              rect.x + (rect.w - text_w) / 2,
              rect.y + (rect.h - text_h) / 2,
              color);
}

static void dashboard_draw_grid(SDL_Renderer *renderer, int win_w, int win_h){
    SDL_SetRenderDrawColor(renderer,
                           Dashboard_Grid.r,
                           Dashboard_Grid.g,
                           Dashboard_Grid.b,
                           Dashboard_Grid.a);

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

static SDL_Rect dashboard_top_rect(int win_w){
    SDL_Rect rect = {0, 0, win_w, DASHBOARD_TOP_H};
    return rect;
}

static SDL_Rect dashboard_content_rect(int win_w, int win_h){
    SDL_Rect rect = {
        DASHBOARD_MARGIN,
        DASHBOARD_TOP_H + DASHBOARD_MARGIN,
        win_w - 2 * DASHBOARD_MARGIN,
        win_h - DASHBOARD_TOP_H - 2 * DASHBOARD_MARGIN - 40
    };

    if (rect.h < DASHBOARD_MIN_MAP_H) rect.h = DASHBOARD_MIN_MAP_H;
    return rect;
}

static void dashboard_make_tabs(int win_w, Type_Dashboard_Tab tabs[4]){
    int total_gap = DASHBOARD_TAB_GAP * 3;
    int tab_w = (win_w - 2 * DASHBOARD_MARGIN - total_gap) / 4;
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
}

static void dashboard_draw_tab(SDL_Renderer *renderer,
                               TTF_Font *font,
                               Type_Dashboard_Tab tab,
                               int active,
                               int hovered){
    SDL_Color fill = active ? Dashboard_Panel_2 : Dashboard_Panel;
    SDL_Color border = (active || hovered) ? Dashboard_Border_Hi : Dashboard_Border;
    SDL_Color text = (active || hovered) ? Dashboard_Text : Dashboard_Muted;

    draw_filled_rect(renderer, tab.rect, fill);
    draw_outline_rect(renderer, tab.rect, border);

    if (active || hovered) {
        SDL_Rect inner = {tab.rect.x + 3,
                          tab.rect.y + 3,
                          tab.rect.w - 6,
                          tab.rect.h - 6};
        draw_outline_rect(renderer, inner, border);
    }

    dashboard_draw_text_centered(renderer, font, tab.label, tab.rect, text);
}

static void dashboard_draw_top_bar(SDL_Renderer *renderer,
                                   TTF_Font *font_small,
                                   TTF_Font *font_medium,
                                   int win_w,
                                   int mouse_x,
                                   int mouse_y,
                                   int active_tab){
    (void)font_medium;

    SDL_Rect top = dashboard_top_rect(win_w);

    draw_filled_rect(renderer, top, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, top, Dashboard_Border);

    Type_Dashboard_Tab tabs[4];
    dashboard_make_tabs(win_w, tabs);

    for (int i = 0; i < 4; i++) {
        dashboard_draw_tab(renderer,
                           font_small,
                           tabs[i],
                           tabs[i].event_id == active_tab,
                           dashboard_point_in_rect(mouse_x, mouse_y, tabs[i].rect));
    }
}

static void dashboard_draw_station_card(SDL_Renderer *renderer,
                                        TTF_Font *font_small,
                                        TTF_Font *font_medium,
                                        SDL_Rect rect,
                                        const char *title,
                                        const char *body,
                                        SDL_Color accent){
    draw_filled_rect(renderer, rect, (SDL_Color){0, 10, 4, 240});
    draw_outline_rect(renderer, rect, accent);

    SDL_Rect stripe = {rect.x, rect.y, 5, rect.h};
    draw_filled_rect(renderer, stripe, accent);

    draw_text(renderer, font_medium, title, rect.x + 16, rect.y + 12, Dashboard_Text);
    draw_text(renderer, font_small, body, rect.x + 16, rect.y + 42, Dashboard_Muted);
}


static unsigned int dashboard_hash_string(const char *text){
    unsigned int h = 2166136261u;
    if (!text) text = "";
    while (*text) {
        h ^= (unsigned char)*text++;
        h *= 16777619u;
    }
    return h;
}

static int dashboard_ascii_equal_ci(const char *a, const char *b){
    if (!a) a = "";
    if (!b) b = "";

    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return 0;
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static int dashboard_ascii_contains_ci(const char *haystack, const char *needle){
    if (!haystack) haystack = "";
    if (!needle || needle[0] == '\0') return 0;

    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < needle_len && p[i]) {
            int ca = tolower((unsigned char)p[i]);
            int cb = tolower((unsigned char)needle[i]);
            if (ca != cb) break;
            i++;
        }
        if (i == needle_len) return 1;
    }

    return 0;
}

static SDL_Color dashboard_case_color(const char *case_number){
    static const SDL_Color palette[] = {
        {  0, 170, 255, 255},   /* bright blue */
        {255, 150,   0, 255},   /* orange */
        {255,  55,  55, 255},   /* red */
        {  0, 230, 120, 255},   /* green */
        {255, 235,  40, 255},   /* yellow */
        {255, 105,  90, 255},   /* coral */
        {150, 255,  45, 255},   /* lime */
        { 80, 140, 255, 255},   /* soft blue */
        {255, 190,  80, 255},   /* amber */
        {255,  80, 180, 255},   /* pink */
        { 80, 255, 220, 255},   /* cyan */
        {210, 120, 255, 255}    /* violet */
    };

    unsigned int h = dashboard_hash_string(case_number);
    SDL_Color c = palette[h % (unsigned int)(sizeof(palette) / sizeof(palette[0]))];

    /* Slight deterministic brightness shift keeps repeated palette entries distinct. */
    int shade = (int)((h >> 12) & 0x1F) - 10;
    int r = (int)c.r + shade;
    int g = (int)c.g + shade;
    int b = (int)c.b + shade;

    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;

    c.r = (Uint8)r;
    c.g = (Uint8)g;
    c.b = (Uint8)b;
    c.a = 255;
    return c;
}

static int dashboard_case_index_for(const char *case_number){
    if (!case_number || !case_number[0]) case_number = "UNCASED";

    for (int i = 0; i < Global_Dashboard_Case_Count; i++) {
        if (strcmp(Global_Dashboard_Cases[i].case_number, case_number) == 0) return i;
    }

    if (Global_Dashboard_Case_Count >= DASHBOARD_MAX_CASES) return -1;

    int idx = Global_Dashboard_Case_Count++;
    snprintf(Global_Dashboard_Cases[idx].case_number,
             sizeof(Global_Dashboard_Cases[idx].case_number),
             "%s",
             case_number);
    Global_Dashboard_Cases[idx].description[0] = '\0';
    Global_Dashboard_Cases[idx].color = dashboard_case_color(case_number);
    Global_Dashboard_Cases[idx].point_count = 0;
    return idx;
}

static int dashboard_csv_parse_line(char *line, char fields[][512], int max_fields){
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
                        if (oi < 511) out[oi++] = '"';
                        p += 2;
                        continue;
                    }
                    p++;
                    break;
                }
                if (oi < 511) out[oi++] = *p;
                p++;
            }
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
        else {
            while (*p && *p != ',' && *p != '\n' && *p != '\r') {
                if (oi < 511) out[oi++] = *p;
                p++;
            }
            if (*p == ',') p++;
        }

        out[oi] = '\0';
        count++;
    }

    return count;
}

static int dashboard_file_has_suffix(const char *name, const char *suffix){
    if (!name || !suffix) return 0;
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

static void dashboard_load_case_descriptions(void){
    FILE *fp = fopen(DASHBOARD_CASE_DESCRIPTION_CSV, "r");
    if (!fp) return;

    char line[2048];
    int first = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (first) {
            first = 0;
            if (strstr(line, "case_number") && strstr(line, "description")) continue;
        }

        char fields[2][512];
        memset(fields, 0, sizeof(fields));
        if (dashboard_csv_parse_line(line, fields, 2) >= 1 && fields[0][0]) {
            int idx = dashboard_case_index_for(fields[0]);
            if (idx >= 0) {
                snprintf(Global_Dashboard_Cases[idx].description,
                         sizeof(Global_Dashboard_Cases[idx].description),
                         "%s",
                         fields[1]);
            }
        }
    }

    fclose(fp);
}

static void dashboard_csv_escape(FILE *fp, const char *text){
    fputc('"', fp);
    if (text) {
        for (const char *p = text; *p; p++) {
            if (*p == '"') fputc('"', fp);
            fputc(*p, fp);
        }
    }
    fputc('"', fp);
}

static void dashboard_save_case_descriptions(void){
    struct stat st;
    if (stat(DASHBOARD_CASE_DIR, &st) != 0) {
        mkdir(DASHBOARD_CASE_DIR, 0755);
    }

    FILE *fp = fopen(DASHBOARD_CASE_DESCRIPTION_CSV, "w");
    if (!fp) return;

    fprintf(fp, "case_number,description\n");
    for (int i = 0; i < Global_Dashboard_Case_Count; i++) {
        dashboard_csv_escape(fp, Global_Dashboard_Cases[i].case_number);
        fputc(',', fp);
        dashboard_csv_escape(fp, Global_Dashboard_Cases[i].description);
        fputc('\n', fp);
    }

    fclose(fp);
}

static void dashboard_load_case_file(const char *path){
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[4096];
    int first = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (first) {
            first = 0;
            if (strstr(line, "case_number") && strstr(line, "latitude")) continue;
        }

        char fields[16][512];
        memset(fields, 0, sizeof(fields));
        int count = dashboard_csv_parse_line(line, fields, 16);
        if (count < 11) continue;

        double lat = strtod(fields[9], NULL);
        double lon = strtod(fields[10], NULL);

        if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) continue;
        if (Global_Dashboard_Case_Point_Count >= DASHBOARD_MAX_CASE_POINTS) break;

        int case_index = dashboard_case_index_for(fields[0]);
        if (case_index < 0) continue;

        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[Global_Dashboard_Case_Point_Count++];
        memset(pt, 0, sizeof(*pt));
        pt->case_index = case_index;
        snprintf(pt->signal_name, sizeof(pt->signal_name), "%s", fields[1]);
        snprintf(pt->country, sizeof(pt->country), "%s", fields[8]);
        snprintf(pt->notes, sizeof(pt->notes), "%s", fields[11]);
        pt->latitude = lat;
        pt->longitude = lon;
        Global_Dashboard_Cases[case_index].point_count++;
    }

    fclose(fp);
}

static void dashboard_reload_cases(Type_Dashboard_State *dashboard){
    (void)dashboard;

    Global_Dashboard_Case_Count = 0;
    Global_Dashboard_Case_Point_Count = 0;

    DIR *dir = opendir(DASHBOARD_CASE_DIR);
    if (dir) {
        struct dirent *entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, "CASE_", 5) != 0) continue;
            if (strcmp(entry->d_name, "CASE_DESCRIPTIONS.csv") == 0) continue;
            if (!dashboard_file_has_suffix(entry->d_name, ".csv")) continue;

            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", DASHBOARD_CASE_DIR, entry->d_name);
            dashboard_load_case_file(path);
        }
        closedir(dir);
    }

    dashboard_load_case_descriptions();

    if (dashboard && dashboard->selected_case >= Global_Dashboard_Case_Count) {
        dashboard->selected_case = -1;
        dashboard->case_desc_editing = 0;
    }
}

static int dashboard_lonlat_to_screen(double lon,
                                      double lat,
                                      SDL_Rect map,
                                      int *sx,
                                      int *sy){
    double min_lon = WM_VIEW.min_lon;
    double max_lon = WM_VIEW.max_lon;
    double min_lat = WM_VIEW.min_lat;
    double max_lat = WM_VIEW.max_lat;

    if (max_lon <= min_lon || max_lat <= min_lat) return 0;
    if (lon < min_lon || lon > max_lon || lat < min_lat || lat > max_lat) return 0;

    double xf = (lon - min_lon) / (max_lon - min_lon);
    double yf = (max_lat - lat) / (max_lat - min_lat);

    if (sx) *sx = map.x + (int)(xf * (double)map.w + 0.5);
    if (sy) *sy = map.y + (int)(yf * (double)map.h + 0.5);
    return 1;
}

static void dashboard_draw_circle_outline(SDL_Renderer *renderer, int cx, int cy, int radius, int thickness){
    if (!renderer || radius <= 0) return;

    int outer2 = radius * radius;
    int inner = radius - thickness;
    if (inner < 0) inner = 0;
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

static void dashboard_draw_case_dot(SDL_Renderer *renderer, int x, int y, SDL_Color color, int enlarged, int search_match){
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

static int dashboard_case_matches_search(Type_Dashboard_State *dashboard, int case_index){
    if (!dashboard || dashboard->case_search_text[0] == '\0') return 0;
    if (case_index < 0 || case_index >= Global_Dashboard_Case_Count) return 0;
    return dashboard_ascii_contains_ci(Global_Dashboard_Cases[case_index].case_number,
                                       dashboard->case_search_text);
}

static void dashboard_draw_case_points(Type_Dashboard_State *dashboard,
                                       SDL_Renderer *renderer,
                                       TTF_Font *font,
                                       SDL_Rect map){
    (void)font;
    if (!dashboard || !renderer) return;

    for (int i = 0; i < Global_Dashboard_Case_Point_Count; i++) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];
        if (pt->case_index < 0 || pt->case_index >= Global_Dashboard_Case_Count) continue;

        int x = 0;
        int y = 0;
        if (!dashboard_lonlat_to_screen(pt->longitude, pt->latitude, map, &x, &y)) continue;

        int selected = (dashboard->selected_case == pt->case_index);
        int search_match = dashboard_case_matches_search(dashboard, pt->case_index);
        dashboard_draw_case_dot(renderer,
                                x,
                                y,
                                Global_Dashboard_Cases[pt->case_index].color,
                                selected || search_match,
                                search_match);
    }
}

static int dashboard_select_case_at(Type_Dashboard_State *dashboard, SDL_Rect map, int x, int y){
    if (!dashboard) return 0;

    for (int i = Global_Dashboard_Case_Point_Count - 1; i >= 0; i--) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];
        int sx = 0;
        int sy = 0;
        if (!dashboard_lonlat_to_screen(pt->longitude, pt->latitude, map, &sx, &sy)) continue;

        int dx = x - sx;
        int dy = y - sy;
        if (dx * dx + dy * dy <= 100) {
            dashboard->selected_case = pt->case_index;
            dashboard->case_desc_editing = 0;
            if (pt->case_index >= 0 && pt->case_index < Global_Dashboard_Case_Count) {
                snprintf(dashboard->case_desc_edit,
                         sizeof(dashboard->case_desc_edit),
                         "%s",
                         Global_Dashboard_Cases[pt->case_index].description);
            }
            return 1;
        }
    }

    return 0;
}

static void dashboard_wrap_text(SDL_Renderer *renderer,
                                TTF_Font *font,
                                const char *text,
                                SDL_Rect rect,
                                SDL_Color color){
    if (!renderer || !font || !text) return;

    char line[512] = "";
    char word[128];
    int y = rect.y;
    int line_h = TTF_FontHeight(font) + 4;
    const char *p = text;

    while (*p && y + line_h <= rect.y + rect.h) {
        int wi = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && *p != '\n' && wi + 1 < (int)sizeof(word)) word[wi++] = *p++;
        word[wi] = '\0';

        char test[640];
        if (line[0]) snprintf(test, sizeof(test), "%s %s", line, word);
        else snprintf(test, sizeof(test), "%s", word);

        int tw = 0;
        int th = 0;
        TTF_SizeText(font, test, &tw, &th);

        if (tw > rect.w && line[0]) {
            draw_text(renderer, font, line, rect.x, y, color);
            y += line_h;
            snprintf(line, sizeof(line), "%s", word);
        }
        else {
            snprintf(line, sizeof(line), "%s", test);
        }

        if (*p == '\n') {
            if (line[0]) draw_text(renderer, font, line, rect.x, y, color);
            y += line_h;
            line[0] = '\0';
            p++;
        }
    }

    if (line[0] && y + line_h <= rect.y + rect.h) {
        draw_text(renderer, font, line, rect.x, y, color);
    }
}

static int dashboard_case_country_matches(const Type_Dashboard_Case_Point *pt, const WM_Country *country){
    if (!pt || !country || pt->country[0] == '\0') return 0;
    if (dashboard_ascii_equal_ci(pt->country, country->name)) return 1;
    if (country->alpha2 && country->alpha2[0] && dashboard_ascii_equal_ci(pt->country, country->alpha2)) return 1;
    return 0;
}

static int dashboard_collect_cases_for_country(const WM_Country *country, int *case_indices, int max_indices){
    int count = 0;
    if (!country || !case_indices || max_indices <= 0) return 0;

    for (int i = 0; i < Global_Dashboard_Case_Point_Count; i++) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];
        if (pt->case_index < 0 || pt->case_index >= Global_Dashboard_Case_Count) continue;
        if (!dashboard_case_country_matches(pt, country)) continue;

        int already = 0;
        for (int j = 0; j < count; j++) {
            if (case_indices[j] == pt->case_index) {
                already = 1;
                break;
            }
        }
        if (already) continue;

        if (count < max_indices) {
            case_indices[count] = pt->case_index;
        }
        count++;
    }

    return count;
}

static void dashboard_draw_hover_country_cases(Type_Dashboard_State *dashboard,
                                               SDL_Renderer *renderer,
                                               TTF_Font *font,
                                               SDL_Rect sidebar){
    if (!dashboard || !renderer || !font || dashboard->selected_case >= 0) return;
    if (dashboard->hover_country < 0 || dashboard->hover_country >= (int)WM_DATA.country_count) return;

    WM_Country *country = &WM_DATA.countries[dashboard->hover_country];
    int case_indices[DASHBOARD_MAX_CASES];
    int total_cases = dashboard_collect_cases_for_country(country,
                                                          case_indices,
                                                          DASHBOARD_MAX_CASES);

    int x = sidebar.x + 18;
    int y = sidebar.y + 352;
    int bottom = sidebar.y + sidebar.h - 18;
    if (y >= bottom) return;

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
    if (visible_rows < 1) visible_rows = 1;

    if (dashboard->country_case_scroll < 0) dashboard->country_case_scroll = 0;
    int max_scroll = total_cases - visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (dashboard->country_case_scroll > max_scroll) dashboard->country_case_scroll = max_scroll;

    if (total_cases <= 0) {
        dashboard_wrap_text(renderer,
                            font,
                            "No saved cases for this country.",
                            (SDL_Rect){panel.x + 10, list_y, panel.w - 20, list_h},
                            Dashboard_Muted);
        return;
    }

    for (int row = 0; row < visible_rows; row++) {
        int idx = dashboard->country_case_scroll + row;
        if (idx >= total_cases || idx >= DASHBOARD_MAX_CASES) break;
        int case_index = case_indices[idx];
        if (case_index < 0 || case_index >= Global_Dashboard_Case_Count) continue;

        SDL_Rect item = {panel.x + 10,
                         list_y + row * row_h,
                         panel.w - 20,
                         row_h - 6};

        SDL_Color c = Global_Dashboard_Cases[case_index].color;
        SDL_Rect swatch = {item.x, item.y + 5, 8, 8};
        draw_filled_rect(renderer, swatch, c);

        char label[256];
        snprintf(label, sizeof(label), "%s  (%d signals)",
                 Global_Dashboard_Cases[case_index].case_number,
                 Global_Dashboard_Cases[case_index].point_count);

        dashboard_wrap_text(renderer,
                            font,
                            label,
                            (SDL_Rect){item.x + 16, item.y, item.w - 16, item.h},
                            Dashboard_Muted);
    }

    if (total_cases > visible_rows) {
        char scroll_line[96];
        snprintf(scroll_line, sizeof(scroll_line), "Scroll %d/%d",
                 dashboard->country_case_scroll + 1,
                 max_scroll + 1);
        draw_text(renderer, font, scroll_line, panel.x + 10, panel.y + panel.h - 22, (SDL_Color){120, 180, 140, 255});
    }
}

static void dashboard_draw_case_search(Type_Dashboard_State *dashboard,
                                       SDL_Renderer *renderer,
                                       TTF_Font *font,
                                       SDL_Rect area){
    if (!dashboard || !renderer || !font) return;

    draw_text(renderer,
              font,
              "Search by Case",
              area.x,
              area.y,
              Dashboard_Text);

    SDL_Rect input = {area.x,
                      area.y + 24,
                      area.w,
                      30};
    dashboard->case_search_rect = input;

    draw_filled_rect(renderer, input, (SDL_Color){0, 12, 5, 245});
    draw_outline_rect(renderer, input, dashboard->case_search_active ? Dashboard_Border_Hi : Dashboard_Border);

    const char *shown = dashboard->case_search_text[0] ? dashboard->case_search_text : "type case name...";
    draw_text(renderer,
              font,
              shown,
              input.x + 8,
              input.y + 7,
              dashboard->case_search_text[0] ? Dashboard_Text : Dashboard_Muted);

    if (dashboard->case_search_active && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {
        int cursor = dashboard->case_search_cursor;
        int len = (int)strlen(dashboard->case_search_text);
        if (cursor < 0) cursor = 0;
        if (cursor > len) cursor = len;

        int text_w = 0;
        int text_h = 0;
        if (cursor > 0) {
            char before[128];
            if (cursor >= (int)sizeof(before)) cursor = (int)sizeof(before) - 1;
            memcpy(before, dashboard->case_search_text, (size_t)cursor);
            before[cursor] = '\0';
            if (TTF_SizeText(font, before, &text_w, &text_h) != 0) text_w = cursor * 8;
        }

        int cx = input.x + 8 + text_w;
        int cy0 = input.y + 6;
        int cy1 = input.y + input.h - 6;
        if (cx < input.x + 8) cx = input.x + 8;
        if (cx > input.x + input.w - 8) cx = input.x + input.w - 8;

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
        SDL_RenderDrawLine(renderer, cx, cy0, cx, cy1);
        SDL_RenderDrawLine(renderer, cx + 1, cy0, cx + 1, cy1);
    }
}

static void dashboard_draw_case_sidebar(Type_Dashboard_State *dashboard,
                                        SDL_Renderer *renderer,
                                        TTF_Font *font,
                                        SDL_Rect sidebar){
    if (!dashboard || dashboard->selected_case < 0 || dashboard->selected_case >= Global_Dashboard_Case_Count) return;

    Type_Dashboard_Case_Info *info = &Global_Dashboard_Cases[dashboard->selected_case];

    draw_filled_rect(renderer, sidebar, (SDL_Color){0, 5, 2, 248});
    draw_outline_rect(renderer, sidebar, info->color);

    draw_text(renderer, font, "CASE", sidebar.x + 16, sidebar.y + 18, Dashboard_Muted);
    draw_text(renderer, font, info->case_number, sidebar.x + 16, sidebar.y + 42, Dashboard_Text);

    char count_line[128];
    snprintf(count_line, sizeof(count_line), "Signals in case: %d", info->point_count);
    draw_text(renderer, font, count_line, sidebar.x + 16, sidebar.y + 72, Dashboard_Muted);

    draw_text(renderer, font, "Description", sidebar.x + 16, sidebar.y + 114, Dashboard_Text);

    dashboard->case_desc_rect = (SDL_Rect){sidebar.x + 16,
                                           sidebar.y + 142,
                                           sidebar.w - 32,
                                           170};

    draw_filled_rect(renderer,
                     dashboard->case_desc_rect,
                     dashboard->case_desc_editing ?
                     (SDL_Color){0, 20, 8, 255} :
                     (SDL_Color){0, 9, 4, 255});
    draw_outline_rect(renderer,
                      dashboard->case_desc_rect,
                      dashboard->case_desc_editing ? Dashboard_Border_Hi : Dashboard_Border);

    const char *shown = dashboard->case_desc_editing ? dashboard->case_desc_edit : info->description;
    if (!shown || !shown[0]) shown = dashboard->case_desc_editing ? "_" : "Click here to add a case description.";

    dashboard_wrap_text(renderer,
                        font,
                        shown,
                        (SDL_Rect){dashboard->case_desc_rect.x + 9,
                                   dashboard->case_desc_rect.y + 9,
                                   dashboard->case_desc_rect.w - 18,
                                   dashboard->case_desc_rect.h - 18},
                        Dashboard_Muted);

    draw_text(renderer,
              font,
              dashboard->case_desc_editing ? "Enter saves | Esc cancels" : "Click description to edit",
              sidebar.x + 16,
              dashboard->case_desc_rect.y + dashboard->case_desc_rect.h + 12,
              Dashboard_Muted);

    int y = dashboard->case_desc_rect.y + dashboard->case_desc_rect.h + 48;
    draw_text(renderer, font, "Signals", sidebar.x + 16, y, Dashboard_Text);
    y += 28;

    int shown_count = 0;
    for (int i = 0; i < Global_Dashboard_Case_Point_Count && shown_count < 7; i++) {
        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[i];
        if (pt->case_index != dashboard->selected_case) continue;

        char line[256];
        snprintf(line, sizeof(line), "%s  %.4f, %.4f", pt->signal_name[0] ? pt->signal_name : "Unnamed signal", pt->latitude, pt->longitude);
        draw_text(renderer, font, line, sidebar.x + 16, y, Dashboard_Muted);
        y += 24;

        if (pt->country[0]) {
            draw_text(renderer, font, pt->country, sidebar.x + 30, y, (SDL_Color){120, 180, 140, 255});
            y += 22;
        }

        shown_count++;
    }
}

static int dashboard_handle_case_search_event(Type_Dashboard_State *dashboard, const SDL_Event *event){
    if (!dashboard || !event) return 0;

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        if (dashboard_point_in_rect(event->button.x, event->button.y, dashboard->case_search_rect)) {
            dashboard->case_search_active = 1;
            dashboard->case_search_cursor = (int)strlen(dashboard->case_search_text);
            return 1;
        }
        dashboard->case_search_active = 0;
        return 0;
    }

    if (!dashboard->case_search_active) return 0;

    if (event->type == SDL_TEXTINPUT) {
        int len = (int)strlen(dashboard->case_search_text);
        int cursor = dashboard->case_search_cursor;
        if (cursor < 0) cursor = 0;
        if (cursor > len) cursor = len;

        size_t add = strlen(event->text.text);
        if (add > 0 && len + (int)add < (int)sizeof(dashboard->case_search_text)) {
            memmove(dashboard->case_search_text + cursor + (int)add,
                    dashboard->case_search_text + cursor,
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
            if (dashboard->case_search_cursor > 0) dashboard->case_search_cursor--;
            return 1;
        }
        if (key == SDLK_RIGHT) {
            if (dashboard->case_search_cursor < len) dashboard->case_search_cursor++;
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
                memmove(dashboard->case_search_text + cursor - 1,
                        dashboard->case_search_text + cursor,
                        (size_t)(len - cursor) + 1U);
                dashboard->case_search_cursor--;
            }
            return 1;
        }
        if (key == SDLK_DELETE) {
            int cursor = dashboard->case_search_cursor;
            if (cursor >= 0 && cursor < len) {
                memmove(dashboard->case_search_text + cursor,
                        dashboard->case_search_text + cursor + 1,
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

static int dashboard_handle_case_sidebar_event(Type_Dashboard_State *dashboard, const SDL_Event *event){
    if (!dashboard || !event) return 0;

    if (dashboard->case_desc_editing) {
        if (event->type == SDL_TEXTINPUT) {
            size_t used = strlen(dashboard->case_desc_edit);
            size_t add = strlen(event->text.text);
            if (used + add < sizeof(dashboard->case_desc_edit)) {
                strcat(dashboard->case_desc_edit, event->text.text);
            }
            return 1;
        }

        if (event->type == SDL_KEYDOWN) {
            SDL_Keycode key = event->key.keysym.sym;
            if (key == SDLK_BACKSPACE) {
                size_t len = strlen(dashboard->case_desc_edit);
                if (len > 0) dashboard->case_desc_edit[len - 1] = '\0';
                return 1;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (dashboard->selected_case >= 0 && dashboard->selected_case < Global_Dashboard_Case_Count) {
                    snprintf(Global_Dashboard_Cases[dashboard->selected_case].description,
                             sizeof(Global_Dashboard_Cases[dashboard->selected_case].description),
                             "%s",
                             dashboard->case_desc_edit);
                    dashboard_save_case_descriptions();
                }
                dashboard->case_desc_editing = 0;
                return 1;
            }
            if (key == SDLK_ESCAPE) {
                if (dashboard->selected_case >= 0 && dashboard->selected_case < Global_Dashboard_Case_Count) {
                    snprintf(dashboard->case_desc_edit,
                             sizeof(dashboard->case_desc_edit),
                             "%s",
                             Global_Dashboard_Cases[dashboard->selected_case].description);
                }
                dashboard->case_desc_editing = 0;
                return 1;
            }
            return 1;
        }
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        if (dashboard->selected_case >= 0 && dashboard_point_in_rect(event->button.x,
                                                                     event->button.y,
                                                                     dashboard->case_desc_rect)) {
            dashboard->case_desc_editing = 1;
            snprintf(dashboard->case_desc_edit,
                     sizeof(dashboard->case_desc_edit),
                     "%s",
                     Global_Dashboard_Cases[dashboard->selected_case].description);
            return 1;
        }
    }

    return 0;
}

static int dashboard_handle_top_tab_event(Type_Dashboard_State *dashboard,
                                          const SDL_Event *event,
                                          int win_w,
                                          int text_entry_active){
    if (!dashboard || !event) return DASHBOARD_EVENT_NONE;

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        if (!text_entry_active) {
            if (key == SDLK_1) return DASHBOARD_EVENT_MAP;
            if (key == SDLK_2) return DASHBOARD_EVENT_RETROSPECTRUM;
            if (key == SDLK_3) return DASHBOARD_EVENT_ANALYSIS;
            if (key == SDLK_4) return DASHBOARD_EVENT_CLASSIFICATION;
        }
        if (key == SDLK_F1) return DASHBOARD_EVENT_MAP;
        if (key == SDLK_F2) return DASHBOARD_EVENT_RETROSPECTRUM;
        if (key == SDLK_F3) return DASHBOARD_EVENT_ANALYSIS;
        if (key == SDLK_F4) return DASHBOARD_EVENT_CLASSIFICATION;
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        Type_Dashboard_Tab tabs[4];
        dashboard_make_tabs(win_w, tabs);
        for (int i = 0; i < 4; i++) {
            if (dashboard_point_in_rect(event->button.x, event->button.y, tabs[i].rect)) {
                return tabs[i].event_id;
            }
        }
    }

    return DASHBOARD_EVENT_NONE;
}

static int dashboard_init(Type_Dashboard_State *dashboard, const char *map_bin_path){
    if (!dashboard) return 0;

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
    dashboard->case_desc_rect = (SDL_Rect){0,0,0,0};
    dashboard->case_search_rect = (SDL_Rect){0,0,0,0};
    dashboard->case_desc_edit[0] = '\0';
    dashboard->case_search_text[0] = '\0';
    dashboard->last_case_scan_ms = 0;

    if (!map_bin_path || map_bin_path[0] == '\0') {
        map_bin_path = "world_map.bin";
    }

    dashboard->map_loaded = WORLD_MAP_load(map_bin_path);

    if (dashboard->map_loaded) {
        snprintf(dashboard->status,
                 sizeof(dashboard->status),
                 "Loaded map data: %s",
                 map_bin_path);
    } else {
        snprintf(dashboard->status,
                 sizeof(dashboard->status),
                 "Map data not loaded. Put world_map.bin next to the executable.");
    }

    dashboard_reload_cases(dashboard);

    return dashboard->map_loaded;
}

static void dashboard_shutdown(void){
    WORLD_MAP_free();
}


static int dashboard_find_country_screen_point(SDL_Rect map,
                                               int country_index,
                                               int *out_x,
                                               int *out_y)
{
    if (country_index < 0 || !out_x || !out_y) return 0;

    if (dashboard_point_in_rect(*out_x, *out_y, map) &&
        WM_country_at(map, *out_x, *out_y) == country_index) {
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

static int dashboard_handle_event(Type_Dashboard_State *dashboard,
                                  const SDL_Event *event,
                                  int win_w,
                                  int win_h){
    if (!dashboard || !event || !dashboard->enabled) return DASHBOARD_EVENT_NONE;

    if (event->type == SDL_QUIT) return DASHBOARD_EVENT_QUIT;

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

        if (key == SDLK_q && !dashboard->case_desc_editing) return DASHBOARD_EVENT_QUIT;
    }

    if (dashboard->current_tab == DASHBOARD_EVENT_MAP && dashboard->map_loaded) {
        int current_mouse_x = 0;
        int current_mouse_y = 0;
        SDL_GetMouseState(&current_mouse_x, &current_mouse_y);

        SDL_Rect content = dashboard_content_rect(win_w, win_h);
        int search_h = 64;
        int search_gap = 10;
        SDL_Rect map = {content.x + 12,
                        content.y + 12,
                        content.w - WORLD_MAP_SIDEBAR_W - 40,
                        content.h - 24};

        if (map.h < DASHBOARD_MIN_MAP_H) {
            map.h = content.h - 24;
        }

        SDL_Rect sidebar = {content.x + content.w - WORLD_MAP_SIDEBAR_W,
                            content.y + 12,
                            WORLD_MAP_SIDEBAR_W,
                            content.h - 24 - search_h - search_gap};
        if (sidebar.h < 260) sidebar.h = content.h - 24;

        if (event->type == SDL_MOUSEWHEEL &&
            dashboard->selected_case < 0 &&
            dashboard_point_in_rect(current_mouse_x, current_mouse_y, sidebar)) {
            dashboard->country_case_scroll -= event->wheel.y;
            if (dashboard->country_case_scroll < 0) dashboard->country_case_scroll = 0;
            return DASHBOARD_EVENT_NONE;
        }

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
            if (dashboard_select_case_at(dashboard, map, event->button.x, event->button.y)) {
                return DASHBOARD_EVENT_NONE;
            }

            if (dashboard_point_in_rect(event->button.x, event->button.y, map)) {
                int clicked_country = WM_country_at(map, event->button.x, event->button.y);
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

        WORLD_MAP_handle_event(event, map);
    }

    return DASHBOARD_EVENT_NONE;
}

static void dashboard_draw(Type_Dashboard_State *dashboard,
                           SDL_Renderer *renderer,
                           TTF_Font *font_small,
                           TTF_Font *font_medium,
                           int win_w,
                           int win_h,
                           int mouse_x,
                           int mouse_y){
    if (!dashboard || !renderer) return;

    SDL_SetRenderDrawColor(renderer,
                           Dashboard_BG.r,
                           Dashboard_BG.g,
                           Dashboard_BG.b,
                           Dashboard_BG.a);
    SDL_RenderClear(renderer);

    dashboard_draw_grid(renderer, win_w, win_h);

    Uint64 now_ms = SDL_GetTicks64();
    if (!dashboard->case_desc_editing && now_ms - dashboard->last_case_scan_ms > 1200) {
        dashboard_reload_cases(dashboard);
        dashboard->last_case_scan_ms = now_ms;
    }

    SDL_Rect content = dashboard_content_rect(win_w, win_h);
    draw_filled_rect(renderer, content, (SDL_Color){0, 6, 3, 235});
    draw_outline_rect(renderer, content, Dashboard_Border);

    if (dashboard->map_loaded) {
        int search_h = 64;
        int search_gap = 10;
        SDL_Rect sidebar = {content.x + content.w - WORLD_MAP_SIDEBAR_W,
                            content.y + 12,
                            WORLD_MAP_SIDEBAR_W,
                            content.h - 24 - search_h - search_gap};
        if (sidebar.h < 260) sidebar.h = content.h - 24;
        SDL_Rect search_area = {content.x + content.w - WORLD_MAP_SIDEBAR_W,
                                sidebar.y + sidebar.h + search_gap,
                                WORLD_MAP_SIDEBAR_W,
                                search_h};
        SDL_Rect map = {content.x + 12,
                        content.y + 12,
                        content.w - WORLD_MAP_SIDEBAR_W - 40,
                        content.h - 24};

        if (map.h < DASHBOARD_MIN_MAP_H) {
            map.h = content.h - 24;
        }

        int draw_mouse_x = mouse_x;
        int draw_mouse_y = mouse_y;

        if (dashboard->locked_country >= 0 &&
            dashboard->locked_country < (int)WM_DATA.country_count) {
            dashboard->hover_country = dashboard->locked_country;
            draw_mouse_x = dashboard->locked_mouse_x;
            draw_mouse_y = dashboard->locked_mouse_y;

            if (dashboard_find_country_screen_point(map,
                                                    dashboard->locked_country,
                                                    &draw_mouse_x,
                                                    &draw_mouse_y)) {
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
    } else {
        draw_text(renderer,
                  font_medium,
                  "world_map.bin was not loaded",
                  content.x + 24,
                  content.y + 26,
                  Dashboard_Warn);
        draw_text(renderer,
                  font_small,
                  "Put world_map.bin in the working directory, or change the path passed to dashboard_init().",
                  content.x + 24,
                  content.y + 58,
                  Dashboard_Muted);
    }

    draw_text(renderer,
              font_small,
              dashboard->status,
              DASHBOARD_MARGIN + 8,
              win_h - 38,
              dashboard->map_loaded ? Dashboard_Muted : Dashboard_Warn);
}

// ======================
// Global Initializations
// ======================

/*
        GLOBAL DEFINITIONS              VALUE
*/

#define DEFAULT_CENTER_FREQ_HZ          101300000ULL
#define DEFAULT_SAMPLE_RATE_HZ          2000000U
#define DEFAULT_DISPLAY_SPAN_HZ         1000000U
#define DEFAULT_LNA_GAIN                16
#define DEFAULT_VGA_GAIN                12
#define DEFAULT_AMP_ENABLE              0
#define DEFAULT_DC_CORRECTION_ENABLE    0
#define DEFAULT_WATERFALL_FPS           60
#define DEFAULT_ROWS_PER_FRAME          4

#define MIN_WINDOW_WIDTH                1320
#define MIN_WINDOW_HEIGHT               650

#define REL_MIN_DB                      2.0
#define REL_MAX_DB                      22.0

#define CONTROL_PANEL_HEIGHT            95
#define AXIS_HEIGHT                     70
#define MARGIN                          20

#define PRE_RECORD_SECONDS              5
#define REC_QUEUE_SECONDS               (PRE_RECORD_SECONDS * 2)
#define REC_PUSH_CHUNK_SAMPLES          4096

#define REC_FIR_TAPS                    255

#ifndef M_PI
#define M_PI                            3.14159265358979323846
#endif

#define MAX_TRANSFER_SAMPLES            262144

#define DEFAULT_RECORD_DIR              "Recordings"

#define ANALYSIS_MAX_FILES               512
#define ANALYSIS_FFT_SIZE                2048
#define ANALYSIS_LIST_WIDTH              430
/* ANALYSIS_MAX_RENDER_W is defined in include/GUIs.h so the extern arrays match */
#define ANALYSIS_MAX_CONST_POINTS        4096


static volatile sig_atomic_t Global_Running = 1;

static pthread_mutex_t Global_Rec_Lock = PTHREAD_MUTEX_INITIALIZER;

/*

TYPE            VARIABLE                VALUE

*/

uint64_t        Global_Rec_Center_Hz    = 0;
uint64_t        Global_Center_Freq_Hz   = DEFAULT_CENTER_FREQ_HZ;
uint32_t        Global_Sample_Rate_Hz   = DEFAULT_SAMPLE_RATE_HZ;
uint32_t        Global_Display_Span_Hz  = DEFAULT_DISPLAY_SPAN_HZ;
int             Global_Amp_Enable       = DEFAULT_AMP_ENABLE;
int             Global_DC_Enable        = DEFAULT_DC_CORRECTION_ENABLE;
int             Global_Fullscreen       = 0;
int             Global_Rec              = 0;
char            Global_Status_Msg[256]  = "";

SDL_Color       Global_Status_Color     = {0, 255, 80, 255};

Type_Selector   Global_Selector         = {.X0 = 0.40,
                                           .X1 = 0.60,
                                           .enabled = 0,
                                           .dragging = 0,
                                           .resizing_left = 0,
                                           .resizing_right = 0
                                          };

/*

        TYPE            VARIABLE                VALUE

*/

static  FILE*           Global_Rec_File         = NULL;
static  uint32_t        Global_Rec_BW_Hz        = 0;
static  uint32_t        Global_Rec_Out_Rate_Hz  = 0;
static  int16_t         *Global_Rec_Pre_I       = NULL;
static  int16_t         *Global_Rec_Pre_Q       = NULL;
double*                 Global_Color_Baseline   = NULL;       
static  double          Global_DC_I             = 0.0;
static  double          Global_DC_Q             = 0.0;
static  double          Global_Rec_Phase        = 0.0;
static  double          Global_Rec_Acc_I        = 0.0;
static  double          Global_Rec_Acc_Q        = 0.0;
static  size_t          Global_Rec_Pre_Count    = 0;
static  int             Global_Rec_FIR_Pos      = 0;
static  int             Global_LNA_Gain         = DEFAULT_LNA_GAIN;
static  int             Global_VGA_Gain         = DEFAULT_VGA_GAIN;
static  int             Global_Waterfall_FPS    = DEFAULT_WATERFALL_FPS;
static  int             Global_Rows_Per_Frame   = DEFAULT_ROWS_PER_FRAME;
static  int             Global_Rec_Acc_Count    = 0;
static  int             Global_Rec_Decimation   = 1;
static  int             Global_Radio_Running    = 0;
static  int             Global_Cached_Recording = 0;
static  char            Global_Record_Dir[512]  = DEFAULT_RECORD_DIR;



static  double          Global_Rec_FIR[REC_FIR_TAPS];
static  double          Global_Rec_Hist_I[REC_FIR_TAPS];
static  double          Global_Rec_Hist_Q[REC_FIR_TAPS];
static  float           temp_I[MAX_TRANSFER_SAMPLES];
static  float           temp_Q[MAX_TRANSFER_SAMPLES];

static  Type_RingBuf    ring_buf;
static  Type_Rec_Cache  Global_Pre_Cache;
static  Type_Rec_Queue  Global_Rec_Queue;

static  pthread_t       Global_Rec_Thread;
static  int             Global_Rec_Thread_Running = 0;

// =========
// Functions
// =========

// OS Signal Handling

static void handle_sigint(int sig){

    /*

    Purpose: Handles SIGINT shutdown requests

    Return: No return

    */

    (void)sig;
    Global_Running = 0;

}

// Hard Bounds

double limit_double(double value, double low, double high){

    /*

    Purpose: Clamps a double value between lower and upper bounds

    Return: Clamped value

    */

    if (value < low) return low;

    if (value > high) return high;

    return value;

}

// Target Path Validation and Creation

static int ensure_record_dir_exists(void){

    /*

    Purpose: Ensures the recording directory exists and is usable

    Return: Directory status

    */

    struct stat st;

    if (stat(Global_Record_Dir, &st) == 0) {

        if (S_ISDIR(st.st_mode)) return 1;

        return 0;

    }

    if (mkdir(Global_Record_Dir, 0755) == 0) {

        return 1;

    }

    return 0;

}

// Selector Helpers

uint64_t selection_center_Hz(void){

    /*

    Purpose: Computes the selected recording center frequency in Hz

    Return: Center frequency

    */

    double Center_Frac = (Global_Selector.X0 + Global_Selector.X1) * 0.5;

    double Offset_Hz = (Center_Frac - 0.5) * (double)Global_Display_Span_Hz;

    double Calc_Freq = (double)Global_Center_Freq_Hz + Offset_Hz;

    if (Calc_Freq < 0.0) Calc_Freq = 0.0;

    return (uint64_t)Calc_Freq;

}

uint32_t selection_BW_Hz(void){

    /*

    Purpose: Computes the selected recording bandwidth in Hz

    Return: Bandwidth value

    */

    double BW = fabs(Global_Selector.X1 - Global_Selector.X0) * (double)Global_Display_Span_Hz;

    if (BW < 1000.0) BW = 1000.0;

    if (BW > (double)Global_Sample_Rate_Hz) BW = (double)Global_Sample_Rate_Hz;

    return (uint32_t)BW;

}

// RF Filter

static void configure_recording_filter(void) {

    /*

    Purpose: Configures the FIR filter and decimation used by recording

    Return: No return

    */

    memset(Global_Rec_FIR, 0, sizeof(Global_Rec_FIR));
    memset(Global_Rec_Hist_I, 0, sizeof(Global_Rec_Hist_I));
    memset(Global_Rec_Hist_Q, 0, sizeof(Global_Rec_Hist_Q));

    Global_Rec_FIR_Pos = 0;
    Global_Rec_Acc_Count = 0;

     // Output rate should be comfortably above selected bandwidth
     // 2.5x gives room for FIR transition

    double wanted_out_rate = (double)Global_Rec_BW_Hz * 3;

    if (wanted_out_rate < 48000.0) wanted_out_rate = 48000.0;
    
    Global_Rec_Decimation = (int)((double)Global_Sample_Rate_Hz / wanted_out_rate);

    if (Global_Rec_Decimation < 1) Global_Rec_Decimation = 1;

    Global_Rec_Out_Rate_Hz = Global_Sample_Rate_Hz / (uint32_t)Global_Rec_Decimation;

    // After shifting selected center to 0 Hz, selected bandwidth is -BW/2 to +BW/2

    double cutoff_hz = (double)Global_Rec_BW_Hz * 0.5;

    // Keep cutoff below decimated Nyquist

    double max_safe_cutoff = (double)Global_Rec_Out_Rate_Hz * 0.45;

    if (cutoff_hz > max_safe_cutoff) cutoff_hz = max_safe_cutoff;

    // Normalized cutoff relative to input sample rate

    double fc = cutoff_hz / (double)Global_Sample_Rate_Hz;

    double sum = 0.0;
    int mid = REC_FIR_TAPS / 2;

    for (int n = 0; n < REC_FIR_TAPS; n++) {

        int m = n - mid;

        double sinc;

        if (m == 0) sinc = 2.0 * fc;

        else sinc = sin(2.0 * M_PI * fc * (double)m) / (M_PI * (double)m);


        // Hamming window

        double window = 0.54 - 0.46 * cos((2.0 * M_PI * (double)n) / (double)(REC_FIR_TAPS - 1));

        Global_Rec_FIR[n] = sinc * window;
        sum += Global_Rec_FIR[n];

    }

    // Normalize gain to 1.0

    if (fabs(sum) > 1e-12) {

        for (int n = 0; n < REC_FIR_TAPS; n++) {

            Global_Rec_FIR[n] /= sum;

        }

    }
}

// Cache Helpers

static int pre_cache_init(Type_Rec_Cache *c, uint32_t sample_rate_hz){

    /*

    Purpose: Initializes the pre-record IQ cache

    Return: Init status

    */

    memset(c, 0, sizeof(*c));
    
    c->capacity = (size_t)sample_rate_hz * PRE_RECORD_SECONDS;

    c->I = malloc(sizeof(int16_t) * c->capacity);
    c->Q = malloc(sizeof(int16_t) * c->capacity);

    if (!c->I || !c->Q){
        free(c->I);
        free(c->Q);
        c->I = NULL;
        c->Q = NULL;
        c->capacity = 0;
        return 0;
    }

    pthread_mutex_init(&c->lock, NULL);

    return 1;

}

static void pre_cache_free(Type_Rec_Cache *c){

    /*

    Purpose: Frees the pre-record IQ cache

    Return: No return

    */
    
    pthread_mutex_destroy(&c->lock);

    free(c->I);
    free(c->Q);

    memset(c, 0, sizeof(*c));

}

static int pre_cache_resize(Type_Rec_Cache *c, uint32_t sample_rate_hz){

    /*

    Purpose: Resizes the pre-record IQ cache for a new sample rate

    Return: Resize status

    */

    pthread_mutex_lock(&c->lock);

    free(c->I);
    free(c->Q);

    c->capacity = (size_t)sample_rate_hz * PRE_RECORD_SECONDS;
    c->write_pos = 0;
    c->count = 0;

    c->I = malloc(sizeof(int16_t) * c->capacity);
    c->Q = malloc(sizeof(int16_t) * c->capacity);

    int status = (c->I && c->Q);

    if (!status){

        free(c->I);
        free(c->Q);
        c->I = NULL;
        c->Q = NULL;
        c->capacity = 0;
        c->write_pos = 0;
        c->count = 0;

    }

    pthread_mutex_unlock(&c->lock);

    return status;

}

static void pre_cache_write(Type_Rec_Cache *c, float I, float Q){

    /*

    Purpose: Writes one IQ sample into the pre-record cache

    Return: No return

    */

    if (!c->I || !c->Q || c->capacity == 0) return;

    if (I > 1.0f) I = 1.0f;
    if (I < -1.0f) I = -1.0f;

    if (Q > 1.0f) Q = 1.0f;
    if (Q < -1.0f) Q = -1.0f;

    c->I[c->write_pos] = (int16_t)(I * 32767.0f);
    c->Q[c->write_pos] = (int16_t)(Q * 32767.0f);

    c->write_pos = (c->write_pos + 1) % c->capacity;

    if(c->count < c->capacity) c->count++;

}

static size_t pre_cache_snapshot_locked(Type_Rec_Cache *c, int16_t **out_I, int16_t **out_Q){

    /*

    Purpose: Copies the current pre-record cache while already locked

    Return: Snapshot count

    */

    *out_I = NULL;
    *out_Q = NULL;

    size_t count = c->count;

    if (count == 0 || !c->I || !c->Q) return 0;

    int16_t *copy_I = malloc(sizeof(int16_t) * count);
    int16_t *copy_Q = malloc(sizeof(int16_t) * count);

    if (!copy_I || !copy_Q) {

        free(copy_I);
        free(copy_Q);
        return 0;

    }

    if(c->count < c->capacity){

        memcpy(copy_I, c->I, sizeof(int16_t) * count);
        memcpy(copy_Q, c->Q, sizeof(int16_t) * count);

    }

    else {

        size_t start = c->write_pos;
        size_t first = c->capacity - start;
        size_t second = start;

        memcpy(copy_I, c->I + start, sizeof(int16_t) * first);
        memcpy(copy_Q, c->Q + start, sizeof(int16_t) * first);

        memcpy(copy_I + first, c->I, sizeof(int16_t) * second);
        memcpy(copy_Q + first, c->Q, sizeof(int16_t) * second);

    }

    *out_I = copy_I;
    *out_Q = copy_Q;

    return count;

}

// Queue Helpers

static int rec_queue_init(Type_Rec_Queue *q, uint32_t sample_rate_hz){

    /*

    Purpose: Initializes the recording queue

    Return: Init status

    */

    memset(q, 0, sizeof(*q));

    // +1 because this ring-buffer design leaves one slot empty
  
    q->capacity = ((size_t)sample_rate_hz * REC_QUEUE_SECONDS) + 1;

    q->I = malloc(sizeof(int16_t) * q->capacity);
    q->Q = malloc(sizeof(int16_t) * q->capacity);

    if (!q->I || !q->Q) {

        free(q->I);
        free(q->Q);
        memset(q, 0, sizeof(*q));
        return 0;

    }

    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->data_cond, NULL);

    return 1;

}

static void rec_queue_free(Type_Rec_Queue *q){

    /*

    Purpose: Frees the recording queue

    Return: No return

    */

    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->data_cond);

    free(q->I);
    free(q->Q);

    memset(q, 0, sizeof(*q));

}

static int rec_queue_resize(Type_Rec_Queue *q, uint32_t sample_rate_hz){

    /*

    Purpose: Resizes the recording queue for a new sample rate

    Return: Resize status

    */

    pthread_mutex_lock(&q->lock);

    free(q->I);
    free(q->Q);

    q->capacity = ((size_t)sample_rate_hz * REC_QUEUE_SECONDS) + 1;
    q->read_pos = 0;
    q->write_pos = 0;
    q->stop_requested = 0;
    q->overflow = 0;

    q->I = malloc(sizeof(int16_t) * q->capacity);
    q->Q = malloc(sizeof(int16_t) * q->capacity);

    int result = (q->I && q->Q);

    if (!result){

        free(q->I);
        free(q->Q);

        q->I = NULL;
        q->Q = NULL;
        q->capacity = 0;
        q->read_pos = 0;
        q->write_pos = 0;

    }

    pthread_mutex_unlock(&q->lock);
    
    return result;

}

static size_t rec_queue_available_locked(Type_Rec_Queue *q){

    /*

    Purpose: Returns the number of queued samples while already locked

    Return: Available samples

    */

    if (q->write_pos >= q->read_pos){
        
        return q->write_pos - q->read_pos;

    }

    return q->capacity - q->read_pos + q->write_pos;

}

static void rec_queue_reset(Type_Rec_Queue *q){

    /*

    Purpose: Resets the recording queue state

    Return: No return

    */

    pthread_mutex_lock(&q->lock);

    q->read_pos = 0;
    q->write_pos = 0;
    q->stop_requested = 0;
    q->overflow = 0;

    pthread_mutex_unlock(&q->lock);

}

static size_t rec_queue_push_block(Type_Rec_Queue *q, const float *in_I, const float *in_Q,
                                    size_t count){

    /*

    Purpose: Pushes a block of IQ samples into the recording queue

    Return: Pushed samples

    */

    if (!q->I || !q->Q || q->capacity == 0) return 0;
    if (!in_I || !in_Q || count == 0) return 0;

    size_t total_pushed = 0;

    while (total_pushed < count){

        size_t chunk_count = count - total_pushed;

        if (chunk_count > REC_PUSH_CHUNK_SAMPLES){

            chunk_count = REC_PUSH_CHUNK_SAMPLES;

        }

        pthread_mutex_lock(&q->lock);

        if (q->stop_requested){
            
            pthread_mutex_unlock(&q->lock);
            break;

        }

        size_t pushed_chunk = 0;

        for (size_t n = 0; n < chunk_count; n++){

            size_t src_idx = total_pushed + n;
            size_t next = (q->write_pos + 1) % q->capacity;

            if (next == q->read_pos){

                q->overflow = 1;
                break;

            }

            float I = in_I[src_idx];
            float Q = in_Q[src_idx];

            if (I > 1.0f) I = 1.0f;
            if (I < -1.0f) I = -1.0f;

            if (Q > 1.0f) Q = 1.0f;
            if (Q < -1.0f) Q = -1.0f;

            q->I[q->write_pos] = (int16_t)(I * 32767.0f);
            q->Q[q->write_pos] = (int16_t)(Q * 32767.0f);

            q->write_pos = next;
            pushed_chunk++;

        }

        if (pushed_chunk > 0) pthread_cond_signal(&q->data_cond);

        pthread_mutex_unlock(&q->lock);

        total_pushed += pushed_chunk;

        // If queue becomes full before full chunk is pushed

        if (pushed_chunk < chunk_count){

            break;

        }

    }

    return total_pushed;

}

static size_t rec_queue_pop_block(Type_Rec_Queue *q, int16_t *out_I, int16_t *out_Q,
                                  size_t max_count){

    /*

    Purpose: Pops a block of IQ samples from the recording queue

    Return: Popped samples

    */

    pthread_mutex_lock(&q->lock);

    while (rec_queue_available_locked(q) == 0 && !q->stop_requested){

        pthread_cond_wait(&q->data_cond, &q->lock);

    }

    size_t available = rec_queue_available_locked(q);

    if (available == 0 && q->stop_requested){
    
        pthread_mutex_unlock(&q->lock);
        return 0;

    }

    if (available > max_count){

        available = max_count;

    }

    for (size_t n = 0; n < available; n++){

        out_I[n] = q->I[q->read_pos];
        out_Q[n] = q->Q[q->read_pos];

        q->read_pos = (q->read_pos + 1) % q->capacity;

    }

    pthread_mutex_unlock(&q->lock);

    return available;

}

static void rec_queue_request_stop(Type_Rec_Queue *q){

    /*

    Purpose: Requests the recording queue to stop blocking operations

    Return: No return

    */

    pthread_mutex_lock(&q->lock);

    q->stop_requested = 1;

    pthread_cond_broadcast(&q->data_cond);

    pthread_mutex_unlock(&q->lock);

}

// Recorder Helpers

static void recorder_reset_processing_state(void){

    /*

    Purpose: Resets recorder filter and mixer state

    Return: No return

    */

    Global_Rec_Phase = 0.0;
    Global_Rec_Acc_I = 0.0;
    Global_Rec_Acc_Q = 0.0;
    Global_Rec_Acc_Count = 0;
    Global_Rec_FIR_Pos = 0;

    memset(Global_Rec_Hist_I, 0, sizeof(Global_Rec_Hist_I));
    memset(Global_Rec_Hist_Q, 0, sizeof(Global_Rec_Hist_Q));

}

static void recorder_write_sample(float I, float Q){

    /*

    Purpose: Processes and writes one IQ sample to the active recording file

    Return: No return

    */

    if (!Global_Rec_File) return;

    // Shift selected center frequency to baseband

    double Freq_Offset_Hz = (double)Global_Rec_Center_Hz - (double)Global_Center_Freq_Hz;
    double Phase_Step = -2.0 * M_PI * Freq_Offset_Hz / (double)Global_Sample_Rate_Hz;

    double C = cos(Global_Rec_Phase);
    double S = sin(Global_Rec_Phase);

    double Shifted_I = I * C - Q * S;
    double Shifted_Q = I * S + Q * C;

    Global_Rec_Phase += Phase_Step;

    if (Global_Rec_Phase > M_PI) Global_Rec_Phase -= 2.0 * M_PI;

    if (Global_Rec_Phase < -M_PI) Global_Rec_Phase += 2.0 * M_PI;

    if (Global_Rec_Decimation <= 1) {
    if (Shifted_I > 1.0) Shifted_I = 1.0;
    if (Shifted_I < -1.0) Shifted_I = -1.0;

    if (Shifted_Q > 1.0) Shifted_Q = 1.0;
    if (Shifted_Q < -1.0) Shifted_Q = -1.0;

    int16_t iq_pair[2];

    iq_pair[0] = (int16_t)(Shifted_I * 32767.0);
    iq_pair[1] = (int16_t)(Shifted_Q * 32767.0);

    fwrite(iq_pair, sizeof(int16_t), 2, Global_Rec_File);
    return;
    }

    // Always store the newest shifted sample

    Global_Rec_Hist_I[Global_Rec_FIR_Pos] = Shifted_I;
    Global_Rec_Hist_Q[Global_Rec_FIR_Pos] = Shifted_Q;

    int newest_pos = Global_Rec_FIR_Pos;

    Global_Rec_FIR_Pos++;

    if (Global_Rec_FIR_Pos >= REC_FIR_TAPS) Global_Rec_FIR_Pos = 0;

    // Decimation gate
    // Do not run the full FIR convolution unless this input sample will produce one output sample

    Global_Rec_Acc_Count++;

    if (Global_Rec_Acc_Count < Global_Rec_Decimation) return;

    Global_Rec_Acc_Count = 0;

    // FIR convolution only on output samples

    double Filtered_I = 0.0;
    double Filtered_Q = 0.0;

    int hist_idx = newest_pos;

    for (int tap = 0; tap < REC_FIR_TAPS; tap++) {

        Filtered_I += Global_Rec_FIR[tap] * Global_Rec_Hist_I[hist_idx];
        Filtered_Q += Global_Rec_FIR[tap] * Global_Rec_Hist_Q[hist_idx];

        hist_idx--;

        if (hist_idx < 0) hist_idx = REC_FIR_TAPS - 1;

    }

    if (Filtered_I > 1.0) Filtered_I = 1.0;
    if (Filtered_I < -1.0) Filtered_I = -1.0;

    if (Filtered_Q > 1.0) Filtered_Q = 1.0;
    if (Filtered_Q < -1.0) Filtered_Q = -1.0;

    int16_t iq_pair[2];

    iq_pair[0] = (int16_t)(Filtered_I * 32767.0);
    iq_pair[1] = (int16_t)(Filtered_Q * 32767.0);

    fwrite(iq_pair, sizeof(int16_t), 2, Global_Rec_File);
}


static void *recorder_thread_main(void *arg){

    /*

    Purpose: Drains queued IQ samples and writes the active recording file

    Return: Thread result

    */

    (void)arg;

    if (Global_Rec_Pre_I && Global_Rec_Pre_Q && Global_Rec_Pre_Count > 0) {

        for (size_t n = 0; n < Global_Rec_Pre_Count; n++) {

            float I = (float)Global_Rec_Pre_I[n] / 32768.0f;
            float Q = (float)Global_Rec_Pre_Q[n] / 32768.0f;

            recorder_write_sample(I, Q);

        }

        fflush(Global_Rec_File);

    }

    int16_t *buf_I = malloc(sizeof(int16_t) * REC_PUSH_CHUNK_SAMPLES);
    int16_t *buf_Q = malloc(sizeof(int16_t) * REC_PUSH_CHUNK_SAMPLES);

    if (!buf_I || !buf_Q) {

        free(buf_I);
        free(buf_Q);

        if (Global_Rec_File) {

            fclose(Global_Rec_File);
            Global_Rec_File = NULL;

        }

        return NULL;

    }

    while(1) {

        size_t popped = rec_queue_pop_block(&Global_Rec_Queue,
                                         buf_I,
                                         buf_Q,
                                         REC_PUSH_CHUNK_SAMPLES);

        if (popped == 0) break;

        for (size_t n = 0; n < popped; n++) {

            float I = (float)buf_I[n] / 32768.0f;
            float Q = (float)buf_Q[n] / 32768.0f;

            recorder_write_sample(I, Q);

        }

    }

    free(buf_I);
    free(buf_Q);

    if (Global_Rec_File) {

        fflush(Global_Rec_File);
        fclose(Global_Rec_File);
        Global_Rec_File = NULL;

    }

    return NULL;

}


static void stop_recording(void){

    /*

    Purpose: Stops recording and drains the recording queue

    Return: No return

    */

    int thread_exists = 0;

    pthread_mutex_lock(&Global_Rec_Lock);

    if (!Global_Rec && !Global_Rec_Thread_Running){

        pthread_mutex_unlock(&Global_Rec_Lock);
        set_status("", (SDL_Color){0, 255, 80, 255});
        return;

    }

    Global_Rec = 0;
    thread_exists = Global_Rec_Thread_Running;

    pthread_mutex_unlock(&Global_Rec_Lock);

    // Finish after draining queued samples
    
    rec_queue_request_stop(&Global_Rec_Queue);

    if (thread_exists){

        pthread_join(Global_Rec_Thread, NULL);
        Global_Rec_Thread_Running = 0;

    }

    free(Global_Rec_Pre_I);
    free(Global_Rec_Pre_Q);

    Global_Rec_Pre_I = NULL;
    Global_Rec_Pre_Q = NULL;
    Global_Rec_Pre_Count = 0;

    recorder_reset_processing_state();

    if(Global_Rec_Queue.overflow){

        set_status("Recording stopped - queue overflow occurred", (SDL_Color){255, 180, 40, 255});

    }

    else {

        set_status("", (SDL_Color){0, 255, 80, 255});

    }

}

static int start_recording(void){

    /*

    Purpose: Runs the background recording writer thread

    Return: Start status

    */

    if (Global_Rec) return 1;

    Global_Rec_Center_Hz = selection_center_Hz();
    Global_Rec_BW_Hz = selection_BW_Hz();

    configure_recording_filter();
    recorder_reset_processing_state();
    rec_queue_reset(&Global_Rec_Queue);

    char datetime_str[32];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    strftime(datetime_str, sizeof(datetime_str), "%m-%d-%Y_%H-%M-%S", tm_now);

    if (!ensure_record_dir_exists()) {

    Global_Rec = 0;
    set_status("Record directory failed", (SDL_Color){255, 60, 40, 255});
    return 0;
    }

    char filename[1024];

    snprintf(filename,
             sizeof(filename),
             "%s/%s_CAPTURE_%.6fMHz_BW_%.3fkHz_SR_%.3fk_Decimation_%d.complex16",
             Global_Record_Dir,
             datetime_str,
             Global_Rec_Center_Hz / 1e6,
             Global_Rec_BW_Hz / 1e3,
             Global_Rec_Out_Rate_Hz / 1e3,
             Global_Rec_Decimation
            );

    Global_Rec_File = fopen(filename, "wb");

    if (!Global_Rec_File){

        Global_Rec = 0;
        set_status("Record Open Failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    free(Global_Rec_Pre_I);
    free(Global_Rec_Pre_Q);

    Global_Rec_Pre_I = NULL;
    Global_Rec_Pre_Q = NULL;
    Global_Rec_Pre_Count = 0;

    // CRITICAL FOR ENSURING MINIMAL GAP BETWEEN CACHE AND LIVE DATA

    if (Global_Cached_Recording) {

        pthread_mutex_lock(&Global_Pre_Cache.lock);

        Global_Rec_Pre_Count = pre_cache_snapshot_locked(&Global_Pre_Cache,
                                                         &Global_Rec_Pre_I,
                                                         &Global_Rec_Pre_Q);

        pthread_mutex_lock(&Global_Rec_Lock);

        Global_Rec = 1;

        pthread_mutex_unlock(&Global_Rec_Lock);

        pthread_mutex_unlock(&Global_Pre_Cache.lock);

    }

    else {

        pthread_mutex_lock(&Global_Rec_Lock);

        Global_Rec = 1;

        pthread_mutex_unlock(&Global_Rec_Lock);

    }

    if (pthread_create(&Global_Rec_Thread, NULL, recorder_thread_main, NULL) != 0){

        pthread_mutex_lock(&Global_Rec_Lock);
        Global_Rec = 0;
        pthread_mutex_unlock(&Global_Rec_Lock);

        rec_queue_request_stop(&Global_Rec_Queue);

        if (Global_Rec_File){

            fclose(Global_Rec_File);
            Global_Rec_File = NULL;

        }

        free(Global_Rec_Pre_I);
        free(Global_Rec_Pre_Q);

        Global_Rec_Pre_I = NULL;
        Global_Rec_Pre_Q = NULL;
        Global_Rec_Pre_Count = 0;

        set_status("Record Thread Failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    Global_Rec_Thread_Running = 1;

    Global_Selector.dragging = 0;
    Global_Selector.resizing_left = 0;
    Global_Selector.resizing_right = 0;

    char msg[256];

    snprintf(msg,
             sizeof(msg),
             "RECORDING %.6f MHz - BW %.3f kHz%s",
             Global_Rec_Center_Hz / 1e6,
             Global_Rec_BW_Hz / 1e3,
             Global_Cached_Recording ? " - CACHE 5s" : "");

    set_status(msg, (SDL_Color){255, 60, 40, 255});

    return 1;

}

// Ring Buffer Helpers

static size_t ring_available_locked(Type_RingBuf* r){

    /*

    Purpose: Returns the number of samples available in the waterfall ring buffer while
             already locked

    Return: Available samples

    */
    
    if (r->write_pos >= r->read_pos) return r->write_pos - r->read_pos;

    return RING_SIZE - r->read_pos + r->write_pos;

}

static void ring_clear(Type_RingBuf *r){

    /*

    Purpose: Clears the waterfall ring buffer

    Return: No return

    */

    pthread_mutex_lock(&r->lock);

    r->write_pos = 0;
    r->read_pos = 0;

    pthread_mutex_unlock(&r->lock);
}

static void ring_write_sample(Type_RingBuf *r, float I, float Q){

    /*

    Purpose: Writes one IQ sample into the waterfall ring buffer

    Return: No return

    */
    
    r->I[r->write_pos] = I;
    r->Q[r->write_pos] = Q;

    r->write_pos = (r->write_pos + 1) % RING_SIZE;

    if (r->write_pos == r->read_pos){

        r->read_pos = (r->read_pos+1) % RING_SIZE;

    }

}

static int ring_read_block(Type_RingBuf *r, fftw_complex *in, double *window){

    /*

    Purpose: Reads one FFT block from the waterfall ring buffer

    Return: Read status

    */

    pthread_mutex_lock(&r->lock);

if (ring_available_locked(r) < FFT_SIZE){

        pthread_mutex_unlock(&r->lock);
        return 0;

    }

    for (int sam = 0; sam < FFT_SIZE; sam++){
        size_t idx = (r->read_pos + sam) % RING_SIZE;
        in[sam][0] = r->I[idx] * window[sam];
        in[sam][1] = r->Q[idx] * window[sam];
    }

    // Hann window is being used, smoothen out any edges and visualize shorter bursts better
    // That explains "+ FFT_SIZE / 2" (Use half of the older samples)

    r->read_pos = (r->read_pos + FFT_SIZE / 2) % RING_SIZE;

    pthread_mutex_unlock(&r->lock);
    return 1;

}

// RX Helper

static int rx_callback(hackrf_transfer *transfer){

    /*

    Purpose: Receives HackRF samples and routes them to cache, display, and recording paths

    Return: Callback status

    */

    const int8_t *buf = (const int8_t *)transfer->buffer;
    int sample_count = transfer->valid_length / 2;

    if (sample_count > MAX_TRANSFER_SAMPLES) {
        sample_count = MAX_TRANSFER_SAMPLES;
    }

    // Convert signed HackRF IQ once

    pthread_mutex_lock(&Global_Pre_Cache.lock);

    for (int n = 0; n < sample_count; n++){

        float I = (float)buf[2 * n] / 128.0f;
        float Q = (float)buf[2 * n + 1] / 128.0f;

        if (Global_DC_Enable) {
            const double alpha = 0.0001;

            Global_DC_I += alpha * ((double)I - Global_DC_I);
            Global_DC_Q += alpha * ((double)Q - Global_DC_Q);

            I -= (float)Global_DC_I;
            Q -= (float)Global_DC_Q;
        }

        temp_I[n] = I;
        temp_Q[n] = Q;

        pre_cache_write(&Global_Pre_Cache, I, Q);
    }

    pthread_mutex_unlock(&Global_Pre_Cache.lock);

    // Waterfall ring buffer
    // Lock only around ring_buf writes

    pthread_mutex_lock(&ring_buf.lock);

    for (int n = 0; n < sample_count; n++){
        ring_write_sample(&ring_buf, temp_I[n], temp_Q[n]);
    }

    pthread_mutex_unlock(&ring_buf.lock);

    // Recorder path
    // No ring_buf.lock here

    int rec_enabled = 0;

    pthread_mutex_lock(&Global_Rec_Lock);
    rec_enabled = Global_Rec;
    pthread_mutex_unlock(&Global_Rec_Lock);

    if (rec_enabled){

        size_t pushed = rec_queue_push_block(&Global_Rec_Queue, temp_I, temp_Q, (size_t)sample_count);

        if (pushed < (size_t)sample_count){
        
            Global_Rec_Queue.overflow = 1;

        }

    }

    return 0;
}

// Graphics Helper

static void compute_DB_from_FFT(fftw_complex *out, double *db){

    /*

    Purpose: Converts FFT output bins into shifted decibel values

    Return: No return

    */

    for (int sam = 0; sam < FFT_SIZE; sam++){

        int shifted_sam = (sam + FFT_SIZE / 2) % FFT_SIZE;
        
        double I = out[shifted_sam][0];
        double Q = out[shifted_sam][1];

        double magnitude = sqrt(I*I + Q*Q) / FFT_SIZE;
        db[sam] = 20.0 * log10(magnitude + 1e-12) + 100.0;

    }

}

static int parse_positive_double(const char *s, double *out) {

    /*

    Purpose: Parses a positive double value from text

    Return: Parse status

    */
    if (!s || !*s) return 0;

    char *end = NULL;
    double v = strtod(s, &end);

if (end == s || *end != '\0' || v <= 0.0) return 0;

    *out = v;
    return 1;
}

static int parse_nonnegative_int(const char *s, int *out) {

    /*

    Purpose: Parses a nonnegative integer value from text

    Return: Parse status

    */
    if (!s || !*s) return 0;

    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (end == s || *end != '\0' || v < 0 || v > 100000) return 0;

    *out = (int)v;
    return 1;
}

// Normalization Helpers

static int normalize_lna_gain(int gain) {

    /*

    Purpose: Normalizes HackRF LNA gain to a valid step

    Return: Gain value

    */
    if (gain < 0) gain = 0;
    if (gain > 40) gain = 40;
    return (gain / 8) * 8;
}

static int normalize_vga_gain(int gain) {

    /*

    Purpose: Normalizes HackRF VGA gain to a valid step

    Return: Gain value

    */
    if (gain < 0) gain = 0;
    if (gain > 62) gain = 62;
    return (gain / 2) * 2;
}

static int normalize_fps(int fps) {

    /*

    Purpose: Normalizes waterfall frame rate

    Return: FPS value

    */
    if (fps < 1) fps = 1;
    if (fps > 1000) fps = 1000;
    return fps;
}

static int normalize_rows_per_frame(int rows) {

    /*

    Purpose: Normalizes waterfall rows rendered per frame

    Return: Row count

    */
    if (rows < 1) rows = 1;
    if (rows > 64) rows = 64;
    return rows;
}

// Radio Helpers

static int stop_radio(hackrf_device *dev) {

    /*

    Purpose: Stops HackRF receive mode when active

    Return: Stop status

    */
    if (Global_Radio_Running) {
        if (hackrf_stop_rx(dev) != HACKRF_SUCCESS) return 0;
        Global_Radio_Running = 0;
    }

    return 1;
}

static int start_radio(hackrf_device *dev) {

    /*

    Purpose: Starts HackRF receive mode when inactive

    Return: Start status

    */
    if (!Global_Radio_Running) {
        if (hackrf_start_rx(dev, rx_callback, NULL) != HACKRF_SUCCESS) return 0;
        Global_Radio_Running = 1;
    }

    return 1;
}

double recommended_antenna_length_inches(uint64_t freq_hz) {

    /*

    Purpose: Computes quarter-wave antenna length in inches

    Return: Antenna length

    */

    if (freq_hz == 0) return 0.0;

    /*
     * Quarter-wave antenna length:
     *
     * wavelength = c / f
     * quarter-wave = wavelength / 4
     *
     * c ≈ 299,792,458 m/s
     *
     * Return value is in inches
     */

    double wavelength_m = 299792458.0 / (double)freq_hz;
    double quarter_wave_m = wavelength_m / 4.0;

    return quarter_wave_m * 39.37007874;
}

static int apply_radio_settings(hackrf_device *dev, uint64_t Center_Hz, 
                                uint32_t Sample_Rate_Hz, uint32_t Display_Span_Hz, 
                                int LNA_Gain, int VGA_Gain, int Amp_Enable){

    /*

    Purpose: Applies center frequency, sample rate, display span, gain, and amp settings

    Return: Apply status

    */

    if (Global_Rec) stop_recording();
    if (!stop_radio(dev)) return 0;

    ring_clear(&ring_buf);

    if (hackrf_set_sample_rate(dev, Sample_Rate_Hz) != HACKRF_SUCCESS) return 0;
    if (hackrf_set_freq(dev, Center_Hz) != HACKRF_SUCCESS) return 0;    

    uint32_t filter_bw = hackrf_compute_baseband_filter_bw_round_down_lt(Sample_Rate_Hz);

    if (filter_bw > 0) hackrf_set_baseband_filter_bandwidth(dev, filter_bw);

    LNA_Gain = normalize_lna_gain(LNA_Gain);
    VGA_Gain = normalize_vga_gain(VGA_Gain);

    if (hackrf_set_lna_gain(dev, (uint32_t)LNA_Gain) != HACKRF_SUCCESS) return 0;
    if (hackrf_set_vga_gain(dev, (uint32_t)VGA_Gain) != HACKRF_SUCCESS) return 0;
    if (hackrf_set_amp_enable(dev, (uint8_t)(Amp_Enable ? 1 : 0)) != HACKRF_SUCCESS) return 0;

    if (Display_Span_Hz > Sample_Rate_Hz) Display_Span_Hz = Sample_Rate_Hz;
    if (Display_Span_Hz < 1000) Display_Span_Hz = 1000; 
    
    Global_Center_Freq_Hz = Center_Hz;
    Global_Sample_Rate_Hz = Sample_Rate_Hz;
    Global_Display_Span_Hz = Display_Span_Hz;
    Global_LNA_Gain = LNA_Gain;
    Global_VGA_Gain = VGA_Gain;
    Global_Amp_Enable = Amp_Enable ? 1 : 0;

    if (!pre_cache_resize(&Global_Pre_Cache, Global_Sample_Rate_Hz)) {

        set_status("Pre-cache resize failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    if (!rec_queue_resize(&Global_Rec_Queue, Global_Sample_Rate_Hz)) {

        set_status("Record queue resize failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    return start_radio(dev);

}

static int apply_from_inputs(
    hackrf_device *dev,
    Type_Input_Box *freq_box,
    Type_Input_Box *sr_box,
    Type_Input_Box *display_box,
    Type_Input_Box *lna_box,
    Type_Input_Box *vga_box,
    Type_Input_Box *fps_box,
    Type_Input_Box *rows_box,
    uint32_t *waterfall_pixels,
    int tex_w,
    int tex_h
) {

    /*

    Purpose: Parses GUI input boxes and applies radio settings

    Return: Apply status

    */
    double freq_mhz = 0.0;
    double sr_msps = 0.0;
    double display_mhz = 0.0;
    int lna = 0, vga = 0, fps = 0, rows = 0;

    if (!parse_positive_double(freq_box->text, &freq_mhz)) return 0;
    if (!parse_positive_double(sr_box->text, &sr_msps)) return 0;
    if (!parse_positive_double(display_box->text, &display_mhz)) return 0;
    if (!parse_nonnegative_int(lna_box->text, &lna)) return 0;
    if (!parse_nonnegative_int(vga_box->text, &vga)) return 0;
    if (!parse_nonnegative_int(fps_box->text, &fps)) return 0;
    if (!parse_nonnegative_int(rows_box->text, &rows)) return 0;

    uint64_t center_hz = (uint64_t)(freq_mhz * 1e6);
    uint32_t sample_rate_hz = (uint32_t)(sr_msps * 1e6);
    uint32_t display_span_hz = (uint32_t)(display_mhz * 1e6);

    if (sample_rate_hz < 2000000 || sample_rate_hz > 20000000) return 0;

    if (display_span_hz > sample_rate_hz) display_span_hz = sample_rate_hz;
    if (display_span_hz < 1000) display_span_hz = 1000;

    lna = normalize_lna_gain(lna);
    vga = normalize_vga_gain(vga);
    fps = normalize_fps(fps);
    rows = normalize_rows_per_frame(rows);

    if (!apply_radio_settings(dev, center_hz, sample_rate_hz, display_span_hz, lna, vga, Global_Amp_Enable)) {
        return 0;
    }

    Global_Waterfall_FPS = fps;
    Global_Rows_Per_Frame = rows;

    snprintf(freq_box->text, sizeof(freq_box->text), "%.3f", Global_Center_Freq_Hz / 1e6);
    snprintf(sr_box->text, sizeof(sr_box->text), "%.3f", Global_Sample_Rate_Hz / 1e6);
    snprintf(display_box->text, sizeof(display_box->text), "%.3f", Global_Display_Span_Hz / 1e6);
    snprintf(lna_box->text, sizeof(lna_box->text), "%d", Global_LNA_Gain);
    snprintf(vga_box->text, sizeof(vga_box->text), "%d", Global_VGA_Gain);
    snprintf(fps_box->text, sizeof(fps_box->text), "%d", Global_Waterfall_FPS);
    snprintf(rows_box->text, sizeof(rows_box->text), "%d", Global_Rows_Per_Frame);

    clear_waterfall(waterfall_pixels, tex_w, tex_h);
    reset_prev_col_db(tex_w);

    set_status("", (SDL_Color){0, 255, 80, 255});
    return 1;
}


// ==========================
// Main Text Box Cursor Helpers
// ==========================

static int main_field_index(Type_Active_Fields field){

    switch (field) {
        case FIELD_FREQ:    return 0;
        case FIELD_SR:      return 1;
        case FIELD_DISPLAY: return 2;
        case FIELD_LNA:     return 3;
        case FIELD_VGA:     return 4;
        case FIELD_FPS:     return 5;
        case FIELD_ROWS:    return 6;
        default:            return -1;
    }

}

static char *main_field_text_by_index(int index,
                                      Type_Input_Box *freq_box,
                                      Type_Input_Box *sr_box,
                                      Type_Input_Box *display_box,
                                      Type_Input_Box *lna_box,
                                      Type_Input_Box *vga_box,
                                      Type_Input_Box *fps_box,
                                      Type_Input_Box *rows_box,
                                      size_t *text_size){

    if (text_size) *text_size = 0;

    switch (index) {
        case 0:
            if (text_size) *text_size = sizeof(freq_box->text);
            return freq_box->text;
        case 1:
            if (text_size) *text_size = sizeof(sr_box->text);
            return sr_box->text;
        case 2:
            if (text_size) *text_size = sizeof(display_box->text);
            return display_box->text;
        case 3:
            if (text_size) *text_size = sizeof(lna_box->text);
            return lna_box->text;
        case 4:
            if (text_size) *text_size = sizeof(vga_box->text);
            return vga_box->text;
        case 5:
            if (text_size) *text_size = sizeof(fps_box->text);
            return fps_box->text;
        case 6:
            if (text_size) *text_size = sizeof(rows_box->text);
            return rows_box->text;
        default:
            return NULL;
    }

}

static char *main_field_text(Type_Active_Fields field,
                             Type_Input_Box *freq_box,
                             Type_Input_Box *sr_box,
                             Type_Input_Box *display_box,
                             Type_Input_Box *lna_box,
                             Type_Input_Box *vga_box,
                             Type_Input_Box *fps_box,
                             Type_Input_Box *rows_box,
                             size_t *text_size){

    return main_field_text_by_index(main_field_index(field),
                                    freq_box,
                                    sr_box,
                                    display_box,
                                    lna_box,
                                    vga_box,
                                    fps_box,
                                    rows_box,
                                    text_size);

}

static void main_clamp_cursor_for_text(const char *text, int *cursor){

    if (!text || !cursor) return;

    int len = (int)strlen(text);

    if (*cursor < 0) *cursor = 0;
    if (*cursor > len) *cursor = len;

}

static void main_reset_input_cursors(int cursors[7],
                                     Type_Input_Box *freq_box,
                                     Type_Input_Box *sr_box,
                                     Type_Input_Box *display_box,
                                     Type_Input_Box *lna_box,
                                     Type_Input_Box *vga_box,
                                     Type_Input_Box *fps_box,
                                     Type_Input_Box *rows_box){

    Type_Input_Box *boxes[7] = {
        freq_box,
        sr_box,
        display_box,
        lna_box,
        vga_box,
        fps_box,
        rows_box
    };

    for (int i = 0; i < 7; i++) {
        cursors[i] = boxes[i] ? (int)strlen(boxes[i]->text) : 0;
    }

}

static void main_set_active_cursor_end(Type_Active_Fields field,
                                       int cursors[7],
                                       Type_Input_Box *freq_box,
                                       Type_Input_Box *sr_box,
                                       Type_Input_Box *display_box,
                                       Type_Input_Box *lna_box,
                                       Type_Input_Box *vga_box,
                                       Type_Input_Box *fps_box,
                                       Type_Input_Box *rows_box){

    int index = main_field_index(field);

    if (index < 0 || index >= 7) return;

    size_t text_size = 0;
    char *text = main_field_text_by_index(index,
                                          freq_box,
                                          sr_box,
                                          display_box,
                                          lna_box,
                                          vga_box,
                                          fps_box,
                                          rows_box,
                                          &text_size);
    (void)text_size;

    cursors[index] = text ? (int)strlen(text) : 0;

}

static void main_insert_text_at_cursor(char *dst, size_t dst_size, int *cursor, const char *src){

    if (!dst || dst_size == 0 || !cursor || !src) return;

    main_clamp_cursor_for_text(dst, cursor);

    while (*src) {
        char c = *src++;

        if (!((c >= '0' && c <= '9') || c == '.')) continue;

        size_t len = strlen(dst);

        if (len + 1 >= dst_size) break;

        int pos = *cursor;

        if (pos < 0) pos = 0;
        if (pos > (int)len) pos = (int)len;

        memmove(dst + pos + 1, dst + pos, len - (size_t)pos + 1);
        dst[pos] = c;
        *cursor = pos + 1;
    }

}

static void main_backspace_at_cursor(char *dst, int *cursor){

    if (!dst || !cursor) return;

    main_clamp_cursor_for_text(dst, cursor);

    if (*cursor <= 0) return;

    size_t len = strlen(dst);
    int pos = *cursor;

    memmove(dst + pos - 1, dst + pos, len - (size_t)pos + 1);
    *cursor = pos - 1;

}

static void main_move_active_cursor(Type_Active_Fields field,
                                    int cursors[7],
                                    int delta,
                                    Type_Input_Box *freq_box,
                                    Type_Input_Box *sr_box,
                                    Type_Input_Box *display_box,
                                    Type_Input_Box *lna_box,
                                    Type_Input_Box *vga_box,
                                    Type_Input_Box *fps_box,
                                    Type_Input_Box *rows_box){

    int index = main_field_index(field);

    if (index < 0 || index >= 7) return;

    size_t text_size = 0;
    char *text = main_field_text_by_index(index,
                                          freq_box,
                                          sr_box,
                                          display_box,
                                          lna_box,
                                          vga_box,
                                          fps_box,
                                          rows_box,
                                          &text_size);
    (void)text_size;

    if (!text) return;

    cursors[index] += delta;
    main_clamp_cursor_for_text(text, &cursors[index]);

}

static void main_make_cursor_box(Type_Input_Box *dst,
                                 const Type_Input_Box *src,
                                 int active,
                                 int cursor){

    if (!dst || !src) return;

    *dst = *src;

    if (!active) return;

    const char *text = src->text;
    int len = (int)strlen(text);

    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;

    size_t out_size = sizeof(dst->text);
    size_t out = 0;

    for (int i = 0; i < cursor && out + 1 < out_size; i++) {
        dst->text[out++] = text[i];
    }

    if (out + 1 < out_size) {
        dst->text[out++] = '_';
    }

    for (int i = cursor; text[i] && out + 1 < out_size; i++) {
        dst->text[out++] = text[i];
    }

    dst->text[out] = '\0';

}


// =====================
// Command Line Handling
// =====================

static int parse_command_line_args(int argc, char **argv){

    /*

    Purpose: Parses supported command-line arguments

    Return: Parse status

    */

    int output_dir_provided = 0;

    if (argc <= 1) {

        fprintf(stderr, "Usage: %s -o record_dir\n", argv[0]);
        fprintf(stderr, "  -o record_dir   Required directory used to save and scan recordings.\n");
        return 0;

    }

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "-o") == 0) {

            if (i + 1 >= argc) {

                fprintf(stderr, "Missing value for -o record directory.\n");
                fprintf(stderr, "Usage: %s -o record_dir\n", argv[0]);
                return 0;

            }

            snprintf(Global_Record_Dir, sizeof(Global_Record_Dir), "%s", argv[i + 1]);
            output_dir_provided = 1;
            i++;

        }

        else {

            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s -o record_dir\n", argv[0]);
            fprintf(stderr, "  -o record_dir   Required directory used to save and scan recordings.\n");
                return 0;

        }

    }

    if (!output_dir_provided) {

        fprintf(stderr, "Missing required -o record directory.\n");
        fprintf(stderr, "Usage: %s -o record_dir\n", argv[0]);
        fprintf(stderr, "  -o record_dir   Required directory used to save and scan recordings.\n");
        return 0;

    }

    return 1;

}

// ==========
// Main Logic
// ==========

int main(int argc, char **argv){

    /*

    Purpose: Runs the RetroSpectrum application event loop

    Return: Exit status

    */

    signal(SIGINT, handle_sigint);

    if (!parse_command_line_args(argc, argv)) {

        return 1;

    }

    memset(&ring_buf, 0, sizeof(ring_buf));

    pthread_mutex_init(&ring_buf.lock, NULL);

    if (!pre_cache_init(&Global_Pre_Cache, Global_Sample_Rate_Hz)) {

        fprintf(stderr, "pre-cache allocation failed\n");
        return 1;

    }

    if (!rec_queue_init(&Global_Rec_Queue, Global_Sample_Rate_Hz)){
        fprintf(stderr, "record queue allocation failed\n");
        pre_cache_free(&Global_Pre_Cache);
        return 1;
    }

    hackrf_device *dev = NULL;

    if(hackrf_init() != HACKRF_SUCCESS){

        fprintf(stderr, "hackrf_init failed\n");
        return 1;

    }

    if (hackrf_open(&dev) != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_open failed\n");
        hackrf_exit();
        return 1;
    }

    if (!apply_radio_settings(dev, Global_Center_Freq_Hz, Global_Sample_Rate_Hz, 
                              Global_Display_Span_Hz, Global_LNA_Gain, Global_VGA_Gain, 
                              Global_Amp_Enable)) {

        fprintf(stderr, "initial HackRF configuration failed\n");
        hackrf_close(dev);
        hackrf_exit();
        return 1;
    }

    fftw_complex *time_domain = fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    fftw_complex *freq_domain = fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);

    double *hann_window = malloc(sizeof(double) * FFT_SIZE);
    double *db = malloc(sizeof(double) * FFT_SIZE);

    if (!time_domain || !freq_domain || !hann_window || !db) {
        fprintf(stderr, "allocation failed\n");
        stop_radio(dev);
        hackrf_close(dev);
        hackrf_exit();
        return 1;
    }

    for (int n = 0; n < FFT_SIZE; n++){
        hann_window[n] = 0.5 - 0.5 * cos((2.0 * M_PI * n) / (FFT_SIZE - 1));
    }

    fftw_plan plan = fftw_plan_dft_1d(FFT_SIZE, time_domain, freq_domain, FFTW_FORWARD, FFTW_MEASURE);

    if (!plan){

        fprintf(stderr, "fftw plan creation failed\n");
        stop_radio(dev);
        hackrf_close(dev);
        hackrf_exit();
        return 1;
    
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return 1;
    }

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init PNG warning: %s\n", IMG_GetError());
    }

    SDL_Window *window_sdl = SDL_CreateWindow(
        "HackRF",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1400,
        820,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window_sdl) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetWindowMinimumSize(window_sdl, MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

    SDL_Renderer *renderer = SDL_CreateRenderer(window_sdl, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }

    int tex_w = 1120;
    int tex_h = 540;

    SDL_Texture *waterfall_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        tex_w,
        tex_h
    );

    if (!waterfall_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 1;
    }

    uint32_t *pixels = malloc(sizeof(uint32_t) * tex_w * tex_h);
    if (!pixels) {
        fprintf(stderr, "pixel allocation failed\n");
        return 1;
    }

    clear_waterfall(pixels, tex_w, tex_h);

    Global_Color_Baseline = malloc(sizeof(double) * tex_w);
    if (!Global_Color_Baseline) {
        fprintf(stderr, "prev-column allocation failed\n");
        return 1;
    }

    reset_prev_col_db(tex_w);

    TTF_Font *font_small = load_font(14);
    TTF_Font *font_medium = load_font(16);

    SDL_StartTextInput();

    Type_Dashboard_State dashboard;

    if (!dashboard_init(&dashboard, "world_map.bin")) {

        set_status("Dashboard map not loaded: world_map.bin",
                   (SDL_Color){255, 180, 40, 255});

    }

    Type_Input_Box freq_box = {.label = "Center MHz", .id = FIELD_FREQ};
    snprintf(freq_box.text, sizeof(freq_box.text), "%.3f", Global_Center_Freq_Hz / 1e6);

    Type_Input_Box sr_box = {.label = "Sample MS/s", .id = FIELD_SR};
    snprintf(sr_box.text, sizeof(sr_box.text), "%.3f", Global_Sample_Rate_Hz / 1e6);

    Type_Input_Box display_box = {.label = "Display MHz", .id = FIELD_DISPLAY};
    snprintf(display_box.text, sizeof(display_box.text), "%.3f", Global_Display_Span_Hz / 1e6);

    Type_Input_Box lna_box = {.label = "LNA", .id = FIELD_LNA};
    snprintf(lna_box.text, sizeof(lna_box.text), "%d", Global_LNA_Gain);

    Type_Input_Box vga_box = {.label = "VGA", .id = FIELD_VGA};
    snprintf(vga_box.text, sizeof(vga_box.text), "%d", Global_VGA_Gain);

    Type_Input_Box fps_box = {.label = "FPS", .id = FIELD_FPS};
    snprintf(fps_box.text, sizeof(fps_box.text), "%d", Global_Waterfall_FPS);

    Type_Input_Box rows_box = {.label = "Rows/Frame", .id = FIELD_ROWS};
    snprintf(rows_box.text, sizeof(rows_box.text), "%d", Global_Rows_Per_Frame);

    Type_Active_Fields active = FIELD_NONE;
    int main_input_cursors[7];

    main_reset_input_cursors(main_input_cursors,
                             &freq_box,
                             &sr_box,
                             &display_box,
                             &lna_box,
                             &vga_box,
                             &fps_box,
                             &rows_box);

    uint64_t next_waterfall_ms = SDL_GetTicks64();


    while (Global_Running) {
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(window_sdl, &win_w, &win_h);

        int station_win_h = win_h - RETROSPECTRUM_DASHBOARD_TAB_BAR_H;
        if (station_win_h < 240) station_win_h = 240;

        SDL_Rect amp_box;
        SDL_Rect dc_box;
        SDL_Rect cache_box;
        SDL_Rect sel_button;
        SDL_Rect rec_button;

        layout_controls(
            win_w,
            &freq_box,
            &sr_box,
            &display_box,
            &lna_box,
            &vga_box,
            &fps_box,
            &rows_box,
            &amp_box,
            &dc_box,
            &sel_button,
            &rec_button
        );

        cache_box = amp_box;
        amp_box.y = 12;
        cache_box.y = 40;
        dc_box.y = 68;

        int waterfall_x = MARGIN;
        int waterfall_y = CONTROL_PANEL_HEIGHT + 12;
        int waterfall_w = win_w - 2 * MARGIN;
        int waterfall_h = station_win_h - waterfall_y - AXIS_HEIGHT - 25;

        if (waterfall_h < 100) waterfall_h = 100;

        SDL_Rect waterfall_rect = {waterfall_x, waterfall_y, waterfall_w, waterfall_h};

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) Global_Running = 0;

            int text_entry_active = (active != FIELD_NONE) ||
                                    (dashboard.enabled && (dashboard.case_desc_editing || dashboard.case_search_active)) ||
                                    (Global_Classification_Mode && CLASSIFICATION_is_text_entry_active()) ||
                                    (Global_Analysis_Mode && ANALYSIS_is_text_entry_active());

            int top_tab_event = dashboard_handle_top_tab_event(&dashboard,
                                                               &event,
                                                               win_w,
                                                               text_entry_active);

            if (top_tab_event != DASHBOARD_EVENT_NONE) {
                if (top_tab_event == DASHBOARD_EVENT_MAP) {
                    if (Global_Classification_Mode) CLASSIFICATION_exit_mode();
                    if (Global_Analysis_Mode) {
                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();
                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 1;
                    dashboard.current_tab = DASHBOARD_EVENT_MAP;
                    set_status("Dashboard", (SDL_Color){0, 255, 80, 255});
                }
                else if (top_tab_event == DASHBOARD_EVENT_RETROSPECTRUM) {
                    if (Global_Classification_Mode) CLASSIFICATION_exit_mode();
                    if (Global_Analysis_Mode) {
                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();
                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_RETROSPECTRUM;
                    set_status("RetroSpectrum Workstation", (SDL_Color){0, 255, 80, 255});
                }
                else if (top_tab_event == DASHBOARD_EVENT_ANALYSIS) {
                    if (Global_Classification_Mode) CLASSIFICATION_exit_mode();
                    if (!Global_Analysis_Mode) {
                        ANALYSIS_enter_mode(Global_Record_Dir,
                                            Global_Center_Freq_Hz,
                                            Global_Rec_Out_Rate_Hz,
                                            Global_Sample_Rate_Hz);
                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_ANALYSIS;
                }
                else if (top_tab_event == DASHBOARD_EVENT_CLASSIFICATION) {
                    if (Global_Analysis_Mode) {
                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();
                    }
                    if (!Global_Classification_Mode) {
                        CLASSIFICATION_enter_mode(Global_Record_Dir);
                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_CLASSIFICATION;
                    set_status("Classification Workstation", (SDL_Color){0, 255, 80, 255});
                }
                continue;
            }

            if (!dashboard.enabled) {
                if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                    if (event.button.y < RETROSPECTRUM_DASHBOARD_TAB_BAR_H) continue;
                    event.button.y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;
                }
                else if (event.type == SDL_MOUSEMOTION) {
                    if (event.motion.y < RETROSPECTRUM_DASHBOARD_TAB_BAR_H) continue;
                    event.motion.y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;
                }
            }

            if (dashboard.enabled) {

                int dashboard_event_result = dashboard_handle_event(&dashboard,
                                                                    &event,
                                                                    win_w,
                                                                    win_h);

                if (dashboard_event_result == DASHBOARD_EVENT_QUIT) {

                    Global_Running = 0;

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_RETROSPECTRUM) {

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_RETROSPECTRUM;
                    Global_Analysis_Mode = 0;
                    Global_Classification_Mode = 0;
                    set_status("RetroSpectrum Workstation",
                               (SDL_Color){0, 255, 80, 255});

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_ANALYSIS) {

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_ANALYSIS;
                    Global_Classification_Mode = 0;

                    ANALYSIS_enter_mode(Global_Record_Dir,
                                        Global_Center_Freq_Hz,
                                        Global_Rec_Out_Rate_Hz,
                                        Global_Sample_Rate_Hz);

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_CLASSIFICATION) {

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_CLASSIFICATION;
                    Global_Analysis_Mode = 0;
                    CLASSIFICATION_enter_mode(Global_Record_Dir);
                    set_status("Classification Workstation",
                               (SDL_Color){0, 255, 80, 255});

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_MAP) {

                    dashboard.current_tab = DASHBOARD_EVENT_MAP;

                }

                continue;

            }

            if (Global_Classification_Mode) {

                if (event.type == SDL_KEYDOWN &&
                    event.key.keysym.sym == SDLK_d &&
                    (SDL_GetModState() & KMOD_CTRL)) {

                    CLASSIFICATION_exit_mode();
                    dashboard.enabled = 1;
                    dashboard.current_tab = DASHBOARD_EVENT_MAP;
                    set_status("Dashboard", (SDL_Color){0, 255, 80, 255});
                    continue;

                }

                int classification_event_result = CLASSIFICATION_handle_event(&event, win_w, station_win_h);

                if (classification_event_result == 2) {

                    CLASSIFICATION_exit_mode();
                    ANALYSIS_enter_mode(Global_Record_Dir,
                                        Global_Center_Freq_Hz,
                                        Global_Rec_Out_Rate_Hz,
                                        Global_Sample_Rate_Hz);

                }

                continue;

            }

            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;

                if (key == SDLK_d &&
                    active == FIELD_NONE &&
                    (SDL_GetModState() & KMOD_CTRL)) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    dashboard.enabled = 1;
                    dashboard.current_tab = DASHBOARD_EVENT_MAP;
                    set_status("Dashboard", (SDL_Color){0, 255, 80, 255});
                    continue;

                }

                if (Global_Analysis_Mode) {

                    int analysis_event_result = ANALYSIS_handle_event(&event,
                                                                      win_w,
                                                                      station_win_h,
                                                                      pixels,
                                                                      tex_w,
                                                                      tex_h,
                                                                      waterfall_texture,
                                                                      &next_waterfall_ms,
                                                                      &active);

                    if (analysis_event_result == ANALYSIS_EVENT_QUIT) {

                        Global_Running = 0;

                    }

                    if (analysis_event_result != ANALYSIS_EVENT_IGNORED) {

                        continue;

                    }

                }

                if (key == SDLK_g && active == FIELD_NONE) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    else {

                        ANALYSIS_enter_mode(Global_Record_Dir,
                                            Global_Center_Freq_Hz,
                                            Global_Rec_Out_Rate_Hz,
                                            Global_Sample_Rate_Hz);

                    }

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_ANALYSIS;

                    continue;

                }

                if (key == SDLK_h && active == FIELD_NONE) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();
                        dashboard.enabled = 0;
                        dashboard.current_tab = DASHBOARD_EVENT_RETROSPECTRUM;
                        set_status("", (SDL_Color){0, 255, 80, 255});

                    }

                    else {

                        if (Global_Analysis_Mode) {

                            ANALYSIS_exit_mode(pixels,
                                               tex_w,
                                               tex_h,
                                               waterfall_texture);
                            next_waterfall_ms = SDL_GetTicks64();

                        }

                        CLASSIFICATION_enter_mode(Global_Record_Dir);
                        dashboard.enabled = 0;
                        dashboard.current_tab = DASHBOARD_EVENT_CLASSIFICATION;

                        set_status("Classification Workstation",
                                   (SDL_Color){0, 255, 80, 255});

                    }

                    continue;

                }

                if (key == SDLK_c &&
                    active == FIELD_NONE &&
                    Global_Analysis_Mode &&
                    !(SDL_GetModState() & KMOD_CTRL)) {

                    char export_file_name[512];
                    double export_frequency_mhz = 0.0;
                    double export_bandwidth_khz = 0.0;
                    double export_start_time = 0.0;
                    double export_end_time = 0.0;

                    if (ANALYSIS_export_classification_fields(export_file_name,
                                                              sizeof(export_file_name),
                                                              &export_frequency_mhz,
                                                              &export_bandwidth_khz,
                                                              &export_start_time,
                                                              &export_end_time)) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                        CLASSIFICATION_enter_mode(Global_Record_Dir);
                        dashboard.enabled = 0;
                        dashboard.current_tab = DASHBOARD_EVENT_CLASSIFICATION;
                        CLASSIFICATION_prefill_from_analysis_selection(export_file_name,
                                                                       export_frequency_mhz,
                                                                       export_bandwidth_khz,
                                                                       export_start_time,
                                                                       export_end_time);

                        set_status("Classification Workstation",
                                   (SDL_Color){0, 255, 80, 255});

                    }

                    continue;

                }

                if (Global_Analysis_Mode) {

                    int analysis_event_result = ANALYSIS_handle_event(&event,
                                                                      win_w,
                                                                      station_win_h,
                                                                      pixels,
                                                                      tex_w,
                                                                      tex_h,
                                                                      waterfall_texture,
                                                                      &next_waterfall_ms,
                                                                      &active);

                    if (analysis_event_result == ANALYSIS_EVENT_QUIT) {

                        Global_Running = 0;

                    }

                    if (analysis_event_result != ANALYSIS_EVENT_IGNORED) {

                        continue;

                    }

                }

                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    if (active != FIELD_NONE) active = FIELD_NONE;
                    else Global_Running = 0;
                } else if (event.key.keysym.sym == SDLK_TAB) {
                    if (active == FIELD_NONE) active = FIELD_FREQ;
                    else if (active == FIELD_FREQ) active = FIELD_SR;
                    else if (active == FIELD_SR) active = FIELD_DISPLAY;
                    else if (active == FIELD_DISPLAY) active = FIELD_LNA;
                    else if (active == FIELD_LNA) active = FIELD_VGA;
                    else if (active == FIELD_VGA) active = FIELD_FPS;
                    else if (active == FIELD_FPS) active = FIELD_ROWS;
                    else active = FIELD_FREQ;

                    main_set_active_cursor_end(active,
                                               main_input_cursors,
                                               &freq_box,
                                               &sr_box,
                                               &display_box,
                                               &lna_box,
                                               &vga_box,
                                               &fps_box,
                                               &rows_box);
                } else if (event.key.keysym.sym == SDLK_LEFT && active != FIELD_NONE) {
                    main_move_active_cursor(active,
                                            main_input_cursors,
                                            -1,
                                            &freq_box,
                                            &sr_box,
                                            &display_box,
                                            &lna_box,
                                            &vga_box,
                                            &fps_box,
                                            &rows_box);
                } else if (event.key.keysym.sym == SDLK_RIGHT && active != FIELD_NONE) {
                    main_move_active_cursor(active,
                                            main_input_cursors,
                                            1,
                                            &freq_box,
                                            &sr_box,
                                            &display_box,
                                            &lna_box,
                                            &vga_box,
                                            &fps_box,
                                            &rows_box);
                } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    size_t text_size = 0;
                    int index = main_field_index(active);
                    char *text = main_field_text(active,
                                                 &freq_box,
                                                 &sr_box,
                                                 &display_box,
                                                 &lna_box,
                                                 &vga_box,
                                                 &fps_box,
                                                 &rows_box,
                                                 &text_size);
                    (void)text_size;

                    if (index >= 0 && index < 7 && text) {
                        main_backspace_at_cursor(text, &main_input_cursors[index]);
                    }
                } else if (
                    event.key.keysym.sym == SDLK_RETURN ||
                    event.key.keysym.sym == SDLK_KP_ENTER
                ) {
                    apply_from_inputs(
                        dev,
                        &freq_box,
                        &sr_box,
                        &display_box,
                        &lna_box,
                        &vga_box,
                        &fps_box,
                        &rows_box,
                        pixels,
                        tex_w,
                        tex_h
                    );

                    main_reset_input_cursors(main_input_cursors,
                                             &freq_box,
                                             &sr_box,
                                             &display_box,
                                             &lna_box,
                                             &vga_box,
                                             &fps_box,
                                             &rows_box);

                    next_waterfall_ms = SDL_GetTicks64();
                } else if (event.key.keysym.sym == SDLK_q && active == FIELD_NONE) {
                    Global_Running = 0;
                }
            }

            if (Global_Analysis_Mode && event.type != SDL_KEYDOWN) {

                int analysis_event_result = ANALYSIS_handle_event(&event,
                                                                  win_w,
                                                                  station_win_h,
                                                                  pixels,
                                                                  tex_w,
                                                                  tex_h,
                                                                  waterfall_texture,
                                                                  &next_waterfall_ms,
                                                                  &active);

                if (analysis_event_result == ANALYSIS_EVENT_QUIT) {

                    Global_Running = 0;

                }

                if (analysis_event_result != ANALYSIS_EVENT_IGNORED) {

                    continue;

                }

            }

            if (event.type == SDL_TEXTINPUT) {

                size_t text_size = 0;
                int index = main_field_index(active);
                char *text = main_field_text(active,
                                             &freq_box,
                                             &sr_box,
                                             &display_box,
                                             &lna_box,
                                             &vga_box,
                                             &fps_box,
                                             &rows_box,
                                             &text_size);

                if (index >= 0 && index < 7 && text) {
                    main_insert_text_at_cursor(text,
                                               text_size,
                                               &main_input_cursors[index],
                                               event.text.text);
                }

            }

            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int x = event.button.x;
                int y = event.button.y;

                if (event.button.clicks == 3 && y < CONTROL_PANEL_HEIGHT) {
              
                    active = FIELD_NONE;
                    toggle_fullscreen(window_sdl);
                    continue;
                }

                if (point_in_rect(x, y, freq_box.rect)) {
                    active = FIELD_FREQ;
                    main_set_active_cursor_end(active,
                                               main_input_cursors,
                                               &freq_box,
                                               &sr_box,
                                               &display_box,
                                               &lna_box,
                                               &vga_box,
                                               &fps_box,
                                               &rows_box);
                }
                else if (point_in_rect(x, y, sr_box.rect)) {
                    active = FIELD_SR;
                    main_set_active_cursor_end(active,
                                               main_input_cursors,
                                               &freq_box,
                                               &sr_box,
                                               &display_box,
                                               &lna_box,
                                               &vga_box,
                                               &fps_box,
                                               &rows_box);
                }
                else if (point_in_rect(x, y, display_box.rect)) {
                    active = FIELD_DISPLAY;
                    main_set_active_cursor_end(active,
                                               main_input_cursors,
                                               &freq_box,
                                               &sr_box,
                                               &display_box,
                                               &lna_box,
                                               &vga_box,
                                               &fps_box,
                                               &rows_box);
                }
                else if (point_in_rect(x, y, lna_box.rect)) {
                    active = FIELD_LNA;
                    main_set_active_cursor_end(active,
                                               main_input_cursors,
                                               &freq_box,
                                               &sr_box,
                                               &display_box,
                                               &lna_box,
                                               &vga_box,
                                               &fps_box,
                                               &rows_box);
                }
                else if (point_in_rect(x, y, vga_box.rect)) {
                    active = FIELD_VGA;
                    main_set_active_cursor_end(active,
                                               main_input_cursors,
                                               &freq_box,
                                               &sr_box,
                                               &display_box,
                                               &lna_box,
                                               &vga_box,
                                               &fps_box,
                                               &rows_box);
                }
                else if (point_in_rect(x, y, fps_box.rect)) {
                    active = FIELD_FPS;
                    main_set_active_cursor_end(active,
                                               main_input_cursors,
                                               &freq_box,
                                               &sr_box,
                                               &display_box,
                                               &lna_box,
                                               &vga_box,
                                               &fps_box,
                                               &rows_box);
                }
                else if (point_in_rect(x, y, rows_box.rect)) {
                    active = FIELD_ROWS;
                    main_set_active_cursor_end(active,
                                               main_input_cursors,
                                               &freq_box,
                                               &sr_box,
                                               &display_box,
                                               &lna_box,
                                               &vga_box,
                                               &fps_box,
                                               &rows_box);
                }
                else if (point_in_rect(x, y, cache_box)) {
                    active = FIELD_NONE;
                    Global_Cached_Recording = !Global_Cached_Recording;

                    set_status(Global_Cached_Recording ? "Cached recording enabled" : "Cached recording disabled",
                               Global_Cached_Recording ?
                               (SDL_Color){0, 255, 90, 255} :
                               (SDL_Color){150, 150, 150, 255});
                }
                else if (point_in_rect(x, y, amp_box)) {
                    Global_Amp_Enable = !Global_Amp_Enable;

                    apply_from_inputs(
                        dev,
                        &freq_box,
                        &sr_box,
                        &display_box,
                        &lna_box,
                        &vga_box,
                        &fps_box,
                        &rows_box,
                        pixels,
                        tex_w,
                        tex_h
                    );

                    main_reset_input_cursors(main_input_cursors,
                                             &freq_box,
                                             &sr_box,
                                             &display_box,
                                             &lna_box,
                                             &vga_box,
                                             &fps_box,
                                             &rows_box);

                    next_waterfall_ms = SDL_GetTicks64();
                } 

                else if (point_in_rect(x, y, dc_box)) {
                    Global_DC_Enable = !Global_DC_Enable;
                    Global_DC_I = 0.0;
                    Global_DC_Q = 0.0;
                } 

                else if (point_in_rect(x, y, sel_button)) {
                    active = FIELD_NONE;

                    if (!Global_Rec) {
                        Global_Selector.enabled = !Global_Selector.enabled;
                    } else {
                        set_status("Selector locked while recording", (SDL_Color){255, 180, 40, 255});
                    }
                }

                else if (point_in_rect(x, y, rec_button)) {
                    active = FIELD_NONE;

                    if (Global_Rec) {
                        stop_recording();
                    } else if (Global_Selector.enabled) {
                        start_recording();
                    }
                } 

                else if (!Global_Rec && Global_Selector.enabled && point_in_rect(x, y, waterfall_rect)) {
                    active = FIELD_NONE;

                    int x0 = waterfall_rect.x + (int)(Global_Selector.X0 * waterfall_rect.w);
                    int x1 = waterfall_rect.x + (int)(Global_Selector.X1 * waterfall_rect.w);

                    if (near_px(x, x0, 8)) {
                        Global_Selector.resizing_left = 1;
                    } 
                    
                    else if (near_px(x, x1, 8)) {
                        Global_Selector.resizing_right = 1;
                    } 

                    else if (x > x0 && x < x1) {
                        Global_Selector.dragging = 1;
                    }
                } 

                else {
                    active = FIELD_NONE;
                }
            }

            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {

                Global_Selector.dragging = 0;
                Global_Selector.resizing_left = 0;
                Global_Selector.resizing_right = 0;

            }

            if (event.type == SDL_MOUSEMOTION) {

              if (!Global_Rec && Global_Selector.enabled && (Global_Selector.dragging || Global_Selector.resizing_left || 
                    Global_Selector.resizing_right)) {

                  update_selection_from_mouse(event.motion.x, waterfall_rect);
                }

            }

        }

        uint64_t now_ms = SDL_GetTicks64();
        uint64_t frame_interval_ms = 1000 / (uint64_t)normalize_fps(Global_Waterfall_FPS);

        if (frame_interval_ms < 1) frame_interval_ms = 1;

        if (!dashboard.enabled && !Global_Analysis_Mode && !Global_Classification_Mode && now_ms >= next_waterfall_ms) {

            int rows_drawn = 0;
            int target_rows = normalize_rows_per_frame(Global_Rows_Per_Frame);

            while (rows_drawn < target_rows && ring_read_block(&ring_buf, time_domain, hann_window)) {
                fftw_execute(plan);
                compute_DB_from_FFT(freq_domain, db);
                add_fft_line_to_waterfall(pixels, tex_w, tex_h, db);
                rows_drawn++;
            }

            if (rows_drawn > 0) {

                SDL_UpdateTexture(waterfall_texture, NULL, pixels, tex_w * sizeof(uint32_t));

            }

            next_waterfall_ms = now_ms + frame_interval_ms;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderSetViewport(renderer, NULL);

        if (!dashboard.enabled) {
            SDL_Rect station_viewport = {0,
                                         RETROSPECTRUM_DASHBOARD_TAB_BAR_H,
                                         win_w,
                                         station_win_h};
            SDL_RenderSetViewport(renderer, &station_viewport);
        }

        if (!dashboard.enabled && !Global_Analysis_Mode && !Global_Classification_Mode) {

            Type_Input_Box draw_freq_box;
            Type_Input_Box draw_sr_box;
            Type_Input_Box draw_display_box;
            Type_Input_Box draw_lna_box;
            Type_Input_Box draw_vga_box;
            Type_Input_Box draw_fps_box;
            Type_Input_Box draw_rows_box;

            main_make_cursor_box(&draw_freq_box,
                                 &freq_box,
                                 active == FIELD_FREQ,
                                 main_input_cursors[0]);
            main_make_cursor_box(&draw_sr_box,
                                 &sr_box,
                                 active == FIELD_SR,
                                 main_input_cursors[1]);
            main_make_cursor_box(&draw_display_box,
                                 &display_box,
                                 active == FIELD_DISPLAY,
                                 main_input_cursors[2]);
            main_make_cursor_box(&draw_lna_box,
                                 &lna_box,
                                 active == FIELD_LNA,
                                 main_input_cursors[3]);
            main_make_cursor_box(&draw_vga_box,
                                 &vga_box,
                                 active == FIELD_VGA,
                                 main_input_cursors[4]);
            main_make_cursor_box(&draw_fps_box,
                                 &fps_box,
                                 active == FIELD_FPS,
                                 main_input_cursors[5]);
            main_make_cursor_box(&draw_rows_box,
                                 &rows_box,
                                 active == FIELD_ROWS,
                                 main_input_cursors[6]);

            draw_control_panel(
                renderer,
                font_medium,
                win_w,
                &draw_freq_box,
                &draw_sr_box,
                &draw_display_box,
                &draw_lna_box,
                &draw_vga_box,
                &draw_fps_box,
                &draw_rows_box,
                amp_box,
                dc_box,
                sel_button,
                rec_button,
                active
            );

            draw_checkbox(renderer, font_medium, cache_box, "Cache 5 sec", Global_Cached_Recording);

        }

        if (dashboard.enabled) {

            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);

            dashboard_draw(&dashboard,
                           renderer,
                           font_small,
                           font_medium,
                           win_w,
                           win_h,
                           mouse_x,
                           mouse_y);

        }

        else if (Global_Analysis_Mode) {

            ANALYSIS_draw_workstation(renderer,
                                      font_small,
                                      waterfall_texture,
                                      pixels,
                                      tex_w,
                                      tex_h,
                                      win_w,
                                      station_win_h);

            ANALYSIS_draw_workstation_overlays(renderer,
                                               font_small,
                                               waterfall_texture,
                                               tex_w,
                                               tex_h,
                                               win_w,
                                               station_win_h);

        }

        else if (Global_Classification_Mode) {

            CLASSIFICATION_draw_workstation(renderer,
                                            font_small,
                                            win_w,
                                            station_win_h);

        }

        else {

            SDL_RenderCopy(renderer, waterfall_texture, NULL, &waterfall_rect);
            draw_selection_overlay(renderer, waterfall_rect);
            draw_selector_bandwidth(renderer, font_small, waterfall_rect);
            draw_border(renderer, waterfall_rect);
            draw_frequency_axis(renderer, font_small, waterfall_rect);

        }

        if (!dashboard.enabled && !Global_Analysis_Mode && !Global_Classification_Mode) {

            int status_w = 0;
            int status_h = 0;

            if (font_medium &&
                TTF_SizeText(font_medium, Global_Status_Msg, &status_w, &status_h) != 0) {

                status_w = 0;
                status_h = 0;

            }

            draw_text(renderer,
                      font_medium,
                      Global_Status_Msg,
                      (win_w - status_w) / 2,
                      station_win_h - 36,
                      Global_Status_Color);

        }

        if (!dashboard.enabled && !Global_Analysis_Mode && !Global_Classification_Mode) {

            draw_antenna_recommendation(renderer, font_small, win_w, station_win_h);

            draw_made_in_usa(renderer, font_medium, win_w, station_win_h);

        }

        SDL_RenderSetViewport(renderer, NULL);

        int tab_mouse_x = 0;
        int tab_mouse_y = 0;
        SDL_GetMouseState(&tab_mouse_x, &tab_mouse_y);
        dashboard_draw_top_bar(renderer,
                               font_small,
                               font_medium,
                               win_w,
                               tab_mouse_x,
                               tab_mouse_y,
                               dashboard.current_tab);

        SDL_RenderPresent(renderer);

        SDL_Delay(1);
    }

    SDL_StopTextInput();

    dashboard_shutdown();

    stop_recording();

    if (Global_Radio_Running) hackrf_stop_rx(dev);

    hackrf_close(dev);
    hackrf_exit();

    if (font_small) TTF_CloseFont(font_small);
    if (font_medium) TTF_CloseFont(font_medium);

    SDL_DestroyTexture(waterfall_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window_sdl);

    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    fftw_destroy_plan(plan);
    fftw_free(time_domain);
    fftw_free(freq_domain);

    free(hann_window);
    free(db);
    free(pixels);
    free(Global_Color_Baseline);

    pre_cache_free(&Global_Pre_Cache);
    rec_queue_free(&Global_Rec_Queue);
    pthread_mutex_destroy(&ring_buf.lock);

    return 0;

}

