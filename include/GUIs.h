#ifndef GUIS_H
#define GUIS_H


#include <stdint.h>
#include <stddef.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// Track active settings
typedef enum {
    FIELD_NONE = 0,
    FIELD_FREQ,
    FIELD_SR,
    FIELD_DISPLAY,
    FIELD_LNA,
    FIELD_VGA,
    FIELD_FPS,
    FIELD_ROWS
} Type_Active_Fields;

// GUI Rectangle
typedef struct {
  SDL_Rect rect;
  char text[32];
  const char *label;
  Type_Active_Fields id;
} Type_Input_Box;

// Selector window
typedef struct {
  double X0;
  double X1;
  int enabled;
  int dragging;
  int resizing_left;
  int resizing_right;
} Type_Selector;

/*
 * Globals used by drawing functions.
 * These are defined in RetroSpectrum.c.
 */
extern uint64_t Global_Center_Freq_Hz;
extern uint32_t Global_Sample_Rate_Hz;
extern uint32_t Global_Display_Span_Hz;

extern int Global_Amp_Enable;
extern int Global_DC_Enable;
extern int Global_Rec;

extern char Global_Status_Msg[256];
extern SDL_Color Global_Status_Color;

extern Type_Selector Global_Selector;

/*
 * Functions still defined in RetroSpectrum.c but used by GUIs.c.
 */
double limit_double(double value, double low, double high);
uint64_t selection_center_Hz(void);
uint32_t selection_BW_Hz(void);
double recommended_antenna_length_inches(uint64_t freq_hz);

/*
 * GUI functions defined in GUIs.c.
 */
TTF_Font *load_font(int size);

void draw_text(SDL_Renderer *renderer,
               TTF_Font *font,
               const char *text,
               int x,
               int y,
               SDL_Color color);

void draw_filled_rect(SDL_Renderer *renderer,
                      SDL_Rect rect,
                      SDL_Color color);

void draw_outline_rect(SDL_Renderer *renderer,
                       SDL_Rect rect,
                       SDL_Color color);

void draw_made_in_usa(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);

void draw_button(SDL_Renderer *renderer,
                 TTF_Font *font,
                 SDL_Rect rect,
                 const char *label,
                 int active,
                 int is_record_button);

int point_in_rect(int x, int y, SDL_Rect r);
int near_px(int a, int b, int tolerance);

void draw_input_box(SDL_Renderer *renderer,
                    TTF_Font *font,
                    Type_Input_Box *box,
                    int active);

void draw_checkbox(SDL_Renderer *renderer,
                   TTF_Font *font,
                   SDL_Rect rect,
                   const char *label,
                   int checked);

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
                     SDL_Rect *rec_button);

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
                        Type_Active_Fields active);

void draw_frequency_axis(SDL_Renderer *renderer,
                         TTF_Font *font,
                         SDL_Rect waterfall_rect);

void draw_border(SDL_Renderer *renderer, SDL_Rect r);

void draw_selection_overlay(SDL_Renderer *renderer,
                            SDL_Rect waterfall_rect);

void draw_selector_bandwidth(SDL_Renderer *renderer,
                             TTF_Font *font,
                             SDL_Rect waterfall_rect);

void update_selection_from_mouse(int mouse_x,
                                 SDL_Rect waterfall_rect);

void draw_antenna_recommendation(SDL_Renderer *renderer,
                                 TTF_Font *font,
                                 int win_w,
                                 int win_h);

#endif
