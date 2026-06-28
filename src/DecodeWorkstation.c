#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * File:            DecodeWorkstation.c
 * Author:          Hassan Fares
 *
 * Description:     Decode workstation for RetroSpectrum. Reads .complex16 IQ
 *                  files, launches a GNU Radio helper flowgraph for symbol
 *                  demodulation, and displays the resulting bitstream.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include "DecodeWorkstation.h"
#include "GUIs.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>


#ifndef RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#endif

#ifndef SDLK_v
#define SDLK_v 'v'
#endif

#ifndef SDLK_a
#define SDLK_a 'a'
#endif

#ifndef SDLK_HOME
#define SDLK_HOME 1073741898
#endif

#ifndef SDLK_END
#define SDLK_END 1073741901
#endif

#ifndef SDLK_UP
#define SDLK_UP 1073741906
#endif

#ifndef SDLK_DOWN
#define SDLK_DOWN 1073741905
#endif

#ifndef KMOD_CTRL
#define KMOD_CTRL 0x00c0
#endif

#ifndef SDL_MAJOR_VERSION
extern int SDL_SetClipboardText(const char *text);
#endif

static void decode_get_adjusted_mouse_state(int *x, int *y){
    SDL_GetMouseState(x, y);
    if (y) *y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;
}

#define DECODE_MAX_FILES             512
#define DECODE_MAX_PATH              1024
#define DECODE_MAX_NAME              512
#define DECODE_MAX_TEXT              128
#define DECODE_BITSTREAM_MAX         262144
#define DECODE_FILE_SEARCH_TEXT_MAX  256
#define DECODE_FILE_SEARCH_ROW_H     34
#define DECODE_ROW_H                 30
#define DECODE_LIST_W                430
#define DECODE_TOP_FILE_PANEL_H      170
#define DECODE_PANEL_MARGIN          20
#define DECODE_FIELD_COUNT           12
#define DECODE_FIELD_NONE           -1
#define DECODE_DEFAULT_SPS           16
#define DECODE_DEFAULT_MAX_SYMBOLS   8192
#define DECODE_ASCII_MAX              8192
#define DECODE_PREAMBLE_MAX_BITS      1024
#define DECODE_LEFT_TAB_DECODER       0
#define DECODE_LEFT_TAB_PREAMBLE      1

typedef enum Type_Decode_Modulation {
    DECODE_MOD_OOK_SYMBOL = 0,
    DECODE_MOD_OOK_RAW,
    DECODE_MOD_BPSK,
    DECODE_MOD_QPSK,
    DECODE_MOD_PSK8,
    DECODE_MOD_FSK2,
    DECODE_MOD_GFSK,
    DECODE_MOD_FSK4,
    DECODE_MOD_AFSK,
    DECODE_MOD_QAM16,
    DECODE_MOD_QAM64,
    DECODE_MOD_COUNT
} Type_Decode_Modulation;

enum {
    DECODE_FIELD_SAMPLES_PER_SYMBOL = 0,
    DECODE_FIELD_BITS_PER_SYMBOL,
    DECODE_FIELD_START_SAMPLE,
    DECODE_FIELD_MAX_SYMBOLS,
    DECODE_FIELD_ASCII_BYTE_LEN,
    DECODE_FIELD_PREAMBLE_SPS_START,
    DECODE_FIELD_PREAMBLE_SPS_END,
    DECODE_FIELD_PREAMBLE_START_BIT,
    DECODE_FIELD_PREAMBLE_SEARCH_BITS,
    DECODE_FIELD_PREAMBLE_MIN_PREFIX_LEN,
    DECODE_FIELD_PREAMBLE_MAX_PREFIX_LEN,
    DECODE_FIELD_PREAMBLE_MIN_REPEATS
};

int Global_Decode_Mode = 0;

static char Global_Decode_Record_Dir[DECODE_MAX_PATH] = "Recordings";
static char Global_Decode_Files[DECODE_MAX_FILES][DECODE_MAX_NAME];
static int  Global_Decode_File_Count = 0;
static int  Global_Decode_Selected_File = -1;
static int  Global_Decode_File_Scroll = 0;

static int  Global_Decode_Modulation = DECODE_MOD_OOK_SYMBOL;
static int  Global_Decode_Mod_Dropdown_Open = 0;
static int  Global_Decode_Mod_Dropdown_Hover = -1;
static int  Global_Decode_Left_Tab = DECODE_LEFT_TAB_DECODER;

static char Global_Decode_Field_Text[DECODE_FIELD_COUNT][DECODE_MAX_TEXT] = {
    "16", "1", "0", "8192", "8", "300", "500", "0", "100", "2", "8", "3"
};
static int  Global_Decode_Field_Cursor[DECODE_FIELD_COUNT] = {2, 1, 1, 4, 1, 3, 3, 1, 3, 1, 1, 1};
static int  Global_Decode_Active_Field = DECODE_FIELD_NONE;

static int  Global_Decode_Normalize = 1;
static int  Global_Decode_Invert_Bits = 0;
static int  Global_Decode_Skip_Whitespace = 1;
static int  Global_Decode_Ascii_Enable = 0;

static char Global_Decode_Ascii_Text[DECODE_ASCII_MAX];
static int  Global_Decode_Ascii_Len = 0;

static char Global_Decode_Bitstream[DECODE_BITSTREAM_MAX];
static int  Global_Decode_Bitstream_Len = 0;
static int  Global_Decode_Bit_Scroll = 0;
static int  Global_Decode_Bit_Edit_Active = 0;
static int  Global_Decode_Bit_Cursor = 0;
static int  Global_Decode_Bit_Selecting = 0;
static int  Global_Decode_Bit_Selection_Start = -1;
static int  Global_Decode_Bit_Selection_End = -1;
static int  Global_Decode_Bit_Last_Char_W = 8;
static int  Global_Decode_Bit_Last_Line_H = 18;
static int  Global_Decode_Bit_Last_Cols = 8;
static int  Global_Decode_Bit_Last_Visible_Lines = 1;
static char Global_Decode_Status[512] = "Select a .complex16 file, choose a GNU Radio demodulator, then Decode.";
static Uint64 Global_Decode_Status_Time = 0;

static int  Global_Decode_Preamble_Has_Candidate = 0;
static int  Global_Decode_Preamble_Candidate_SPS = 0;
static int  Global_Decode_Preamble_Candidate_Start_Bit = 0;
static int  Global_Decode_Preamble_Candidate_Length = 0;
static int  Global_Decode_Preamble_Candidate_Repeats = 0;
static int  Global_Decode_Preamble_Next_SPS = 0;
static int  Global_Decode_Preamble_Next_Bit = 0;
static char Global_Decode_Preamble_Candidate_Bits[DECODE_PREAMBLE_MAX_BITS];
static int  Global_Decode_Preamble_Candidate_Bit_Len = 0;
static volatile int Global_Decode_Preamble_Searching = 0;
static volatile int Global_Decode_Preamble_Progress = 0;
static volatile int Global_Decode_Preamble_Progress_SPS = 0;
static SDL_Thread *Global_Decode_Preamble_Thread = NULL;
static TTF_Font *Global_Decode_Preamble_Progress_Font = NULL;

static int  Global_Decode_File_Search_Open = 0;
static int  Global_Decode_File_Search_Active = 0;
static int  Global_Decode_File_Search_Cursor = 0;
static int  Global_Decode_File_Search_Scroll = 0;
static int  Global_Decode_File_Search_Hover = -1;
static char Global_Decode_File_Search_Text[DECODE_FILE_SEARCH_TEXT_MAX] = "";

static SDL_Color Decode_BG        = {0,   0,   0,   255};
static SDL_Color Decode_Panel     = {0,   10,  4,   245};
static SDL_Color Decode_Panel_2   = {0,   18,  8,   255};
static SDL_Color Decode_Border    = {0,   150, 60,  255};
static SDL_Color Decode_Border_Hi = {0,   255, 90,  255};
static SDL_Color Decode_Text      = {0,   255, 90,  255};
static SDL_Color Decode_Muted     = {0,   155, 65,  255};
static SDL_Color Decode_Warn      = {255, 180, 40,  255};
static SDL_Color Decode_Red       = {255, 75,  55,  255};
static SDL_Color Decode_Blue      = {70,  190, 255, 255};

static const char *DECODE_MOD_LABELS[DECODE_MOD_COUNT] = {
    "OOK / ASK Symbol Slicer",
    "OOK RAW Pulse Decoder",
    "BPSK",
    "QPSK",
    "8PSK",
    "2-FSK / FSK",
    "GFSK",
    "4-FSK",
    "AFSK",
    "16QAM",
    "64QAM"
};

static void decode_get_layout(int win_w,
                              int win_h,
                              SDL_Rect *file_panel,
                              SDL_Rect *file_list,
                              SDL_Rect *controls,
                              SDL_Rect *output);
static SDL_Rect decode_file_search_popup_rect(int win_w, int win_h);
static SDL_Rect decode_file_search_input_rect(SDL_Rect popup);
static int decode_filtered_file_count(void);
static int decode_filtered_index_to_file_index(int filtered_index);
static void decode_file_search_clamp_scroll(void);
static void decode_close_file_search_menu(void);
static void decode_file_search_select_index(int index);
static void decode_short_text(TTF_Font *font, const char *src, char *dst, size_t dst_size, int max_px);
static void decode_copy_bitstream_to_clipboard(void);
static int decode_parse_int_field(int field, int fallback, int low, int high);
static void decode_set_status(const char *msg);
static void decode_bit_metrics(TTF_Font *font, SDL_Rect rect, int *char_w, int *line_h, int *cols, int *visible_lines);
static int decode_bit_index_from_point(int x, int y, TTF_Font *font, SDL_Rect rect);
static int decode_get_bit_selection(int *start, int *end);
static void decode_clear_bit_selection(void);
static void decode_clamp_bit_cursor(void);
static void decode_set_bit_cursor_visible(void);
static void decode_delete_selected_bits(void);
static void decode_insert_bit_text(const char *text);
static void decode_backspace_bitstream(void);
static void decode_delete_bitstream(void);
static void decode_update_ascii_from_bitstream(void);
static void decode_draw_ascii_panel(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect);

static int decode_run_helper_capture_bits(int samples_per_symbol, int max_bits, char *bits, int bits_cap);
static int decode_find_repeated_preamble_in_bits(const char *bits, int bit_len, int min_start, int min_prefix_len, int max_prefix_len, int min_repeats, int *found_start, int *found_len, int *found_repeats);
static int decode_preamble_search_next(int reset);
static int decode_start_preamble_search_thread(int reset);
static int decode_preamble_search_thread_entry(void *data);
static int decode_preamble_search_thread_entry(void *data)
{
    int reset = (int)(intptr_t)data;

    decode_preamble_search_next(reset);

    Global_Decode_Preamble_Searching = 0;
    Global_Decode_Preamble_Thread = NULL;
    return 0;
}

static int decode_start_preamble_search_thread(int reset)
{
    if (Global_Decode_Preamble_Searching) {
        decode_set_status("Preamble search already running.");
        return 0;
    }

    Global_Decode_Preamble_Searching = 1;
    Global_Decode_Preamble_Progress = 0;
    Global_Decode_Preamble_Progress_SPS = 0;

    decode_set_status(reset ? "Preamble search started." : "Preamble search continuing.");

    Global_Decode_Preamble_Thread =
        SDL_CreateThread(decode_preamble_search_thread_entry,
                         "decode_preamble_search",
                         (void *)(intptr_t)(reset ? 1 : 0));

    if (!Global_Decode_Preamble_Thread) {
        Global_Decode_Preamble_Searching = 0;
        Global_Decode_Preamble_Progress = 0;
        decode_set_status("Could not start preamble search thread.");
        return 0;
    }

    SDL_DetachThread(Global_Decode_Preamble_Thread);
    return 1;
}

static void decode_export_preamble_candidate_to_decoder(void);
static TTF_Font *decode_get_progress_font(TTF_Font *fallback);
static void decode_draw_centered_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect, SDL_Color color);

static int decode_point_in_rect(int x, int y, SDL_Rect r){
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void decode_copy_text(char *dst, size_t dst_size, const char *src){
    size_t i = 0;
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int decode_name_compare(const void *a, const void *b){
    const char *aa = (const char *)a;
    const char *bb = (const char *)b;
    return strcasecmp(aa, bb);
}

static int decode_has_complex16_extension(const char *name){
    const char *dot = NULL;
    if (!name) return 0;
    dot = strrchr(name, '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".complex16") == 0 ||
           strcasecmp(dot, ".c16") == 0 ||
           strcasecmp(dot, ".iq16") == 0;
}

static int decode_text_contains_ci(const char *haystack, const char *needle){
    size_t needle_len;
    if (!needle || needle[0] == '\0') return 1;
    if (!haystack) return 0;

    needle_len = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < needle_len && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == needle_len) return 1;
    }
    return 0;
}


static void decode_get_layout(int win_w,
                              int win_h,
                              SDL_Rect *file_panel,
                              SDL_Rect *file_list,
                              SDL_Rect *controls,
                              SDL_Rect *output)
{
    int margin = DECODE_PANEL_MARGIN;
    int file_h = win_h / 5;
    int bottom_y;
    int bottom_h;
    int controls_w = 360;

    if (file_h < DECODE_TOP_FILE_PANEL_H) file_h = DECODE_TOP_FILE_PANEL_H;
    if (file_h > 230) file_h = 230;
    if (win_h < 560) file_h = 140;

    if (controls_w > win_w / 3) controls_w = win_w / 3;
    if (controls_w < 300) controls_w = 300;

    if (file_panel) {
        *file_panel = (SDL_Rect){margin, margin, win_w - 2 * margin, file_h};
        if (file_panel->w < 320) file_panel->w = 320;
    }

    if (file_list) {
        SDL_Rect panel = {margin, margin, win_w - 2 * margin, file_h};
        *file_list = (SDL_Rect){panel.x + 12, panel.y + 58, panel.w - 24, panel.h - 70};
        if (file_list->h < DECODE_ROW_H) file_list->h = DECODE_ROW_H;
    }

    bottom_y = margin + file_h + margin;
    bottom_h = win_h - bottom_y - margin;
    if (bottom_h < 260) bottom_h = 260;

    if (controls) {
        *controls = (SDL_Rect){margin, bottom_y, controls_w, bottom_h};
    }

    if (output) {
        int out_x = margin + controls_w + margin;
        *output = (SDL_Rect){out_x, bottom_y, win_w - out_x - margin, bottom_h};
        if (output->w < 320) output->w = 320;
    }
}

static void decode_short_text(TTF_Font *font, const char *src, char *dst, size_t dst_size, int max_px)
{
    int len;
    int w = 0;
    int h = 0;

    if (!dst || dst_size == 0) return;
    if (!src) src = "";

    decode_copy_text(dst, dst_size, src);
    if (!font || max_px <= 0) return;

    if (TTF_SizeText(font, dst, &w, &h) != 0 || w <= max_px) return;

    len = (int)strlen(dst);
    while (len > 3) {
        dst[len - 3] = '.';
        dst[len - 2] = '.';
        dst[len - 1] = '.';
        dst[len] = '\0';
        if (TTF_SizeText(font, dst, &w, &h) == 0 && w <= max_px) return;
        len--;
        dst[len] = '\0';
    }
}

static void decode_set_status(const char *msg){
    decode_copy_text(Global_Decode_Status, sizeof(Global_Decode_Status), msg ? msg : "");
    Global_Decode_Status_Time = SDL_GetTicks64();
}

static TTF_Font *decode_get_progress_font(TTF_Font *fallback)
{
    const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        NULL
    };

    if (Global_Decode_Preamble_Progress_Font) {
        return Global_Decode_Preamble_Progress_Font;
    }

    for (int i = 0; paths[i] != NULL; i++) {
        Global_Decode_Preamble_Progress_Font = TTF_OpenFont(paths[i], 24);
        if (Global_Decode_Preamble_Progress_Font) {
            return Global_Decode_Preamble_Progress_Font;
        }
    }

    return fallback;
}

static void decode_draw_centered_text(SDL_Renderer *renderer,
                                      TTF_Font *font,
                                      const char *text,
                                      SDL_Rect rect,
                                      SDL_Color color)
{
    int tw = 0;
    int th = 0;

    if (!renderer || !font || !text) return;

    if (TTF_SizeText(font, text, &tw, &th) != 0) {
        tw = 0;
        th = 0;
    }

    draw_text(renderer,
              font,
              text,
              rect.x + (rect.w - tw) / 2,
              rect.y + (rect.h - th) / 2,
              color);
}

static int decode_get_bit_selection(int *start, int *end)
{
    int a = Global_Decode_Bit_Selection_Start;
    int b = Global_Decode_Bit_Selection_End;

    if (a < 0 || b < 0 || a == b || Global_Decode_Bitstream_Len <= 0) return 0;
    if (a > b) {
        int tmp = a;
        a = b;
        b = tmp;
    }

    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (a > Global_Decode_Bitstream_Len) a = Global_Decode_Bitstream_Len;
    if (b > Global_Decode_Bitstream_Len) b = Global_Decode_Bitstream_Len;
    if (a >= b) return 0;

    if (start) *start = a;
    if (end) *end = b;
    return 1;
}

static void decode_clear_bit_selection(void)
{
    Global_Decode_Bit_Selecting = 0;
    Global_Decode_Bit_Selection_Start = -1;
    Global_Decode_Bit_Selection_End = -1;
}

static void decode_clamp_bit_cursor(void)
{
    if (Global_Decode_Bit_Cursor < 0) Global_Decode_Bit_Cursor = 0;
    if (Global_Decode_Bit_Cursor > Global_Decode_Bitstream_Len) {
        Global_Decode_Bit_Cursor = Global_Decode_Bitstream_Len;
    }
}

static void decode_set_bit_cursor_visible(void)
{
    int cols = Global_Decode_Bit_Last_Cols;
    int visible = Global_Decode_Bit_Last_Visible_Lines;
    int line;

    if (cols < 1) cols = 8;
    if (visible < 1) visible = 1;

    decode_clamp_bit_cursor();

    line = Global_Decode_Bit_Cursor / cols;
    if (Global_Decode_Bit_Cursor == Global_Decode_Bitstream_Len &&
        Global_Decode_Bitstream_Len > 0 &&
        (Global_Decode_Bitstream_Len % cols) == 0) {
        line = (Global_Decode_Bitstream_Len - 1) / cols;
    }

    if (line < Global_Decode_Bit_Scroll) {
        Global_Decode_Bit_Scroll = line;
    }
    else if (line >= Global_Decode_Bit_Scroll + visible) {
        Global_Decode_Bit_Scroll = line - visible + 1;
    }

    if (Global_Decode_Bit_Scroll < 0) Global_Decode_Bit_Scroll = 0;
}

static void decode_delete_selected_bits(void)
{
    int start = 0;
    int end = 0;
    int remove_len;

    if (!decode_get_bit_selection(&start, &end)) return;

    remove_len = end - start;
    if (remove_len <= 0) return;

    memmove(Global_Decode_Bitstream + start,
            Global_Decode_Bitstream + end,
            (size_t)(Global_Decode_Bitstream_Len - end + 1));
    Global_Decode_Bitstream_Len -= remove_len;
    Global_Decode_Bit_Cursor = start;
    decode_clear_bit_selection();
    decode_set_bit_cursor_visible();
    decode_update_ascii_from_bitstream();
}

static void decode_insert_bit_text(const char *text)
{
    char filtered[1024];
    int filtered_len = 0;

    if (!text) return;

    for (const char *p = text; *p && filtered_len + 1 < (int)sizeof(filtered); p++) {
        if (*p == '0' || *p == '1') {
            filtered[filtered_len++] = *p;
        }
    }

    if (filtered_len <= 0) return;

    if (decode_get_bit_selection(NULL, NULL)) {
        decode_delete_selected_bits();
    }

    decode_clamp_bit_cursor();
    if (Global_Decode_Bitstream_Len + filtered_len >= DECODE_BITSTREAM_MAX) {
        filtered_len = DECODE_BITSTREAM_MAX - Global_Decode_Bitstream_Len - 1;
    }
    if (filtered_len <= 0) {
        decode_set_status("Bitstream buffer is full.");
        return;
    }

    memmove(Global_Decode_Bitstream + Global_Decode_Bit_Cursor + filtered_len,
            Global_Decode_Bitstream + Global_Decode_Bit_Cursor,
            (size_t)(Global_Decode_Bitstream_Len - Global_Decode_Bit_Cursor + 1));
    memcpy(Global_Decode_Bitstream + Global_Decode_Bit_Cursor, filtered, (size_t)filtered_len);

    Global_Decode_Bitstream_Len += filtered_len;
    Global_Decode_Bit_Cursor += filtered_len;
    decode_clear_bit_selection();
    decode_set_bit_cursor_visible();
    decode_update_ascii_from_bitstream();
    decode_set_status("Bitstream edited. ASCII refreshed.");
}

static void decode_backspace_bitstream(void)
{
    if (decode_get_bit_selection(NULL, NULL)) {
        decode_delete_selected_bits();
        decode_set_status("Selected bits deleted. ASCII refreshed.");
        return;
    }

    decode_clamp_bit_cursor();
    if (Global_Decode_Bit_Cursor <= 0 || Global_Decode_Bitstream_Len <= 0) return;

    memmove(Global_Decode_Bitstream + Global_Decode_Bit_Cursor - 1,
            Global_Decode_Bitstream + Global_Decode_Bit_Cursor,
            (size_t)(Global_Decode_Bitstream_Len - Global_Decode_Bit_Cursor + 1));
    Global_Decode_Bitstream_Len--;
    Global_Decode_Bit_Cursor--;
    decode_set_bit_cursor_visible();
    decode_update_ascii_from_bitstream();
    decode_set_status("Bit deleted. ASCII refreshed.");
}

static void decode_delete_bitstream(void)
{
    if (decode_get_bit_selection(NULL, NULL)) {
        decode_delete_selected_bits();
        decode_set_status("Selected bits deleted. ASCII refreshed.");
        return;
    }

    decode_clamp_bit_cursor();
    if (Global_Decode_Bit_Cursor >= Global_Decode_Bitstream_Len || Global_Decode_Bitstream_Len <= 0) return;

    memmove(Global_Decode_Bitstream + Global_Decode_Bit_Cursor,
            Global_Decode_Bitstream + Global_Decode_Bit_Cursor + 1,
            (size_t)(Global_Decode_Bitstream_Len - Global_Decode_Bit_Cursor));
    Global_Decode_Bitstream_Len--;
    decode_set_bit_cursor_visible();
    decode_update_ascii_from_bitstream();
    decode_set_status("Bit deleted. ASCII refreshed.");
}

static void decode_update_ascii_from_bitstream(void)
{
    int byte_len;
    int bit_count = 0;
    unsigned int value = 0;

    Global_Decode_Ascii_Len = 0;
    Global_Decode_Ascii_Text[0] = '\0';

    if (!Global_Decode_Ascii_Enable || Global_Decode_Bitstream_Len <= 0) return;

    byte_len = decode_parse_int_field(DECODE_FIELD_ASCII_BYTE_LEN, 8, 1, 16);

    for (int i = 0; i < Global_Decode_Bitstream_Len; i++) {
        char ch = Global_Decode_Bitstream[i];
        if (ch != '0' && ch != '1') continue;

        value = (value << 1) | (unsigned int)(ch == '1');
        bit_count++;

        if (bit_count == byte_len) {
            char out_ch = '.';
            if (value >= 32U && value <= 126U) {
                out_ch = (char)value;
            }
            if (Global_Decode_Ascii_Len + 2 >= DECODE_ASCII_MAX) break;
            Global_Decode_Ascii_Text[Global_Decode_Ascii_Len++] = out_ch;
            Global_Decode_Ascii_Text[Global_Decode_Ascii_Len] = '\0';
            bit_count = 0;
            value = 0;
        }
    }
}


static void decode_copy_bitstream_to_clipboard(void)
{
    int start = 0;
    int end = 0;
    int copy_len;
    char *copy_text;

    if (Global_Decode_Bitstream_Len <= 0) {
        decode_set_status("No bitstream to copy.");
        return;
    }

    if (!decode_get_bit_selection(&start, &end)) {
        start = 0;
        end = Global_Decode_Bitstream_Len;
    }

    copy_len = end - start;
    if (copy_len <= 0) {
        decode_set_status("No selected bitstream text to copy.");
        return;
    }

    copy_text = (char *)malloc((size_t)copy_len + 1U);
    if (!copy_text) {
        decode_set_status("Could not allocate bitstream copy buffer.");
        return;
    }

    memcpy(copy_text, Global_Decode_Bitstream + start, (size_t)copy_len);
    copy_text[copy_len] = '\0';

    if (SDL_SetClipboardText(copy_text) == 0) {
        if (start == 0 && end == Global_Decode_Bitstream_Len) {
            decode_set_status("Bitstream copied to clipboard.");
        }
        else {
            decode_set_status("Selected bitstream copied to clipboard.");
        }
    }
    else {
        decode_set_status("Could not copy bitstream to clipboard.");
    }

    free(copy_text);
}

static int decode_parse_int_field(int field, int fallback, int low, int high){
    long v;
    char *end = NULL;

    if (field < 0 || field >= DECODE_FIELD_COUNT) return fallback;
    v = strtol(Global_Decode_Field_Text[field], &end, 10);
    if (end == Global_Decode_Field_Text[field]) v = fallback;
    if (v < low) v = low;
    if (v > high) v = high;
    return (int)v;
}

static void decode_update_bits_per_symbol_from_modulation(void){
    int bps = 1;

    switch (Global_Decode_Modulation) {
        case DECODE_MOD_QPSK:
        case DECODE_MOD_FSK4:
            bps = 2;
            break;
        case DECODE_MOD_PSK8:
            bps = 3;
            break;
        case DECODE_MOD_QAM16:
            bps = 4;
            break;
        case DECODE_MOD_QAM64:
            bps = 6;
            break;
        case DECODE_MOD_OOK_SYMBOL:
        case DECODE_MOD_OOK_RAW:
        case DECODE_MOD_BPSK:
        case DECODE_MOD_FSK2:
        case DECODE_MOD_GFSK:
        case DECODE_MOD_AFSK:
        default:
            bps = 1;
            break;
    }

    snprintf(Global_Decode_Field_Text[DECODE_FIELD_BITS_PER_SYMBOL],
             sizeof(Global_Decode_Field_Text[DECODE_FIELD_BITS_PER_SYMBOL]),
             "%d",
             bps);
    Global_Decode_Field_Cursor[DECODE_FIELD_BITS_PER_SYMBOL] =
        (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_BITS_PER_SYMBOL]);
}

static const char *decode_mod_arg(void){
    switch (Global_Decode_Modulation) {
        case DECODE_MOD_OOK_RAW:    return "ook_raw";
        case DECODE_MOD_BPSK:       return "bpsk";
        case DECODE_MOD_QPSK:       return "qpsk";
        case DECODE_MOD_PSK8:       return "psk8";
        case DECODE_MOD_FSK2:       return "fsk2";
        case DECODE_MOD_GFSK:       return "gfsk";
        case DECODE_MOD_FSK4:       return "fsk4";
        case DECODE_MOD_AFSK:       return "afsk";
        case DECODE_MOD_QAM16:      return "qam16";
        case DECODE_MOD_QAM64:      return "qam64";
        case DECODE_MOD_OOK_SYMBOL:
        default:                    return "ook";
    }
}

static int decode_find_gnuradio_helper(char *out, size_t out_size){
    const char *paths[] = {
        "src/scripts/gnuradio_decode_file.py",
        "./src/scripts/gnuradio_decode_file.py",
        "scripts/gnuradio_decode_file.py",
        "./scripts/gnuradio_decode_file.py",
        "gnuradio_decode_file.py",
        "./gnuradio_decode_file.py",
        NULL
    };

    if (!out || out_size == 0) return 0;

    for (int i = 0; paths[i]; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0 && S_ISREG(st.st_mode)) {
            snprintf(out, out_size, "%s", paths[i]);
            return 1;
        }
    }

    out[0] = '\0';
    return 0;
}

static void decode_shell_quote(char *out, size_t out_size, const char *src){
    size_t pos = 0;

    if (!out || out_size == 0) return;
    if (!src) src = "";

    if (pos + 1 < out_size) out[pos++] = '\'';

    for (const char *p = src; *p && pos + 5 < out_size; p++) {
        if (*p == '\'') {
            out[pos++] = '\'';
            out[pos++] = '\\';
            out[pos++] = '\'';
            out[pos++] = '\'';
        }
        else {
            out[pos++] = *p;
        }
    }

    if (pos + 1 < out_size) out[pos++] = '\'';
    out[pos] = '\0';
}

static void decode_scan_files(void){
    DIR *dir;
    struct dirent *entry;

    Global_Decode_File_Count = 0;
    Global_Decode_Selected_File = -1;
    Global_Decode_File_Scroll = 0;

    dir = opendir(Global_Decode_Record_Dir);
    if (!dir) {
        decode_set_status("Could not open recording directory.");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[DECODE_MAX_PATH + DECODE_MAX_NAME + 4];
        struct stat st;

        if (entry->d_name[0] == '.') continue;
        if (!decode_has_complex16_extension(entry->d_name)) continue;

        snprintf(path, sizeof(path), "%s/%s", Global_Decode_Record_Dir, entry->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (Global_Decode_File_Count < DECODE_MAX_FILES) {
            decode_copy_text(Global_Decode_Files[Global_Decode_File_Count],
                             sizeof(Global_Decode_Files[Global_Decode_File_Count]),
                             entry->d_name);
            Global_Decode_File_Count++;
        }
    }

    closedir(dir);

    qsort(Global_Decode_Files,
          (size_t)Global_Decode_File_Count,
          sizeof(Global_Decode_Files[0]),
          decode_name_compare);

    if (Global_Decode_File_Count > 0) {
        Global_Decode_Selected_File = 0;
        decode_set_status("Recordings scanned. Select a file and press Decode.");
    } else {
        decode_set_status("No .complex16 files found in the recording directory.");
    }
}

static void decode_open_file_search_menu(void){
    if (Global_Decode_File_Count <= 0) {
        decode_scan_files();
    }

    Global_Decode_File_Search_Open = 1;
    Global_Decode_File_Search_Active = 1;
    Global_Decode_File_Search_Hover = -1;
    Global_Decode_File_Search_Text[0] = '\0';
    Global_Decode_File_Search_Cursor = 0;
    Global_Decode_File_Search_Scroll = 0;
    Global_Decode_Active_Field = DECODE_FIELD_NONE;
    Global_Decode_Mod_Dropdown_Open = 0;
    decode_set_status("Filename search menu opened.");
    SDL_StartTextInput();
}

static void decode_close_file_search_menu(void)
{
    Global_Decode_File_Search_Open = 0;
    Global_Decode_File_Search_Active = 0;
    Global_Decode_File_Search_Hover = -1;
}

static void decode_file_search_clamp_scroll(void)
{
    int filtered_count = decode_filtered_file_count();
    int visible = 14;
    int max_scroll = filtered_count - visible;

    if (max_scroll < 0) max_scroll = 0;
    if (Global_Decode_File_Search_Scroll < 0) Global_Decode_File_Search_Scroll = 0;
    if (Global_Decode_File_Search_Scroll > max_scroll) Global_Decode_File_Search_Scroll = max_scroll;
}

static void decode_file_search_select_index(int index)
{
    if (index < 0 || index >= Global_Decode_File_Count) return;

    Global_Decode_Selected_File = index;
    Global_Decode_File_Scroll = Global_Decode_Selected_File - 2;
    if (Global_Decode_File_Scroll < 0) Global_Decode_File_Scroll = 0;

    decode_close_file_search_menu();

    {
        char msg[512];
        snprintf(msg, sizeof(msg), "Selected %.220s", Global_Decode_Files[Global_Decode_Selected_File]);
        decode_set_status(msg);
    }
}


static int decode_selected_file_path(char *out, size_t out_size){
    if (!out || out_size == 0) return 0;
    if (Global_Decode_Selected_File < 0 || Global_Decode_Selected_File >= Global_Decode_File_Count) return 0;
    snprintf(out,
             out_size,
             "%s/%s",
             Global_Decode_Record_Dir,
             Global_Decode_Files[Global_Decode_Selected_File]);
    return 1;
}

static int decode_append_bit(char bit){
    if (Global_Decode_Bitstream_Len + 2 >= DECODE_BITSTREAM_MAX) return 0;
    Global_Decode_Bitstream[Global_Decode_Bitstream_Len++] = bit;
    Global_Decode_Bitstream[Global_Decode_Bitstream_Len] = '\0';
    return 1;
}

static int decode_run_helper_capture_bits(int samples_per_symbol, int max_bits, char *bits, int bits_cap)
{
    char path[DECODE_MAX_PATH + DECODE_MAX_NAME + 4];
    char helper[DECODE_MAX_PATH];
    char q_helper[DECODE_MAX_PATH * 2];
    char q_input[(DECODE_MAX_PATH + DECODE_MAX_NAME + 4) * 2];
    char cmd[8192];
    FILE *pipe = NULL;
    int start_sample;
    int user_bps;
    int max_symbols;
    int out_len = 0;

    if (!bits || bits_cap <= 1 || max_bits <= 0) return 0;
    bits[0] = '\0';

    if (!decode_selected_file_path(path, sizeof(path))) {
        decode_set_status("No decode file selected for preamble search.");
        return -1;
    }

    if (!decode_find_gnuradio_helper(helper, sizeof(helper))) {
        decode_set_status("GNU Radio helper not found for preamble search.");
        return -1;
    }

    if (samples_per_symbol < 1) samples_per_symbol = 1;
    if (max_bits >= bits_cap) max_bits = bits_cap - 1;

    start_sample = decode_parse_int_field(DECODE_FIELD_START_SAMPLE,
                                          0,
                                          0,
                                          2000000000);
    user_bps = decode_parse_int_field(DECODE_FIELD_BITS_PER_SYMBOL,
                                      1,
                                      1,
                                      8);
    max_symbols = max_bits;
    if (user_bps > 1) {
        max_symbols = (max_bits + user_bps - 1) / user_bps + 4;
    }
    if (max_symbols < 8) max_symbols = 8;
    if (max_symbols > 200000) max_symbols = 200000;

    decode_shell_quote(q_helper, sizeof(q_helper), helper);
    decode_shell_quote(q_input, sizeof(q_input), path);

    snprintf(cmd,
             sizeof(cmd),
             "python3 %s --input %s --mod %s --sps %d --start-sample %d --max-symbols %d --bits-per-symbol %d --normalize %d --invert %d --tight 1",
             q_helper,
             q_input,
             decode_mod_arg(),
             samples_per_symbol,
             start_sample,
             max_symbols,
             user_bps,
             Global_Decode_Normalize ? 1 : 0,
             Global_Decode_Invert_Bits ? 1 : 0);

    pipe = popen(cmd, "r");
    if (!pipe) {
        decode_set_status("Could not launch GNU Radio helper for preamble search.");
        return -1;
    }

    while (out_len < max_bits && out_len + 1 < bits_cap) {
        int ch = fgetc(pipe);
        if (ch == EOF) break;
        if (ch == '0' || ch == '1') {
            bits[out_len++] = (char)ch;
        }
    }
    bits[out_len] = '\0';

    pclose(pipe);
    return out_len;
}

static int decode_pattern_has_zero_and_one(const char *bits, int start, int len)
{
    int has_zero = 0;
    int has_one = 0;

    if (!bits || len <= 0) return 0;
    for (int i = 0; i < len; i++) {
        if (bits[start + i] == '0') has_zero = 1;
        else if (bits[start + i] == '1') has_one = 1;
        if (has_zero && has_one) return 1;
    }
    return 0;
}

static int decode_find_repeated_preamble_in_bits(const char *bits,
                                                 int bit_len,
                                                 int min_start,
                                                 int min_prefix_len,
                                                 int max_prefix_len,
                                                 int min_repeats,
                                                 int *found_start,
                                                 int *found_len,
                                                 int *found_repeats)
{
    if (!bits || bit_len < 6) return 0;
    if (min_prefix_len < 1) min_prefix_len = 1;
    if (max_prefix_len < min_prefix_len) max_prefix_len = min_prefix_len;
    if (min_repeats < 2) min_repeats = 2;
    if (min_start < 0) min_start = 0;
    if (min_start > bit_len - (2 * min_repeats)) return 0;

    for (int start = min_start; start <= bit_len - (2 * min_repeats); start++) {
        int best_len = 0;
        int best_repeats = 0;
        int best_score = 0;
        int max_len = (bit_len - start) / min_repeats;

        if (max_len > max_prefix_len) max_len = max_prefix_len;
        if (max_len < min_prefix_len) continue;

        for (int len = min_prefix_len; len <= max_len; len++) {
            int repeats = 1;
            int score;

            if (!decode_pattern_has_zero_and_one(bits, start, len)) continue;

            while (start + (repeats + 1) * len <= bit_len &&
                   memcmp(bits + start, bits + start + repeats * len, (size_t)len) == 0) {
                repeats++;
            }

            if (repeats < min_repeats) continue;

            score = repeats * len;
            if (score > best_score || (score == best_score && len > best_len)) {
                best_score = score;
                best_len = len;
                best_repeats = repeats;
            }
        }

        if (best_len > 0 && best_repeats >= min_repeats) {
            if (found_start) *found_start = start;
            if (found_len) *found_len = best_len;
            if (found_repeats) *found_repeats = best_repeats;
            return 1;
        }
    }

    return 0;
}

static int decode_preamble_search_next(int reset)
{
    int sps_start = decode_parse_int_field(DECODE_FIELD_PREAMBLE_SPS_START, 300, 1, 1000000);
    int sps_end = decode_parse_int_field(DECODE_FIELD_PREAMBLE_SPS_END, 500, 1, 1000000);
    int base_start_bit = decode_parse_int_field(DECODE_FIELD_PREAMBLE_START_BIT, 0, 0, DECODE_PREAMBLE_MAX_BITS - 6);
    int search_bits = decode_parse_int_field(DECODE_FIELD_PREAMBLE_SEARCH_BITS, 100, 6, DECODE_PREAMBLE_MAX_BITS - 1);
    int min_prefix_len = decode_parse_int_field(DECODE_FIELD_PREAMBLE_MIN_PREFIX_LEN, 2, 1, DECODE_PREAMBLE_MAX_BITS / 2);
    int max_prefix_len = decode_parse_int_field(DECODE_FIELD_PREAMBLE_MAX_PREFIX_LEN, 8, 1, DECODE_PREAMBLE_MAX_BITS / 2);
    int min_repeats = decode_parse_int_field(DECODE_FIELD_PREAMBLE_MIN_REPEATS, 3, 2, 64);
    int sps;
    int start_bit;
    char bits[DECODE_PREAMBLE_MAX_BITS];

    if (sps_start > sps_end) {
        int tmp = sps_start;
        sps_start = sps_end;
        sps_end = tmp;
    }

    if (min_prefix_len > max_prefix_len) {
        int tmp = min_prefix_len;
        min_prefix_len = max_prefix_len;
        max_prefix_len = tmp;
    }

    Global_Decode_Preamble_Progress = 0;
    Global_Decode_Preamble_Progress_SPS = sps_start;

    if (reset || Global_Decode_Preamble_Next_SPS < sps_start || Global_Decode_Preamble_Next_SPS > sps_end) {
        sps = sps_start;
        start_bit = base_start_bit;
        Global_Decode_Preamble_Has_Candidate = 0;
    }
    else {
        sps = Global_Decode_Preamble_Next_SPS;
        start_bit = Global_Decode_Preamble_Next_Bit;
    }

    if (start_bit < base_start_bit) start_bit = base_start_bit;

    for (; sps <= sps_end; sps++) {
        int total_sps = sps_end - sps_start + 1;
        int done_sps = sps - sps_start;
        if (total_sps < 1) total_sps = 1;
        Global_Decode_Preamble_Progress_SPS = sps;
        Global_Decode_Preamble_Progress = (done_sps * 100) / total_sps;
        if (Global_Decode_Preamble_Progress < 0) Global_Decode_Preamble_Progress = 0;
        if (Global_Decode_Preamble_Progress > 99) Global_Decode_Preamble_Progress = 99;

        int bit_len = decode_run_helper_capture_bits(sps, search_bits, bits, sizeof(bits));
        int found_start = -1;
        int found_len = 0;
        int found_repeats = 0;

        if (bit_len < 0) {
            Global_Decode_Preamble_Progress = 100;
            return 0;
        }
        if (bit_len <= 0) {
            start_bit = base_start_bit;
            continue;
        }

        if (start_bit > bit_len - (2 * min_repeats)) {
            start_bit = base_start_bit;
            continue;
        }

        if (decode_find_repeated_preamble_in_bits(bits,
                                                  bit_len,
                                                  start_bit,
                                                  min_prefix_len,
                                                  max_prefix_len,
                                                  min_repeats,
                                                  &found_start,
                                                  &found_len,
                                                  &found_repeats)) {
            int copy_len = found_len;
            if (copy_len >= (int)sizeof(Global_Decode_Preamble_Candidate_Bits)) {
                copy_len = (int)sizeof(Global_Decode_Preamble_Candidate_Bits) - 1;
            }

            Global_Decode_Preamble_Has_Candidate = 1;
            Global_Decode_Preamble_Candidate_SPS = sps;
            Global_Decode_Preamble_Candidate_Start_Bit = found_start;
            Global_Decode_Preamble_Candidate_Length = found_len;
            Global_Decode_Preamble_Candidate_Repeats = found_repeats;
            Global_Decode_Preamble_Candidate_Bit_Len = copy_len;
            memcpy(Global_Decode_Preamble_Candidate_Bits, bits + found_start, (size_t)copy_len);
            Global_Decode_Preamble_Candidate_Bits[copy_len] = '\0';

            Global_Decode_Preamble_Next_SPS = sps;
            Global_Decode_Preamble_Next_Bit = found_start + 1;
            if (Global_Decode_Preamble_Next_Bit > bit_len - (2 * min_repeats)) {
                Global_Decode_Preamble_Next_SPS = sps + 1;
                Global_Decode_Preamble_Next_Bit = base_start_bit;
            }

            Global_Decode_Preamble_Progress = 100;

            {
                char msg[512];
                snprintf(msg,
                         sizeof(msg),
                         "Preamble candidate found: SPS %d, bit %d, len %d, repeats %d. Export or press Next.",
                         Global_Decode_Preamble_Candidate_SPS,
                         Global_Decode_Preamble_Candidate_Start_Bit,
                         Global_Decode_Preamble_Candidate_Length,
                         Global_Decode_Preamble_Candidate_Repeats);
                decode_set_status(msg);
            }
            return 1;
        }

        start_bit = base_start_bit;
    }

    Global_Decode_Preamble_Progress = 100;
    Global_Decode_Preamble_Has_Candidate = 0;
    Global_Decode_Preamble_Next_SPS = sps_start;
    Global_Decode_Preamble_Next_Bit = base_start_bit;
    decode_set_status("No repeated preamble candidate found in the selected SPS range.");
    return 0;
}

static void decode_export_preamble_candidate_to_decoder(void)
{
    if (!Global_Decode_Preamble_Has_Candidate) {
        decode_set_status("No preamble candidate to export.");
        return;
    }

    snprintf(Global_Decode_Field_Text[DECODE_FIELD_SAMPLES_PER_SYMBOL],
             sizeof(Global_Decode_Field_Text[DECODE_FIELD_SAMPLES_PER_SYMBOL]),
             "%d",
             Global_Decode_Preamble_Candidate_SPS);
    Global_Decode_Field_Cursor[DECODE_FIELD_SAMPLES_PER_SYMBOL] =
        (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_SAMPLES_PER_SYMBOL]);

    Global_Decode_Left_Tab = DECODE_LEFT_TAB_DECODER;
    Global_Decode_Active_Field = DECODE_FIELD_NONE;
    Global_Decode_Bit_Edit_Active = 0;
    decode_set_status("Exported candidate Samples/Symbol to Decoder tab.");
}

static int decode_run_selected_file(void){
    char path[DECODE_MAX_PATH + DECODE_MAX_NAME + 4];
    char helper[DECODE_MAX_PATH];
    char q_helper[DECODE_MAX_PATH * 2];
    char q_input[(DECODE_MAX_PATH + DECODE_MAX_NAME + 4) * 2];
    char cmd[8192];
    FILE *pipe = NULL;
    int samples_per_symbol;
    int start_sample;
    int max_symbols;
    int user_bps;
    int decoded_chars = 0;
    int rc;

    if (!decode_selected_file_path(path, sizeof(path))) {
        decode_set_status("No decode file selected.");
        return 0;
    }

    if (!decode_find_gnuradio_helper(helper, sizeof(helper))) {
        decode_set_status("GNU Radio helper not found. Put gnuradio_decode_file.py in src/scripts/, ./scripts/, or the app directory.");
        return 0;
    }

    samples_per_symbol = decode_parse_int_field(DECODE_FIELD_SAMPLES_PER_SYMBOL,
                                                DECODE_DEFAULT_SPS,
                                                1,
                                                1000000);
    start_sample = decode_parse_int_field(DECODE_FIELD_START_SAMPLE,
                                          0,
                                          0,
                                          2000000000);
    max_symbols = decode_parse_int_field(DECODE_FIELD_MAX_SYMBOLS,
                                         DECODE_DEFAULT_MAX_SYMBOLS,
                                         1,
                                         200000);
    user_bps = decode_parse_int_field(DECODE_FIELD_BITS_PER_SYMBOL,
                                      1,
                                      1,
                                      8);

    decode_shell_quote(q_helper, sizeof(q_helper), helper);
    decode_shell_quote(q_input, sizeof(q_input), path);

    snprintf(cmd,
             sizeof(cmd),
             "python3 %s --input %s --mod %s --sps %d --start-sample %d --max-symbols %d --bits-per-symbol %d --normalize %d --invert %d --tight %d",
             q_helper,
             q_input,
             decode_mod_arg(),
             samples_per_symbol,
             start_sample,
             max_symbols,
             user_bps,
             Global_Decode_Normalize ? 1 : 0,
             Global_Decode_Invert_Bits ? 1 : 0,
             Global_Decode_Skip_Whitespace ? 1 : 0);

    Global_Decode_Bitstream_Len = 0;
    Global_Decode_Bitstream[0] = '\0';
    Global_Decode_Ascii_Len = 0;
    Global_Decode_Ascii_Text[0] = '\0';
    Global_Decode_Bit_Scroll = 0;
    Global_Decode_Bit_Cursor = 0;
    Global_Decode_Bit_Edit_Active = 0;
    decode_clear_bit_selection();

    pipe = popen(cmd, "r");
    if (!pipe) {
        decode_set_status("Could not launch GNU Radio decode helper.");
        return 0;
    }

    while (Global_Decode_Bitstream_Len + 2 < DECODE_BITSTREAM_MAX) {
        int ch = fgetc(pipe);
        if (ch == EOF) break;

        if (ch == '0' || ch == '1') {
            Global_Decode_Bitstream[Global_Decode_Bitstream_Len++] = (char)ch;
            Global_Decode_Bitstream[Global_Decode_Bitstream_Len] = '\0';
            decoded_chars++;
        }
        else if (!Global_Decode_Skip_Whitespace && (ch == ' ' || ch == '\n' || ch == '\t')) {
            if (Global_Decode_Bitstream_Len > 0 &&
                Global_Decode_Bitstream[Global_Decode_Bitstream_Len - 1] != ' ' &&
                Global_Decode_Bitstream[Global_Decode_Bitstream_Len - 1] != '\n') {
                Global_Decode_Bitstream[Global_Decode_Bitstream_Len++] = ' ';
                Global_Decode_Bitstream[Global_Decode_Bitstream_Len] = '\0';
            }
        }
    }

    rc = pclose(pipe);

    if (decoded_chars <= 0) {
        Global_Decode_Ascii_Len = 0;
        Global_Decode_Ascii_Text[0] = '\0';
        if (rc != 0) {
            decode_set_status("GNU Radio decode helper failed or produced no bits. Run it in terminal for detailed errors.");
        }
        else {
            decode_set_status("GNU Radio decode helper produced no bits.");
        }
        return 0;
    }

    Global_Decode_Bit_Cursor = Global_Decode_Bitstream_Len;
    decode_set_bit_cursor_visible();
    decode_update_ascii_from_bitstream();

    {
        char msg[512];
        snprintf(msg,
                 sizeof(msg),
                 "GNU Radio decoded %d bits from %s using %s.",
                 decoded_chars,
                 Global_Decode_Files[Global_Decode_Selected_File],
                 DECODE_MOD_LABELS[Global_Decode_Modulation]);
        decode_set_status(msg);
    }

    return 1;
}

static void decode_insert_text(char *text, size_t text_size, int *cursor, const char *src){
    size_t len;
    size_t src_len;

    if (!text || text_size == 0 || !cursor || !src) return;
    len = strlen(text);
    src_len = strlen(src);
    if (*cursor < 0) *cursor = 0;
    if ((size_t)*cursor > len) *cursor = (int)len;
    if (src_len == 0 || len + src_len >= text_size) return;

    memmove(text + *cursor + src_len,
            text + *cursor,
            len - (size_t)*cursor + 1);
    memcpy(text + *cursor, src, src_len);
    *cursor += (int)src_len;
}

static void decode_backspace_text(char *text, int *cursor){
    size_t len;
    if (!text || !cursor) return;
    len = strlen(text);
    if (*cursor <= 0) return;
    if ((size_t)*cursor > len) *cursor = (int)len;
    memmove(text + *cursor - 1,
            text + *cursor,
            len - (size_t)*cursor + 1);
    (*cursor)--;
}

static void decode_delete_text(char *text, int *cursor){
    size_t len;
    if (!text || !cursor) return;
    len = strlen(text);
    if (*cursor < 0) *cursor = 0;
    if ((size_t)*cursor >= len) return;
    memmove(text + *cursor,
            text + *cursor + 1,
            len - (size_t)*cursor);
}

static void decode_clamp_cursor(char *text, int *cursor){
    int len;
    if (!text || !cursor) return;
    len = (int)strlen(text);
    if (*cursor < 0) *cursor = 0;
    if (*cursor > len) *cursor = len;
}

static int decode_filtered_index_to_file_index(int filtered_index){
    int seen = 0;
    for (int i = 0; i < Global_Decode_File_Count; i++) {
        if (!decode_text_contains_ci(Global_Decode_Files[i], Global_Decode_File_Search_Text)) continue;
        if (seen == filtered_index) return i;
        seen++;
    }
    return -1;
}

static int decode_filtered_file_count(void){
    int count = 0;
    for (int i = 0; i < Global_Decode_File_Count; i++) {
        if (decode_text_contains_ci(Global_Decode_Files[i], Global_Decode_File_Search_Text)) count++;
    }
    return count;
}

static void decode_draw_modal_button(SDL_Renderer *renderer,
                                     TTF_Font *font,
                                     SDL_Rect rect,
                                     const char *label,
                                     int hovered)
{
    SDL_Color fill = hovered ? (SDL_Color){0, 44, 16, 255} : (SDL_Color){0, 8, 3, 255};
    SDL_Color border = hovered ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 150, 60, 255};
    SDL_Color text = hovered ? (SDL_Color){235, 255, 240, 255} : (SDL_Color){0, 255, 90, 255};

    if (hovered) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_Rect glow = {rect.x - 4, rect.y - 4, rect.w + 8, rect.h + 8};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 38});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);

    if (font && label) {
        int tw = 0;
        int th = 0;
        if (TTF_SizeText(font, label, &tw, &th) != 0) {
            tw = 0;
            th = 0;
        }

        draw_text(renderer,
                  font,
                  label,
                  rect.x + (rect.w - tw) / 2,
                  rect.y + (rect.h - th) / 2,
                  text);
    }
}

static SDL_Rect decode_file_search_button_rect(int win_w, int win_h){
    SDL_Rect file_panel;
    decode_get_layout(win_w, win_h, &file_panel, NULL, NULL, NULL);

    SDL_Rect button = {
        file_panel.x + file_panel.w - 178,
        file_panel.y + 8,
        166,
        28
    };

    if (button.x < file_panel.x + 12) button.x = file_panel.x + 12;
    if (button.w > file_panel.w - 24) button.w = file_panel.w - 24;
    return button;
}

static SDL_Rect decode_file_search_popup_rect(int win_w, int win_h)
{
    SDL_Rect r = {
        (win_w - 1050) / 2,
        (win_h - 740) / 2,
        1050,
        740
    };

    if (r.x < DECODE_PANEL_MARGIN) r.x = DECODE_PANEL_MARGIN;
    if (r.y < DECODE_PANEL_MARGIN) r.y = DECODE_PANEL_MARGIN;
    if (r.w > win_w - 2 * DECODE_PANEL_MARGIN) r.w = win_w - 2 * DECODE_PANEL_MARGIN;
    if (r.h > win_h - 2 * DECODE_PANEL_MARGIN) r.h = win_h - 2 * DECODE_PANEL_MARGIN;
    if (r.w < 320) r.w = 320;
    if (r.h < 260) r.h = 260;
    return r;
}

static SDL_Rect decode_file_search_input_rect(SDL_Rect popup)
{
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = {close_btn.x - 292, popup.y + 14, 276, 30};

    if (search.x < popup.x + 180) {
        search.x = popup.x + 180;
        search.w = close_btn.x - search.x - 16;
    }

    if (search.w < 120) search.w = 120;
    return search;
}

static int decode_handle_file_search_event(const SDL_Event *event, int win_w, int win_h){
    if (!Global_Decode_File_Search_Open || !event) return 0;

    SDL_Rect popup = decode_file_search_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = decode_file_search_input_rect(popup);
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};

    if (event->type == SDL_TEXTINPUT) {
        if (Global_Decode_File_Search_Active) {
            decode_insert_text(Global_Decode_File_Search_Text,
                               sizeof(Global_Decode_File_Search_Text),
                               &Global_Decode_File_Search_Cursor,
                               event->text.text);
            Global_Decode_File_Search_Scroll = 0;
        }
        return 1;
    }

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        int len = (int)strlen(Global_Decode_File_Search_Text);

        if (key == SDLK_ESCAPE) {
            decode_close_file_search_menu();
            return 1;
        }

        if (key == SDLK_BACKSPACE) {
            decode_backspace_text(Global_Decode_File_Search_Text,
                                  &Global_Decode_File_Search_Cursor);
            Global_Decode_File_Search_Scroll = 0;
            return 1;
        }

        if (key == SDLK_DELETE) {
            decode_delete_text(Global_Decode_File_Search_Text,
                               &Global_Decode_File_Search_Cursor);
            Global_Decode_File_Search_Scroll = 0;
            return 1;
        }

        if (key == SDLK_LEFT) {
            if (Global_Decode_File_Search_Cursor > 0) Global_Decode_File_Search_Cursor--;
            return 1;
        }

        if (key == SDLK_RIGHT) {
            if (Global_Decode_File_Search_Cursor < len) Global_Decode_File_Search_Cursor++;
            return 1;
        }

        if (key == SDLK_HOME) {
            Global_Decode_File_Search_Cursor = 0;
            return 1;
        }

        if (key == SDLK_END) {
            Global_Decode_File_Search_Cursor = len;
            return 1;
        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            int index = decode_filtered_index_to_file_index(Global_Decode_File_Search_Scroll);
            if (index >= 0) decode_file_search_select_index(index);
            return 1;
        }

        if (key == SDLK_DOWN) {
            Global_Decode_File_Search_Scroll++;
            decode_file_search_clamp_scroll();
            return 1;
        }

        if (key == SDLK_UP) {
            Global_Decode_File_Search_Scroll--;
            decode_file_search_clamp_scroll();
            return 1;
        }

        return 1;
    }

    if (event->type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        decode_get_adjusted_mouse_state(&mx, &my);

        if (decode_point_in_rect(mx, my, list)) {
            Global_Decode_File_Search_Scroll -= event->wheel.y * 3;
            decode_file_search_clamp_scroll();
        }

        return 1;
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int mx = event->button.x;
        int my = event->button.y;

        if (!decode_point_in_rect(mx, my, popup)) {
            decode_close_file_search_menu();
            return 0;
        }

        if (decode_point_in_rect(mx, my, close_btn)) {
            decode_close_file_search_menu();
            return 1;
        }

        if (decode_point_in_rect(mx, my, search)) {
            Global_Decode_File_Search_Active = 1;
            return 1;
        }

        Global_Decode_File_Search_Active = 0;

        if (decode_point_in_rect(mx, my, list)) {
            int row = (my - list.y - 4) / DECODE_FILE_SEARCH_ROW_H;
            int visible = list.h / DECODE_FILE_SEARCH_ROW_H;
            if (visible < 1) visible = 1;
            if (visible > 14) visible = 14;

            if (row >= 0 && row < visible) {
                int filtered_index = Global_Decode_File_Search_Scroll + row;
                int index = decode_filtered_index_to_file_index(filtered_index);
                if (index >= 0) decode_file_search_select_index(index);
            }

            return 1;
        }

        return 1;
    }

    return 1;
}

static void decode_draw_file_search_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h){
    if (!renderer || !font || !Global_Decode_File_Search_Open) return;

    SDL_Rect popup = decode_file_search_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = decode_file_search_input_rect(popup);
    SDL_Rect current_rect = {popup.x + 18, popup.y + 62, popup.w - 36, 42};
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};
    int mx = 0;
    int my = 0;
    int filtered_count = decode_filtered_file_count();

    decode_get_adjusted_mouse_state(&mx, &my);
    decode_file_search_clamp_scroll();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 155});

    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, (SDL_Color){0, 255, 90, 255});
    SDL_Rect inner = {popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8};
    draw_outline_rect(renderer, inner, (SDL_Color){0, 150, 60, 255});

    draw_text(renderer,
              font,
              "FILENAME SEARCH",
              popup.x + 18,
              popup.y + 20,
              (SDL_Color){0, 255, 90, 255});

    decode_draw_modal_button(renderer,
                             font,
                             close_btn,
                             "Close",
                             decode_point_in_rect(mx, my, close_btn));

    draw_filled_rect(renderer,
                     search,
                     Global_Decode_File_Search_Active ?
                     (SDL_Color){0, 20, 8, 255} :
                     (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer,
                      search,
                      Global_Decode_File_Search_Active ?
                      (SDL_Color){0, 255, 90, 255} :
                      (SDL_Color){0, 150, 60, 255});

    if (Global_Decode_File_Search_Text[0]) {
        draw_text(renderer,
                  font,
                  Global_Decode_File_Search_Text,
                  search.x + 10,
                  search.y + 8,
                  (SDL_Color){0, 255, 90, 255});
    }
    else {
        draw_text(renderer,
                  font,
                  "Search file",
                  search.x + 10,
                  search.y + 8,
                  (SDL_Color){0, 155, 65, 255});
    }

    if (Global_Decode_File_Search_Active && ((SDL_GetTicks64() / 450ULL) % 2ULL) == 0ULL) {
        int tw = 0;
        int th = 0;
        char prefix[DECODE_FILE_SEARCH_TEXT_MAX];
        int cursor = Global_Decode_File_Search_Cursor;
        int len = (int)strlen(Global_Decode_File_Search_Text);

        if (cursor < 0) cursor = 0;
        if (cursor > len) cursor = len;
        snprintf(prefix, sizeof(prefix), "%.*s", cursor, Global_Decode_File_Search_Text);
        if (font && TTF_SizeText(font, prefix, &tw, &th) != 0) tw = cursor * 8;

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
        SDL_RenderDrawLine(renderer, search.x + 10 + tw, search.y + 6, search.x + 10 + tw, search.y + search.h - 6);
        SDL_RenderDrawLine(renderer, search.x + 11 + tw, search.y + 6, search.x + 11 + tw, search.y + search.h - 6);
    }

    draw_text(renderer,
              font,
              "Currently selected",
              current_rect.x,
              current_rect.y - 18,
              (SDL_Color){0, 155, 65, 255});
    draw_filled_rect(renderer, current_rect, (SDL_Color){0, 20, 8, 255});
    draw_outline_rect(renderer, current_rect, (SDL_Color){0, 255, 90, 255});

    {
        char short_name[512];
        const char *current = "(none selected)";
        if (Global_Decode_File_Count > 0 &&
            Global_Decode_Selected_File >= 0 &&
            Global_Decode_Selected_File < Global_Decode_File_Count) {
            current = Global_Decode_Files[Global_Decode_Selected_File];
        }

        decode_short_text(font,
                          current,
                          short_name,
                          sizeof(short_name),
                          current_rect.w - 20);

        draw_text(renderer,
                  font,
                  short_name,
                  current_rect.x + 10,
                  current_rect.y + 12,
                  current[0] == '(' ? (SDL_Color){0, 155, 65, 255} : (SDL_Color){0, 255, 90, 255});
    }

    draw_filled_rect(renderer, list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, list, (SDL_Color){0, 150, 60, 255});

    if (Global_Decode_File_Count <= 0) {
        char empty_msg[640];
        snprintf(empty_msg, sizeof(empty_msg), "No .complex16 files found in %s/", Global_Decode_Record_Dir);
        draw_text(renderer, font, empty_msg, list.x + 12, list.y + 14, (SDL_Color){255, 180, 40, 255});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;
    }

    if (filtered_count <= 0) {
        draw_text(renderer, font, "No files match the search.", list.x + 12, list.y + 14, (SDL_Color){255, 180, 40, 255});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;
    }

    {
        int visible = list.h / DECODE_FILE_SEARCH_ROW_H;
        if (visible > 14) visible = 14;
        if (visible < 1) visible = 1;

        Global_Decode_File_Search_Hover = -1;

        if (decode_point_in_rect(mx, my, list)) {
            int row = (my - list.y - 4) / DECODE_FILE_SEARCH_ROW_H;
            int filtered_index = Global_Decode_File_Search_Scroll + row;
            int index = decode_filtered_index_to_file_index(filtered_index);

            if (row >= 0 && row < visible && index >= 0 && index < Global_Decode_File_Count) {
                Global_Decode_File_Search_Hover = index;
            }
        }

        for (int row = 0; row < visible; row++) {
            int filtered_index = Global_Decode_File_Search_Scroll + row;
            int index = decode_filtered_index_to_file_index(filtered_index);
            SDL_Rect item = {list.x + 4, list.y + 4 + row * DECODE_FILE_SEARCH_ROW_H, list.w - 8, DECODE_FILE_SEARCH_ROW_H - 3};

            if (index < 0 || index >= Global_Decode_File_Count) break;

            int hovered = index == Global_Decode_File_Search_Hover;
            int selected = index == Global_Decode_Selected_File;
            char short_name[512];

            if (hovered) {
                draw_filled_rect(renderer, item, (SDL_Color){0, 44, 16, 255});
                SDL_Rect halo = {item.x - 2, item.y - 2, item.w + 4, item.h + 4};
                draw_outline_rect(renderer, halo, (SDL_Color){0, 255, 90, 255});
            }
            else if (selected) {
                draw_filled_rect(renderer, item, (SDL_Color){15, 85, 45, 245});
            }

            draw_outline_rect(renderer,
                              item,
                              hovered ?
                              (SDL_Color){0, 255, 90, 255} :
                              selected ?
                              (SDL_Color){0, 220, 80, 255} :
                              (SDL_Color){0, 130, 55, 255});

            decode_short_text(font,
                              Global_Decode_Files[index],
                              short_name,
                              sizeof(short_name),
                              item.w - 20);

            draw_text(renderer,
                      font,
                      short_name,
                      item.x + 10,
                      item.y + 8,
                      hovered ?
                      (SDL_Color){235, 255, 240, 255} :
                      selected ?
                      (SDL_Color){255, 255, 255, 255} :
                      (SDL_Color){0, 255, 90, 255});
        }
    }

    {
        char count_label[128];
        if (Global_Decode_File_Search_Text[0]) {
            snprintf(count_label, sizeof(count_label), "%d of %d files", filtered_count, Global_Decode_File_Count);
        }
        else {
            snprintf(count_label, sizeof(count_label), "%d files", Global_Decode_File_Count);
        }

        draw_text(renderer, font, count_label, popup.x + 18, popup.y + popup.h - 24, (SDL_Color){0, 155, 65, 255});
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void decode_draw_input_field(SDL_Renderer *renderer,
                                    TTF_Font *font,
                                    const char *label,
                                    char *text,
                                    int field,
                                    SDL_Rect rect)
{
    int active = (Global_Decode_Active_Field == field);

    draw_text(renderer, font, label, rect.x, rect.y - 18, Decode_Muted);
    draw_filled_rect(renderer, rect, (SDL_Color){0, 8, 3, 255});
    draw_outline_rect(renderer, rect, active ? Decode_Blue : Decode_Border);
    draw_text(renderer, font, text, rect.x + 8, rect.y + 8, Decode_Text);

    if (active && ((SDL_GetTicks64() / 450) % 2 == 0)) {
        int cursor = Global_Decode_Field_Cursor[field];
        int prefix_w = 0;
        char tmp[DECODE_MAX_TEXT];
        decode_clamp_cursor(text, &cursor);
        snprintf(tmp, sizeof(tmp), "%.*s", cursor, text);
        if (font) { int tmp_h = 0; TTF_SizeText(font, tmp, &prefix_w, &tmp_h); }
        SDL_SetRenderDrawColor(renderer, Decode_Blue.r, Decode_Blue.g, Decode_Blue.b, 255);
        SDL_RenderDrawLine(renderer, rect.x + 8 + prefix_w, rect.y + 6, rect.x + 8 + prefix_w, rect.y + rect.h - 6);
    }
}

static void decode_draw_checkbox(SDL_Renderer *renderer,
                                 TTF_Font *font,
                                 SDL_Rect rect,
                                 const char *label,
                                 int checked)
{
    draw_filled_rect(renderer, rect, (SDL_Color){0, 8, 3, 255});
    draw_outline_rect(renderer, rect, checked ? Decode_Border_Hi : Decode_Border);
    if (checked) {
        SDL_SetRenderDrawColor(renderer, Decode_Text.r, Decode_Text.g, Decode_Text.b, 255);
        SDL_RenderDrawLine(renderer, rect.x + 5, rect.y + rect.h / 2, rect.x + rect.w / 2, rect.y + rect.h - 6);
        SDL_RenderDrawLine(renderer, rect.x + rect.w / 2, rect.y + rect.h - 6, rect.x + rect.w - 5, rect.y + 5);
    }
    draw_text(renderer, font, label, rect.x + rect.w + 8, rect.y + 4, Decode_Muted);
}

static void decode_bit_metrics(TTF_Font *font,
                               SDL_Rect rect,
                               int *char_w,
                               int *line_h,
                               int *cols,
                               int *visible_lines)
{
    int cw = 8;
    int lh = 18;
    int c;
    int v;

    if (!font && Global_Decode_Bit_Last_Cols > 0) {
        if (char_w) *char_w = Global_Decode_Bit_Last_Char_W;
        if (line_h) *line_h = Global_Decode_Bit_Last_Line_H;
        if (cols) *cols = Global_Decode_Bit_Last_Cols;
        if (visible_lines) *visible_lines = Global_Decode_Bit_Last_Visible_Lines;
        return;
    }

    if (font) {
        int w0 = 0;
        int h0 = 0;
        int w8 = 0;
        int h8 = 0;

        if (TTF_SizeText(font, "0", &w0, &h0) == 0 && w0 > 0) cw = w0;
        if (TTF_SizeText(font, "00000000", &w8, &h8) == 0 && w8 > 0) cw = (w8 + 7) / 8;
        if (h0 > 0) lh = h0 + 2;
        if (h8 > 0 && h8 + 2 > lh) lh = h8 + 2;
    }

    if (cw < 1) cw = 8;
    if (lh < 12) lh = 18;

    c = (rect.w - 12) / cw;
    if (c < 8) c = 8;

    v = (rect.h - 18) / lh;
    if (v < 1) v = 1;

    if (char_w) *char_w = cw;
    if (line_h) *line_h = lh;
    if (cols) *cols = c;
    if (visible_lines) *visible_lines = v;
}

static int decode_bit_index_from_point(int x, int y, TTF_Font *font, SDL_Rect rect)
{
    int char_w = 8;
    int line_h = 18;
    int cols = 8;
    int visible_lines = 1;
    int col;
    int line;
    int idx;

    if (Global_Decode_Bitstream_Len <= 0) return -1;
    if (!decode_point_in_rect(x, y, rect)) return -1;

    decode_bit_metrics(font, rect, &char_w, &line_h, &cols, &visible_lines);

    col = (x - rect.x - 6) / char_w;
    line = (y - rect.y - 10) / line_h;

    if (col < 0) col = 0;
    if (line < 0) line = 0;
    if (col >= cols) col = cols - 1;
    if (line >= visible_lines) line = visible_lines - 1;

    idx = (Global_Decode_Bit_Scroll + line) * cols + col;
    if (idx < 0) idx = 0;
    if (idx > Global_Decode_Bitstream_Len) idx = Global_Decode_Bitstream_Len;
    return idx;
}

static int decode_draw_wrapped_bits(SDL_Renderer *renderer,
                                    TTF_Font *font,
                                    SDL_Rect rect)
{
    int char_w = 8;
    int line_h = 18;
    int cols = 8;
    int total_lines;
    int visible_lines;
    int first;
    int sel_start = -1;
    int sel_end = -1;

    draw_filled_rect(renderer, rect, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, rect, Decode_Border);

    decode_bit_metrics(font, rect, &char_w, &line_h, &cols, &visible_lines);
    Global_Decode_Bit_Last_Char_W = char_w;
    Global_Decode_Bit_Last_Line_H = line_h;
    Global_Decode_Bit_Last_Cols = cols;
    Global_Decode_Bit_Last_Visible_Lines = visible_lines;

    total_lines = (Global_Decode_Bitstream_Len + cols - 1) / cols;
    if (total_lines < 1) total_lines = 1;

    if (Global_Decode_Bit_Scroll > total_lines - visible_lines) {
        Global_Decode_Bit_Scroll = total_lines > visible_lines ? total_lines - visible_lines : 0;
    }
    if (Global_Decode_Bit_Scroll < 0) Global_Decode_Bit_Scroll = 0;

    if (Global_Decode_Bitstream_Len <= 0) {
        draw_text(renderer, font, "No decoded bits yet.", rect.x + 6, rect.y + 10, Decode_Warn);
        return 0;
    }

    decode_get_bit_selection(&sel_start, &sel_end);

    first = Global_Decode_Bit_Scroll * cols;
    for (int line = 0; line < visible_lines; line++) {
        int start = first + line * cols;
        int count = cols;
        char *buf;

        if (start >= Global_Decode_Bitstream_Len) break;
        if (start + count > Global_Decode_Bitstream_Len) count = Global_Decode_Bitstream_Len - start;
        if (count <= 0) break;

        if (sel_start >= 0 && sel_end > sel_start) {
            int line_start = start;
            int line_end = start + count;
            int hi_start = sel_start > line_start ? sel_start : line_start;
            int hi_end = sel_end < line_end ? sel_end : line_end;

            if (hi_start < hi_end) {
                SDL_Rect hi = {
                    rect.x + 6 + (hi_start - line_start) * char_w,
                    rect.y + 9 + line * line_h,
                    (hi_end - hi_start) * char_w,
                    line_h
                };
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                draw_filled_rect(renderer, hi, (SDL_Color){0, 115, 255, 150});
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }
        }

        buf = (char *)malloc((size_t)count + 1U);
        if (!buf) break;
        memcpy(buf, Global_Decode_Bitstream + start, (size_t)count);
        buf[count] = '\0';

        draw_text(renderer, font, buf, rect.x + 6, rect.y + 10 + line * line_h, Decode_Text);
        free(buf);
    }

    if (Global_Decode_Bit_Edit_Active && ((SDL_GetTicks64() / 450ULL) % 2ULL) == 0ULL) {
        int cursor = Global_Decode_Bit_Cursor;
        int cursor_line;
        int cursor_col;

        decode_clamp_bit_cursor();
        cursor = Global_Decode_Bit_Cursor;
        cursor_line = cursor / cols - Global_Decode_Bit_Scroll;
        cursor_col = cursor % cols;

        if (cursor == Global_Decode_Bitstream_Len &&
            Global_Decode_Bitstream_Len > 0 &&
            cursor_col == 0) {
            cursor_line = (cursor - 1) / cols - Global_Decode_Bit_Scroll;
            cursor_col = cols;
        }

        if (cursor_line >= 0 && cursor_line < visible_lines) {
            int cx = rect.x + 6 + cursor_col * char_w;
            int cy = rect.y + 9 + cursor_line * line_h;
            SDL_SetRenderDrawColor(renderer, Decode_Blue.r, Decode_Blue.g, Decode_Blue.b, 255);
            SDL_RenderDrawLine(renderer, cx, cy, cx, cy + line_h);
            SDL_RenderDrawLine(renderer, cx + 1, cy, cx + 1, cy + line_h);
        }
    }

    return total_lines;
}

void DECODE_enter_mode(const char *record_dir){
    Global_Decode_Mode = 1;
    if (record_dir && record_dir[0] != '\0') {
        decode_copy_text(Global_Decode_Record_Dir, sizeof(Global_Decode_Record_Dir), record_dir);
    }
    decode_update_bits_per_symbol_from_modulation();
    decode_scan_files();
    SDL_StartTextInput();
}

void DECODE_exit_mode(void){
    Global_Decode_Mode = 0;
    Global_Decode_Active_Field = DECODE_FIELD_NONE;
    Global_Decode_Mod_Dropdown_Open = 0;
    Global_Decode_File_Search_Open = 0;
    Global_Decode_File_Search_Active = 0;
    Global_Decode_Bit_Edit_Active = 0;
    SDL_StartTextInput();
}

int DECODE_is_text_entry_active(void){
    return Global_Decode_Mode &&
           (Global_Decode_Active_Field != DECODE_FIELD_NONE ||
            Global_Decode_Bit_Edit_Active ||
            Global_Decode_File_Search_Open ||
            Global_Decode_File_Search_Active);
}

static void decode_draw_ascii_panel(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect)
{
    char label[256];
    char shown[DECODE_ASCII_MAX];
    int byte_len;

    if (!renderer || !font) return;

    draw_filled_rect(renderer, rect, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, rect, Decode_Border);

    byte_len = decode_parse_int_field(DECODE_FIELD_ASCII_BYTE_LEN, 8, 1, 16);
    snprintf(label,
             sizeof(label),
             "ASCII: %s | byte len %d | %d chars",
             Global_Decode_Ascii_Enable ? "ON" : "OFF",
             byte_len,
             Global_Decode_Ascii_Len);
    draw_text(renderer, font, label, rect.x + 8, rect.y + 7, Decode_Muted);

    if (!Global_Decode_Ascii_Enable) {
        draw_text(renderer, font, "Enable ASCII to decode displayed bits after Decode.", rect.x + 8, rect.y + 31, Decode_Warn);
        return;
    }

    if (Global_Decode_Ascii_Len <= 0) {
        draw_text(renderer, font, "No printable ASCII yet.", rect.x + 8, rect.y + 31, Decode_Warn);
        return;
    }

    decode_short_text(font, Global_Decode_Ascii_Text, shown, sizeof(shown), rect.w - 18);
    draw_text(renderer, font, shown, rect.x + 8, rect.y + 31, Decode_Text);
}


int DECODE_handle_event(const SDL_Event *event, int win_w, int win_h){
    SDL_Rect file_panel;
    SDL_Rect file_list;
    SDL_Rect controls;
    SDL_Rect output;
    SDL_Rect search_button;
    SDL_Rect rescan_button;
    SDL_Rect decode_button;
    SDL_Rect clear_button;
    SDL_Rect copy_button;
    SDL_Rect mod_rect;
    SDL_Rect sps_rect;
    SDL_Rect bps_rect;
    SDL_Rect start_rect;
    SDL_Rect max_rect;
    SDL_Rect normalize_box;
    SDL_Rect invert_box;
    SDL_Rect tight_box;
    SDL_Rect ascii_box;
    SDL_Rect ascii_byte_rect;
    SDL_Rect bits_rect;
    SDL_Rect ascii_rect;
    SDL_Rect decoder_tab;
    SDL_Rect preamble_tab;
    SDL_Rect ps_sps_start_rect;
    SDL_Rect ps_sps_end_rect;
    SDL_Rect ps_start_bit_rect;
    SDL_Rect ps_search_bits_rect;
    SDL_Rect ps_min_prefix_rect;
    SDL_Rect ps_max_prefix_rect;
    SDL_Rect ps_min_repeats_rect;
    SDL_Rect ps_search_button;
    SDL_Rect ps_next_button;
    SDL_Rect ps_export_button;

    if (!event || !Global_Decode_Mode) return 0;

    decode_get_layout(win_w, win_h, &file_panel, &file_list, &controls, &output);
    search_button = decode_file_search_button_rect(win_w, win_h);
    rescan_button = (SDL_Rect){search_button.x - 96, search_button.y, 86, 28};
    if (rescan_button.x < file_panel.x + 160) {
        rescan_button.x = file_panel.x + 160;
    }

    decode_button = (SDL_Rect){controls.x + 18, controls.y + controls.h - 88, 106, 34};
    clear_button = (SDL_Rect){controls.x + 136, controls.y + controls.h - 88, 86, 34};
    copy_button = (SDL_Rect){output.x + output.w - 110, output.y + 12, 92, 30};
    mod_rect = (SDL_Rect){controls.x + 18, controls.y + 64, controls.w - 36, 38};
    sps_rect = (SDL_Rect){controls.x + 18, controls.y + 136, 150, 34};
    bps_rect = (SDL_Rect){controls.x + 190, controls.y + 136, controls.w - 208, 34};
    start_rect = (SDL_Rect){controls.x + 18, controls.y + 206, 150, 34};
    max_rect = (SDL_Rect){controls.x + 190, controls.y + 206, controls.w - 208, 34};
    normalize_box = (SDL_Rect){controls.x + 20, controls.y + 258, 18, 18};
    invert_box = (SDL_Rect){controls.x + 20, controls.y + 286, 18, 18};
    tight_box = (SDL_Rect){controls.x + 170, controls.y + 258, 18, 18};
    ascii_box = (SDL_Rect){controls.x + 170, controls.y + 286, 18, 18};
    ascii_byte_rect = (SDL_Rect){controls.x + 18, controls.y + 350, 150, 34};
    ascii_rect = (SDL_Rect){output.x + 8, output.y + output.h - 68, output.w - 16, 58};
    bits_rect = (SDL_Rect){output.x + 8, output.y + 72, output.w - 16, output.h - 150};
    if (bits_rect.h < 60) bits_rect.h = output.h - 84;
    {
        int tab_gap = 8;
        int tab_x = controls.x + 14;
        int tab_w = (controls.w - 28 - tab_gap) / 2;
        decoder_tab = (SDL_Rect){tab_x, controls.y + 12, tab_w, 30};
        preamble_tab = (SDL_Rect){tab_x + tab_w + tab_gap, controls.y + 12, tab_w, 30};
    }
    ps_sps_start_rect = (SDL_Rect){controls.x + 18, controls.y + 86, 150, 34};
    ps_sps_end_rect = (SDL_Rect){controls.x + 190, controls.y + 86, controls.w - 208, 34};
    ps_start_bit_rect = (SDL_Rect){controls.x + 18, controls.y + 156, 150, 34};
    ps_search_bits_rect = (SDL_Rect){controls.x + 190, controls.y + 156, controls.w - 208, 34};
    ps_min_prefix_rect = (SDL_Rect){controls.x + 18, controls.y + 226, 150, 34};
    ps_max_prefix_rect = (SDL_Rect){controls.x + 190, controls.y + 226, controls.w - 208, 34};
    ps_min_repeats_rect = (SDL_Rect){controls.x + 18, controls.y + 296, 150, 34};
    ps_search_button = (SDL_Rect){controls.x + 18, controls.y + 356, 76, 34};
    ps_next_button = (SDL_Rect){controls.x + 106, controls.y + 356, 64, 34};
    ps_export_button = (SDL_Rect){controls.x + 182, controls.y + 356, controls.w - 200, 34};

    if (decode_handle_file_search_event(event, win_w, win_h)) return 1;

    if (event->type == SDL_MOUSEMOTION && Global_Decode_Bit_Selecting) {
        int idx = decode_bit_index_from_point(event->motion.x, event->motion.y, NULL, bits_rect);
        if (idx >= 0) {
            if (idx >= Global_Decode_Bit_Selection_Start) {
                Global_Decode_Bit_Selection_End = idx + 1;
            }
            else {
                Global_Decode_Bit_Selection_End = idx;
            }
            if (Global_Decode_Bit_Selection_End > Global_Decode_Bitstream_Len) {
                Global_Decode_Bit_Selection_End = Global_Decode_Bitstream_Len;
            }
            Global_Decode_Bit_Cursor = Global_Decode_Bit_Selection_End;
            decode_clamp_bit_cursor();
        }
        return 1;
    }

    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        if (Global_Decode_Bit_Selecting) {
            int idx = decode_bit_index_from_point(event->button.x, event->button.y, NULL, bits_rect);
            if (idx >= 0) {
                if (idx == Global_Decode_Bit_Selection_Start &&
                    Global_Decode_Bit_Selection_End == Global_Decode_Bit_Selection_Start) {
                    Global_Decode_Bit_Cursor = idx;
                    decode_clear_bit_selection();
                }
                else {
                    if (idx >= Global_Decode_Bit_Selection_Start) {
                        Global_Decode_Bit_Selection_End = idx + 1;
                    }
                    else {
                        Global_Decode_Bit_Selection_End = idx;
                    }
                    if (Global_Decode_Bit_Selection_End > Global_Decode_Bitstream_Len) {
                        Global_Decode_Bit_Selection_End = Global_Decode_Bitstream_Len;
                    }
                    Global_Decode_Bit_Cursor = Global_Decode_Bit_Selection_End;
                    decode_clamp_bit_cursor();
                }
            }
            Global_Decode_Bit_Selecting = 0;
            return 1;
        }
    }

    if (event->type == SDL_TEXTINPUT &&
        Global_Decode_Bit_Edit_Active &&
        Global_Decode_Active_Field == DECODE_FIELD_NONE) {
        decode_insert_bit_text(event->text.text);
        return 1;
    }

    if (event->type == SDL_TEXTINPUT && Global_Decode_Active_Field != DECODE_FIELD_NONE) {
        int f = Global_Decode_Active_Field;
        for (const char *pp = event->text.text; *pp; pp++) {
            if (!isdigit((unsigned char)*pp)) return 1;
        }
        decode_insert_text(Global_Decode_Field_Text[f],
                           sizeof(Global_Decode_Field_Text[f]),
                           &Global_Decode_Field_Cursor[f],
                           event->text.text);
        if (f == DECODE_FIELD_ASCII_BYTE_LEN) decode_update_ascii_from_bitstream();
        return 1;
    }

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;

        if (key == SDLK_c && (SDL_GetModState() & KMOD_CTRL)) {
            decode_copy_bitstream_to_clipboard();
            return 1;
        }

        if (key == SDLK_a && (SDL_GetModState() & KMOD_CTRL) && Global_Decode_Bit_Edit_Active) {
            if (Global_Decode_Bitstream_Len > 0) {
                Global_Decode_Bit_Selection_Start = 0;
                Global_Decode_Bit_Selection_End = Global_Decode_Bitstream_Len;
                Global_Decode_Bit_Cursor = Global_Decode_Bitstream_Len;
                decode_set_bit_cursor_visible();
                decode_set_status("Bitstream selected.");
            }
            return 1;
        }

        if (Global_Decode_Bit_Edit_Active && Global_Decode_Active_Field == DECODE_FIELD_NONE) {
            if (key == SDLK_BACKSPACE) {
                decode_backspace_bitstream();
                return 1;
            }
            if (key == SDLK_DELETE) {
                decode_delete_bitstream();
                return 1;
            }
            if (key == SDLK_LEFT) {
                Global_Decode_Bit_Cursor--;
                decode_clamp_bit_cursor();
                decode_clear_bit_selection();
                decode_set_bit_cursor_visible();
                return 1;
            }
            if (key == SDLK_RIGHT) {
                Global_Decode_Bit_Cursor++;
                decode_clamp_bit_cursor();
                decode_clear_bit_selection();
                decode_set_bit_cursor_visible();
                return 1;
            }
            if (key == SDLK_HOME) {
                Global_Decode_Bit_Cursor = 0;
                decode_clear_bit_selection();
                decode_set_bit_cursor_visible();
                return 1;
            }
            if (key == SDLK_END) {
                Global_Decode_Bit_Cursor = Global_Decode_Bitstream_Len;
                decode_clear_bit_selection();
                decode_set_bit_cursor_visible();
                return 1;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                Global_Decode_Bit_Edit_Active = 0;
                decode_clear_bit_selection();
                decode_set_status("Bitstream edit mode closed.");
                return 1;
            }
            if (key == SDLK_ESCAPE) {
                Global_Decode_Bit_Edit_Active = 0;
                decode_clear_bit_selection();
                return 1;
            }
            return 1;
        }

        if (key == SDLK_ESCAPE) {
            Global_Decode_Active_Field = DECODE_FIELD_NONE;
            Global_Decode_Mod_Dropdown_Open = 0;
            Global_Decode_Bit_Edit_Active = 0;
            return 1;
        }
        if (key == SDLK_r && Global_Decode_Active_Field == DECODE_FIELD_NONE) {
            decode_scan_files();
            return 1;
        }
        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && Global_Decode_Active_Field == DECODE_FIELD_NONE) {
            if (Global_Decode_Left_Tab == DECODE_LEFT_TAB_PREAMBLE) {
                decode_start_preamble_search_thread(1);
            }
            else {
                decode_run_selected_file();
            }
            return 1;
        }
        if (key == SDLK_c && Global_Decode_Active_Field == DECODE_FIELD_NONE) {
            Global_Decode_Bitstream_Len = 0;
            Global_Decode_Bitstream[0] = '\0';
            Global_Decode_Ascii_Len = 0;
            Global_Decode_Ascii_Text[0] = '\0';
            Global_Decode_Bit_Scroll = 0;
            Global_Decode_Bit_Cursor = 0;
            Global_Decode_Bit_Edit_Active = 0;
            decode_clear_bit_selection();
            decode_set_status("Bitstream cleared.");
            return 1;
        }
        if (Global_Decode_Active_Field != DECODE_FIELD_NONE) {
            int f = Global_Decode_Active_Field;
            if (key == SDLK_BACKSPACE) {
                decode_backspace_text(Global_Decode_Field_Text[f], &Global_Decode_Field_Cursor[f]);
                if (f == DECODE_FIELD_ASCII_BYTE_LEN) decode_update_ascii_from_bitstream();
                return 1;
            }
            if (key == SDLK_DELETE) {
                decode_delete_text(Global_Decode_Field_Text[f], &Global_Decode_Field_Cursor[f]);
                if (f == DECODE_FIELD_ASCII_BYTE_LEN) decode_update_ascii_from_bitstream();
                return 1;
            }
            if (key == SDLK_LEFT) {
                Global_Decode_Field_Cursor[f]--;
                decode_clamp_cursor(Global_Decode_Field_Text[f], &Global_Decode_Field_Cursor[f]);
                return 1;
            }
            if (key == SDLK_RIGHT) {
                Global_Decode_Field_Cursor[f]++;
                decode_clamp_cursor(Global_Decode_Field_Text[f], &Global_Decode_Field_Cursor[f]);
                return 1;
            }
            if (key == SDLK_HOME) {
                Global_Decode_Field_Cursor[f] = 0;
                return 1;
            }
            if (key == SDLK_END) {
                Global_Decode_Field_Cursor[f] = (int)strlen(Global_Decode_Field_Text[f]);
                return 1;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                Global_Decode_Active_Field = DECODE_FIELD_NONE;
                return 1;
            }
        }
        return 0;
    }

    if (event->type == SDL_MOUSEWHEEL) {
        int mx = 0, my = 0;
        decode_get_adjusted_mouse_state(&mx, &my);
        if (decode_point_in_rect(mx, my, file_list)) {
            int max_scroll = Global_Decode_File_Count - (file_list.h / DECODE_ROW_H);
            Global_Decode_File_Scroll -= event->wheel.y;
            if (Global_Decode_File_Scroll < 0) Global_Decode_File_Scroll = 0;
            if (Global_Decode_File_Scroll > max_scroll) Global_Decode_File_Scroll = max_scroll > 0 ? max_scroll : 0;
            return 1;
        }
        if (decode_point_in_rect(mx, my, bits_rect) || decode_point_in_rect(mx, my, output)) {
            Global_Decode_Bit_Scroll -= event->wheel.y;
            if (Global_Decode_Bit_Scroll < 0) Global_Decode_Bit_Scroll = 0;
            return 1;
        }
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x = event->button.x;
        int y = event->button.y;

        Global_Decode_Active_Field = DECODE_FIELD_NONE;
        if (!decode_point_in_rect(x, y, bits_rect)) {
            Global_Decode_Bit_Edit_Active = 0;
        }

        if (decode_point_in_rect(x, y, decoder_tab)) {
            Global_Decode_Left_Tab = DECODE_LEFT_TAB_DECODER;
            Global_Decode_Mod_Dropdown_Open = 0;
            return 1;
        }
        if (decode_point_in_rect(x, y, preamble_tab)) {
            Global_Decode_Left_Tab = DECODE_LEFT_TAB_PREAMBLE;
            Global_Decode_Mod_Dropdown_Open = 0;
            return 1;
        }

        if (decode_point_in_rect(x, y, bits_rect)) {
            int idx = decode_bit_index_from_point(x, y, NULL, bits_rect);
            Global_Decode_Bit_Edit_Active = 1;
            SDL_StartTextInput();
            if (idx >= 0) {
                Global_Decode_Bit_Cursor = idx;
                decode_clamp_bit_cursor();
                Global_Decode_Bit_Selecting = 1;
                Global_Decode_Bit_Selection_Start = idx;
                Global_Decode_Bit_Selection_End = idx;
                decode_set_status("Bitstream edit mode: type 0/1, Backspace/Delete remove, arrows move.");
                return 1;
            }
            Global_Decode_Bit_Cursor = Global_Decode_Bitstream_Len;
            decode_clear_bit_selection();
            return 1;
        }

        if (decode_point_in_rect(x, y, search_button)) {
            decode_open_file_search_menu();
            return 1;
        }
        if (decode_point_in_rect(x, y, rescan_button)) {
            decode_scan_files();
            return 1;
        }
        if (decode_point_in_rect(x, y, copy_button)) {
            decode_copy_bitstream_to_clipboard();
            return 1;
        }

        if (Global_Decode_Left_Tab == DECODE_LEFT_TAB_PREAMBLE) {
            if (decode_point_in_rect(x, y, ps_search_button)) {
                decode_start_preamble_search_thread(1);
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_next_button)) {
                decode_start_preamble_search_thread(0);
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_export_button)) {
                decode_export_preamble_candidate_to_decoder();
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_sps_start_rect)) {
                Global_Decode_Active_Field = DECODE_FIELD_PREAMBLE_SPS_START;
                Global_Decode_Field_Cursor[DECODE_FIELD_PREAMBLE_SPS_START] =
                    (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_SPS_START]);
                SDL_StartTextInput();
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_sps_end_rect)) {
                Global_Decode_Active_Field = DECODE_FIELD_PREAMBLE_SPS_END;
                Global_Decode_Field_Cursor[DECODE_FIELD_PREAMBLE_SPS_END] =
                    (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_SPS_END]);
                SDL_StartTextInput();
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_start_bit_rect)) {
                Global_Decode_Active_Field = DECODE_FIELD_PREAMBLE_START_BIT;
                Global_Decode_Field_Cursor[DECODE_FIELD_PREAMBLE_START_BIT] =
                    (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_START_BIT]);
                SDL_StartTextInput();
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_search_bits_rect)) {
                Global_Decode_Active_Field = DECODE_FIELD_PREAMBLE_SEARCH_BITS;
                Global_Decode_Field_Cursor[DECODE_FIELD_PREAMBLE_SEARCH_BITS] =
                    (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_SEARCH_BITS]);
                SDL_StartTextInput();
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_min_prefix_rect)) {
                Global_Decode_Active_Field = DECODE_FIELD_PREAMBLE_MIN_PREFIX_LEN;
                Global_Decode_Field_Cursor[DECODE_FIELD_PREAMBLE_MIN_PREFIX_LEN] =
                    (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_MIN_PREFIX_LEN]);
                SDL_StartTextInput();
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_max_prefix_rect)) {
                Global_Decode_Active_Field = DECODE_FIELD_PREAMBLE_MAX_PREFIX_LEN;
                Global_Decode_Field_Cursor[DECODE_FIELD_PREAMBLE_MAX_PREFIX_LEN] =
                    (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_MAX_PREFIX_LEN]);
                SDL_StartTextInput();
                return 1;
            }
            if (decode_point_in_rect(x, y, ps_min_repeats_rect)) {
                Global_Decode_Active_Field = DECODE_FIELD_PREAMBLE_MIN_REPEATS;
                Global_Decode_Field_Cursor[DECODE_FIELD_PREAMBLE_MIN_REPEATS] =
                    (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_MIN_REPEATS]);
                SDL_StartTextInput();
                return 1;
            }
            if (decode_point_in_rect(x, y, controls)) {
                return 1;
            }
        }

        if (decode_point_in_rect(x, y, decode_button)) {
            decode_run_selected_file();
            return 1;
        }
        if (decode_point_in_rect(x, y, clear_button)) {
            Global_Decode_Bitstream_Len = 0;
            Global_Decode_Bitstream[0] = '\0';
            Global_Decode_Ascii_Len = 0;
            Global_Decode_Ascii_Text[0] = '\0';
            Global_Decode_Bit_Scroll = 0;
            Global_Decode_Bit_Cursor = 0;
            Global_Decode_Bit_Edit_Active = 0;
            decode_clear_bit_selection();
            decode_set_status("Bitstream cleared.");
            return 1;
        }
        if (Global_Decode_Mod_Dropdown_Open) {
            SDL_Rect dd = {mod_rect.x, mod_rect.y + mod_rect.h + 4, mod_rect.w, DECODE_MOD_COUNT * 38};
            if (decode_point_in_rect(x, y, dd)) {
                int idx = (y - dd.y) / 38;
                if (idx >= 0 && idx < DECODE_MOD_COUNT) {
                    Global_Decode_Modulation = idx;
                    decode_update_bits_per_symbol_from_modulation();
                    Global_Decode_Mod_Dropdown_Open = 0;
                    decode_set_status("GNU Radio demodulator changed. Tune samples/symbol before decoding.");
                }
                return 1;
            }
            if (!decode_point_in_rect(x, y, mod_rect)) {
                Global_Decode_Mod_Dropdown_Open = 0;
            }
        }
        if (decode_point_in_rect(x, y, mod_rect)) {
            Global_Decode_Mod_Dropdown_Open = !Global_Decode_Mod_Dropdown_Open;
            return 1;
        }
        if (decode_point_in_rect(x, y, sps_rect)) {
            Global_Decode_Active_Field = DECODE_FIELD_SAMPLES_PER_SYMBOL;
            Global_Decode_Field_Cursor[DECODE_FIELD_SAMPLES_PER_SYMBOL] =
                (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_SAMPLES_PER_SYMBOL]);
            SDL_StartTextInput();
            return 1;
        }
        if (decode_point_in_rect(x, y, bps_rect)) {
            Global_Decode_Active_Field = DECODE_FIELD_BITS_PER_SYMBOL;
            Global_Decode_Field_Cursor[DECODE_FIELD_BITS_PER_SYMBOL] =
                (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_BITS_PER_SYMBOL]);
            SDL_StartTextInput();
            return 1;
        }
        if (decode_point_in_rect(x, y, start_rect)) {
            Global_Decode_Active_Field = DECODE_FIELD_START_SAMPLE;
            Global_Decode_Field_Cursor[DECODE_FIELD_START_SAMPLE] =
                (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_START_SAMPLE]);
            SDL_StartTextInput();
            return 1;
        }
        if (decode_point_in_rect(x, y, max_rect)) {
            Global_Decode_Active_Field = DECODE_FIELD_MAX_SYMBOLS;
            Global_Decode_Field_Cursor[DECODE_FIELD_MAX_SYMBOLS] =
                (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_MAX_SYMBOLS]);
            SDL_StartTextInput();
            return 1;
        }
        if (decode_point_in_rect(x, y, ascii_byte_rect)) {
            Global_Decode_Active_Field = DECODE_FIELD_ASCII_BYTE_LEN;
            Global_Decode_Field_Cursor[DECODE_FIELD_ASCII_BYTE_LEN] =
                (int)strlen(Global_Decode_Field_Text[DECODE_FIELD_ASCII_BYTE_LEN]);
            SDL_StartTextInput();
            return 1;
        }
        if (decode_point_in_rect(x, y, normalize_box)) {
            Global_Decode_Normalize = !Global_Decode_Normalize;
            return 1;
        }
        if (decode_point_in_rect(x, y, invert_box)) {
            Global_Decode_Invert_Bits = !Global_Decode_Invert_Bits;
            return 1;
        }
        if (decode_point_in_rect(x, y, tight_box)) {
            Global_Decode_Skip_Whitespace = !Global_Decode_Skip_Whitespace;
            return 1;
        }
        if (decode_point_in_rect(x, y, ascii_box)) {
            Global_Decode_Ascii_Enable = !Global_Decode_Ascii_Enable;
            decode_update_ascii_from_bitstream();
            return 1;
        }
        if (decode_point_in_rect(x, y, file_list)) {
            int row = (y - file_list.y) / DECODE_ROW_H;
            int idx = Global_Decode_File_Scroll + row;
            if (idx >= 0 && idx < Global_Decode_File_Count) {
                Global_Decode_Selected_File = idx;
                decode_set_status("Selected decode input file.");
            }
            return 1;
        }
    }

    return 0;
}

void DECODE_draw_workstation(SDL_Renderer *renderer,
                             TTF_Font *font,
                             int win_w,
                             int win_h)
{
    SDL_Rect file_panel;
    SDL_Rect file_list;
    SDL_Rect controls;
    SDL_Rect output;
    SDL_Rect search_button;
    SDL_Rect rescan_button;
    SDL_Rect decode_button;
    SDL_Rect clear_button;
    SDL_Rect copy_button;
    SDL_Rect mod_rect;
    SDL_Rect sps_rect;
    SDL_Rect bps_rect;
    SDL_Rect start_rect;
    SDL_Rect max_rect;
    SDL_Rect normalize_box;
    SDL_Rect invert_box;
    SDL_Rect tight_box;
    SDL_Rect ascii_box;
    SDL_Rect ascii_byte_rect;
    SDL_Rect bits_rect;
    SDL_Rect ascii_rect;
    SDL_Rect decoder_tab;
    SDL_Rect preamble_tab;
    SDL_Rect ps_sps_start_rect;
    SDL_Rect ps_sps_end_rect;
    SDL_Rect ps_start_bit_rect;
    SDL_Rect ps_search_bits_rect;
    SDL_Rect ps_min_prefix_rect;
    SDL_Rect ps_max_prefix_rect;
    SDL_Rect ps_min_repeats_rect;
    SDL_Rect ps_search_button;
    SDL_Rect ps_next_button;
    SDL_Rect ps_export_button;
    int row_count;
    int mx = 0;
    int my = 0;

    if (!renderer || !font) return;

    decode_get_layout(win_w, win_h, &file_panel, &file_list, &controls, &output);
    search_button = decode_file_search_button_rect(win_w, win_h);
    rescan_button = (SDL_Rect){search_button.x - 96, search_button.y, 86, 28};
    if (rescan_button.x < file_panel.x + 160) {
        rescan_button.x = file_panel.x + 160;
    }

    decode_button = (SDL_Rect){controls.x + 18, controls.y + controls.h - 88, 106, 34};
    clear_button = (SDL_Rect){controls.x + 136, controls.y + controls.h - 88, 86, 34};
    copy_button = (SDL_Rect){output.x + output.w - 110, output.y + 12, 92, 30};
    mod_rect = (SDL_Rect){controls.x + 18, controls.y + 64, controls.w - 36, 38};
    sps_rect = (SDL_Rect){controls.x + 18, controls.y + 136, 150, 34};
    bps_rect = (SDL_Rect){controls.x + 190, controls.y + 136, controls.w - 208, 34};
    start_rect = (SDL_Rect){controls.x + 18, controls.y + 206, 150, 34};
    max_rect = (SDL_Rect){controls.x + 190, controls.y + 206, controls.w - 208, 34};
    normalize_box = (SDL_Rect){controls.x + 20, controls.y + 258, 18, 18};
    invert_box = (SDL_Rect){controls.x + 20, controls.y + 286, 18, 18};
    tight_box = (SDL_Rect){controls.x + 170, controls.y + 258, 18, 18};
    ascii_box = (SDL_Rect){controls.x + 170, controls.y + 286, 18, 18};
    ascii_byte_rect = (SDL_Rect){controls.x + 18, controls.y + 350, 150, 34};
    ascii_rect = (SDL_Rect){output.x + 8, output.y + output.h - 68, output.w - 16, 58};
    bits_rect = (SDL_Rect){output.x + 8, output.y + 72, output.w - 16, output.h - 150};
    if (bits_rect.h < 60) bits_rect.h = output.h - 84;
    {
        int tab_gap = 8;
        int tab_x = controls.x + 14;
        int tab_w = (controls.w - 28 - tab_gap) / 2;
        decoder_tab = (SDL_Rect){tab_x, controls.y + 12, tab_w, 30};
        preamble_tab = (SDL_Rect){tab_x + tab_w + tab_gap, controls.y + 12, tab_w, 30};
    }
    ps_sps_start_rect = (SDL_Rect){controls.x + 18, controls.y + 86, 150, 34};
    ps_sps_end_rect = (SDL_Rect){controls.x + 190, controls.y + 86, controls.w - 208, 34};
    ps_start_bit_rect = (SDL_Rect){controls.x + 18, controls.y + 156, 150, 34};
    ps_search_bits_rect = (SDL_Rect){controls.x + 190, controls.y + 156, controls.w - 208, 34};
    ps_min_prefix_rect = (SDL_Rect){controls.x + 18, controls.y + 226, 150, 34};
    ps_max_prefix_rect = (SDL_Rect){controls.x + 190, controls.y + 226, controls.w - 208, 34};
    ps_min_repeats_rect = (SDL_Rect){controls.x + 18, controls.y + 296, 150, 34};
    ps_search_button = (SDL_Rect){controls.x + 18, controls.y + 356, 76, 34};
    ps_next_button = (SDL_Rect){controls.x + 106, controls.y + 356, 64, 34};
    ps_export_button = (SDL_Rect){controls.x + 182, controls.y + 356, controls.w - 200, 34};

    decode_get_adjusted_mouse_state(&mx, &my);

    SDL_SetRenderDrawColor(renderer, Decode_BG.r, Decode_BG.g, Decode_BG.b, Decode_BG.a);
    SDL_RenderClear(renderer);

    draw_filled_rect(renderer, file_panel, Decode_Panel);
    draw_outline_rect(renderer, file_panel, Decode_Border);
    draw_text(renderer, font, "DECODE INPUT", file_panel.x + 12, file_panel.y + 16, Decode_Text);
    decode_draw_modal_button(renderer, font, search_button, "Open Search Menu", decode_point_in_rect(mx, my, search_button));
    decode_draw_modal_button(renderer, font, rescan_button, "Rescan", decode_point_in_rect(mx, my, rescan_button));

    {
        char selected_line[768];
        const char *selected = "none";
        char short_selected[512];
        if (Global_Decode_Selected_File >= 0 && Global_Decode_Selected_File < Global_Decode_File_Count) {
            selected = Global_Decode_Files[Global_Decode_Selected_File];
        }
        decode_short_text(font, selected, short_selected, sizeof(short_selected), file_panel.w - 520);
        snprintf(selected_line, sizeof(selected_line), "Selected: %s", short_selected);
        draw_text(renderer, font, selected_line, file_panel.x + 12, file_panel.y + 42, Decode_Muted);
    }

    draw_filled_rect(renderer, file_list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, file_list, Decode_Border);
    row_count = file_list.h / DECODE_ROW_H;
    if (row_count < 1) row_count = 1;

    if (Global_Decode_File_Count == 0) {
        draw_text(renderer, font, "No .complex16 files found.", file_list.x + 8, file_list.y + 12, Decode_Warn);
    }
    else {
        for (int r = 0; r < row_count; r++) {
            int idx = Global_Decode_File_Scroll + r;
            SDL_Rect row = {file_list.x + 4, file_list.y + 4 + r * DECODE_ROW_H, file_list.w - 8, DECODE_ROW_H - 3};
            char short_name[512];
            if (idx >= Global_Decode_File_Count) break;

            if (idx == Global_Decode_Selected_File) {
                draw_filled_rect(renderer, row, (SDL_Color){0, 44, 16, 255});
                draw_outline_rect(renderer, row, Decode_Border_Hi);
            }
            else if (decode_point_in_rect(mx, my, row)) {
                draw_filled_rect(renderer, row, (SDL_Color){0, 26, 10, 255});
                draw_outline_rect(renderer, row, Decode_Border);
            }

            decode_short_text(font,
                              Global_Decode_Files[idx],
                              short_name,
                              sizeof(short_name),
                              row.w - 16);
            draw_text(renderer,
                      font,
                      short_name,
                      row.x + 8,
                      row.y + 7,
                      idx == Global_Decode_Selected_File ? Decode_Text : Decode_Muted);
        }
    }

    draw_filled_rect(renderer, controls, Decode_Panel);
    draw_outline_rect(renderer, controls, Decode_Border);

    draw_filled_rect(renderer,
                     decoder_tab,
                     Global_Decode_Left_Tab == DECODE_LEFT_TAB_DECODER ? (SDL_Color){0, 44, 16, 255} : (SDL_Color){0, 8, 3, 255});
    draw_outline_rect(renderer,
                      decoder_tab,
                      Global_Decode_Left_Tab == DECODE_LEFT_TAB_DECODER ? Decode_Border_Hi : Decode_Border);
    decode_draw_centered_text(renderer,
                              font,
                              "Decoder",
                              decoder_tab,
                              Global_Decode_Left_Tab == DECODE_LEFT_TAB_DECODER ? Decode_Text : Decode_Muted);

    draw_filled_rect(renderer,
                     preamble_tab,
                     Global_Decode_Left_Tab == DECODE_LEFT_TAB_PREAMBLE ? (SDL_Color){0, 44, 16, 255} : (SDL_Color){0, 8, 3, 255});
    draw_outline_rect(renderer,
                      preamble_tab,
                      Global_Decode_Left_Tab == DECODE_LEFT_TAB_PREAMBLE ? Decode_Border_Hi : Decode_Border);
    decode_draw_centered_text(renderer,
                              font,
                              "Preamble Search",
                              preamble_tab,
                              Global_Decode_Left_Tab == DECODE_LEFT_TAB_PREAMBLE ? Decode_Text : Decode_Muted);

    if (Global_Decode_Left_Tab == DECODE_LEFT_TAB_DECODER) {
        draw_text(renderer, font, "Modulation", mod_rect.x, mod_rect.y - 18, Decode_Muted);
        draw_filled_rect(renderer, mod_rect, Decode_Panel_2);
        draw_outline_rect(renderer, mod_rect, Global_Decode_Mod_Dropdown_Open ? Decode_Border_Hi : Decode_Border);
        draw_text(renderer, font, DECODE_MOD_LABELS[Global_Decode_Modulation], mod_rect.x + 10, mod_rect.y + 10, Decode_Text);
        draw_text(renderer, font, "v", mod_rect.x + mod_rect.w - 22, mod_rect.y + 10, Decode_Muted);

        decode_draw_input_field(renderer, font, "Samples/Symbol", Global_Decode_Field_Text[DECODE_FIELD_SAMPLES_PER_SYMBOL], DECODE_FIELD_SAMPLES_PER_SYMBOL, sps_rect);
        decode_draw_input_field(renderer, font, "Bits/Symbol", Global_Decode_Field_Text[DECODE_FIELD_BITS_PER_SYMBOL], DECODE_FIELD_BITS_PER_SYMBOL, bps_rect);
        decode_draw_input_field(renderer, font, "Start Sample", Global_Decode_Field_Text[DECODE_FIELD_START_SAMPLE], DECODE_FIELD_START_SAMPLE, start_rect);
        decode_draw_input_field(renderer, font, "Max Symbols", Global_Decode_Field_Text[DECODE_FIELD_MAX_SYMBOLS], DECODE_FIELD_MAX_SYMBOLS, max_rect);

        decode_draw_checkbox(renderer, font, normalize_box, "Normalize", Global_Decode_Normalize);
        decode_draw_checkbox(renderer, font, invert_box, "Invert bits", Global_Decode_Invert_Bits);
        decode_draw_checkbox(renderer, font, tight_box, "Tight stream", Global_Decode_Skip_Whitespace);
        decode_draw_checkbox(renderer, font, ascii_box, "ASCII", Global_Decode_Ascii_Enable);
        decode_draw_input_field(renderer, font, "ASCII Byte Len", Global_Decode_Field_Text[DECODE_FIELD_ASCII_BYTE_LEN], DECODE_FIELD_ASCII_BYTE_LEN, ascii_byte_rect);

        decode_draw_modal_button(renderer, font, decode_button, "Decode", decode_point_in_rect(mx, my, decode_button));
        decode_draw_modal_button(renderer, font, clear_button, "Clear", decode_point_in_rect(mx, my, clear_button));


    }
    else {
        SDL_Rect candidate_box = {controls.x + 18, controls.y + 426, controls.w - 36, controls.h - 504};
        if (candidate_box.h < 120) candidate_box.h = 120;

            decode_draw_input_field(renderer, font, "SPS Start", Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_SPS_START], DECODE_FIELD_PREAMBLE_SPS_START, ps_sps_start_rect);
        decode_draw_input_field(renderer, font, "SPS End", Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_SPS_END], DECODE_FIELD_PREAMBLE_SPS_END, ps_sps_end_rect);
        decode_draw_input_field(renderer, font, "Start Bit", Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_START_BIT], DECODE_FIELD_PREAMBLE_START_BIT, ps_start_bit_rect);
        decode_draw_input_field(renderer, font, "Search First Bits", Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_SEARCH_BITS], DECODE_FIELD_PREAMBLE_SEARCH_BITS, ps_search_bits_rect);
        decode_draw_input_field(renderer, font, "Min Prefix Len", Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_MIN_PREFIX_LEN], DECODE_FIELD_PREAMBLE_MIN_PREFIX_LEN, ps_min_prefix_rect);
        decode_draw_input_field(renderer, font, "Max Prefix Len", Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_MAX_PREFIX_LEN], DECODE_FIELD_PREAMBLE_MAX_PREFIX_LEN, ps_max_prefix_rect);
        decode_draw_input_field(renderer, font, "Min Repeats", Global_Decode_Field_Text[DECODE_FIELD_PREAMBLE_MIN_REPEATS], DECODE_FIELD_PREAMBLE_MIN_REPEATS, ps_min_repeats_rect);

        decode_draw_modal_button(renderer, font, ps_search_button, "Search", decode_point_in_rect(mx, my, ps_search_button));
        decode_draw_modal_button(renderer, font, ps_next_button, "Next", decode_point_in_rect(mx, my, ps_next_button));
        decode_draw_modal_button(renderer, font, ps_export_button, "Export to Decoder", decode_point_in_rect(mx, my, ps_export_button));

        draw_text(renderer, font, "Repeated-prefix candidates", candidate_box.x, candidate_box.y - 24, Decode_Muted);
        draw_filled_rect(renderer, candidate_box, (SDL_Color){0, 5, 2, 255});
        draw_outline_rect(renderer, candidate_box, Decode_Border);

        if (Global_Decode_Preamble_Has_Candidate) {
            char line[512];
            char short_bits[256];
            snprintf(line,
                     sizeof(line),
                     "Candidate: SPS %d | start bit %d | len %d | repeats %d",
                     Global_Decode_Preamble_Candidate_SPS,
                     Global_Decode_Preamble_Candidate_Start_Bit,
                     Global_Decode_Preamble_Candidate_Length,
                     Global_Decode_Preamble_Candidate_Repeats);
            draw_text(renderer, font, line, candidate_box.x + 8, candidate_box.y + 10, Decode_Text);
            decode_short_text(font, Global_Decode_Preamble_Candidate_Bits, short_bits, sizeof(short_bits), candidate_box.w - 16);
            draw_text(renderer, font, short_bits, candidate_box.x + 8, candidate_box.y + 36, Decode_Muted);
            draw_text(renderer, font, "Export fills SPS. Next continues.", candidate_box.x + 8, candidate_box.y + 64, Decode_Warn);
        }
        else {
            if (Global_Decode_Preamble_Searching) {
                draw_text(renderer, font, "Searching repeated-prefix candidates...", candidate_box.x + 8, candidate_box.y + 10, Decode_Warn);
            }
            else {
                draw_text(renderer, font, "No preamble candidate yet.", candidate_box.x + 8, candidate_box.y + 10, Decode_Warn);
                draw_text(renderer, font, "Needs mixed 0/1 repeats.", candidate_box.x + 8, candidate_box.y + 36, Decode_Muted);
            }
        }

        if (Global_Decode_Preamble_Searching || Global_Decode_Preamble_Progress > 0) {
            char progress_text[96];
            int pct = Global_Decode_Preamble_Progress;
            SDL_Rect progress_rect;
            SDL_Rect progress_bar;
            SDL_Rect progress_fill;
            TTF_Font *progress_font = decode_get_progress_font(font);

            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;

            snprintf(progress_text,
                     sizeof(progress_text),
                     "Preamble Search %d%%",
                     pct);

            progress_rect = (SDL_Rect){
                candidate_box.x + 8,
                candidate_box.y + 82,
                candidate_box.w - 16,
                34
            };
            if (progress_rect.y + progress_rect.h > candidate_box.y + candidate_box.h - 26) {
                progress_rect.y = candidate_box.y + (candidate_box.h - progress_rect.h) / 2;
            }

            decode_draw_centered_text(renderer,
                                      progress_font,
                                      progress_text,
                                      progress_rect,
                                      Decode_Warn);

            progress_bar = (SDL_Rect){
                candidate_box.x + 18,
                candidate_box.y + candidate_box.h - 22,
                candidate_box.w - 36,
                12
            };
            progress_fill = progress_bar;
            progress_fill.w = (progress_bar.w * pct) / 100;

            draw_filled_rect(renderer, progress_bar, (SDL_Color){0, 8, 3, 255});
            draw_outline_rect(renderer, progress_bar, Decode_Border);
            if (progress_fill.w > 0) {
                draw_filled_rect(renderer, progress_fill, Decode_Warn);
            }
        }

    }

    draw_filled_rect(renderer, output, Decode_Panel);
    draw_outline_rect(renderer, output, Decode_Border);
    draw_text(renderer, font, Global_Decode_Bit_Edit_Active ? "BITS [EDIT]" : "BITS", output.x + 12, output.y + 16, Decode_Text);

    {
        char short_status[256];
        int status_x = output.x + 110;
        int status_max_w = copy_button.x - status_x - 14;
        if (status_max_w < 80) status_max_w = 80;
        decode_short_text(font, Global_Decode_Status, short_status, sizeof(short_status), status_max_w);
        draw_text(renderer, font, short_status, status_x, output.y + 16, Decode_Warn);
    }
    decode_draw_modal_button(renderer, font, copy_button, "Copy", decode_point_in_rect(mx, my, copy_button));

    {
        char summary[512];
        char short_file[256];
        const char *file = "none";
        if (Global_Decode_Selected_File >= 0 && Global_Decode_Selected_File < Global_Decode_File_Count) {
            file = Global_Decode_Files[Global_Decode_Selected_File];
        }
        decode_short_text(font, file, short_file, sizeof(short_file), output.w - 310);
        snprintf(summary,
                 sizeof(summary),
                 "%d chars | %s | %s",
                 Global_Decode_Bitstream_Len,
                 DECODE_MOD_LABELS[Global_Decode_Modulation],
                 short_file);
        draw_text(renderer, font, summary, output.x + 12, output.y + 44, Decode_Muted);
    }

    decode_draw_wrapped_bits(renderer, font, bits_rect);
    decode_draw_ascii_panel(renderer, font, ascii_rect);

    if (Global_Decode_Mod_Dropdown_Open) {
        SDL_Rect dd = {mod_rect.x, mod_rect.y + mod_rect.h + 4, mod_rect.w, DECODE_MOD_COUNT * 38};
        draw_filled_rect(renderer, dd, (SDL_Color){0, 5, 2, 255});
        draw_outline_rect(renderer, dd, Decode_Border_Hi);
        for (int i = 0; i < DECODE_MOD_COUNT; i++) {
            SDL_Rect opt = {dd.x + 3, dd.y + 3 + i * 38, dd.w - 6, 35};
            int hovered = decode_point_in_rect(mx, my, opt);
            if (i == Global_Decode_Modulation) {
                draw_filled_rect(renderer, opt, (SDL_Color){0, 55, 20, 255});
            }
            else if (hovered) {
                draw_filled_rect(renderer, opt, (SDL_Color){0, 34, 14, 255});
            }
            draw_outline_rect(renderer, opt, hovered || i == Global_Decode_Modulation ? Decode_Border_Hi : Decode_Border);
            draw_text(renderer,
                      font,
                      DECODE_MOD_LABELS[i],
                      opt.x + 10,
                      opt.y + 9,
                      hovered || i == Global_Decode_Modulation ? Decode_Text : Decode_Muted);
        }
    }

    decode_draw_file_search_popup(renderer, font, win_w, win_h);
}
