/*
 * World Map Hover + Flags, binary-data version.
 *
 * Compile standalone:
 *   gcc -Wall -Wextra -O2 world_map_bin_loader.c -o world_map_bin_loader \
 *       -lSDL2 -lSDL2_image -lSDL2_ttf -lm
 *
 * Runtime files:
 *   world_map.bin
 *   flags/<iso-alpha-2>.png
 *
 * Use inside your app:
 *   #define WORLD_MAP_NO_DEMO
 *   #include "world_map_bin_loader.c"
 *   WORLD_MAP_load("world_map.bin");
 *   WORLD_MAP_handle_event(&event, map_rect);
 *   WORLD_MAP_draw(renderer, font, map_rect, sidebar_rect, mouse_x, mouse_y,
 * "flags");
 *
 * Left-click a country to zoom the map view to that country.
 * Drag with the left mouse button to pan across the map.
 * Mouse wheel zooms in/out around the cursor. Escape resets the view.
 * Dateline-spanning countries use the clicked land segment for zoom bounds.
 * High-resolution coastline/island/border linework is loaded from
 * world_map.bin. Dateline line jumps are skipped to prevent horizontal wrap
 * artifacts. Country polygons are now high-resolution and are used for fill,
 * hit-testing, visible borders, and hover selection, so the hover outline
 * matches the border geometry exactly.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WORLD_MAP_SIDEBAR_W
#define WORLD_MAP_SIDEBAR_W 340
#endif

#ifndef WORLD_MAP_MAX_SCREEN_POINTS
#define WORLD_MAP_MAX_SCREEN_POINTS 32768
#endif

#define WM_BIN_MAGIC "WMBIN001"
#define WM_BIN_VERSION 2U

#define WM_DETAIL_LAYER_BORDER 0U
#define WM_DETAIL_LAYER_COAST 1U

typedef struct WM_Point {
    int16_t lon100;
    int16_t lat100;
} WM_Point;

typedef struct WM_Polygon {
    uint32_t start;
    uint32_t count;
    uint16_t country;
    int16_t min_lon100;
    int16_t min_lat100;
    int16_t max_lon100;
    int16_t max_lat100;
} WM_Polygon;

typedef struct WM_DetailSegment {
    uint32_t start;
    uint32_t count;
    uint16_t layer;
    int16_t min_lon100;
    int16_t min_lat100;
    int16_t max_lon100;
    int16_t max_lat100;
} WM_DetailSegment;

typedef struct WM_Country {
    const char *name;
    const char *alpha2;
    uint32_t poly_start;
    uint32_t poly_count;
    int16_t min_lon100;
    int16_t min_lat100;
    int16_t max_lon100;
    int16_t max_lat100;
    SDL_Texture *flag_texture;
    int flag_attempted;
    int flag_w;
    int flag_h;
} WM_Country;

typedef struct WM_Map_Data {
    WM_Point *points;
    WM_Polygon *polygons;
    WM_Country *countries;
    WM_Point *detail_points;
    WM_DetailSegment *detail_segments;
    char *strings;
    uint32_t point_count;
    uint32_t polygon_count;
    uint32_t country_count;
    uint32_t string_bytes;
    uint32_t detail_point_count;
    uint32_t detail_segment_count;
    int loaded;
    int attempted_default_load;
} WM_Map_Data;

typedef struct WM_View {
    double min_lon;
    double max_lon;
    double min_lat;
    double max_lat;
    int zoomed_country;
    int dragging;
    int drag_moved;
    int drag_start_x;
    int drag_start_y;
    int drag_last_x;
    int drag_last_y;
    int drag_offset_x;
    int drag_offset_y;
    int drag_hover_country;
} WM_View;

typedef struct WM_Render_Cache {
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    int texture_w;
    int texture_h;
    int map_w;
    int map_h;
    double min_lon;
    double max_lon;
    double min_lat;
    double max_lat;
    double source_lon_span;
    double source_lat_span;
    int valid;
} WM_Render_Cache;

static WM_Map_Data WM_DATA = {0};
static WM_View WM_VIEW = {
    .min_lon = -180.0,
    .max_lon = 180.0,
    .min_lat = -90.0,
    .max_lat = 90.0,
    .zoomed_country = -1,
    .drag_hover_country = -1,
};
static WM_Render_Cache WM_RENDER_CACHE = {0};
static int WM_LAST_HOVER_POLYGON = -1;
/* -2 means no completed click is pending; -1 means the user clicked ocean/background. */
static int WM_PENDING_CLICK_COUNTRY = -2;
static int WM_RENDERING_CACHE_TEXTURE = 0;

static void WM_destroy_render_cache(void) {
    /*
        Purpose: Destroys the cached world-map texture
        Returns: No value
    */

    if (WM_RENDER_CACHE.texture) {

        SDL_DestroyTexture(WM_RENDER_CACHE.texture);

    }

    memset(&WM_RENDER_CACHE, 0, sizeof(WM_RENDER_CACHE));
}

static int WM_read_exact(FILE *fp, void *dst, size_t bytes) {
    /*
        Purpose: Reads an exact number of bytes
        Returns: Success status
    */

    return fp && dst && fread(dst, 1, bytes, fp) == bytes;
}

static int WM_read_u32(FILE *fp, uint32_t *out) {
    /*
        Purpose: Reads the 32-bit unsigned
        Returns: Success status
    */

    unsigned char b[4];

    if (!WM_read_exact(fp, b, 4)) {

        return 0;

    }
    *out = ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static int WM_read_u16(FILE *fp, uint16_t *out) {
    /*
        Purpose: Reads the 16-bit unsigned
        Returns: Success status
    */

    unsigned char b[2];

    if (!WM_read_exact(fp, b, 2)) {

        return 0;

    }
    *out = (uint16_t)(((uint16_t)b[0]) | ((uint16_t)b[1] << 8));
    return 1;
}

static int WM_read_i16(FILE *fp, int16_t *out) {
    /*
        Purpose: Reads the 16-bit signed
        Returns: Success status
    */

    uint16_t v = 0;

    if (!WM_read_u16(fp, &v)) {

        return 0;

    }
    *out = (int16_t)v;
    return 1;
}

static void WM_recompute_bounds_from_points(WM_Point *points, uint32_t point_count, WM_Polygon *polygons,
                                            uint32_t polygon_count, WM_Country *countries, uint32_t country_count) {
    /*
        Purpose: Recomputes the bounds from points
        Returns: No value
    */

    if (!points || !polygons || !countries) {

        return;

    }

    for (uint32_t c = 0; c < country_count; c++) {
        countries[c].min_lon100 = 32767;
        countries[c].min_lat100 = 32767;
        countries[c].max_lon100 = -32768;
        countries[c].max_lat100 = -32768;
    }

    for (uint32_t p = 0; p < polygon_count; p++) {
        WM_Polygon *poly = &polygons[p];

        if (poly->count == 0 || poly->start >= point_count || poly->start + poly->count > point_count) {

            continue;

        }

        int16_t min_lon = points[poly->start].lon100;
        int16_t max_lon = points[poly->start].lon100;
        int16_t min_lat = points[poly->start].lat100;
        int16_t max_lat = points[poly->start].lat100;

        for (uint32_t i = 1; i < poly->count; i++) {
            WM_Point pt = points[poly->start + i];

            if (pt.lon100 < min_lon) {

                min_lon = pt.lon100;

            }

            if (pt.lon100 > max_lon) {

                max_lon = pt.lon100;

            }

            if (pt.lat100 < min_lat) {

                min_lat = pt.lat100;

            }

            if (pt.lat100 > max_lat) {

                max_lat = pt.lat100;

            }
        }

        poly->min_lon100 = min_lon;
        poly->max_lon100 = max_lon;
        poly->min_lat100 = min_lat;
        poly->max_lat100 = max_lat;

        if (poly->country < country_count) {

            WM_Country *country = &countries[poly->country];

            if (min_lon < country->min_lon100) {

                country->min_lon100 = min_lon;

            }

            if (max_lon > country->max_lon100) {

                country->max_lon100 = max_lon;

            }

            if (min_lat < country->min_lat100) {

                country->min_lat100 = min_lat;

            }

            if (max_lat > country->max_lat100) {

                country->max_lat100 = max_lat;

            }

        }
    }

    for (uint32_t c = 0; c < country_count; c++) {

        if (countries[c].min_lon100 == 32767) {

            countries[c].min_lon100 = -18000;
            countries[c].max_lon100 = 18000;
            countries[c].min_lat100 = -9000;
            countries[c].max_lat100 = 9000;

        }
    }
}

static void WM_draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    /*
        Purpose: Draws the text
        Returns: No value
    */

    if (!renderer || !font || !text || text[0] == '\0') {

        return;

    }
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);

    if (!surface) {

        return;

    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);

    if (!texture) {

        SDL_FreeSurface(surface);
        return;

    }
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

static void WM_copy_text(char *dst, size_t dst_size, const char *src) {
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

static void WM_draw_text_wrapped(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect,
                                 SDL_Color color) {
    /*
        Purpose: Draws the text wrapped
        Returns: No value
    */

    if (!renderer || !font || !text) {

        return;

    }

    char line[256];
    line[0] = '\0';
    int y = rect.y;
    int line_h = TTF_FontHeight(font) + 4;
    const char *p = text;

    while (*p && y + line_h <= rect.y + rect.h) {
        char word[128];
        int wi = 0;

        while (*p == ' ') {
            p++;
        }
        while (*p && *p != ' ' && wi < (int)sizeof(word) - 1) {
            word[wi++] = *p++;
        }
        word[wi] = '\0';

        if (wi == 0) {

            break;

        }

        char trial[384];

        if (line[0]) {

            snprintf(trial, sizeof(trial), "%s %s", line, word);

        }

        else {

            snprintf(trial, sizeof(trial), "%s", word);

        }

        int tw = 0;
        int th = 0;

        if (TTF_SizeUTF8(font, trial, &tw, &th) == 0 && tw <= rect.w) {

            WM_copy_text(line, sizeof(line), trial);

        }

        else {

            if (line[0]) {

                WM_draw_text(renderer, font, line, rect.x, y, color);
                y += line_h;
                WM_copy_text(line, sizeof(line), word);

            }

            else {

                WM_draw_text(renderer, font, word, rect.x, y, color);
                y += line_h;

            }

        }
    }

    if (line[0] && y + line_h <= rect.y + rect.h) {

        WM_draw_text(renderer, font, line, rect.x, y, color);

    }
}

void WORLD_MAP_free_flags(void) {
    /*
        Purpose: Frees the flags
        Returns: No value
    */

    if (!WM_DATA.countries) {

        return;

    }
    for (uint32_t i = 0; i < WM_DATA.country_count; i++) {

        if (WM_DATA.countries[i].flag_texture) {

            SDL_DestroyTexture(WM_DATA.countries[i].flag_texture);

        }
        WM_DATA.countries[i].flag_texture = NULL;
        WM_DATA.countries[i].flag_attempted = 0;
        WM_DATA.countries[i].flag_w = 0;
        WM_DATA.countries[i].flag_h = 0;
    }
}

void WORLD_MAP_reset_view(void) {
    /*
        Purpose: Resets the view
        Returns: No value
    */

    WM_VIEW.min_lon = -180.0;
    WM_VIEW.max_lon = 180.0;
    WM_VIEW.min_lat = -90.0;
    WM_VIEW.max_lat = 90.0;
    WM_VIEW.zoomed_country = -1;
    WM_VIEW.dragging = 0;
    WM_VIEW.drag_moved = 0;
    WM_VIEW.drag_start_x = 0;
    WM_VIEW.drag_start_y = 0;
    WM_VIEW.drag_last_x = 0;
    WM_VIEW.drag_last_y = 0;
    WM_VIEW.drag_offset_x = 0;
    WM_VIEW.drag_offset_y = 0;
    WM_VIEW.drag_hover_country = -1;
    WM_LAST_HOVER_POLYGON = -1;
    WM_PENDING_CLICK_COUNTRY = -2;
    WM_RENDER_CACHE.valid = 0;
}

void WORLD_MAP_free(void) {
    /*
        Purpose: Frees the requested operation
        Returns: No value
    */

    WM_destroy_render_cache();
    WORLD_MAP_free_flags();
    free(WM_DATA.points);
    free(WM_DATA.polygons);
    free(WM_DATA.countries);
    free(WM_DATA.detail_points);
    free(WM_DATA.detail_segments);
    free(WM_DATA.strings);
    memset(&WM_DATA, 0, sizeof(WM_DATA));
    WORLD_MAP_reset_view();
}

int WORLD_MAP_load(const char *path) {
    /*
        Purpose: Loads the requested operation
        Returns: Success status
    */

    FILE *fp = fopen(path ? path : "world_map.bin", "rb");

    if (!fp) {

        return 0;

    }

    WORLD_MAP_free();

    char magic[8];
    uint32_t version = 0;
    uint32_t point_count = 0;
    uint32_t polygon_count = 0;
    uint32_t country_count = 0;
    uint32_t string_bytes = 0;
    uint32_t detail_point_count = 0;
    uint32_t detail_segment_count = 0;

    if (!WM_read_exact(fp, magic, 8) || memcmp(magic, WM_BIN_MAGIC, 8) != 0 || !WM_read_u32(fp, &version) ||
        version != WM_BIN_VERSION || !WM_read_u32(fp, &point_count) || !WM_read_u32(fp, &polygon_count) ||
        !WM_read_u32(fp, &country_count) || !WM_read_u32(fp, &string_bytes) || !WM_read_u32(fp, &detail_point_count) ||
        !WM_read_u32(fp, &detail_segment_count)) {

        fclose(fp);
        return 0;

    }

    WM_Point *points = calloc(point_count, sizeof(WM_Point));
    WM_Polygon *polygons = calloc(polygon_count, sizeof(WM_Polygon));
    WM_Country *countries = calloc(country_count, sizeof(WM_Country));
    WM_Point *detail_points = calloc(detail_point_count, sizeof(WM_Point));
    WM_DetailSegment *detail_segments = calloc(detail_segment_count, sizeof(WM_DetailSegment));
    char *strings = calloc((size_t)string_bytes + 1, 1);

    if (!points || !polygons || !countries || !detail_points || !detail_segments || !strings) {

        free(points);
        free(polygons);
        free(countries);
        free(detail_points);
        free(detail_segments);
        free(strings);
        fclose(fp);
        return 0;

    }

    for (uint32_t i = 0; i < point_count; i++) {

        if (!WM_read_i16(fp, &points[i].lon100) || !WM_read_i16(fp, &points[i].lat100)) {

            goto fail;

        }
    }

    for (uint32_t i = 0; i < polygon_count; i++) {

        if (!WM_read_u32(fp, &polygons[i].start) || !WM_read_u32(fp, &polygons[i].count) ||
            !WM_read_u16(fp, &polygons[i].country) || !WM_read_i16(fp, &polygons[i].min_lon100) ||
            !WM_read_i16(fp, &polygons[i].min_lat100) || !WM_read_i16(fp, &polygons[i].max_lon100) ||
            !WM_read_i16(fp, &polygons[i].max_lat100)) {

            goto fail;

        }
    }

    uint32_t *name_offsets = calloc(country_count, sizeof(uint32_t));
    uint32_t *alpha_offsets = calloc(country_count, sizeof(uint32_t));

    if (!name_offsets || !alpha_offsets) {

        free(name_offsets);
        free(alpha_offsets);
        goto fail;

    }

    for (uint32_t i = 0; i < country_count; i++) {

        if (!WM_read_u32(fp, &name_offsets[i]) || !WM_read_u32(fp, &alpha_offsets[i]) ||
            !WM_read_u32(fp, &countries[i].poly_start) || !WM_read_u32(fp, &countries[i].poly_count) ||
            !WM_read_i16(fp, &countries[i].min_lon100) || !WM_read_i16(fp, &countries[i].min_lat100) ||
            !WM_read_i16(fp, &countries[i].max_lon100) || !WM_read_i16(fp, &countries[i].max_lat100)) {

            free(name_offsets);
            free(alpha_offsets);
            goto fail;

        }
    }

    if (!WM_read_exact(fp, strings, string_bytes)) {

        free(name_offsets);
        free(alpha_offsets);
        goto fail;

    }

    for (uint32_t i = 0; i < detail_point_count; i++) {

        if (!WM_read_i16(fp, &detail_points[i].lon100) || !WM_read_i16(fp, &detail_points[i].lat100)) {

            free(name_offsets);
            free(alpha_offsets);
            goto fail;

        }
    }

    for (uint32_t i = 0; i < detail_segment_count; i++) {

        if (!WM_read_u32(fp, &detail_segments[i].start) || !WM_read_u32(fp, &detail_segments[i].count) ||
            !WM_read_u16(fp, &detail_segments[i].layer) || !WM_read_i16(fp, &detail_segments[i].min_lon100) ||
            !WM_read_i16(fp, &detail_segments[i].min_lat100) || !WM_read_i16(fp, &detail_segments[i].max_lon100) ||
            !WM_read_i16(fp, &detail_segments[i].max_lat100)) {

            free(name_offsets);
            free(alpha_offsets);
            goto fail;

        }
    }

    for (uint32_t i = 0; i < country_count; i++) {

        if (name_offsets[i] >= string_bytes || alpha_offsets[i] >= string_bytes) {

            free(name_offsets);
            free(alpha_offsets);
            goto fail;

        }
        countries[i].name = strings + name_offsets[i];
        countries[i].alpha2 = strings + alpha_offsets[i];
    }

    WM_recompute_bounds_from_points(points, point_count, polygons, polygon_count, countries, country_count);

    free(name_offsets);
    free(alpha_offsets);
    fclose(fp);

    WM_DATA.points = points;
    WM_DATA.polygons = polygons;
    WM_DATA.countries = countries;
    WM_DATA.detail_points = detail_points;
    WM_DATA.detail_segments = detail_segments;
    WM_DATA.strings = strings;
    WM_DATA.point_count = point_count;
    WM_DATA.polygon_count = polygon_count;
    WM_DATA.country_count = country_count;
    WM_DATA.string_bytes = string_bytes;
    WM_DATA.detail_point_count = detail_point_count;
    WM_DATA.detail_segment_count = detail_segment_count;
    WM_DATA.loaded = 1;
    return 1;

fail:
    free(points);
    free(polygons);
    free(countries);
    free(detail_points);
    free(detail_segments);
    free(strings);
    fclose(fp);
    return 0;
}

static int WM_ensure_loaded(void) {
    /*
        Purpose: Ensures the world map data is loaded
        Returns: Success status
    */

    if (WM_DATA.loaded) {

        return 1;

    }

    if (WM_DATA.attempted_default_load) {

        return 0;

    }
    WM_DATA.attempted_default_load = 1;
    return WORLD_MAP_load("world_map.bin");
}

static void WM_project_point(int16_t lon100, int16_t lat100, SDL_Rect map_rect, int *out_x, int *out_y) {
    /*
        Purpose: Projects the point
        Returns: No value
    */

    double lon = (double)lon100 / 100.0;
    double lat = (double)lat100 / 100.0;
    double lon_span = WM_VIEW.max_lon - WM_VIEW.min_lon;
    double lat_span = WM_VIEW.max_lat - WM_VIEW.min_lat;

    if (lon_span <= 0.0001) {

        lon_span = 360.0;

    }

    if (lat_span <= 0.0001) {

        lat_span = 180.0;

    }

    double xf = (lon - WM_VIEW.min_lon) / lon_span;
    double yf = (WM_VIEW.max_lat - lat) / lat_span;

    int x = map_rect.x + (int)(xf * (double)map_rect.w);
    int y = map_rect.y + (int)(yf * (double)map_rect.h);

    int min_x = map_rect.x - map_rect.w;
    int max_x = map_rect.x + map_rect.w * 2;
    int min_y = map_rect.y - map_rect.h;
    int max_y = map_rect.y + map_rect.h * 2;

    if (x < min_x) {

        x = min_x;

    }

    if (x > max_x) {

        x = max_x;

    }

    if (y < min_y) {

        y = min_y;

    }

    if (y > max_y) {

        y = max_y;

    }

    *out_x = x;
    *out_y = y;
}

static void WM_screen_to_lonlat100(SDL_Rect map_rect, int x, int y, int16_t *lon100, int16_t *lat100) {
    /*
        Purpose: Converts the screen to the lonlat100
        Returns: No value
    */

    double xf = (double)(x - map_rect.x) / (double)map_rect.w;
    double yf = (double)(y - map_rect.y) / (double)map_rect.h;
    double lon = WM_VIEW.min_lon + xf * (WM_VIEW.max_lon - WM_VIEW.min_lon);
    double lat = WM_VIEW.max_lat - yf * (WM_VIEW.max_lat - WM_VIEW.min_lat);

    if (lon < -180.0) {

        lon = -180.0;

    }

    if (lon > 180.0) {

        lon = 180.0;

    }

    if (lat < -90.0) {

        lat = -90.0;

    }

    if (lat > 90.0) {

        lat = 90.0;

    }

    *lon100 = (int16_t)(lon * 100.0);
    *lat100 = (int16_t)(lat * 100.0);
}

static int WM_point_in_polygon_lonlat(int16_t test_lon100, int16_t test_lat100, const WM_Polygon *poly) {
    /*
        Purpose: Checks whether a geographic point lies inside a polygon
        Returns: Boolean status
    */

    int inside = 0;
    uint32_t start = poly->start;
    uint32_t count = poly->count;

    if (!WM_DATA.points || count < 3 || start + count > WM_DATA.point_count) {

        return 0;

    }
    for (uint32_t i = 0, j = count - 1; i < count; j = i++) {
        double xi = (double)WM_DATA.points[start + i].lon100;
        double yi = (double)WM_DATA.points[start + i].lat100;
        double xj = (double)WM_DATA.points[start + j].lon100;
        double yj = (double)WM_DATA.points[start + j].lat100;
        double yv = (double)test_lat100;
        double xv = (double)test_lon100;
        int intersect = ((yi > yv) != (yj > yv)) && (xv < (xj - xi) * (yv - yi) / ((yj - yi) + 1e-12) + xi);

        if (intersect) {

            inside = !inside;

        }
    }
    return inside;
}

static int WM_country_at(SDL_Rect map_rect, int mouse_x, int mouse_y);

static void WM_clamp_view(void);

static int WM_polygon_intersects_view(const WM_Polygon *poly) {
    /*
        Purpose: Checks whether the polygon intersects the view
        Returns: Success status
    */

    if (!poly) {

        return 0;

    }
    double min_lon = (double)poly->min_lon100 / 100.0;
    double max_lon = (double)poly->max_lon100 / 100.0;
    double min_lat = (double)poly->min_lat100 / 100.0;
    double max_lat = (double)poly->max_lat100 / 100.0;
    return !(max_lon < WM_VIEW.min_lon || min_lon > WM_VIEW.max_lon || max_lat < WM_VIEW.min_lat ||
             min_lat > WM_VIEW.max_lat);
}

static void WM_zoom_to_country(int country_index, int polygon_index, SDL_Rect map_rect) {
    /*
        Purpose: Zooms the to country
        Returns: No value
    */

    if (country_index < 0 || country_index >= (int)WM_DATA.country_count || map_rect.w <= 0 || map_rect.h <= 0) {

        return;

    }

    WM_Country *country = &WM_DATA.countries[country_index];
    double min_lon = (double)country->min_lon100 / 100.0;
    double max_lon = (double)country->max_lon100 / 100.0;
    double min_lat = (double)country->min_lat100 / 100.0;
    double max_lat = (double)country->max_lat100 / 100.0;

    if (polygon_index >= 0 && polygon_index < (int)WM_DATA.polygon_count) {

        WM_Polygon *poly = &WM_DATA.polygons[polygon_index];
        double country_lon_span = max_lon - min_lon;

        if (poly->country == (uint16_t)country_index && country_lon_span > 300.0) {

            min_lon = (double)poly->min_lon100 / 100.0;
            max_lon = (double)poly->max_lon100 / 100.0;
            min_lat = (double)poly->min_lat100 / 100.0;
            max_lat = (double)poly->max_lat100 / 100.0;

        }

    }

    double center_lon = (min_lon + max_lon) * 0.5;
    double center_lat = (min_lat + max_lat) * 0.5;
    double lon_span = max_lon - min_lon;
    double lat_span = max_lat - min_lat;

    if (lon_span < 1.0) {

        lon_span = 1.0;

    }

    if (lat_span < 1.0) {

        lat_span = 1.0;

    }

    double map_aspect = (double)map_rect.w / (double)map_rect.h;
    double view_aspect = lon_span / lat_span;

    if (view_aspect < map_aspect) {

        lon_span = lat_span * map_aspect;

    }

    else {

        lat_span = lon_span / map_aspect;

    }

    lon_span *= 1.08;
    lat_span *= 1.08;

    if (lon_span > 360.0) {

        lon_span = 360.0;

    }

    if (lat_span > 180.0) {

        lat_span = 180.0;

    }

    WM_VIEW.min_lon = center_lon - lon_span * 0.5;
    WM_VIEW.max_lon = center_lon + lon_span * 0.5;
    WM_VIEW.min_lat = center_lat - lat_span * 0.5;
    WM_VIEW.max_lat = center_lat + lat_span * 0.5;

    WM_clamp_view();
    WM_VIEW.zoomed_country = country_index;
}

static void WM_clamp_view(void) {
    /*
        Purpose: Clamps the view
        Returns: No value
    */

    if (WM_VIEW.min_lon < -180.0) {

        WM_VIEW.max_lon += -180.0 - WM_VIEW.min_lon;
        WM_VIEW.min_lon = -180.0;

    }

    if (WM_VIEW.max_lon > 180.0) {

        WM_VIEW.min_lon -= WM_VIEW.max_lon - 180.0;
        WM_VIEW.max_lon = 180.0;

    }

    if (WM_VIEW.min_lat < -90.0) {

        WM_VIEW.max_lat += -90.0 - WM_VIEW.min_lat;
        WM_VIEW.min_lat = -90.0;

    }

    if (WM_VIEW.max_lat > 90.0) {

        WM_VIEW.min_lat -= WM_VIEW.max_lat - 90.0;
        WM_VIEW.max_lat = 90.0;

    }

    if (WM_VIEW.min_lon < -180.0) {

        WM_VIEW.min_lon = -180.0;

    }

    if (WM_VIEW.max_lon > 180.0) {

        WM_VIEW.max_lon = 180.0;

    }

    if (WM_VIEW.min_lat < -90.0) {

        WM_VIEW.min_lat = -90.0;

    }

    if (WM_VIEW.max_lat > 90.0) {

        WM_VIEW.max_lat = 90.0;

    }
}

static void WM_zoom_view_at(double focus_lon, double focus_lat, double factor) {
    /*
        Purpose: Zooms the view at
        Returns: No value
    */

    double lon_span = WM_VIEW.max_lon - WM_VIEW.min_lon;
    double lat_span = WM_VIEW.max_lat - WM_VIEW.min_lat;

    if (lon_span <= 0.0001) {

        lon_span = 360.0;

    }

    if (lat_span <= 0.0001) {

        lat_span = 180.0;

    }

    double focus_x = (focus_lon - WM_VIEW.min_lon) / lon_span;
    double focus_y = (WM_VIEW.max_lat - focus_lat) / lat_span;

    if (focus_x < 0.0) {

        focus_x = 0.0;

    }

    if (focus_x > 1.0) {

        focus_x = 1.0;

    }

    if (focus_y < 0.0) {

        focus_y = 0.0;

    }

    if (focus_y > 1.0) {

        focus_y = 1.0;

    }

    lon_span *= factor;
    lat_span *= factor;

    if (lon_span < 0.25) {

        lon_span = 0.25;

    }

    if (lat_span < 0.25) {

        lat_span = 0.25;

    }

    if (lon_span > 360.0) {

        lon_span = 360.0;

    }

    if (lat_span > 180.0) {

        lat_span = 180.0;

    }

    WM_VIEW.min_lon = focus_lon - focus_x * lon_span;
    WM_VIEW.max_lon = WM_VIEW.min_lon + lon_span;
    WM_VIEW.max_lat = focus_lat + focus_y * lat_span;
    WM_VIEW.min_lat = WM_VIEW.max_lat - lat_span;
    WM_clamp_view();
    WM_VIEW.zoomed_country = -1;
}

static void WM_pan_view_pixels(SDL_Rect map_rect, int dx, int dy) {
    /*
        Purpose: Pans the view pixels
        Returns: No value
    */

    if (map_rect.w <= 0 || map_rect.h <= 0) {

        return;

    }

    double lon_span = WM_VIEW.max_lon - WM_VIEW.min_lon;
    double lat_span = WM_VIEW.max_lat - WM_VIEW.min_lat;

    if (lon_span <= 0.0001) {

        lon_span = 360.0;

    }

    if (lat_span <= 0.0001) {

        lat_span = 180.0;

    }

    double dlon = -((double)dx / (double)map_rect.w) * lon_span;
    double dlat = ((double)dy / (double)map_rect.h) * lat_span;

    WM_VIEW.min_lon += dlon;
    WM_VIEW.max_lon += dlon;
    WM_VIEW.min_lat += dlat;
    WM_VIEW.max_lat += dlat;
    WM_clamp_view();
    WM_VIEW.zoomed_country = -1;
}

int WORLD_MAP_is_dragging(void) {
    /*
        Purpose: Reports whether the map is currently being dragged
        Returns: Boolean dragging state
    */

    return WM_VIEW.dragging;
}

void WORLD_MAP_get_drag_offset(int *out_x, int *out_y) {
    /*
        Purpose: Returns the temporary screen-space map offset used during dragging
        Returns: No value
    */

    if (out_x) {

        *out_x = WM_VIEW.dragging ? WM_VIEW.drag_offset_x : 0;

    }

    if (out_y) {

        *out_y = WM_VIEW.dragging ? WM_VIEW.drag_offset_y : 0;

    }
}

int WORLD_MAP_take_clicked_country(void) {
    /*
        Purpose: Returns a completed map click exactly once
        Returns: Country index, -1 for map background, or -2 when no click is pending
    */

    int country = WM_PENDING_CLICK_COUNTRY;
    WM_PENDING_CLICK_COUNTRY = -2;
    return country;
}

static void WM_clamp_drag_offset(SDL_Rect map_rect, int raw_dx, int raw_dy, int *out_dx, int *out_dy) {
    /*
        Purpose: Clamps the temporary drag offset to the geographic world bounds
        Returns: No value
    */

    int clamped_dx = raw_dx;
    int clamped_dy = raw_dy;

    if (map_rect.w > 0 && map_rect.h > 0) {

        double lon_span = WM_VIEW.max_lon - WM_VIEW.min_lon;
        double lat_span = WM_VIEW.max_lat - WM_VIEW.min_lat;

        if (lon_span > 0.0001) {

            double requested_dlon = -((double)raw_dx / (double)map_rect.w) * lon_span;
            double min_dlon = -180.0 - WM_VIEW.min_lon;
            double max_dlon = 180.0 - WM_VIEW.max_lon;

            if (requested_dlon < min_dlon) {

                requested_dlon = min_dlon;

            }

            if (requested_dlon > max_dlon) {

                requested_dlon = max_dlon;

            }

            clamped_dx = (int)llround(-(requested_dlon / lon_span) * (double)map_rect.w);

        }

        if (lat_span > 0.0001) {

            double requested_dlat = ((double)raw_dy / (double)map_rect.h) * lat_span;
            double min_dlat = -90.0 - WM_VIEW.min_lat;
            double max_dlat = 90.0 - WM_VIEW.max_lat;

            if (requested_dlat < min_dlat) {

                requested_dlat = min_dlat;

            }

            if (requested_dlat > max_dlat) {

                requested_dlat = max_dlat;

            }

            clamped_dy = (int)llround((requested_dlat / lat_span) * (double)map_rect.h);

        }

    }

    if (out_dx) {

        *out_dx = clamped_dx;

    }

    if (out_dy) {

        *out_dy = clamped_dy;

    }
}

static void WM_update_drag_position(SDL_Rect map_rect, int mouse_x, int mouse_y) {
    /*
        Purpose: Updates drag displacement from the newest physical mouse position
        Returns: No value
    */

    int total_dx = mouse_x - WM_VIEW.drag_start_x;
    int total_dy = mouse_y - WM_VIEW.drag_start_y;

    WM_clamp_drag_offset(map_rect, total_dx, total_dy, &WM_VIEW.drag_offset_x, &WM_VIEW.drag_offset_y);
    WM_VIEW.drag_last_x = mouse_x;
    WM_VIEW.drag_last_y = mouse_y;

    if (total_dx * total_dx + total_dy * total_dy > 16) {

        WM_VIEW.drag_moved = 1;

    }
}

static void WM_finish_drag(SDL_Rect map_rect, int released_x, int released_y, int allow_click) {
    /*
        Purpose: Commits a drag immediately and releases mouse capture
        Returns: No value
    */

    if (!WM_VIEW.dragging) {

        return;

    }

    WM_update_drag_position(map_rect, released_x, released_y);

    int final_dx = WM_VIEW.drag_offset_x;
    int final_dy = WM_VIEW.drag_offset_y;
    int should_click_zoom = allow_click && !WM_VIEW.drag_moved;

    if (!should_click_zoom && (final_dx != 0 || final_dy != 0)) {

        WM_pan_view_pixels(map_rect, final_dx, final_dy);
        /* Keep the oversized cache. WM_render_cache_source_rect() will reuse it
         * when possible and rebuild only when the committed view leaves it. */

    }

    WM_VIEW.dragging = 0;
    WM_VIEW.drag_moved = 0;
    WM_VIEW.drag_offset_x = 0;
    WM_VIEW.drag_offset_y = 0;
    WM_VIEW.drag_hover_country = -1;
    SDL_CaptureMouse(SDL_FALSE);

    if (should_click_zoom && released_x >= map_rect.x && released_x < map_rect.x + map_rect.w &&
        released_y >= map_rect.y && released_y < map_rect.y + map_rect.h) {

        int hover_country = WM_country_at(map_rect, released_x, released_y);
        WM_PENDING_CLICK_COUNTRY = hover_country;

        if (hover_country >= 0) {

            WM_zoom_to_country(hover_country, WM_LAST_HOVER_POLYGON, map_rect);

        }

    }
}

void WORLD_MAP_handle_event(const SDL_Event *event, SDL_Rect map_rect) {
    /*
        Purpose: Handles the event
        Returns: No value
    */

    if (!event) {

        return;

    }

    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE && event->key.repeat == 0) {

        WORLD_MAP_reset_view();
        return;

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mouse_x = 0;
        int mouse_y = 0;
        SDL_GetMouseState(&mouse_x, &mouse_y);

        if (mouse_x < map_rect.x || mouse_x >= map_rect.x + map_rect.w || mouse_y < map_rect.y ||
            mouse_y >= map_rect.y + map_rect.h) {

            return;

        }

        int16_t lon100 = 0;
        int16_t lat100 = 0;
        WM_screen_to_lonlat100(map_rect, mouse_x, mouse_y, &lon100, &lat100);

        double factor = event->wheel.y > 0 ? 0.82 : 1.22;

        if (event->wheel.y == 0) {

            return;

        }

        WM_zoom_view_at((double)lon100 / 100.0, (double)lat100 / 100.0, factor);
        return;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        if (event->button.x >= map_rect.x && event->button.x < map_rect.x + map_rect.w &&
            event->button.y >= map_rect.y && event->button.y < map_rect.y + map_rect.h) {

            WM_PENDING_CLICK_COUNTRY = -2;
            WM_VIEW.dragging = 1;
            WM_VIEW.drag_moved = 0;
            WM_VIEW.drag_start_x = event->button.x;
            WM_VIEW.drag_start_y = event->button.y;
            WM_VIEW.drag_last_x = event->button.x;
            WM_VIEW.drag_last_y = event->button.y;
            WM_VIEW.drag_offset_x = 0;
            WM_VIEW.drag_offset_y = 0;
            WM_VIEW.drag_hover_country = -1;
            SDL_CaptureMouse(SDL_TRUE);

        }
        return;

    }

    if (event->type == SDL_MOUSEMOTION && WM_VIEW.dragging) {

        int mouse_x = event->motion.x;
        int mouse_y = event->motion.y;
        Uint32 buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

        if (!(buttons & SDL_BUTTON_LMASK)) {

            WM_finish_drag(map_rect, WM_VIEW.drag_last_x, WM_VIEW.drag_last_y, 0);
            return;

        }

        /* Use the newest mouse position instead of replaying stale queued motion. */
        WM_update_drag_position(map_rect, mouse_x, mouse_y);
        return;

    }

    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT && WM_VIEW.dragging) {

        WM_finish_drag(map_rect, event->button.x, event->button.y, 1);
        return;

    }

    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_FOCUS_LOST && WM_VIEW.dragging) {

        WM_finish_drag(map_rect, WM_VIEW.drag_last_x, WM_VIEW.drag_last_y, 0);
        return;

    }
}

static int WM_country_at(SDL_Rect map_rect, int mouse_x, int mouse_y) {
    /*
        Purpose: Gets the country at a position
        Returns: Success status
    */

    WM_LAST_HOVER_POLYGON = -1;

    if (!WM_ensure_loaded()) {

        return -1;

    }

    if (mouse_x < map_rect.x || mouse_x >= map_rect.x + map_rect.w || mouse_y < map_rect.y ||
        mouse_y >= map_rect.y + map_rect.h) {

        return -1;

    }

    int16_t lon100 = 0;
    int16_t lat100 = 0;
    WM_screen_to_lonlat100(map_rect, mouse_x, mouse_y, &lon100, &lat100);

    for (int c = (int)WM_DATA.country_count - 1; c >= 0; c--) {
        const WM_Country *country = &WM_DATA.countries[c];

        if (lon100 < country->min_lon100 || lon100 > country->max_lon100 || lat100 < country->min_lat100 ||
            lat100 > country->max_lat100) {

            continue;

        }
        for (uint32_t k = 0; k < country->poly_count; k++) {
            uint32_t pi = country->poly_start + k;

            if (pi >= WM_DATA.polygon_count) {

                continue;

            }
            const WM_Polygon *poly = &WM_DATA.polygons[pi];

            if (lon100 < poly->min_lon100 || lon100 > poly->max_lon100 || lat100 < poly->min_lat100 ||
                lat100 > poly->max_lat100) {

                continue;

            }

            if (WM_point_in_polygon_lonlat(lon100, lat100, poly)) {

                WM_LAST_HOVER_POLYGON = (int)pi;
                return c;

            }
        }
    }

    return -1;
}

static void WM_fill_poly(SDL_Renderer *renderer, SDL_Point *pts, int n) {
    /*
        Purpose: Fills the polygon
        Returns: No value
    */

    if (!renderer || !pts || n < 3) {

        return;

    }
    int min_y = pts[0].y;
    int max_y = pts[0].y;
    for (int i = 1; i < n; i++) {

        if (pts[i].y < min_y) {

            min_y = pts[i].y;

        }

        if (pts[i].y > max_y) {

            max_y = pts[i].y;

        }
    }

    int nodes[WORLD_MAP_MAX_SCREEN_POINTS];
    for (int y = min_y; y <= max_y; y++) {
        int node_count = 0;
        int j = n - 1;
        for (int i = 0; i < n; i++) {

            if ((pts[i].y < y && pts[j].y >= y) || (pts[j].y < y && pts[i].y >= y)) {

                int denom = pts[j].y - pts[i].y;

                if (denom != 0 && node_count < WORLD_MAP_MAX_SCREEN_POINTS) {

                    nodes[node_count++] = pts[i].x + (y - pts[i].y) * (pts[j].x - pts[i].x) / denom;

                }

            }
            j = i;
        }
        for (int i = 0; i < node_count - 1; i++) {
            for (int k = i + 1; k < node_count; k++) {

                if (nodes[i] > nodes[k]) {

                    int tmp = nodes[i];
                    nodes[i] = nodes[k];
                    nodes[k] = tmp;

                }
            }
        }
        for (int i = 0; i < node_count - 1; i += 2) {
            SDL_RenderDrawLine(renderer, nodes[i], y, nodes[i + 1], y);
        }
    }
}

static int WM_detail_segment_intersects_view(const WM_DetailSegment *seg) {
    /*
        Purpose: Checks whether the detail segment intersects the view
        Returns: Success status
    */

    if (!seg) {

        return 0;

    }
    double min_lon = (double)seg->min_lon100 / 100.0;
    double max_lon = (double)seg->max_lon100 / 100.0;
    double min_lat = (double)seg->min_lat100 / 100.0;
    double max_lat = (double)seg->max_lat100 / 100.0;
    return !(max_lon < WM_VIEW.min_lon || min_lon > WM_VIEW.max_lon || max_lat < WM_VIEW.min_lat ||
             min_lat > WM_VIEW.max_lat);
}

static int WM_lon_jump_crosses_dateline(int16_t a_lon100, int16_t b_lon100) {
    /*
        Purpose: Checks whether the longitude jump crosses the dateline
        Returns: Success status
    */

    int diff = (int)a_lon100 - (int)b_lon100;

    if (diff < 0) {

        diff = -diff;

    }
    return diff > 18000;
}

static void WM_draw_detail_pair(SDL_Renderer *renderer, SDL_Rect map_rect, WM_Point a, WM_Point b) {
    /*
        Purpose: Draws the detail pair
        Returns: No value
    */

    if (WM_lon_jump_crosses_dateline(a.lon100, b.lon100)) {

        return;

    }

    int ax = 0;
    int ay = 0;
    int bx = 0;
    int by = 0;
    WM_project_point(a.lon100, a.lat100, map_rect, &ax, &ay);
    WM_project_point(b.lon100, b.lat100, map_rect, &bx, &by);

    SDL_RenderDrawLine(renderer, ax, ay, bx, by);
}

static void WM_draw_detail_lines(SDL_Renderer *renderer, SDL_Rect map_rect) {
    /*
        Purpose: Draws the detail lines
        Returns: No value
    */

    if (!renderer || !WM_DATA.detail_points || !WM_DATA.detail_segments) {

        return;

    }

    for (uint32_t s = 0; s < WM_DATA.detail_segment_count; s++) {
        WM_DetailSegment *seg = &WM_DATA.detail_segments[s];

        if (seg->count < 2 || seg->start >= WM_DATA.detail_point_count ||
            seg->start + seg->count > WM_DATA.detail_point_count) {

            continue;

        }

        if (!WM_detail_segment_intersects_view(seg)) {

            continue;

        }

        SDL_Color color =
            seg->layer == WM_DETAIL_LAYER_BORDER ? (SDL_Color){0, 210, 90, 210} : (SDL_Color){0, 255, 120, 230};
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

        for (uint32_t i = 1; i < seg->count; i++) {
            WM_Point a = WM_DATA.detail_points[seg->start + i - 1];
            WM_Point b = WM_DATA.detail_points[seg->start + i];
            WM_draw_detail_pair(renderer, map_rect, a, b);
        }
    }
}

static void WM_draw_hover_detail_lines(SDL_Renderer *renderer, SDL_Rect map_rect, int hover_country) {
    /*
        Purpose: Draws the hover detail lines
        Returns: No value
    */

    if (!renderer || hover_country < 0 || hover_country >= (int)WM_DATA.country_count) {

        return;

    }

    const WM_Country *country = &WM_DATA.countries[hover_country];
    SDL_SetRenderDrawColor(renderer, 0, 255, 95, 255);

    for (uint32_t k = 0; k < country->poly_count; k++) {
        uint32_t pi = country->poly_start + k;

        if (pi >= WM_DATA.polygon_count) {

            continue;

        }
        const WM_Polygon *poly = &WM_DATA.polygons[pi];

        if (poly->country != (uint16_t)hover_country) {

            continue;

        }

        if (!WM_polygon_intersects_view(poly)) {

            continue;

        }

        if (poly->count < 3 || poly->count > WORLD_MAP_MAX_SCREEN_POINTS) {

            continue;

        }

        if (poly->start + poly->count > WM_DATA.point_count) {

            continue;

        }

        SDL_Point pts[WORLD_MAP_MAX_SCREEN_POINTS];
        for (uint32_t i = 0; i < poly->count; i++) {
            WM_project_point(WM_DATA.points[poly->start + i].lon100, WM_DATA.points[poly->start + i].lat100, map_rect,
                             &pts[i].x, &pts[i].y);
        }

        for (uint32_t i = 1; i < poly->count; i++) {
            WM_Point a = WM_DATA.points[poly->start + i - 1];
            WM_Point b = WM_DATA.points[poly->start + i];

            if (!WM_lon_jump_crosses_dateline(a.lon100, b.lon100)) {

                SDL_RenderDrawLine(renderer, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y);

            }
        }

        WM_Point last = WM_DATA.points[poly->start + poly->count - 1];
        WM_Point first = WM_DATA.points[poly->start];

        if (!WM_lon_jump_crosses_dateline(last.lon100, first.lon100)) {

            SDL_RenderDrawLine(renderer, pts[poly->count - 1].x, pts[poly->count - 1].y, pts[0].x, pts[0].y);

        }
    }
}

static void WM_draw_polygon(SDL_Renderer *renderer, SDL_Rect map_rect, const WM_Polygon *poly, int fill,
                            SDL_Color fill_color, SDL_Color outline_color) {
    /*
        Purpose: Draws the polygon
        Returns: No value
    */

    if (!renderer || !poly || poly->count < 3 || poly->count > WORLD_MAP_MAX_SCREEN_POINTS) {

        return;

    }

    if (poly->start + poly->count > WM_DATA.point_count) {

        return;

    }

    SDL_Point pts[WORLD_MAP_MAX_SCREEN_POINTS];
    for (uint32_t i = 0; i < poly->count; i++) {
        WM_project_point(WM_DATA.points[poly->start + i].lon100, WM_DATA.points[poly->start + i].lat100, map_rect,
                         &pts[i].x, &pts[i].y);
    }

    SDL_SetRenderDrawBlendMode(renderer, WM_RENDERING_CACHE_TEXTURE ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);

    if (fill) {

        SDL_SetRenderDrawColor(renderer, fill_color.r, fill_color.g, fill_color.b, fill_color.a);
        WM_fill_poly(renderer, pts, (int)poly->count);

    }

    if (outline_color.a > 0) {

        SDL_SetRenderDrawColor(renderer, outline_color.r, outline_color.g, outline_color.b, outline_color.a);
        for (uint32_t i = 1; i < poly->count; i++) {
            WM_Point a = WM_DATA.points[poly->start + i - 1];
            WM_Point b = WM_DATA.points[poly->start + i];

            if (!WM_lon_jump_crosses_dateline(a.lon100, b.lon100)) {

                SDL_RenderDrawLine(renderer, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y);

            }
        }

    }
}

static void WM_fill_map_background(SDL_Renderer *renderer, SDL_Rect map_rect) {
    /*
        Purpose: Draws the map background
        Returns: No value
    */

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 5, 4, 255);
    SDL_RenderFillRect(renderer, &map_rect);
}

static void WM_draw_map_grid(SDL_Renderer *renderer, SDL_Rect map_rect) {
    /*
        Purpose: Draws the map grid for the current viewport
        Returns: No value
    */

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 42, 32, 120);
    for (int i = 0; i <= 12; i++) {
        int x = map_rect.x + (map_rect.w * i) / 12;
        SDL_RenderDrawLine(renderer, x, map_rect.y, x, map_rect.y + map_rect.h);
    }
    for (int i = 0; i <= 6; i++) {
        int y = map_rect.y + (map_rect.h * i) / 6;
        SDL_RenderDrawLine(renderer, map_rect.x, y, map_rect.x + map_rect.w, y);
    }
}

static void WM_render_base_map_layer(SDL_Renderer *renderer, SDL_Rect map_rect) {
    /*
        Purpose: Renders the non-interactive world-map geometry layer
        Returns: No value
    */

    SDL_RenderSetClipRect(renderer, &map_rect);
    for (uint32_t i = 0; i < WM_DATA.polygon_count; i++) {

        if (!WM_polygon_intersects_view(&WM_DATA.polygons[i])) {

            continue;

        }

        WM_draw_polygon(renderer, map_rect, &WM_DATA.polygons[i], 1, (SDL_Color){0, 35, 18, 135},
                        (SDL_Color){0, 85, 38, 115});
    }
    SDL_SetRenderDrawBlendMode(renderer, WM_RENDERING_CACHE_TEXTURE ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    WM_draw_detail_lines(renderer, map_rect);
    SDL_RenderSetClipRect(renderer, NULL);
}

static void WM_draw_hover_layer(SDL_Renderer *renderer, SDL_Rect map_rect, int hover_country) {
    /*
        Purpose: Draws the currently hovered country over the cached base map
        Returns: No value
    */

    if (hover_country < 0 || hover_country >= (int)WM_DATA.country_count) {

        return;

    }

    SDL_RenderSetClipRect(renderer, &map_rect);
    for (uint32_t i = 0; i < WM_DATA.polygon_count; i++) {
        const WM_Polygon *polygon = &WM_DATA.polygons[i];

        if (polygon->country != hover_country || !WM_polygon_intersects_view(polygon)) {

            continue;

        }

        WM_draw_polygon(renderer, map_rect, polygon, 1, (SDL_Color){0, 150, 65, 70}, (SDL_Color){0, 255, 95, 0});
    }
    WM_draw_hover_detail_lines(renderer, map_rect, hover_country);
    SDL_RenderSetClipRect(renderer, NULL);
}

static int WM_render_cache_factor(SDL_Renderer *renderer, SDL_Rect map_rect) {
    /*
        Purpose: Selects the largest safe overscan factor for the map texture
        Returns: Texture overscan factor
    */

    int factor = 3;
    SDL_RendererInfo info;
    memset(&info, 0, sizeof(info));

    if (SDL_GetRendererInfo(renderer, &info) != 0 || !(info.flags & SDL_RENDERER_TARGETTEXTURE)) {

        return 0;

    }

    while (factor > 1) {
        int texture_w = map_rect.w * factor;
        int texture_h = map_rect.h * factor;
        int width_ok = info.max_texture_width <= 0 || texture_w <= info.max_texture_width;
        int height_ok = info.max_texture_height <= 0 || texture_h <= info.max_texture_height;

        if (width_ok && height_ok) {

            break;

        }

        factor--;
    }

    return factor;
}

static int WM_render_cache_source_rect(SDL_Rect map_rect, SDL_Rect *source_rect) {
    /*
        Purpose: Calculates the visible source rectangle inside the cached texture
        Returns: Success status
    */

    if (!source_rect || !WM_RENDER_CACHE.valid || !WM_RENDER_CACHE.texture || WM_RENDER_CACHE.map_w != map_rect.w ||
        WM_RENDER_CACHE.map_h != map_rect.h) {

        return 0;

    }

    double lon_span = WM_VIEW.max_lon - WM_VIEW.min_lon;
    double lat_span = WM_VIEW.max_lat - WM_VIEW.min_lat;
    double lon_tolerance = fabs(WM_RENDER_CACHE.source_lon_span) * 1e-9 + 1e-9;
    double lat_tolerance = fabs(WM_RENDER_CACHE.source_lat_span) * 1e-9 + 1e-9;

    if (fabs(lon_span - WM_RENDER_CACHE.source_lon_span) > lon_tolerance ||
        fabs(lat_span - WM_RENDER_CACHE.source_lat_span) > lat_tolerance) {

        return 0;

    }

    double cache_lon_span = WM_RENDER_CACHE.max_lon - WM_RENDER_CACHE.min_lon;
    double cache_lat_span = WM_RENDER_CACHE.max_lat - WM_RENDER_CACHE.min_lat;

    if (cache_lon_span <= 0.0 || cache_lat_span <= 0.0) {

        return 0;

    }

    int source_x =
        (int)llround((WM_VIEW.min_lon - WM_RENDER_CACHE.min_lon) * (double)WM_RENDER_CACHE.texture_w / cache_lon_span);
    int source_y =
        (int)llround((WM_RENDER_CACHE.max_lat - WM_VIEW.max_lat) * (double)WM_RENDER_CACHE.texture_h / cache_lat_span);

    if (source_x < 0 || source_y < 0 || source_x + map_rect.w > WM_RENDER_CACHE.texture_w ||
        source_y + map_rect.h > WM_RENDER_CACHE.texture_h) {

        return 0;

    }

    *source_rect = (SDL_Rect){source_x, source_y, map_rect.w, map_rect.h};
    return 1;
}

static int WM_rebuild_render_cache(SDL_Renderer *renderer, SDL_Rect map_rect) {
    /*
        Purpose: Renders an oversized world map into an SDL texture
        Returns: Success status
    */

    if (!renderer || map_rect.w <= 0 || map_rect.h <= 0) {

        return 0;

    }

    int factor = WM_render_cache_factor(renderer, map_rect);

    if (factor <= 0) {

        WM_destroy_render_cache();
        return 0;

    }

    int texture_w = map_rect.w * factor;
    int texture_h = map_rect.h * factor;

    if (!WM_RENDER_CACHE.texture || WM_RENDER_CACHE.renderer != renderer || WM_RENDER_CACHE.texture_w != texture_w ||
        WM_RENDER_CACHE.texture_h != texture_h) {

        WM_destroy_render_cache();
        WM_RENDER_CACHE.texture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, texture_w, texture_h);

        if (!WM_RENDER_CACHE.texture) {

            return 0;

        }

        SDL_SetTextureBlendMode(WM_RENDER_CACHE.texture, SDL_BLENDMODE_BLEND);
        WM_RENDER_CACHE.renderer = renderer;
        WM_RENDER_CACHE.texture_w = texture_w;
        WM_RENDER_CACHE.texture_h = texture_h;

    }

    double source_lon_span = WM_VIEW.max_lon - WM_VIEW.min_lon;
    double source_lat_span = WM_VIEW.max_lat - WM_VIEW.min_lat;
    double center_lon = (WM_VIEW.min_lon + WM_VIEW.max_lon) * 0.5;
    double center_lat = (WM_VIEW.min_lat + WM_VIEW.max_lat) * 0.5;
    double cache_lon_span = source_lon_span * (double)factor;
    double cache_lat_span = source_lat_span * (double)factor;

    WM_RENDER_CACHE.min_lon = center_lon - cache_lon_span * 0.5;
    WM_RENDER_CACHE.max_lon = center_lon + cache_lon_span * 0.5;
    WM_RENDER_CACHE.min_lat = center_lat - cache_lat_span * 0.5;
    WM_RENDER_CACHE.max_lat = center_lat + cache_lat_span * 0.5;
    WM_RENDER_CACHE.source_lon_span = source_lon_span;
    WM_RENDER_CACHE.source_lat_span = source_lat_span;
    WM_RENDER_CACHE.map_w = map_rect.w;
    WM_RENDER_CACHE.map_h = map_rect.h;
    WM_RENDER_CACHE.valid = 0;

    SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
    SDL_Rect previous_viewport;
    SDL_Rect previous_clip;
    SDL_bool previous_clip_enabled = SDL_RenderIsClipEnabled(renderer);
    SDL_RenderGetViewport(renderer, &previous_viewport);
    SDL_RenderGetClipRect(renderer, &previous_clip);

    if (SDL_SetRenderTarget(renderer, WM_RENDER_CACHE.texture) != 0) {

        WM_destroy_render_cache();
        return 0;

    }

    SDL_RenderSetViewport(renderer, NULL);
    SDL_RenderSetClipRect(renderer, NULL);

    WM_View saved_view = WM_VIEW;
    WM_VIEW.min_lon = WM_RENDER_CACHE.min_lon;
    WM_VIEW.max_lon = WM_RENDER_CACHE.max_lon;
    WM_VIEW.min_lat = WM_RENDER_CACHE.min_lat;
    WM_VIEW.max_lat = WM_RENDER_CACHE.max_lat;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    WM_RENDERING_CACHE_TEXTURE = 1;
    WM_render_base_map_layer(renderer, (SDL_Rect){0, 0, texture_w, texture_h});
    WM_RENDERING_CACHE_TEXTURE = 0;
    WM_VIEW = saved_view;

    SDL_SetRenderTarget(renderer, previous_target);
    SDL_RenderSetViewport(renderer, &previous_viewport);

    if (previous_clip_enabled) {

        SDL_RenderSetClipRect(renderer, &previous_clip);

    }

    else {

        SDL_RenderSetClipRect(renderer, NULL);

    }

    WM_RENDER_CACHE.valid = 1;
    return 1;
}

static int WM_draw_cached_base_map(SDL_Renderer *renderer, SDL_Rect map_rect) {
    /*
        Purpose: Copies the visible world-map region from the render cache
        Returns: Success status
    */

    SDL_Rect source_rect;

    if (!WM_render_cache_source_rect(map_rect, &source_rect)) {

        if (!WM_rebuild_render_cache(renderer, map_rect) || !WM_render_cache_source_rect(map_rect, &source_rect)) {

            return 0;

        }

    }

    return SDL_RenderCopy(renderer, WM_RENDER_CACHE.texture, &source_rect, &map_rect) == 0;
}

static int WM_draw_dragged_cached_base_map(SDL_Renderer *renderer, SDL_Rect map_rect) {
    /*
        Purpose: Shifts the frozen map texture during a drag without changing the geographic view
        Returns: Success status
    */

    SDL_Rect source_rect;

    if (!WM_render_cache_source_rect(map_rect, &source_rect)) {

        if (!WM_rebuild_render_cache(renderer, map_rect) || !WM_render_cache_source_rect(map_rect, &source_rect)) {

            return 0;

        }

    }

    source_rect.x -= WM_VIEW.drag_offset_x;
    source_rect.y -= WM_VIEW.drag_offset_y;

    SDL_Rect clipped_source = source_rect;
    SDL_Rect destination = map_rect;

    if (clipped_source.x < 0) {

        int clipped = -clipped_source.x;
        clipped_source.x = 0;
        clipped_source.w -= clipped;
        destination.x += clipped;
        destination.w -= clipped;

    }

    if (clipped_source.y < 0) {

        int clipped = -clipped_source.y;
        clipped_source.y = 0;
        clipped_source.h -= clipped;
        destination.y += clipped;
        destination.h -= clipped;

    }

    if (clipped_source.x + clipped_source.w > WM_RENDER_CACHE.texture_w) {

        clipped_source.w = WM_RENDER_CACHE.texture_w - clipped_source.x;
        destination.w = clipped_source.w;

    }

    if (clipped_source.y + clipped_source.h > WM_RENDER_CACHE.texture_h) {

        clipped_source.h = WM_RENDER_CACHE.texture_h - clipped_source.y;
        destination.h = clipped_source.h;

    }

    if (clipped_source.w <= 0 || clipped_source.h <= 0 || destination.w <= 0 || destination.h <= 0) {

        return 0;

    }

    return SDL_RenderCopy(renderer, WM_RENDER_CACHE.texture, &clipped_source, &destination) == 0;
}

static void WM_load_flag(SDL_Renderer *renderer, WM_Country *country, const char *flags_dir) {
    /*
        Purpose: Loads the flag
        Returns: No value
    */

    if (!renderer || !country || country->flag_attempted) {

        return;

    }
    country->flag_attempted = 1;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", flags_dir ? flags_dir : "flags", country->alpha2 ? country->alpha2 : "");

    SDL_Surface *surf = IMG_Load(path);

    if (!surf) {

        return;

    }

    country->flag_texture = SDL_CreateTextureFromSurface(renderer, surf);
    country->flag_w = surf->w;
    country->flag_h = surf->h;
    SDL_FreeSurface(surf);
}

static void WM_draw_sidebar(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect sidebar, int hover_country,
                            const char *flags_dir) {
    /*
        Purpose: Draws the sidebar
        Returns: No value
    */

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 235);
    SDL_RenderFillRect(renderer, &sidebar);
    SDL_SetRenderDrawColor(renderer, 0, 180, 70, 230);
    SDL_RenderDrawRect(renderer, &sidebar);

    int x = sidebar.x + 18;
    int y = sidebar.y + 18;
    WM_draw_text(renderer, font, "COUNTRY", x, y, (SDL_Color){0, 255, 90, 255});
    y += 42;

    if (hover_country < 0 || hover_country >= (int)WM_DATA.country_count) {

        WM_draw_text_wrapped(renderer, font, "Hover over a country", (SDL_Rect){x, y, sidebar.w - 36, 70},
                             (SDL_Color){120, 160, 135, 255});
        return;

    }

    WM_Country *country = &WM_DATA.countries[hover_country];
    WM_draw_text_wrapped(renderer, font, country->name, (SDL_Rect){x, y, sidebar.w - 36, 80},
                         (SDL_Color){0, 255, 90, 255});
    y += 88;

    WM_draw_text(renderer, font, country->alpha2, x, y, (SDL_Color){0, 180, 70, 255});
    y += 38;

    WM_load_flag(renderer, country, flags_dir);

    SDL_Rect flag_box = {x, y, sidebar.w - 36, 150};
    SDL_SetRenderDrawColor(renderer, 0, 12, 4, 255);
    SDL_RenderFillRect(renderer, &flag_box);
    SDL_SetRenderDrawColor(renderer, 0, 120, 50, 255);
    SDL_RenderDrawRect(renderer, &flag_box);

    if (country->flag_texture && country->flag_w > 0 && country->flag_h > 0) {

        double scale_x = (double)(flag_box.w - 24) / (double)country->flag_w;
        double scale_y = (double)(flag_box.h - 24) / (double)country->flag_h;
        double scale = scale_x < scale_y ? scale_x : scale_y;
        int fw = (int)((double)country->flag_w * scale);
        int fh = (int)((double)country->flag_h * scale);
        SDL_Rect dst = {flag_box.x + (flag_box.w - fw) / 2, flag_box.y + (flag_box.h - fh) / 2, fw, fh};
        SDL_RenderCopy(renderer, country->flag_texture, NULL, &dst);

    }

    else {

        WM_draw_text_wrapped(renderer, font, "Flag image missing. Run python3 download_world_flags.py",
                             (SDL_Rect){flag_box.x + 12, flag_box.y + 18, flag_box.w - 24, flag_box.h - 36},
                             (SDL_Color){150, 150, 150, 255});

    }
}

void WORLD_MAP_draw(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect map_rect, SDL_Rect sidebar_rect, int mouse_x,
                    int mouse_y, const char *flags_dir) {
    /*
        Purpose: Draws the requested operation
        Returns: No value
    */

    if (!renderer) {

        return;

    }

    if (!WM_ensure_loaded()) {

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer, &map_rect);
        WM_draw_text_wrapped(renderer, font, "world_map.bin missing or invalid", map_rect,
                             (SDL_Color){255, 80, 80, 255});
        return;

    }

    int hover = WM_VIEW.dragging ? WM_VIEW.drag_hover_country : WM_country_at(map_rect, mouse_x, mouse_y);

    WM_fill_map_background(renderer, map_rect);
    WM_draw_map_grid(renderer, map_rect);

    if (WM_VIEW.dragging) {

        if (!WM_draw_dragged_cached_base_map(renderer, map_rect)) {

            WM_render_base_map_layer(renderer, map_rect);

        }

    }

    else {

        if (!WM_draw_cached_base_map(renderer, map_rect)) {

            WM_render_base_map_layer(renderer, map_rect);

        }

        WM_draw_hover_layer(renderer, map_rect, hover);

    }

    SDL_SetRenderDrawColor(renderer, 0, 180, 70, 255);
    SDL_RenderDrawRect(renderer, &map_rect);
    WM_draw_sidebar(renderer, font, sidebar_rect, hover, flags_dir);
}

#ifndef WORLD_MAP_NO_DEMO
static TTF_Font *WM_load_font(int size) {
    /*
        Purpose: Loads the font
        Returns: Font pointer
    */

    const char *paths[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                           "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf", NULL};
    for (int i = 0; paths[i]; i++) {
        TTF_Font *f = TTF_OpenFont(paths[i], size);

        if (f) {

            return f;

        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    /*
        Purpose: Runs the RetroSpectrum application
        Returns: Process exit status
    */

    const char *bin_path = argc > 1 ? argv[1] : "world_map.bin";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {

        return 1;

    }

    if (TTF_Init() != 0) {

        return 1;

    }
    IMG_Init(IMG_INIT_PNG);

    if (!WORLD_MAP_load(bin_path)) {

        fprintf(stderr, "Failed to load %s\n", bin_path);
        return 1;

    }

    SDL_Window *win = SDL_CreateWindow("World Map Hover Flags - Binary Data", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 1400, 780, SDL_WINDOW_RESIZABLE);

    if (!win) {

        return 1;

    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!ren) {

        return 1;

    }

    TTF_Font *font = WM_load_font(18);
    int running = 1;

    while (running) {
        int w = 0;
        int h = 0;
        int mx = 0;
        int my = 0;
        SDL_GetWindowSize(win, &w, &h);
        SDL_Rect sidebar = {w - WORLD_MAP_SIDEBAR_W - 20, 20, WORLD_MAP_SIDEBAR_W, h - 40};
        SDL_Rect map = {20, 20, w - WORLD_MAP_SIDEBAR_W - 60, h - 40};

        SDL_Event e;
        while (SDL_PollEvent(&e)) {

            if (e.type == SDL_QUIT) {

                running = 0;

            }
            WORLD_MAP_handle_event(&e, map);
        }

        SDL_GetMouseState(&mx, &my);

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);

        WORLD_MAP_draw(ren, font, map, sidebar, mx, my, "flags");

        SDL_RenderPresent(ren);
    }

    WORLD_MAP_free();

    if (font) {

        TTF_CloseFont(font);

    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
#endif
