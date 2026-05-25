/*
 * ============================================================================
 * File:            GUIs.c
 * Author:          Hassan Fares
 *
 * Confidential:    No
 *
 * Description:     GUI drawing and SDL UI helper functions for RetroSpectrum.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "GUIs.h"
#include "IQs.h"

#define CONTROL_PANEL_HEIGHT            95
#define AXIS_HEIGHT                     70
#define MARGIN                          20

TTF_Font *load_font(int size) {
    const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        NULL
    };

    for (int i = 0; paths[i] != NULL; i++) {
        TTF_Font *font = TTF_OpenFont(paths[i], size);
        if (font) return font;
    }

    return NULL;
}

void draw_text(SDL_Renderer *renderer,
               TTF_Font *font,
               const char *text,
               int x,
               int y,
               SDL_Color color) {
    if (!font || !text || text[0] == '\0') return;

    SDL_Surface *surface = TTF_RenderText_Blended(font, text, color);
    if (!surface) return;

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

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b){

    return 0xFF000000U | ((uint32_t)r << 16) | ((uint32_t) g << 8) | b;

}

void toggle_fullscreen(SDL_Window *window){

    Global_Fullscreen = !Global_Fullscreen;

    SDL_SetWindowFullscreen(
        window,
        Global_Fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0
    );
}

void set_status(const char *msg, SDL_Color color){

    snprintf(Global_Status_Msg, sizeof(Global_Status_Msg), "%s", msg);

    Global_Status_Color = color;

}

void draw_filled_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void draw_outline_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

void draw_made_in_usa(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    (void)win_w;

    if (!renderer || !font) return;

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

        SDL_Rect stripe = {
            flag_x,
            stripe_y0,
            flag_w,
            stripe_y1 - stripe_y0
        };

        if ((i % 2) == 0) {
            SDL_SetRenderDrawColor(renderer, 191, 10, 48, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        }

        SDL_RenderFillRect(renderer, &stripe);
    }

    /*
     * Blue canton.
     */
    int canton_w = (int)(flag_w * 0.40);
    int canton_h = (7 * flag_h) / 13;

    SDL_Rect canton = {
        flag_x,
        flag_y,
        canton_w,
        canton_h
    };

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
            } else {
                px = canton.x + left_margin + (usable_w / 10) + (col * usable_w) / 5;
            }

            SDL_RenderDrawPoint(renderer, px, py);
        }
    }

}

void draw_button(SDL_Renderer *renderer,
                 TTF_Font *font,
                 SDL_Rect rect,
                 const char *label,
                 int active,
                 int is_record_button) {
    SDL_Color fill, border, text;

    if (is_record_button) {
        fill   = active ? (SDL_Color){130, 0, 0, 255} : (SDL_Color){0, 8, 3, 255};
        border = active ? (SDL_Color){255, 80, 60, 255} : (SDL_Color){0, 100, 40, 255};
        text   = active ? (SDL_Color){255, 130, 110, 255} : (SDL_Color){0, 180, 70, 255};
    } else {
        fill   = active ? (SDL_Color){0, 70, 25, 255} : (SDL_Color){0, 8, 3, 255};
        border = active ? (SDL_Color){255, 60, 40, 255} : (SDL_Color){0, 180, 60, 255};
        text   = active ? (SDL_Color){255, 70, 50, 255} : (SDL_Color){0, 255, 90, 255};
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
    return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
}

int near_px(int a, int b, int tolerance) {
    return abs(a - b) <= tolerance;
}

void draw_input_box(SDL_Renderer *renderer,
                    TTF_Font *font,
                    Type_Input_Box *box,
                    int active) {
    SDL_Color border = active ? (SDL_Color){0, 255, 80, 255} : (SDL_Color){0, 100, 40, 255};
    SDL_Color fill = {0, 10, 3, 255};
    SDL_Color label = {0, 210, 70, 255};
    SDL_Color text = {0, 255, 90, 255};

    draw_text(renderer, font, box->label, box->rect.x, box->rect.y - 22, label);
    draw_filled_rect(renderer, box->rect, fill);
    draw_outline_rect(renderer, box->rect, border);
    draw_text(renderer, font, box->text, box->rect.x + 8, box->rect.y + 10, text);
}

void draw_checkbox(SDL_Renderer *renderer,
                   TTF_Font *font,
                   SDL_Rect rect,
                   const char *label,
                   int checked) {
    draw_outline_rect(renderer, rect, (SDL_Color){0, 255, 80, 255});

    if (checked) {
        SDL_Rect inner = {rect.x + 5, rect.y + 5, rect.w - 10, rect.h - 10};
        draw_filled_rect(renderer, inner, (SDL_Color){0, 255, 80, 255});
    }

    draw_text(renderer, font, label, rect.x + rect.w + 8, rect.y + 2, (SDL_Color){0, 220, 70, 255});
}

void layout_controls(int win_w,
                     Type_Input_Box *freq_box,
                     Type_Input_Box *sr_box,
                     Type_Input_Box *display_box,
                     Type_Input_Box *lna_box,
                     Type_Input_Box *vga_box,
                     Type_Input_Box *fps_box,
                     Type_Input_Box *rows_box,
                     SDL_Rect *amp_box,
                     SDL_Rect *dc_box,
                     SDL_Rect *sel_button,
                     SDL_Rect *rec_button) {
    int y = 42;
    int x = MARGIN + 20;
    int box_h = 42;
    int gap = 12;

    freq_box->rect = (SDL_Rect){x, y, 145, box_h}; x += 145 + gap;
    sr_box->rect = (SDL_Rect){x, y, 145, box_h}; x += 145 + gap;
    display_box->rect = (SDL_Rect){x, y, 155, box_h}; x += 155 + gap;
    lna_box->rect = (SDL_Rect){x, y, 82, box_h}; x += 82 + gap;
    vga_box->rect = (SDL_Rect){x, y, 82, box_h}; x += 82 + gap;
    fps_box->rect = (SDL_Rect){x, y, 95, box_h}; x += 95 + gap;
    rows_box->rect = (SDL_Rect){x, y, 105, box_h};

    *amp_box = (SDL_Rect){win_w - 410, y - 8, 22, 22};
    *dc_box = (SDL_Rect){win_w - 410, y + 22, 22, 22};
    *sel_button = (SDL_Rect){win_w - 260, y, 105, box_h};
    *rec_button = (SDL_Rect){win_w - 140, y, 105, box_h};
}

void draw_control_panel(SDL_Renderer *renderer,
                        TTF_Font *font,
                        int win_w,
                        Type_Input_Box *freq_box,
                        Type_Input_Box *sr_box,
                        Type_Input_Box *display_box,
                        Type_Input_Box *lna_box,
                        Type_Input_Box *vga_box,
                        Type_Input_Box *fps_box,
                        Type_Input_Box *rows_box,
                        SDL_Rect amp_box,
                        SDL_Rect dc_box,
                        SDL_Rect sel_button,
                        SDL_Rect rec_button,
                        Type_Active_Fields active) {
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

    draw_checkbox(renderer, font, amp_box, "Amplify", Global_Amp_Enable);
    draw_checkbox(renderer, font, dc_box, "DC Correction", Global_DC_Enable);
    draw_button(renderer, font, sel_button, "Selector", Global_Selector.enabled, 0);
    draw_button(renderer, font, rec_button, "RECORD", Global_Rec, 1);
}

void draw_frequency_axis(SDL_Renderer *renderer,
                         TTF_Font *font,
                         SDL_Rect waterfall_rect) {
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
        } else {
            snprintf(label, sizeof(label), "%.3f", freq_mhz);
        }

        int label_x = x - 24;

        if (label_x < waterfall_rect.x) label_x = waterfall_rect.x;

        if (label_x > waterfall_rect.x + waterfall_rect.w - 70) {
            label_x = waterfall_rect.x + waterfall_rect.w - 70;
        }

        draw_text(renderer, font, label, label_x, axis_y + 12, (SDL_Color){0, 220, 70, 255});
    }
}

void draw_border(SDL_Renderer *renderer, SDL_Rect r) {
    draw_outline_rect(renderer, r, (SDL_Color){0, 180, 60, 255});
}

void draw_selection_overlay(SDL_Renderer *renderer, SDL_Rect waterfall_rect) {
    if (!Global_Selector.enabled) return;

    double x0f = limit_double(Global_Selector.X0, 0.0, 1.0);
    double x1f = limit_double(Global_Selector.X1, 0.0, 1.0);

    if (x1f < x0f) {
        double tmp = x0f;
        x0f = x1f;
        x1f = tmp;
    }

    int x0 = waterfall_rect.x + (int)(x0f * waterfall_rect.w);
    int x1 = waterfall_rect.x + (int)(x1f * waterfall_rect.w);

    if (x1 <= x0) x1 = x0 + 1;

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

void draw_selector_bandwidth(SDL_Renderer *renderer,
                             TTF_Font *font,
                             SDL_Rect waterfall_rect) {
    if (!Global_Selector.enabled) return;

    uint32_t bw_hz = selection_BW_Hz();
    uint64_t center_hz = selection_center_Hz();

    char msg[128];

    if (bw_hz >= 1000000) {
        snprintf(
            msg,
            sizeof(msg),
            "Selector: %.6f MHz | BW: %.3f MHz",
            center_hz / 1e6,
            bw_hz / 1e6
        );
    } else {
        snprintf(
            msg,
            sizeof(msg),
            "Selector: %.6f MHz | BW: %.3f kHz",
            center_hz / 1e6,
            bw_hz / 1e3
        );
    }

    int text_w = 0;
    int text_h = 0;

    if (font && TTF_SizeText(font, msg, &text_w, &text_h) != 0) {
        text_w = 0;
        text_h = 0;
    }

    int x = waterfall_rect.x + 12;
    int y = waterfall_rect.y + 12;

    SDL_Rect bg = {
        x - 6,
        y - 4,
        text_w + 12,
        text_h + 8
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, bg, (SDL_Color){0, 0, 0, 180});
    draw_outline_rect(renderer, bg, (SDL_Color){0, 180, 60, 220});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    draw_text(renderer, font, msg, x, y, (SDL_Color){0, 255, 90, 255});
}

void update_selection_from_mouse(int mouse_x, SDL_Rect waterfall_rect) {
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
    } else if (Global_Selector.resizing_right) {
        Global_Selector.X1 = frac;

        if (Global_Selector.X1 < Global_Selector.X0 + 0.002) {
            Global_Selector.X1 = Global_Selector.X0 + 0.002;
        }

        if (Global_Selector.X1 > 1.0) {
            Global_Selector.X1 = 1.0;
        }
    } else if (Global_Selector.dragging) {
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

void draw_antenna_recommendation(SDL_Renderer *renderer,
                                 TTF_Font *font,
                                 int win_w,
                                 int win_h) {
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
