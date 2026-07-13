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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

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
#define DASHBOARD_MAX_CASE_POINTS 4096
#define DASHBOARD_CASE_DIR "Classification"
#define DASHBOARD_CASE_DESCRIPTION_CSV "Classification/CASE_DESCRIPTIONS.csv"

static Type_Dashboard_Case_Info Global_Dashboard_Cases[DASHBOARD_MAX_CASES];
static Type_Dashboard_Case_Point Global_Dashboard_Case_Points[DASHBOARD_MAX_CASE_POINTS];
static int Global_Dashboard_Case_Count = 0;
static int Global_Dashboard_Case_Point_Count = 0;
static int Global_Dashboard_Case_Desc_Cursor = 0;
static int Global_Dashboard_Case_Desc_Selecting = 0;
static int Global_Dashboard_Case_Desc_Selection_Start = -1;
static int Global_Dashboard_Case_Desc_Selection_End = -1;
static TTF_Font *Global_Dashboard_Case_Desc_Font = NULL;
static int Global_Dashboard_Case_Desc_Wrap_Px = 0;

#define DASHBOARD_MARGIN 20
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#define DASHBOARD_TOP_H RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define DASHBOARD_TAB_H 42
#define DASHBOARD_TAB_GAP 10
#define DASHBOARD_TAB_COUNT 6
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

static int dashboard_point_in_rect(int x, int y, SDL_Rect r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void dashboard_copy_text(char *dst, size_t dst_size, const char *src) {
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
    SDL_Rect rect = {0, 0, win_w, DASHBOARD_TOP_H};
    return rect;
}

static SDL_Rect dashboard_content_rect(int win_w, int win_h) {
    SDL_Rect rect = {DASHBOARD_MARGIN, DASHBOARD_TOP_H + DASHBOARD_MARGIN, win_w - 2 * DASHBOARD_MARGIN,
                     win_h - DASHBOARD_TOP_H - 2 * DASHBOARD_MARGIN - 40};

    if (rect.h < DASHBOARD_MIN_MAP_H) {
        rect.h = DASHBOARD_MIN_MAP_H;
    }
    return rect;
}

static void dashboard_make_tabs(int win_w, Type_Dashboard_Tab tabs[DASHBOARD_TAB_COUNT]) {
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
}

void dashboard_draw_tab(SDL_Renderer *renderer, TTF_Font *font, Type_Dashboard_Tab tab, int active, int hovered) {
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
    draw_filled_rect(renderer, rect, (SDL_Color){0, 10, 4, 240});
    draw_outline_rect(renderer, rect, accent);

    SDL_Rect stripe = {rect.x, rect.y, 5, rect.h};
    draw_filled_rect(renderer, stripe, accent);

    draw_text(renderer, font_medium, title, rect.x + 16, rect.y + 12, Dashboard_Text);
    draw_text(renderer, font_small, body, rect.x + 16, rect.y + 42, Dashboard_Muted);
}

static unsigned int dashboard_hash_string(const char *text) {
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

static int dashboard_case_index_for(const char *case_number) {
    if (!case_number || !case_number[0]) {
        case_number = "UNCASED";
    }

    for (int i = 0; i < Global_Dashboard_Case_Count; i++) {
        if (strcmp(Global_Dashboard_Cases[i].case_number, case_number) == 0) {
            return i;
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
        } else {
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

static int dashboard_file_has_suffix(const char *name, const char *suffix) {
    if (!name || !suffix) {
        return 0;
    }
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

static void dashboard_unescape_multiline_text(char *text) {
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
        } else if (r[0] == '\\' && r[1] == 'r') {
            r += 2;
        } else {
            *w++ = *r++;
        }
    }

    *w = '\0';
}

static void dashboard_load_case_descriptions(void) {
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
            int idx = dashboard_case_index_for(fields[0]);
            dashboard_unescape_multiline_text(fields[1]);
            if (idx >= 0) {
                snprintf(Global_Dashboard_Cases[idx].description, sizeof(Global_Dashboard_Cases[idx].description), "%s",
                         fields[1]);
            }
        }
    }

    fclose(fp);
}

static void dashboard_csv_escape(FILE *fp, const char *text) {
    fputc('"', fp);
    if (text) {
        for (const char *p = text; *p; p++) {
            if (*p == '"') {
                fputc('"', fp);
                fputc('"', fp);
            } else if (*p == '\n') {
                fputc('\\', fp);
                fputc('n', fp);
            } else if (*p == '\r') {
                fputc('\\', fp);
                fputc('r', fp);
            } else {
                fputc(*p, fp);
            }
        }
    }
    fputc('"', fp);
}

static void dashboard_save_case_descriptions(void) {
    struct stat st;
    if (stat(DASHBOARD_CASE_DIR, &st) != 0) {
        mkdir(DASHBOARD_CASE_DIR, 0755);
    }

    FILE *fp = fopen(DASHBOARD_CASE_DESCRIPTION_CSV, "w");
    if (!fp) {
        return;
    }

    fprintf(fp, "case_number,description\n");
    for (int i = 0; i < Global_Dashboard_Case_Count; i++) {
        dashboard_csv_escape(fp, Global_Dashboard_Cases[i].case_number);
        fputc(',', fp);
        dashboard_csv_escape(fp, Global_Dashboard_Cases[i].description);
        fputc('\n', fp);
    }

    fclose(fp);
}

static void dashboard_load_case_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    char line[4096];
    int first = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (first) {
            first = 0;
            if (strstr(line, "case_number") && strstr(line, "latitude")) {
                continue;
            }
        }

        char fields[16][512];
        memset(fields, 0, sizeof(fields));
        int count = dashboard_csv_parse_line(line, fields, 16);
        if (count < 11) {
            continue;
        }

        double lat = strtod(fields[9], NULL);
        double lon = strtod(fields[10], NULL);

        if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
            continue;
        }
        if (Global_Dashboard_Case_Point_Count >= DASHBOARD_MAX_CASE_POINTS) {
            break;
        }

        int case_index = dashboard_case_index_for(fields[0]);
        if (case_index < 0) {
            continue;
        }

        Type_Dashboard_Case_Point *pt = &Global_Dashboard_Case_Points[Global_Dashboard_Case_Point_Count++];
        memset(pt, 0, sizeof(*pt));
        pt->case_index = case_index;
        dashboard_copy_text(pt->signal_name, sizeof(pt->signal_name), fields[1]);
        dashboard_copy_text(pt->country, sizeof(pt->country), fields[8]);
        dashboard_copy_text(pt->notes, sizeof(pt->notes), fields[11]);
        pt->latitude = lat;
        pt->longitude = lon;
        Global_Dashboard_Cases[case_index].point_count++;
    }

    fclose(fp);
}

static void dashboard_reload_cases(Type_Dashboard_State *dashboard) {
    (void)dashboard;

    Global_Dashboard_Case_Count = 0;
    Global_Dashboard_Case_Point_Count = 0;

    DIR *dir = opendir(DASHBOARD_CASE_DIR);
    if (dir) {
        struct dirent *entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, "CASE_", 5) != 0) {
                continue;
            }
            if (strcmp(entry->d_name, "CASE_DESCRIPTIONS.csv") == 0) {
                continue;
            }
            if (!dashboard_file_has_suffix(entry->d_name, ".csv")) {
                continue;
            }

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

static int dashboard_lonlat_to_screen(double lon, double lat, SDL_Rect map, int *sx, int *sy) {
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
    if (!dashboard || dashboard->case_search_text[0] == '\0') {
        return 0;
    }
    if (case_index < 0 || case_index >= Global_Dashboard_Case_Count) {
        return 0;
    }
    return dashboard_ascii_contains_ci(Global_Dashboard_Cases[case_index].case_number, dashboard->case_search_text);
}

void dashboard_draw_case_points(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font, SDL_Rect map) {
    (void)font;
    if (!dashboard || !renderer) {
        return;
    }

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

        int selected = (dashboard->selected_case == pt->case_index);
        int search_match = dashboard_case_matches_search(dashboard, pt->case_index);
        dashboard_draw_case_dot(renderer, x, y, Global_Dashboard_Cases[pt->case_index].color, selected || search_match,
                                search_match);
    }
}

static int dashboard_select_case_at(Type_Dashboard_State *dashboard, SDL_Rect map, int x, int y) {
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
        } else {
            snprintf(test, sizeof(test), "%s", word);
        }

        int tw = 0;
        int th = 0;
        TTF_SizeText(font, test, &tw, &th);

        if (tw > rect.w && line[0]) {
            draw_text(renderer, font, line, rect.x, y, color);
            y += line_h;
            dashboard_copy_text(line, sizeof(line), word);
        } else {
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

static int dashboard_text_range_width(TTF_Font *font, const char *text, size_t start, size_t end);
static int dashboard_case_desc_build_lines(const char *text, int starts[128], int ends[128]);
static int dashboard_case_desc_selection_range(int *a, int *b);
static void dashboard_set_case_description_cursor_from_mouse(Type_Dashboard_State *dashboard, SDL_Rect rect,
                                                             int mouse_x, int mouse_y);
static void dashboard_case_description_start_selection(Type_Dashboard_State *dashboard);
static void dashboard_case_description_update_selection(Type_Dashboard_State *dashboard);
static void dashboard_case_desc_clear_selection(void);

void dashboard_draw_case_sidebar(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font,
                                 SDL_Rect sidebar) {
    if (!dashboard || dashboard->selected_case < 0 || dashboard->selected_case >= Global_Dashboard_Case_Count) {
        return;
    }

    Type_Dashboard_Case_Info *info = &Global_Dashboard_Cases[dashboard->selected_case];

    draw_filled_rect(renderer, sidebar, (SDL_Color){0, 5, 2, 248});
    draw_outline_rect(renderer, sidebar, info->color);

    draw_text(renderer, font, "CASE", sidebar.x + 16, sidebar.y + 18, Dashboard_Muted);
    draw_text(renderer, font, info->case_number, sidebar.x + 16, sidebar.y + 42, Dashboard_Text);

    char count_line[128];
    snprintf(count_line, sizeof(count_line), "Signals in case: %d", info->point_count);
    draw_text(renderer, font, count_line, sidebar.x + 16, sidebar.y + 72, Dashboard_Muted);

    draw_text(renderer, font, "Description", sidebar.x + 16, sidebar.y + 114, Dashboard_Text);

    dashboard->case_desc_rect = (SDL_Rect){sidebar.x + 16, sidebar.y + 142, sidebar.w - 32, 170};

    draw_filled_rect(renderer, dashboard->case_desc_rect,
                     dashboard->case_desc_editing ? (SDL_Color){0, 20, 8, 255} : (SDL_Color){0, 9, 4, 255});
    draw_outline_rect(renderer, dashboard->case_desc_rect,
                      dashboard->case_desc_editing ? Dashboard_Border_Hi : Dashboard_Border);

    SDL_Rect desc_text_rect = {dashboard->case_desc_rect.x + 9, dashboard->case_desc_rect.y + 9,
                               dashboard->case_desc_rect.w - 18, dashboard->case_desc_rect.h - 18};

    Global_Dashboard_Case_Desc_Font = font;
    Global_Dashboard_Case_Desc_Wrap_Px = desc_text_rect.w;

    const char *shown = dashboard->case_desc_editing ? dashboard->case_desc_edit : info->description;
    if (!shown || !shown[0]) {
        shown = dashboard->case_desc_editing ? "_" : "Click here to add a case description.";
    }

    if (dashboard->case_desc_editing) {
        int sel_a = 0;
        int sel_b = 0;
        int starts[128];
        int ends[128];
        int line_count = dashboard_case_desc_build_lines(dashboard->case_desc_edit, starts, ends);
        int line_h = TTF_FontHeight(font) + 4;

        if (dashboard_case_desc_selection_range(&sel_a, &sel_b)) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

            for (int line = 0; line < line_count; line++) {
                int line_a = starts[line];
                int line_b = ends[line];
                int a = sel_a > line_a ? sel_a : line_a;
                int b = sel_b < line_b ? sel_b : line_b;

                if (a < b) {
                    int x0 = desc_text_rect.x +
                             dashboard_text_range_width(font, dashboard->case_desc_edit, (size_t)line_a, (size_t)a);
                    int x1 = desc_text_rect.x +
                             dashboard_text_range_width(font, dashboard->case_desc_edit, (size_t)line_a, (size_t)b);
                    int y0 = desc_text_rect.y + line * line_h;

                    SDL_Rect hi = {x0, y0, x1 - x0, line_h};
                    draw_filled_rect(renderer, hi, (SDL_Color){0, 100, 220, 105});
                }
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
    }

    dashboard_wrap_text(renderer, font, shown, desc_text_rect, Dashboard_Muted);

    if (dashboard->case_desc_editing && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {
        int starts[128];
        int ends[128];
        int line_count = dashboard_case_desc_build_lines(dashboard->case_desc_edit, starts, ends);
        int line_h = TTF_FontHeight(font) + 4;
        int cursor = Global_Dashboard_Case_Desc_Cursor;
        int len = (int)strlen(dashboard->case_desc_edit);

        if (cursor < 0) {
            cursor = 0;
        }
        if (cursor > len) {
            cursor = len;
        }

        for (int line = 0; line < line_count; line++) {
            if (cursor >= starts[line] && cursor <= ends[line]) {
                int cx = desc_text_rect.x + dashboard_text_range_width(font, dashboard->case_desc_edit,
                                                                       (size_t)starts[line], (size_t)cursor);
                int cy0 = desc_text_rect.y + line * line_h;
                int cy1 = cy0 + line_h - 2;

                if (cx < desc_text_rect.x) {
                    cx = desc_text_rect.x;
                }
                if (cx > desc_text_rect.x + desc_text_rect.w) {
                    cx = desc_text_rect.x + desc_text_rect.w;
                }

                SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
                SDL_RenderDrawLine(renderer, cx, cy0, cx, cy1);
                SDL_RenderDrawLine(renderer, cx + 1, cy0, cx + 1, cy1);
                break;
            }
        }
    }

    draw_text(renderer, font, dashboard->case_desc_editing ? "Enter saves | Esc cancels" : "Click description to edit",
              sidebar.x + 16, dashboard->case_desc_rect.y + dashboard->case_desc_rect.h + 12, Dashboard_Muted);

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

static int dashboard_text_range_width(TTF_Font *font, const char *text, size_t start, size_t end) {
    char buf[512];
    int w = 0;
    int h = 0;

    if (!text || end <= start) {
        return 0;
    }
    if (end - start >= sizeof(buf)) {
        end = start + sizeof(buf) - 1;
    }

    memcpy(buf, text + start, end - start);
    buf[end - start] = '\0';

    if (!font || TTF_SizeText(font, buf, &w, &h) != 0) {
        return (int)(end - start) * 8;
    }

    return w;
}

static void dashboard_clamp_case_desc_cursor(Type_Dashboard_State *dashboard) {
    int len;

    if (!dashboard) {
        return;
    }

    len = (int)strlen(dashboard->case_desc_edit);

    if (Global_Dashboard_Case_Desc_Cursor < 0) {
        Global_Dashboard_Case_Desc_Cursor = 0;
    }
    if (Global_Dashboard_Case_Desc_Cursor > len) {
        Global_Dashboard_Case_Desc_Cursor = len;
    }
}

static void dashboard_case_desc_clear_selection(void) {
    Global_Dashboard_Case_Desc_Selecting = 0;
    Global_Dashboard_Case_Desc_Selection_Start = -1;
    Global_Dashboard_Case_Desc_Selection_End = -1;
}

static int dashboard_case_desc_selection_range(int *a, int *b) {
    int s = Global_Dashboard_Case_Desc_Selection_Start;
    int e = Global_Dashboard_Case_Desc_Selection_End;

    if (s < 0 || e < 0 || s == e) {
        return 0;
    }
    if (s > e) {
        int tmp = s;
        s = e;
        e = tmp;
    }

    if (a) {
        *a = s;
    }
    if (b) {
        *b = e;
    }
    return 1;
}

static int dashboard_case_desc_delete_selection(Type_Dashboard_State *dashboard) {
    int a = 0;
    int b = 0;
    int len;

    if (!dashboard || !dashboard_case_desc_selection_range(&a, &b)) {
        return 0;
    }

    len = (int)strlen(dashboard->case_desc_edit);
    if (a < 0) {
        a = 0;
    }
    if (b > len) {
        b = len;
    }
    if (a >= b) {
        dashboard_case_desc_clear_selection();
        return 0;
    }

    memmove(dashboard->case_desc_edit + a, dashboard->case_desc_edit + b, (size_t)(len - b) + 1U);

    Global_Dashboard_Case_Desc_Cursor = a;
    dashboard_case_desc_clear_selection();
    return 1;
}

static int dashboard_case_desc_build_lines(const char *text, int starts[128], int ends[128]) {
    int len = text ? (int)strlen(text) : 0;
    int line_count = 0;
    int start = 0;

    while (line_count < 128) {
        int end = start;
        while (end < len && text[end] != '\n') {
            end++;
        }
        starts[line_count] = start;
        ends[line_count] = end;
        line_count++;
        if (end >= len) {
            break;
        }
        start = end + 1;
    }

    if (line_count < 1) {
        starts[0] = 0;
        ends[0] = 0;
        line_count = 1;
    }

    return line_count;
}

static void dashboard_auto_wrap_text_field_px(char *text, size_t text_size, int *cursor, TTF_Font *font, int max_px) {
    size_t len;
    size_t line_start;

    if (!text || text_size == 0 || max_px < 16) {
        return;
    }

    len = strlen(text);
    line_start = 0;

    while (line_start < len) {
        size_t line_end = line_start;
        size_t segment_start = line_start;

        while (line_end < len && text[line_end] != '\n') {
            line_end++;
        }

        while (line_end > segment_start && dashboard_text_range_width(font, text, segment_start, line_end) > max_px) {
            size_t fit = segment_start + 1;
            size_t break_pos;
            int found_space = 0;

            for (size_t i = segment_start + 1; i <= line_end; i++) {
                if (dashboard_text_range_width(font, text, segment_start, i) <= max_px) {
                    fit = i;
                } else {
                    break;
                }
            }

            if (fit <= segment_start) {
                fit = segment_start + 1;
            }
            if (fit > line_end) {
                fit = line_end;
            }

            break_pos = fit;

            for (size_t i = fit; i > segment_start; i--) {
                if (text[i] == ' ' || text[i] == '\t') {
                    break_pos = i;
                    found_space = 1;
                    break;
                }
            }

            if (found_space) {
                text[break_pos] = '\n';
                segment_start = break_pos + 1;
            } else {
                if (len + 1 >= text_size) {
                    break;
                }

                memmove(text + break_pos + 1, text + break_pos, len - break_pos + 1);
                text[break_pos] = '\n';
                len++;
                line_end++;

                if (cursor && *cursor >= (int)break_pos) {
                    (*cursor)++;
                }
                segment_start = break_pos + 1;
            }
        }

        if (line_end >= len) {
            break;
        }
        line_start = line_end + 1;
    }
}

static void dashboard_insert_case_description_text(Type_Dashboard_State *dashboard, const char *src) {
    size_t len;
    size_t add;
    int cursor;

    if (!dashboard || !src || !src[0]) {
        return;
    }

    dashboard_clamp_case_desc_cursor(dashboard);
    dashboard_case_desc_delete_selection(dashboard);
    dashboard_clamp_case_desc_cursor(dashboard);

    len = strlen(dashboard->case_desc_edit);
    add = strlen(src);
    cursor = Global_Dashboard_Case_Desc_Cursor;

    if (len >= sizeof(dashboard->case_desc_edit) - 1) {
        return;
    }
    if (add > (sizeof(dashboard->case_desc_edit) - 1) - len) {
        add = (sizeof(dashboard->case_desc_edit) - 1) - len;
    }

    memmove(dashboard->case_desc_edit + cursor + (int)add, dashboard->case_desc_edit + cursor,
            len - (size_t)cursor + 1U);
    memcpy(dashboard->case_desc_edit + cursor, src, add);
    Global_Dashboard_Case_Desc_Cursor = cursor + (int)add;

    dashboard_auto_wrap_text_field_px(dashboard->case_desc_edit, sizeof(dashboard->case_desc_edit),
                                      &Global_Dashboard_Case_Desc_Cursor, Global_Dashboard_Case_Desc_Font,
                                      Global_Dashboard_Case_Desc_Wrap_Px);
    dashboard_clamp_case_desc_cursor(dashboard);
}

static void dashboard_paste_case_description(Type_Dashboard_State *dashboard) {
    char *clip;

    if (!dashboard) {
        return;
    }

    clip = SDL_GetClipboardText();
    if (clip) {
        dashboard_insert_case_description_text(dashboard, clip);
        SDL_free(clip);
    }
}

static void dashboard_backspace_case_description(Type_Dashboard_State *dashboard) {
    int len;
    int cursor;

    if (!dashboard) {
        return;
    }
    if (dashboard_case_desc_delete_selection(dashboard)) {
        return;
    }

    dashboard_clamp_case_desc_cursor(dashboard);
    len = (int)strlen(dashboard->case_desc_edit);
    cursor = Global_Dashboard_Case_Desc_Cursor;

    if (cursor <= 0 || len <= 0) {
        return;
    }

    memmove(dashboard->case_desc_edit + cursor - 1, dashboard->case_desc_edit + cursor, (size_t)(len - cursor) + 1U);
    Global_Dashboard_Case_Desc_Cursor--;
}

static void dashboard_delete_case_description(Type_Dashboard_State *dashboard) {
    int len;
    int cursor;

    if (!dashboard) {
        return;
    }
    if (dashboard_case_desc_delete_selection(dashboard)) {
        return;
    }

    dashboard_clamp_case_desc_cursor(dashboard);
    len = (int)strlen(dashboard->case_desc_edit);
    cursor = Global_Dashboard_Case_Desc_Cursor;

    if (cursor < 0 || cursor >= len) {
        return;
    }

    memmove(dashboard->case_desc_edit + cursor, dashboard->case_desc_edit + cursor + 1, (size_t)(len - cursor));
}

static void dashboard_set_case_description_cursor_from_mouse(Type_Dashboard_State *dashboard, SDL_Rect rect,
                                                             int mouse_x, int mouse_y) {
    int starts[128];
    int ends[128];
    int line_count;
    int line_h;
    int line;
    int rel_line;
    int line_len;
    int rel_x;
    int cursor;

    if (!dashboard) {
        return;
    }

    line_count = dashboard_case_desc_build_lines(dashboard->case_desc_edit, starts, ends);
    line_h = Global_Dashboard_Case_Desc_Font ? TTF_FontHeight(Global_Dashboard_Case_Desc_Font) + 4 : 20;
    if (line_h < 1) {
        line_h = 20;
    }

    rel_line = (mouse_y - (rect.y + 9)) / line_h;
    if (rel_line < 0) {
        rel_line = 0;
    }
    if (rel_line >= line_count) {
        rel_line = line_count - 1;
    }

    line = rel_line;
    line_len = ends[line] - starts[line];
    rel_x = mouse_x - (rect.x + 9);
    cursor = starts[line];

    for (int i = 0; i <= line_len; i++) {
        int w0 = dashboard_text_range_width(Global_Dashboard_Case_Desc_Font, dashboard->case_desc_edit,
                                            (size_t)starts[line], (size_t)(starts[line] + i));
        int w1 = w0;
        if (i < line_len) {
            w1 = dashboard_text_range_width(Global_Dashboard_Case_Desc_Font, dashboard->case_desc_edit,
                                            (size_t)starts[line], (size_t)(starts[line] + i + 1));
        }
        if (i == line_len || rel_x < (w0 + w1) / 2) {
            cursor = starts[line] + i;
            break;
        }
    }

    Global_Dashboard_Case_Desc_Cursor = cursor;
    dashboard_clamp_case_desc_cursor(dashboard);
}

static void dashboard_case_description_start_selection(Type_Dashboard_State *dashboard) {
    dashboard_clamp_case_desc_cursor(dashboard);
    Global_Dashboard_Case_Desc_Selecting = 1;
    Global_Dashboard_Case_Desc_Selection_Start = Global_Dashboard_Case_Desc_Cursor;
    Global_Dashboard_Case_Desc_Selection_End = Global_Dashboard_Case_Desc_Cursor;
}

static void dashboard_case_description_update_selection(Type_Dashboard_State *dashboard) {
    dashboard_clamp_case_desc_cursor(dashboard);
    if (Global_Dashboard_Case_Desc_Selection_Start < 0) {
        Global_Dashboard_Case_Desc_Selection_Start = Global_Dashboard_Case_Desc_Cursor;
    }
    Global_Dashboard_Case_Desc_Selection_End = Global_Dashboard_Case_Desc_Cursor;
}

static int dashboard_handle_case_search_event(Type_Dashboard_State *dashboard, const SDL_Event *event) {
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
    if (!dashboard || !event) {
        return 0;
    }

    if (dashboard->case_desc_editing) {
        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
            if (dashboard_point_in_rect(event->button.x, event->button.y, dashboard->case_desc_rect)) {
                dashboard_set_case_description_cursor_from_mouse(dashboard, dashboard->case_desc_rect, event->button.x,
                                                                 event->button.y);
                dashboard_case_description_start_selection(dashboard);
                return 1;
            }
        }

        if (event->type == SDL_MOUSEMOTION && Global_Dashboard_Case_Desc_Selecting) {
            dashboard_set_case_description_cursor_from_mouse(dashboard, dashboard->case_desc_rect, event->motion.x,
                                                             event->motion.y);
            dashboard_case_description_update_selection(dashboard);
            return 1;
        }

        if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
            Global_Dashboard_Case_Desc_Selecting = 0;
            return 1;
        }

        if (event->type == SDL_TEXTINPUT) {
            dashboard_insert_case_description_text(dashboard, event->text.text);
            return 1;
        }

        if (event->type == SDL_KEYDOWN) {
            SDL_Keycode key = event->key.keysym.sym;
            SDL_Keymod mod = SDL_GetModState();
            int len = (int)strlen(dashboard->case_desc_edit);

            if ((mod & KMOD_CTRL) && key == SDLK_v) {
                dashboard_paste_case_description(dashboard);
                return 1;
            }
            if (key == SDLK_BACKSPACE) {
                dashboard_backspace_case_description(dashboard);
                return 1;
            }
            if (key == SDLK_DELETE) {
                dashboard_delete_case_description(dashboard);
                return 1;
            }
            if (key == SDLK_LEFT) {
                dashboard_clamp_case_desc_cursor(dashboard);
                if (Global_Dashboard_Case_Desc_Cursor > 0) {
                    Global_Dashboard_Case_Desc_Cursor--;
                }
                dashboard_case_desc_clear_selection();
                return 1;
            }
            if (key == SDLK_RIGHT) {
                dashboard_clamp_case_desc_cursor(dashboard);
                if (Global_Dashboard_Case_Desc_Cursor < len) {
                    Global_Dashboard_Case_Desc_Cursor++;
                }
                dashboard_case_desc_clear_selection();
                return 1;
            }
            if (key == SDLK_HOME) {
                Global_Dashboard_Case_Desc_Cursor = 0;
                dashboard_case_desc_clear_selection();
                return 1;
            }
            if (key == SDLK_END) {
                Global_Dashboard_Case_Desc_Cursor = len;
                dashboard_case_desc_clear_selection();
                return 1;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (dashboard->selected_case >= 0 && dashboard->selected_case < Global_Dashboard_Case_Count) {
                    snprintf(Global_Dashboard_Cases[dashboard->selected_case].description,
                             sizeof(Global_Dashboard_Cases[dashboard->selected_case].description), "%s",
                             dashboard->case_desc_edit);
                    dashboard_save_case_descriptions();
                }
                dashboard->case_desc_editing = 0;
                dashboard_case_desc_clear_selection();
                return 1;
            }
            if (key == SDLK_ESCAPE) {
                if (dashboard->selected_case >= 0 && dashboard->selected_case < Global_Dashboard_Case_Count) {
                    snprintf(dashboard->case_desc_edit, sizeof(dashboard->case_desc_edit), "%s",
                             Global_Dashboard_Cases[dashboard->selected_case].description);
                }
                dashboard->case_desc_editing = 0;
                dashboard_case_desc_clear_selection();
                return 1;
            }
            return 1;
        }
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        if (dashboard->selected_case >= 0 &&
            dashboard_point_in_rect(event->button.x, event->button.y, dashboard->case_desc_rect)) {
            dashboard->case_desc_editing = 1;
            snprintf(dashboard->case_desc_edit, sizeof(dashboard->case_desc_edit), "%s",
                     Global_Dashboard_Cases[dashboard->selected_case].description);
            Global_Dashboard_Case_Desc_Cursor = (int)strlen(dashboard->case_desc_edit);
            dashboard_set_case_description_cursor_from_mouse(dashboard, dashboard->case_desc_rect, event->button.x,
                                                             event->button.y);
            dashboard_case_description_start_selection(dashboard);
            return 1;
        }
    }

    return 0;
}

int dashboard_handle_top_tab_event(Type_Dashboard_State *dashboard, const SDL_Event *event, int win_w,
                                   int text_entry_active) {
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
    } else {
        snprintf(dashboard->status, sizeof(dashboard->status),
                 "Map data not loaded. Put world_map.bin next to the executable.");
    }

    dashboard_reload_cases(dashboard);

    return dashboard->map_loaded;
}

void dashboard_shutdown(void) {
    WORLD_MAP_free();
}

static int dashboard_find_country_screen_point(SDL_Rect map, int country_index, int *out_x, int *out_y) {
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
    if (!dashboard || !event || !dashboard->enabled) {
        return DASHBOARD_EVENT_NONE;
    }

    if (event->type == SDL_QUIT) {
        return DASHBOARD_EVENT_QUIT;
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

        if (key == SDLK_q && !dashboard->case_desc_editing) {
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
                } else {
                    dashboard->locked_country = -1;
                }
            }
        }

        WORLD_MAP_handle_event(event, map);
    }

    return DASHBOARD_EVENT_NONE;
}

void dashboard_draw(Type_Dashboard_State *dashboard, SDL_Renderer *renderer, TTF_Font *font_small,
                    TTF_Font *font_medium, int win_w, int win_h, int mouse_x, int mouse_y) {
    if (!dashboard || !renderer) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, Dashboard_BG.r, Dashboard_BG.g, Dashboard_BG.b, Dashboard_BG.a);
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

        if (dashboard->locked_country >= 0 && dashboard->locked_country < (int)WM_DATA.country_count) {
            dashboard->hover_country = dashboard->locked_country;
            draw_mouse_x = dashboard->locked_mouse_x;
            draw_mouse_y = dashboard->locked_mouse_y;

            if (dashboard_find_country_screen_point(map, dashboard->locked_country, &draw_mouse_x, &draw_mouse_y)) {
                dashboard->locked_mouse_x = draw_mouse_x;
                dashboard->locked_mouse_y = draw_mouse_y;
                dashboard->hover_mouse_x = draw_mouse_x;
                dashboard->hover_mouse_y = draw_mouse_y;
            }
        } else if (dashboard_point_in_rect(mouse_x, mouse_y, map)) {
            int hovered = WM_country_at(map, mouse_x, mouse_y);
            if (hovered != dashboard->hover_country) {
                dashboard->country_case_scroll = 0;
            }
            dashboard->hover_country = hovered;
            if (hovered >= 0) {
                dashboard->hover_mouse_x = mouse_x;
                dashboard->hover_mouse_y = mouse_y;
            }
        } else if (dashboard_point_in_rect(mouse_x, mouse_y, sidebar) && dashboard->hover_country >= 0) {
            draw_mouse_x = dashboard->hover_mouse_x;
            draw_mouse_y = dashboard->hover_mouse_y;
        } else {
            dashboard->hover_country = -1;
        }

        WORLD_MAP_draw(renderer, font_small, map, sidebar, draw_mouse_x, draw_mouse_y, "flags");
        dashboard_draw_hover_country_cases(dashboard, renderer, font_small, sidebar);
        dashboard_draw_case_points(dashboard, renderer, font_small, map);
        dashboard_draw_case_search(dashboard, renderer, font_small, search_area);
        dashboard_draw_case_sidebar(dashboard, renderer, font_small, sidebar);
    } else {
        draw_text(renderer, font_medium, "world_map.bin was not loaded", content.x + 24, content.y + 26,
                  Dashboard_Warn);
        draw_text(renderer, font_small,
                  "Put world_map.bin in the working directory, or change the path "
                  "passed to dashboard_init().",
                  content.x + 24, content.y + 58, Dashboard_Muted);
    }

    draw_text(renderer, font_small, dashboard->status, DASHBOARD_MARGIN + 8, win_h - 38,
              dashboard->map_loaded ? Dashboard_Muted : Dashboard_Warn);
}
