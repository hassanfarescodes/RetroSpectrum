/*
 * ============================================================================
 * File:            MapDashboard.h
 * Author:          Hassan Fares
 *
 * Description:     Public dashboard/map interface for RetroSpectrum.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#ifndef MAP_DASHBOARD_H
#define MAP_DASHBOARD_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#ifndef RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#endif

typedef enum Type_Dashboard_Event {
    DASHBOARD_EVENT_NONE = 0,
    DASHBOARD_EVENT_QUIT,
    DASHBOARD_EVENT_RETROSPECTRUM,
    DASHBOARD_EVENT_ANALYSIS,
    DASHBOARD_EVENT_CLASSIFICATION,
    DASHBOARD_EVENT_DECODE,
    DASHBOARD_EVENT_CASE_MANAGEMENT,
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

int dashboard_init(Type_Dashboard_State *dashboard, const char *map_bin_path);
void dashboard_shutdown(void);

int dashboard_handle_top_tab_event(Type_Dashboard_State *dashboard,
                                   const SDL_Event *event,
                                   int win_w,
                                   int text_entry_active);

int dashboard_handle_event(Type_Dashboard_State *dashboard,
                           const SDL_Event *event,
                           int win_w,
                           int win_h);

void dashboard_draw(Type_Dashboard_State *dashboard,
                    SDL_Renderer *renderer,
                    TTF_Font *font_small,
                    TTF_Font *font_medium,
                    int win_w,
                    int win_h,
                    int mouse_x,
                    int mouse_y);

void dashboard_draw_top_bar(SDL_Renderer *renderer,
                            TTF_Font *font_small,
                            TTF_Font *font_medium,
                            int win_w,
                            int mouse_x,
                            int mouse_y,
                            int active_tab);

#endif
