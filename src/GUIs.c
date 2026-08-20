/*
 * ============================================================================
 * File:            GUIs.c
 * Author:          Hassan Fares
 *
 * Description:     Shared graphical interface helper functions for RetroSpectrum
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux
 *
 *                                                               05/04/2026
 * ============================================================================
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "AnalysisWorkstation.h"
#include "GUIs.h"
#include "IQs.h"

#define CONTROL_PANEL_HEIGHT 95
#define AXIS_HEIGHT 70
#define MARGIN 20

#define REL_MIN_DB 2.0
#define REL_MAX_DB 22.0

TTF_Font *load_font(int size) {
    /*
        Purpose: Loads a font at the requested size
        Returns: Font pointer
    */

    const char *paths[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                           "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf", NULL};

    for (int i = 0; paths[i] != NULL; i++) {
        TTF_Font *font = TTF_OpenFont(paths[i], size);

        if (font) {

            return font;

        }
    }

    return NULL;
}

void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    /*
        Purpose: Draws the text
        Returns: No value
    */

    if (!font || !text || text[0] == '\0') {

        return;

    }

    SDL_Surface *surface = TTF_RenderText_Blended(font, text, color);

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

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    /*
        Purpose: Builds a packed RGB color value
        Returns: Packed color value
    */

    return 0xFF000000U | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void toggle_fullscreen(SDL_Window *window) {
    /*
        Purpose: Toggles the fullscreen
        Returns: No value
    */

    Global_Fullscreen = !Global_Fullscreen;

    SDL_SetWindowFullscreen(window, Global_Fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void set_status(const char *msg, SDL_Color color) {
    /*
        Purpose: Sets the status
        Returns: No value
    */

    snprintf(Global_Status_Msg, sizeof(Global_Status_Msg), "%s", msg);

    Global_Status_Color = color;
}

void draw_filled_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Draws the filled rectangle
        Returns: No value
    */

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void draw_outline_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Draws the outline rectangle
        Returns: No value
    */

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

void draw_made_in_usa(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the Made in USA label
        Returns: No value
    */

    (void)win_w;

    if (!renderer || !font) {

        return;

    }

    /*
     * Flag size.
     * Official US flag ratio is 19:10.
     */
    int flag_h = 40;
    int flag_w = (flag_h * 19) / 10;

    int flag_x = 24;
    int flag_y = win_h - flag_h - 8;

    int text_x = flag_x + 90;
    int text_y = flag_y + 12;

    SDL_Color text_color = {0, 255, 80, 255};
    draw_text(renderer, font, "Made in the USA", text_x, text_y, text_color);

    /*
     * White background.
     */
    SDL_Rect flag_bg = {flag_x, flag_y, flag_w, flag_h};
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &flag_bg);

    /*
     * 13 stripes.
     */
    for (int i = 0; i < 13; i++) {
        int stripe_y0 = flag_y + (i * flag_h) / 13;
        int stripe_y1 = flag_y + ((i + 1) * flag_h) / 13;

        SDL_Rect stripe = {flag_x, stripe_y0, flag_w, stripe_y1 - stripe_y0};

        if ((i % 2) == 0) {

            SDL_SetRenderDrawColor(renderer, 191, 10, 48, 255);

        }

        else {

            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        }

        SDL_RenderFillRect(renderer, &stripe);
    }

    /*
     * Blue canton.
     */
    int canton_w = (int)(flag_w * 0.40);
    int canton_h = (7 * flag_h) / 13;

    SDL_Rect canton = {flag_x, flag_y, canton_w, canton_h};

    SDL_SetRenderDrawColor(renderer, 0, 40, 104, 255);
    SDL_RenderFillRect(renderer, &canton);

    /*
     * Stars as subtle single-pixel dots.
     * 9 rows: 6,5,6,5,6,5,6,5,6.
     */
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    int top_margin = 2;
    int bottom_margin = 3;
    int left_margin = 2;
    int right_margin = 3;

    int usable_h = canton.h - top_margin - bottom_margin;
    int usable_w = canton.w - left_margin - right_margin;

    for (int row = 0; row < 9; row++) {
        int stars_in_row = (row % 2 == 0) ? 6 : 5;
        int py = canton.y + top_margin + (row * usable_h) / 8;

        for (int col = 0; col < stars_in_row; col++) {
            int px;

            if (stars_in_row == 6) {

                px = canton.x + left_margin + (col * usable_w) / 5;

            }

            else {

                px = canton.x + left_margin + (usable_w / 10) + (col * usable_w) / 5;

            }

            SDL_RenderDrawPoint(renderer, px, py);
        }
    }
}

void draw_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, int active,
                 int is_record_button) {
    /*
        Purpose: Draws the button
        Returns: No value
    */

    SDL_Color fill, border, text;

    if (is_record_button) {

        fill = active ? (SDL_Color){130, 0, 0, 255} : (SDL_Color){0, 8, 3, 255};
        border = active ? (SDL_Color){255, 80, 60, 255} : (SDL_Color){0, 100, 40, 255};
        text = active ? (SDL_Color){255, 130, 110, 255} : (SDL_Color){0, 180, 70, 255};

    }

    else {

        fill = active ? (SDL_Color){0, 70, 25, 255} : (SDL_Color){0, 8, 3, 255};
        border = active ? (SDL_Color){255, 60, 40, 255} : (SDL_Color){0, 180, 60, 255};
        text = active ? (SDL_Color){255, 70, 50, 255} : (SDL_Color){0, 255, 90, 255};

    }

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);

    int text_w = 0;
    int text_h = 0;

    if (font && TTF_SizeText(font, label, &text_w, &text_h) != 0) {

        text_w = 0;
        text_h = 0;

    }

    int text_x = rect.x + (rect.w - text_w) / 2;
    int text_y = rect.y + (rect.h - text_h) / 2;

    draw_text(renderer, font, label, text_x, text_y, text);
}

int point_in_rect(int x, int y, SDL_Rect r) {
    /*
        Purpose: Checks whether a point lies inside a rectangle
        Returns: Boolean status
    */

    return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
}

int near_px(int a, int b, int tolerance) {
    /*
        Purpose: Checks whether two pixel positions are near each other
        Returns: Boolean status
    */

    return abs(a - b) <= tolerance;
}

void draw_input_box(SDL_Renderer *renderer, TTF_Font *font, Type_Input_Box *box, int active) {
    /*
        Purpose: Draws the input box
        Returns: No value
    */

    SDL_Color border = active ? (SDL_Color){0, 255, 80, 255} : (SDL_Color){0, 100, 40, 255};
    SDL_Color fill = {0, 10, 3, 255};
    SDL_Color label = {0, 210, 70, 255};
    SDL_Color text = {0, 255, 90, 255};

    draw_text(renderer, font, box->label, box->rect.x, box->rect.y - 22, label);
    draw_filled_rect(renderer, box->rect, fill);
    draw_outline_rect(renderer, box->rect, border);
    draw_text(renderer, font, box->text, box->rect.x + 8, box->rect.y + 10, text);
}

void draw_checkbox(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, int checked) {
    /*
        Purpose: Draws the checkbox
        Returns: No value
    */

    draw_outline_rect(renderer, rect, (SDL_Color){0, 255, 80, 255});

    if (checked) {

        SDL_Rect inner = {rect.x + 5, rect.y + 5, rect.w - 10, rect.h - 10};
        draw_filled_rect(renderer, inner, (SDL_Color){0, 255, 80, 255});

    }

    draw_text(renderer, font, label, rect.x + rect.w + 8, rect.y + 2, (SDL_Color){0, 220, 70, 255});
}

void layout_controls(int win_w, Type_Input_Box *freq_box, Type_Input_Box *sr_box, Type_Input_Box *display_box,
                     Type_Input_Box *lna_box, Type_Input_Box *vga_box, Type_Input_Box *fps_box,
                     Type_Input_Box *rows_box, SDL_Rect *amp_box, SDL_Rect *dc_box, SDL_Rect *sel_button,
                     SDL_Rect *rec_button) {
    /*
        Purpose: Computes the main control layout
        Returns: No value
    */

    int y = 42;
    int x = MARGIN + 20;
    int box_h = 42;
    int gap = 12;

    freq_box->rect = (SDL_Rect){x, y, 145, box_h};
    x += 145 + gap;
    sr_box->rect = (SDL_Rect){x, y, 145, box_h};
    x += 145 + gap;
    display_box->rect = (SDL_Rect){x, y, 155, box_h};
    x += 155 + gap;
    lna_box->rect = (SDL_Rect){x, y, 82, box_h};
    x += 82 + gap;
    vga_box->rect = (SDL_Rect){x, y, 82, box_h};
    x += 82 + gap;
    fps_box->rect = (SDL_Rect){x, y, 95, box_h};
    x += 95 + gap;
    rows_box->rect = (SDL_Rect){x, y, 105, box_h};

    *amp_box = (SDL_Rect){win_w - 410, y - 8, 22, 22};
    *dc_box = (SDL_Rect){win_w - 410, y + 22, 22, 22};
    *sel_button = (SDL_Rect){win_w - 260, y, 105, box_h};
    *rec_button = (SDL_Rect){win_w - 140, y, 105, box_h};
}

void draw_control_panel(SDL_Renderer *renderer, TTF_Font *font, int win_w, Type_Input_Box *freq_box,
                        Type_Input_Box *sr_box, Type_Input_Box *display_box, Type_Input_Box *lna_box,
                        Type_Input_Box *vga_box, Type_Input_Box *fps_box, Type_Input_Box *rows_box, SDL_Rect amp_box,
                        SDL_Rect dc_box, SDL_Rect sel_button, SDL_Rect rec_button, Type_Active_Fields active) {
    /*
        Purpose: Draws the control panel
        Returns: No value
    */

    SDL_Rect panel = {MARGIN, 8, win_w - 2 * MARGIN, CONTROL_PANEL_HEIGHT - 8};

    draw_filled_rect(renderer, panel, (SDL_Color){0, 0, 0, 255});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 90, 35, 255});

    draw_input_box(renderer, font, freq_box, active == FIELD_FREQ);
    draw_input_box(renderer, font, sr_box, active == FIELD_SR);
    draw_input_box(renderer, font, display_box, active == FIELD_DISPLAY);
    draw_input_box(renderer, font, lna_box, active == FIELD_LNA);
    draw_input_box(renderer, font, vga_box, active == FIELD_VGA);
    draw_input_box(renderer, font, fps_box, active == FIELD_FPS);
    draw_input_box(renderer, font, rows_box, active == FIELD_ROWS);

    draw_checkbox(renderer, font, amp_box, "AGC / Amp", Global_Amp_Enable);
    draw_checkbox(renderer, font, dc_box, "DC Correction", Global_DC_Enable);
    draw_button(renderer, font, sel_button, "Selector", Global_Selector.enabled, 0);
    draw_button(renderer, font, rec_button, "RECORD", Global_Rec, 1);
}

void draw_frequency_axis(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect waterfall_rect) {
    /*
        Purpose: Draws the frequency axis
        Returns: No value
    */

    int axis_y = waterfall_rect.y + waterfall_rect.h + 14;

    SDL_SetRenderDrawColor(renderer, 0, 220, 70, 255);
    SDL_RenderDrawLine(renderer, waterfall_rect.x, axis_y, waterfall_rect.x + waterfall_rect.w, axis_y);

    double start_hz = Global_Center_Freq_Hz - Global_Display_Span_Hz / 2.0;
    double span_hz = Global_Display_Span_Hz;

    int ticks = 10;

    for (int t = 0; t <= ticks; t++) {
        double frac = (double)t / ticks;
        int x = waterfall_rect.x + (int)(frac * waterfall_rect.w);
        double freq_mhz = (start_hz + frac * span_hz) / 1e6;

        SDL_RenderDrawLine(renderer, x, axis_y - 8, x, axis_y + 8);

        char label[32];

        if (Global_Display_Span_Hz < 1000000) {

            snprintf(label, sizeof(label), "%.4f", freq_mhz);

        }

        else {

            snprintf(label, sizeof(label), "%.3f", freq_mhz);

        }

        int label_x = x - 24;

        if (label_x < waterfall_rect.x) {

            label_x = waterfall_rect.x;

        }

        if (label_x > waterfall_rect.x + waterfall_rect.w - 70) {

            label_x = waterfall_rect.x + waterfall_rect.w - 70;

        }

        draw_text(renderer, font, label, label_x, axis_y + 12, (SDL_Color){0, 220, 70, 255});
    }
}

void draw_border(SDL_Renderer *renderer, SDL_Rect r) {
    /*
        Purpose: Draws the border
        Returns: No value
    */

    draw_outline_rect(renderer, r, (SDL_Color){0, 180, 60, 255});
}

void draw_selection_overlay(SDL_Renderer *renderer, SDL_Rect waterfall_rect) {
    /*
        Purpose: Draws the selection overlay
        Returns: No value
    */

    if (!Global_Selector.enabled) {

        return;

    }

    double x0f = limit_double(Global_Selector.X0, 0.0, 1.0);
    double x1f = limit_double(Global_Selector.X1, 0.0, 1.0);

    if (x1f < x0f) {

        double tmp = x0f;
        x0f = x1f;
        x1f = tmp;

    }

    int x0 = waterfall_rect.x + (int)(x0f * waterfall_rect.w);
    int x1 = waterfall_rect.x + (int)(x1f * waterfall_rect.w);

    if (x1 <= x0) {

        x1 = x0 + 1;

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect sel_rect = {x0, waterfall_rect.y, x1 - x0, waterfall_rect.h};

    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 50);
    SDL_RenderFillRect(renderer, &sel_rect);

    SDL_SetRenderDrawColor(renderer, 230, 230, 230, 180);
    SDL_RenderDrawRect(renderer, &sel_rect);

    int mid = (x0 + x1) / 2;

    SDL_SetRenderDrawColor(renderer, 255, 25, 20, 240);
    SDL_RenderDrawLine(renderer, mid, waterfall_rect.y, mid, waterfall_rect.y + waterfall_rect.h);

    SDL_Rect left_handle = {x0 - 3, waterfall_rect.y, 6, waterfall_rect.h};
    SDL_Rect right_handle = {x1 - 3, waterfall_rect.y, 6, waterfall_rect.h};

    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 145);
    SDL_RenderFillRect(renderer, &left_handle);
    SDL_RenderFillRect(renderer, &right_handle);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void draw_selector_bandwidth(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect waterfall_rect) {
    /*
        Purpose: Draws the selector bandwidth
        Returns: No value
    */

    if (!Global_Selector.enabled) {

        return;

    }

    uint32_t bw_hz = selection_BW_Hz();
    uint64_t center_hz = selection_center_Hz();

    char msg[128];

    if (bw_hz >= 1000000) {

        snprintf(msg, sizeof(msg), "Selector: %.6f MHz | BW: %.3f MHz", center_hz / 1e6, bw_hz / 1e6);

    }

    else {

        snprintf(msg, sizeof(msg), "Selector: %.6f MHz | BW: %.3f kHz", center_hz / 1e6, bw_hz / 1e3);

    }

    int text_w = 0;
    int text_h = 0;

    if (font && TTF_SizeText(font, msg, &text_w, &text_h) != 0) {

        text_w = 0;
        text_h = 0;

    }

    int x = waterfall_rect.x + 12;
    int y = waterfall_rect.y + 12;

    SDL_Rect bg = {x - 6, y - 4, text_w + 12, text_h + 8};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, bg, (SDL_Color){0, 0, 0, 180});
    draw_outline_rect(renderer, bg, (SDL_Color){0, 180, 60, 220});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    draw_text(renderer, font, msg, x, y, (SDL_Color){0, 255, 90, 255});
}

void update_selection_from_mouse(int mouse_x, SDL_Rect waterfall_rect) {
    /*
        Purpose: Updates the selection from mouse
        Returns: No value
    */

    double frac = (double)(mouse_x - waterfall_rect.x) / (double)waterfall_rect.w;
    frac = limit_double(frac, 0.0, 1.0);

    if (Global_Selector.resizing_left) {

        Global_Selector.X0 = frac;

        if (Global_Selector.X0 > Global_Selector.X1 - 0.002) {

            Global_Selector.X0 = Global_Selector.X1 - 0.002;

        }

        if (Global_Selector.X0 < 0.0) {

            Global_Selector.X0 = 0.0;

        }

    }

    else if (Global_Selector.resizing_right) {

        Global_Selector.X1 = frac;

        if (Global_Selector.X1 < Global_Selector.X0 + 0.002) {

            Global_Selector.X1 = Global_Selector.X0 + 0.002;

        }

        if (Global_Selector.X1 > 1.0) {

            Global_Selector.X1 = 1.0;

        }

    }

    else if (Global_Selector.dragging) {

        double width = Global_Selector.X1 - Global_Selector.X0;
        double new_x0 = frac - width * 0.5;
        double new_x1 = frac + width * 0.5;

        if (new_x0 < 0.0) {

            new_x1 -= new_x0;
            new_x0 = 0.0;

        }

        if (new_x1 > 1.0) {

            double excess = new_x1 - 1.0;
            new_x0 -= excess;
            new_x1 = 1.0;

        }

        Global_Selector.X0 = limit_double(new_x0, 0.0, 1.0);
        Global_Selector.X1 = limit_double(new_x1, 0.0, 1.0);

    }
}

void draw_antenna_recommendation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the antenna recommendation
        Returns: No value
    */

    double length_in = recommended_antenna_length_inches(Global_Center_Freq_Hz);

    char msg[96];
    snprintf(msg, sizeof(msg), "Recommended antenna length: %.2f in", length_in);

    int text_w = 0;
    int text_h = 0;

    if (font && TTF_SizeText(font, msg, &text_w, &text_h) != 0) {

        text_w = 0;
        text_h = 0;

    }

    int x = win_w - text_w - 24;
    int y = win_h - 32;

    draw_text(renderer, font, msg, x, y, (SDL_Color){0, 220, 70, 255});
}

// ==========================
// Waterfall and Input Helpers
// ==========================

uint32_t power_to_color_relative(double rel_db, double delta_db, double peakness_db) {
    /*

    Purpose: Maps relative signal power to a waterfall color

    Return: RGB color

    */

    if (rel_db < REL_MIN_DB) {

        return rgb(0, 0, 0);

    }

    double norm = (rel_db - REL_MIN_DB) / (REL_MAX_DB - REL_MIN_DB);

    if (norm < 0.0) {

        norm = 0.0;

    }

    if (norm > 1.0) {

        norm = 1.0;

    }

    uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.30) {

        double t = norm / 0.30;
        g = (uint8_t)(10 + 55 * t);

    }

    else if (norm < 0.70) {

        double t = (norm - 0.30) / 0.40;
        g = (uint8_t)(65 + 135 * t);

    }

    else {

        double t = (norm - 0.70) / 0.30;
        r = (uint8_t)(0 + 35 * t);
        g = (uint8_t)(200 + 55 * t);
        b = (uint8_t)(0 + 15 * t);

    }

    double event_strength = 0.0;

    if (delta_db > 10.0) {

        event_strength += (delta_db - 10.0) / 30.0;

    }

    if (peakness_db > 12.0) {

        event_strength += (peakness_db - 12.0) / 30.0;

    }

    if (rel_db > 34.0) {

        event_strength += (rel_db - 34.0) / 30.0;

    }

    if (event_strength > 1.0) {

        event_strength = 1.0;

    }

    if (event_strength < 0.10) {

        event_strength = 0.0;

    }

    if (event_strength > 0.0) {

        uint8_t er = 255;
        uint8_t eg = (uint8_t)(100 * (1.0 - event_strength));
        uint8_t eb = 0;

        r = (uint8_t)((1.0 - event_strength) * r + event_strength * er);
        g = (uint8_t)((1.0 - event_strength) * g + event_strength * eg);
        b = (uint8_t)((1.0 - event_strength) * b + event_strength * eb);

    }

    return rgb(r, g, b);
}

void clear_waterfall(uint32_t *pixels, int w, int h) {
    /*
        Purpose: Clears a waterfall pixel buffer
        Returns: No value
    */

    for (int i = 0; i < w * h; i++) {
        pixels[i] = rgb(0, 0, 0);
    }
}

void reset_prev_col_db(int tex_w) {
    /*
        Purpose: Resets the per-column waterfall baseline values
        Returns: No value
    */

    if (!Global_Color_Baseline) {

        return;

    }
    for (int i = 0; i < tex_w; i++) {
        Global_Color_Baseline[i] = -300.0;
    }
}

void append_text(char *dst, size_t dst_sz, const char *src) {
    /*
        Purpose: Appends numeric input characters into an input string
        Returns: No value
    */

    size_t len = strlen(dst);

    while (*src && len + 1 < dst_sz) {
        char c = *src++;

        if ((c >= '0' && c <= '9') || c == '.') {

            dst[len++] = c;

        }
    }

    dst[len] = '\0';
}

void backspace_text(char *dst) {
    /*
        Purpose: Removes the last character from an input string
        Returns: No value
    */

    size_t len = strlen(dst);

    if (len > 0) {

        dst[len - 1] = '\0';

    }
}

// ======================
// Live Waterfall GUI Render Helpers
// ======================

int cmp_double_for_qsort(const void *a, const void *b) {
    /*

    Purpose: Compares two double values for sorting

    Return: Sort order

    */
    double da = *(const double *)a;
    double db = *(const double *)b;

    if (da < db) {

        return -1;

    }

    if (da > db) {

        return 1;

    }
    return 0;
}

void get_visible_bin_range(int *start_bin, int *end_bin) {
    /*
        Purpose: Computes the visible FFT bin range for the current display span
        Returns: No value
    */

    double bins_per_hz = (double)FFT_SIZE / (double)Global_Sample_Rate_Hz;
    int visible_bins = (int)((double)Global_Display_Span_Hz * bins_per_hz);

    if (visible_bins < 8) {

        visible_bins = 8;

    }

    if (visible_bins > FFT_SIZE) {

        visible_bins = FFT_SIZE;

    }

    int center = FFT_SIZE / 2;

    *start_bin = center - visible_bins / 2;
    *end_bin = *start_bin + visible_bins;

    if (*start_bin < 0) {

        *start_bin = 0;
        *end_bin = visible_bins;

    }

    if (*end_bin > FFT_SIZE) {

        *end_bin = FFT_SIZE;
        *start_bin = FFT_SIZE - visible_bins;

        if (*start_bin < 0) {

            *start_bin = 0;

        }

    }
}

double estimate_noise_floor_median_visible(double *db, int start_bin, int end_bin) {
    /*
        Purpose: Estimates the visible noise floor using the median bin level
        Returns: Noise floor
    */

    static double temp[FFT_SIZE];
    int count = 0;

    for (int x = start_bin; (x < end_bin) && (count < FFT_SIZE); x++) {

        if (abs(x - FFT_SIZE / 2) < 2) {

            continue;

        }
        temp[count++] = db[x];
    }

    if (count <= 0) {

        return -300.0;

    }

    qsort(temp, count, sizeof(double), cmp_double_for_qsort);
    return temp[count / 2];
}

void add_fft_line_to_waterfall(uint32_t *pixels, int tex_w, int tex_h, double *db) {
    /*
        Purpose: Adds one FFT row to the live waterfall display
        Returns: No value
    */

    memmove(pixels + tex_w, pixels, sizeof(uint32_t) * tex_w * (tex_h - 1));

    int visible_start = 0;
    int visible_end = FFT_SIZE;

    get_visible_bin_range(&visible_start, &visible_end);

    double noise_floor_db = estimate_noise_floor_median_visible(db, visible_start, visible_end);

    for (int x = 0; x < tex_w; x++) {
        int start_bin = visible_start + x * (visible_end - visible_start) / tex_w;
        int end_bin = visible_start + (x + 1) * (visible_end - visible_start) / tex_w;

        if (end_bin <= start_bin) {

            end_bin = start_bin + 1;

        }

        if (end_bin > visible_end) {

            end_bin = visible_end;

        }

        double max_db = -300.0;
        double sum_db = 0.0;
        int count = 0;

        /*
         * Track the top 3 bins instead of trusting one random spike.
         */
        double top1 = -300.0;
        double top2 = -300.0;
        double top3 = -300.0;

        for (int k = start_bin; k < end_bin; k++) {

            if (abs(k - FFT_SIZE / 2) < 2) {

                continue;

            }

            double v = db[k];

            if (v > top1) {

                top3 = top2;
                top2 = top1;
                top1 = v;

            }

            else if (v > top2) {

                top3 = top2;
                top2 = v;

            }

            else if (v > top3) {

                top3 = v;

            }

            if (v > max_db) {

                max_db = v;

            }

            sum_db += v;
            count++;
        }

        double avg_db = (count > 0) ? (sum_db / count) : max_db;

        /*
         * Smoothed peak reduces salt-and-pepper red noise.
         */

        double color_db;

        if (count >= 3) {

            color_db = (top1 + top2 + top3) / 3.0;

        }

        else {

            color_db = max_db;

        }

        double peakness_db = color_db - avg_db;
        double rel_db = color_db - noise_floor_db;
        double delta_db = 0.0;

        if (Global_Color_Baseline) {

            if (Global_Color_Baseline[x] < -200.0) {

                Global_Color_Baseline[x] = color_db;

            }

            else {

                delta_db = color_db - Global_Color_Baseline[x];
                Global_Color_Baseline[x] = 0.95 * Global_Color_Baseline[x] + 0.05 * color_db;

            }

        }

        pixels[x] = power_to_color_relative(rel_db, delta_db, peakness_db);
    }
}

// ==============================
// Analysis Workstation GUI Helpers
// ==============================

void ANALYSIS_draw_line_plot(SDL_Renderer *renderer, SDL_Rect rect, const float *values, int count, int bipolar,
                             SDL_Color color, const char *label, TTF_Font *font) {
    /*

    Purpose: Draws an analysis line plot inside a rectangle

    Return: No return

    */

    (void)label;
    (void)font;

    draw_filled_rect(renderer, rect, (SDL_Color){5, 5, 5, 255});
    draw_outline_rect(renderer, rect, (SDL_Color){120, 120, 120, 255});

    int mid = rect.y + rect.h / 2;

    SDL_SetRenderDrawColor(renderer, 70, 70, 70, 255);

    if (bipolar) {

        SDL_RenderDrawLine(renderer, rect.x, mid, rect.x + rect.w, mid);

    }

    else {

        SDL_RenderDrawLine(renderer, rect.x, rect.y + rect.h - 12, rect.x + rect.w, rect.y + rect.h - 12);

    }

    if (!values || count <= 1) {

        return;

    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    int prev_x = rect.x;
    int prev_y = bipolar ? mid : rect.y + rect.h - 12;

    for (int px = 0; px < rect.w; px++) {
        int idx = (px * count) / rect.w;

        if (idx >= count) {

            idx = count - 1;

        }

        float v = values[idx];
        int y;

        if (bipolar) {

            if (v < -1.0f) {

                v = -1.0f;

            }

            if (v > 1.0f) {

                v = 1.0f;

            }
            y = mid - (int)(v * (float)(rect.h * 0.42f));

        }

        else {

            if (v < 0.0f) {

                v = 0.0f;

            }

            if (v > 1.0f) {

                v = 1.0f;

            }
            y = rect.y + rect.h - 12 - (int)(v * (float)(rect.h - 24));

        }

        int x = rect.x + px;

        if (px > 0) {

            SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);

        }

        prev_x = x;
        prev_y = y;
    }
}

void ANALYSIS_draw_constellation_plot(SDL_Renderer *renderer, SDL_Rect rect, const float *i_values,
                                      const float *q_values, int count, TTF_Font *font) {
    /*
        Purpose: Draws an I/Q constellation plot inside a rectangle
        Returns: No value
    */

    (void)font;

    draw_filled_rect(renderer, rect, (SDL_Color){5, 5, 5, 255});
    draw_outline_rect(renderer, rect, (SDL_Color){120, 120, 120, 255});

    int cx = rect.x + rect.w / 2;
    int cy = rect.y + rect.h / 2;
    int radius = rect.w < rect.h ? rect.w : rect.h;
    radius = (int)((double)radius * 0.43);

    if (radius < 4) {

        return;

    }

    SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
    SDL_RenderDrawLine(renderer, rect.x + 8, cy, rect.x + rect.w - 8, cy);
    SDL_RenderDrawLine(renderer, cx, rect.y + 8, cx, rect.y + rect.h - 8);

    SDL_Rect unit_box = {cx - radius, cy - radius, radius * 2, radius * 2};
    SDL_RenderDrawRect(renderer, &unit_box);

    if (!i_values || !q_values || count <= 0) {

        return;

    }

    SDL_SetRenderDrawColor(renderer, 0, 210, 255, 255);

    for (int n = 0; n < count; n++) {
        float I = i_values[n];
        float Q = q_values[n];

        if (I < -1.0f) {

            I = -1.0f;

        }

        if (I > 1.0f) {

            I = 1.0f;

        }

        if (Q < -1.0f) {

            Q = -1.0f;

        }

        if (Q > 1.0f) {

            Q = 1.0f;

        }

        int x = cx + (int)(I * (float)radius);
        int y = cy - (int)(Q * (float)radius);

        if (x >= rect.x + 1 && x < rect.x + rect.w - 1 && y >= rect.y + 1 && y < rect.y + rect.h - 1) {

            SDL_RenderDrawPoint(renderer, x, y);

            if (rect.w > 180 && rect.h > 100) {

                SDL_RenderDrawPoint(renderer, x + 1, y);
                SDL_RenderDrawPoint(renderer, x, y + 1);

            }

        }
    }
}

void ANALYSIS_make_ellipsis_text(TTF_Font *font, const char *src, char *dst, size_t dst_sz, int max_px) {
    /*
        Purpose: Shortens text with an ellipsis to fit a pixel width
        Returns: No value
    */

    if (!src || !dst || dst_sz == 0) {

        return;

    }

    snprintf(dst, dst_sz, "%s", src);

    if (!font) {

        return;

    }

    int text_w = 0;
    int text_h = 0;

    if (TTF_SizeText(font, dst, &text_w, &text_h) != 0) {

        return;

    }

    if (text_w <= max_px) {

        return;

    }

    const char *ellipsis = "...";
    int ell_w = 0;

    if (TTF_SizeText(font, ellipsis, &ell_w, &text_h) != 0) {

        return;

    }

    size_t len = strlen(src);

    while (len > 4) {
        snprintf(dst, dst_sz, "%.*s...", (int)len, src);

        if (TTF_SizeText(font, dst, &text_w, &text_h) != 0) {

            return;

        }

        if (text_w <= max_px) {

            return;

        }

        len--;
    }

    snprintf(dst, dst_sz, "...");
}

void ANALYSIS_draw_loading_indicator(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect parent_rect) {
    /*
        Purpose: Draws the analysis loading indicator in the selector panel
        Returns: No value
    */

    if (!font || !Global_Analysis_Loading) {

        return;

    }

    int box_w = 150;
    int box_h = 28;

    SDL_Rect box = {parent_rect.x + parent_rect.w - box_w - 12, parent_rect.y + 8, box_w, box_h};

    const char *frames[] = {"Loading   ", "Loading.  ", "Loading.. ", "Loading..."};
    Global_Analysis_Load_Frame = (int)((SDL_GetTicks64() / 180) % 4);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, box, (SDL_Color){0, 0, 0, 220});
    draw_outline_rect(renderer, box, (SDL_Color){0, 220, 80, 210});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    draw_text(renderer, font, frames[Global_Analysis_Load_Frame], box.x + 10, box.y + 7, (SDL_Color){0, 255, 90, 255});

    int pulse_w = 10;
    int pulse_gap = 4;
    int pulse_x = box.x + box.w - 52;
    int pulse_y = box.y + 9;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < 3; i++) {
        int active = (i == Global_Analysis_Load_Frame % 3);

        SDL_Rect pulse = {pulse_x + i * (pulse_w + pulse_gap), pulse_y, pulse_w, 10};

        SDL_Color color = active ? (SDL_Color){0, 255, 90, 230} : (SDL_Color){0, 100, 40, 160};

        draw_filled_rect(renderer, pulse, color);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void ANALYSIS_draw_file_list(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect) {
    /*
        Purpose: Draws the analysis recording file selector
        Returns: No value
    */

    if (!font) {

        return;

    }

    draw_filled_rect(renderer, rect, (SDL_Color){0, 0, 0, 255});
    draw_outline_rect(renderer, rect, (SDL_Color){120, 120, 120, 255});

    draw_text(renderer, font,
              "Analysis Workstation | R rescan | Up/Down select | Enter open | "
              "Wheel scroll/zoom | G/Esc exit",
              rect.x + 12, rect.y + 12, (SDL_Color){235, 235, 235, 255});

    ANALYSIS_draw_loading_indicator(renderer, font, rect);

    char short_status[256];

    ANALYSIS_make_ellipsis_text(font, Global_Analysis_Status, short_status, sizeof(short_status), rect.w - 24);

    SDL_Rect status_clip = {rect.x + 8, rect.y + 38, rect.w - 16, 24};

    SDL_RenderSetClipRect(renderer, &status_clip);

    draw_text(renderer, font, short_status, rect.x + 12, rect.y + 40, (SDL_Color){170, 220, 170, 255});

    SDL_RenderSetClipRect(renderer, NULL);

    int row_h = 22;
    int list_y = rect.y + 70;
    int visible = (rect.h - 82) / row_h;

    if (visible < 1) {

        visible = 1;

    }

    int first = Global_Analysis_List_Scroll;

    if (first < 0) {

        first = 0;

    }

    if (first + visible > Global_Analysis_File_Count) {

        first = Global_Analysis_File_Count - visible;

        if (first < 0) {

            first = 0;

        }

    }

    Global_Analysis_List_Scroll = first;

    for (int i = first; i < Global_Analysis_File_Count && i < first + visible; i++) {
        int y = list_y + (i - first) * row_h;
        int is_selected = (i == Global_Analysis_Selected);
        int is_open = (i == Global_Analysis_Loaded_Index);

        if (is_selected) {

            SDL_Rect row = {rect.x + 6, y - 2, rect.w - 12, row_h};
            draw_filled_rect(renderer, row, (SDL_Color){35, 35, 35, 255});
            draw_outline_rect(renderer, row, (SDL_Color){130, 130, 130, 255});

        }

        SDL_Color color = is_selected ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){145, 145, 145, 255};

        char short_name[256];
        char line[320];

        int max_name_px = rect.w - 52;

        if (max_name_px < 40) {

            max_name_px = 40;

        }

        ANALYSIS_make_ellipsis_text(font, Global_Analysis_Files[i], short_name, sizeof(short_name), max_name_px);

        snprintf(line, sizeof(line), "%c%c %s", is_selected ? '>' : ' ', is_open ? '*' : ' ', short_name);

        SDL_Rect clip = {rect.x + 8, y - 2, rect.w - 16, row_h};

        SDL_RenderSetClipRect(renderer, &clip);

        draw_text(renderer, font, line, rect.x + 12, y, color);

        SDL_RenderSetClipRect(renderer, NULL);
    }
}

void ANALYSIS_draw_filter_overlay(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect spec_rect) {
    /*
        Purpose: Draws the analysis frequency filter selector over the spectrogram
        Returns: No value
    */

    if (!Global_Analysis_Filter_Visible && !Global_Analysis_Filter_Active && !Global_Analysis_Filter_Selecting) {

        return;

    }

    double y0f = limit_double(Global_Analysis_Filter_Y0, 0.0, 1.0);
    double y1f = limit_double(Global_Analysis_Filter_Y1, 0.0, 1.0);

    if (y1f < y0f) {

        double tmp = y0f;
        y0f = y1f;
        y1f = tmp;

    }

    int y0 = spec_rect.y + (int)(y0f * (double)spec_rect.h);
    int y1 = spec_rect.y + (int)(y1f * (double)spec_rect.h);

    if (y0 < spec_rect.y) {

        y0 = spec_rect.y;

    }

    if (y1 > spec_rect.y + spec_rect.h) {

        y1 = spec_rect.y + spec_rect.h;

    }

    if (y1 <= y0) {

        y1 = y0 + 2;

    }

    if (y1 > spec_rect.y + spec_rect.h) {

        y1 = spec_rect.y + spec_rect.h;

    }

    SDL_Rect band = {spec_rect.x, y0, spec_rect.w, y1 - y0};

    SDL_Rect old_clip;
    SDL_bool old_clip_enabled = SDL_RenderIsClipEnabled(renderer);
    SDL_RenderGetClipRect(renderer, &old_clip);
    SDL_RenderSetClipRect(renderer, &spec_rect);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(renderer, 0, 210, 255, 45);
    for (int yy = band.y; yy < band.y + band.h; yy++) {
        SDL_RenderDrawLine(renderer, band.x, yy, band.x + band.w - 1, yy);
    }

    SDL_SetRenderDrawColor(renderer, 0, 240, 255, 220);
    SDL_RenderDrawLine(renderer, spec_rect.x, y0, spec_rect.x + spec_rect.w, y0);
    SDL_RenderDrawLine(renderer, spec_rect.x, y1, spec_rect.x + spec_rect.w, y1);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 150);
    SDL_RenderDrawRect(renderer, &band);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    if (old_clip_enabled) {

        SDL_RenderSetClipRect(renderer, &old_clip);

    }

    else {

        SDL_RenderSetClipRect(renderer, NULL);

    }

    char msg[160];

    snprintf(msg, sizeof(msg), "Ctrl+drag vertically here to select a frequency band");

    if (!Global_Analysis_Filter_Active) {

        int text_w = 0;
        int text_h = 0;

        if (font && TTF_SizeText(font, msg, &text_w, &text_h) != 0) {

            text_w = 0;
            text_h = 0;

        }

        SDL_Rect bg = {spec_rect.x + 12, spec_rect.y + spec_rect.h - text_h - 18, text_w + 16, text_h + 10};

        if (bg.w > spec_rect.w - 24) {

            bg.w = spec_rect.w - 24;

        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        draw_filled_rect(renderer, bg, (SDL_Color){0, 0, 0, 190});
        draw_outline_rect(renderer, bg, (SDL_Color){0, 220, 255, 210});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        draw_text(renderer, font, msg, bg.x + 8, bg.y + 5, (SDL_Color){210, 245, 255, 255});

    }
}

void ANALYSIS_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, SDL_Texture *texture, uint32_t *pixels,
                               int tex_w, int tex_h, int win_w, int win_h) {
    /*
        Purpose: Draws the full analysis workstation layout
        Returns: No value
    */

    SDL_Rect full = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, full, (SDL_Color){0, 0, 0, 255});

    int selector_h = (int)((double)win_h * 0.22);

    if (selector_h < 130) {

        selector_h = 130;

    }

    int gap = 10;
    int title_h = 22;
    int col_gap = 12;
    int panel_h = selector_h - MARGIN;
    int work_x = MARGIN;
    int work_w = win_w - 2 * MARGIN;

    if (work_w < 100) {

        return;

    }

    int half_w = (work_w - col_gap) / 2;

    if (half_w < 60) {

        half_w = work_w / 2;

    }

    SDL_Rect list_rect = {MARGIN, MARGIN, half_w, panel_h};

    SDL_Rect psd_rect = {MARGIN + half_w + col_gap, MARGIN + title_h, work_w - half_w - col_gap, panel_h - title_h};

    if (psd_rect.h < 70) {

        psd_rect.h = 70;

    }

    ANALYSIS_draw_file_list(renderer, font, list_rect);

    int work_y = list_rect.y + list_rect.h + MARGIN;
    int work_h = win_h - work_y - MARGIN;

    if (work_h < 260) {

        return;

    }

    int top_row_h = psd_rect.h;
    int mid_row_h = psd_rect.h;
    int spec_h = work_h - top_row_h - mid_row_h - (gap * 2) - (title_h * 3);

    if (top_row_h < 70) {

        top_row_h = 70;

    }

    if (mid_row_h < 70) {

        mid_row_h = 70;

    }

    if (spec_h < 110) {

        spec_h = 110;

    }

    int top_title_y = work_y;

    SDL_Rect mag_rect = {work_x, top_title_y + title_h, half_w, top_row_h};

    SDL_Rect phase_rect = {work_x + half_w + col_gap, top_title_y + title_h, work_w - half_w - col_gap, top_row_h};

    int mid_title_y = mag_rect.y + mag_rect.h + gap;

    SDL_Rect inst_rect = {work_x, mid_title_y + title_h, half_w, mid_row_h};

    SDL_Rect const_rect = {work_x + half_w + col_gap, mid_title_y + title_h, work_w - half_w - col_gap, mid_row_h};

    int spec_title_y = inst_rect.y + inst_rect.h + gap;

    SDL_Rect spec_rect = {work_x, spec_title_y + title_h, work_w, win_h - (spec_title_y + title_h) - MARGIN};

    if (spec_rect.h < 110) {

        spec_rect.h = 110;

    }

    if (Global_Analysis_Dirty) {

        if (Global_Analysis_Loading) {

            ANALYSIS_draw_file_list(renderer, font, list_rect);
            SDL_RenderPresent(renderer);

        }

        ANALYSIS_render_workstation_data(pixels, tex_w, tex_h);
        SDL_UpdateTexture(texture, NULL, pixels, tex_w * sizeof(uint32_t));

        Global_Analysis_Loading = 0;
        Global_Analysis_Dirty = 0;

    }

    draw_text(renderer, font, "Frequency Spectrum", psd_rect.x + 10, MARGIN + 3, (SDL_Color){190, 190, 190, 255});

    draw_text(renderer, font, "Magnitude Envelope", mag_rect.x + 10, top_title_y + 3, (SDL_Color){190, 190, 190, 255});

    draw_text(renderer, font, "Phase", phase_rect.x + 10, top_title_y + 3, (SDL_Color){190, 190, 190, 255});

    draw_text(renderer, font, "Instantaneous Frequency", inst_rect.x + 10, mid_title_y + 3,
              (SDL_Color){190, 190, 190, 255});

    draw_text(renderer, font, "Constellation I/Q", const_rect.x + 10, mid_title_y + 3, (SDL_Color){190, 190, 190, 255});

    if (Global_Analysis_Path[0] == '\0') {

        draw_filled_rect(renderer, psd_rect, (SDL_Color){5, 5, 5, 255});
        draw_outline_rect(renderer, psd_rect, (SDL_Color){120, 120, 120, 255});

        draw_filled_rect(renderer, mag_rect, (SDL_Color){5, 5, 5, 255});
        draw_outline_rect(renderer, mag_rect, (SDL_Color){120, 120, 120, 255});
        draw_text(renderer, font, "Select a recording, then press Enter to open it.", mag_rect.x + 12, mag_rect.y + 42,
                  (SDL_Color){200, 200, 200, 255});

        draw_filled_rect(renderer, phase_rect, (SDL_Color){5, 5, 5, 255});
        draw_outline_rect(renderer, phase_rect, (SDL_Color){120, 120, 120, 255});

        draw_filled_rect(renderer, inst_rect, (SDL_Color){5, 5, 5, 255});
        draw_outline_rect(renderer, inst_rect, (SDL_Color){120, 120, 120, 255});

        draw_filled_rect(renderer, const_rect, (SDL_Color){5, 5, 5, 255});
        draw_outline_rect(renderer, const_rect, (SDL_Color){120, 120, 120, 255});

        draw_filled_rect(renderer, spec_rect, (SDL_Color){5, 5, 5, 255});
        draw_outline_rect(renderer, spec_rect, (SDL_Color){120, 120, 120, 255});
        return;

    }

    ANALYSIS_draw_line_plot(renderer, psd_rect, Global_Analysis_PSD_Line, Global_Analysis_Render_W, 0,
                            (SDL_Color){0, 255, 90, 255}, NULL, font);

    ANALYSIS_draw_line_plot(renderer, mag_rect, Global_Analysis_Mag_Line, Global_Analysis_Render_W, 0,
                            (SDL_Color){0, 255, 0, 255}, NULL, font);

    ANALYSIS_draw_line_plot(renderer, phase_rect, Global_Analysis_Phase_Line, Global_Analysis_Render_W, 1,
                            (SDL_Color){255, 180, 0, 255}, NULL, font);

    ANALYSIS_draw_line_plot(renderer, inst_rect, Global_Analysis_InstFreq_Line, Global_Analysis_Render_W, 1,
                            (SDL_Color){0, 190, 255, 255}, NULL, font);

    ANALYSIS_draw_constellation_plot(renderer, const_rect, Global_Analysis_Const_I, Global_Analysis_Const_Q,
                                     Global_Analysis_Const_Count, font);

    SDL_RenderCopy(renderer, texture, NULL, &spec_rect);
    draw_border(renderer, spec_rect);
    ANALYSIS_draw_filter_overlay(renderer, font, spec_rect);

    /*
     * Keep the greyscale spectrogram unobstructed. Status/frequency labels are
     * drawn outside the spectrogram by the Analysis overlay path.
     */
}

void ANALYSIS_exit_mode(uint32_t *pixels, int tex_w, int tex_h, SDL_Texture *texture) {
    /*
        Purpose: Exits analysis mode and restores live waterfall rendering
        Returns: No value
    */

    Global_Analysis_Mode = 0;
    Global_Analysis_Dirty = 0;
    Global_Analysis_Dragging = 0;
    Global_Analysis_Filter_Selecting = 0;

    clear_waterfall(pixels, tex_w, tex_h);
    SDL_UpdateTexture(texture, NULL, pixels, tex_w * sizeof(uint32_t));
    reset_prev_col_db(tex_w);

    set_status("", (SDL_Color){0, 255, 80, 255});
}
