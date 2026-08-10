#define _POSIX_C_SOURCE 200809L

/*
 * ============================================================================
 * File:            AnalysisWorkstation.c
 * Author:          Hassan Fares
 *
 * Description:     Signal analysis workstation logic for RetroSpectrum
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux
 *
 *                                                               05/04/2026
 * ============================================================================
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <fftw3.h>

#include "AnalysisWorkstation.h"
#include "GUIs.h"
#include "SecureFunctions.h"

/* Kept here so this source also builds when older headers omit the new API. */
const char *AUTH_get_current_username(void);
int AUTH_verify_current_password(const char *password, char *error, size_t error_size);
int RETROSPECTRUM_start_file_transmission(const char *path, uint64_t center_frequency_hz, uint32_t sample_rate_hz,
                                          uint32_t bandwidth_hz, int tx_gain_db, unsigned int repeat_count, char *error,
                                          size_t error_size);
void RETROSPECTRUM_cancel_file_transmission(void);
int RETROSPECTRUM_get_transmission_status(double *progress, int *active, int *result_ready, int *succeeded,
                                          char *message, size_t message_size);
void RETROSPECTRUM_acknowledge_transmission_result(void);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MARGIN 20
#define ANALYSIS_MAX_FILES 512
#define ANALYSIS_FFT_SIZE 2048
#define ANALYSIS_MAX_CONST_POINTS 4096
#define ANALYSIS_WORKSPACE_COUNT 5
#define ANALYSIS_SIGNAL_FIELD_COUNT 7
#define ANALYSIS_SIGNAL_TEXT_MAX 512
#define ANALYSIS_SIGNAL_FIELD_NONE -1
#define ANALYSIS_SIGNAL_DECIMATION_FIELD 5
#define ANALYSIS_SIGNAL_FILENAME_FIELD 6
#define ANALYSIS_FILE_SEARCH_TEXT_MAX 256
#define ANALYSIS_FILE_SEARCH_ROW_H 34
#define ANALYSIS_MULTITHREAD_COUNT 10
#define ANALYSIS_TRANSMIT_PASSWORD_MAX 127
#define ANALYSIS_TRANSMIT_FIELD_COUNT 5
#define ANALYSIS_TRANSMIT_TEXT_MAX 32
#define ANALYSIS_TRANSMIT_FIELD_FREQUENCY 0
#define ANALYSIS_TRANSMIT_FIELD_SAMPLE_RATE 1
#define ANALYSIS_TRANSMIT_FIELD_BANDWIDTH 2
#define ANALYSIS_TRANSMIT_FIELD_GAIN 3
#define ANALYSIS_TRANSMIT_FIELD_REPEAT 4

#define ANALYSIS_NOISE_GRAPH_NONE 0
#define ANALYSIS_NOISE_GRAPH_MAG 1
#define ANALYSIS_NOISE_GRAPH_INST 2

#define ANALYSIS_CONSTELLATION_MODE_OFF 0
#define ANALYSIS_CONSTELLATION_MODE_PSK 1
#define ANALYSIS_CONSTELLATION_MODE_QAM 2
#define ANALYSIS_CONSTELLATION_MODE_ASK_OOK 3
#define ANALYSIS_CONSTELLATION_MODE_FSK_MSK 4
#define ANALYSIS_CONSTELLATION_MODE_OFDM 5
#define ANALYSIS_CONSTELLATION_MODE_COUNT 6
#define ANALYSIS_CONSTELLATION_MAX_INPUT 131072U
#define ANALYSIS_CONSTELLATION_PSK_BPSK 2
#define ANALYSIS_CONSTELLATION_PSK_QPSK 4
#define ANALYSIS_CONSTELLATION_PSK_8PSK 8
#define ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT 3

#ifndef RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#endif

static void ANALYSIS_get_adjusted_mouse_state(int *x, int *y) {
    /*
        Purpose: Gets the adjusted mouse state
        Returns: No value
    */

    SDL_GetMouseState(x, y);

    if (y) {

        *y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

    }
}

static char Global_Analysis_Record_Dir[512] = "Recordings";
static uint64_t Global_Analysis_Fallback_Center_Hz = 0;
static uint32_t Global_Analysis_Fallback_Rec_Out_Rate_Hz = 0;
static uint32_t Global_Analysis_Fallback_Sample_Rate_Hz = 0;

static void ANALYSIS_secure_clear(void *memory, size_t size) {
    /*
        Purpose: Clears sensitive prompt data without allowing compiler removal
        Returns: No value
    */

    volatile unsigned char *bytes = (volatile unsigned char *)memory;

    if (!bytes) {

        return;

    }

    while (size > 0) {
        *bytes++ = 0;
        size--;
    }
}

static double ANALYSIS_limit_double(double value, double low, double high) {
    /*
        Purpose: Limits the double
        Returns: Computed value
    */

    if (value < low) {

        return low;

    }

    if (value > high) {

        return high;

    }
    return value;
}

static void ANALYSIS_set_context(const char *record_dir, uint64_t fallback_center_hz, uint32_t fallback_rec_out_rate_hz,
                                 uint32_t fallback_sample_rate_hz) {
    /*
        Purpose: Sets the context
        Returns: No value
    */

    if (record_dir && record_dir[0] != '\0') {

        snprintf(Global_Analysis_Record_Dir, sizeof(Global_Analysis_Record_Dir), "%s", record_dir);

    }

    Global_Analysis_Fallback_Center_Hz = fallback_center_hz;
    Global_Analysis_Fallback_Rec_Out_Rate_Hz = fallback_rec_out_rate_hz;
    Global_Analysis_Fallback_Sample_Rate_Hz = fallback_sample_rate_hz;
}

int Global_Analysis_Mode = 0;
int Global_Analysis_Dirty = 0;
int Global_Analysis_File_Count = 0;
int Global_Analysis_Selected = 0;
int Global_Analysis_List_Scroll = 0;
int Global_Analysis_Dragging = 0;
int Global_Analysis_Drag_Last_X = 0;
int Global_Analysis_Loading = 0;
int Global_Analysis_Load_Frame = 0;
int Global_Analysis_Loaded_Index = -1;
int Global_Analysis_Render_W = 0;
size_t Global_Analysis_IQ_Count = 0;
size_t Global_Analysis_View_Start = 0;
size_t Global_Analysis_View_Len = 0;
double Global_Analysis_Sample_Rate = 0.0;
double Global_Analysis_Center_Hz = 0.0;
char Global_Analysis_Path[1024] = "";
int Global_Analysis_Const_Count = 0;
int Global_Analysis_Constellation_Mode = ANALYSIS_CONSTELLATION_MODE_OFF;
int Global_Analysis_Constellation_PSK_Order = ANALYSIS_CONSTELLATION_PSK_BPSK;
int Global_Analysis_Filter_Visible = 0;
int Global_Analysis_Filter_Selecting = 0;
int Global_Analysis_Filter_Active = 0;
double Global_Analysis_Filter_Y0 = 0.40;
double Global_Analysis_Filter_Y1 = 0.60;
int Global_Analysis_Marker_Active = 0;
size_t Global_Analysis_Marker_Sample = 0;
double Global_Analysis_Marker_Time = 0.0;
int Global_Analysis_Column_Selecting = 0;
int Global_Analysis_Column_Visible = 0;
int Global_Analysis_Column_Active = 0;
double Global_Analysis_Column_X0 = 0.0;
double Global_Analysis_Column_X1 = 0.0;
char Global_Analysis_Status[256] = "Press R to scan recordings";
char Global_Analysis_Files[ANALYSIS_MAX_FILES][512];
float Global_Analysis_Mag_Line[ANALYSIS_MAX_RENDER_W];
float Global_Analysis_Phase_Line[ANALYSIS_MAX_RENDER_W];
float Global_Analysis_InstFreq_Line[ANALYSIS_MAX_RENDER_W];
float Global_Analysis_PSD_Line[ANALYSIS_MAX_RENDER_W];
float Global_Analysis_Const_I[ANALYSIS_MAX_CONST_POINTS];
float Global_Analysis_Const_Q[ANALYSIS_MAX_CONST_POINTS];

static const char *Global_Analysis_Constellation_Mode_Labels[ANALYSIS_CONSTELLATION_MODE_COUNT] = {
    "Off", "PSK", "QAM", "ASK/OOK", "FSK/MSK", "OFDM"};
static const char *Global_Analysis_Constellation_PSK_Labels[ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT] = {"BPSK", "QPSK",
                                                                                                        "8PSK"};
static const int Global_Analysis_Constellation_PSK_Orders[ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT] = {
    ANALYSIS_CONSTELLATION_PSK_BPSK, ANALYSIS_CONSTELLATION_PSK_QPSK, ANALYSIS_CONSTELLATION_PSK_8PSK};
static int Global_Analysis_Constellation_PSK_Prompt_Open = 0;

static int Global_Analysis_Signal_Menu_Open = 0;
static int Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
static int Global_Analysis_Signal_File_Manual_Edit = 0;
static int Global_Analysis_Signal_File_Cursor = 0;
static int Global_Analysis_Signal_File_Selecting = 0;
static int Global_Analysis_Signal_File_Selection_Start = -1;
static int Global_Analysis_Signal_File_Selection_End = -1;
static TTF_Font *Global_Analysis_Signal_Last_Font = NULL;
static char Global_Analysis_Signal_Menu_File[512] = "";
static char Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FIELD_COUNT][ANALYSIS_SIGNAL_TEXT_MAX] = {"", "", "", "",
                                                                                                        "", "", ""};
static SDL_Rect Global_Analysis_Signal_Icon_Rect = {0, 8, 34, 34};
static int Global_Analysis_Signal_Icon_Rect_Valid = 0;
static SDL_Rect Global_Analysis_Signal_Trash_Rect = {0, 8, 34, 34};
static int Global_Analysis_Signal_Trash_Rect_Valid = 0;
static SDL_Rect Global_Analysis_Multithread_Rect = {0, 8, 34, 34};
static int Global_Analysis_Multithread_Rect_Valid = 0;
static int Global_Analysis_Multithread_Enabled = 0;
static int Global_Analysis_Multithread_Prompt_Open = 0;
static SDL_Rect Global_Analysis_Transmit_Rect = {0, 8, 34, 34};
static int Global_Analysis_Transmit_Rect_Valid = 0;
static int Global_Analysis_Transmit_Auth_Prompt_Open = 0;
static int Global_Analysis_Transmit_Config_Prompt_Open = 0;
static int Global_Analysis_Transmit_Progress_Prompt_Open = 0;
static int Global_Analysis_Transmit_Result_Prompt_Open = 0;
static int Global_Analysis_Transmit_Result_Succeeded = 0;
static int Global_Analysis_Transmit_Config_Active_Field = 0;
static int Global_Analysis_Transmit_Password_Cursor = 0;
static int Global_Analysis_Transmit_Field_Cursor[ANALYSIS_TRANSMIT_FIELD_COUNT] = {0, 0, 0, 0, 0};
static char Global_Analysis_Transmit_Password[ANALYSIS_TRANSMIT_PASSWORD_MAX + 1] = "";
static char Global_Analysis_Transmit_Auth_Status[256] = "";
static char Global_Analysis_Transmit_Config_Status[256] = "";
static char Global_Analysis_Transmit_Field_Text[ANALYSIS_TRANSMIT_FIELD_COUNT][ANALYSIS_TRANSMIT_TEXT_MAX] = {
    "", "", "", "20", "0"};
static char Global_Analysis_Transmit_Result_Message[256] = "";
static double Global_Analysis_Transmit_Progress = 0.0;
static double Global_Analysis_Signal_Icon_Freq_Frac = 0.0;
static int Global_Analysis_Delete_Confirm_Open = 0;
static char Global_Analysis_Delete_Confirm_File[512] = "";
static char Global_Analysis_Delete_Confirm_Path[1024] = "";

static int Global_Analysis_File_Search_Open = 0;
static int Global_Analysis_File_Search_Active = 0;
static int Global_Analysis_File_Search_Cursor = 0;
static int Global_Analysis_File_Search_Scroll = 0;
static int Global_Analysis_File_Search_Hover = -1;
static char Global_Analysis_File_Search_Text[ANALYSIS_FILE_SEARCH_TEXT_MAX] = "";

static int Global_Analysis_Noise_Key_Down = 0;
static int Global_Analysis_Noise_Visible = 0;
static int Global_Analysis_Noise_Selecting = 0;
static int Global_Analysis_Noise_Active = 0;
static int Global_Analysis_Noise_Graph = ANALYSIS_NOISE_GRAPH_NONE;
static double Global_Analysis_Noise_Y0 = 0.0;
static double Global_Analysis_Noise_Y1 = 0.0;
static unsigned char Global_Analysis_Noise_Column_Mask[ANALYSIS_MAX_RENDER_W];

static SDL_Rect ANALYSIS_crop_button_rect(int win_w, int win_h);
static void ANALYSIS_draw_crop_button(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static SDL_Rect ANALYSIS_clear_workspace_button_rect(int win_w, int win_h);
static void ANALYSIS_draw_clear_workspace_button(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static void ANALYSIS_draw_noise_filter_overlay(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static void ANALYSIS_update_noise_column_mask(int render_w);
static void ANALYSIS_apply_noise_filter_to_rendered_lines(int render_w);
static int ANALYSIS_crop_current_selection(uint32_t *pixels, int tex_w, int tex_h, SDL_Texture *texture);
static void ANALYSIS_clear_noise_filter(void);
static void ANALYSIS_draw_centered_button_text(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *text,
                                               SDL_Color color);
static int ANALYSIS_get_constellation_mode_button_rects(int win_w, int win_h, SDL_Rect *rects);
static void ANALYSIS_draw_constellation_mode_buttons(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static int ANALYSIS_handle_constellation_mode_click(int x, int y, int win_w, int win_h);
static void ANALYSIS_draw_constellation_psk_prompt(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static int ANALYSIS_handle_constellation_psk_prompt_event(SDL_Event *event, int win_w, int win_h);
static void ANALYSIS_invalidate_constellation_cache(void);
static void ANALYSIS_get_signal_icon_rect(int win_w, int win_h, SDL_Rect *out);
static void ANALYSIS_submit_transmission_settings(int password_verified);
void ANALYSIS_render_workstation_data(uint32_t *pixels, int tex_w, int tex_h);

static const char *ANALYSIS_SIGNAL_FIELD_LABELS[ANALYSIS_SIGNAL_FIELD_COUNT] = {
    "Center Frequency MHz", "Bandwidth kHz", "Sample Rate kS/s", "Start Time sec",
    "End Time sec",         "Decimation",    "File Name"};

typedef struct Type_Analysis_Workspace_State {
    double column_x0;
    double column_x1;
    double sample_rate;
    double center_hz;
    double filter_y0;
    double filter_y1;
    double noise_y0;
    double noise_y1;
    double marker_time;
    size_t iq_count;
    size_t view_start;
    size_t view_len;
    size_t marker_sample;
    float mag_line[ANALYSIS_MAX_RENDER_W];
    float phase_line[ANALYSIS_MAX_RENDER_W];
    float inst_freq_line[ANALYSIS_MAX_RENDER_W];
    float psd_line[ANALYSIS_MAX_RENDER_W];
    float const_i[ANALYSIS_MAX_CONST_POINTS];
    float const_q[ANALYSIS_MAX_CONST_POINTS];
    int dirty;
    int file_count;
    int selected;
    int list_scroll;
    int dragging;
    int drag_last_x;
    int loading;
    int load_frame;
    int loaded_index;
    int render_w;
    int const_count;
    int constellation_mode;
    int constellation_psk_order;
    int filter_visible;
    int filter_selecting;
    int filter_active;
    int noise_visible;
    int noise_selecting;
    int noise_active;
    int noise_graph;
    int marker_active;
    int column_selecting;
    int column_visible;
    int column_active;
    int signal_menu_open;
    int signal_active_field;
    int signal_file_manual_edit;
    int signal_file_cursor;
    int signal_file_selecting;
    int signal_file_selection_start;
    int signal_file_selection_end;
    char signal_menu_file[512];
    char signal_field_text[ANALYSIS_SIGNAL_FIELD_COUNT][ANALYSIS_SIGNAL_TEXT_MAX];
    char status[256];
    char path[1024];
    char files[ANALYSIS_MAX_FILES][512];

} Type_Analysis_Workspace_State;

typedef struct Type_Analysis_Constellation_Cache {
    int ready;
    int mode;
    int psk_order;
    int filter_active;
    int column_active;
    int count;
    size_t iq_count;
    size_t view_start;
    size_t view_len;
    double sample_rate;
    double filter_y0;
    double filter_y1;
    double column_x0;
    double column_x1;
    char path[1024];
    float i[ANALYSIS_MAX_CONST_POINTS];
    float q[ANALYSIS_MAX_CONST_POINTS];
} Type_Analysis_Constellation_Cache;

static Type_Analysis_Workspace_State Global_Analysis_Workspaces[ANALYSIS_WORKSPACE_COUNT];
static Type_Analysis_Constellation_Cache Global_Analysis_Constellation_Caches[ANALYSIS_WORKSPACE_COUNT];
static int Global_Analysis_Active_Workspace = 0;
static int Global_Analysis_Workspaces_Initialized = 0;

static void ANALYSIS_invalidate_constellation_cache(void) {
    /*
        Purpose: Invalidates the cached constellation synchronization for the active workspace
        Returns: No value
    */

    if (Global_Analysis_Active_Workspace < 0 || Global_Analysis_Active_Workspace >= ANALYSIS_WORKSPACE_COUNT) {

        return;

    }

    Global_Analysis_Constellation_Caches[Global_Analysis_Active_Workspace].ready = 0;
}

static int ANALYSIS_constellation_cache_matches(void) {
    /*
        Purpose: Checks whether the active workspace constellation cache still matches its signal settings
        Returns: Boolean status
    */

    if (Global_Analysis_Active_Workspace < 0 || Global_Analysis_Active_Workspace >= ANALYSIS_WORKSPACE_COUNT) {

        return 0;

    }

    Type_Analysis_Constellation_Cache *cache = &Global_Analysis_Constellation_Caches[Global_Analysis_Active_Workspace];

    return cache->ready && cache->mode == Global_Analysis_Constellation_Mode &&
           cache->psk_order == Global_Analysis_Constellation_PSK_Order &&
           cache->filter_active == Global_Analysis_Filter_Active &&
           cache->column_active == Global_Analysis_Column_Active && cache->iq_count == Global_Analysis_IQ_Count &&
           cache->view_start == Global_Analysis_View_Start && cache->view_len == Global_Analysis_View_Len &&
           cache->sample_rate == Global_Analysis_Sample_Rate && cache->filter_y0 == Global_Analysis_Filter_Y0 &&
           cache->filter_y1 == Global_Analysis_Filter_Y1 && cache->column_x0 == Global_Analysis_Column_X0 &&
           cache->column_x1 == Global_Analysis_Column_X1 && strcmp(cache->path, Global_Analysis_Path) == 0;
}

static void ANALYSIS_restore_constellation_cache(void) {
    /*
        Purpose: Restores the cached constellation points for the active workspace
        Returns: No value
    */

    Type_Analysis_Constellation_Cache *cache = &Global_Analysis_Constellation_Caches[Global_Analysis_Active_Workspace];

    Global_Analysis_Const_Count = cache->count;
    memcpy(Global_Analysis_Const_I, cache->i, sizeof(Global_Analysis_Const_I));
    memcpy(Global_Analysis_Const_Q, cache->q, sizeof(Global_Analysis_Const_Q));
}

static void ANALYSIS_store_constellation_cache(void) {
    /*
        Purpose: Stores the calculated constellation for the current visible greyscale view
        Returns: No value
    */

    if (Global_Analysis_Active_Workspace < 0 || Global_Analysis_Active_Workspace >= ANALYSIS_WORKSPACE_COUNT) {

        return;

    }

    Type_Analysis_Constellation_Cache *cache = &Global_Analysis_Constellation_Caches[Global_Analysis_Active_Workspace];

    cache->ready = 1;
    cache->mode = Global_Analysis_Constellation_Mode;
    cache->psk_order = Global_Analysis_Constellation_PSK_Order;
    cache->filter_active = Global_Analysis_Filter_Active;
    cache->column_active = Global_Analysis_Column_Active;
    cache->count = Global_Analysis_Const_Count;
    cache->iq_count = Global_Analysis_IQ_Count;
    cache->view_start = Global_Analysis_View_Start;
    cache->view_len = Global_Analysis_View_Len;
    cache->sample_rate = Global_Analysis_Sample_Rate;
    cache->filter_y0 = Global_Analysis_Filter_Y0;
    cache->filter_y1 = Global_Analysis_Filter_Y1;
    cache->column_x0 = Global_Analysis_Column_X0;
    cache->column_x1 = Global_Analysis_Column_X1;
    snprintf(cache->path, sizeof(cache->path), "%s", Global_Analysis_Path);
    memcpy(cache->i, Global_Analysis_Const_I, sizeof(cache->i));
    memcpy(cache->q, Global_Analysis_Const_Q, sizeof(cache->q));
}

static void ANALYSIS_save_workspace_state(int index) {
    /*
        Purpose: Saves the workspace state
        Returns: No value
    */

    if (index < 0 || index >= ANALYSIS_WORKSPACE_COUNT) {

        return;

    }

    Type_Analysis_Workspace_State *ws = &Global_Analysis_Workspaces[index];

    ws->dirty = Global_Analysis_Dirty;
    ws->file_count = Global_Analysis_File_Count;
    ws->selected = Global_Analysis_Selected;
    ws->list_scroll = Global_Analysis_List_Scroll;
    ws->dragging = Global_Analysis_Dragging;
    ws->drag_last_x = Global_Analysis_Drag_Last_X;
    ws->loading = Global_Analysis_Loading;
    ws->load_frame = Global_Analysis_Load_Frame;
    ws->loaded_index = Global_Analysis_Loaded_Index;
    ws->render_w = Global_Analysis_Render_W;
    ws->iq_count = Global_Analysis_IQ_Count;
    ws->view_start = Global_Analysis_View_Start;
    ws->view_len = Global_Analysis_View_Len;
    ws->sample_rate = Global_Analysis_Sample_Rate;
    ws->center_hz = Global_Analysis_Center_Hz;
    snprintf(ws->path, sizeof(ws->path), "%s", Global_Analysis_Path);
    ws->const_count = Global_Analysis_Const_Count;
    ws->constellation_mode = Global_Analysis_Constellation_Mode;
    ws->constellation_psk_order = Global_Analysis_Constellation_PSK_Order;
    ws->filter_visible = Global_Analysis_Filter_Visible;
    ws->filter_selecting = Global_Analysis_Filter_Selecting;
    ws->filter_active = Global_Analysis_Filter_Active;
    ws->filter_y0 = Global_Analysis_Filter_Y0;
    ws->filter_y1 = Global_Analysis_Filter_Y1;
    ws->noise_visible = Global_Analysis_Noise_Visible;
    ws->noise_selecting = Global_Analysis_Noise_Selecting;
    ws->noise_active = Global_Analysis_Noise_Active;
    ws->noise_graph = Global_Analysis_Noise_Graph;
    ws->noise_y0 = Global_Analysis_Noise_Y0;
    ws->noise_y1 = Global_Analysis_Noise_Y1;
    ws->marker_active = Global_Analysis_Marker_Active;
    ws->marker_sample = Global_Analysis_Marker_Sample;
    ws->marker_time = Global_Analysis_Marker_Time;
    ws->column_selecting = Global_Analysis_Column_Selecting;
    ws->column_visible = Global_Analysis_Column_Visible;
    ws->column_active = Global_Analysis_Column_Active;
    ws->column_x0 = Global_Analysis_Column_X0;
    ws->column_x1 = Global_Analysis_Column_X1;
    ws->signal_menu_open = Global_Analysis_Signal_Menu_Open;
    ws->signal_active_field = Global_Analysis_Signal_Active_Field;
    ws->signal_file_manual_edit = Global_Analysis_Signal_File_Manual_Edit;
    ws->signal_file_cursor = Global_Analysis_Signal_File_Cursor;
    ws->signal_file_selecting = Global_Analysis_Signal_File_Selecting;
    ws->signal_file_selection_start = Global_Analysis_Signal_File_Selection_Start;
    ws->signal_file_selection_end = Global_Analysis_Signal_File_Selection_End;
    snprintf(ws->signal_menu_file, sizeof(ws->signal_menu_file), "%s", Global_Analysis_Signal_Menu_File);
    memcpy(ws->signal_field_text, Global_Analysis_Signal_Field_Text, sizeof(ws->signal_field_text));
    snprintf(ws->status, sizeof(ws->status), "%s", Global_Analysis_Status);
    memcpy(ws->files, Global_Analysis_Files, sizeof(ws->files));
    memcpy(ws->mag_line, Global_Analysis_Mag_Line, sizeof(ws->mag_line));
    memcpy(ws->phase_line, Global_Analysis_Phase_Line, sizeof(ws->phase_line));
    memcpy(ws->inst_freq_line, Global_Analysis_InstFreq_Line, sizeof(ws->inst_freq_line));
    memcpy(ws->psd_line, Global_Analysis_PSD_Line, sizeof(ws->psd_line));
    memcpy(ws->const_i, Global_Analysis_Const_I, sizeof(ws->const_i));
    memcpy(ws->const_q, Global_Analysis_Const_Q, sizeof(ws->const_q));
}

static void ANALYSIS_load_workspace_state(int index) {
    /*
        Purpose: Loads the workspace state
        Returns: No value
    */

    if (index < 0 || index >= ANALYSIS_WORKSPACE_COUNT) {

        return;

    }

    Type_Analysis_Workspace_State *ws = &Global_Analysis_Workspaces[index];

    Global_Analysis_Dirty = ws->dirty;
    Global_Analysis_File_Count = ws->file_count;
    Global_Analysis_Selected = ws->selected;
    Global_Analysis_List_Scroll = ws->list_scroll;
    Global_Analysis_Dragging = ws->dragging;
    Global_Analysis_Drag_Last_X = ws->drag_last_x;
    Global_Analysis_Loading = ws->loading;
    Global_Analysis_Load_Frame = ws->load_frame;
    Global_Analysis_Loaded_Index = ws->loaded_index;
    Global_Analysis_Render_W = ws->render_w;
    Global_Analysis_IQ_Count = ws->iq_count;
    Global_Analysis_View_Start = ws->view_start;
    Global_Analysis_View_Len = ws->view_len;
    Global_Analysis_Sample_Rate = ws->sample_rate;
    Global_Analysis_Center_Hz = ws->center_hz;
    snprintf(Global_Analysis_Path, sizeof(Global_Analysis_Path), "%s", ws->path);
    Global_Analysis_Const_Count = ws->const_count;
    Global_Analysis_Constellation_Mode = ws->constellation_mode;
    Global_Analysis_Constellation_PSK_Order = ws->constellation_psk_order;
    Global_Analysis_Constellation_PSK_Prompt_Open = 0;
    Global_Analysis_Filter_Visible = ws->filter_visible;
    Global_Analysis_Filter_Selecting = ws->filter_selecting;
    Global_Analysis_Filter_Active = ws->filter_active;
    Global_Analysis_Filter_Y0 = ws->filter_y0;
    Global_Analysis_Filter_Y1 = ws->filter_y1;
    Global_Analysis_Noise_Visible = ws->noise_visible;
    Global_Analysis_Noise_Selecting = ws->noise_selecting;
    Global_Analysis_Noise_Active = ws->noise_active;
    Global_Analysis_Noise_Graph = ws->noise_graph;
    Global_Analysis_Noise_Y0 = ws->noise_y0;
    Global_Analysis_Noise_Y1 = ws->noise_y1;
    Global_Analysis_Marker_Active = ws->marker_active;
    Global_Analysis_Marker_Sample = ws->marker_sample;
    Global_Analysis_Marker_Time = ws->marker_time;
    Global_Analysis_Column_Selecting = ws->column_selecting;
    Global_Analysis_Column_Visible = ws->column_visible;
    Global_Analysis_Column_Active = ws->column_active;
    Global_Analysis_Column_X0 = ws->column_x0;
    Global_Analysis_Column_X1 = ws->column_x1;
    Global_Analysis_Signal_Menu_Open = ws->signal_menu_open;
    Global_Analysis_Signal_Active_Field = ws->signal_active_field;
    Global_Analysis_Signal_File_Manual_Edit = ws->signal_file_manual_edit;
    Global_Analysis_Signal_File_Cursor = ws->signal_file_cursor;
    Global_Analysis_Signal_File_Selecting = ws->signal_file_selecting;
    Global_Analysis_Signal_File_Selection_Start = ws->signal_file_selection_start;
    Global_Analysis_Signal_File_Selection_End = ws->signal_file_selection_end;
    snprintf(Global_Analysis_Signal_Menu_File, sizeof(Global_Analysis_Signal_Menu_File), "%s", ws->signal_menu_file);
    memcpy(Global_Analysis_Signal_Field_Text, ws->signal_field_text, sizeof(Global_Analysis_Signal_Field_Text));
    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "%s", ws->status);
    memcpy(Global_Analysis_Files, ws->files, sizeof(Global_Analysis_Files));
    memcpy(Global_Analysis_Mag_Line, ws->mag_line, sizeof(Global_Analysis_Mag_Line));
    memcpy(Global_Analysis_Phase_Line, ws->phase_line, sizeof(Global_Analysis_Phase_Line));
    memcpy(Global_Analysis_InstFreq_Line, ws->inst_freq_line, sizeof(Global_Analysis_InstFreq_Line));
    memcpy(Global_Analysis_PSD_Line, ws->psd_line, sizeof(Global_Analysis_PSD_Line));
    memcpy(Global_Analysis_Const_I, ws->const_i, sizeof(Global_Analysis_Const_I));
    memcpy(Global_Analysis_Const_Q, ws->const_q, sizeof(Global_Analysis_Const_Q));
}

static void ANALYSIS_switch_workspace(int delta) {
    /*
        Purpose: Switches the workspace
        Returns: No value
    */

    if (delta == 0) {

        return;

    }

    ANALYSIS_save_workspace_state(Global_Analysis_Active_Workspace);

    Global_Analysis_Active_Workspace += delta;

    if (Global_Analysis_Active_Workspace < 0) {

        Global_Analysis_Active_Workspace = ANALYSIS_WORKSPACE_COUNT - 1;

    }

    if (Global_Analysis_Active_Workspace >= ANALYSIS_WORKSPACE_COUNT) {

        Global_Analysis_Active_Workspace = 0;

    }

    ANALYSIS_load_workspace_state(Global_Analysis_Active_Workspace);

    Global_Analysis_Dragging = 0;
    Global_Analysis_Filter_Selecting = 0;
    Global_Analysis_Column_Selecting = 0;
    Global_Analysis_Noise_Selecting = 0;
    Global_Analysis_Dirty = 1;
}

// ==========================
// Greyscale Recordings Viewer
// ==========================

static int ANALYSIS_name_compare(const void *a, const void *b) {
    /*

    Purpose: Compares analysis recording file names for sorting

    Return: Sort order

    */

    const char *sa = (const char *)a;
    const char *sb = (const char *)b;

    return strcmp(sa, sb);
}

static int ANALYSIS_is_complex16_file(const char *name) {
    /*
        Purpose: Checks whether a filename has the complex16 recording suffix
        Returns: Match status
    */

    size_t len = strlen(name);
    const char *suffix = ".complex16";
    size_t suffix_len = strlen(suffix);

    if (len < suffix_len) {

        return 0;

    }

    return strcmp(name + len - suffix_len, suffix) == 0;
}

static void ANALYSIS_clear_loaded_file(void) {
    /*
        Purpose: Clears the currently loaded analysis recording state
        Returns: No value
    */

    Global_Analysis_Loaded_Index = -1;
    Global_Analysis_IQ_Count = 0;
    Global_Analysis_View_Start = 0;
    Global_Analysis_View_Len = 0;
    Global_Analysis_Sample_Rate = 0.0;
    Global_Analysis_Center_Hz = 0.0;
    Global_Analysis_Path[0] = '\0';
    Global_Analysis_Render_W = 0;
    memset(Global_Analysis_Mag_Line, 0, sizeof(Global_Analysis_Mag_Line));
    memset(Global_Analysis_Phase_Line, 0, sizeof(Global_Analysis_Phase_Line));
    memset(Global_Analysis_InstFreq_Line, 0, sizeof(Global_Analysis_InstFreq_Line));
    memset(Global_Analysis_PSD_Line, 0, sizeof(Global_Analysis_PSD_Line));
    memset(Global_Analysis_Const_I, 0, sizeof(Global_Analysis_Const_I));
    memset(Global_Analysis_Const_Q, 0, sizeof(Global_Analysis_Const_Q));
    memset(Global_Analysis_Noise_Column_Mask, 0, sizeof(Global_Analysis_Noise_Column_Mask));
    Global_Analysis_Const_Count = 0;
    Global_Analysis_Constellation_Mode = ANALYSIS_CONSTELLATION_MODE_OFF;
    Global_Analysis_Constellation_PSK_Order = ANALYSIS_CONSTELLATION_PSK_BPSK;
    Global_Analysis_Constellation_PSK_Prompt_Open = 0;
    ANALYSIS_invalidate_constellation_cache();
    Global_Analysis_Filter_Visible = 0;
    Global_Analysis_Filter_Selecting = 0;
    Global_Analysis_Filter_Active = 0;
    Global_Analysis_Filter_Y0 = 0.40;
    Global_Analysis_Filter_Y1 = 0.60;
    Global_Analysis_Noise_Key_Down = 0;
    Global_Analysis_Noise_Visible = 0;
    Global_Analysis_Noise_Selecting = 0;
    Global_Analysis_Noise_Active = 0;
    Global_Analysis_Noise_Graph = ANALYSIS_NOISE_GRAPH_NONE;
    Global_Analysis_Noise_Y0 = 0.0;
    Global_Analysis_Noise_Y1 = 0.0;
    memset(Global_Analysis_Noise_Column_Mask, 0, sizeof(Global_Analysis_Noise_Column_Mask));
    Global_Analysis_Marker_Active = 0;
    Global_Analysis_Marker_Sample = 0;
    Global_Analysis_Marker_Time = 0.0;
    Global_Analysis_Column_Selecting = 0;
    Global_Analysis_Column_Visible = 0;
    Global_Analysis_Column_Active = 0;
    Global_Analysis_Column_X0 = 0.0;
    Global_Analysis_Column_X1 = 0.0;
    Global_Analysis_Signal_Menu_Open = 0;
    Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
    Global_Analysis_Signal_File_Manual_Edit = 0;
    Global_Analysis_Signal_File_Selecting = 0;
    Global_Analysis_Signal_File_Selection_Start = -1;
    Global_Analysis_Signal_File_Selection_End = -1;
    Global_Analysis_Signal_Menu_File[0] = '\0';
    memset(Global_Analysis_Signal_Field_Text, 0, sizeof(Global_Analysis_Signal_Field_Text));
    Global_Analysis_Dirty = 1;
}

static void ANALYSIS_clear_current_workspace(void) {
    /*
        Purpose: Clears the active Analysis workspace so Correlation can reuse it
        Returns: No value
    */

    ANALYSIS_clear_loaded_file();
    Global_Analysis_Loading = 0;
    Global_Analysis_Load_Frame = 0;
    Global_Analysis_Dragging = 0;
    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Workspace %d cleared and available for another signal", Global_Analysis_Active_Workspace + 1);
    ANALYSIS_save_workspace_state(Global_Analysis_Active_Workspace);
}

static void ANALYSIS_parse_recording_metadata(const char *name) {
    /*
        Purpose: Parses sample rate and center frequency from a recording filename
        Returns: No value
    */

    Global_Analysis_Center_Hz = (double)Global_Analysis_Fallback_Center_Hz;
    Global_Analysis_Sample_Rate =
        (double)((Global_Analysis_Fallback_Rec_Out_Rate_Hz > 0) ? Global_Analysis_Fallback_Rec_Out_Rate_Hz
                                                                : Global_Analysis_Fallback_Sample_Rate_Hz);

    const char *cap = strstr(name, "_CAPTURE_");

    if (cap) {

        double mhz = 0.0;

        if (sscanf(cap, "_CAPTURE_%lfMHz", &mhz) == 1 && mhz > 0.0) {

            Global_Analysis_Center_Hz = mhz * 1e6;

        }

    }

    const char *sr = strstr(name, "_SR_");

    if (sr) {

        double khz = 0.0;

        if (sscanf(sr, "_SR_%lfk", &khz) == 1 && khz > 0.0) {

            Global_Analysis_Sample_Rate = khz * 1000.0;

        }

    }

    if (Global_Analysis_Sample_Rate <= 0.0) {

        Global_Analysis_Sample_Rate = (double)Global_Analysis_Fallback_Sample_Rate_Hz;

    }
}

static int ANALYSIS_scan_recordings(void) {
    /*
        Purpose: Scans the recording directory for complex16 files
        Returns: Scan status
    */

    DIR *dir = opendir(Global_Analysis_Record_Dir);

    Global_Analysis_File_Count = 0;

    if (!dir) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Could not open recording directory: %.180s",
                 Global_Analysis_Record_Dir);
        ANALYSIS_clear_loaded_file();
        return 0;

    }

    struct dirent *entry = NULL;

    while ((entry = readdir(dir)) != NULL && Global_Analysis_File_Count < ANALYSIS_MAX_FILES) {

        if (!ANALYSIS_is_complex16_file(entry->d_name)) {

            continue;

        }

        snprintf(Global_Analysis_Files[Global_Analysis_File_Count],
                 sizeof(Global_Analysis_Files[Global_Analysis_File_Count]), "%s", entry->d_name);
        Global_Analysis_File_Count++;
    }

    closedir(dir);

    qsort(Global_Analysis_Files, (size_t)Global_Analysis_File_Count, sizeof(Global_Analysis_Files[0]),
          ANALYSIS_name_compare);

    if (Global_Analysis_File_Count <= 0) {

        Global_Analysis_Selected = 0;
        Global_Analysis_List_Scroll = 0;
        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "No .complex16 recordings found in %.180s",
                 Global_Analysis_Record_Dir);
        ANALYSIS_clear_loaded_file();
        return 0;

    }

    if (Global_Analysis_Selected < 0) {

        Global_Analysis_Selected = 0;

    }

    if (Global_Analysis_Selected >= Global_Analysis_File_Count) {

        Global_Analysis_Selected = Global_Analysis_File_Count - 1;

    }

    if (Global_Analysis_List_Scroll < 0) {

        Global_Analysis_List_Scroll = 0;

    }

    if (Global_Analysis_List_Scroll >= Global_Analysis_File_Count) {

        Global_Analysis_List_Scroll = Global_Analysis_File_Count - 1;

    }

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Found %d recording(s) in %.180s",
             Global_Analysis_File_Count, Global_Analysis_Record_Dir);

    return 1;
}

static int ANALYSIS_open_selected_recording(void) {
    /*
        Purpose: Opens the selected recording for analysis
        Returns: Open status
    */

    if (Global_Analysis_File_Count <= 0) {

        return 0;

    }

    Global_Analysis_Loading = 1;
    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Loading %.180s",
             Global_Analysis_Files[Global_Analysis_Selected]);

    char path[1024];

    snprintf(path, sizeof(path), "%s/%s", Global_Analysis_Record_Dir, Global_Analysis_Files[Global_Analysis_Selected]);

    FILE *fp = NULL;
    size_t iq_count = 0;

    if (!sec_fopen_complex16(path, &fp, &iq_count)) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Invalid or unreadable IQ file: %.180s",
                 Global_Analysis_Files[Global_Analysis_Selected]);
        ANALYSIS_clear_loaded_file();
        Global_Analysis_Loading = 0;
        return 0;

    }

    fclose(fp);

    snprintf(Global_Analysis_Path, sizeof(Global_Analysis_Path), "%s", path);
    ANALYSIS_invalidate_constellation_cache();

    Global_Analysis_Loaded_Index = Global_Analysis_Selected;
    Global_Analysis_IQ_Count = iq_count;
    Global_Analysis_View_Start = 0;
    Global_Analysis_View_Len = Global_Analysis_IQ_Count;

    if (Global_Analysis_View_Len < ANALYSIS_FFT_SIZE) {

        Global_Analysis_View_Len = Global_Analysis_IQ_Count;

    }

    ANALYSIS_parse_recording_metadata(Global_Analysis_Files[Global_Analysis_Selected]);

    Global_Analysis_Marker_Active = 0;
    Global_Analysis_Marker_Sample = 0;
    Global_Analysis_Marker_Time = 0.0;
    Global_Analysis_Column_Selecting = 0;
    Global_Analysis_Column_Visible = 0;
    Global_Analysis_Column_Active = 0;
    Global_Analysis_Column_X0 = 0.0;
    Global_Analysis_Column_X1 = 0.0;
    ANALYSIS_clear_noise_filter();

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Opened %.180s | %.6f sec",
             Global_Analysis_Files[Global_Analysis_Selected],
             Global_Analysis_Sample_Rate > 0.0 ? (double)Global_Analysis_IQ_Count / Global_Analysis_Sample_Rate : 0.0);

    Global_Analysis_Dirty = 1;

    return 1;
}

static const char *ANALYSIS_path_file_name(const char *path) {
    /*
        Purpose: Returns the filename portion of an analysis recording path
        Returns: Filename pointer
    */

    const char *slash;

    if (!path) {

        return "";

    }

    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int ANALYSIS_workspace_is_empty(int index) {
    /*
        Purpose: Checks whether an Analysis workspace can accept an exported recording
        Returns: Boolean status
    */

    if (index < 0 || index >= ANALYSIS_WORKSPACE_COUNT) {

        return 0;

    }

    if (!Global_Analysis_Workspaces_Initialized) {

        return 1;

    }

    if (index == Global_Analysis_Active_Workspace) {

        return Global_Analysis_Path[0] == '\0' || Global_Analysis_IQ_Count == 0;

    }

    return Global_Analysis_Workspaces[index].path[0] == '\0' || Global_Analysis_Workspaces[index].iq_count == 0;
}

static int ANALYSIS_initialize_workspaces_for_export(void) {
    /*
        Purpose: Initializes Analysis workspaces when Correlation exports before Analysis was opened
        Returns: Initialization status
    */

    int previous_mode;

    if (Global_Analysis_Workspaces_Initialized) {

        return 1;

    }

    previous_mode = Global_Analysis_Mode;
    Global_Analysis_Active_Workspace = 0;

    for (int i = 0; i < ANALYSIS_WORKSPACE_COUNT; i++) {
        Global_Analysis_Active_Workspace = i;
        ANALYSIS_clear_loaded_file();
        (void)ANALYSIS_scan_recordings();
        ANALYSIS_save_workspace_state(i);
    }

    Global_Analysis_Workspaces_Initialized = 1;
    Global_Analysis_Active_Workspace = 0;
    ANALYSIS_load_workspace_state(0);
    Global_Analysis_Mode = previous_mode;
    return 1;
}

int ANALYSIS_get_recording_workspace(const char *file_name) {
    /*
        Purpose: Finds the Analysis workspace currently holding a recording
        Returns: One-based workspace number, or zero when not loaded
    */

    if (!file_name || !file_name[0] || !Global_Analysis_Workspaces_Initialized) {

        return 0;

    }

    for (int i = 0; i < ANALYSIS_WORKSPACE_COUNT; i++) {
        const char *path =
            i == Global_Analysis_Active_Workspace ? Global_Analysis_Path : Global_Analysis_Workspaces[i].path;

        if (path[0] && strcmp(ANALYSIS_path_file_name(path), file_name) == 0) {

            return i + 1;

        }
    }

    return 0;
}

int ANALYSIS_get_available_workspace_count(void) {
    /*
        Purpose: Counts Analysis workspaces available for Correlation exports
        Returns: Empty workspace count
    */

    int available = 0;

    if (!Global_Analysis_Workspaces_Initialized) {

        return ANALYSIS_WORKSPACE_COUNT;

    }

    for (int i = 0; i < ANALYSIS_WORKSPACE_COUNT; i++) {

        if (ANALYSIS_workspace_is_empty(i)) {

            available++;

        }
    }

    return available;
}

int ANALYSIS_export_recording_to_workspace(const char *record_dir, const char *file_name, uint64_t fallback_center_hz,
                                           uint32_t fallback_rec_out_rate_hz, uint32_t fallback_sample_rate_hz,
                                           int *workspace_number, char *error, size_t error_size) {
    /*
        Purpose: Loads a Correlation result into the first empty Analysis workspace
        Returns: Export status
    */

    int existing_workspace;
    int target_workspace = -1;
    int original_workspace;
    int file_index = -1;
    int opened = 0;

    if (workspace_number) {

        *workspace_number = 0;

    }

    if (error && error_size > 0) {

        error[0] = '\0';

    }

    if (!file_name || !file_name[0] || strchr(file_name, '/') || strchr(file_name, '\\')) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Invalid recording filename.");

        }
        return 0;

    }

    ANALYSIS_set_context(record_dir, fallback_center_hz, fallback_rec_out_rate_hz, fallback_sample_rate_hz);

    if (!ANALYSIS_initialize_workspaces_for_export()) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Unable to initialize Analysis workspaces.");

        }
        return 0;

    }

    existing_workspace = ANALYSIS_get_recording_workspace(file_name);

    if (existing_workspace > 0) {

        if (workspace_number) {

            *workspace_number = existing_workspace;

        }
        return 1;

    }

    for (int i = 0; i < ANALYSIS_WORKSPACE_COUNT; i++) {

        if (ANALYSIS_workspace_is_empty(i)) {

            target_workspace = i;
            break;

        }
    }

    if (target_workspace < 0) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "All five Analysis workspaces are occupied. Clear a workspace and try again.");

        }
        return 0;

    }

    original_workspace = Global_Analysis_Active_Workspace;
    ANALYSIS_save_workspace_state(original_workspace);
    Global_Analysis_Active_Workspace = target_workspace;
    ANALYSIS_load_workspace_state(target_workspace);

    if (ANALYSIS_scan_recordings()) {

        for (int i = 0; i < Global_Analysis_File_Count; i++) {

            if (strcmp(Global_Analysis_Files[i], file_name) == 0) {

                file_index = i;
                break;

            }
        }

    }

    if (file_index >= 0) {

        Global_Analysis_Selected = file_index;
        Global_Analysis_List_Scroll = file_index > 2 ? file_index - 2 : 0;
        opened = ANALYSIS_open_selected_recording();

    }

    if (opened) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Exported from Correlation: %.180s",
                 file_name);
        ANALYSIS_save_workspace_state(target_workspace);

    }

    Global_Analysis_Active_Workspace = original_workspace;
    ANALYSIS_load_workspace_state(original_workspace);

    if (!opened) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Unable to open %.180s in Analysis.", file_name);

        }
        return 0;

    }

    if (workspace_number) {

        *workspace_number = target_workspace + 1;

    }

    return 1;
}

static void ANALYSIS_select_relative(int delta) {
    /*
        Purpose: Moves the selected analysis file by a relative offset
        Returns: No value
    */

    if (Global_Analysis_File_Count <= 0) {

        return;

    }

    Global_Analysis_Selected += delta;

    if (Global_Analysis_Selected < 0) {

        Global_Analysis_Selected = Global_Analysis_File_Count - 1;

    }

    if (Global_Analysis_Selected >= Global_Analysis_File_Count) {

        Global_Analysis_Selected = 0;

    }

    Global_Analysis_List_Scroll = Global_Analysis_Selected - 2;

    if (Global_Analysis_List_Scroll < 0) {

        Global_Analysis_List_Scroll = 0;

    }

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Selected %.180s | Press Enter to open",
             Global_Analysis_Files[Global_Analysis_Selected]);
}

static void ANALYSIS_zoom_at_fraction(double frac, int zoom_in) {
    /*
        Purpose: Zooms the analysis view around a fractional cursor position
        Returns: No value
    */

    if (Global_Analysis_IQ_Count == 0 || Global_Analysis_Path[0] == '\0') {

        return;

    }

    if (frac < 0.0) {

        frac = 0.0;

    }

    if (frac > 1.0) {

        frac = 1.0;

    }

    size_t cursor_sample = Global_Analysis_View_Start + (size_t)(frac * (double)Global_Analysis_View_Len);

    size_t min_len = ANALYSIS_FFT_SIZE * 8;

    if (min_len > Global_Analysis_IQ_Count) {

        min_len = Global_Analysis_IQ_Count;

    }

    if (zoom_in) {

        Global_Analysis_View_Len /= 2;

        if (Global_Analysis_View_Len < min_len) {

            Global_Analysis_View_Len = min_len;

        }

    }

    else {

        Global_Analysis_View_Len *= 2;

        if (Global_Analysis_View_Len > Global_Analysis_IQ_Count) {

            Global_Analysis_View_Len = Global_Analysis_IQ_Count;

        }

    }

    size_t anchor = (size_t)(frac * (double)Global_Analysis_View_Len);

    if (cursor_sample > anchor) {

        Global_Analysis_View_Start = cursor_sample - anchor;

    }

    else {

        Global_Analysis_View_Start = 0;

    }

    if (Global_Analysis_View_Start + Global_Analysis_View_Len > Global_Analysis_IQ_Count) {

        Global_Analysis_View_Start = Global_Analysis_IQ_Count > Global_Analysis_View_Len
                                         ? Global_Analysis_IQ_Count - Global_Analysis_View_Len
                                         : 0;

    }

    Global_Analysis_Dirty = 1;
}

static void ANALYSIS_drag_move_view(int dx, int graph_w) {
    /*
        Purpose: Moves the analysis view according to horizontal mouse drag distance
        Returns: No value
    */

    if (Global_Analysis_IQ_Count == 0 || Global_Analysis_Path[0] == '\0' || graph_w <= 0 ||
        Global_Analysis_View_Len >= Global_Analysis_IQ_Count) {

        return;

    }

    double samples_per_pixel = (double)Global_Analysis_View_Len / (double)graph_w;
    long delta_samples = (long)((double)(-dx) * samples_per_pixel);

    if (delta_samples == 0 && dx != 0) {

        delta_samples = (dx > 0) ? -1 : 1;

    }

    if (delta_samples < 0) {

        size_t step = (size_t)(-delta_samples);

        Global_Analysis_View_Start = Global_Analysis_View_Start > step ? Global_Analysis_View_Start - step : 0;

    }

    else if (delta_samples > 0) {

        size_t step = (size_t)delta_samples;
        size_t max_start = Global_Analysis_IQ_Count > Global_Analysis_View_Len
                               ? Global_Analysis_IQ_Count - Global_Analysis_View_Len
                               : 0;

        if (Global_Analysis_View_Start + step < max_start) {

            Global_Analysis_View_Start += step;

        }

        else {

            Global_Analysis_View_Start = max_start;

        }

    }

    Global_Analysis_Dirty = 1;
}

static void ANALYSIS_get_layout(int win_w, int win_h, SDL_Rect *list_rect, SDL_Rect *spec_rect) {
    /*
        Purpose: Computes analysis workstation rectangles used by input handling
        Returns: No value
    */

    int selector_h = (int)((double)win_h * 0.22);

    if (selector_h < 130) {

        selector_h = 130;

    }

    int gap = 10;
    int title_h = 22;
    int col_gap = 12;
    int panel_h = selector_h - MARGIN;
    int work_w = win_w - 2 * MARGIN;
    int half_w = (work_w - col_gap) / 2;

    if (half_w < 60) {

        half_w = work_w / 2;

    }

    SDL_Rect local_list = {MARGIN, MARGIN, half_w, panel_h};

    int work_x = MARGIN;
    int work_y = local_list.y + local_list.h + MARGIN;

    int top_row_h = panel_h - title_h;
    int mid_row_h = top_row_h;

    if (top_row_h < 70) {

        top_row_h = 70;

    }

    if (mid_row_h < 70) {

        mid_row_h = 70;

    }

    int mag_y = work_y + title_h;
    int mid_title_y = mag_y + top_row_h + gap;
    int inst_y = mid_title_y + title_h;
    int spec_title_y = inst_y + mid_row_h + gap;

    SDL_Rect local_spec = {work_x, spec_title_y + title_h, work_w, win_h - (spec_title_y + title_h) - MARGIN};

    if (local_spec.h < 110) {

        local_spec.h = 110;

    }

    if (list_rect) {

        *list_rect = local_list;

    }

    if (spec_rect) {

        *spec_rect = local_spec;

    }
}

static void ANALYSIS_get_hover_graph_layout(int win_w, int win_h, SDL_Rect *psd_rect, SDL_Rect *mag_rect,
                                            SDL_Rect *phase_rect, SDL_Rect *inst_rect, SDL_Rect *const_rect,
                                            SDL_Rect *spec_rect) {
    /*
        Purpose: Computes the visible analysis graph rectangles for hover-sync lines
        Returns: No value
    */

    if (psd_rect) {

        *psd_rect = (SDL_Rect){0, 0, 0, 0};

    }

    if (mag_rect) {

        *mag_rect = (SDL_Rect){0, 0, 0, 0};

    }

    if (phase_rect) {

        *phase_rect = (SDL_Rect){0, 0, 0, 0};

    }

    if (inst_rect) {

        *inst_rect = (SDL_Rect){0, 0, 0, 0};

    }

    if (const_rect) {

        *const_rect = (SDL_Rect){0, 0, 0, 0};

    }

    if (spec_rect) {

        *spec_rect = (SDL_Rect){0, 0, 0, 0};

    }

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

    SDL_Rect local_psd = {MARGIN + half_w + col_gap, MARGIN + title_h, work_w - half_w - col_gap, panel_h - title_h};

    if (local_psd.h < 70) {

        local_psd.h = 70;

    }

    int work_y = list_rect.y + list_rect.h + MARGIN;

    int top_row_h = local_psd.h;
    int mid_row_h = local_psd.h;

    if (top_row_h < 70) {

        top_row_h = 70;

    }

    if (mid_row_h < 70) {

        mid_row_h = 70;

    }

    int top_title_y = work_y;

    SDL_Rect local_mag = {work_x, top_title_y + title_h, half_w, top_row_h};

    SDL_Rect local_phase = {work_x + half_w + col_gap, top_title_y + title_h, work_w - half_w - col_gap, top_row_h};

    int mid_title_y = local_mag.y + local_mag.h + gap;

    SDL_Rect local_inst = {work_x, mid_title_y + title_h, half_w, mid_row_h};

    SDL_Rect local_const = {work_x + half_w + col_gap, mid_title_y + title_h, work_w - half_w - col_gap, mid_row_h};

    int spec_title_y = local_inst.y + local_inst.h + gap;

    SDL_Rect local_spec = {work_x, spec_title_y + title_h, work_w, win_h - (spec_title_y + title_h) - MARGIN};

    if (local_spec.h < 110) {

        local_spec.h = 110;

    }

    if (psd_rect) {

        *psd_rect = local_psd;

    }

    if (mag_rect) {

        *mag_rect = local_mag;

    }

    if (phase_rect) {

        *phase_rect = local_phase;

    }

    if (inst_rect) {

        *inst_rect = local_inst;

    }

    if (const_rect) {

        *const_rect = local_const;

    }

    if (spec_rect) {

        *spec_rect = local_spec;

    }
}

static int ANALYSIS_get_constellation_mode_button_rects(int win_w, int win_h, SDL_Rect *rects) {
    /*
        Purpose: Computes the equal-size constellation family button rectangles
        Returns: Number of rectangles written
    */

    if (!rects) {

        return 0;

    }

    SDL_Rect psd_rect;
    SDL_Rect mag_rect;
    SDL_Rect phase_rect;
    SDL_Rect inst_rect;
    SDL_Rect const_rect;
    SDL_Rect spec_rect;

    ANALYSIS_get_hover_graph_layout(win_w, win_h, &psd_rect, &mag_rect, &phase_rect, &inst_rect, &const_rect,
                                    &spec_rect);
    (void)psd_rect;
    (void)mag_rect;
    (void)phase_rect;
    (void)inst_rect;
    (void)spec_rect;

    const int title_height = 22;
    const int title_reserve = 148;
    const int gap = 4;
    int available_width = const_rect.w - title_reserve;

    if (available_width <= gap * (ANALYSIS_CONSTELLATION_MODE_COUNT - 1)) {

        return 0;

    }

    int button_width =
        (available_width - gap * (ANALYSIS_CONSTELLATION_MODE_COUNT - 1)) / ANALYSIS_CONSTELLATION_MODE_COUNT;

    if (button_width < 42) {

        return 0;

    }

    int start_x = const_rect.x + title_reserve;
    int title_y = const_rect.y - title_height;

    for (int i = 0; i < ANALYSIS_CONSTELLATION_MODE_COUNT; i++) {
        rects[i] = (SDL_Rect){start_x + i * (button_width + gap), title_y, button_width, title_height};
    }

    return ANALYSIS_CONSTELLATION_MODE_COUNT;
}

static void ANALYSIS_draw_constellation_mode_buttons(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the constellation modulation-family controls beside the graph title
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Mode) {

        return;

    }

    SDL_Rect rects[ANALYSIS_CONSTELLATION_MODE_COUNT];
    int count = ANALYSIS_get_constellation_mode_button_rects(win_w, win_h, rects);
    int mouse_x = 0;
    int mouse_y = 0;

    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    for (int i = 0; i < count; i++) {
        int selected = Global_Analysis_Constellation_Mode == i;
        int hover = point_in_rect(mouse_x, mouse_y, rects[i]);
        SDL_Color fill =
            selected ? (SDL_Color){0, 70, 28, 245} : (hover ? (SDL_Color){24, 34, 28, 240} : (SDL_Color){5, 8, 6, 230});
        SDL_Color border = selected ? (SDL_Color){0, 255, 90, 255}
                                    : (hover ? (SDL_Color){0, 205, 76, 245} : (SDL_Color){0, 120, 48, 225});
        SDL_Color text = selected ? (SDL_Color){235, 255, 242, 255}
                                  : (hover ? (SDL_Color){190, 255, 210, 255} : (SDL_Color){120, 205, 145, 255});

        draw_filled_rect(renderer, rects[i], fill);
        draw_outline_rect(renderer, rects[i], border);
        ANALYSIS_draw_centered_button_text(renderer, font, rects[i], Global_Analysis_Constellation_Mode_Labels[i],
                                           text);
    }
}

static void ANALYSIS_get_constellation_psk_prompt_rects(int win_w, int win_h, SDL_Rect *panel_rect,
                                                        SDL_Rect *option_rects) {
    /*
        Purpose: Computes the PSK subtype prompt and option rectangles
        Returns: No value
    */

    int panel_w = 540;
    int panel_h = 190;

    if (panel_w > win_w - 60) {

        panel_w = win_w - 60;

    }

    if (panel_h > win_h - 60) {

        panel_h = win_h - 60;

    }

    SDL_Rect panel = {(win_w - panel_w) / 2, (win_h - panel_h) / 2, panel_w, panel_h};

    if (panel_rect) {

        *panel_rect = panel;

    }

    if (option_rects) {

        const int gap = 12;
        const int side_margin = 24;
        int button_width = (panel.w - side_margin * 2 - gap * 2) / ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT;
        int button_y = panel.y + panel.h - 62;

        for (int i = 0; i < ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT; i++) {
            option_rects[i] = (SDL_Rect){panel.x + side_margin + i * (button_width + gap), button_y, button_width, 36};
        }

    }
}

static void ANALYSIS_draw_constellation_psk_prompt(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the BPSK, QPSK, and 8PSK selection prompt
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Constellation_PSK_Prompt_Open) {

        return;

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 170});

    SDL_Rect panel;
    SDL_Rect option_rects[ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT];
    ANALYSIS_get_constellation_psk_prompt_rects(win_w, win_h, &panel, option_rects);

    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 248});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 255, 90, 255});

    SDL_Rect title_bar = {panel.x, panel.y, panel.w, 52};
    draw_filled_rect(renderer, title_bar, (SDL_Color){0, 24, 8, 245});
    draw_outline_rect(renderer, title_bar, (SDL_Color){0, 160, 60, 230});

    draw_text(renderer, font, "Select PSK Modulation", panel.x + 22, panel.y + 17, (SDL_Color){0, 255, 90, 255});
    draw_text(renderer, font, "Choose the PSK order used for carrier and symbol recovery.", panel.x + 22, panel.y + 76,
              (SDL_Color){220, 220, 220, 255});

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    for (int i = 0; i < ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT; i++) {
        int selected = Global_Analysis_Constellation_Mode == ANALYSIS_CONSTELLATION_MODE_PSK &&
                       Global_Analysis_Constellation_PSK_Order == Global_Analysis_Constellation_PSK_Orders[i];
        int hover = point_in_rect(mouse_x, mouse_y, option_rects[i]);
        SDL_Color fill = selected ? (SDL_Color){0, 70, 28, 255}
                                  : (hover ? (SDL_Color){24, 44, 24, 255} : (SDL_Color){8, 18, 10, 255});
        SDL_Color border = selected ? (SDL_Color){0, 255, 90, 255}
                                    : (hover ? (SDL_Color){0, 220, 80, 255} : (SDL_Color){110, 140, 120, 255});
        SDL_Color text = selected ? (SDL_Color){235, 255, 242, 255} : (SDL_Color){205, 225, 212, 255};

        draw_filled_rect(renderer, option_rects[i], fill);
        draw_outline_rect(renderer, option_rects[i], border);
        ANALYSIS_draw_centered_button_text(renderer, font, option_rects[i], Global_Analysis_Constellation_PSK_Labels[i],
                                           text);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static int ANALYSIS_handle_constellation_psk_prompt_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles PSK subtype selection while the prompt is open
        Returns: Boolean handling status
    */

    if (!event || !Global_Analysis_Constellation_PSK_Prompt_Open) {

        return 0;

    }

    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) {

        Global_Analysis_Constellation_PSK_Prompt_Open = 0;
        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        SDL_Rect panel;
        SDL_Rect option_rects[ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT];
        ANALYSIS_get_constellation_psk_prompt_rects(win_w, win_h, &panel, option_rects);

        for (int i = 0; i < ANALYSIS_CONSTELLATION_PSK_OPTION_COUNT; i++) {

            if (!point_in_rect(event->button.x, event->button.y, option_rects[i])) {

                continue;

            }

            Global_Analysis_Constellation_Mode = ANALYSIS_CONSTELLATION_MODE_PSK;
            Global_Analysis_Constellation_PSK_Order = Global_Analysis_Constellation_PSK_Orders[i];
            Global_Analysis_Constellation_PSK_Prompt_Open = 0;
            ANALYSIS_invalidate_constellation_cache();
            Global_Analysis_Dirty = 1;

            snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "IQ constellation modulation: %s",
                     Global_Analysis_Constellation_PSK_Labels[i]);
            return 1;
        }

        (void)panel;
        return 1;

    }

    return 1;
}

static int ANALYSIS_handle_constellation_mode_click(int x, int y, int win_w, int win_h) {
    /*
        Purpose: Selects the requested constellation processing family
        Returns: Boolean handling status
    */

    SDL_Rect rects[ANALYSIS_CONSTELLATION_MODE_COUNT];
    int count = ANALYSIS_get_constellation_mode_button_rects(win_w, win_h, rects);

    for (int i = 0; i < count; i++) {

        if (!point_in_rect(x, y, rects[i])) {

            continue;

        }

        if (i == ANALYSIS_CONSTELLATION_MODE_PSK) {

            Global_Analysis_Constellation_PSK_Prompt_Open = 1;
            return 1;

        }

        Global_Analysis_Constellation_PSK_Prompt_Open = 0;
        Global_Analysis_Constellation_Mode = i;
        ANALYSIS_invalidate_constellation_cache();
        Global_Analysis_Dirty = 1;

        if (i == ANALYSIS_CONSTELLATION_MODE_OFF) {

            Global_Analysis_Const_Count = 0;
            memset(Global_Analysis_Const_I, 0, sizeof(Global_Analysis_Const_I));
            memset(Global_Analysis_Const_Q, 0, sizeof(Global_Analysis_Const_Q));
            snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                     "IQ constellation disabled for the current signal");

        }

        else {

            snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "IQ constellation family: %s",
                     Global_Analysis_Constellation_Mode_Labels[i]);

        }

        return 1;
    }

    return 0;
}

static void ANALYSIS_draw_hover_sync_line(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws synchronized hover markers across related analysis views
        Returns: No value
    */

    if (!renderer || Global_Analysis_Path[0] == '\0') {

        return;

    }

    SDL_Rect psd_rect;
    SDL_Rect mag_rect;
    SDL_Rect phase_rect;
    SDL_Rect inst_rect;
    SDL_Rect const_rect;
    SDL_Rect spec_rect;

    ANALYSIS_get_hover_graph_layout(win_w, win_h, &psd_rect, &mag_rect, &phase_rect, &inst_rect, &const_rect,
                                    &spec_rect);
    (void)const_rect;

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    SDL_Rect time_rects[4] = {mag_rect, phase_rect, inst_rect, spec_rect};

    int found_time_hover = 0;
    double time_frac = 0.0;

    for (int i = 0; i < 4; i++) {

        if (point_in_rect(mouse_x, mouse_y, time_rects[i]) && time_rects[i].w > 0) {

            time_frac = (double)(mouse_x - time_rects[i].x) / (double)time_rects[i].w;
            found_time_hover = 1;
            break;

        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (found_time_hover) {

        time_frac = ANALYSIS_limit_double(time_frac, 0.0, 1.0);

        SDL_SetRenderDrawColor(renderer, 0, 255, 90, 190);

        for (int i = 0; i < 4; i++) {
            SDL_Rect r = time_rects[i];

            if (r.w <= 0 || r.h <= 0) {

                continue;

            }

            int line_x = r.x + (int)(time_frac * (double)r.w);

            if (line_x < r.x) {

                line_x = r.x;

            }

            if (line_x > r.x + r.w - 1) {

                line_x = r.x + r.w - 1;

            }

            SDL_RenderDrawLine(renderer, line_x, r.y, line_x, r.y + r.h);
        }

    }

    int icon_hover_keeps_frequency_label =
        (Global_Analysis_Signal_Icon_Rect_Valid && point_in_rect(mouse_x, mouse_y, Global_Analysis_Signal_Icon_Rect)) ||
        (Global_Analysis_Multithread_Rect_Valid && point_in_rect(mouse_x, mouse_y, Global_Analysis_Multithread_Rect)) ||
        (Global_Analysis_Transmit_Rect_Valid && point_in_rect(mouse_x, mouse_y, Global_Analysis_Transmit_Rect)) ||
        (Global_Analysis_Signal_Trash_Rect_Valid && point_in_rect(mouse_x, mouse_y, Global_Analysis_Signal_Trash_Rect));

    int found_freq_hover = 0;
    double freq_frac = 0.0;

    if (point_in_rect(mouse_x, mouse_y, spec_rect) && spec_rect.h > 0) {

        freq_frac = (double)(mouse_y - spec_rect.y) / (double)spec_rect.h;
        found_freq_hover = 1;

    }

    else if (point_in_rect(mouse_x, mouse_y, psd_rect) && psd_rect.w > 0) {

        double psd_frac = (double)(mouse_x - psd_rect.x) / (double)psd_rect.w;
        freq_frac = 1.0 - psd_frac;
        found_freq_hover = 1;

    }

    else if (icon_hover_keeps_frequency_label) {

        freq_frac = Global_Analysis_Signal_Icon_Freq_Frac;
        found_freq_hover = 1;

    }

    if (found_freq_hover) {

        freq_frac = ANALYSIS_limit_double(freq_frac, 0.0, 1.0);

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 210);

        if (spec_rect.w > 0 && spec_rect.h > 0) {

            int line_y = spec_rect.y + (int)(freq_frac * (double)spec_rect.h);

            if (line_y < spec_rect.y) {

                line_y = spec_rect.y;

            }

            if (line_y > spec_rect.y + spec_rect.h - 1) {

                line_y = spec_rect.y + spec_rect.h - 1;

            }

            SDL_RenderDrawLine(renderer, spec_rect.x, line_y, spec_rect.x + spec_rect.w, line_y);

        }

        if (psd_rect.w > 0 && psd_rect.h > 0) {

            int line_x = psd_rect.x + (int)((1.0 - freq_frac) * (double)psd_rect.w);

            if (line_x < psd_rect.x) {

                line_x = psd_rect.x;

            }

            if (line_x > psd_rect.x + psd_rect.w - 1) {

                line_x = psd_rect.x + psd_rect.w - 1;

            }

            SDL_RenderDrawLine(renderer, line_x, psd_rect.y, line_x, psd_rect.y + psd_rect.h);

            if (font) {

                double hover_freq_hz = Global_Analysis_Center_Hz + ((0.5 - freq_frac) * Global_Analysis_Sample_Rate);

                char freq_label[96];

                snprintf(freq_label, sizeof(freq_label), "%.6f MHz", hover_freq_hz / 1e6);

                int text_w = 0;
                int text_h = 0;

                if (TTF_SizeText(font, freq_label, &text_w, &text_h) != 0) {

                    text_w = 0;
                    text_h = 0;

                }

                int label_w = 136;

                if (label_w < text_w + 14) {

                    label_w = text_w + 14;

                }

                SDL_Rect settings_rect;
                ANALYSIS_get_signal_icon_rect(win_w, win_h, &settings_rect);

                SDL_Rect label_bg = {settings_rect.x + settings_rect.w + 12, psd_rect.y - text_h - 16, label_w,
                                     text_h + 6};

                if (label_bg.y < 0) {

                    label_bg.y = psd_rect.y + 4;

                }

                if (label_bg.x + label_bg.w > win_w - MARGIN) {

                    label_bg.x = win_w - MARGIN - label_bg.w;

                }

                if (label_bg.x < settings_rect.x + settings_rect.w + 12) {

                    label_bg.x = settings_rect.x + settings_rect.w + 12;

                }

                Global_Analysis_Signal_Icon_Freq_Frac = freq_frac;

                draw_filled_rect(renderer, label_bg, (SDL_Color){0, 0, 0, 210});
                draw_outline_rect(renderer, label_bg, (SDL_Color){0, 170, 255, 220});
                draw_text(renderer, font, freq_label,
                          label_bg.x + 7, // 7
                          label_bg.y + 3, (SDL_Color){0, 200, 255, 255});

            }

        }

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static double ANALYSIS_freq_frac_from_mouse_y(int mouse_y, SDL_Rect spec_rect) {
    /*
        Purpose: Converts a spectrogram mouse Y coordinate into a normalized frequency fraction
        Returns: Frequency fraction
    */

    if (spec_rect.h <= 0) {

        return 0.0;

    }

    double frac = (double)(mouse_y - spec_rect.y) / (double)spec_rect.h;

    return ANALYSIS_limit_double(frac, 0.0, 1.0);
}

static void ANALYSIS_update_filter_from_mouse(int mouse_y, SDL_Rect spec_rect) {
    /*
        Purpose: Updates the analysis frequency filter selector from mouse movement
        Returns: No value
    */

    Global_Analysis_Filter_Y1 = ANALYSIS_freq_frac_from_mouse_y(mouse_y, spec_rect);
}

static void ANALYSIS_apply_filter_selection(void) {
    /*
        Purpose: Applies the analysis frequency filter selector and requests a replot
        Returns: No value
    */

    double y0 = Global_Analysis_Filter_Y0;
    double y1 = Global_Analysis_Filter_Y1;

    if (y1 < y0) {

        double tmp = y0;
        y0 = y1;
        y1 = tmp;

    }

    if (fabs(y1 - y0) < 0.006) {

        double mid = (y0 + y1) * 0.5;
        y0 = mid - 0.003;
        y1 = mid + 0.003;

    }

    Global_Analysis_Filter_Y0 = ANALYSIS_limit_double(y0, 0.0, 1.0);
    Global_Analysis_Filter_Y1 = ANALYSIS_limit_double(y1, 0.0, 1.0);
    Global_Analysis_Filter_Active = 1;
    Global_Analysis_Filter_Visible = 1;
    Global_Analysis_Filter_Selecting = 0;
    ANALYSIS_invalidate_constellation_cache();
    Global_Analysis_Dirty = 1;

    double top = Global_Analysis_Filter_Y0;
    double bottom = Global_Analysis_Filter_Y1;
    double center_y = (top + bottom) * 0.5;
    double bw_frac = fabs(bottom - top);
    double offset_hz = (0.5 - center_y) * Global_Analysis_Sample_Rate;
    double center_hz = Global_Analysis_Center_Hz + offset_hz;
    double bw_hz = bw_frac * Global_Analysis_Sample_Rate;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Filter %.6f MHz | BW %.3f kHz | Backspace clears",
             center_hz / 1e6, bw_hz / 1e3);
}

static void ANALYSIS_clear_filter(void) {
    /*
        Purpose: Clears the analysis frequency filter selector and requests a replot
        Returns: No value
    */

    Global_Analysis_Filter_Visible = 0;
    Global_Analysis_Filter_Selecting = 0;
    Global_Analysis_Filter_Active = 0;

    Global_Analysis_Marker_Active = 0;
    Global_Analysis_Marker_Sample = 0;
    Global_Analysis_Marker_Time = 0.0;

    Global_Analysis_Column_Selecting = 0;
    Global_Analysis_Column_Visible = 0;
    Global_Analysis_Column_Active = 0;
    Global_Analysis_Column_X0 = 0.0;
    Global_Analysis_Column_X1 = 0.0;

    ANALYSIS_clear_noise_filter();
    ANALYSIS_invalidate_constellation_cache();

    Global_Analysis_Dirty = 1;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Analysis filters, noise mask, and marker cleared");
}

static double ANALYSIS_frequency_from_spec_frac(double frac) {
    /*
        Purpose: Converts a greyscale spectrogram vertical fraction into RF frequency
        Returns: Frequency in Hz
    */

    frac = ANALYSIS_limit_double(frac, 0.0, 1.0);

    double offset_hz = (0.5 - frac) * Global_Analysis_Sample_Rate;

    return Global_Analysis_Center_Hz + offset_hz;
}

static void ANALYSIS_get_filter_label(char *out, size_t out_size) {
    /*
        Purpose: Builds the visible frequency label for the analysis frequency filter
        Returns: No value
    */

    if (!out || out_size == 0) {

        return;

    }

    double y0 = Global_Analysis_Filter_Y0;
    double y1 = Global_Analysis_Filter_Y1;

    if (y1 < y0) {

        double tmp = y0;
        y0 = y1;
        y1 = tmp;

    }

    double center_y = (y0 + y1) * 0.5;
    double bw_hz = fabs(y1 - y0) * Global_Analysis_Sample_Rate;
    double center_hz = ANALYSIS_frequency_from_spec_frac(center_y);

    snprintf(out, out_size, "Filtered center %.6f MHz | BW %.3f kHz", center_hz / 1e6, bw_hz / 1e3);
}

static void ANALYSIS_set_marker_from_mouse(int mouse_x, SDL_Rect spec_rect) {
    /*
        Purpose: Places the analysis time marker from a greyscale spectrogram click
        Returns: No value
    */

    if (Global_Analysis_IQ_Count == 0 || Global_Analysis_Path[0] == '\0' || spec_rect.w <= 0 ||
        Global_Analysis_Sample_Rate <= 0.0) {

        return;

    }

    double frac = (double)(mouse_x - spec_rect.x) / (double)spec_rect.w;

    frac = ANALYSIS_limit_double(frac, 0.0, 1.0);

    size_t marker_sample = Global_Analysis_View_Start + (size_t)(frac * (double)Global_Analysis_View_Len);

    if (marker_sample >= Global_Analysis_IQ_Count) {

        marker_sample = Global_Analysis_IQ_Count - 1;

    }

    Global_Analysis_Marker_Active = 1;
    Global_Analysis_Marker_Sample = marker_sample;
    Global_Analysis_Marker_Time = (double)marker_sample / Global_Analysis_Sample_Rate;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Marker %.6f sec | right-click greyscale spectrogram to move", Global_Analysis_Marker_Time);
}

static double ANALYSIS_time_frac_from_mouse_x(int mouse_x, SDL_Rect spec_rect) {
    /*
        Purpose: Converts a spectrogram mouse X coordinate into a normalized time fraction
        Returns: Time fraction
    */

    if (spec_rect.w <= 0) {

        return 0.0;

    }

    double frac = (double)(mouse_x - spec_rect.x) / (double)spec_rect.w;

    return ANALYSIS_limit_double(frac, 0.0, 1.0);
}

static void ANALYSIS_update_column_selection_from_mouse(int mouse_x, SDL_Rect spec_rect) {
    /*
        Purpose: Updates the analysis time-column selector from mouse movement
        Returns: No value
    */

    Global_Analysis_Column_X1 = ANALYSIS_time_frac_from_mouse_x(mouse_x, spec_rect);
}

static void ANALYSIS_apply_column_selection(void) {
    /*
        Purpose: Applies the analysis time-column selector and requests a replot
        Returns: No value
    */

    if (Global_Analysis_IQ_Count == 0 || Global_Analysis_Path[0] == '\0' || Global_Analysis_View_Len == 0) {

        Global_Analysis_Column_Selecting = 0;
        Global_Analysis_Column_Visible = 0;
        Global_Analysis_Column_Active = 0;
        return;

    }

    double x0 = Global_Analysis_Column_X0;
    double x1 = Global_Analysis_Column_X1;

    if (x1 < x0) {

        double tmp = x0;
        x0 = x1;
        x1 = tmp;

    }

    if (fabs(x1 - x0) < 0.002) {

        double mid = (x0 + x1) * 0.5;
        x0 = mid - 0.001;
        x1 = mid + 0.001;

    }

    Global_Analysis_Column_X0 = ANALYSIS_limit_double(x0, 0.0, 1.0);
    Global_Analysis_Column_X1 = ANALYSIS_limit_double(x1, 0.0, 1.0);
    Global_Analysis_Column_Selecting = 0;
    Global_Analysis_Column_Visible = 1;
    Global_Analysis_Column_Active = 1;
    ANALYSIS_invalidate_constellation_cache();
    Global_Analysis_Dirty = 1;

    double start_sec = Global_Analysis_Sample_Rate > 0.0
                           ? (double)(Global_Analysis_View_Start +
                                      (size_t)(Global_Analysis_Column_X0 * (double)Global_Analysis_View_Len)) /
                                 Global_Analysis_Sample_Rate
                           : 0.0;
    double end_sec = Global_Analysis_Sample_Rate > 0.0
                         ? (double)(Global_Analysis_View_Start +
                                    (size_t)(Global_Analysis_Column_X1 * (double)Global_Analysis_View_Len)) /
                               Global_Analysis_Sample_Rate
                         : 0.0;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Time filter %.6f sec to %.6f sec | Backspace clears", start_sec, end_sec);
}

static SDL_Rect ANALYSIS_crop_button_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the compact Crop button rectangle on the left
        Returns: Computed rectangle
    */

    SDL_Rect list_rect;
    SDL_Rect spec_rect;
    const int horizontal_margin = 4;
    const int crop_width = 64; /* Tight around "Crop" while retaining padding. */
    const int button_height = 24;

    ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
    (void)list_rect;

    return (SDL_Rect){spec_rect.x + horizontal_margin, spec_rect.y - 30, crop_width, button_height};
}

static SDL_Rect ANALYSIS_clear_workspace_button_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the padded Clear Workspace button rectangle on the right
        Returns: Computed rectangle
    */

    SDL_Rect list_rect;
    SDL_Rect spec_rect;
    SDL_Rect crop_rect = ANALYSIS_crop_button_rect(win_w, win_h);
    const int horizontal_margin = 4;
    const int clear_workspace_width = 156; /* Extra text padding on both sides. */
    SDL_Rect rect;

    ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
    (void)list_rect;

    rect = (SDL_Rect){spec_rect.x + spec_rect.w - clear_workspace_width - horizontal_margin, crop_rect.y,
                      clear_workspace_width, crop_rect.h};

    if (rect.x < spec_rect.x + horizontal_margin) {

        rect.x = spec_rect.x + horizontal_margin;

    }

    return rect;
}

static void ANALYSIS_draw_crop_button(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the crop button
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Mode) {

        return;

    }

    SDL_Rect rect = ANALYSIS_crop_button_rect(win_w, win_h);

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int hover = point_in_rect(mouse_x, mouse_y, rect);
    int enabled = Global_Analysis_Path[0] != '\0' && Global_Analysis_IQ_Count > 0;

    SDL_Color fill =
        enabled ? (hover ? (SDL_Color){38, 38, 38, 245} : (SDL_Color){12, 12, 12, 235}) : (SDL_Color){6, 6, 6, 210};
    SDL_Color border = enabled ? (hover ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){170, 170, 170, 240})
                               : (SDL_Color){80, 80, 80, 220};
    SDL_Color text = enabled ? (SDL_Color){245, 245, 245, 255} : (SDL_Color){110, 110, 110, 255};

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);
    ANALYSIS_draw_centered_button_text(renderer, font, rect, "Crop", text);
}

static void ANALYSIS_draw_clear_workspace_button(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the green Clear Workspace button
        Returns: No value
    */

    SDL_Rect rect;
    int mouse_x = 0;
    int mouse_y = 0;
    int hover;
    int enabled;
    SDL_Color fill;
    SDL_Color border;
    SDL_Color text;

    if (!renderer || !font || !Global_Analysis_Mode) {

        return;

    }

    rect = ANALYSIS_clear_workspace_button_rect(win_w, win_h);
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);
    hover = point_in_rect(mouse_x, mouse_y, rect);
    enabled = Global_Analysis_Path[0] != '\0' && Global_Analysis_IQ_Count > 0;

    fill = enabled ? (hover ? (SDL_Color){0, 118, 42, 245} : (SDL_Color){0, 82, 30, 238}) : (SDL_Color){0, 30, 12, 210};
    border =
        enabled ? (hover ? (SDL_Color){70, 255, 130, 255} : (SDL_Color){0, 205, 82, 245}) : (SDL_Color){0, 78, 32, 220};
    text = enabled ? (SDL_Color){240, 255, 245, 255} : (SDL_Color){90, 140, 105, 255};

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);
    ANALYSIS_draw_centered_button_text(renderer, font, rect, "Clear Workspace", text);
}

static int ANALYSIS_noise_graph_from_point(int x, int y, int win_w, int win_h, SDL_Rect *graph_rect) {
    /*
        Purpose: Gets the noise graph from the point
        Returns: Selected type
    */

    SDL_Rect psd_rect;
    SDL_Rect mag_rect;
    SDL_Rect phase_rect;
    SDL_Rect inst_rect;
    SDL_Rect const_rect;
    SDL_Rect spec_rect;

    ANALYSIS_get_hover_graph_layout(win_w, win_h, &psd_rect, &mag_rect, &phase_rect, &inst_rect, &const_rect,
                                    &spec_rect);

    (void)psd_rect;
    (void)phase_rect;
    (void)const_rect;
    (void)spec_rect;

    if (point_in_rect(x, y, mag_rect)) {

        if (graph_rect) {

            *graph_rect = mag_rect;

        }
        return ANALYSIS_NOISE_GRAPH_MAG;

    }

    if (point_in_rect(x, y, inst_rect)) {

        if (graph_rect) {

            *graph_rect = inst_rect;

        }
        return ANALYSIS_NOISE_GRAPH_INST;

    }

    return ANALYSIS_NOISE_GRAPH_NONE;
}

static double ANALYSIS_noise_frac_from_mouse_y(int mouse_y, SDL_Rect graph_rect) {
    /*
        Purpose: Gets the noise frac from the mouse y
        Returns: Computed value
    */

    if (graph_rect.h <= 0) {

        return 0.0;

    }

    double frac = (double)(mouse_y - graph_rect.y) / (double)graph_rect.h;

    return ANALYSIS_limit_double(frac, 0.0, 1.0);
}

static void ANALYSIS_update_noise_selection_from_mouse(int mouse_y, SDL_Rect graph_rect) {
    /*
        Purpose: Updates the noise selection from mouse
        Returns: No value
    */

    Global_Analysis_Noise_Y1 = ANALYSIS_noise_frac_from_mouse_y(mouse_y, graph_rect);
}

static void ANALYSIS_apply_noise_selection(void) {
    /*
        Purpose: Applies the noise selection
        Returns: No value
    */

    if (Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_NONE) {

        Global_Analysis_Noise_Selecting = 0;
        Global_Analysis_Noise_Visible = 0;
        Global_Analysis_Noise_Active = 0;
        return;

    }

    double y0 = Global_Analysis_Noise_Y0;
    double y1 = Global_Analysis_Noise_Y1;

    if (y1 < y0) {

        double tmp = y0;
        y0 = y1;
        y1 = tmp;

    }

    if (fabs(y1 - y0) < 0.012) {

        double mid = (y0 + y1) * 0.5;
        y0 = mid - 0.006;
        y1 = mid + 0.006;

    }

    Global_Analysis_Noise_Y0 = ANALYSIS_limit_double(y0, 0.0, 1.0);
    Global_Analysis_Noise_Y1 = ANALYSIS_limit_double(y1, 0.0, 1.0);
    Global_Analysis_Noise_Selecting = 0;
    Global_Analysis_Noise_Visible = 1;
    Global_Analysis_Noise_Active = 1;
    Global_Analysis_Dirty = 1;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "%s noise mask applied | Crop will zero matching IQ samples | "
             "Backspace clears",
             Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_MAG ? "Magnitude" : "Instantaneous frequency");
}

static void ANALYSIS_clear_noise_filter(void) {
    /*
        Purpose: Clears the noise filter
        Returns: No value
    */

    Global_Analysis_Noise_Visible = 0;
    Global_Analysis_Noise_Selecting = 0;
    Global_Analysis_Noise_Active = 0;
    Global_Analysis_Noise_Graph = ANALYSIS_NOISE_GRAPH_NONE;
    Global_Analysis_Noise_Y0 = 0.0;
    Global_Analysis_Noise_Y1 = 0.0;
    memset(Global_Analysis_Noise_Column_Mask, 0, sizeof(Global_Analysis_Noise_Column_Mask));
}

static void ANALYSIS_noise_value_range(double *out_min, double *out_max) {
    /*
        Purpose: Gets the noise value range
        Returns: No value
    */

    double y0 = Global_Analysis_Noise_Y0;
    double y1 = Global_Analysis_Noise_Y1;

    if (y1 < y0) {

        double tmp = y0;
        y0 = y1;
        y1 = tmp;

    }

    y0 = ANALYSIS_limit_double(y0, 0.0, 1.0);
    y1 = ANALYSIS_limit_double(y1, 0.0, 1.0);

    double v0 = 0.0;
    double v1 = 0.0;

    if (Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_MAG) {

        double h = 1000.0;
        v0 = (h - 12.0 - (y0 * h)) / (h - 24.0);
        v1 = (h - 12.0 - (y1 * h)) / (h - 24.0);
        v0 = ANALYSIS_limit_double(v0, 0.0, 1.0);
        v1 = ANALYSIS_limit_double(v1, 0.0, 1.0);

    }

    else {

        v0 = (0.5 - y0) / 0.42;
        v1 = (0.5 - y1) / 0.42;
        v0 = ANALYSIS_limit_double(v0, -1.0, 1.0);
        v1 = ANALYSIS_limit_double(v1, -1.0, 1.0);

    }

    if (v1 < v0) {

        double tmp = v0;
        v0 = v1;
        v1 = tmp;

    }

    if (out_min) {

        *out_min = v0;

    }

    if (out_max) {

        *out_max = v1;

    }
}

static int ANALYSIS_noise_value_matches(float value) {
    /*
        Purpose: Checks whether the noise value matches the requested data
        Returns: Boolean status
    */

    if (!Global_Analysis_Noise_Active || Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_NONE) {

        return 0;

    }

    double v_min = 0.0;
    double v_max = 0.0;

    ANALYSIS_noise_value_range(&v_min, &v_max);

    return (double)value >= v_min && (double)value <= v_max;
}

static void ANALYSIS_update_noise_column_mask(int render_w) {
    /*
        Purpose: Updates the noise column mask
        Returns: No value
    */

    memset(Global_Analysis_Noise_Column_Mask, 0, sizeof(Global_Analysis_Noise_Column_Mask));

    if (!Global_Analysis_Noise_Active || Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_NONE || render_w <= 0) {

        return;

    }

    if (render_w > ANALYSIS_MAX_RENDER_W) {

        render_w = ANALYSIS_MAX_RENDER_W;

    }

    for (int x = 0; x < render_w; x++) {
        float v = Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_MAG ? Global_Analysis_Mag_Line[x]
                                                                          : Global_Analysis_InstFreq_Line[x];

        if (ANALYSIS_noise_value_matches(v)) {

            Global_Analysis_Noise_Column_Mask[x] = 1;

        }
    }
}

static void ANALYSIS_apply_noise_filter_to_rendered_lines(int render_w) {
    /*
        Purpose: Applies the noise filter to rendered lines
        Returns: No value
    */

    /*
     * The graph noise selection is a crop-only mask. Do not alter the live
     * magnitude or instantaneous-frequency graph arrays here; only refresh the
     * per-column mask that crop/export uses to zero matching IQ samples.
     */
    ANALYSIS_update_noise_column_mask(render_w);
}

static void ANALYSIS_draw_noise_filter_overlay(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the noise filter overlay
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Mode) {

        return;

    }

    if (!(Global_Analysis_Noise_Visible || Global_Analysis_Noise_Selecting)) {

        return;

    }

    if (Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_NONE) {

        return;

    }

    SDL_Rect psd_rect;
    SDL_Rect mag_rect;
    SDL_Rect phase_rect;
    SDL_Rect inst_rect;
    SDL_Rect const_rect;
    SDL_Rect spec_rect;

    ANALYSIS_get_hover_graph_layout(win_w, win_h, &psd_rect, &mag_rect, &phase_rect, &inst_rect, &const_rect,
                                    &spec_rect);

    (void)psd_rect;
    (void)phase_rect;
    (void)const_rect;
    (void)spec_rect;

    SDL_Rect graph_rect = Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_MAG ? mag_rect : inst_rect;

    double y0 = Global_Analysis_Noise_Y0;
    double y1 = Global_Analysis_Noise_Y1;

    if (y1 < y0) {

        double tmp = y0;
        y0 = y1;
        y1 = tmp;

    }

    y0 = ANALYSIS_limit_double(y0, 0.0, 1.0);
    y1 = ANALYSIS_limit_double(y1, 0.0, 1.0);

    int select_y0 = graph_rect.y + (int)(y0 * (double)graph_rect.h);
    int select_y1 = graph_rect.y + (int)(y1 * (double)graph_rect.h);

    if (select_y1 <= select_y0) {

        select_y1 = select_y0 + 1;

    }

    if (select_y1 - select_y0 < 4) {

        int mid = (select_y0 + select_y1) / 2;
        select_y0 = mid - 2;
        select_y1 = mid + 2;

    }

    if (select_y0 < graph_rect.y) {

        select_y0 = graph_rect.y;

    }

    if (select_y1 > graph_rect.y + graph_rect.h) {

        select_y1 = graph_rect.y + graph_rect.h;

    }

    if (select_y1 <= select_y0) {

        select_y1 = select_y0 + 1;

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect noise_rect = {graph_rect.x, select_y0, graph_rect.w, select_y1 - select_y0};

    draw_filled_rect(renderer, noise_rect, (SDL_Color){255, 255, 255, 44});
    draw_outline_rect(renderer, noise_rect, (SDL_Color){255, 255, 255, 235});

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 235);
    SDL_RenderDrawLine(renderer, graph_rect.x, select_y0, graph_rect.x + graph_rect.w, select_y0);
    SDL_RenderDrawLine(renderer, graph_rect.x, select_y1, graph_rect.x + graph_rect.w, select_y1);

    const char *label =
        Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_MAG ? "Magnitude noise mask" : "Inst. freq noise mask";

    SDL_Rect label_bg = {graph_rect.x + graph_rect.w - 210, graph_rect.y + 6, 204, 22};

    if (label_bg.x < graph_rect.x + 6) {

        label_bg.x = graph_rect.x + 6;

    }

    draw_filled_rect(renderer, label_bg, (SDL_Color){0, 0, 0, 190});
    draw_outline_rect(renderer, label_bg, (SDL_Color){255, 255, 255, 200});
    draw_text(renderer, font, label, label_bg.x + 7, label_bg.y + 4, (SDL_Color){245, 245, 245, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

int ANALYSIS_export_classification_fields(char *file_name, size_t file_name_size, double *frequency_mhz,
                                          double *bandwidth_khz, double *start_time, double *end_time) {
    /*
        Purpose: Exports the currently loaded analysis selection as classification fields
        Returns: Export status
    */

    if (!file_name || file_name_size == 0 || !frequency_mhz || !bandwidth_khz || !start_time || !end_time) {

        return 0;

    }

    file_name[0] = '\0';
    *frequency_mhz = 0.0;
    *bandwidth_khz = 0.0;
    *start_time = 0.0;
    *end_time = 0.0;

    if (Global_Analysis_IQ_Count == 0 || Global_Analysis_View_Len == 0 || Global_Analysis_Path[0] == '\0' ||
        Global_Analysis_Sample_Rate <= 0.0) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                 "Open a recording before exporting to classification");
        return 0;

    }

    const char *name = Global_Analysis_Path;

    for (const char *p = Global_Analysis_Path; *p; p++) {

        if (*p == '/' || *p == '\\') {

            name = p + 1;

        }
    }

    if (Global_Analysis_Loaded_Index >= 0 && Global_Analysis_Loaded_Index < Global_Analysis_File_Count &&
        Global_Analysis_Files[Global_Analysis_Loaded_Index][0] != '\0') {

        name = Global_Analysis_Files[Global_Analysis_Loaded_Index];

    }

    snprintf(file_name, file_name_size, "%s", name);

    double center_hz = Global_Analysis_Center_Hz;
    double bw_hz = Global_Analysis_Sample_Rate;

    if (Global_Analysis_Filter_Active || Global_Analysis_Filter_Visible) {

        double y0 = Global_Analysis_Filter_Y0;
        double y1 = Global_Analysis_Filter_Y1;

        if (y1 < y0) {

            double tmp = y0;
            y0 = y1;
            y1 = tmp;

        }

        y0 = ANALYSIS_limit_double(y0, 0.0, 1.0);
        y1 = ANALYSIS_limit_double(y1, 0.0, 1.0);

        double center_y = (y0 + y1) * 0.5;
        center_hz = ANALYSIS_frequency_from_spec_frac(center_y);
        bw_hz = fabs(y1 - y0) * Global_Analysis_Sample_Rate;

    }

    double x0 = 0.0;
    double x1 = 1.0;

    if (Global_Analysis_Column_Active || Global_Analysis_Column_Visible) {

        x0 = Global_Analysis_Column_X0;
        x1 = Global_Analysis_Column_X1;

        if (x1 < x0) {

            double tmp = x0;
            x0 = x1;
            x1 = tmp;

        }

        x0 = ANALYSIS_limit_double(x0, 0.0, 1.0);
        x1 = ANALYSIS_limit_double(x1, 0.0, 1.0);

    }

    size_t start_sample = Global_Analysis_View_Start + (size_t)(x0 * (double)Global_Analysis_View_Len);
    size_t end_sample = Global_Analysis_View_Start + (size_t)(x1 * (double)Global_Analysis_View_Len);

    if (start_sample > Global_Analysis_IQ_Count) {

        start_sample = Global_Analysis_IQ_Count;

    }

    if (end_sample > Global_Analysis_IQ_Count) {

        end_sample = Global_Analysis_IQ_Count;

    }

    if (end_sample < start_sample) {

        size_t tmp = start_sample;
        start_sample = end_sample;
        end_sample = tmp;

    }

    *frequency_mhz = center_hz / 1e6;
    *bandwidth_khz = bw_hz / 1e3;
    *start_time = (double)start_sample / Global_Analysis_Sample_Rate;
    *end_time = (double)end_sample / Global_Analysis_Sample_Rate;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Exported selection to classification fields");

    return 1;
}

static int ANALYSIS_signal_menu_available(void) {
    /*
        Purpose: Checks whether the signal menu is available
        Returns: Boolean status
    */

    return Global_Analysis_File_Count > 0 && Global_Analysis_Selected >= 0 &&
           Global_Analysis_Selected < Global_Analysis_File_Count;
}

static const char *ANALYSIS_selected_file_name(void) {
    /*
        Purpose: Gets the selected file name
        Returns: Text pointer
    */

    if (!ANALYSIS_signal_menu_available()) {

        return "";

    }

    return Global_Analysis_Files[Global_Analysis_Selected];
}

static void ANALYSIS_short_text(TTF_Font *font, const char *src, char *dst, size_t dst_size, int max_px) {
    /*
        Purpose: Shortens text for display
        Returns: No value
    */

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src) {

        src = "";

    }

    size_t copy_len = strlen(src);

    if (copy_len >= dst_size) {

        copy_len = dst_size - 1;

    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    if (!font || max_px <= 0) {

        return;

    }

    int text_w = 0;
    int text_h = 0;

    if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) {

        return;

    }

    size_t len = strlen(dst);

    while (len > 4) {
        len--;
        dst[len] = '\0';

        if (len >= 3) {

            dst[len - 3] = '.';
            dst[len - 2] = '.';
            dst[len - 1] = '.';
            dst[len] = '\0';

        }

        if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) {

            return;

        }

        if (len >= 3) {

            dst[len - 3] = '\0';

        }
    }

    snprintf(dst, dst_size, "...");
}

static int ANALYSIS_file_search_matches(const char *name) {
    /*
        Purpose: Checks whether the file search matches the requested data
        Returns: Boolean status
    */

    char hay[512];
    char needle[ANALYSIS_FILE_SEARCH_TEXT_MAX];
    size_t i;

    if (!name) {

        name = "";

    }

    if (Global_Analysis_File_Search_Text[0] == '\0') {

        return 1;

    }

    for (i = 0; i + 1 < sizeof(hay) && name[i]; i++) {
        hay[i] = (char)tolower((unsigned char)name[i]);
    }

    hay[i] = '\0';

    for (i = 0; i + 1 < sizeof(needle) && Global_Analysis_File_Search_Text[i]; i++) {
        needle[i] = (char)tolower((unsigned char)Global_Analysis_File_Search_Text[i]);
    }

    needle[i] = '\0';

    return strstr(hay, needle) != NULL;
}

static int ANALYSIS_file_search_filtered_count(void) {
    /*
        Purpose: Counts filtered file search results
        Returns: Item count
    */

    int count = 0;

    for (int i = 0; i < Global_Analysis_File_Count; i++) {

        if (ANALYSIS_file_search_matches(Global_Analysis_Files[i])) {

            count++;

        }
    }

    return count;
}

static int ANALYSIS_file_search_filtered_index_at(int filtered_index) {
    /*
        Purpose: Gets the file search filtered index at a position
        Returns: Item index
    */

    int seen = 0;

    if (filtered_index < 0) {

        return -1;

    }

    for (int i = 0; i < Global_Analysis_File_Count; i++) {

        if (!ANALYSIS_file_search_matches(Global_Analysis_Files[i])) {

            continue;

        }

        if (seen == filtered_index) {

            return i;

        }
        seen++;
    }

    return -1;
}

static SDL_Rect ANALYSIS_file_search_popup_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the file search popup rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {(win_w - 1050) / 2, (win_h - 740) / 2, 1050, 740};

    if (r.x < MARGIN) {

        r.x = MARGIN;

    }

    if (r.y < MARGIN) {

        r.y = MARGIN;

    }

    if (r.w > win_w - 2 * MARGIN) {

        r.w = win_w - 2 * MARGIN;

    }

    if (r.h > win_h - 2 * MARGIN) {

        r.h = win_h - 2 * MARGIN;

    }

    if (r.w < 320) {

        r.w = 320;

    }

    if (r.h < 260) {

        r.h = 260;

    }
    return r;
}

static SDL_Rect ANALYSIS_file_search_input_rect(SDL_Rect popup) {
    /*
        Purpose: Computes the file search input rectangle
        Returns: Computed rectangle
    */

    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = {close_btn.x - 292, popup.y + 14, 276, 30};

    if (search.x < popup.x + 180) {

        search.x = popup.x + 180;
        search.w = close_btn.x - search.x - 16;

    }

    if (search.w < 120) {

        search.w = 120;

    }
    return search;
}

static SDL_Rect ANALYSIS_file_search_button_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the file search button rectangle
        Returns: Computed rectangle
    */

    SDL_Rect list_rect;
    SDL_Rect spec_rect;
    (void)spec_rect;

    ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);

    SDL_Rect button = {list_rect.x + list_rect.w - 178, list_rect.y + 8, 166, 28};

    if (button.x < list_rect.x + 12) {

        button.x = list_rect.x + 12;

    }

    if (button.w > list_rect.w - 24) {

        button.w = list_rect.w - 24;

    }
    return button;
}

static void ANALYSIS_file_search_clamp_scroll(void) {
    /*
        Purpose: Clamps the file search scroll
        Returns: No value
    */

    int filtered_count = ANALYSIS_file_search_filtered_count();
    int visible = 14;
    int max_scroll = filtered_count - visible;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Analysis_File_Search_Scroll < 0) {

        Global_Analysis_File_Search_Scroll = 0;

    }

    if (Global_Analysis_File_Search_Scroll > max_scroll) {

        Global_Analysis_File_Search_Scroll = max_scroll;

    }
}

static void ANALYSIS_open_file_search_menu(void) {
    /*
        Purpose: Opens the file search menu
        Returns: No value
    */

    if (Global_Analysis_File_Count <= 0) {

        ANALYSIS_scan_recordings();

    }

    Global_Analysis_File_Search_Open = 1;
    Global_Analysis_File_Search_Active = 1;
    Global_Analysis_File_Search_Hover = -1;
    Global_Analysis_File_Search_Text[0] = '\0';
    Global_Analysis_File_Search_Cursor = 0;
    Global_Analysis_File_Search_Scroll = 0;
    Global_Analysis_Signal_Menu_Open = 0;
    Global_Analysis_Dragging = 0;
    Global_Analysis_Filter_Selecting = 0;
    Global_Analysis_Column_Selecting = 0;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Filename search menu opened");
}

static void ANALYSIS_close_file_search_menu(void) {
    /*
        Purpose: Closes the file search menu
        Returns: No value
    */

    Global_Analysis_File_Search_Open = 0;
    Global_Analysis_File_Search_Active = 0;
    Global_Analysis_File_Search_Hover = -1;
}

static void ANALYSIS_file_search_select_index(int index, int open_after_select) {
    /*
        Purpose: Selects the file search index
        Returns: No value
    */

    if (index < 0 || index >= Global_Analysis_File_Count) {

        return;

    }

    Global_Analysis_Selected = index;
    Global_Analysis_List_Scroll = Global_Analysis_Selected - 2;

    if (Global_Analysis_List_Scroll < 0) {

        Global_Analysis_List_Scroll = 0;

    }

    ANALYSIS_close_file_search_menu();

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Selected %.180s | Press Enter to open",
             Global_Analysis_Files[Global_Analysis_Selected]);

    if (open_after_select) {

        ANALYSIS_open_selected_recording();

    }
}

static void ANALYSIS_file_search_insert_text(const char *text) {
    /*
        Purpose: Inserts the file search text
        Returns: No value
    */

    if (!text || text[0] == '\0') {

        return;

    }

    int len = (int)strlen(Global_Analysis_File_Search_Text);
    int add = (int)strlen(text);

    if (Global_Analysis_File_Search_Cursor < 0) {

        Global_Analysis_File_Search_Cursor = 0;

    }

    if (Global_Analysis_File_Search_Cursor > len) {

        Global_Analysis_File_Search_Cursor = len;

    }

    if (len + add >= ANALYSIS_FILE_SEARCH_TEXT_MAX) {

        add = ANALYSIS_FILE_SEARCH_TEXT_MAX - len - 1;

    }

    if (add <= 0) {

        return;

    }

    memmove(Global_Analysis_File_Search_Text + Global_Analysis_File_Search_Cursor + add,
            Global_Analysis_File_Search_Text + Global_Analysis_File_Search_Cursor,
            (size_t)(len - Global_Analysis_File_Search_Cursor + 1));

    memcpy(Global_Analysis_File_Search_Text + Global_Analysis_File_Search_Cursor, text, (size_t)add);

    Global_Analysis_File_Search_Cursor += add;
    Global_Analysis_File_Search_Scroll = 0;
}

static void ANALYSIS_file_search_backspace(void) {
    /*
        Purpose: Removes the previous character from the file search
        Returns: No value
    */

    int len = (int)strlen(Global_Analysis_File_Search_Text);

    if (Global_Analysis_File_Search_Cursor <= 0 || len <= 0) {

        return;

    }

    if (Global_Analysis_File_Search_Cursor > len) {

        Global_Analysis_File_Search_Cursor = len;

    }

    memmove(Global_Analysis_File_Search_Text + Global_Analysis_File_Search_Cursor - 1,
            Global_Analysis_File_Search_Text + Global_Analysis_File_Search_Cursor,
            (size_t)(len - Global_Analysis_File_Search_Cursor + 1));

    Global_Analysis_File_Search_Cursor--;
    Global_Analysis_File_Search_Scroll = 0;
}

static void ANALYSIS_file_search_delete(void) {
    /*
        Purpose: Deletes the file search
        Returns: No value
    */

    int len = (int)strlen(Global_Analysis_File_Search_Text);

    if (Global_Analysis_File_Search_Cursor < 0) {

        Global_Analysis_File_Search_Cursor = 0;

    }

    if (Global_Analysis_File_Search_Cursor >= len) {

        return;

    }

    memmove(Global_Analysis_File_Search_Text + Global_Analysis_File_Search_Cursor,
            Global_Analysis_File_Search_Text + Global_Analysis_File_Search_Cursor + 1,
            (size_t)(len - Global_Analysis_File_Search_Cursor));

    Global_Analysis_File_Search_Scroll = 0;
}

static void ANALYSIS_draw_modal_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label,
                                       int hovered) {
    /*
        Purpose: Draws the modal button
        Returns: No value
    */

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

    int tw = 0;
    int th = 0;

    if (font && label && TTF_SizeText(font, label, &tw, &th) != 0) {

        tw = 0;
        th = 0;

    }

    draw_text(renderer, font, label, rect.x + (rect.w - tw) / 2, rect.y + (rect.h - th) / 2, text);
}

static int ANALYSIS_handle_file_search_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the file search event
        Returns: Handling status
    */

    if (!event || !Global_Analysis_File_Search_Open) {

        return 0;

    }

    SDL_Rect popup = ANALYSIS_file_search_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = ANALYSIS_file_search_input_rect(popup);
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};

    if (event->type == SDL_TEXTINPUT) {

        if (Global_Analysis_File_Search_Active) {

            ANALYSIS_file_search_insert_text(event->text.text);

        }
        return 1;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;
        int len = (int)strlen(Global_Analysis_File_Search_Text);

        if (key == SDLK_ESCAPE) {

            ANALYSIS_close_file_search_menu();
            return 1;

        }

        if (key == SDLK_BACKSPACE) {

            ANALYSIS_file_search_backspace();
            return 1;

        }

        if (key == SDLK_DELETE) {

            ANALYSIS_file_search_delete();
            return 1;

        }

        if (key == SDLK_LEFT) {

            if (Global_Analysis_File_Search_Cursor > 0) {

                Global_Analysis_File_Search_Cursor--;

            }
            return 1;

        }

        if (key == SDLK_RIGHT) {

            if (Global_Analysis_File_Search_Cursor < len) {

                Global_Analysis_File_Search_Cursor++;

            }
            return 1;

        }

        if (key == SDLK_HOME) {

            Global_Analysis_File_Search_Cursor = 0;
            return 1;

        }

        if (key == SDLK_END) {

            Global_Analysis_File_Search_Cursor = len;
            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            int index = ANALYSIS_file_search_filtered_index_at(Global_Analysis_File_Search_Scroll);

            if (index >= 0) {

                ANALYSIS_file_search_select_index(index, 1);

            }
            return 1;

        }

        if (key == SDLK_DOWN) {

            Global_Analysis_File_Search_Scroll++;
            ANALYSIS_file_search_clamp_scroll();
            return 1;

        }

        if (key == SDLK_UP) {

            Global_Analysis_File_Search_Scroll--;
            ANALYSIS_file_search_clamp_scroll();
            return 1;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mx = 0;
        int my = 0;
        ANALYSIS_get_adjusted_mouse_state(&mx, &my);

        if (point_in_rect(mx, my, list)) {

            Global_Analysis_File_Search_Scroll -= event->wheel.y * 3;
            ANALYSIS_file_search_clamp_scroll();

        }

        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        int mx = event->button.x;
        int my = event->button.y;

        if (!point_in_rect(mx, my, popup) || point_in_rect(mx, my, close_btn)) {

            ANALYSIS_close_file_search_menu();
            return 1;

        }

        if (point_in_rect(mx, my, search)) {

            Global_Analysis_File_Search_Active = 1;
            return 1;

        }

        Global_Analysis_File_Search_Active = 0;

        if (point_in_rect(mx, my, list)) {

            int row = (my - list.y - 4) / ANALYSIS_FILE_SEARCH_ROW_H;
            int visible = list.h / ANALYSIS_FILE_SEARCH_ROW_H;

            if (visible < 1) {

                visible = 1;

            }

            if (visible > 14) {

                visible = 14;

            }

            if (row >= 0 && row < visible) {

                int filtered_index = Global_Analysis_File_Search_Scroll + row;
                int index = ANALYSIS_file_search_filtered_index_at(filtered_index);

                if (index >= 0) {

                    ANALYSIS_file_search_select_index(index, event->button.clicks >= 2);

                }

            }

            return 1;

        }

        return 1;

    }

    return 1;
}

static void ANALYSIS_draw_file_search_button(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the file search button
        Returns: No value
    */

    if (!renderer || !font) {

        return;

    }

    int mx = 0;
    int my = 0;
    ANALYSIS_get_adjusted_mouse_state(&mx, &my);

    SDL_Rect button = ANALYSIS_file_search_button_rect(win_w, win_h);

    ANALYSIS_draw_modal_button(renderer, font, button, "Open Search Menu", point_in_rect(mx, my, button));
}

static void ANALYSIS_draw_file_search_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the file search popup
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_File_Search_Open) {

        return;

    }

    SDL_Rect popup = ANALYSIS_file_search_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = ANALYSIS_file_search_input_rect(popup);
    SDL_Rect current_rect = {popup.x + 18, popup.y + 62, popup.w - 36, 42};
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};
    int mx = 0;
    int my = 0;
    int filtered_count = ANALYSIS_file_search_filtered_count();

    ANALYSIS_get_adjusted_mouse_state(&mx, &my);
    ANALYSIS_file_search_clamp_scroll();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 155});

    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, (SDL_Color){0, 255, 90, 255});
    SDL_Rect inner = {popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8};
    draw_outline_rect(renderer, inner, (SDL_Color){0, 150, 60, 255});

    draw_text(renderer, font, "FILENAME SEARCH", popup.x + 18, popup.y + 20, (SDL_Color){0, 255, 90, 255});

    ANALYSIS_draw_modal_button(renderer, font, close_btn, "Close", point_in_rect(mx, my, close_btn));

    draw_filled_rect(renderer, search,
                     Global_Analysis_File_Search_Active ? (SDL_Color){0, 20, 8, 255} : (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, search,
                      Global_Analysis_File_Search_Active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 150, 60, 255});

    if (Global_Analysis_File_Search_Text[0]) {

        draw_text(renderer, font, Global_Analysis_File_Search_Text, search.x + 10, search.y + 8,
                  (SDL_Color){0, 255, 90, 255});

    }

    else {

        draw_text(renderer, font, "Search file", search.x + 10, search.y + 8, (SDL_Color){0, 155, 65, 255});

    }

    if (Global_Analysis_File_Search_Active && ((SDL_GetTicks64() / 450ULL) % 2ULL) == 0ULL) {

        int tw = 0;
        int th = 0;
        char prefix[ANALYSIS_FILE_SEARCH_TEXT_MAX];
        int cursor = Global_Analysis_File_Search_Cursor;
        int len = (int)strlen(Global_Analysis_File_Search_Text);

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > len) {

            cursor = len;

        }
        snprintf(prefix, sizeof(prefix), "%.*s", cursor, Global_Analysis_File_Search_Text);

        if (font && TTF_SizeText(font, prefix, &tw, &th) != 0) {

            tw = cursor * 8;

        }

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
        SDL_RenderDrawLine(renderer, search.x + 10 + tw, search.y + 6, search.x + 10 + tw, search.y + search.h - 6);
        SDL_RenderDrawLine(renderer, search.x + 11 + tw, search.y + 6, search.x + 11 + tw, search.y + search.h - 6);

    }

    draw_text(renderer, font, "Currently selected", current_rect.x, current_rect.y - 18, (SDL_Color){0, 155, 65, 255});
    draw_filled_rect(renderer, current_rect, (SDL_Color){0, 20, 8, 255});
    draw_outline_rect(renderer, current_rect, (SDL_Color){0, 255, 90, 255});

    {
        char short_name[512];
        const char *current = ANALYSIS_selected_file_name();

        if (!current || current[0] == '\0') {

            current = "(none selected)";

        }

        ANALYSIS_short_text(font, current, short_name, sizeof(short_name), current_rect.w - 20);

        draw_text(renderer, font, short_name, current_rect.x + 10, current_rect.y + 12,
                  current[0] == '(' ? (SDL_Color){0, 155, 65, 255} : (SDL_Color){0, 255, 90, 255});
    }

    draw_filled_rect(renderer, list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, list, (SDL_Color){0, 150, 60, 255});

    if (Global_Analysis_File_Count <= 0) {

        char empty_msg[640];
        snprintf(empty_msg, sizeof(empty_msg), "No .complex16 files found in %s/", Global_Analysis_Record_Dir);
        draw_text(renderer, font, empty_msg, list.x + 12, list.y + 14, (SDL_Color){255, 180, 40, 255});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    if (filtered_count <= 0) {

        draw_text(renderer, font, "No files match the search.", list.x + 12, list.y + 14,
                  (SDL_Color){255, 180, 40, 255});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    int visible = list.h / ANALYSIS_FILE_SEARCH_ROW_H;

    if (visible > 14) {

        visible = 14;

    }

    if (visible < 1) {

        visible = 1;

    }

    Global_Analysis_File_Search_Hover = -1;

    if (point_in_rect(mx, my, list)) {

        int row = (my - list.y - 4) / ANALYSIS_FILE_SEARCH_ROW_H;
        int filtered_index = Global_Analysis_File_Search_Scroll + row;
        int index = ANALYSIS_file_search_filtered_index_at(filtered_index);

        if (row >= 0 && row < visible && index >= 0 && index < Global_Analysis_File_Count) {

            Global_Analysis_File_Search_Hover = index;

        }

    }

    for (int row = 0; row < visible; row++) {
        int filtered_index = Global_Analysis_File_Search_Scroll + row;
        int index = ANALYSIS_file_search_filtered_index_at(filtered_index);
        SDL_Rect item = {list.x + 4, list.y + 4 + row * ANALYSIS_FILE_SEARCH_ROW_H, list.w - 8,
                         ANALYSIS_FILE_SEARCH_ROW_H - 3};

        if (index < 0 || index >= Global_Analysis_File_Count) {

            break;

        }

        int hovered = index == Global_Analysis_File_Search_Hover;
        int selected = index == Global_Analysis_Selected;
        char short_name[512];

        if (hovered) {

            draw_filled_rect(renderer, item, (SDL_Color){0, 44, 16, 255});
            SDL_Rect halo = {item.x - 2, item.y - 2, item.w + 4, item.h + 4};
            draw_outline_rect(renderer, halo, (SDL_Color){0, 255, 90, 255});

        }

        else if (selected) {

            draw_filled_rect(renderer, item, (SDL_Color){15, 85, 45, 245});

        }

        draw_outline_rect(renderer, item,
                          hovered    ? (SDL_Color){0, 255, 90, 255}
                          : selected ? (SDL_Color){0, 220, 80, 255}
                                     : (SDL_Color){0, 130, 55, 255});

        ANALYSIS_short_text(font, Global_Analysis_Files[index], short_name, sizeof(short_name), item.w - 20);

        draw_text(renderer, font, short_name, item.x + 10, item.y + 8,
                  hovered    ? (SDL_Color){235, 255, 240, 255}
                  : selected ? (SDL_Color){255, 255, 255, 255}
                             : (SDL_Color){0, 255, 90, 255});
    }

    char count_label[128];

    if (Global_Analysis_File_Search_Text[0]) {

        snprintf(count_label, sizeof(count_label), "%d of %d files", filtered_count, Global_Analysis_File_Count);

    }

    else {

        snprintf(count_label, sizeof(count_label), "%d files", Global_Analysis_File_Count);

    }

    draw_text(renderer, font, count_label, popup.x + 18, popup.y + popup.h - 24, (SDL_Color){0, 155, 65, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static int ANALYSIS_draw_wrapped_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y,
                                      int max_px, int line_h, SDL_Color color) {
    /*
        Purpose: Draws the wrapped text
        Returns: Success status
    */

    if (!renderer || !font || !text || max_px <= 0 || line_h <= 0) {

        return 0;

    }

    int len = (int)strlen(text);
    int pos = 0;
    int lines = 0;

    while (pos < len) {
        while (pos < len && text[pos] == ' ') {
            pos++;
        }

        if (pos >= len) {

            break;

        }

        int best = 1;
        int best_break = -1;

        for (int n = 1; pos + n <= len && n < 1000; n++) {
            char tmp[1024];

            memcpy(tmp, text + pos, (size_t)n);
            tmp[n] = '\0';

            int text_w = 0;
            int text_h = 0;

            if (TTF_SizeText(font, tmp, &text_w, &text_h) != 0) {

                break;

            }

            if (text_w > max_px) {

                break;

            }

            best = n;

            char c = text[pos + n - 1];

            if (c == ' ' || c == '_' || c == '-' || c == '/') {

                best_break = n;

            }
        }

        if (pos + best < len && best_break > 8) {

            best = best_break;

        }

        char line[1024];

        if (best >= (int)sizeof(line)) {

            best = (int)sizeof(line) - 1;

        }

        memcpy(line, text + pos, (size_t)best);
        line[best] = '\0';

        draw_text(renderer, font, line, x, y + (lines * line_h), color);

        pos += best;
        lines++;
    }

    return lines;
}

static int ANALYSIS_draw_wrapped_text_limited(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y,
                                              int max_px, int line_h, int max_lines, SDL_Color color) {
    /*
        Purpose: Draws wrapped text while limiting it to a fixed number of lines
        Returns: Number of rendered lines
    */

    if (!renderer || !font || !text || max_px <= 0 || line_h <= 0 || max_lines <= 0) {

        return 0;

    }

    int len = (int)strlen(text);
    int pos = 0;
    int lines = 0;

    while (pos < len && lines < max_lines) {
        while (pos < len && text[pos] == ' ') {
            pos++;
        }

        if (pos >= len) {

            break;

        }

        if (lines == max_lines - 1) {

            char remaining[1024];
            char shortened[1024];
            size_t remaining_length = strlen(text + pos);

            if (remaining_length >= sizeof(remaining)) {

                remaining_length = sizeof(remaining) - 1;

            }

            memcpy(remaining, text + pos, remaining_length);
            remaining[remaining_length] = '\0';
            ANALYSIS_short_text(font, remaining, shortened, sizeof(shortened), max_px);
            draw_text(renderer, font, shortened, x, y + (lines * line_h), color);
            lines++;
            break;

        }

        int best = 1;
        int best_break = -1;

        for (int n = 1; pos + n <= len && n < 1000; n++) {
            char candidate[1024];
            int text_w = 0;
            int text_h = 0;

            memcpy(candidate, text + pos, (size_t)n);
            candidate[n] = '\0';

            if (TTF_SizeText(font, candidate, &text_w, &text_h) != 0 || text_w > max_px) {

                break;

            }

            best = n;

            if (text[pos + n - 1] == ' ' || text[pos + n - 1] == '_' || text[pos + n - 1] == '-' ||
                text[pos + n - 1] == '/') {

                best_break = n;

            }
        }

        if (pos + best < len && best_break > 8) {

            best = best_break;

        }

        char line[1024];

        if (best >= (int)sizeof(line)) {

            best = (int)sizeof(line) - 1;

        }

        memcpy(line, text + pos, (size_t)best);
        line[best] = '\0';
        draw_text(renderer, font, line, x, y + (lines * line_h), color);
        pos += best;
        lines++;
    }

    return lines;
}

static void ANALYSIS_format_transmit_live_conversion(int field, char *buffer, size_t buffer_size) {
    /*
        Purpose: Formats the live million-unit equivalent for a transmission field
        Returns: No value
    */

    const char *text;
    char *end = NULL;
    unsigned long long value;

    if (!buffer || buffer_size == 0) {

        return;

    }

    buffer[0] = '\0';

    if (field < ANALYSIS_TRANSMIT_FIELD_FREQUENCY || field > ANALYSIS_TRANSMIT_FIELD_BANDWIDTH) {

        return;

    }

    text = Global_Analysis_Transmit_Field_Text[field];

    if (!text || text[0] == '\0') {

        snprintf(buffer, buffer_size, field == ANALYSIS_TRANSMIT_FIELD_SAMPLE_RATE ? "0.000000 MS/s" : "0.000000 MHz");
        return;

    }

    errno = 0;
    value = strtoull(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {

        snprintf(buffer, buffer_size, field == ANALYSIS_TRANSMIT_FIELD_SAMPLE_RATE ? "Invalid MS/s" : "Invalid MHz");
        return;

    }

    snprintf(buffer, buffer_size, field == ANALYSIS_TRANSMIT_FIELD_SAMPLE_RATE ? "%.6f MS/s" : "%.6f MHz",
             (double)value / 1000000.0);
}

static void ANALYSIS_signal_refresh_filename_if_auto(void);
static void ANALYSIS_draw_centered_button_text(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *text,
                                               SDL_Color color);

static void ANALYSIS_signal_clamp_file_cursor(void) {
    /*
        Purpose: Clamps the signal file cursor
        Returns: No value
    */

    int len = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);

    if (Global_Analysis_Signal_File_Cursor < 0) {

        Global_Analysis_Signal_File_Cursor = 0;

    }

    if (Global_Analysis_Signal_File_Cursor > len) {

        Global_Analysis_Signal_File_Cursor = len;

    }
}

static void ANALYSIS_signal_clear_file_selection(void) {
    /*
        Purpose: Clears the signal file selection
        Returns: No value
    */

    Global_Analysis_Signal_File_Selecting = 0;
    Global_Analysis_Signal_File_Selection_Start = -1;
    Global_Analysis_Signal_File_Selection_End = -1;
}

static int ANALYSIS_signal_file_has_selection(void) {
    /*
        Purpose: Checks whether the signal file has selection
        Returns: Success status
    */

    return Global_Analysis_Signal_File_Selection_Start >= 0 && Global_Analysis_Signal_File_Selection_End >= 0 &&
           Global_Analysis_Signal_File_Selection_Start != Global_Analysis_Signal_File_Selection_End;
}

static void ANALYSIS_signal_get_file_selection_range(int *start, int *end) {
    /*
        Purpose: Gets the signal file selection range
        Returns: No value
    */

    int a = Global_Analysis_Signal_File_Selection_Start;
    int b = Global_Analysis_Signal_File_Selection_End;
    int len = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);

    if (a < 0) {

        a = 0;

    }

    if (b < 0) {

        b = 0;

    }

    if (a > len) {

        a = len;

    }

    if (b > len) {

        b = len;

    }

    if (b < a) {

        int tmp = a;
        a = b;
        b = tmp;

    }

    if (start) {

        *start = a;

    }

    if (end) {

        *end = b;

    }
}

static int ANALYSIS_signal_delete_file_selection(void) {
    /*
        Purpose: Deletes the signal file selection
        Returns: Success status
    */

    if (!ANALYSIS_signal_file_has_selection()) {

        return 0;

    }

    char *dst = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];
    int start = 0;
    int end = 0;

    ANALYSIS_signal_get_file_selection_range(&start, &end);

    if (end <= start) {

        ANALYSIS_signal_clear_file_selection();
        return 0;

    }

    size_t len = strlen(dst);

    memmove(dst + start, dst + end, len - (size_t)end + 1);

    Global_Analysis_Signal_File_Cursor = start;
    Global_Analysis_Signal_File_Manual_Edit = 1;
    ANALYSIS_signal_clear_file_selection();

    return 1;
}

static int ANALYSIS_signal_text_width_range(TTF_Font *font, const char *text, int start, int end) {
    /*
        Purpose: Gets the signal text width range
        Returns: Text width
    */

    if (!text || end <= start) {

        return 0;

    }

    int len = (int)strlen(text);

    if (start < 0) {

        start = 0;

    }

    if (end < start) {

        end = start;

    }

    if (end > len) {

        end = len;

    }

    int count = end - start;

    if (count <= 0) {

        return 0;

    }

    char tmp[1024];

    if (count >= (int)sizeof(tmp)) {

        count = (int)sizeof(tmp) - 1;

    }

    memcpy(tmp, text + start, (size_t)count);
    tmp[count] = '\0';

    if (font) {

        int text_w = 0;
        int text_h = 0;

        if (TTF_SizeText(font, tmp, &text_w, &text_h) == 0) {

            return text_w;

        }

    }

    return count * 8;
}

#define ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES 16

static int ANALYSIS_signal_filename_wrap_lines(TTF_Font *font, const char *text, int max_px,
                                               int starts[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES],
                                               int ends[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES]) {
    /*
        Purpose: Wraps the signal filename lines
        Returns: Success status
    */

    if (!starts || !ends) {

        return 0;

    }

    if (!text) {

        text = "";

    }

    int len = (int)strlen(text);
    int pos = 0;
    int lines = 0;

    if (max_px < 8) {

        max_px = 8;

    }

    while (pos < len && lines < ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES) {
        int best = 1;
        int best_break = -1;

        for (int n = 1; pos + n <= len && n < 1000; n++) {
            char tmp[1024];

            memcpy(tmp, text + pos, (size_t)n);
            tmp[n] = '\0';

            int text_w = 0;
            int text_h = 0;

            if (font) {

                if (TTF_SizeText(font, tmp, &text_w, &text_h) != 0) {

                    break;

                }

            }

            else {

                text_w = n * 8;

            }

            if (text_w > max_px) {

                break;

            }

            best = n;

            char c = text[pos + n - 1];

            if (c == '_' || c == '-' || c == '.' || c == ' ') {

                best_break = n;

            }
        }

        if (pos + best < len && best_break > 8) {

            best = best_break;

        }

        starts[lines] = pos;
        ends[lines] = pos + best;
        pos += best;
        lines++;
    }

    if (lines == 0) {

        starts[0] = 0;
        ends[0] = 0;
        lines = 1;

    }

    if (pos < len && lines > 0) {

        ends[lines - 1] = len;

    }

    return lines;
}

static void ANALYSIS_signal_insert_file_cursor_text(const char *src) {
    /*
        Purpose: Inserts the signal file cursor text
        Returns: No value
    */

    if (!src || src[0] == '\0') {

        return;

    }

    char *dst = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];

    if (ANALYSIS_signal_file_has_selection()) {

        ANALYSIS_signal_delete_file_selection();

    }

    size_t len = strlen(dst);
    size_t add = strlen(src);

    ANALYSIS_signal_clamp_file_cursor();

    if (len >= ANALYSIS_SIGNAL_TEXT_MAX - 1) {

        return;

    }

    if (add > (ANALYSIS_SIGNAL_TEXT_MAX - 1) - len) {

        add = (ANALYSIS_SIGNAL_TEXT_MAX - 1) - len;

    }

    memmove(dst + Global_Analysis_Signal_File_Cursor + (int)add, dst + Global_Analysis_Signal_File_Cursor,
            len - (size_t)Global_Analysis_Signal_File_Cursor + 1);

    memcpy(dst + Global_Analysis_Signal_File_Cursor, src, add);
    Global_Analysis_Signal_File_Cursor += (int)add;
    Global_Analysis_Signal_File_Manual_Edit = 1;
    ANALYSIS_signal_clear_file_selection();
}

static void ANALYSIS_signal_backspace_file_cursor_text(void) {
    /*
        Purpose: Removes the previous character from the signal file cursor text
        Returns: No value
    */

    char *dst = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];

    ANALYSIS_signal_clamp_file_cursor();

    if (ANALYSIS_signal_delete_file_selection()) {

        return;

    }

    if (Global_Analysis_Signal_File_Cursor <= 0) {

        return;

    }

    size_t len = strlen(dst);

    memmove(dst + Global_Analysis_Signal_File_Cursor - 1, dst + Global_Analysis_Signal_File_Cursor,
            len - (size_t)Global_Analysis_Signal_File_Cursor + 1);

    Global_Analysis_Signal_File_Cursor--;
    Global_Analysis_Signal_File_Manual_Edit = 1;
    ANALYSIS_signal_clear_file_selection();
}

static void ANALYSIS_signal_delete_file_cursor_text(void) {
    /*
        Purpose: Deletes the signal file cursor text
        Returns: No value
    */

    char *dst = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];

    ANALYSIS_signal_clamp_file_cursor();

    if (ANALYSIS_signal_delete_file_selection()) {

        return;

    }

    size_t len = strlen(dst);

    if (Global_Analysis_Signal_File_Cursor >= (int)len) {

        return;

    }

    memmove(dst + Global_Analysis_Signal_File_Cursor, dst + Global_Analysis_Signal_File_Cursor + 1,
            len - (size_t)Global_Analysis_Signal_File_Cursor);

    Global_Analysis_Signal_File_Manual_Edit = 1;
    ANALYSIS_signal_clear_file_selection();
}

static int ANALYSIS_signal_set_file_cursor_from_mouse(TTF_Font *font, SDL_Rect rect, int mouse_x, int mouse_y) {
    /*
        Purpose: Sets the signal file cursor from mouse
        Returns: Success status
    */

    const char *text = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];
    int starts[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES];
    int ends[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES];
    int text_x = rect.x + 8;
    int text_y = rect.y + 8;
    int line_h = 18;
    int max_px = rect.w - 16;
    int lines = ANALYSIS_signal_filename_wrap_lines(font, text, max_px, starts, ends);
    int line = (mouse_y - text_y) / line_h;

    if (line < 0) {

        line = 0;

    }

    if (line >= lines) {

        line = lines - 1;

    }

    int line_start = starts[line];
    int line_end = ends[line];
    int line_len = line_end - line_start;
    int rel_x = mouse_x - text_x;

    if (rel_x <= 0) {

        Global_Analysis_Signal_File_Cursor = line_start;
        ANALYSIS_signal_clamp_file_cursor();
        return Global_Analysis_Signal_File_Cursor;

    }

    for (int i = 0; i < line_len; i++) {
        char left[1024];
        char right[1024];
        int left_w = 0;
        int right_w = 0;
        int text_h = 0;

        if (i >= (int)sizeof(left) - 1) {

            break;

        }

        if (i + 1 >= (int)sizeof(right)) {

            break;

        }

        memcpy(left, text + line_start, (size_t)i);
        left[i] = '\0';

        memcpy(right, text + line_start, (size_t)(i + 1));
        right[i + 1] = '\0';

        if (font) {

            TTF_SizeText(font, left, &left_w, &text_h);
            TTF_SizeText(font, right, &right_w, &text_h);

        }

        else {

            left_w = i * 8;
            right_w = (i + 1) * 8;

        }

        if (rel_x < (left_w + right_w) / 2) {

            Global_Analysis_Signal_File_Cursor = line_start + i;
            ANALYSIS_signal_clamp_file_cursor();
            return Global_Analysis_Signal_File_Cursor;

        }
    }

    Global_Analysis_Signal_File_Cursor = line_end;
    ANALYSIS_signal_clamp_file_cursor();
    return Global_Analysis_Signal_File_Cursor;
}

static void ANALYSIS_signal_draw_filename_field_text(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect,
                                                     int active) {
    /*
        Purpose: Draws the signal filename field text
        Returns: No value
    */

    const char *src = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];
    SDL_Color text_color = src[0] != '\0' || active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){95, 130, 95, 255};
    int starts[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES];
    int ends[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES];
    int line_h = 18;
    int max_lines = (rect.h - 12) / line_h;
    int lines = 0;

    if (max_lines < 1) {

        max_lines = 1;

    }

    if (src[0] == '\0' && !active) {

        draw_text(renderer, font, "Click to type", rect.x + 8, rect.y + 8, text_color);
        return;

    }

    lines = ANALYSIS_signal_filename_wrap_lines(font, src, rect.w - 16, starts, ends);

    if (lines > max_lines) {

        lines = max_lines;

    }

    if (active && ANALYSIS_signal_file_has_selection()) {

        int sel_start = 0;
        int sel_end = 0;

        ANALYSIS_signal_get_file_selection_range(&sel_start, &sel_end);

        for (int i = 0; i < lines; i++) {
            int line_start = starts[i];
            int line_end = ends[i];
            int draw_start = sel_start > line_start ? sel_start : line_start;
            int draw_end = sel_end < line_end ? sel_end : line_end;

            if (draw_end <= draw_start) {

                continue;

            }

            int x0 = rect.x + 8 + ANALYSIS_signal_text_width_range(font, src, line_start, draw_start);
            int x1 = rect.x + 8 + ANALYSIS_signal_text_width_range(font, src, line_start, draw_end);

            if (x1 <= x0) {

                x1 = x0 + 2;

            }

            SDL_Rect selection_rect = {x0, rect.y + 7 + (i * line_h), x1 - x0, line_h};

            draw_filled_rect(renderer, selection_rect, (SDL_Color){0, 90, 255, 120});
        }

    }

    for (int i = 0; i < lines; i++) {
        char line[1024];
        int count = ends[i] - starts[i];

        if (count < 0) {

            count = 0;

        }

        if (count >= (int)sizeof(line)) {

            count = (int)sizeof(line) - 1;

        }

        memcpy(line, src + starts[i], (size_t)count);
        line[count] = '\0';

        draw_text(renderer, font, line, rect.x + 8, rect.y + 8 + (i * line_h), text_color);
    }

    if (active && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {

        ANALYSIS_signal_clamp_file_cursor();

        int cursor = Global_Analysis_Signal_File_Cursor;
        int cursor_line = 0;
        int cursor_x = rect.x + 8;
        int cursor_y = rect.y + 8;

        for (int i = 0; i < lines; i++) {

            if (cursor >= starts[i] && cursor <= ends[i]) {

                cursor_line = i;
                break;

            }

            if (i == lines - 1 && cursor > ends[i]) {

                cursor_line = i;

            }
        }

        if (cursor_line < 0) {

            cursor_line = 0;

        }

        if (cursor_line >= lines) {

            cursor_line = lines - 1;

        }

        if (lines > 0) {

            int line_start = starts[cursor_line];
            int line_end = ends[cursor_line];

            if (cursor < line_start) {

                cursor = line_start;

            }

            if (cursor > line_end) {

                cursor = line_end;

            }

            if (cursor > line_start) {

                char before[1024];
                int before_len = cursor - line_start;

                if (before_len >= (int)sizeof(before)) {

                    before_len = (int)sizeof(before) - 1;

                }

                memcpy(before, src + line_start, (size_t)before_len);
                before[before_len] = '\0';

                int text_w = 0;
                int text_h = 0;

                if (font && TTF_SizeText(font, before, &text_w, &text_h) == 0) {

                    cursor_x += text_w;

                }

                else {

                    cursor_x += before_len * 8;

                }

            }

            cursor_y = rect.y + 8 + (cursor_line * line_h);

        }

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
        SDL_RenderDrawLine(renderer, cursor_x, cursor_y, cursor_x, cursor_y + line_h - 2);
        SDL_RenderDrawLine(renderer, cursor_x + 1, cursor_y, cursor_x + 1, cursor_y + line_h - 2);

    }
}

static void ANALYSIS_signal_append_text(const char *src) {
    /*
        Purpose: Appends the signal text
        Returns: No value
    */

    if (!src || Global_Analysis_Signal_Active_Field < 0 ||
        Global_Analysis_Signal_Active_Field >= ANALYSIS_SIGNAL_FIELD_COUNT) {

        return;

    }

    if (Global_Analysis_Signal_Active_Field == ANALYSIS_SIGNAL_FILENAME_FIELD) {

        ANALYSIS_signal_insert_file_cursor_text(src);
        return;

    }

    char *dst = Global_Analysis_Signal_Field_Text[Global_Analysis_Signal_Active_Field];
    size_t used = strlen(dst);

    if (used >= ANALYSIS_SIGNAL_TEXT_MAX - 1) {

        return;

    }

    if (!sec_strcat(dst, ANALYSIS_SIGNAL_TEXT_MAX, src)) {

        return;

    }

    ANALYSIS_signal_refresh_filename_if_auto();
}

static void ANALYSIS_signal_backspace_text(void) {
    /*
        Purpose: Removes the previous character from the signal text
        Returns: No value
    */

    if (Global_Analysis_Signal_Active_Field < 0 || Global_Analysis_Signal_Active_Field >= ANALYSIS_SIGNAL_FIELD_COUNT) {

        return;

    }

    if (Global_Analysis_Signal_Active_Field == ANALYSIS_SIGNAL_FILENAME_FIELD) {

        ANALYSIS_signal_backspace_file_cursor_text();
        return;

    }

    char *dst = Global_Analysis_Signal_Field_Text[Global_Analysis_Signal_Active_Field];
    size_t len = strlen(dst);

    if (len > 0) {

        dst[len - 1] = '\0';

    }

    ANALYSIS_signal_refresh_filename_if_auto();
}

static void ANALYSIS_signal_clear_active_text(void) {
    /*
        Purpose: Clears the signal active text
        Returns: No value
    */

    if (Global_Analysis_Signal_Active_Field < 0 || Global_Analysis_Signal_Active_Field >= ANALYSIS_SIGNAL_FIELD_COUNT) {

        return;

    }

    Global_Analysis_Signal_Field_Text[Global_Analysis_Signal_Active_Field][0] = '\0';

    if (Global_Analysis_Signal_Active_Field == ANALYSIS_SIGNAL_FILENAME_FIELD) {

        Global_Analysis_Signal_File_Cursor = 0;
        Global_Analysis_Signal_File_Manual_Edit = 1;
        ANALYSIS_signal_clear_file_selection();
        return;

    }

    ANALYSIS_signal_refresh_filename_if_auto();
}

static void ANALYSIS_get_signal_icon_rect(int win_w, int win_h, SDL_Rect *out) {
    /*
        Purpose: Gets the signal icon rectangle
        Returns: No value
    */

    (void)win_h;

    if (!out) {

        return;

    }

    *out = (SDL_Rect){win_w - 394, 8, 34, 34};

    if (out->x < MARGIN + (3 * (out->w + 8))) {

        out->x = MARGIN + (3 * (out->w + 8));

    }

    Global_Analysis_Signal_Icon_Rect = *out;
    Global_Analysis_Signal_Icon_Rect_Valid = 1;
}

static void ANALYSIS_get_transmit_rect(int win_w, int win_h, SDL_Rect *out) {
    /*
        Purpose: Gets the transmit icon rectangle
        Returns: No value
    */

    if (!out) {

        return;

    }

    SDL_Rect settings_rect;
    ANALYSIS_get_signal_icon_rect(win_w, win_h, &settings_rect);

    *out = (SDL_Rect){settings_rect.x - settings_rect.w - 8, settings_rect.y, settings_rect.w, settings_rect.h};

    Global_Analysis_Transmit_Rect = *out;
    Global_Analysis_Transmit_Rect_Valid = 1;
}

static void ANALYSIS_get_multithread_rect(int win_w, int win_h, SDL_Rect *out) {
    /*
        Purpose: Gets the multithread rectangle
        Returns: No value
    */

    if (!out) {

        return;

    }

    SDL_Rect transmit_rect;
    ANALYSIS_get_transmit_rect(win_w, win_h, &transmit_rect);

    *out = (SDL_Rect){transmit_rect.x - transmit_rect.w - 8, transmit_rect.y, transmit_rect.w, transmit_rect.h};

    Global_Analysis_Multithread_Rect = *out;
    Global_Analysis_Multithread_Rect_Valid = 1;
}

static void ANALYSIS_get_signal_trash_rect(int win_w, int win_h, SDL_Rect *out) {
    /*
        Purpose: Gets the signal trash rectangle
        Returns: No value
    */

    if (!out) {

        return;

    }

    SDL_Rect thread_rect;
    ANALYSIS_get_multithread_rect(win_w, win_h, &thread_rect);

    *out = (SDL_Rect){thread_rect.x - thread_rect.w - 8, thread_rect.y, thread_rect.w, thread_rect.h};

    Global_Analysis_Signal_Trash_Rect = *out;
    Global_Analysis_Signal_Trash_Rect_Valid = 1;
}

static void ANALYSIS_get_signal_menu_rects(int win_w, int win_h, SDL_Rect *panel_rect,
                                           SDL_Rect field_rects[ANALYSIS_SIGNAL_FIELD_COUNT], SDL_Rect *save_rect,
                                           SDL_Rect *close_rect) {
    /*
        Purpose: Gets the signal menu rects
        Returns: No value
    */

    int panel_w = 720;
    int panel_h = 585;

    if (panel_w > win_w - 60) {

        panel_w = win_w - 60;

    }

    if (panel_h > win_h - 60) {

        panel_h = win_h - 60;

    }

    if (panel_w < 560) {

        panel_w = 560;

    }

    if (panel_h < 520) {

        panel_h = 520;

    }

    SDL_Rect panel = {(win_w - panel_w) / 2, (win_h - panel_h) / 2, panel_w, panel_h};

    if (panel_rect) {

        *panel_rect = panel;

    }

    int left_x = panel.x + 28;
    int right_x = panel.x + (panel.w / 2) + 10;
    int top_y = panel.y + 232;
    int box_w = (panel.w - 66) / 2;
    int box_h = 38;
    int row_gap = 64;

    if (field_rects) {

        field_rects[0] = (SDL_Rect){left_x, top_y, box_w, box_h};
        field_rects[1] = (SDL_Rect){right_x, top_y, box_w, box_h};
        field_rects[2] = (SDL_Rect){left_x, top_y + row_gap, box_w, box_h};
        field_rects[ANALYSIS_SIGNAL_DECIMATION_FIELD] = (SDL_Rect){right_x, top_y + row_gap, box_w, box_h};
        field_rects[3] = (SDL_Rect){left_x, top_y + row_gap * 2, box_w - 82, box_h};
        field_rects[4] = (SDL_Rect){right_x, top_y + row_gap * 2, box_w - 82, box_h};
        field_rects[ANALYSIS_SIGNAL_FILENAME_FIELD] = (SDL_Rect){left_x, top_y + row_gap * 3, panel.w - 56, 94};

    }

    if (save_rect) {

        *save_rect = (SDL_Rect){panel.x + panel.w - 252, panel.y + panel.h - 58, 116, 36};

    }

    if (close_rect) {

        *close_rect = (SDL_Rect){panel.x + panel.w - 120, panel.y + panel.h - 58, 92, 36};

    }
}

static void ANALYSIS_get_signal_marker_rects(SDL_Rect field_rects[ANALYSIS_SIGNAL_FIELD_COUNT],
                                             SDL_Rect *start_marker_rect, SDL_Rect *end_marker_rect) {
    /*
        Purpose: Gets the signal marker rects
        Returns: No value
    */

    int marker_w = 74;
    int marker_gap = 8;

    if (start_marker_rect) {

        SDL_Rect r = field_rects[3];
        *start_marker_rect = (SDL_Rect){r.x + r.w + marker_gap, r.y, marker_w, r.h};

    }

    if (end_marker_rect) {

        SDL_Rect r = field_rects[4];
        *end_marker_rect = (SDL_Rect){r.x + r.w + marker_gap, r.y, marker_w, r.h};

    }
}

static void ANALYSIS_signal_set_time_field_from_marker(int field_index) {
    /*
        Purpose: Sets the signal time field from marker
        Returns: No value
    */

    if (!Global_Analysis_Marker_Active || field_index < 0 || field_index >= ANALYSIS_SIGNAL_FIELD_COUNT) {

        return;

    }

    snprintf(Global_Analysis_Signal_Field_Text[field_index], ANALYSIS_SIGNAL_TEXT_MAX, "%.6f",
             Global_Analysis_Marker_Time);

    Global_Analysis_Signal_Active_Field = field_index;
    ANALYSIS_signal_clear_file_selection();
    ANALYSIS_signal_refresh_filename_if_auto();
}

static void ANALYSIS_draw_signal_marker_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, int enabled,
                                               int hover) {
    /*
        Purpose: Draws the signal marker button
        Returns: No value
    */

    if (!renderer || !font) {

        return;

    }

    if (enabled && hover) {

        SDL_Rect glow = {rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 38});
        draw_outline_rect(renderer, glow, (SDL_Color){0, 255, 90, 170});

    }

    draw_filled_rect(renderer, rect,
                     !enabled ? (SDL_Color){24, 24, 24, 255}
                     : hover  ? (SDL_Color){0, 55, 20, 255}
                              : (SDL_Color){0, 30, 12, 255});

    draw_outline_rect(renderer, rect,
                      !enabled ? (SDL_Color){82, 82, 82, 255}
                      : hover  ? (SDL_Color){0, 255, 90, 255}
                               : (SDL_Color){0, 150, 55, 255});

    ANALYSIS_draw_centered_button_text(renderer, font, rect, "Marker",
                                       !enabled ? (SDL_Color){110, 110, 110, 255} : (SDL_Color){0, 255, 90, 255});
}

static void ANALYSIS_signal_menu_prefill(void) {
    /*
        Purpose: Prefills the signal menu
        Returns: No value
    */

    const char *name = ANALYSIS_selected_file_name();

    if (!name || name[0] == '\0') {

        return;

    }

    if (strcmp(Global_Analysis_Signal_Menu_File, name) == 0) {

        return;

    }

    snprintf(Global_Analysis_Signal_Menu_File, sizeof(Global_Analysis_Signal_Menu_File), "%s", name);

    memset(Global_Analysis_Signal_Field_Text, 0, sizeof(Global_Analysis_Signal_Field_Text));

    if (Global_Analysis_Path[0] != '\0' && Global_Analysis_Sample_Rate > 0.0) {

        double center_hz = Global_Analysis_Center_Hz;
        double bw_hz = Global_Analysis_Sample_Rate;
        double sample_rate_hz = Global_Analysis_Sample_Rate;
        double start_sec = 0.0;
        double end_sec =
            Global_Analysis_IQ_Count > 0 ? (double)Global_Analysis_IQ_Count / Global_Analysis_Sample_Rate : 0.0;

        if (Global_Analysis_Filter_Active || Global_Analysis_Filter_Visible) {

            double y0 = Global_Analysis_Filter_Y0;
            double y1 = Global_Analysis_Filter_Y1;

            if (y1 < y0) {

                double tmp = y0;
                y0 = y1;
                y1 = tmp;

            }

            double center_y = (y0 + y1) * 0.5;
            center_hz = ANALYSIS_frequency_from_spec_frac(center_y);
            bw_hz = fabs(y1 - y0) * Global_Analysis_Sample_Rate;

        }

        if (Global_Analysis_Column_Active || Global_Analysis_Column_Visible) {

            double x0 = Global_Analysis_Column_X0;
            double x1 = Global_Analysis_Column_X1;

            if (x1 < x0) {

                double tmp = x0;
                x0 = x1;
                x1 = tmp;

            }

            x0 = ANALYSIS_limit_double(x0, 0.0, 1.0);
            x1 = ANALYSIS_limit_double(x1, 0.0, 1.0);

            start_sec = (double)(Global_Analysis_View_Start + (size_t)(x0 * (double)Global_Analysis_View_Len)) /
                        Global_Analysis_Sample_Rate;
            end_sec = (double)(Global_Analysis_View_Start + (size_t)(x1 * (double)Global_Analysis_View_Len)) /
                      Global_Analysis_Sample_Rate;

        }

        snprintf(Global_Analysis_Signal_Field_Text[0], ANALYSIS_SIGNAL_TEXT_MAX, "%.6f", center_hz / 1e6);
        snprintf(Global_Analysis_Signal_Field_Text[1], ANALYSIS_SIGNAL_TEXT_MAX, "%.3f", bw_hz / 1e3);
        snprintf(Global_Analysis_Signal_Field_Text[2], ANALYSIS_SIGNAL_TEXT_MAX, "%.3f", sample_rate_hz / 1e3);
        snprintf(Global_Analysis_Signal_Field_Text[3], ANALYSIS_SIGNAL_TEXT_MAX, "%.6f", start_sec);
        snprintf(Global_Analysis_Signal_Field_Text[4], ANALYSIS_SIGNAL_TEXT_MAX, "%.6f", end_sec);
        snprintf(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_DECIMATION_FIELD], ANALYSIS_SIGNAL_TEXT_MAX, "1");

        Global_Analysis_Signal_File_Manual_Edit = 0;
        ANALYSIS_signal_refresh_filename_if_auto();
        Global_Analysis_Signal_File_Cursor =
            (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);
        ANALYSIS_signal_clear_file_selection();

    }
}

static void ANALYSIS_draw_thick_line(SDL_Renderer *renderer, int x0, int y0, int x1, int y1, int thickness,
                                     SDL_Color color) {
    /*
        Purpose: Draws the thick line
        Returns: No value
    */

    if (!renderer || thickness <= 0) {

        return;

    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    int dx = x1 - x0;
    int dy = y1 - y0;

    if (abs(dx) >= abs(dy)) {

        for (int off = -(thickness / 2); off <= thickness / 2; off++) {
            SDL_RenderDrawLine(renderer, x0, y0 + off, x1, y1 + off);
        }

    }

    else {

        for (int off = -(thickness / 2); off <= thickness / 2; off++) {
            SDL_RenderDrawLine(renderer, x0 + off, y0, x1 + off, y1);
        }

    }
}

static void ANALYSIS_draw_circle_outline(SDL_Renderer *renderer, int cx, int cy, int radius, int thickness,
                                         SDL_Color color) {
    /*
        Purpose: Draws the circle outline
        Returns: No value
    */

    if (!renderer || radius <= 0 || thickness <= 0) {

        return;

    }

    int segments = 48;

    for (int t = 0; t < thickness; t++) {
        double r = (double)(radius - t);

        if (r <= 0.0) {

            break;

        }

        for (int i = 0; i < segments; i++) {
            double a0 = ((double)i / (double)segments) * 2.0 * M_PI;
            double a1 = ((double)(i + 1) / (double)segments) * 2.0 * M_PI;
            int x0 = cx + (int)lrint(cos(a0) * r);
            int y0 = cy + (int)lrint(sin(a0) * r);
            int x1 = cx + (int)lrint(cos(a1) * r);
            int y1 = cy + (int)lrint(sin(a1) * r);

            ANALYSIS_draw_thick_line(renderer, x0, y0, x1, y1, 1, color);
        }
    }
}

static void ANALYSIS_draw_signal_gear_shape(SDL_Renderer *renderer, SDL_Rect icon_rect, SDL_Color gear,
                                            SDL_Color cutout) {
    /*
        Purpose: Draws the signal gear shape
        Returns: No value
    */

    (void)cutout;

    if (!renderer) {

        return;

    }

    int cx = icon_rect.x + icon_rect.w / 2;
    int cy = icon_rect.y + icon_rect.h / 2;
    int points_x[32];
    int points_y[32];
    int point_count = 0;

    /*
     * Classic outline gear: eight blocky teeth, thick continuous outline,
     * and a clean inner circle. It is intentionally outline-based like the
     * reference image so it reads as a gear even at icon size.
     */

    for (int i = 0; i < 8; i++) {
        double base = ((double)i / 8.0) * 2.0 * M_PI;
        double angles[4] = {base - 0.245, base - 0.115, base + 0.115, base + 0.245};
        double radii[4] = {11.0, 15.0, 15.0, 11.0};

        for (int j = 0; j < 4 && point_count < 32; j++) {
            points_x[point_count] = cx + (int)lrint(cos(angles[j]) * radii[j]);
            points_y[point_count] = cy + (int)lrint(sin(angles[j]) * radii[j]);
            point_count++;
        }
    }

    for (int i = 0; i < point_count; i++) {
        int j = (i + 1) % point_count;

        ANALYSIS_draw_thick_line(renderer, points_x[i], points_y[i], points_x[j], points_y[j], 3, gear);
    }

    ANALYSIS_draw_circle_outline(renderer, cx, cy, 7, 3, gear);
}

static void ANALYSIS_draw_signal_settings_icon(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the signal settings icon
        Returns: No value
    */

    if (!renderer || !font || !ANALYSIS_signal_menu_available()) {

        return;

    }

    SDL_Rect icon_rect;
    ANALYSIS_get_signal_icon_rect(win_w, win_h, &icon_rect);

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int hover = point_in_rect(mouse_x, mouse_y, icon_rect);

    SDL_Color bg = hover || Global_Analysis_Signal_Menu_Open ? (SDL_Color){0, 40, 16, 235} : (SDL_Color){0, 0, 0, 220};
    SDL_Color border =
        hover || Global_Analysis_Signal_Menu_Open ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 130, 50, 230};
    SDL_Color gear =
        hover || Global_Analysis_Signal_Menu_Open ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 185, 70, 255};

    if (hover || Global_Analysis_Signal_Menu_Open) {

        SDL_Rect glow = {icon_rect.x - 3, icon_rect.y - 3, icon_rect.w + 6, icon_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 35});
        draw_outline_rect(renderer, glow, (SDL_Color){0, 255, 90, 150});

    }

    draw_filled_rect(renderer, icon_rect, bg);
    draw_outline_rect(renderer, icon_rect, border);
    ANALYSIS_draw_signal_gear_shape(renderer, icon_rect, gear, bg);
}

static void ANALYSIS_draw_signal_trash_shape(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Draws the signal trash shape
        Returns: No value
    */

    if (!renderer) {

        return;

    }

    int x = rect.x;
    int y = rect.y;
    int w = rect.w;
    int h = rect.h;
    int left = x + 10;
    int right = x + w - 10;
    int top = y + 12;
    int bottom = y + h - 7;

    ANALYSIS_draw_thick_line(renderer, left, top, right, top, 2, color);
    ANALYSIS_draw_thick_line(renderer, left + 1, top, left + 3, bottom, 2, color);
    ANALYSIS_draw_thick_line(renderer, right - 1, top, right - 3, bottom, 2, color);
    ANALYSIS_draw_thick_line(renderer, left + 3, bottom, right - 3, bottom, 2, color);

    ANALYSIS_draw_thick_line(renderer, x + 8, y + 9, x + w - 8, y + 9, 2, color);
    ANALYSIS_draw_thick_line(renderer, x + 13, y + 6, x + w - 13, y + 6, 2, color);
    ANALYSIS_draw_thick_line(renderer, x + 14, y + 6, x + 14, y + 9, 2, color);
    ANALYSIS_draw_thick_line(renderer, x + w - 14, y + 6, x + w - 14, y + 9, 2, color);

    ANALYSIS_draw_thick_line(renderer, x + 15, y + 15, x + 15, y + h - 10, 1, color);
    ANALYSIS_draw_thick_line(renderer, x + w / 2, y + 15, x + w / 2, y + h - 10, 1, color);
    ANALYSIS_draw_thick_line(renderer, x + w - 15, y + 15, x + w - 15, y + h - 10, 1, color);
}

static void ANALYSIS_draw_signal_trash_icon(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the signal trash icon
        Returns: No value
    */

    if (!renderer || !font || !ANALYSIS_signal_menu_available()) {

        return;

    }

    SDL_Rect trash_rect;
    ANALYSIS_get_signal_trash_rect(win_w, win_h, &trash_rect);

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int hover = point_in_rect(mouse_x, mouse_y, trash_rect);

    SDL_Color bg =
        hover || Global_Analysis_Delete_Confirm_Open ? (SDL_Color){42, 0, 0, 235} : (SDL_Color){0, 0, 0, 220};
    SDL_Color border =
        hover || Global_Analysis_Delete_Confirm_Open ? (SDL_Color){255, 60, 60, 255} : (SDL_Color){120, 120, 120, 230};
    SDL_Color trash =
        hover || Global_Analysis_Delete_Confirm_Open ? (SDL_Color){255, 70, 70, 255} : (SDL_Color){150, 150, 150, 255};

    if (hover || Global_Analysis_Delete_Confirm_Open) {

        SDL_Rect glow = {trash_rect.x - 3, trash_rect.y - 3, trash_rect.w + 6, trash_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){255, 40, 40, 35});
        draw_outline_rect(renderer, glow, (SDL_Color){255, 60, 60, 150});

    }

    draw_filled_rect(renderer, trash_rect, bg);
    draw_outline_rect(renderer, trash_rect, border);
    ANALYSIS_draw_signal_trash_shape(renderer, trash_rect, trash);
}

static void ANALYSIS_draw_multithread_icon(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the multithread icon
        Returns: No value
    */

    if (!renderer || !font || !ANALYSIS_signal_menu_available()) {

        return;

    }

    SDL_Rect rect;
    ANALYSIS_get_multithread_rect(win_w, win_h, &rect);

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int hover = point_in_rect(mouse_x, mouse_y, rect);
    int active = Global_Analysis_Multithread_Enabled;

    SDL_Color state_color = active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){255, 70, 70, 255};
    SDL_Color bg = active ? (SDL_Color){0, 34, 12, 235} : (SDL_Color){38, 0, 0, 235};
    SDL_Color border = hover || Global_Analysis_Multithread_Prompt_Open ? state_color : (SDL_Color){120, 120, 120, 230};

    if (hover || Global_Analysis_Multithread_Prompt_Open) {

        SDL_Rect glow = {rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6};
        SDL_Color glow_color = active ? (SDL_Color){0, 255, 90, 38} : (SDL_Color){255, 50, 50, 38};
        SDL_Color glow_border = active ? (SDL_Color){0, 255, 90, 160} : (SDL_Color){255, 70, 70, 160};
        draw_filled_rect(renderer, glow, glow_color);
        draw_outline_rect(renderer, glow, glow_border);

    }

    draw_filled_rect(renderer, rect, bg);
    draw_outline_rect(renderer, rect, border);
    ANALYSIS_draw_centered_button_text(renderer, font, rect, "T", state_color);
}

static void ANALYSIS_draw_transmit_shape(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Draws a miniature transmitting tower with propagating waves
        Returns: No value
    */

    if (!renderer) {

        return;

    }

    int cx = rect.x + rect.w / 2;
    int top = rect.y + 9;
    int bottom = rect.y + rect.h - 7;

    ANALYSIS_draw_thick_line(renderer, cx, top + 4, cx - 6, bottom, 2, color);
    ANALYSIS_draw_thick_line(renderer, cx, top + 4, cx + 6, bottom, 2, color);
    ANALYSIS_draw_thick_line(renderer, cx - 7, bottom, cx + 7, bottom, 2, color);
    ANALYSIS_draw_thick_line(renderer, cx - 4, bottom - 6, cx + 4, bottom - 6, 1, color);
    ANALYSIS_draw_circle_outline(renderer, cx, top + 2, 2, 2, color);

    for (int radius = 6; radius <= 11; radius += 5) {
        for (int dy = -radius; dy <= radius; dy++) {
            double inside = (double)(radius * radius - dy * dy);

            if (inside < 0.0) {

                continue;

            }

            int dx = (int)lrint(sqrt(inside));

            if (dx >= 4) {

                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
                SDL_RenderDrawPoint(renderer, cx - dx, top + 2 + dy);
                SDL_RenderDrawPoint(renderer, cx + dx, top + 2 + dy);

            }
        }
    }
}

static void ANALYSIS_draw_transmit_icon(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the file-transmission icon
        Returns: No value
    */

    if (!renderer || !font || !ANALYSIS_signal_menu_available()) {

        return;

    }

    SDL_Rect rect;
    ANALYSIS_get_transmit_rect(win_w, win_h, &rect);

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int hover = point_in_rect(mouse_x, mouse_y, rect);
    int active = Global_Analysis_Transmit_Auth_Prompt_Open || Global_Analysis_Transmit_Config_Prompt_Open ||
                 Global_Analysis_Transmit_Progress_Prompt_Open || Global_Analysis_Transmit_Result_Prompt_Open;
    SDL_Color bg = hover || active ? (SDL_Color){0, 40, 16, 235} : (SDL_Color){0, 0, 0, 220};
    SDL_Color border = hover || active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 130, 50, 230};
    SDL_Color icon = hover || active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 185, 70, 255};

    if (hover || active) {

        SDL_Rect glow = {rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 35});
        draw_outline_rect(renderer, glow, (SDL_Color){0, 255, 90, 150});

    }

    draw_filled_rect(renderer, rect, bg);
    draw_outline_rect(renderer, rect, border);
    ANALYSIS_draw_transmit_shape(renderer, rect, icon);
}

static void ANALYSIS_get_transmit_auth_rects(int win_w, int win_h, SDL_Rect *panel_rect, SDL_Rect *password_rect,
                                             SDL_Rect *authorize_rect, SDL_Rect *cancel_rect) {
    /*
        Purpose: Gets the password-authorization prompt rectangles
        Returns: No value
    */

    int panel_w = 590;
    int panel_h = 330;

    if (panel_w > win_w - 60) {

        panel_w = win_w - 60;

    }

    if (panel_h > win_h - 60) {

        panel_h = win_h - 60;

    }

    SDL_Rect panel = {(win_w - panel_w) / 2, (win_h - panel_h) / 2, panel_w, panel_h};

    if (panel_rect) {

        *panel_rect = panel;

    }

    if (password_rect) {

        *password_rect = (SDL_Rect){panel.x + 28, panel.y + 174, panel.w - 56, 42};

    }

    if (authorize_rect) {

        *authorize_rect = (SDL_Rect){panel.x + panel.w - 274, panel.y + panel.h - 58, 126, 36};

    }

    if (cancel_rect) {

        *cancel_rect = (SDL_Rect){panel.x + panel.w - 132, panel.y + panel.h - 58, 104, 36};

    }
}

static void ANALYSIS_get_transmit_config_rects(int win_w, int win_h, SDL_Rect *panel_rect,
                                               SDL_Rect field_rects[ANALYSIS_TRANSMIT_FIELD_COUNT],
                                               SDL_Rect *transmit_rect, SDL_Rect *cancel_rect) {
    /*
        Purpose: Gets the explicit transmission-settings prompt rectangles
        Returns: No value
    */

    int panel_w = 700;
    int panel_h = 560;

    if (panel_w > win_w - 40) {

        panel_w = win_w - 40;

    }

    if (panel_h > win_h - 30) {

        panel_h = win_h - 30;

    }

    SDL_Rect panel = {(win_w - panel_w) / 2, (win_h - panel_h) / 2, panel_w, panel_h};

    if (panel_rect) {

        *panel_rect = panel;

    }

    if (field_rects) {

        int first_y = panel.y + 150;
        int spacing = 68;

        for (int i = 0; i < ANALYSIS_TRANSMIT_FIELD_COUNT; i++) {
            field_rects[i] = (SDL_Rect){panel.x + 28, first_y + (i * spacing), panel.w - 56, 38};
        }

    }

    if (transmit_rect) {

        *transmit_rect = (SDL_Rect){panel.x + panel.w - 274, panel.y + panel.h - 50, 126, 34};

    }

    if (cancel_rect) {

        *cancel_rect = (SDL_Rect){panel.x + panel.w - 132, panel.y + panel.h - 50, 104, 34};

    }
}

static void ANALYSIS_get_transmit_progress_rects(int win_w, int win_h, SDL_Rect *panel_rect, SDL_Rect *abort_rect) {
    /*
        Purpose: Gets the active-transmission prompt rectangles
        Returns: No value
    */

    int panel_w = 600;
    int panel_h = 380;

    if (panel_w > win_w - 50) {

        panel_w = win_w - 50;

    }

    if (panel_h > win_h - 50) {

        panel_h = win_h - 50;

    }

    SDL_Rect panel = {(win_w - panel_w) / 2, (win_h - panel_h) / 2, panel_w, panel_h};

    if (panel_rect) {

        *panel_rect = panel;

    }

    if (abort_rect) {

        *abort_rect = (SDL_Rect){panel.x + panel.w - 132, panel.y + panel.h - 52, 104, 34};

    }
}

static void ANALYSIS_get_transmit_result_rects(int win_w, int win_h, SDL_Rect *panel_rect, SDL_Rect *close_rect) {
    /*
        Purpose: Gets the completed-transmission prompt rectangles
        Returns: No value
    */

    int panel_w = 560;
    int panel_h = 240;

    if (panel_w > win_w - 50) {

        panel_w = win_w - 50;

    }

    if (panel_h > win_h - 50) {

        panel_h = win_h - 50;

    }

    SDL_Rect panel = {(win_w - panel_w) / 2, (win_h - panel_h) / 2, panel_w, panel_h};

    if (panel_rect) {

        *panel_rect = panel;

    }

    if (close_rect) {

        *close_rect = (SDL_Rect){panel.x + panel.w - 132, panel.y + panel.h - 52, 104, 34};

    }
}

static void ANALYSIS_draw_transmit_text_field(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *text,
                                              int active, int password, int cursor) {
    /*
        Purpose: Draws a transmission text field with a blinking blue insertion cursor
        Returns: No value
    */

    char display[ANALYSIS_TRANSMIT_PASSWORD_MAX + 1];
    int text_length;

    if (!renderer || !font) {

        return;

    }

    text_length = text ? (int)strlen(text) : 0;

    if (cursor < 0) {

        cursor = 0;

    }

    if (cursor > text_length) {

        cursor = text_length;

    }

    if (password) {

        size_t length = text ? strlen(text) : 0;

        if (length > sizeof(display) - 1) {

            length = sizeof(display) - 1;

        }

        memset(display, '*', length);
        display[length] = '\0';

    }

    else {

        snprintf(display, sizeof(display), "%s", text ? text : "");

    }

    draw_filled_rect(renderer, rect, (SDL_Color){0, 20, 8, 255});
    draw_outline_rect(renderer, rect, active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 130, 50, 230});
    draw_text(renderer, font, display[0] ? display : " ", rect.x + 12, rect.y + 10, (SDL_Color){220, 255, 230, 255});

    if (active && ((SDL_GetTicks64() / 500ULL) % 2ULL) == 0ULL) {

        char prefix[ANALYSIS_TRANSMIT_PASSWORD_MAX + 1];
        int prefix_width = 0;
        int prefix_height = 0;
        int cursor_x;

        if (cursor >= (int)sizeof(prefix)) {

            cursor = (int)sizeof(prefix) - 1;

        }

        memcpy(prefix, display, (size_t)cursor);
        prefix[cursor] = '\0';
        (void)TTF_SizeUTF8(font, prefix, &prefix_width, &prefix_height);
        cursor_x = rect.x + 12 + prefix_width + 1;

        if (cursor_x > rect.x + rect.w - 8) {

            cursor_x = rect.x + rect.w - 8;

        }

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
        SDL_RenderDrawLine(renderer, cursor_x, rect.y + 7, cursor_x, rect.y + rect.h - 7);
        SDL_RenderDrawLine(renderer, cursor_x + 1, rect.y + 7, cursor_x + 1, rect.y + rect.h - 7);

    }
}

static void ANALYSIS_transmit_clamp_cursor(const char *text, int *cursor) {
    /*
        Purpose: Keeps a transmission text cursor inside its field
        Returns: No value
    */

    int length = text ? (int)strlen(text) : 0;

    if (!cursor) {

        return;

    }

    if (*cursor < 0) {

        *cursor = 0;

    }

    if (*cursor > length) {

        *cursor = length;

    }
}

static void ANALYSIS_transmit_insert_text(char *destination, size_t capacity, int *cursor, const char *source,
                                          int numeric_mode) {
    /*
        Purpose: Inserts text at a transmission field cursor
        Returns: No value
    */

    if (!destination || capacity == 0 || !cursor || !source) {

        return;

    }

    ANALYSIS_transmit_clamp_cursor(destination, cursor);

    for (const char *p = source; *p; p++) {
        size_t length = strlen(destination);

        if (length + 1 >= capacity) {

            break;

        }

        if (numeric_mode != 0 && !isdigit((unsigned char)*p)) {

            if (!(numeric_mode == 2 && *p == '-' && *cursor == 0 && destination[0] != '-')) {

                continue;

            }

        }

        memmove(destination + *cursor + 1, destination + *cursor, length - (size_t)*cursor + 1);
        destination[*cursor] = *p;
        (*cursor)++;
    }
}

static void ANALYSIS_transmit_backspace(char *text, int *cursor) {
    /*
        Purpose: Removes the character before a transmission field cursor
        Returns: No value
    */

    size_t length;

    if (!text || !cursor) {

        return;

    }

    ANALYSIS_transmit_clamp_cursor(text, cursor);
    length = strlen(text);

    if (*cursor > 0) {

        memmove(text + *cursor - 1, text + *cursor, length - (size_t)*cursor + 1);
        (*cursor)--;

    }
}

static void ANALYSIS_transmit_delete(char *text, int *cursor) {
    /*
        Purpose: Removes the character at a transmission field cursor
        Returns: No value
    */

    size_t length;

    if (!text || !cursor) {

        return;

    }

    ANALYSIS_transmit_clamp_cursor(text, cursor);
    length = strlen(text);

    if ((size_t)*cursor < length) {

        memmove(text + *cursor, text + *cursor + 1, length - (size_t)*cursor);

    }
}

static void ANALYSIS_close_transmit_prompts(void) {
    /*
        Purpose: Closes transmission entry prompts and clears sensitive input
        Returns: No value
    */

    Global_Analysis_Transmit_Auth_Prompt_Open = 0;
    Global_Analysis_Transmit_Config_Prompt_Open = 0;
    Global_Analysis_Transmit_Config_Active_Field = 0;
    Global_Analysis_Transmit_Password_Cursor = 0;
    Global_Analysis_Transmit_Auth_Status[0] = '\0';
    Global_Analysis_Transmit_Config_Status[0] = '\0';
    ANALYSIS_secure_clear(Global_Analysis_Transmit_Password, sizeof(Global_Analysis_Transmit_Password));
}

static void ANALYSIS_prefill_transmit_fields(void) {
    /*
        Purpose: Prefills explicit TX settings without reading any recording filename metadata
        Returns: No value
    */

    uint32_t sample_rate = Global_Analysis_Fallback_Rec_Out_Rate_Hz > 0 ? Global_Analysis_Fallback_Rec_Out_Rate_Hz
                                                                        : Global_Analysis_Fallback_Sample_Rate_Hz;

    memset(Global_Analysis_Transmit_Field_Text, 0, sizeof(Global_Analysis_Transmit_Field_Text));

    if (Global_Analysis_Fallback_Center_Hz > 0) {

        snprintf(Global_Analysis_Transmit_Field_Text[ANALYSIS_TRANSMIT_FIELD_FREQUENCY], ANALYSIS_TRANSMIT_TEXT_MAX,
                 "%llu", (unsigned long long)Global_Analysis_Fallback_Center_Hz);

    }

    if (sample_rate > 0) {

        snprintf(Global_Analysis_Transmit_Field_Text[ANALYSIS_TRANSMIT_FIELD_SAMPLE_RATE], ANALYSIS_TRANSMIT_TEXT_MAX,
                 "%u", sample_rate);
        snprintf(Global_Analysis_Transmit_Field_Text[ANALYSIS_TRANSMIT_FIELD_BANDWIDTH], ANALYSIS_TRANSMIT_TEXT_MAX,
                 "%u", sample_rate);

    }

    snprintf(Global_Analysis_Transmit_Field_Text[ANALYSIS_TRANSMIT_FIELD_GAIN], ANALYSIS_TRANSMIT_TEXT_MAX, "20");
    snprintf(Global_Analysis_Transmit_Field_Text[ANALYSIS_TRANSMIT_FIELD_REPEAT], ANALYSIS_TRANSMIT_TEXT_MAX, "0");

    for (int i = 0; i < ANALYSIS_TRANSMIT_FIELD_COUNT; i++) {
        Global_Analysis_Transmit_Field_Cursor[i] = (int)strlen(Global_Analysis_Transmit_Field_Text[i]);
    }

    Global_Analysis_Transmit_Config_Active_Field = ANALYSIS_TRANSMIT_FIELD_FREQUENCY;
    Global_Analysis_Transmit_Config_Status[0] = '\0';
}

static void ANALYSIS_open_transmit_config_prompt(void) {
    /*
        Purpose: Opens explicit transmission settings before password confirmation
        Returns: No value
    */

    const char *username = AUTH_get_current_username();
    double progress = 0.0;
    int active = 0;
    int result_ready = 0;
    int succeeded = 0;
    char result[256] = "";

    (void)RETROSPECTRUM_get_transmission_status(&progress, &active, &result_ready, &succeeded, result, sizeof(result));

    if (active) {

        Global_Analysis_Transmit_Progress_Prompt_Open = 1;
        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "A transmission is already active");
        return;

    }

    if (result_ready) {

        Global_Analysis_Transmit_Result_Prompt_Open = 1;
        Global_Analysis_Transmit_Result_Succeeded = succeeded;
        snprintf(Global_Analysis_Transmit_Result_Message, sizeof(Global_Analysis_Transmit_Result_Message), "%s",
                 result[0] ? result : (succeeded ? "Transmission succeeded." : "Transmission failed."));
        return;

    }

    ANALYSIS_close_transmit_prompts();

    if (!username || username[0] == '\0') {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                 "Unable to authorize transmission: no authenticated username is available");
        return;

    }

    if (Global_Analysis_Path[0] == '\0' || Global_Analysis_IQ_Count == 0) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Open an IQ recording before transmitting");
        return;

    }

    ANALYSIS_prefill_transmit_fields();
    Global_Analysis_Transmit_Config_Prompt_Open = 1;
    Global_Analysis_Signal_Menu_Open = 0;
    Global_Analysis_Multithread_Prompt_Open = 0;
    Global_Analysis_Delete_Confirm_Open = 0;
    Global_Analysis_Dragging = 0;
    Global_Analysis_Filter_Selecting = 0;
    Global_Analysis_Column_Selecting = 0;
    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Configure transmission settings");
}

static void ANALYSIS_open_transmit_auth_prompt(void) {
    /*
        Purpose: Opens password confirmation after transmission settings are validated
        Returns: No value
    */

    const char *username = AUTH_get_current_username();

    if (!username || username[0] == '\0') {

        snprintf(Global_Analysis_Transmit_Config_Status, sizeof(Global_Analysis_Transmit_Config_Status),
                 "No authenticated username is available.");
        Global_Analysis_Transmit_Config_Prompt_Open = 1;
        return;

    }

    ANALYSIS_secure_clear(Global_Analysis_Transmit_Password, sizeof(Global_Analysis_Transmit_Password));
    Global_Analysis_Transmit_Password_Cursor = 0;
    Global_Analysis_Transmit_Auth_Status[0] = '\0';
    Global_Analysis_Transmit_Config_Prompt_Open = 0;
    Global_Analysis_Transmit_Auth_Prompt_Open = 1;
    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Password confirmation required to start transmission");
}

static void ANALYSIS_authorize_transmission(void) {
    /*
        Purpose: Verifies the current user's password and starts the configured transmission
        Returns: No value
    */

    char error[256] = "";

    if (Global_Analysis_Transmit_Password[0] == '\0') {

        snprintf(Global_Analysis_Transmit_Auth_Status, sizeof(Global_Analysis_Transmit_Auth_Status),
                 "Enter your password.");
        return;

    }

    if (!AUTH_verify_current_password(Global_Analysis_Transmit_Password, error, sizeof(error))) {

        ANALYSIS_secure_clear(Global_Analysis_Transmit_Password, sizeof(Global_Analysis_Transmit_Password));
        Global_Analysis_Transmit_Password_Cursor = 0;
        snprintf(Global_Analysis_Transmit_Auth_Status, sizeof(Global_Analysis_Transmit_Auth_Status), "%s",
                 error[0] ? error : "Invalid password.");
        return;

    }

    ANALYSIS_secure_clear(Global_Analysis_Transmit_Password, sizeof(Global_Analysis_Transmit_Password));
    Global_Analysis_Transmit_Password_Cursor = 0;
    Global_Analysis_Transmit_Auth_Status[0] = '\0';
    Global_Analysis_Transmit_Auth_Prompt_Open = 0;
    ANALYSIS_submit_transmission_settings(1);
}

static int ANALYSIS_parse_transmit_integer(int field, const char *label, uint64_t minimum, uint64_t maximum,
                                           uint64_t *value) {
    /*
        Purpose: Parses and validates an unsigned integer transmission field
        Returns: Validation status
    */

    char *end = NULL;
    unsigned long long parsed;
    const char *text;

    if (field < 0 || field >= ANALYSIS_TRANSMIT_FIELD_COUNT || !value) {

        return 0;

    }

    text = Global_Analysis_Transmit_Field_Text[field];
    errno = 0;
    parsed = strtoull(text, &end, 10);

    if (errno != 0 || !end || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {

        snprintf(Global_Analysis_Transmit_Config_Status, sizeof(Global_Analysis_Transmit_Config_Status),
                 "%s must be from %llu to %llu.", label, (unsigned long long)minimum, (unsigned long long)maximum);
        Global_Analysis_Transmit_Config_Active_Field = field;
        return 0;

    }

    *value = (uint64_t)parsed;
    return 1;
}

static int ANALYSIS_parse_transmit_signed_integer(int field, const char *label, int64_t minimum, int64_t maximum,
                                                  int64_t *value) {
    /*
        Purpose: Parses and validates a signed integer transmission field
        Returns: Validation status
    */

    char *end = NULL;
    long long parsed;
    const char *text;

    if (field < 0 || field >= ANALYSIS_TRANSMIT_FIELD_COUNT || !value) {

        return 0;

    }

    text = Global_Analysis_Transmit_Field_Text[field];
    errno = 0;
    parsed = strtoll(text, &end, 10);

    if (errno != 0 || !end || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {

        snprintf(Global_Analysis_Transmit_Config_Status, sizeof(Global_Analysis_Transmit_Config_Status),
                 "%s must be from %lld to %lld.", label, (long long)minimum, (long long)maximum);
        Global_Analysis_Transmit_Config_Active_Field = field;
        return 0;

    }

    *value = (int64_t)parsed;
    return 1;
}

static void ANALYSIS_submit_transmission_settings(int password_verified) {
    /*
        Purpose: Validates explicit RF settings, then requests password confirmation or starts SoapySDR TX
        Returns: No value
    */

    uint64_t frequency_hz = 0;
    uint64_t sample_rate_sps = 0;
    uint64_t bandwidth_hz = 0;
    int64_t gain_db = 0;
    uint64_t repeat_count = 0;
    char error[256] = "";

    if (Global_Analysis_Path[0] == '\0' || Global_Analysis_IQ_Count == 0) {

        snprintf(Global_Analysis_Transmit_Config_Status, sizeof(Global_Analysis_Transmit_Config_Status),
                 "The selected IQ recording is no longer available.");
        Global_Analysis_Transmit_Auth_Prompt_Open = 0;
        Global_Analysis_Transmit_Config_Prompt_Open = 1;
        return;

    }

    if (!ANALYSIS_parse_transmit_integer(ANALYSIS_TRANSMIT_FIELD_FREQUENCY, "Frequency", 1ULL, 1000000000000ULL,
                                         &frequency_hz) ||
        !ANALYSIS_parse_transmit_integer(ANALYSIS_TRANSMIT_FIELD_SAMPLE_RATE, "Sample rate", 1000ULL, UINT32_MAX,
                                         &sample_rate_sps) ||
        !ANALYSIS_parse_transmit_integer(ANALYSIS_TRANSMIT_FIELD_BANDWIDTH, "Bandwidth", 0ULL, UINT32_MAX,
                                         &bandwidth_hz) ||
        !ANALYSIS_parse_transmit_signed_integer(ANALYSIS_TRANSMIT_FIELD_GAIN, "TX gain", -200, 200, &gain_db) ||
        !ANALYSIS_parse_transmit_integer(ANALYSIS_TRANSMIT_FIELD_REPEAT, "Repeat count", 0ULL, 100ULL, &repeat_count)) {

        if (password_verified) {

            Global_Analysis_Transmit_Auth_Prompt_Open = 0;
            Global_Analysis_Transmit_Config_Prompt_Open = 1;

        }
        return;

    }

    if (bandwidth_hz > sample_rate_sps) {

        snprintf(Global_Analysis_Transmit_Config_Status, sizeof(Global_Analysis_Transmit_Config_Status),
                 "Bandwidth cannot exceed the sample rate.");
        Global_Analysis_Transmit_Config_Active_Field = ANALYSIS_TRANSMIT_FIELD_BANDWIDTH;

        if (password_verified) {

            Global_Analysis_Transmit_Auth_Prompt_Open = 0;
            Global_Analysis_Transmit_Config_Prompt_Open = 1;

        }
        return;

    }

    if (!password_verified) {

        ANALYSIS_open_transmit_auth_prompt();
        return;

    }

    if (!RETROSPECTRUM_start_file_transmission(Global_Analysis_Path, frequency_hz, (uint32_t)sample_rate_sps,
                                               (uint32_t)bandwidth_hz, (int)gain_db, (unsigned int)repeat_count, error,
                                               sizeof(error))) {

        snprintf(Global_Analysis_Transmit_Config_Status, sizeof(Global_Analysis_Transmit_Config_Status), "%s",
                 error[0] ? error : "Unable to start SoapySDR transmission.");
        Global_Analysis_Transmit_Auth_Prompt_Open = 0;
        Global_Analysis_Transmit_Config_Prompt_Open = 1;
        return;

    }

    Global_Analysis_Transmit_Auth_Prompt_Open = 0;
    Global_Analysis_Transmit_Config_Prompt_Open = 0;
    Global_Analysis_Transmit_Progress_Prompt_Open = 1;
    Global_Analysis_Transmit_Progress = 0.0;
    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Transmitting selected IQ recording");
}

static void ANALYSIS_update_transmission_state(void) {
    /*
        Purpose: Synchronizes the Analysis UI with the asynchronous SoapySDR TX state
        Returns: No value
    */

    double progress = 0.0;
    int active = 0;
    int result_ready = 0;
    int succeeded = 0;
    char message[256] = "";

    (void)RETROSPECTRUM_get_transmission_status(&progress, &active, &result_ready, &succeeded, message,
                                                sizeof(message));
    Global_Analysis_Transmit_Progress = progress;

    if (active) {

        Global_Analysis_Transmit_Progress_Prompt_Open = 1;

    }

    if (result_ready) {

        Global_Analysis_Transmit_Progress_Prompt_Open = 0;
        Global_Analysis_Transmit_Result_Prompt_Open = 1;
        Global_Analysis_Transmit_Result_Succeeded = succeeded;
        snprintf(Global_Analysis_Transmit_Result_Message, sizeof(Global_Analysis_Transmit_Result_Message), "%s",
                 message[0] ? message : (succeeded ? "Transmission succeeded." : "Transmission failed."));
        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "%s", Global_Analysis_Transmit_Result_Message);

    }
}

static void ANALYSIS_draw_transmit_auth_prompt(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the transmission password prompt
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Transmit_Auth_Prompt_Open) {

        return;

    }

    SDL_Rect panel;
    SDL_Rect password_rect;
    SDL_Rect authorize_rect;
    SDL_Rect cancel_rect;
    const char *username = AUTH_get_current_username();
    int mouse_x = 0;
    int mouse_y = 0;

    ANALYSIS_get_transmit_auth_rects(win_w, win_h, &panel, &password_rect, &authorize_rect, &cancel_rect);
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, (SDL_Rect){0, 0, win_w, win_h}, (SDL_Color){0, 0, 0, 175});
    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 250});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 255, 90, 255});
    draw_filled_rect(renderer, (SDL_Rect){panel.x, panel.y, panel.w, 54}, (SDL_Color){0, 24, 8, 245});
    draw_outline_rect(renderer, (SDL_Rect){panel.x, panel.y, panel.w, 54}, (SDL_Color){0, 160, 60, 230});

    draw_text(renderer, font, "Confirm Transmission", panel.x + 24, panel.y + 18, (SDL_Color){0, 255, 90, 255});
    draw_text(renderer, font, "Enter your password to start transmitting with these settings.", panel.x + 24,
              panel.y + 76, (SDL_Color){220, 220, 220, 255});
    draw_text(renderer, font, "Username", panel.x + 28, panel.y + 112, (SDL_Color){0, 155, 65, 255});
    draw_text(renderer, font, username && username[0] ? username : "Unavailable", panel.x + 126, panel.y + 112,
              (SDL_Color){0, 255, 90, 255});
    draw_text(renderer, font, "Password", panel.x + 28, panel.y + 148, (SDL_Color){0, 155, 65, 255});
    ANALYSIS_draw_transmit_text_field(renderer, font, password_rect, Global_Analysis_Transmit_Password, 1, 1,
                                      Global_Analysis_Transmit_Password_Cursor);

    if (Global_Analysis_Transmit_Auth_Status[0]) {

        draw_text(renderer, font, Global_Analysis_Transmit_Auth_Status, panel.x + 28, panel.y + 230,
                  (SDL_Color){255, 75, 55, 255});

    }

    draw_filled_rect(renderer, authorize_rect,
                     point_in_rect(mouse_x, mouse_y, authorize_rect) ? (SDL_Color){0, 50, 20, 255}
                                                                     : (SDL_Color){8, 18, 10, 255});
    draw_outline_rect(renderer, authorize_rect,
                      point_in_rect(mouse_x, mouse_y, authorize_rect) ? (SDL_Color){0, 255, 90, 255}
                                                                      : (SDL_Color){120, 120, 120, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, authorize_rect, "Transmit", (SDL_Color){0, 255, 90, 255});

    draw_filled_rect(renderer, cancel_rect,
                     point_in_rect(mouse_x, mouse_y, cancel_rect) ? (SDL_Color){34, 34, 34, 255}
                                                                  : (SDL_Color){12, 12, 12, 255});
    draw_outline_rect(renderer, cancel_rect,
                      point_in_rect(mouse_x, mouse_y, cancel_rect) ? (SDL_Color){230, 230, 230, 255}
                                                                   : (SDL_Color){130, 130, 130, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, cancel_rect, "Cancel", (SDL_Color){200, 200, 200, 255});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void ANALYSIS_draw_transmit_config_prompt(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws explicit frequency, sample-rate, bandwidth, gain, and repeat fields
        Returns: No value
    */

    static const char *labels[ANALYSIS_TRANSMIT_FIELD_COUNT] = {
        "Frequency (Hz)", "Sample Rate (Sps)", "Bandwidth (Hz)", "TX Gain (device-clamped dB)",
        "Repeat after first transmission (0-100; 0 = transmit once)"};
    SDL_Rect panel;
    SDL_Rect fields[ANALYSIS_TRANSMIT_FIELD_COUNT];
    SDL_Rect transmit_rect;
    SDL_Rect cancel_rect;
    int mouse_x = 0;
    int mouse_y = 0;
    const char *file = ANALYSIS_selected_file_name();
    char file_label[420];

    if (!renderer || !font || !Global_Analysis_Transmit_Config_Prompt_Open) {

        return;

    }

    ANALYSIS_get_transmit_config_rects(win_w, win_h, &panel, fields, &transmit_rect, &cancel_rect);
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);
    snprintf(file_label, sizeof(file_label), "File: %.380s", file ? file : "No recording selected");

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, (SDL_Rect){0, 0, win_w, win_h}, (SDL_Color){0, 0, 0, 175});
    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 250});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 255, 90, 255});
    draw_filled_rect(renderer, (SDL_Rect){panel.x, panel.y, panel.w, 54}, (SDL_Color){0, 24, 8, 245});
    draw_outline_rect(renderer, (SDL_Rect){panel.x, panel.y, panel.w, 54}, (SDL_Color){0, 160, 60, 230});

    draw_text(renderer, font, "Transmit IQ Recording", panel.x + 24, panel.y + 18, (SDL_Color){0, 255, 90, 255});
    ANALYSIS_draw_wrapped_text_limited(renderer, font, file_label, panel.x + 28, panel.y + 66, panel.w - 56, 19, 3,
                                       (SDL_Color){220, 220, 220, 255});

    for (int i = 0; i < ANALYSIS_TRANSMIT_FIELD_COUNT; i++) {
        char live_conversion[96];

        draw_text(renderer, font, labels[i], fields[i].x, fields[i].y - 22, (SDL_Color){0, 155, 65, 255});

        ANALYSIS_format_transmit_live_conversion(i, live_conversion, sizeof(live_conversion));

        if (live_conversion[0] != '\0') {

            int conversion_w = 0;
            int conversion_h = 0;

            (void)TTF_SizeUTF8(font, live_conversion, &conversion_w, &conversion_h);
            draw_text(renderer, font, live_conversion, fields[i].x + fields[i].w - conversion_w, fields[i].y - 22,
                      (SDL_Color){0, 170, 255, 255});

        }

        ANALYSIS_draw_transmit_text_field(renderer, font, fields[i], Global_Analysis_Transmit_Field_Text[i],
                                          Global_Analysis_Transmit_Config_Active_Field == i, 0,
                                          Global_Analysis_Transmit_Field_Cursor[i]);
    }

    if (Global_Analysis_Transmit_Config_Status[0]) {

        SDL_Color status_color = (SDL_Color){255, 85, 65, 255};
        draw_text(renderer, font, Global_Analysis_Transmit_Config_Status, panel.x + 28, panel.y + panel.h - 82,
                  status_color);

    }

    draw_filled_rect(renderer, transmit_rect,
                     point_in_rect(mouse_x, mouse_y, transmit_rect) ? (SDL_Color){0, 50, 20, 255}
                                                                    : (SDL_Color){8, 18, 10, 255});
    draw_outline_rect(renderer, transmit_rect,
                      point_in_rect(mouse_x, mouse_y, transmit_rect) ? (SDL_Color){0, 255, 90, 255}
                                                                     : (SDL_Color){120, 120, 120, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, transmit_rect, "Transmit", (SDL_Color){0, 255, 90, 255});

    draw_filled_rect(renderer, cancel_rect,
                     point_in_rect(mouse_x, mouse_y, cancel_rect) ? (SDL_Color){34, 34, 34, 255}
                                                                  : (SDL_Color){12, 12, 12, 255});
    draw_outline_rect(renderer, cancel_rect,
                      point_in_rect(mouse_x, mouse_y, cancel_rect) ? (SDL_Color){230, 230, 230, 255}
                                                                   : (SDL_Color){130, 130, 130, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, cancel_rect, "Cancel", (SDL_Color){200, 200, 200, 255});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void ANALYSIS_draw_animated_transmit_waves(SDL_Renderer *renderer, int center_x, int center_y) {
    /*
        Purpose: Draws expanding blue radio waves during active transmission
        Returns: No value
    */

    double phase = fmod((double)SDL_GetTicks64() / 1200.0, 1.0);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int ring = 0; ring < 3; ring++) {
        double ring_phase = fmod(phase + ((double)ring / 3.0), 1.0);
        double radius = 20.0 + (ring_phase * 82.0);
        Uint8 alpha = (Uint8)(235.0 * (1.0 - ring_phase));

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, alpha);

        for (int side = -1; side <= 1; side += 2) {
            int previous_x = center_x + (int)(side * radius * cos(-0.72));
            int previous_y = center_y + (int)(radius * sin(-0.72));

            for (int segment = 1; segment <= 18; segment++) {
                double angle = -0.72 + (1.44 * (double)segment / 18.0);
                int x = center_x + (int)(side * radius * cos(angle));
                int y = center_y + (int)(radius * sin(angle));

                SDL_RenderDrawLine(renderer, previous_x, previous_y, x, y);
                previous_x = x;
                previous_y = y;
            }
        }
    }
}

static void ANALYSIS_draw_transmit_progress_prompt(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the active SoapySDR transmission animation and progress
        Returns: No value
    */

    SDL_Rect panel;
    SDL_Rect abort_rect;
    SDL_Rect tower_rect;
    SDL_Rect progress_track;
    SDL_Rect progress_fill;
    int mouse_x = 0;
    int mouse_y = 0;
    char percent_text[64];

    ANALYSIS_update_transmission_state();

    if (!renderer || !font || !Global_Analysis_Transmit_Progress_Prompt_Open) {

        return;

    }

    ANALYSIS_get_transmit_progress_rects(win_w, win_h, &panel, &abort_rect);
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);
    tower_rect = (SDL_Rect){panel.x + (panel.w / 2) - 30, panel.y + 88, 60, 86};
    progress_track = (SDL_Rect){panel.x + 44, panel.y + 226, panel.w - 88, 20};
    progress_fill = progress_track;
    progress_fill.w = (int)((double)progress_track.w * Global_Analysis_Transmit_Progress);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, (SDL_Rect){0, 0, win_w, win_h}, (SDL_Color){0, 0, 0, 185});
    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 250});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 170, 255, 255});
    draw_filled_rect(renderer, (SDL_Rect){panel.x, panel.y, panel.w, 54}, (SDL_Color){0, 20, 30, 245});
    draw_outline_rect(renderer, (SDL_Rect){panel.x, panel.y, panel.w, 54}, (SDL_Color){0, 170, 255, 230});
    draw_text(renderer, font, "Transmission in Progress", panel.x + 24, panel.y + 18, (SDL_Color){0, 190, 255, 255});

    ANALYSIS_draw_transmit_shape(renderer, tower_rect, (SDL_Color){0, 210, 255, 255});
    ANALYSIS_draw_animated_transmit_waves(renderer, panel.x + (panel.w / 2), panel.y + 127);

    draw_filled_rect(renderer, progress_track, (SDL_Color){6, 24, 30, 255});
    draw_outline_rect(renderer, progress_track, (SDL_Color){0, 120, 180, 255});

    if (progress_fill.w > 0) {

        draw_filled_rect(renderer, progress_fill, (SDL_Color){0, 150, 230, 255});

    }

    snprintf(percent_text, sizeof(percent_text), "%.1f%%", Global_Analysis_Transmit_Progress * 100.0);
    draw_text(renderer, font, percent_text, panel.x + (panel.w / 2) - 24, panel.y + 258,
              (SDL_Color){210, 245, 255, 255});
    draw_text(renderer, font, "Streaming signed complex16 IQ samples to the connected SDR...", panel.x + 44,
              panel.y + 292, (SDL_Color){180, 220, 235, 255});

    draw_filled_rect(renderer, abort_rect,
                     point_in_rect(mouse_x, mouse_y, abort_rect) ? (SDL_Color){55, 20, 20, 255}
                                                                 : (SDL_Color){20, 10, 10, 255});
    draw_outline_rect(renderer, abort_rect,
                      point_in_rect(mouse_x, mouse_y, abort_rect) ? (SDL_Color){255, 90, 75, 255}
                                                                  : (SDL_Color){150, 70, 65, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, abort_rect, "Abort", (SDL_Color){255, 110, 90, 255});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void ANALYSIS_close_transmit_result(void) {
    /*
        Purpose: Closes and acknowledges the transmission result prompt
        Returns: No value
    */

    Global_Analysis_Transmit_Result_Prompt_Open = 0;
    Global_Analysis_Transmit_Result_Message[0] = '\0';
    RETROSPECTRUM_acknowledge_transmission_result();
}

static void ANALYSIS_draw_transmit_result_prompt(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the final transmission success or failure prompt
        Returns: No value
    */

    SDL_Rect panel;
    SDL_Rect close_rect;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_Color result_color;

    ANALYSIS_update_transmission_state();

    if (!renderer || !font || !Global_Analysis_Transmit_Result_Prompt_Open) {

        return;

    }

    ANALYSIS_get_transmit_result_rects(win_w, win_h, &panel, &close_rect);
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);
    result_color =
        Global_Analysis_Transmit_Result_Succeeded ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){255, 85, 65, 255};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, (SDL_Rect){0, 0, win_w, win_h}, (SDL_Color){0, 0, 0, 185});
    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 250});
    draw_outline_rect(renderer, panel, result_color);
    draw_filled_rect(renderer, (SDL_Rect){panel.x, panel.y, panel.w, 54}, (SDL_Color){0, 24, 8, 245});
    draw_outline_rect(renderer, (SDL_Rect){panel.x, panel.y, panel.w, 54}, result_color);
    draw_text(renderer, font,
              Global_Analysis_Transmit_Result_Succeeded ? "Transmission Succeeded" : "Transmission Ended", panel.x + 24,
              panel.y + 18, result_color);
    draw_text(renderer, font,
              Global_Analysis_Transmit_Result_Message[0]
                  ? Global_Analysis_Transmit_Result_Message
                  : (Global_Analysis_Transmit_Result_Succeeded ? "Transmission succeeded."
                                                               : "Transmission did not complete."),
              panel.x + 28, panel.y + 96, (SDL_Color){225, 235, 225, 255});

    draw_filled_rect(renderer, close_rect,
                     point_in_rect(mouse_x, mouse_y, close_rect) ? (SDL_Color){34, 34, 34, 255}
                                                                 : (SDL_Color){12, 12, 12, 255});
    draw_outline_rect(renderer, close_rect,
                      point_in_rect(mouse_x, mouse_y, close_rect) ? (SDL_Color){230, 230, 230, 255}
                                                                  : (SDL_Color){130, 130, 130, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, close_rect, "Close", (SDL_Color){210, 210, 210, 255});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void ANALYSIS_handle_transmit_auth_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles password authorization prompt input and cursor movement
        Returns: No value
    */

    if (!event || !Global_Analysis_Transmit_Auth_Prompt_Open) {

        return;

    }

    if (event->type == SDL_TEXTINPUT) {

        ANALYSIS_transmit_insert_text(Global_Analysis_Transmit_Password, sizeof(Global_Analysis_Transmit_Password),
                                      &Global_Analysis_Transmit_Password_Cursor, event->text.text, 0);
        Global_Analysis_Transmit_Auth_Status[0] = '\0';
        return;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (key == SDLK_ESCAPE) {

            ANALYSIS_close_transmit_prompts();
            return;

        }

        if (key == SDLK_LEFT) {

            Global_Analysis_Transmit_Password_Cursor--;
            ANALYSIS_transmit_clamp_cursor(Global_Analysis_Transmit_Password,
                                           &Global_Analysis_Transmit_Password_Cursor);
            return;

        }

        if (key == SDLK_RIGHT) {

            Global_Analysis_Transmit_Password_Cursor++;
            ANALYSIS_transmit_clamp_cursor(Global_Analysis_Transmit_Password,
                                           &Global_Analysis_Transmit_Password_Cursor);
            return;

        }

        if (key == SDLK_HOME) {

            Global_Analysis_Transmit_Password_Cursor = 0;
            return;

        }

        if (key == SDLK_END) {

            Global_Analysis_Transmit_Password_Cursor = (int)strlen(Global_Analysis_Transmit_Password);
            return;

        }

        if (key == SDLK_BACKSPACE) {

            ANALYSIS_transmit_backspace(Global_Analysis_Transmit_Password, &Global_Analysis_Transmit_Password_Cursor);
            Global_Analysis_Transmit_Auth_Status[0] = '\0';
            return;

        }

        if (key == SDLK_DELETE) {

            ANALYSIS_transmit_delete(Global_Analysis_Transmit_Password, &Global_Analysis_Transmit_Password_Cursor);
            Global_Analysis_Transmit_Auth_Status[0] = '\0';
            return;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            ANALYSIS_authorize_transmission();
            return;

        }

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        SDL_Rect panel;
        SDL_Rect password_rect;
        SDL_Rect authorize_rect;
        SDL_Rect cancel_rect;

        ANALYSIS_get_transmit_auth_rects(win_w, win_h, &panel, &password_rect, &authorize_rect, &cancel_rect);

        if (point_in_rect(event->button.x, event->button.y, password_rect)) {

            Global_Analysis_Transmit_Password_Cursor = (int)strlen(Global_Analysis_Transmit_Password);
            return;

        }

        if (point_in_rect(event->button.x, event->button.y, authorize_rect)) {

            ANALYSIS_authorize_transmission();
            return;

        }

        if (point_in_rect(event->button.x, event->button.y, cancel_rect)) {

            ANALYSIS_close_transmit_prompts();
            return;

        }

        (void)panel;

    }
}

static void ANALYSIS_handle_transmit_config_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles explicit transmission settings and field cursor movement
        Returns: No value
    */

    char *destination;
    int *cursor;

    if (!event || !Global_Analysis_Transmit_Config_Prompt_Open) {

        return;

    }

    destination = Global_Analysis_Transmit_Field_Text[Global_Analysis_Transmit_Config_Active_Field];
    cursor = &Global_Analysis_Transmit_Field_Cursor[Global_Analysis_Transmit_Config_Active_Field];

    if (event->type == SDL_TEXTINPUT) {

        int numeric_mode = Global_Analysis_Transmit_Config_Active_Field == ANALYSIS_TRANSMIT_FIELD_GAIN ? 2 : 1;

        ANALYSIS_transmit_insert_text(destination, ANALYSIS_TRANSMIT_TEXT_MAX, cursor, event->text.text, numeric_mode);
        Global_Analysis_Transmit_Config_Status[0] = '\0';
        return;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (key == SDLK_ESCAPE) {

            ANALYSIS_close_transmit_prompts();
            return;

        }

        if (key == SDLK_TAB) {

            int direction = (event->key.keysym.mod & KMOD_SHIFT) ? -1 : 1;

            Global_Analysis_Transmit_Config_Active_Field += direction;

            if (Global_Analysis_Transmit_Config_Active_Field < 0) {

                Global_Analysis_Transmit_Config_Active_Field = ANALYSIS_TRANSMIT_FIELD_COUNT - 1;

            }

            if (Global_Analysis_Transmit_Config_Active_Field >= ANALYSIS_TRANSMIT_FIELD_COUNT) {

                Global_Analysis_Transmit_Config_Active_Field = 0;

            }
            return;

        }

        if (key == SDLK_LEFT) {

            (*cursor)--;
            ANALYSIS_transmit_clamp_cursor(destination, cursor);
            return;

        }

        if (key == SDLK_RIGHT) {

            (*cursor)++;
            ANALYSIS_transmit_clamp_cursor(destination, cursor);
            return;

        }

        if (key == SDLK_HOME) {

            *cursor = 0;
            return;

        }

        if (key == SDLK_END) {

            *cursor = (int)strlen(destination);
            return;

        }

        if (key == SDLK_BACKSPACE) {

            ANALYSIS_transmit_backspace(destination, cursor);
            Global_Analysis_Transmit_Config_Status[0] = '\0';
            return;

        }

        if (key == SDLK_DELETE) {

            ANALYSIS_transmit_delete(destination, cursor);
            Global_Analysis_Transmit_Config_Status[0] = '\0';
            return;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            ANALYSIS_submit_transmission_settings(0);
            return;

        }

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        SDL_Rect panel;
        SDL_Rect fields[ANALYSIS_TRANSMIT_FIELD_COUNT];
        SDL_Rect transmit_rect;
        SDL_Rect cancel_rect;

        ANALYSIS_get_transmit_config_rects(win_w, win_h, &panel, fields, &transmit_rect, &cancel_rect);

        for (int i = 0; i < ANALYSIS_TRANSMIT_FIELD_COUNT; i++) {

            if (point_in_rect(event->button.x, event->button.y, fields[i])) {

                Global_Analysis_Transmit_Config_Active_Field = i;
                Global_Analysis_Transmit_Field_Cursor[i] = (int)strlen(Global_Analysis_Transmit_Field_Text[i]);
                return;

            }
        }

        if (point_in_rect(event->button.x, event->button.y, transmit_rect)) {

            ANALYSIS_submit_transmission_settings(0);
            return;

        }

        if (point_in_rect(event->button.x, event->button.y, cancel_rect)) {

            ANALYSIS_close_transmit_prompts();
            return;

        }

        (void)panel;

    }
}

static void ANALYSIS_handle_transmit_progress_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles cancellation of an active transmission
        Returns: No value
    */

    if (!event || !Global_Analysis_Transmit_Progress_Prompt_Open) {

        return;

    }

    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) {

        RETROSPECTRUM_cancel_file_transmission();
        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Canceling transmission...");
        return;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        SDL_Rect panel;
        SDL_Rect abort_rect;

        ANALYSIS_get_transmit_progress_rects(win_w, win_h, &panel, &abort_rect);

        if (point_in_rect(event->button.x, event->button.y, abort_rect)) {

            RETROSPECTRUM_cancel_file_transmission();
            snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Canceling transmission...");

        }

        (void)panel;

    }
}

static void ANALYSIS_handle_transmit_result_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles dismissal of the transmission result prompt
        Returns: No value
    */

    if (!event || !Global_Analysis_Transmit_Result_Prompt_Open) {

        return;

    }

    if (event->type == SDL_KEYDOWN && (event->key.keysym.sym == SDLK_ESCAPE || event->key.keysym.sym == SDLK_RETURN ||
                                       event->key.keysym.sym == SDLK_KP_ENTER)) {

        ANALYSIS_close_transmit_result();
        return;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        SDL_Rect panel;
        SDL_Rect close_rect;

        ANALYSIS_get_transmit_result_rects(win_w, win_h, &panel, &close_rect);

        if (point_in_rect(event->button.x, event->button.y, close_rect)) {

            ANALYSIS_close_transmit_result();

        }

        (void)panel;

    }
}

static void ANALYSIS_get_multithread_prompt_rects(int win_w, int win_h, SDL_Rect *panel_rect, SDL_Rect *action_rect,
                                                  SDL_Rect *close_rect) {
    /*
        Purpose: Gets the multithread prompt rects
        Returns: No value
    */

    int panel_w = 570;
    int panel_h = 230;

    if (panel_w > win_w - 60) {

        panel_w = win_w - 60;

    }

    if (panel_h > win_h - 60) {

        panel_h = win_h - 60;

    }

    if (panel_w < 470) {

        panel_w = 470;

    }

    if (panel_h < 210) {

        panel_h = 210;

    }

    SDL_Rect panel = {(win_w - panel_w) / 2, (win_h - panel_h) / 2, panel_w, panel_h};

    if (panel_rect) {

        *panel_rect = panel;

    }

    if (action_rect) {

        *action_rect = (SDL_Rect){panel.x + panel.w - 246, panel.y + panel.h - 58, 104, 36};

    }

    if (close_rect) {

        *close_rect = (SDL_Rect){panel.x + panel.w - 126, panel.y + panel.h - 58, 98, 36};

    }
}

static void ANALYSIS_draw_multithread_prompt(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the multithread prompt
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Multithread_Prompt_Open) {

        return;

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 165});

    SDL_Rect panel;
    SDL_Rect action_rect;
    SDL_Rect close_rect;
    ANALYSIS_get_multithread_prompt_rects(win_w, win_h, &panel, &action_rect, &close_rect);

    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 248});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 255, 90, 255});

    SDL_Rect title_bar = {panel.x, panel.y, panel.w, 54};
    draw_filled_rect(renderer, title_bar, (SDL_Color){0, 24, 8, 245});
    draw_outline_rect(renderer, title_bar, (SDL_Color){0, 160, 60, 230});

    draw_text(renderer, font, "Multithread IQ Loading", panel.x + 24, panel.y + 18, (SDL_Color){0, 255, 90, 255});
    draw_text(renderer, font, "Uses 10 threads to load IQ data.", panel.x + 24, panel.y + 78,
              (SDL_Color){220, 220, 220, 255});
    draw_text(renderer, font, "Faster on SSDs; not recommended for hard drives.", panel.x + 24, panel.y + 108,
              (SDL_Color){255, 180, 40, 255});

    const char *state = Global_Analysis_Multithread_Enabled ? "Current: Enabled" : "Current: Disabled";
    SDL_Color state_color =
        Global_Analysis_Multithread_Enabled ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){255, 70, 70, 255};
    draw_text(renderer, font, state, panel.x + 24, panel.y + 142, state_color);

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int action_hover = point_in_rect(mouse_x, mouse_y, action_rect);
    int close_hover = point_in_rect(mouse_x, mouse_y, close_rect);
    SDL_Color action_color =
        Global_Analysis_Multithread_Enabled ? (SDL_Color){255, 70, 70, 255} : (SDL_Color){0, 255, 90, 255};

    draw_filled_rect(renderer, action_rect, action_hover ? (SDL_Color){24, 44, 24, 255} : (SDL_Color){8, 18, 10, 255});
    draw_outline_rect(renderer, action_rect, action_hover ? action_color : (SDL_Color){120, 120, 120, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, action_rect,
                                       Global_Analysis_Multithread_Enabled ? "Disable" : "Enable", action_color);

    draw_filled_rect(renderer, close_rect, close_hover ? (SDL_Color){34, 34, 34, 255} : (SDL_Color){12, 12, 12, 255});
    draw_outline_rect(renderer, close_rect,
                      close_hover ? (SDL_Color){230, 230, 230, 255} : (SDL_Color){130, 130, 130, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, close_rect, "Close", (SDL_Color){200, 200, 200, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void ANALYSIS_set_multithread_enabled(int enabled) {
    /*
        Purpose: Sets the multithread enabled
        Returns: No value
    */

    Global_Analysis_Multithread_Enabled = enabled ? 1 : 0;
    Global_Analysis_Multithread_Prompt_Open = 0;

    if (Global_Analysis_Path[0] != '\0' && Global_Analysis_IQ_Count > 0) {

        Global_Analysis_Loading = 1;
        Global_Analysis_Dirty = 1;

    }

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "10-thread IQ loading %s",
             Global_Analysis_Multithread_Enabled ? "enabled" : "disabled");
}

static void ANALYSIS_handle_multithread_prompt_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the multithread prompt event
        Returns: No value
    */

    if (!event || !Global_Analysis_Multithread_Prompt_Open) {

        return;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (key == SDLK_ESCAPE) {

            Global_Analysis_Multithread_Prompt_Open = 0;
            return;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            ANALYSIS_set_multithread_enabled(!Global_Analysis_Multithread_Enabled);
            return;

        }

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        SDL_Rect panel;
        SDL_Rect action_rect;
        SDL_Rect close_rect;
        ANALYSIS_get_multithread_prompt_rects(win_w, win_h, &panel, &action_rect, &close_rect);

        if (point_in_rect(event->button.x, event->button.y, action_rect)) {

            ANALYSIS_set_multithread_enabled(!Global_Analysis_Multithread_Enabled);
            return;

        }

        if (point_in_rect(event->button.x, event->button.y, close_rect)) {

            Global_Analysis_Multithread_Prompt_Open = 0;
            return;

        }

        (void)panel;

    }
}

static void ANALYSIS_get_delete_confirm_rects(int win_w, int win_h, SDL_Rect *panel_rect, SDL_Rect *yes_rect,
                                              SDL_Rect *no_rect) {
    /*
        Purpose: Gets the delete confirm rects
        Returns: No value
    */

    int panel_w = 640;
    int panel_h = 300;

    if (panel_w > win_w - 60) {

        panel_w = win_w - 60;

    }

    if (panel_h > win_h - 60) {

        panel_h = win_h - 60;

    }

    if (panel_w < 520) {

        panel_w = 520;

    }

    if (panel_h < 260) {

        panel_h = 260;

    }

    SDL_Rect panel = {(win_w - panel_w) / 2, (win_h - panel_h) / 2, panel_w, panel_h};

    if (panel_rect) {

        *panel_rect = panel;

    }

    if (yes_rect) {

        *yes_rect = (SDL_Rect){panel.x + panel.w - 236, panel.y + panel.h - 58, 92, 36};

    }

    if (no_rect) {

        *no_rect = (SDL_Rect){panel.x + panel.w - 124, panel.y + panel.h - 58, 92, 36};

    }
}

static void ANALYSIS_draw_delete_confirm_menu(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the delete confirm menu
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Delete_Confirm_Open) {

        return;

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 155});

    SDL_Rect panel;
    SDL_Rect yes_rect;
    SDL_Rect no_rect;

    ANALYSIS_get_delete_confirm_rects(win_w, win_h, &panel, &yes_rect, &no_rect);

    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 245});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 255, 90, 255});

    SDL_Rect title_bar = {panel.x, panel.y, panel.w, 54};
    draw_filled_rect(renderer, title_bar, (SDL_Color){0, 24, 8, 245});
    draw_outline_rect(renderer, title_bar, (SDL_Color){0, 160, 60, 230});

    draw_text(renderer, font, "Delete Recording", panel.x + 24, panel.y + 18, (SDL_Color){255, 80, 80, 255});

    draw_text(renderer, font, "Are you sure you want to delete the following file?", panel.x + 24, panel.y + 74,
              (SDL_Color){0, 255, 90, 255});

    ANALYSIS_draw_wrapped_text(renderer, font, Global_Analysis_Delete_Confirm_File, panel.x + 24, panel.y + 108,
                               panel.w - 48, 20, (SDL_Color){190, 220, 190, 255});

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int yes_hover = point_in_rect(mouse_x, mouse_y, yes_rect);
    int no_hover = point_in_rect(mouse_x, mouse_y, no_rect);

    if (yes_hover) {

        SDL_Rect glow = {yes_rect.x - 3, yes_rect.y - 3, yes_rect.w + 6, yes_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){255, 40, 40, 40});
        draw_outline_rect(renderer, glow, (SDL_Color){255, 70, 70, 180});

    }

    draw_filled_rect(renderer, yes_rect, yes_hover ? (SDL_Color){78, 0, 0, 255} : (SDL_Color){44, 0, 0, 255});
    draw_outline_rect(renderer, yes_rect, yes_hover ? (SDL_Color){255, 120, 120, 255} : (SDL_Color){255, 70, 70, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, yes_rect, "Yes", (SDL_Color){255, 70, 70, 255});

    if (no_hover) {

        SDL_Rect glow = {no_rect.x - 3, no_rect.y - 3, no_rect.w + 6, no_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){220, 220, 220, 34});
        draw_outline_rect(renderer, glow, (SDL_Color){230, 230, 230, 170});

    }

    draw_filled_rect(renderer, no_rect, no_hover ? (SDL_Color){34, 34, 34, 255} : (SDL_Color){12, 12, 12, 255});
    draw_outline_rect(renderer, no_rect, no_hover ? (SDL_Color){230, 230, 230, 255} : (SDL_Color){130, 130, 130, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, no_rect, "No", (SDL_Color){190, 190, 190, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void ANALYSIS_open_delete_confirm(void) {
    /*
        Purpose: Opens the delete confirm
        Returns: No value
    */

    const char *name = ANALYSIS_selected_file_name();

    if (!name || name[0] == '\0') {

        return;

    }

    snprintf(Global_Analysis_Delete_Confirm_File, sizeof(Global_Analysis_Delete_Confirm_File), "%s", name);
    snprintf(Global_Analysis_Delete_Confirm_Path, sizeof(Global_Analysis_Delete_Confirm_Path), "%s/%s",
             Global_Analysis_Record_Dir, name);
    Global_Analysis_Delete_Confirm_Open = 1;
    Global_Analysis_Signal_Menu_Open = 0;
    Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
    ANALYSIS_signal_clear_file_selection();
}

static int ANALYSIS_delete_confirmed_file(void) {
    /*
        Purpose: Deletes the confirmed file
        Returns: Success status
    */

    if (!Global_Analysis_Delete_Confirm_Open || Global_Analysis_Delete_Confirm_Path[0] == '\0') {

        return 0;

    }

    char deleted_name[512];
    char deleted_path[1024];

    snprintf(deleted_name, sizeof(deleted_name), "%s", Global_Analysis_Delete_Confirm_File);
    snprintf(deleted_path, sizeof(deleted_path), "%s", Global_Analysis_Delete_Confirm_Path);

    if (remove(deleted_path) != 0) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Failed to delete %.160s", deleted_name);
        Global_Analysis_Delete_Confirm_Open = 0;
        Global_Analysis_Delete_Confirm_File[0] = '\0';
        Global_Analysis_Delete_Confirm_Path[0] = '\0';
        return 0;

    }

    Global_Analysis_Delete_Confirm_Open = 0;
    Global_Analysis_Delete_Confirm_File[0] = '\0';
    Global_Analysis_Delete_Confirm_Path[0] = '\0';

    if (strcmp(Global_Analysis_Path, deleted_path) == 0) {

        ANALYSIS_clear_loaded_file();

    }

    ANALYSIS_scan_recordings();

    if (Global_Analysis_File_Count > 0 && Global_Analysis_Selected >= Global_Analysis_File_Count) {

        Global_Analysis_Selected = Global_Analysis_File_Count - 1;

    }

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Deleted %.160s", deleted_name);
    Global_Analysis_Dirty = 1;
    return 1;
}

static void ANALYSIS_handle_delete_confirm_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the delete confirm event
        Returns: No value
    */

    if (!event || !Global_Analysis_Delete_Confirm_Open) {

        return;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (key == SDLK_ESCAPE) {

            Global_Analysis_Delete_Confirm_Open = 0;
            Global_Analysis_Delete_Confirm_File[0] = '\0';
            Global_Analysis_Delete_Confirm_Path[0] = '\0';
            return;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            ANALYSIS_delete_confirmed_file();
            return;

        }

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        SDL_Rect panel;
        SDL_Rect yes_rect;
        SDL_Rect no_rect;

        ANALYSIS_get_delete_confirm_rects(win_w, win_h, &panel, &yes_rect, &no_rect);

        if (point_in_rect(event->button.x, event->button.y, yes_rect)) {

            ANALYSIS_delete_confirmed_file();
            return;

        }

        if (point_in_rect(event->button.x, event->button.y, no_rect)) {

            Global_Analysis_Delete_Confirm_Open = 0;
            Global_Analysis_Delete_Confirm_File[0] = '\0';
            Global_Analysis_Delete_Confirm_Path[0] = '\0';
            return;

        }

        (void)panel;
        return;

    }
}

static void ANALYSIS_draw_signal_menu(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the signal menu
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Signal_Menu_Open) {

        return;

    }

    Global_Analysis_Signal_Last_Font = font;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 155});

    SDL_Rect panel;
    SDL_Rect field_rects[ANALYSIS_SIGNAL_FIELD_COUNT];
    SDL_Rect save_rect;
    SDL_Rect close_rect;

    ANALYSIS_get_signal_menu_rects(win_w, win_h, &panel, field_rects, &save_rect, &close_rect);

    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 245});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 255, 90, 255});

    SDL_Rect title_bar = {panel.x, panel.y, panel.w, 54};
    draw_filled_rect(renderer, title_bar, (SDL_Color){0, 24, 8, 245});
    draw_outline_rect(renderer, title_bar, (SDL_Color){0, 160, 60, 230});

    draw_text(renderer, font, "Signal File Settings", panel.x + 24, panel.y + 18, (SDL_Color){0, 255, 90, 255});

    char file_label[ANALYSIS_SIGNAL_TEXT_MAX + 16];

    snprintf(file_label, sizeof(file_label), "Output: %s",
             Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD][0]
                 ? Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]
                 : "");

    ANALYSIS_draw_wrapped_text(renderer, font, file_label, panel.x + 24, panel.y + 66, panel.w - 48, 19,
                               (SDL_Color){190, 220, 190, 255});

    draw_text(renderer, font, "Save New creates a new .complex16 file and opens it for every graph.", panel.x + 24,
              panel.y + 180, (SDL_Color){130, 170, 130, 255});

    for (int i = 0; i < ANALYSIS_SIGNAL_FIELD_COUNT; i++) {
        SDL_Rect r = field_rects[i];
        int active = (Global_Analysis_Signal_Active_Field == i);

        draw_text(renderer, font, ANALYSIS_SIGNAL_FIELD_LABELS[i], r.x, r.y - 22, (SDL_Color){0, 210, 70, 255});

        draw_filled_rect(renderer, r, (SDL_Color){0, 0, 0, 255});
        draw_outline_rect(renderer, r, active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 105, 42, 230});

        if (i == ANALYSIS_SIGNAL_FILENAME_FIELD) {

            ANALYSIS_signal_draw_filename_field_text(renderer, font, r, active);

        }

        else {

            char visible_text[ANALYSIS_SIGNAL_TEXT_MAX + 4];

            if (Global_Analysis_Signal_Field_Text[i][0] != '\0') {

                ANALYSIS_short_text(font, Global_Analysis_Signal_Field_Text[i], visible_text, sizeof(visible_text),
                                    r.w - 18);

            }

            else {

                snprintf(visible_text, sizeof(visible_text), "%s", active ? "_" : "Click to type");

            }

            if (active && Global_Analysis_Signal_Field_Text[i][0] != '\0') {

                size_t len = strlen(visible_text);

                if (len + 1 < sizeof(visible_text)) {

                    visible_text[len] = '_';
                    visible_text[len + 1] = '\0';

                }

            }

            draw_text(renderer, font, visible_text, r.x + 8, r.y + 10,
                      Global_Analysis_Signal_Field_Text[i][0] != '\0' || active ? (SDL_Color){0, 255, 90, 255}
                                                                                : (SDL_Color){95, 130, 95, 255});

        }
    }

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    SDL_Rect start_marker_rect;
    SDL_Rect end_marker_rect;

    ANALYSIS_get_signal_marker_rects(field_rects, &start_marker_rect, &end_marker_rect);

    int marker_enabled = Global_Analysis_Marker_Active;
    int start_marker_hover = marker_enabled && point_in_rect(mouse_x, mouse_y, start_marker_rect);
    int end_marker_hover = marker_enabled && point_in_rect(mouse_x, mouse_y, end_marker_rect);

    ANALYSIS_draw_signal_marker_button(renderer, font, start_marker_rect, marker_enabled, start_marker_hover);
    ANALYSIS_draw_signal_marker_button(renderer, font, end_marker_rect, marker_enabled, end_marker_hover);

    int save_hover = point_in_rect(mouse_x, mouse_y, save_rect);
    int close_hover = point_in_rect(mouse_x, mouse_y, close_rect);

    if (save_hover) {

        SDL_Rect glow = {save_rect.x - 3, save_rect.y - 3, save_rect.w + 6, save_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 40});
        draw_outline_rect(renderer, glow, (SDL_Color){0, 255, 90, 180});

    }

    draw_filled_rect(renderer, save_rect, save_hover ? (SDL_Color){0, 78, 28, 255} : (SDL_Color){0, 48, 18, 255});
    draw_outline_rect(renderer, save_rect, save_hover ? (SDL_Color){120, 255, 160, 255} : (SDL_Color){0, 255, 90, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, save_rect, "Save New", (SDL_Color){0, 255, 90, 255});

    if (close_hover) {

        SDL_Rect glow = {close_rect.x - 3, close_rect.y - 3, close_rect.w + 6, close_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){220, 220, 220, 34});
        draw_outline_rect(renderer, glow, (SDL_Color){230, 230, 230, 170});

    }

    draw_filled_rect(renderer, close_rect, close_hover ? (SDL_Color){34, 34, 34, 255} : (SDL_Color){12, 12, 12, 255});
    draw_outline_rect(renderer, close_rect,
                      close_hover ? (SDL_Color){230, 230, 230, 255} : (SDL_Color){130, 130, 130, 255});
    ANALYSIS_draw_centered_button_text(renderer, font, close_rect, "Close", (SDL_Color){230, 230, 230, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static int ANALYSIS_signal_parse_double_field(int index, const char *label, double min_value, double max_value,
                                              double *out) {
    /*
        Purpose: Parses the signal double field
        Returns: Field index
    */

    if (!out || index < 0 || index >= ANALYSIS_SIGNAL_FIELD_COUNT) {

        return 0;

    }

    const char *text = Global_Analysis_Signal_Field_Text[index];

    if (!text || text[0] == '\0') {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "%s is empty", label);
        return 0;

    }

    char cleaned[ANALYSIS_SIGNAL_TEXT_MAX];

    if (!sec_strcpy(cleaned, sizeof(cleaned), text)) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "%s is too long", label);
        return 0;

    }

    size_t cleaned_len = strlen(cleaned);

    while (cleaned_len > 0 && (cleaned[cleaned_len - 1] == ' ' || cleaned[cleaned_len - 1] == '\t')) {
        cleaned[--cleaned_len] = '\0';
    }

    double value = 0.0;

    if (!sec_str_to_double(cleaned, &value)) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "%s has invalid characters", label);
        return 0;

    }

    if (!sec_str_to_double_bound(cleaned, min_value, max_value, &value)) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "%s is out of range", label);
        return 0;

    }

    *out = value;
    return 1;
}

static void ANALYSIS_signal_make_base_name(const char *src, char *out, size_t out_size) {
    /*
        Purpose: Builds the signal base name
        Returns: No value
    */

    if (!out || out_size == 0) {

        return;

    }

    const char *name = src ? src : "signal";
    const char *slash = strrchr(name, '/');

    if (slash) {

        name = slash + 1;

    }

    snprintf(out, out_size, "%s", name[0] ? name : "signal");

    const char *suffix = ".complex16";
    size_t len = strlen(out);
    size_t suffix_len = strlen(suffix);

    if (len >= suffix_len && strcmp(out + len - suffix_len, suffix) == 0) {

        out[len - suffix_len] = '\0';

    }

    for (size_t i = 0; out[i] != '\0'; i++) {

        if (out[i] == '/' || out[i] == '\\' || out[i] == ':' || out[i] == '*' || out[i] == '?' || out[i] == '"' ||
            out[i] == '<' || out[i] == '>' || out[i] == '|') {

            out[i] = '_';

        }
    }
}

static void ANALYSIS_signal_sanitize_output_filename(const char *src, char *out, size_t out_size) {
    /*
        Purpose: Sanitizes the signal output filename
        Returns: No value
    */

    if (!out || out_size == 0) {

        return;

    }

    const char *name = src ? src : "signal";
    const char *slash = strrchr(name, '/');

    if (slash) {

        name = slash + 1;

    }

    const char *backslash = strrchr(name, '\\');

    if (backslash) {

        name = backslash + 1;

    }

    snprintf(out, out_size, "%s", name[0] ? name : "signal");

    for (size_t i = 0; out[i] != '\0'; i++) {

        if (out[i] == '/' || out[i] == '\\' || out[i] == ':' || out[i] == '*' || out[i] == '?' || out[i] == '"' ||
            out[i] == '<' || out[i] == '>' || out[i] == '|') {

            out[i] = '_';

        }
    }

    const char *suffix = ".complex16";
    size_t len = strlen(out);
    size_t suffix_len = strlen(suffix);

    if (len < suffix_len || strcmp(out + len - suffix_len, suffix) != 0) {

        snprintf(out + len, out_size > len ? out_size - len : 0, "%s", suffix);

    }
}

static void ANALYSIS_signal_copy_component(const char *src, char *dst, size_t dst_size, size_t max_chars) {
    /*
        Purpose: Copies the signal component
        Returns: No value
    */

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src || src[0] == '\0') {

        src = "0";

    }

    size_t len = strlen(src);

    if (len > max_chars) {

        len = max_chars;

    }

    if (len >= dst_size) {

        len = dst_size - 1;

    }

    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void ANALYSIS_signal_build_live_filename(char *out, size_t out_size) {
    /*
        Purpose: Builds the signal live filename
        Returns: No value
    */

    if (!out || out_size == 0) {

        return;

    }

    char base[192];
    char center[96];
    char bw[96];
    char sr[96];
    char start[96];
    char end[96];
    char decimation[64];
    char raw[2048];

    ANALYSIS_signal_make_base_name(Global_Analysis_Signal_Menu_File, base, sizeof(base));
    ANALYSIS_signal_copy_component(Global_Analysis_Signal_Field_Text[0], center, sizeof(center), 80);
    ANALYSIS_signal_copy_component(Global_Analysis_Signal_Field_Text[1], bw, sizeof(bw), 80);
    ANALYSIS_signal_copy_component(Global_Analysis_Signal_Field_Text[2], sr, sizeof(sr), 80);
    ANALYSIS_signal_copy_component(Global_Analysis_Signal_Field_Text[3], start, sizeof(start), 80);
    ANALYSIS_signal_copy_component(Global_Analysis_Signal_Field_Text[4], end, sizeof(end), 80);
    ANALYSIS_signal_copy_component(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_DECIMATION_FIELD], decimation,
                                   sizeof(decimation), 40);

    snprintf(raw, sizeof(raw),
             "%.150s_NEW_%.80ss_%.80ss_CAPTURE_%.80sMHz_BW_%.80skHz_SR_%.80sk_"
             "Decimation_%.40s.complex16",
             base, start, end, center, bw, sr, decimation);

    ANALYSIS_signal_sanitize_output_filename(raw, out, out_size);
}

static void ANALYSIS_signal_refresh_filename_if_auto(void) {
    /*
        Purpose: Refreshes the automatic signal filename
        Returns: No value
    */

    if (Global_Analysis_Signal_File_Manual_Edit) {

        return;

    }

    ANALYSIS_signal_build_live_filename(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD],
                                        ANALYSIS_SIGNAL_TEXT_MAX);
    Global_Analysis_Signal_File_Cursor = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);
    ANALYSIS_signal_clear_file_selection();
}

static void ANALYSIS_draw_centered_button_text(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *text,
                                               SDL_Color color) {
    /*
        Purpose: Draws the centered button text
        Returns: No value
    */

    if (!renderer || !font || !text) {

        return;

    }

    int text_w = 0;
    int text_h = 0;

    if (TTF_SizeText(font, text, &text_w, &text_h) != 0) {

        text_w = 0;
        text_h = 0;

    }

    draw_text(renderer, font, text, rect.x + (rect.w - text_w) / 2, rect.y + (rect.h - text_h) / 2, color);
}

static int ANALYSIS_signal_copy_crop(const char *src_path, const char *dst_path, size_t start_sample,
                                     size_t end_sample) {
    /*
        Purpose: Copies the signal crop
        Returns: Success status
    */

    if (!src_path || !dst_path || end_sample <= start_sample) {

        return 0;

    }

    FILE *src = NULL;
    size_t source_iq_count = 0;

    if (!sec_fopen_complex16(src_path, &src, &source_iq_count) || end_sample > source_iq_count) {

        if (src) {

            fclose(src);

        }
        return 0;

    }

    FILE *dst = fopen(dst_path, "wb");

    if (!dst) {

        fclose(src);
        return 0;

    }

    size_t bytes_per_iq = sizeof(int16_t) * 2;
    size_t offset_bytes = start_sample * bytes_per_iq;
    size_t bytes_left = (end_sample - start_sample) * bytes_per_iq;

    if (fseek(src, (long)offset_bytes, SEEK_SET) != 0) {

        fclose(src);
        fclose(dst);
        remove(dst_path);
        return 0;

    }

    uint8_t buffer[65536];

    while (bytes_left > 0) {
        size_t want = bytes_left < sizeof(buffer) ? bytes_left : sizeof(buffer);
        size_t got = fread(buffer, 1, want, src);

        if (got == 0) {

            break;

        }

        if (fwrite(buffer, 1, got, dst) != got) {

            fclose(src);
            fclose(dst);
            remove(dst_path);
            return 0;

        }

        bytes_left -= got;
    }

    int ok = (bytes_left == 0);

    fclose(src);
    fclose(dst);

    if (!ok) {

        remove(dst_path);

    }

    return ok;
}

static int ANALYSIS_signal_apply_crop_settings(void) {
    /*
        Purpose: Applies the signal crop settings
        Returns: Success status
    */

    if (Global_Analysis_Path[0] == '\0' || Global_Analysis_IQ_Count == 0) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Open a recording before saving a new file");
        return 0;

    }

    double center_mhz = 0.0;
    double bandwidth_khz = 0.0;
    double sample_rate_ksps = 0.0;
    double start_sec = 0.0;
    double end_sec = 0.0;
    double decimation = 0.0;

    if (!ANALYSIS_signal_parse_double_field(0, "Center frequency", 0.000001, 1000000.0, &center_mhz) ||
        !ANALYSIS_signal_parse_double_field(1, "Bandwidth", 0.001, 1000000000.0, &bandwidth_khz) ||
        !ANALYSIS_signal_parse_double_field(2, "Sample rate", 0.001, 1000000000.0, &sample_rate_ksps) ||
        !ANALYSIS_signal_parse_double_field(3, "Start time", 0.0, 1000000000.0, &start_sec) ||
        !ANALYSIS_signal_parse_double_field(4, "End time", 0.0, 1000000000.0, &end_sec) ||
        !ANALYSIS_signal_parse_double_field(ANALYSIS_SIGNAL_DECIMATION_FIELD, "Decimation", 1.0, 1000000000.0,
                                            &decimation)) {

        return 0;

    }

    if (end_sec <= start_sec) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "End time must be after start time");
        return 0;

    }

    (void)decimation;

    double sample_rate_hz = sample_rate_ksps * 1000.0;
    size_t start_sample = (size_t)llround(start_sec * sample_rate_hz);
    size_t end_sample = (size_t)llround(end_sec * sample_rate_hz);

    if (start_sample >= Global_Analysis_IQ_Count) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Start time is beyond the file length");
        return 0;

    }

    if (end_sample > Global_Analysis_IQ_Count) {

        end_sample = Global_Analysis_IQ_Count;

    }

    if (end_sample <= start_sample) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Selected time range is empty");
        return 0;

    }

    char candidate[1024];
    char created_name[512];
    char base_no_suffix[512];

    if (Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD][0] == '\0') {

        ANALYSIS_signal_refresh_filename_if_auto();

    }

    ANALYSIS_signal_sanitize_output_filename(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD],
                                             created_name, sizeof(created_name));

    ANALYSIS_signal_make_base_name(created_name, base_no_suffix, sizeof(base_no_suffix));

    snprintf(candidate, sizeof(candidate), "%s/%s", Global_Analysis_Record_Dir, created_name);

    for (int i = 2; i < 1000; i++) {
        FILE *test = fopen(candidate, "rb");

        if (!test) {

            break;

        }

        fclose(test);

        snprintf(created_name, sizeof(created_name), "%.470s_v%d.complex16", base_no_suffix, i);
        snprintf(candidate, sizeof(candidate), "%s/%s", Global_Analysis_Record_Dir, created_name);
    }

    if (!ANALYSIS_signal_copy_crop(Global_Analysis_Path, candidate, start_sample, end_sample)) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Failed to create new complex16 file");
        return 0;

    }

    if (ANALYSIS_scan_recordings()) {

        for (int i = 0; i < Global_Analysis_File_Count; i++) {

            if (strcmp(Global_Analysis_Files[i], created_name) == 0) {

                Global_Analysis_Selected = i;
                Global_Analysis_List_Scroll = i - 2;

                if (Global_Analysis_List_Scroll < 0) {

                    Global_Analysis_List_Scroll = 0;

                }
                break;

            }
        }

        ANALYSIS_open_selected_recording();

    }

    Global_Analysis_Filter_Visible = 0;
    Global_Analysis_Filter_Selecting = 0;
    Global_Analysis_Filter_Active = 0;
    Global_Analysis_Column_Selecting = 0;
    Global_Analysis_Column_Visible = 0;
    Global_Analysis_Column_Active = 0;
    Global_Analysis_Column_X0 = 0.0;
    Global_Analysis_Column_X1 = 0.0;
    Global_Analysis_Marker_Active = 0;
    ANALYSIS_clear_noise_filter();
    Global_Analysis_Dirty = 1;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Created new file %.160s", created_name);

    snprintf(Global_Analysis_Signal_Menu_File, sizeof(Global_Analysis_Signal_Menu_File), "%s", created_name);
    snprintf(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD], ANALYSIS_SIGNAL_TEXT_MAX, "%s",
             created_name);
    Global_Analysis_Signal_File_Cursor = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);
    Global_Analysis_Signal_File_Manual_Edit = 0;
    Global_Analysis_Signal_File_Selecting = 0;
    Global_Analysis_Signal_File_Selection_Start = -1;
    Global_Analysis_Signal_File_Selection_End = -1;

    return 1;
}

static int ANALYSIS_get_current_time_range(size_t *start_sample, size_t *end_sample) {
    /*
        Purpose: Gets the current time range
        Returns: Success status
    */

    if (!start_sample || !end_sample) {

        return 0;

    }

    if (Global_Analysis_IQ_Count == 0 || Global_Analysis_View_Len == 0 || Global_Analysis_Sample_Rate <= 0.0) {

        return 0;

    }

    double x0 = 0.0;
    double x1 = 1.0;

    if (Global_Analysis_Column_Active || Global_Analysis_Column_Visible) {

        x0 = Global_Analysis_Column_X0;
        x1 = Global_Analysis_Column_X1;

        if (x1 < x0) {

            double tmp = x0;
            x0 = x1;
            x1 = tmp;

        }

        x0 = ANALYSIS_limit_double(x0, 0.0, 1.0);
        x1 = ANALYSIS_limit_double(x1, 0.0, 1.0);

    }

    *start_sample = Global_Analysis_View_Start + (size_t)(x0 * (double)Global_Analysis_View_Len);
    *end_sample = Global_Analysis_View_Start + (size_t)(x1 * (double)Global_Analysis_View_Len);

    if (*start_sample > Global_Analysis_IQ_Count) {

        *start_sample = Global_Analysis_IQ_Count;

    }

    if (*end_sample > Global_Analysis_IQ_Count) {

        *end_sample = Global_Analysis_IQ_Count;

    }

    if (*end_sample < *start_sample) {

        size_t tmp = *start_sample;
        *start_sample = *end_sample;
        *end_sample = tmp;

    }

    if (*end_sample <= *start_sample) {

        return 0;

    }

    return 1;
}

static int ANALYSIS_get_current_filter_bins(int *filter_bin_low, int *filter_bin_high) {
    /*
        Purpose: Gets the current filter bins
        Returns: Success status
    */

    if (!filter_bin_low || !filter_bin_high) {

        return 0;

    }

    *filter_bin_low = 0;
    *filter_bin_high = ANALYSIS_FFT_SIZE - 1;

    if (!Global_Analysis_Filter_Active) {

        return 0;

    }

    double y0 = Global_Analysis_Filter_Y0;
    double y1 = Global_Analysis_Filter_Y1;

    if (y1 < y0) {

        double tmp = y0;
        y0 = y1;
        y1 = tmp;

    }

    int bin_a = (int)((1.0 - y0) * (double)(ANALYSIS_FFT_SIZE - 1));
    int bin_b = (int)((1.0 - y1) * (double)(ANALYSIS_FFT_SIZE - 1));

    *filter_bin_low = bin_a < bin_b ? bin_a : bin_b;
    *filter_bin_high = bin_a > bin_b ? bin_a : bin_b;

    if (*filter_bin_low < 0) {

        *filter_bin_low = 0;

    }

    if (*filter_bin_high >= ANALYSIS_FFT_SIZE) {

        *filter_bin_high = ANALYSIS_FFT_SIZE - 1;

    }

    if (*filter_bin_high <= *filter_bin_low) {

        *filter_bin_high = *filter_bin_low + 1;

    }

    if (*filter_bin_high >= ANALYSIS_FFT_SIZE) {

        *filter_bin_high = ANALYSIS_FFT_SIZE - 1;

    }

    return 1;
}

static int ANALYSIS_noise_sample_is_muted(size_t sample_index) {
    /*
        Purpose: Checks whether the noise sample is muted
        Returns: Success status
    */

    if (!Global_Analysis_Noise_Active || Global_Analysis_Render_W <= 0 || Global_Analysis_View_Len == 0) {

        return 0;

    }

    if (sample_index < Global_Analysis_View_Start) {

        return 0;

    }

    size_t rel = sample_index - Global_Analysis_View_Start;

    if (rel > Global_Analysis_View_Len) {

        return 0;

    }

    int x = Global_Analysis_Render_W > 1
                ? (int)(((double)rel / (double)Global_Analysis_View_Len) * (double)(Global_Analysis_Render_W - 1))
                : 0;

    if (x < 0) {

        x = 0;

    }

    if (x >= Global_Analysis_Render_W) {

        x = Global_Analysis_Render_W - 1;

    }

    if (x >= ANALYSIS_MAX_RENDER_W) {

        x = ANALYSIS_MAX_RENDER_W - 1;

    }

    return Global_Analysis_Noise_Column_Mask[x] != 0;
}

static void ANALYSIS_zero_noise_samples(int16_t *iq, size_t count, size_t absolute_start_sample) {
    /*
        Purpose: Zeros the noise samples
        Returns: No value
    */

    if (!iq || count == 0 || !Global_Analysis_Noise_Active) {

        return;

    }

    for (size_t i = 0; i < count; i++) {

        if (ANALYSIS_noise_sample_is_muted(absolute_start_sample + i)) {

            iq[i * 2] = 0;
            iq[i * 2 + 1] = 0;

        }
    }
}

static int ANALYSIS_copy_crop_with_optional_noise(const char *src_path, const char *dst_path, size_t start_sample,
                                                  size_t end_sample) {
    /*
        Purpose: Copies the crop with optional noise
        Returns: Success status
    */

    if (!src_path || !dst_path || end_sample <= start_sample) {

        return 0;

    }

    FILE *src = NULL;
    size_t source_iq_count = 0;

    if (!sec_fopen_complex16(src_path, &src, &source_iq_count) || end_sample > source_iq_count) {

        if (src) {

            fclose(src);

        }
        return 0;

    }

    FILE *dst = fopen(dst_path, "wb");

    if (!dst) {

        fclose(src);
        return 0;

    }

    size_t bytes_per_iq = sizeof(int16_t) * 2;
    size_t offset_bytes = start_sample * bytes_per_iq;
    size_t samples_left = end_sample - start_sample;
    size_t absolute_sample = start_sample;

    if (fseek(src, (long)offset_bytes, SEEK_SET) != 0) {

        fclose(src);
        fclose(dst);
        remove(dst_path);
        return 0;

    }

    int16_t buffer[32768];
    size_t iq_capacity = sizeof(buffer) / (sizeof(int16_t) * 2);

    while (samples_left > 0) {
        size_t want_iq = samples_left < iq_capacity ? samples_left : iq_capacity;
        size_t got_shorts = fread(buffer, sizeof(int16_t), want_iq * 2, src);
        size_t got_iq = got_shorts / 2;

        if (got_iq == 0) {

            break;

        }

        ANALYSIS_zero_noise_samples(buffer, got_iq, absolute_sample);

        if (fwrite(buffer, sizeof(int16_t), got_iq * 2, dst) != got_iq * 2) {

            fclose(src);
            fclose(dst);
            remove(dst_path);
            return 0;

        }

        samples_left -= got_iq;
        absolute_sample += got_iq;
    }

    int ok = (samples_left == 0);

    fclose(src);
    fclose(dst);

    if (!ok) {

        remove(dst_path);

    }

    return ok;
}

static int ANALYSIS_process_crop_frequency_and_noise(const char *src_path, const char *dst_path, size_t start_sample,
                                                     size_t end_sample, int filter_bin_low, int filter_bin_high) {
    /*
        Purpose: Processes crop frequency and noise filtering
        Returns: Success status
    */

    if (!src_path || !dst_path || end_sample <= start_sample) {

        return 0;

    }

    FILE *src = NULL;
    size_t source_iq_count = 0;

    if (!sec_fopen_complex16(src_path, &src, &source_iq_count) || end_sample > source_iq_count) {

        if (src) {

            fclose(src);

        }
        return 0;

    }

    FILE *dst = fopen(dst_path, "wb");

    if (!dst) {

        fclose(src);
        return 0;

    }

    fftw_complex *freq_in = fftw_malloc(sizeof(fftw_complex) * ANALYSIS_FFT_SIZE);
    fftw_complex *freq_out = fftw_malloc(sizeof(fftw_complex) * ANALYSIS_FFT_SIZE);
    fftw_complex *time_out = fftw_malloc(sizeof(fftw_complex) * ANALYSIS_FFT_SIZE);
    int16_t *iq = malloc(sizeof(int16_t) * ANALYSIS_FFT_SIZE * 2);

    if (!freq_in || !freq_out || !time_out || !iq) {

        if (freq_in) {

            fftw_free(freq_in);

        }

        if (freq_out) {

            fftw_free(freq_out);

        }

        if (time_out) {

            fftw_free(time_out);

        }
        free(iq);
        fclose(src);
        fclose(dst);
        remove(dst_path);
        return 0;

    }

    fftw_plan forward = fftw_plan_dft_1d(ANALYSIS_FFT_SIZE, freq_in, freq_out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_plan inverse = fftw_plan_dft_1d(ANALYSIS_FFT_SIZE, freq_in, time_out, FFTW_BACKWARD, FFTW_ESTIMATE);

    if (!forward || !inverse) {

        if (forward) {

            fftw_destroy_plan(forward);

        }

        if (inverse) {

            fftw_destroy_plan(inverse);

        }
        fftw_free(freq_in);
        fftw_free(freq_out);
        fftw_free(time_out);
        free(iq);
        fclose(src);
        fclose(dst);
        remove(dst_path);
        return 0;

    }

    if (fseek(src, (long)(start_sample * 2 * sizeof(int16_t)), SEEK_SET) != 0) {

        fftw_destroy_plan(forward);
        fftw_destroy_plan(inverse);
        fftw_free(freq_in);
        fftw_free(freq_out);
        fftw_free(time_out);
        free(iq);
        fclose(src);
        fclose(dst);
        remove(dst_path);
        return 0;

    }

    size_t samples_left = end_sample - start_sample;
    size_t absolute_sample = start_sample;
    int ok = 1;

    while (samples_left > 0) {
        size_t want_iq = samples_left < ANALYSIS_FFT_SIZE ? samples_left : ANALYSIS_FFT_SIZE;
        size_t got_shorts = fread(iq, sizeof(int16_t), want_iq * 2, src);
        size_t got_iq = got_shorts / 2;

        if (got_iq == 0) {

            ok = 0;
            break;

        }

        for (int i = 0; i < ANALYSIS_FFT_SIZE; i++) {

            if ((size_t)i < got_iq) {

                freq_in[i][0] = (double)iq[i * 2] / 32768.0;
                freq_in[i][1] = (double)iq[i * 2 + 1] / 32768.0;

            }

            else {

                freq_in[i][0] = 0.0;
                freq_in[i][1] = 0.0;

            }
        }

        fftw_execute(forward);

        for (int i = 0; i < ANALYSIS_FFT_SIZE; i++) {
            freq_in[i][0] = 0.0;
            freq_in[i][1] = 0.0;
        }

        int filter_center_bin = (filter_bin_low + filter_bin_high) / 2;
        for (int y = filter_bin_low; y <= filter_bin_high; y++) {
            int src_shifted = (y + ANALYSIS_FFT_SIZE / 2) % ANALYSIS_FFT_SIZE;
            int centered_y = y - filter_center_bin + (ANALYSIS_FFT_SIZE / 2);

            if (centered_y < 0 || centered_y >= ANALYSIS_FFT_SIZE) {

                continue;

            }

            int dst_shifted = (centered_y + ANALYSIS_FFT_SIZE / 2) % ANALYSIS_FFT_SIZE;

            freq_in[dst_shifted][0] = freq_out[src_shifted][0];
            freq_in[dst_shifted][1] = freq_out[src_shifted][1];
        }

        fftw_execute(inverse);

        for (size_t i = 0; i < got_iq; i++) {
            double re = time_out[i][0] / (double)ANALYSIS_FFT_SIZE;
            double im = time_out[i][1] / (double)ANALYSIS_FFT_SIZE;

            if (ANALYSIS_noise_sample_is_muted(absolute_sample + i)) {

                re = 0.0;
                im = 0.0;

            }

            if (re > 0.999969) {

                re = 0.999969;

            }

            if (re < -1.0) {

                re = -1.0;

            }

            if (im > 0.999969) {

                im = 0.999969;

            }

            if (im < -1.0) {

                im = -1.0;

            }

            iq[i * 2] = (int16_t)lrint(re * 32767.0);
            iq[i * 2 + 1] = (int16_t)lrint(im * 32767.0);
        }

        if (fwrite(iq, sizeof(int16_t), got_iq * 2, dst) != got_iq * 2) {

            ok = 0;
            break;

        }

        samples_left -= got_iq;
        absolute_sample += got_iq;
    }

    fftw_destroy_plan(forward);
    fftw_destroy_plan(inverse);
    fftw_free(freq_in);
    fftw_free(freq_out);
    fftw_free(time_out);
    free(iq);
    fclose(src);
    fclose(dst);

    if (!ok) {

        remove(dst_path);

    }

    return ok;
}

static void ANALYSIS_build_crop_filename(char *out, size_t out_size, size_t start_sample, size_t end_sample) {
    /*
        Purpose: Builds the crop filename
        Returns: No value
    */

    if (!out || out_size == 0) {

        return;

    }

    char base[192];
    double center_hz = Global_Analysis_Center_Hz;
    double bw_hz = Global_Analysis_Sample_Rate;

    if (Global_Analysis_Filter_Active || Global_Analysis_Filter_Visible) {

        double y0 = Global_Analysis_Filter_Y0;
        double y1 = Global_Analysis_Filter_Y1;

        if (y1 < y0) {

            double tmp = y0;
            y0 = y1;
            y1 = tmp;

        }

        double center_y = (y0 + y1) * 0.5;
        center_hz = ANALYSIS_frequency_from_spec_frac(center_y);
        bw_hz = fabs(y1 - y0) * Global_Analysis_Sample_Rate;

    }

    ANALYSIS_signal_make_base_name(ANALYSIS_selected_file_name(), base, sizeof(base));

    double start_sec = Global_Analysis_Sample_Rate > 0.0 ? (double)start_sample / Global_Analysis_Sample_Rate : 0.0;
    double end_sec = Global_Analysis_Sample_Rate > 0.0 ? (double)end_sample / Global_Analysis_Sample_Rate : 0.0;

    char raw[1024];

    snprintf(raw, sizeof(raw),
             "%.150s_CROP_%.6fs_%.6fs_CAPTURE_%.6fMHz_BW_%.3fkHz_SR_%.3fk_"
             "Decimation_1.complex16",
             base, start_sec, end_sec, center_hz / 1e6, bw_hz / 1e3, Global_Analysis_Sample_Rate / 1e3);

    ANALYSIS_signal_sanitize_output_filename(raw, out, out_size);
}

static int ANALYSIS_crop_current_selection(uint32_t *pixels, int tex_w, int tex_h, SDL_Texture *texture) {
    /*
        Purpose: Crops the current analysis selection
        Returns: Success status
    */

    if (Global_Analysis_Path[0] == '\0' || Global_Analysis_IQ_Count == 0 || Global_Analysis_Sample_Rate <= 0.0) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Open a recording before cropping");
        return 0;

    }

    if (Global_Analysis_Dirty || Global_Analysis_Render_W <= 0) {

        ANALYSIS_render_workstation_data(pixels, tex_w, tex_h);

        if (texture && pixels && tex_w > 0 && tex_h > 0) {

            SDL_UpdateTexture(texture, NULL, pixels, tex_w * sizeof(uint32_t));

        }
        Global_Analysis_Dirty = 0;

    }

    if (Global_Analysis_Noise_Active) {

        ANALYSIS_update_noise_column_mask(Global_Analysis_Render_W);

    }

    size_t start_sample = 0;
    size_t end_sample = 0;

    if (!ANALYSIS_get_current_time_range(&start_sample, &end_sample)) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Selected crop range is empty");
        return 0;

    }

    char created_name[512];
    char base_no_suffix[512];
    char candidate[1024];

    ANALYSIS_build_crop_filename(created_name, sizeof(created_name), start_sample, end_sample);
    ANALYSIS_signal_make_base_name(created_name, base_no_suffix, sizeof(base_no_suffix));
    snprintf(candidate, sizeof(candidate), "%s/%s", Global_Analysis_Record_Dir, created_name);

    for (int i = 2; i < 1000; i++) {
        FILE *test = fopen(candidate, "rb");

        if (!test) {

            break;

        }

        fclose(test);
        snprintf(created_name, sizeof(created_name), "%.470s_v%d.complex16", base_no_suffix, i);
        snprintf(candidate, sizeof(candidate), "%s/%s", Global_Analysis_Record_Dir, created_name);
    }

    int filter_bin_low = 0;
    int filter_bin_high = ANALYSIS_FFT_SIZE - 1;
    int use_frequency_filter = ANALYSIS_get_current_filter_bins(&filter_bin_low, &filter_bin_high);
    int ok = 0;

    if (use_frequency_filter) {

        ok = ANALYSIS_process_crop_frequency_and_noise(Global_Analysis_Path, candidate, start_sample, end_sample,
                                                       filter_bin_low, filter_bin_high);

    }

    else {

        ok = ANALYSIS_copy_crop_with_optional_noise(Global_Analysis_Path, candidate, start_sample, end_sample);

    }

    if (!ok) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Failed to create cropped complex16 file");
        return 0;

    }

    if (ANALYSIS_scan_recordings()) {

        for (int i = 0; i < Global_Analysis_File_Count; i++) {

            if (strcmp(Global_Analysis_Files[i], created_name) == 0) {

                Global_Analysis_Selected = i;
                Global_Analysis_List_Scroll = i - 2;

                if (Global_Analysis_List_Scroll < 0) {

                    Global_Analysis_List_Scroll = 0;

                }
                break;

            }
        }

        ANALYSIS_open_selected_recording();

    }

    Global_Analysis_Filter_Visible = 0;
    Global_Analysis_Filter_Selecting = 0;
    Global_Analysis_Filter_Active = 0;
    Global_Analysis_Column_Selecting = 0;
    Global_Analysis_Column_Visible = 0;
    Global_Analysis_Column_Active = 0;
    Global_Analysis_Column_X0 = 0.0;
    Global_Analysis_Column_X1 = 0.0;
    Global_Analysis_Marker_Active = 0;
    ANALYSIS_clear_noise_filter();
    Global_Analysis_Dirty = 1;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Created cropped file %.160s", created_name);

    return 1;
}

static int ANALYSIS_handle_signal_menu_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the signal menu event
        Returns: Handling status
    */

    if (!event || (!ANALYSIS_signal_menu_available() && !Global_Analysis_Delete_Confirm_Open &&
                   !Global_Analysis_Multithread_Prompt_Open && !Global_Analysis_Transmit_Auth_Prompt_Open &&
                   !Global_Analysis_Transmit_Config_Prompt_Open && !Global_Analysis_Transmit_Progress_Prompt_Open &&
                   !Global_Analysis_Transmit_Result_Prompt_Open)) {

        return 0;

    }

    if (Global_Analysis_Transmit_Result_Prompt_Open) {

        ANALYSIS_handle_transmit_result_event(event, win_w, win_h);
        return 1;

    }

    if (Global_Analysis_Transmit_Progress_Prompt_Open) {

        ANALYSIS_handle_transmit_progress_event(event, win_w, win_h);
        return 1;

    }

    if (Global_Analysis_Transmit_Auth_Prompt_Open) {

        ANALYSIS_handle_transmit_auth_event(event, win_w, win_h);
        return 1;

    }

    if (Global_Analysis_Transmit_Config_Prompt_Open) {

        ANALYSIS_handle_transmit_config_event(event, win_w, win_h);
        return 1;

    }

    if (Global_Analysis_Multithread_Prompt_Open) {

        ANALYSIS_handle_multithread_prompt_event(event, win_w, win_h);
        return 1;

    }

    if (Global_Analysis_Delete_Confirm_Open) {

        ANALYSIS_handle_delete_confirm_event(event, win_w, win_h);
        return 1;

    }

    if (!Global_Analysis_Signal_Menu_Open) {

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

            SDL_Rect icon_rect;
            SDL_Rect thread_rect;
            SDL_Rect transmit_rect;
            SDL_Rect trash_rect;

            ANALYSIS_get_signal_icon_rect(win_w, win_h, &icon_rect);
            ANALYSIS_get_multithread_rect(win_w, win_h, &thread_rect);
            ANALYSIS_get_transmit_rect(win_w, win_h, &transmit_rect);
            ANALYSIS_get_signal_trash_rect(win_w, win_h, &trash_rect);

            if (point_in_rect(event->button.x, event->button.y, trash_rect)) {

                ANALYSIS_open_delete_confirm();
                return 1;

            }

            if (point_in_rect(event->button.x, event->button.y, thread_rect)) {

                Global_Analysis_Multithread_Prompt_Open = 1;
                Global_Analysis_Signal_Menu_Open = 0;
                Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
                ANALYSIS_signal_clear_file_selection();
                return 1;

            }

            if (point_in_rect(event->button.x, event->button.y, transmit_rect)) {

                if (Global_Analysis_Loaded_Index != Global_Analysis_Selected || Global_Analysis_Path[0] == '\0') {

                    ANALYSIS_open_selected_recording();

                }

                if (Global_Analysis_Path[0] != '\0') {

                    ANALYSIS_open_transmit_config_prompt();

                }
                return 1;

            }

            if (point_in_rect(event->button.x, event->button.y, icon_rect)) {

                if (Global_Analysis_Loaded_Index != Global_Analysis_Selected || Global_Analysis_Path[0] == '\0') {

                    ANALYSIS_open_selected_recording();

                }

                ANALYSIS_signal_menu_prefill();
                Global_Analysis_Signal_Menu_Open = 1;
                Global_Analysis_Signal_Active_Field = 0;
                Global_Analysis_Dragging = 0;
                Global_Analysis_Filter_Selecting = 0;
                Global_Analysis_Column_Selecting = 0;
                snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Signal metadata menu opened");
                return 1;

            }

        }

        return 0;

    }

    if (event->type == SDL_TEXTINPUT) {

        ANALYSIS_signal_append_text(event->text.text);
        return 1;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (key == SDLK_ESCAPE) {

            Global_Analysis_Signal_Menu_Open = 0;
            Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
            ANALYSIS_signal_clear_file_selection();
            snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Signal metadata menu closed");
            return 1;

        }

        if (key == SDLK_TAB) {

            if (Global_Analysis_Signal_Active_Field < 0) {

                Global_Analysis_Signal_Active_Field = 0;

            }

            else if (SDL_GetModState() & KMOD_SHIFT) {

                Global_Analysis_Signal_Active_Field--;

                if (Global_Analysis_Signal_Active_Field < 0) {

                    Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_COUNT - 1;

                }

            }

            else {

                Global_Analysis_Signal_Active_Field++;

                if (Global_Analysis_Signal_Active_Field >= ANALYSIS_SIGNAL_FIELD_COUNT) {

                    Global_Analysis_Signal_Active_Field = 0;

                }

            }

            return 1;

        }

        if (Global_Analysis_Signal_Active_Field == ANALYSIS_SIGNAL_FILENAME_FIELD &&
            (key == SDLK_LEFT || key == SDLK_RIGHT)) {

            int old_cursor = Global_Analysis_Signal_File_Cursor;
            int shift_down = (SDL_GetModState() & KMOD_SHIFT) != 0;

            if (shift_down && !ANALYSIS_signal_file_has_selection()) {

                Global_Analysis_Signal_File_Selection_Start = old_cursor;

            }

            if (key == SDLK_LEFT) {

                Global_Analysis_Signal_File_Cursor--;

            }

            else {

                Global_Analysis_Signal_File_Cursor++;

            }

            ANALYSIS_signal_clamp_file_cursor();

            if (shift_down) {

                if (Global_Analysis_Signal_File_Selection_Start < 0) {

                    Global_Analysis_Signal_File_Selection_Start = old_cursor;

                }

                Global_Analysis_Signal_File_Selection_End = Global_Analysis_Signal_File_Cursor;

                if (Global_Analysis_Signal_File_Selection_Start == Global_Analysis_Signal_File_Selection_End) {

                    ANALYSIS_signal_clear_file_selection();

                }

            }

            else {

                ANALYSIS_signal_clear_file_selection();

            }

            return 1;

        }

        if (key == SDLK_BACKSPACE) {

            ANALYSIS_signal_backspace_text();
            return 1;

        }

        if (key == SDLK_DELETE) {

            if (Global_Analysis_Signal_Active_Field == ANALYSIS_SIGNAL_FILENAME_FIELD) {

                ANALYSIS_signal_delete_file_cursor_text();

            }

            else {

                ANALYSIS_signal_clear_active_text();

            }

            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            if (ANALYSIS_signal_apply_crop_settings()) {

                Global_Analysis_Signal_Menu_Open = 0;
                Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
                ANALYSIS_signal_clear_file_selection();

            }

            return 1;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEMOTION && Global_Analysis_Signal_File_Selecting) {

        SDL_Rect panel;
        SDL_Rect field_rects[ANALYSIS_SIGNAL_FIELD_COUNT];
        SDL_Rect save_rect;
        SDL_Rect close_rect;

        ANALYSIS_get_signal_menu_rects(win_w, win_h, &panel, field_rects, &save_rect, &close_rect);
        (void)panel;
        (void)save_rect;
        (void)close_rect;

        int cursor = ANALYSIS_signal_set_file_cursor_from_mouse(Global_Analysis_Signal_Last_Font,
                                                                field_rects[ANALYSIS_SIGNAL_FILENAME_FIELD],
                                                                event->motion.x, event->motion.y);

        Global_Analysis_Signal_File_Selection_End = cursor;

        if (Global_Analysis_Signal_File_Selection_Start == Global_Analysis_Signal_File_Selection_End) {

            Global_Analysis_Signal_File_Selection_End = Global_Analysis_Signal_File_Selection_Start;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {

        Global_Analysis_Signal_File_Selecting = 0;

        if (!ANALYSIS_signal_file_has_selection()) {

            ANALYSIS_signal_clear_file_selection();

        }

        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        SDL_Rect panel;
        SDL_Rect field_rects[ANALYSIS_SIGNAL_FIELD_COUNT];
        SDL_Rect save_rect;
        SDL_Rect close_rect;

        ANALYSIS_get_signal_menu_rects(win_w, win_h, &panel, field_rects, &save_rect, &close_rect);

        SDL_Rect start_marker_rect;
        SDL_Rect end_marker_rect;

        ANALYSIS_get_signal_marker_rects(field_rects, &start_marker_rect, &end_marker_rect);

        if (Global_Analysis_Marker_Active && point_in_rect(event->button.x, event->button.y, start_marker_rect)) {

            ANALYSIS_signal_set_time_field_from_marker(3);
            return 1;

        }

        if (Global_Analysis_Marker_Active && point_in_rect(event->button.x, event->button.y, end_marker_rect)) {

            ANALYSIS_signal_set_time_field_from_marker(4);
            return 1;

        }

        for (int i = 0; i < ANALYSIS_SIGNAL_FIELD_COUNT; i++) {

            if (point_in_rect(event->button.x, event->button.y, field_rects[i])) {

                Global_Analysis_Signal_Active_Field = i;

                if (i == ANALYSIS_SIGNAL_FILENAME_FIELD) {

                    int cursor = ANALYSIS_signal_set_file_cursor_from_mouse(
                        Global_Analysis_Signal_Last_Font, field_rects[i], event->button.x, event->button.y);

                    Global_Analysis_Signal_File_Selecting = 1;
                    Global_Analysis_Signal_File_Selection_Start = cursor;
                    Global_Analysis_Signal_File_Selection_End = cursor;

                }

                else {

                    ANALYSIS_signal_clear_file_selection();

                }

                return 1;

            }
        }

        if (point_in_rect(event->button.x, event->button.y, save_rect)) {

            if (ANALYSIS_signal_apply_crop_settings()) {

                Global_Analysis_Signal_Menu_Open = 0;
                Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
                ANALYSIS_signal_clear_file_selection();

            }

            return 1;

        }

        if (point_in_rect(event->button.x, event->button.y, close_rect)) {

            Global_Analysis_Signal_Menu_Open = 0;
            Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
            ANALYSIS_signal_clear_file_selection();
            snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Signal metadata menu closed");
            return 1;

        }

        if (!point_in_rect(event->button.x, event->button.y, panel)) {

            return 1;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONUP || event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEWHEEL ||
        event->type == SDL_KEYUP) {

        return 1;

    }

    return 1;
}

void ANALYSIS_draw_workstation_overlays(SDL_Renderer *renderer, TTF_Font *font, SDL_Texture *texture, int tex_w,
                                        int tex_h, int win_w, int win_h) {
    /*
        Purpose: Draws analysis-only filter frequency and time marker overlays
        Returns: No value
    */

    if (!renderer || !font || !Global_Analysis_Mode) {

        return;

    }

    SDL_Rect list_rect;
    SDL_Rect spec_rect;

    ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
    (void)list_rect;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    char workspace_label[64];
    snprintf(workspace_label, sizeof(workspace_label), "Workspace %d/%d", Global_Analysis_Active_Workspace + 1,
             ANALYSIS_WORKSPACE_COUNT);

    SDL_Rect workspace_bg = {win_w - 150, 8, 130, 24};

    if (workspace_bg.x < MARGIN) {

        workspace_bg.x = MARGIN;

    }

    draw_filled_rect(renderer, workspace_bg, (SDL_Color){0, 0, 0, 210});
    draw_outline_rect(renderer, workspace_bg, (SDL_Color){120, 120, 120, 220});
    draw_text(renderer, font, workspace_label, workspace_bg.x + 8, workspace_bg.y + 5, (SDL_Color){230, 230, 230, 255});

    int filter_overlay_visible =
        (Global_Analysis_Filter_Active || Global_Analysis_Filter_Selecting) && Global_Analysis_Path[0] != '\0';

    if (!filter_overlay_visible && texture && tex_w > 0 && tex_h > 0 && spec_rect.w > 0 && spec_rect.h > 0) {

        int clear_h = 42;

        if (clear_h > spec_rect.h) {

            clear_h = spec_rect.h;

        }

        SDL_Rect src_clear = {0, 0, tex_w, (clear_h * tex_h) / spec_rect.h};

        SDL_Rect dst_clear = {spec_rect.x, spec_rect.y, spec_rect.w, clear_h};

        if (src_clear.h < 1) {

            src_clear.h = 1;

        }

        if (src_clear.h > tex_h) {

            src_clear.h = tex_h;

        }

        SDL_RenderCopy(renderer, texture, &src_clear, &dst_clear);

    }

    if (Global_Analysis_Path[0] != '\0') {

        double display_freq_hz = Global_Analysis_Center_Hz;

        if (Global_Analysis_Filter_Active || Global_Analysis_Filter_Selecting) {

            double y0 = Global_Analysis_Filter_Y0;
            double y1 = Global_Analysis_Filter_Y1;

            if (y1 < y0) {

                double tmp = y0;
                y0 = y1;
                y1 = tmp;

            }

            display_freq_hz = ANALYSIS_frequency_from_spec_frac((y0 + y1) * 0.5);

        }

        double time_start =
            Global_Analysis_Sample_Rate > 0.0 ? (double)Global_Analysis_View_Start / Global_Analysis_Sample_Rate : 0.0;
        double time_end =
            Global_Analysis_Sample_Rate > 0.0
                ? (double)(Global_Analysis_View_Start + Global_Analysis_View_Len) / Global_Analysis_Sample_Rate
                : 0.0;

        char status_label[256];

        snprintf(status_label, sizeof(status_label), "%.6f MHz | %.6f sec to %.6f sec | %.3f kS/s",
                 display_freq_hz / 1e6, time_start, time_end, Global_Analysis_Sample_Rate / 1e3);

        SDL_Rect status_bg = {spec_rect.x + 500, spec_rect.y - 30, 500, 24};

        if (status_bg.w < 220) {

            status_bg.x = spec_rect.x + 4;
            status_bg.w = spec_rect.x - 8;

        }

        draw_filled_rect(renderer, status_bg, (SDL_Color){0, 0, 0, 210});
        draw_outline_rect(renderer, status_bg, (SDL_Color){90, 90, 90, 220});
        draw_text(renderer, font, status_label, status_bg.x + 7, status_bg.y + 5, (SDL_Color){230, 230, 230, 255});

    }

    if ((Global_Analysis_Filter_Active || Global_Analysis_Filter_Selecting) && Global_Analysis_Path[0] != '\0') {

        double y0 = Global_Analysis_Filter_Y0;
        double y1 = Global_Analysis_Filter_Y1;

        if (y1 < y0) {

            double tmp = y0;
            y0 = y1;
            y1 = tmp;

        }

        y0 = ANALYSIS_limit_double(y0, 0.0, 1.0);
        y1 = ANALYSIS_limit_double(y1, 0.0, 1.0);

        int select_y0 = spec_rect.y + (int)(y0 * (double)spec_rect.h);
        int select_y1 = spec_rect.y + (int)(y1 * (double)spec_rect.h);

        if (select_y1 < select_y0) {

            int tmp = select_y0;
            select_y0 = select_y1;
            select_y1 = tmp;

        }

        if (select_y0 < spec_rect.y) {

            select_y0 = spec_rect.y;

        }

        if (select_y1 > spec_rect.y + spec_rect.h) {

            select_y1 = spec_rect.y + spec_rect.h;

        }

        /*
         * Keep the frequency selector visible even when the selected band is
         * very small or dragged against the top/bottom edge of the greyscale
         * spectrogram. Hit-testing already worked; this only fixes rendering.
         */

        if (select_y1 <= select_y0) {

            select_y1 = select_y0 + 1;

        }

        if (select_y1 - select_y0 < 4) {

            int mid = (select_y0 + select_y1) / 2;
            select_y0 = mid - 2;
            select_y1 = mid + 2;

            if (select_y0 < spec_rect.y) {

                select_y0 = spec_rect.y;
                select_y1 = spec_rect.y + 4;

            }

            if (select_y1 > spec_rect.y + spec_rect.h) {

                select_y1 = spec_rect.y + spec_rect.h;
                select_y0 = select_y1 - 4;

            }

        }

        SDL_Rect filter_rect = {spec_rect.x, select_y0, spec_rect.w, select_y1 - select_y0};

        draw_filled_rect(renderer, filter_rect, (SDL_Color){220, 220, 220, 50});
        draw_outline_rect(renderer, filter_rect, (SDL_Color){230, 230, 230, 180});

        SDL_SetRenderDrawColor(renderer, 230, 230, 230, 180);
        SDL_RenderDrawLine(renderer, spec_rect.x, select_y0, spec_rect.x + spec_rect.w, select_y0);
        SDL_RenderDrawLine(renderer, spec_rect.x, select_y1, spec_rect.x + spec_rect.w, select_y1);

        char filter_label[160];

        ANALYSIS_get_filter_label(filter_label, sizeof(filter_label));

        int filter_label_width = 0;
        int filter_label_height = 0;

        if (!font || TTF_SizeUTF8(font, filter_label, &filter_label_width, &filter_label_height) != 0) {

            filter_label_width = 180;

        }

        const int filter_label_padding_left = 10;
        const int filter_label_padding_right = 10;
        const int filter_label_x_offset = 96;

        SDL_Rect label_bg = {spec_rect.x + filter_label_x_offset, spec_rect.y - 30,
                             filter_label_width + filter_label_padding_left + filter_label_padding_right, 24};

        if (label_bg.w > spec_rect.w - filter_label_x_offset) {

            label_bg.w = spec_rect.w - filter_label_x_offset;

        }

        draw_filled_rect(renderer, label_bg, (SDL_Color){0, 0, 0, 210});
        draw_outline_rect(renderer, label_bg, (SDL_Color){0, 220, 80, 220});
        draw_text(renderer, font, filter_label, label_bg.x + filter_label_padding_left, label_bg.y + 5,
                  (SDL_Color){0, 255, 90, 255});

    }

    if (Global_Analysis_Column_Visible && Global_Analysis_Path[0] != '\0') {

        double x0 = Global_Analysis_Column_X0;
        double x1 = Global_Analysis_Column_X1;

        if (x1 < x0) {

            double tmp = x0;
            x0 = x1;
            x1 = tmp;

        }

        x0 = ANALYSIS_limit_double(x0, 0.0, 1.0);
        x1 = ANALYSIS_limit_double(x1, 0.0, 1.0);

        int select_x0 = spec_rect.x + (int)(x0 * (double)spec_rect.w);
        int select_x1 = spec_rect.x + (int)(x1 * (double)spec_rect.w);

        if (select_x1 < select_x0) {

            int tmp = select_x0;
            select_x0 = select_x1;
            select_x1 = tmp;

        }

        if (select_x0 < spec_rect.x) {

            select_x0 = spec_rect.x;

        }

        if (select_x1 > spec_rect.x + spec_rect.w) {

            select_x1 = spec_rect.x + spec_rect.w;

        }

        if (select_x1 <= select_x0) {

            select_x1 = select_x0 + 1;

        }

        SDL_Rect column_rect = {select_x0, spec_rect.y, select_x1 - select_x0, spec_rect.h};

        draw_filled_rect(renderer, column_rect, (SDL_Color){255, 255, 0, 45});
        draw_outline_rect(renderer, column_rect, (SDL_Color){255, 255, 0, 230});

        double start_sec =
            Global_Analysis_Sample_Rate > 0.0
                ? (double)(Global_Analysis_View_Start + (size_t)(x0 * (double)Global_Analysis_View_Len)) /
                      Global_Analysis_Sample_Rate
                : 0.0;
        double end_sec = Global_Analysis_Sample_Rate > 0.0
                             ? (double)(Global_Analysis_View_Start + (size_t)(x1 * (double)Global_Analysis_View_Len)) /
                                   Global_Analysis_Sample_Rate
                             : 0.0;

        char column_label[128];

        snprintf(column_label, sizeof(column_label), "%.4f s - %.4f s     (%.4f s)", start_sec, end_sec,
                 end_sec - start_sec);

        SDL_Rect column_bg = {spec_rect.x + spec_rect.w - 225 - 192, spec_rect.y - 30,
                              240, // 240
                              24};

        if (column_bg.x < spec_rect.x) {

            column_bg.x = spec_rect.x + 4;

        }

        draw_filled_rect(renderer, column_bg, (SDL_Color){0, 0, 0, 210});
        draw_outline_rect(renderer, column_bg, (SDL_Color){255, 255, 0, 220});
        draw_text(renderer, font, column_label, column_bg.x + 7, column_bg.y + 5, (SDL_Color){255, 255, 0, 255});

    }

    if (Global_Analysis_Marker_Active && Global_Analysis_Path[0] != '\0' && Global_Analysis_View_Len > 0 &&
        Global_Analysis_Marker_Sample >= Global_Analysis_View_Start &&
        Global_Analysis_Marker_Sample <= Global_Analysis_View_Start + Global_Analysis_View_Len) {

        double marker_frac =
            (double)(Global_Analysis_Marker_Sample - Global_Analysis_View_Start) / (double)Global_Analysis_View_Len;

        int marker_x = spec_rect.x + (int)(marker_frac * (double)spec_rect.w);

        if (marker_x < spec_rect.x) {

            marker_x = spec_rect.x;

        }

        if (marker_x > spec_rect.x + spec_rect.w - 1) {

            marker_x = spec_rect.x + spec_rect.w - 1;

        }

        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 230);
        SDL_RenderDrawLine(renderer, marker_x, spec_rect.y, marker_x, spec_rect.y + spec_rect.h);

        char marker_label[96];

        snprintf(marker_label, sizeof(marker_label), "Marker %.6f s", Global_Analysis_Marker_Time);

        SDL_Rect marker_bg = {marker_x + 6, spec_rect.y + 6, 150, 24};

        if (marker_bg.x + marker_bg.w > spec_rect.x + spec_rect.w - 4) {

            marker_bg.x = spec_rect.x + spec_rect.w - marker_bg.w - 4;

        }

        if (marker_bg.x < spec_rect.x + 4) {

            marker_bg.x = spec_rect.x + 4;

        }

        draw_filled_rect(renderer, marker_bg, (SDL_Color){0, 0, 0, 210});
        draw_outline_rect(renderer, marker_bg, (SDL_Color){255, 255, 0, 220});
        draw_text(renderer, font, marker_label, marker_bg.x + 7, marker_bg.y + 5, (SDL_Color){255, 255, 0, 255});

    }

    ANALYSIS_draw_hover_sync_line(renderer, font, win_w, win_h);
    ANALYSIS_draw_constellation_mode_buttons(renderer, font, win_w, win_h);

    ANALYSIS_draw_file_search_button(renderer, font, win_w, win_h);

    ANALYSIS_draw_signal_trash_icon(renderer, font, win_w, win_h);
    ANALYSIS_draw_multithread_icon(renderer, font, win_w, win_h);
    ANALYSIS_draw_transmit_icon(renderer, font, win_w, win_h);
    ANALYSIS_draw_signal_settings_icon(renderer, font, win_w, win_h);
    ANALYSIS_draw_signal_menu(renderer, font, win_w, win_h);
    ANALYSIS_draw_delete_confirm_menu(renderer, font, win_w, win_h);
    ANALYSIS_draw_file_search_popup(renderer, font, win_w, win_h);

    ANALYSIS_draw_noise_filter_overlay(renderer, font, win_w, win_h);
    ANALYSIS_draw_clear_workspace_button(renderer, font, win_w, win_h);
    ANALYSIS_draw_crop_button(renderer, font, win_w, win_h);
    ANALYSIS_draw_multithread_prompt(renderer, font, win_w, win_h);
    ANALYSIS_draw_transmit_auth_prompt(renderer, font, win_w, win_h);
    ANALYSIS_draw_transmit_config_prompt(renderer, font, win_w, win_h);
    ANALYSIS_draw_transmit_progress_prompt(renderer, font, win_w, win_h);
    ANALYSIS_draw_transmit_result_prompt(renderer, font, win_w, win_h);
    ANALYSIS_draw_constellation_psk_prompt(renderer, font, win_w, win_h);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static double ANALYSIS_wrap_phase(double value) {
    /*
        Purpose: Wraps phase into the -pi to pi range
        Returns: Wrapped phase
    */

    while (value > M_PI) {
        value -= 2.0 * M_PI;
    }
    while (value < -M_PI) {
        value += 2.0 * M_PI;
    }

    return value;
}

static uint32_t ANALYSIS_gray(double v) {
    /*
        Purpose: Maps a normalized value to a grayscale pixel color
        Returns: RGB color
    */

    if (v < 0.0) {

        v = 0.0;

    }

    if (v > 1.0) {

        v = 1.0;

    }

    uint8_t c = (uint8_t)(v * 255.0);

    return rgb(c, c, c);
}

static int ANALYSIS_is_center_display_bin(int bin) {
    /*
        Purpose: Identifies the FFT DC/midpoint bins that create the persistent horizontal center stripe in the
       greyscale spectrogram Returns: Non-zero if this bin should be hidden from the greyscale image
    */

    int center = ANALYSIS_FFT_SIZE / 2;
    int guard_bins = 10;

    return bin >= center - guard_bins && bin <= center + guard_bins;
}

static double ANALYSIS_spectrogram_display_db(const double *db_img, int x, int bin) {
    /*
        Purpose: Returns a greyscale-spectrogram-only display value
        Returns: Display dB value for one spectrogram pixel
    */

    if (!db_img) {

        return -300.0;

    }

    if (!ANALYSIS_is_center_display_bin(bin)) {

        return db_img[(size_t)x * ANALYSIS_FFT_SIZE + bin];

    }

    int center = ANALYSIS_FFT_SIZE / 2;
    int guard_bins = 10;
    int ref_gap = 18;

    int low_bin = center - ref_gap;
    int high_bin = center + ref_gap;

    if (low_bin < 0) {

        low_bin = 0;

    }

    if (high_bin >= ANALYSIS_FFT_SIZE) {

        high_bin = ANALYSIS_FFT_SIZE - 1;

    }

    if (low_bin >= center - guard_bins && center - guard_bins > 0) {

        low_bin = center - guard_bins - 1;

    }

    if (high_bin <= center + guard_bins && center + guard_bins + 1 < ANALYSIS_FFT_SIZE) {

        high_bin = center + guard_bins + 1;

    }

    double low = db_img[(size_t)x * ANALYSIS_FFT_SIZE + low_bin];
    double high = db_img[(size_t)x * ANALYSIS_FFT_SIZE + high_bin];

    double denom = (double)(high_bin - low_bin);
    double t = denom > 0.0 ? (double)(bin - low_bin) / denom : 0.5;

    if (t < 0.0) {

        t = 0.0;

    }

    if (t > 1.0) {

        t = 1.0;

    }

    return low + ((high - low) * t);
}

typedef struct Type_Analysis_Load_Task {
    int fd;
    int16_t *blocks;
    const size_t *starts;
    int first_x;
    int end_x;
    size_t i16_per_block;
    int read_error;
} Type_Analysis_Load_Task;

static void *ANALYSIS_load_iq_blocks_worker(void *opaque) {
    /*
        Purpose: Loads an IQ block in a worker thread
        Returns: Thread result
    */

    Type_Analysis_Load_Task *task = (Type_Analysis_Load_Task *)opaque;

    if (!task || task->fd < 0 || !task->blocks || !task->starts || task->i16_per_block == 0) {

        return NULL;

    }

    size_t bytes_per_block = task->i16_per_block * sizeof(int16_t);

    for (int x = task->first_x; x < task->end_x; x++) {
        int16_t *dst = task->blocks + ((size_t)x * task->i16_per_block);
        off_t offset = (off_t)(task->starts[x] * 2U * sizeof(int16_t));
        size_t total = 0;

        memset(dst, 0, bytes_per_block);

        while (total < bytes_per_block) {
            ssize_t got =
                pread(task->fd, ((unsigned char *)dst) + total, bytes_per_block - total, offset + (off_t)total);

            if (got > 0) {

                total += (size_t)got;
                continue;

            }

            if (got == 0) {

                break;

            }

            if (errno == EINTR) {

                continue;

            }

            task->read_error = errno ? errno : EIO;
            break;
        }
    }

    return NULL;
}

static int ANALYSIS_load_iq_blocks_multithreaded(FILE *fp, int16_t *blocks, const size_t *starts, int render_w,
                                                 size_t i16_per_block) {
    /*
        Purpose: Loads the IQ blocks multithreaded
        Returns: Success status
    */

    if (!fp || !blocks || !starts || render_w <= 0 || i16_per_block == 0) {

        return 0;

    }

    int fd = fileno(fp);

    if (fd < 0) {

        return 0;

    }

    int thread_count = ANALYSIS_MULTITHREAD_COUNT;

    if (thread_count > render_w) {

        thread_count = render_w;

    }

    if (thread_count < 1) {

        thread_count = 1;

    }

    pthread_t threads[ANALYSIS_MULTITHREAD_COUNT];
    Type_Analysis_Load_Task tasks[ANALYSIS_MULTITHREAD_COUNT];
    int created[ANALYSIS_MULTITHREAD_COUNT];

    memset(threads, 0, sizeof(threads));
    memset(tasks, 0, sizeof(tasks));
    memset(created, 0, sizeof(created));

    for (int i = 0; i < thread_count; i++) {
        tasks[i].fd = fd;
        tasks[i].blocks = blocks;
        tasks[i].starts = starts;
        tasks[i].first_x = (render_w * i) / thread_count;
        tasks[i].end_x = (render_w * (i + 1)) / thread_count;
        tasks[i].i16_per_block = i16_per_block;
        tasks[i].read_error = 0;

        if (pthread_create(&threads[i], NULL, ANALYSIS_load_iq_blocks_worker, &tasks[i]) == 0) {

            created[i] = 1;

        }

        else {

            ANALYSIS_load_iq_blocks_worker(&tasks[i]);

        }
    }

    for (int i = 0; i < thread_count; i++) {

        if (created[i]) {

            pthread_join(threads[i], NULL);

        }
    }

    for (int i = 0; i < thread_count; i++) {

        if (tasks[i].read_error != 0) {

            return 0;

        }
    }

    return 1;
}

typedef struct Type_Analysis_Constellation_Point {
    double i;
    double q;
} Type_Analysis_Constellation_Point;

static int ANALYSIS_constellation_double_compare(const void *left, const void *right) {
    /*
        Purpose: Compares constellation double values for sorting
        Returns: Sort order
    */

    double a = *(const double *)left;
    double b = *(const double *)right;

    if (a < b) {

        return -1;

    }

    if (a > b) {

        return 1;

    }

    return 0;
}

static size_t ANALYSIS_constellation_next_power_of_two(size_t value) {
    /*
        Purpose: Rounds a constellation sample count up to the next supported power of two
        Returns: Power-of-two sample count
    */

    size_t power = 1U;

    while (power < value && power < ANALYSIS_CONSTELLATION_MAX_INPUT) {
        power <<= 1U;
    }

    return power;
}

static void ANALYSIS_constellation_apply_frequency_correction(double *i_data, double *q_data, size_t count,
                                                              double radians_per_sample) {
    /*
        Purpose: Rotates IQ samples to compensate for a frequency offset
        Returns: No value
    */

    if (!i_data || !q_data || count == 0 || fabs(radians_per_sample) < 1e-15) {

        return;

    }

    double step_i = cos(-radians_per_sample);
    double step_q = sin(-radians_per_sample);
    double oscillator_i = 1.0;
    double oscillator_q = 0.0;

    for (size_t n = 0; n < count; n++) {
        double input_i = i_data[n];
        double input_q = q_data[n];

        i_data[n] = input_i * oscillator_i - input_q * oscillator_q;
        q_data[n] = input_i * oscillator_q + input_q * oscillator_i;

        double next_i = oscillator_i * step_i - oscillator_q * step_q;
        double next_q = oscillator_q * step_i + oscillator_i * step_q;

        oscillator_i = next_i;
        oscillator_q = next_q;

        if ((n & 4095U) == 4095U) {

            double magnitude = hypot(oscillator_i, oscillator_q);

            if (magnitude > 1e-12) {

                oscillator_i /= magnitude;
                oscillator_q /= magnitude;

            }

        }
    }
}

static void ANALYSIS_constellation_complex_power(double input_i, double input_q, int order, double *output_i,
                                                 double *output_q) {
    /*
        Purpose: Raises a complex IQ value to an integer power
        Returns: No value
    */

    double result_i = 1.0;
    double result_q = 0.0;

    for (int k = 0; k < order; k++) {
        double next_i = result_i * input_i - result_q * input_q;
        double next_q = result_i * input_q + result_q * input_i;
        result_i = next_i;
        result_q = next_q;
    }

    if (output_i) {

        *output_i = result_i;

    }

    if (output_q) {

        *output_q = result_q;

    }
}

static double ANALYSIS_constellation_estimate_mth_frequency(const double *i_data, const double *q_data, size_t count,
                                                            int order, double magnitude_gate) {
    /*
        Purpose: Estimates angular frequency offset using an Mth-power phase measurement
        Returns: Estimated radians per sample
    */

    double sum_i = 0.0;
    double sum_q = 0.0;
    double previous_i = 0.0;
    double previous_q = 0.0;
    int have_previous = 0;

    for (size_t n = 0; n < count; n++) {
        double magnitude = hypot(i_data[n], q_data[n]);

        if (magnitude < magnitude_gate) {

            have_previous = 0;
            continue;

        }

        double unit_i = i_data[n] / magnitude;
        double unit_q = q_data[n] / magnitude;
        double powered_i = 0.0;
        double powered_q = 0.0;

        ANALYSIS_constellation_complex_power(unit_i, unit_q, order, &powered_i, &powered_q);

        if (have_previous) {

            sum_i += powered_i * previous_i + powered_q * previous_q;
            sum_q += powered_q * previous_i - powered_i * previous_q;

        }

        previous_i = powered_i;
        previous_q = powered_q;
        have_previous = 1;
    }

    if (hypot(sum_i, sum_q) < 1e-12) {

        return 0.0;

    }

    return atan2(sum_q, sum_i) / (double)order;
}

static double ANALYSIS_constellation_mth_coherence(const double *i_data, const double *q_data, size_t count, int order,
                                                   double frequency, double magnitude_gate) {
    /*
        Purpose: Measures Mth-order phase coherence after applying a candidate frequency correction
        Returns: Normalized coherence score
    */

    double sum_i = 0.0;
    double sum_q = 0.0;
    int used = 0;
    double step_i = cos(-frequency);
    double step_q = sin(-frequency);
    double oscillator_i = 1.0;
    double oscillator_q = 0.0;

    for (size_t n = 0; n < count; n++) {
        double corrected_i = i_data[n] * oscillator_i - q_data[n] * oscillator_q;
        double corrected_q = i_data[n] * oscillator_q + q_data[n] * oscillator_i;
        double magnitude = hypot(corrected_i, corrected_q);

        if (magnitude >= magnitude_gate) {

            double powered_i = 0.0;
            double powered_q = 0.0;

            ANALYSIS_constellation_complex_power(corrected_i / magnitude, corrected_q / magnitude, order, &powered_i,
                                                 &powered_q);
            sum_i += powered_i;
            sum_q += powered_q;
            used++;

        }

        double next_i = oscillator_i * step_i - oscillator_q * step_q;
        double next_q = oscillator_q * step_i + oscillator_i * step_q;
        oscillator_i = next_i;
        oscillator_q = next_q;
    }

    return used > 0 ? hypot(sum_i, sum_q) / (double)used : 0.0;
}

static double ANALYSIS_constellation_estimate_direct_frequency(const double *i_data, const double *q_data, size_t count,
                                                               double magnitude_gate) {
    /*
        Purpose: Estimates direct sample-to-sample angular frequency from gated IQ samples
        Returns: Estimated radians per sample
    */

    double sum_i = 0.0;
    double sum_q = 0.0;

    for (size_t n = 1; n < count; n++) {
        double previous_magnitude = hypot(i_data[n - 1U], q_data[n - 1U]);
        double current_magnitude = hypot(i_data[n], q_data[n]);

        if (previous_magnitude < magnitude_gate || current_magnitude < magnitude_gate) {

            continue;

        }

        double weight = previous_magnitude * current_magnitude;
        sum_i += weight * (i_data[n] * i_data[n - 1U] + q_data[n] * q_data[n - 1U]);
        sum_q += weight * (q_data[n] * i_data[n - 1U] - i_data[n] * q_data[n - 1U]);
    }

    return hypot(sum_i, sum_q) > 1e-12 ? atan2(sum_q, sum_i) : 0.0;
}

static int ANALYSIS_constellation_prepare_samples(FILE *fp, int filter_active, int filter_bin_low, int filter_bin_high,
                                                  int time_filter_active, int time_col_low, int time_col_high,
                                                  int render_w, double **output_i, double **output_q,
                                                  size_t *output_count, double *output_bandwidth_hz) {
    /*
        Purpose: Loads and prepares the filtered IQ sample set used for constellation analysis
        Returns: Success status
    */

    if (!fp || !output_i || !output_q || !output_count || Global_Analysis_Sample_Rate <= 0.0 ||
        Global_Analysis_IQ_Count == 0 || Global_Analysis_View_Len == 0) {

        return 0;

    }

    *output_i = NULL;
    *output_q = NULL;
    *output_count = 0;

    size_t selection_start = Global_Analysis_View_Start;
    size_t selection_length = Global_Analysis_View_Len;

    if (time_filter_active && render_w > 1) {

        size_t start = Global_Analysis_View_Start +
                       (size_t)(((double)time_col_low / (double)(render_w - 1)) * (double)Global_Analysis_View_Len);
        size_t end = Global_Analysis_View_Start +
                     (size_t)(((double)time_col_high / (double)(render_w - 1)) * (double)Global_Analysis_View_Len);

        if (start >= Global_Analysis_IQ_Count) {

            start = Global_Analysis_IQ_Count - 1U;

        }

        if (end > Global_Analysis_IQ_Count) {

            end = Global_Analysis_IQ_Count;

        }

        if (end > start) {

            selection_start = start;
            selection_length = end - start;

        }

    }

    if (selection_length < 64U) {

        return 0;

    }

    size_t target_count = selection_length;

    if (target_count > ANALYSIS_CONSTELLATION_MAX_INPUT) {

        target_count = ANALYSIS_CONSTELLATION_MAX_INPUT;

    }

    size_t chosen_start = selection_start;

    if (selection_length > target_count) {

        const int probes = 17;
        const size_t probe_count = 4096U;
        int16_t *probe = malloc(probe_count * 2U * sizeof(int16_t));
        double best_power = -1.0;
        size_t best_center = selection_start + selection_length / 2U;

        if (probe) {

            for (int p = 0; p < probes; p++) {
                size_t center =
                    selection_start + (size_t)(((double)p / (double)(probes - 1)) * (double)(selection_length - 1U));
                size_t probe_start = center > probe_count / 2U ? center - probe_count / 2U : selection_start;

                if (probe_start < selection_start) {

                    probe_start = selection_start;

                }

                if (probe_start + probe_count > selection_start + selection_length) {

                    probe_start = selection_start + selection_length - probe_count;

                }

                clearerr(fp);

                if (fseeko(fp, (off_t)(probe_start * 2U * sizeof(int16_t)), SEEK_SET) != 0) {

                    continue;

                }

                size_t values = fread(probe, sizeof(int16_t), probe_count * 2U, fp);
                size_t samples = values / 2U;
                double power = 0.0;

                for (size_t n = 0; n < samples; n++) {
                    double sample_i = (double)probe[n * 2U] / 32768.0;
                    double sample_q = (double)probe[n * 2U + 1U] / 32768.0;
                    power += sample_i * sample_i + sample_q * sample_q;
                }

                if (samples > 0) {

                    power /= (double)samples;

                }

                if (power > best_power) {

                    best_power = power;
                    best_center = center;

                }
            }

            free(probe);

        }

        chosen_start = best_center > target_count / 2U ? best_center - target_count / 2U : selection_start;

        if (chosen_start < selection_start) {

            chosen_start = selection_start;

        }

        if (chosen_start + target_count > selection_start + selection_length) {

            chosen_start = selection_start + selection_length - target_count;

        }

    }

    int16_t *raw = malloc(target_count * 2U * sizeof(int16_t));
    double *i_data = malloc(target_count * sizeof(double));
    double *q_data = malloc(target_count * sizeof(double));

    if (!raw || !i_data || !q_data) {

        free(raw);
        free(i_data);
        free(q_data);
        return 0;

    }

    clearerr(fp);

    if (fseeko(fp, (off_t)(chosen_start * 2U * sizeof(int16_t)), SEEK_SET) != 0) {

        free(raw);
        free(i_data);
        free(q_data);
        return 0;

    }

    size_t values_read = fread(raw, sizeof(int16_t), target_count * 2U, fp);
    size_t sample_count = values_read / 2U;

    if (sample_count < 64U) {

        free(raw);
        free(i_data);
        free(q_data);
        return 0;

    }

    double dc_i = 0.0;
    double dc_q = 0.0;

    for (size_t n = 0; n < sample_count; n++) {
        dc_i += (double)raw[n * 2U] / 32768.0;
        dc_q += (double)raw[n * 2U + 1U] / 32768.0;
    }

    dc_i /= (double)sample_count;
    dc_q /= (double)sample_count;

    double center_offset_hz = 0.0;
    double bandwidth_hz = Global_Analysis_Sample_Rate * 0.90;

    if (filter_active) {

        double bin_center = ((double)filter_bin_low + (double)filter_bin_high) * 0.5;
        double bin_width_hz = Global_Analysis_Sample_Rate / (double)ANALYSIS_FFT_SIZE;
        center_offset_hz = (bin_center - ((double)ANALYSIS_FFT_SIZE * 0.5)) * bin_width_hz;
        bandwidth_hz = (double)(filter_bin_high - filter_bin_low + 1) * bin_width_hz;

    }

    if (output_bandwidth_hz) {

        *output_bandwidth_hz = bandwidth_hz;

    }

    double mixer_step = -2.0 * M_PI * center_offset_hz / Global_Analysis_Sample_Rate;
    double step_i = cos(mixer_step);
    double step_q = sin(mixer_step);
    double oscillator_i = 1.0;
    double oscillator_q = 0.0;

    for (size_t n = 0; n < sample_count; n++) {
        double input_i = ((double)raw[n * 2U] / 32768.0) - dc_i;
        double input_q = ((double)raw[n * 2U + 1U] / 32768.0) - dc_q;

        i_data[n] = input_i * oscillator_i - input_q * oscillator_q;
        q_data[n] = input_i * oscillator_q + input_q * oscillator_i;

        double next_i = oscillator_i * step_i - oscillator_q * step_q;
        double next_q = oscillator_q * step_i + oscillator_i * step_q;
        oscillator_i = next_i;
        oscillator_q = next_q;
    }

    free(raw);

    size_t fft_size = ANALYSIS_constellation_next_power_of_two(sample_count);

    if (fft_size < sample_count || fft_size > ANALYSIS_CONSTELLATION_MAX_INPUT) {

        free(i_data);
        free(q_data);
        return 0;

    }

    fftw_complex *time_data = fftw_malloc(sizeof(fftw_complex) * fft_size);
    fftw_complex *frequency_data = fftw_malloc(sizeof(fftw_complex) * fft_size);

    if (!time_data || !frequency_data) {

        if (time_data) {

            fftw_free(time_data);

        }

        if (frequency_data) {

            fftw_free(frequency_data);

        }
        free(i_data);
        free(q_data);
        return 0;

    }

    for (size_t n = 0; n < fft_size; n++) {
        time_data[n][0] = n < sample_count ? i_data[n] : 0.0;
        time_data[n][1] = n < sample_count ? q_data[n] : 0.0;
    }

    fftw_plan forward = fftw_plan_dft_1d((int)fft_size, time_data, frequency_data, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_plan inverse = fftw_plan_dft_1d((int)fft_size, frequency_data, time_data, FFTW_BACKWARD, FFTW_ESTIMATE);

    if (!forward || !inverse) {

        if (forward) {

            fftw_destroy_plan(forward);

        }

        if (inverse) {

            fftw_destroy_plan(inverse);

        }
        fftw_free(time_data);
        fftw_free(frequency_data);
        free(i_data);
        free(q_data);
        return 0;

    }

    fftw_execute(forward);

    double cutoff_hz = bandwidth_hz * 0.55;

    if (cutoff_hz > Global_Analysis_Sample_Rate * 0.47) {

        cutoff_hz = Global_Analysis_Sample_Rate * 0.47;

    }

    if (cutoff_hz < Global_Analysis_Sample_Rate / (double)fft_size) {

        cutoff_hz = Global_Analysis_Sample_Rate / (double)fft_size;

    }

    for (size_t k = 0; k < fft_size; k++) {
        double frequency =
            k <= fft_size / 2U
                ? ((double)k * Global_Analysis_Sample_Rate / (double)fft_size)
                : ((double)((long long)k - (long long)fft_size) * Global_Analysis_Sample_Rate / (double)fft_size);

        if (fabs(frequency) > cutoff_hz) {

            frequency_data[k][0] = 0.0;
            frequency_data[k][1] = 0.0;

        }
    }

    fftw_execute(inverse);

    for (size_t n = 0; n < sample_count; n++) {
        i_data[n] = time_data[n][0] / (double)fft_size;
        q_data[n] = time_data[n][1] / (double)fft_size;
    }

    fftw_destroy_plan(forward);
    fftw_destroy_plan(inverse);
    fftw_free(time_data);
    fftw_free(frequency_data);

    dc_i = 0.0;
    dc_q = 0.0;

    for (size_t n = 0; n < sample_count; n++) {
        dc_i += i_data[n];
        dc_q += q_data[n];
    }

    dc_i /= (double)sample_count;
    dc_q /= (double)sample_count;

    for (size_t n = 0; n < sample_count; n++) {
        i_data[n] -= dc_i;
        q_data[n] -= dc_q;
    }

    *output_i = i_data;
    *output_q = q_data;
    *output_count = sample_count;
    return 1;
}

static double ANALYSIS_constellation_kmeans_1d(const double *values, int count, int level_count) {
    /*
        Purpose: Measures normalized one-dimensional K-means clustering distortion for the requested level count
        Returns: Normalized clustering error
    */

    if (!values || count < level_count || level_count < 2 || level_count > 8) {

        return 1.0;

    }

    double minimum = values[0];
    double maximum = values[0];
    double mean = 0.0;

    for (int i = 0; i < count; i++) {

        if (values[i] < minimum) {

            minimum = values[i];

        }

        if (values[i] > maximum) {

            maximum = values[i];

        }
        mean += values[i];
    }

    mean /= (double)count;

    if (maximum - minimum < 1e-9) {

        return 1.0;

    }

    double centers[8] = {0.0};

    for (int level = 0; level < level_count; level++) {
        centers[level] = minimum + ((double)level + 0.5) * (maximum - minimum) / (double)level_count;
    }

    for (int iteration = 0; iteration < 10; iteration++) {
        double sums[8] = {0.0};
        int counts[8] = {0};

        for (int i = 0; i < count; i++) {
            int best_level = 0;
            double best_distance = fabs(values[i] - centers[0]);

            for (int level = 1; level < level_count; level++) {
                double distance = fabs(values[i] - centers[level]);

                if (distance < best_distance) {

                    best_distance = distance;
                    best_level = level;

                }
            }

            sums[best_level] += values[i];
            counts[best_level]++;
        }

        for (int level = 0; level < level_count; level++) {

            if (counts[level] > 0) {

                centers[level] = sums[level] / (double)counts[level];

            }
        }
    }

    double error = 0.0;
    double variance = 0.0;

    for (int i = 0; i < count; i++) {
        double best_distance = fabs(values[i] - centers[0]);

        for (int level = 1; level < level_count; level++) {
            double distance = fabs(values[i] - centers[level]);

            if (distance < best_distance) {

                best_distance = distance;

            }
        }

        error += best_distance * best_distance;
        double centered = values[i] - mean;
        variance += centered * centered;
    }

    return variance > 1e-12 ? error / variance : 1.0;
}

static double ANALYSIS_constellation_psk_score(const Type_Analysis_Constellation_Point *points, int count, int order) {
    /*
        Purpose: Scores how closely constellation points fit the requested PSK order
        Returns: PSK fit score
    */

    if (!points || count < 8) {

        return 0.0;

    }

    double sum_i = 0.0;
    double sum_q = 0.0;
    double radius_sum = 0.0;
    double radius_squared_sum = 0.0;
    int used = 0;

    for (int p = 0; p < count; p++) {
        double radius = hypot(points[p].i, points[p].q);

        if (radius < 1e-9) {

            continue;

        }

        double powered_i = 0.0;
        double powered_q = 0.0;
        ANALYSIS_constellation_complex_power(points[p].i / radius, points[p].q / radius, order, &powered_i, &powered_q);
        sum_i += powered_i;
        sum_q += powered_q;
        radius_sum += radius;
        radius_squared_sum += radius * radius;
        used++;
    }

    if (used < 8) {

        return 0.0;

    }

    double coherence = hypot(sum_i, sum_q) / (double)used;
    double mean_radius = radius_sum / (double)used;
    double variance = radius_squared_sum / (double)used - mean_radius * mean_radius;
    double relative_variance = variance > 0.0 ? variance / (mean_radius * mean_radius + 1e-12) : 0.0;

    return coherence / (1.0 + 3.0 * relative_variance);
}

static double ANALYSIS_constellation_qam_score(const Type_Analysis_Constellation_Point *points, int count) {
    /*
        Purpose: Scores how closely constellation points fit a QAM arrangement
        Returns: QAM fit score
    */

    if (!points || count < 16) {

        return 0.0;

    }

    double mean_i = 0.0;
    double mean_q = 0.0;

    for (int p = 0; p < count; p++) {
        mean_i += points[p].i;
        mean_q += points[p].q;
    }

    mean_i /= (double)count;
    mean_q /= (double)count;

    double fourth_i = 0.0;
    double fourth_q = 0.0;

    for (int p = 0; p < count; p++) {
        double powered_i = 0.0;
        double powered_q = 0.0;
        ANALYSIS_constellation_complex_power(points[p].i - mean_i, points[p].q - mean_q, 4, &powered_i, &powered_q);
        fourth_i += powered_i;
        fourth_q += powered_q;
    }

    double phase = 0.25 * ANALYSIS_wrap_phase(atan2(fourth_q, fourth_i) - M_PI);
    double rotation_i = cos(-phase);
    double rotation_q = sin(-phase);
    double axis_i[512];
    double axis_q[512];
    int used = count > 512 ? 512 : count;

    for (int p = 0; p < used; p++) {
        double centered_i = points[p].i - mean_i;
        double centered_q = points[p].q - mean_q;
        axis_i[p] = centered_i * rotation_i - centered_q * rotation_q;
        axis_q[p] = centered_i * rotation_q + centered_q * rotation_i;
    }

    double best_distortion = 1.0;
    const int levels[] = {2, 4, 8};

    for (size_t index = 0; index < sizeof(levels) / sizeof(levels[0]); index++) {
        double distortion = 0.5 * (ANALYSIS_constellation_kmeans_1d(axis_i, used, levels[index]) +
                                   ANALYSIS_constellation_kmeans_1d(axis_q, used, levels[index]));
        distortion *= 1.0 + 0.025 * (double)(levels[index] - 2);

        if (distortion < best_distortion) {

            best_distortion = distortion;

        }
    }

    return 1.0 / (0.02 + best_distortion);
}

static double ANALYSIS_constellation_ask_score(const Type_Analysis_Constellation_Point *points, int count) {
    /*
        Purpose: Scores how closely constellation points fit an ASK or OOK arrangement
        Returns: ASK or OOK fit score
    */

    if (!points || count < 12) {

        return 0.0;

    }

    double mean_i = 0.0;
    double mean_q = 0.0;

    for (int p = 0; p < count; p++) {
        mean_i += points[p].i;
        mean_q += points[p].q;
    }

    mean_i /= (double)count;
    mean_q /= (double)count;

    double covariance_ii = 0.0;
    double covariance_qq = 0.0;
    double covariance_iq = 0.0;

    for (int p = 0; p < count; p++) {
        double centered_i = points[p].i - mean_i;
        double centered_q = points[p].q - mean_q;
        covariance_ii += centered_i * centered_i;
        covariance_qq += centered_q * centered_q;
        covariance_iq += centered_i * centered_q;
    }

    double phase = 0.5 * atan2(2.0 * covariance_iq, covariance_ii - covariance_qq);
    double rotation_i = cos(-phase);
    double rotation_q = sin(-phase);
    double axis[512];
    double axis_variance = 0.0;
    double perpendicular_variance = 0.0;
    int used = count > 512 ? 512 : count;

    for (int p = 0; p < used; p++) {
        double centered_i = points[p].i - mean_i;
        double centered_q = points[p].q - mean_q;
        double along = centered_i * rotation_i - centered_q * rotation_q;
        double perpendicular = centered_i * rotation_q + centered_q * rotation_i;
        axis[p] = along;
        axis_variance += along * along;
        perpendicular_variance += perpendicular * perpendicular;
    }

    double best_distortion = 1.0;
    const int levels[] = {2, 4, 8};

    for (size_t index = 0; index < sizeof(levels) / sizeof(levels[0]); index++) {
        double distortion = ANALYSIS_constellation_kmeans_1d(axis, used, levels[index]);
        distortion *= 1.0 + 0.025 * (double)(levels[index] - 2);

        if (distortion < best_distortion) {

            best_distortion = distortion;

        }
    }

    double line_ratio = axis_variance / (perpendicular_variance + axis_variance * 0.01 + 1e-12);
    return line_ratio / (0.03 + best_distortion);
}

static int ANALYSIS_constellation_collect_candidate(const double *i_data, const double *q_data, size_t count,
                                                    size_t guard, int samples_per_symbol, int offset,
                                                    Type_Analysis_Constellation_Point *points, int maximum_points) {
    /*
        Purpose: Collects symbol-spaced constellation points for a timing candidate
        Returns: Number of points collected
    */

    if (!i_data || !q_data || !points || samples_per_symbol < 2 || maximum_points < 1 || count <= guard * 2U) {

        return 0;

    }

    size_t first = guard + (size_t)offset;

    if (first >= count - guard) {

        return 0;

    }

    size_t available = (count - guard - first) / (size_t)samples_per_symbol;

    if (available < 4U) {

        return 0;

    }

    size_t symbol_stride = available > (size_t)maximum_points ? available / (size_t)maximum_points : 1U;
    int half_window = samples_per_symbol / 10;

    if (half_window < 0) {

        half_window = 0;

    }

    if (half_window > 16) {

        half_window = 16;

    }

    int point_count = 0;

    for (size_t symbol = 0; symbol < available && point_count < maximum_points; symbol += symbol_stride) {
        size_t center = first + symbol * (size_t)samples_per_symbol;
        double sum_i = 0.0;
        double sum_q = 0.0;
        int samples = 0;

        for (int k = -half_window; k <= half_window; k++) {
            long long index = (long long)center + (long long)k;

            if (index < 0 || (size_t)index >= count) {

                continue;

            }

            sum_i += i_data[index];
            sum_q += q_data[index];
            samples++;
        }

        if (samples > 0) {

            points[point_count].i = sum_i / (double)samples;
            points[point_count].q = sum_q / (double)samples;
            point_count++;

        }
    }

    return point_count;
}

static double ANALYSIS_constellation_transition_timing_score(const double *i_data, const double *q_data, size_t count,
                                                             size_t guard, int samples_per_symbol, int center_offset) {
    /*
        Purpose: Scores a symbol timing candidate from transition concentration around the symbol center
        Returns: Timing score
    */

    if (!i_data || !q_data || samples_per_symbol < 4 || count <= guard * 2U + 4U) {

        return 1.0;

    }

    size_t region_start = guard > 1U ? guard : 1U;
    size_t region_end = count - guard;
    double total_energy = 0.0;

    for (size_t n = region_start; n < region_end; n++) {
        double delta_i = i_data[n] - i_data[n - 1U];
        double delta_q = q_data[n] - q_data[n - 1U];
        total_energy += delta_i * delta_i + delta_q * delta_q;
    }

    if (total_energy <= 1e-18) {

        return 1.0;

    }

    int half_window = samples_per_symbol / 30;

    if (half_window < 2) {

        half_window = 2;

    }

    if (half_window > 8) {

        half_window = 8;

    }

    long long first_boundary = (long long)guard + (long long)center_offset - samples_per_symbol / 2;

    while (first_boundary < (long long)region_start) {
        first_boundary += samples_per_symbol;
    }

    double captured_energy = 0.0;
    double center_energy = 0.0;
    size_t covered_samples = 0U;
    int boundary_count = 0;
    int center_count = 0;

    for (long long boundary = first_boundary; boundary < (long long)region_end; boundary += samples_per_symbol) {
        boundary_count++;

        for (int k = -half_window; k <= half_window; k++) {
            long long index = boundary + k;

            if (index <= 0 || index < (long long)region_start || index >= (long long)region_end) {

                continue;

            }

            double delta_i = i_data[index] - i_data[index - 1];
            double delta_q = q_data[index] - q_data[index - 1];
            captured_energy += delta_i * delta_i + delta_q * delta_q;
            covered_samples++;
        }
    }

    size_t first_center = guard + (size_t)center_offset;

    for (size_t center = first_center; center < region_end; center += (size_t)samples_per_symbol) {

        if (center == 0U) {

            continue;

        }

        double delta_i = i_data[center] - i_data[center - 1U];
        double delta_q = q_data[center] - q_data[center - 1U];
        center_energy += delta_i * delta_i + delta_q * delta_q;
        center_count++;
    }

    if (boundary_count < 2 || covered_samples == 0U) {

        return 0.1;

    }

    double region_samples = (double)(region_end - region_start);
    double coverage_fraction = (double)covered_samples / region_samples;
    double captured_fraction = captured_energy / total_energy;
    double concentration = captured_fraction / (coverage_fraction + 1e-12);
    double average_energy = total_energy / region_samples;
    double center_average = center_count > 0 ? center_energy / (double)center_count : average_energy;
    double center_penalty = 1.0 + center_average / (average_energy + 1e-18);

    return concentration * sqrt(ANALYSIS_limit_double(captured_fraction, 0.0, 1.0)) / center_penalty;
}

static int ANALYSIS_constellation_find_symbol_timing(const double *i_data, const double *q_data, size_t count,
                                                     double bandwidth_hz, int family, int psk_order,
                                                     int *best_samples_per_symbol, int *best_offset) {
    /*
        Purpose: Searches for the best samples-per-symbol value and sampling offset for the selected modulation family
        Returns: Success status
    */

    if (!i_data || !q_data || count < 256U || !best_samples_per_symbol || !best_offset ||
        Global_Analysis_Sample_Rate <= 0.0) {

        return 0;

    }

    double effective_bandwidth = bandwidth_hz;

    if (effective_bandwidth < Global_Analysis_Sample_Rate / 4096.0) {

        effective_bandwidth = Global_Analysis_Sample_Rate / 4096.0;

    }

    double estimated_sps = 1.35 * Global_Analysis_Sample_Rate / effective_bandwidth;
    int minimum_sps = (int)floor(estimated_sps * 0.35);
    int maximum_sps = (int)ceil(estimated_sps * 8.0);

    if (minimum_sps < 2) {

        minimum_sps = 2;

    }

    if (maximum_sps > 1024) {

        maximum_sps = 1024;

    }

    if (maximum_sps <= minimum_sps) {

        maximum_sps = minimum_sps + 1;

    }

    int sps_step = (maximum_sps - minimum_sps) / 96;

    if (sps_step < 1) {

        sps_step = 1;

    }

    size_t guard = count / 40U;

    if (guard < 128U) {

        guard = 128U;

    }

    if (guard * 2U >= count) {

        guard = 0U;

    }

    double best_score = -1.0;
    Type_Analysis_Constellation_Point candidate[384];

    for (int sps = minimum_sps; sps <= maximum_sps; sps += sps_step) {
        int offset_step = sps / 32;

        if (offset_step < 1) {

            offset_step = 1;

        }

        for (int offset = 0; offset < sps; offset += offset_step) {
            int candidate_count =
                ANALYSIS_constellation_collect_candidate(i_data, q_data, count, guard, sps, offset, candidate, 384);
            double score = 0.0;

            if (family == ANALYSIS_CONSTELLATION_MODE_PSK) {

                score = ANALYSIS_constellation_psk_score(candidate, candidate_count, psk_order);

            }

            else if (family == ANALYSIS_CONSTELLATION_MODE_QAM) {

                score = ANALYSIS_constellation_qam_score(candidate, candidate_count);

            }

            else {

                score = ANALYSIS_constellation_ask_score(candidate, candidate_count);

            }

            double transition_score =
                ANALYSIS_constellation_transition_timing_score(i_data, q_data, count, guard, sps, offset);
            transition_score = ANALYSIS_limit_double(transition_score, 0.05, 25.0);
            score *= sqrt(transition_score);

            if (score > best_score) {

                best_score = score;
                *best_samples_per_symbol = sps;
                *best_offset = offset;

            }
        }
    }

    return best_score > 0.0;
}

static void ANALYSIS_constellation_normalize_output(int preserve_origin) {
    /*
        Purpose: Normalizes the generated constellation output for display while optionally preserving the origin
        Returns: No value
    */

    int count = Global_Analysis_Const_Count;

    if (count <= 0) {

        return;

    }

    if (!preserve_origin) {

        double mean_i = 0.0;
        double mean_q = 0.0;

        for (int p = 0; p < count; p++) {
            mean_i += Global_Analysis_Const_I[p];
            mean_q += Global_Analysis_Const_Q[p];
        }

        mean_i /= (double)count;
        mean_q /= (double)count;

        for (int p = 0; p < count; p++) {
            Global_Analysis_Const_I[p] = (float)((double)Global_Analysis_Const_I[p] - mean_i);
            Global_Analysis_Const_Q[p] = (float)((double)Global_Analysis_Const_Q[p] - mean_q);
        }

    }

    double *components = malloc((size_t)count * sizeof(double));

    if (!components) {

        return;

    }

    for (int p = 0; p < count; p++) {
        double absolute_i = fabs((double)Global_Analysis_Const_I[p]);
        double absolute_q = fabs((double)Global_Analysis_Const_Q[p]);
        components[p] = absolute_i > absolute_q ? absolute_i : absolute_q;
    }

    qsort(components, (size_t)count, sizeof(double), ANALYSIS_constellation_double_compare);
    int percentile_index = (int)((double)(count - 1) * 0.985);
    double scale = components[percentile_index];
    free(components);

    if (scale < 1e-12) {

        return;

    }

    scale /= 0.86;

    for (int p = 0; p < count; p++) {
        double normalized_i = (double)Global_Analysis_Const_I[p] / scale;
        double normalized_q = (double)Global_Analysis_Const_Q[p] / scale;

        Global_Analysis_Const_I[p] = (float)ANALYSIS_limit_double(normalized_i, -1.0, 1.0);
        Global_Analysis_Const_Q[p] = (float)ANALYSIS_limit_double(normalized_q, -1.0, 1.0);
    }
}

static double ANALYSIS_constellation_estimate_psk_frequency_fft(const double *i_data, const double *q_data,
                                                                size_t count, int order, double magnitude_gate,
                                                                double maximum_offset_hz) {
    /*
        Purpose: Estimates PSK carrier frequency offset with an FFT-based Mth-power measurement
        Returns: Estimated radians per sample
    */

    if (!i_data || !q_data || count < 64U || order < 2 || Global_Analysis_Sample_Rate <= 0.0) {

        return 0.0;

    }

    size_t fft_size = ANALYSIS_constellation_next_power_of_two(count);

    if (fft_size < count || fft_size > ANALYSIS_CONSTELLATION_MAX_INPUT) {

        return 0.0;

    }

    fftw_complex *time_data = fftw_malloc(sizeof(fftw_complex) * fft_size);
    fftw_complex *frequency_data = fftw_malloc(sizeof(fftw_complex) * fft_size);

    if (!time_data || !frequency_data) {

        if (time_data) {

            fftw_free(time_data);

        }

        if (frequency_data) {

            fftw_free(frequency_data);

        }
        return 0.0;

    }

    for (size_t n = 0; n < fft_size; n++) {
        time_data[n][0] = 0.0;
        time_data[n][1] = 0.0;
    }

    for (size_t n = 0; n < count; n++) {
        double magnitude = hypot(i_data[n], q_data[n]);

        if (magnitude < magnitude_gate) {

            continue;

        }

        double powered_i = 0.0;
        double powered_q = 0.0;
        double unit_i = i_data[n] / magnitude;
        double unit_q = q_data[n] / magnitude;
        double window = count > 1U ? 0.5 - 0.5 * cos((2.0 * M_PI * (double)n) / (double)(count - 1U)) : 1.0;

        ANALYSIS_constellation_complex_power(unit_i, unit_q, order, &powered_i, &powered_q);
        time_data[n][0] = powered_i * window;
        time_data[n][1] = powered_q * window;
    }

    fftw_plan plan = fftw_plan_dft_1d((int)fft_size, time_data, frequency_data, FFTW_FORWARD, FFTW_ESTIMATE);

    if (!plan) {

        fftw_free(time_data);
        fftw_free(frequency_data);
        return 0.0;

    }

    fftw_execute(plan);

    double maximum_power = -1.0;
    size_t maximum_bin = 0U;
    double search_hz = maximum_offset_hz * (double)order;

    if (search_hz < Global_Analysis_Sample_Rate / (double)fft_size) {

        search_hz = Global_Analysis_Sample_Rate / (double)fft_size;

    }

    if (search_hz > Global_Analysis_Sample_Rate * 0.48) {

        search_hz = Global_Analysis_Sample_Rate * 0.48;

    }

    for (size_t bin = 0; bin < fft_size; bin++) {
        long long signed_bin = bin <= fft_size / 2U ? (long long)bin : (long long)bin - (long long)fft_size;
        double frequency_hz = (double)signed_bin * Global_Analysis_Sample_Rate / (double)fft_size;

        if (fabs(frequency_hz) > search_hz) {

            continue;

        }

        double power =
            frequency_data[bin][0] * frequency_data[bin][0] + frequency_data[bin][1] * frequency_data[bin][1];

        if (power > maximum_power) {

            maximum_power = power;
            maximum_bin = bin;

        }
    }

    double fractional_bin = 0.0;

    if (maximum_bin > 0U && maximum_bin + 1U < fft_size) {

        double left_power = frequency_data[maximum_bin - 1U][0] * frequency_data[maximum_bin - 1U][0] +
                            frequency_data[maximum_bin - 1U][1] * frequency_data[maximum_bin - 1U][1];
        double center_power = frequency_data[maximum_bin][0] * frequency_data[maximum_bin][0] +
                              frequency_data[maximum_bin][1] * frequency_data[maximum_bin][1];
        double right_power = frequency_data[maximum_bin + 1U][0] * frequency_data[maximum_bin + 1U][0] +
                             frequency_data[maximum_bin + 1U][1] * frequency_data[maximum_bin + 1U][1];
        double left_log = log(left_power + 1e-30);
        double center_log = log(center_power + 1e-30);
        double right_log = log(right_power + 1e-30);
        double denominator = left_log - 2.0 * center_log + right_log;

        if (fabs(denominator) > 1e-12) {

            fractional_bin = 0.5 * (left_log - right_log) / denominator;
            fractional_bin = ANALYSIS_limit_double(fractional_bin, -0.5, 0.5);

        }

    }

    long long signed_peak_bin =
        maximum_bin <= fft_size / 2U ? (long long)maximum_bin : (long long)maximum_bin - (long long)fft_size;
    double powered_frequency_hz =
        ((double)signed_peak_bin + fractional_bin) * Global_Analysis_Sample_Rate / (double)fft_size;
    double carrier_frequency_hz = powered_frequency_hz / (double)order;

    fftw_destroy_plan(plan);
    fftw_free(time_data);
    fftw_free(frequency_data);

    return 2.0 * M_PI * carrier_frequency_hz / Global_Analysis_Sample_Rate;
}

static double ANALYSIS_constellation_estimate_qam_frequency_fft(const double *i_data, const double *q_data,
                                                                size_t count, double magnitude_gate,
                                                                double maximum_offset_hz) {
    /*
        Purpose: Estimates QAM carrier frequency offset with an FFT-based measurement
        Returns: Estimated radians per sample
    */

    if (!i_data || !q_data || count < 64U || Global_Analysis_Sample_Rate <= 0.0) {

        return 0.0;

    }

    size_t fft_size = ANALYSIS_constellation_next_power_of_two(count);

    if (fft_size < count || fft_size > ANALYSIS_CONSTELLATION_MAX_INPUT) {

        return 0.0;

    }

    fftw_complex *time_data = fftw_malloc(sizeof(fftw_complex) * fft_size);
    fftw_complex *frequency_data = fftw_malloc(sizeof(fftw_complex) * fft_size);

    if (!time_data || !frequency_data) {

        if (time_data) {

            fftw_free(time_data);

        }

        if (frequency_data) {

            fftw_free(frequency_data);

        }
        return 0.0;

    }

    double average_power = 0.0;

    for (size_t n = 0; n < count; n++) {
        average_power += i_data[n] * i_data[n] + q_data[n] * q_data[n];
    }

    double rms = sqrt(average_power / (double)count);

    if (rms < 1e-12) {

        fftw_free(time_data);
        fftw_free(frequency_data);
        return 0.0;

    }

    for (size_t n = 0; n < fft_size; n++) {
        time_data[n][0] = 0.0;
        time_data[n][1] = 0.0;
    }

    for (size_t n = 0; n < count; n++) {
        double magnitude = hypot(i_data[n], q_data[n]);

        if (magnitude < magnitude_gate) {

            continue;

        }

        double normalized_i = i_data[n] / rms;
        double normalized_q = q_data[n] / rms;
        double normalized_magnitude = hypot(normalized_i, normalized_q);

        /* Limit impulsive samples without discarding QAM amplitude levels. */

        if (normalized_magnitude > 4.0) {

            normalized_i *= 4.0 / normalized_magnitude;
            normalized_q *= 4.0 / normalized_magnitude;

        }

        double powered_i = 0.0;
        double powered_q = 0.0;
        double window = count > 1U ? 0.5 - 0.5 * cos((2.0 * M_PI * (double)n) / (double)(count - 1U)) : 1.0;

        /* Square QAM has a non-zero fourth moment at four times the CFO. */
        ANALYSIS_constellation_complex_power(normalized_i, normalized_q, 4, &powered_i, &powered_q);
        time_data[n][0] = powered_i * window;
        time_data[n][1] = powered_q * window;
    }

    fftw_plan plan = fftw_plan_dft_1d((int)fft_size, time_data, frequency_data, FFTW_FORWARD, FFTW_ESTIMATE);

    if (!plan) {

        fftw_free(time_data);
        fftw_free(frequency_data);
        return 0.0;

    }

    fftw_execute(plan);

    double search_hz = maximum_offset_hz * 4.0;

    if (search_hz < Global_Analysis_Sample_Rate / (double)fft_size) {

        search_hz = Global_Analysis_Sample_Rate / (double)fft_size;

    }

    if (search_hz > Global_Analysis_Sample_Rate * 0.48) {

        search_hz = Global_Analysis_Sample_Rate * 0.48;

    }

    double maximum_power = -1.0;
    size_t maximum_bin = 0U;

    for (size_t bin = 0; bin < fft_size; bin++) {
        long long signed_bin = bin <= fft_size / 2U ? (long long)bin : (long long)bin - (long long)fft_size;
        double frequency_hz = (double)signed_bin * Global_Analysis_Sample_Rate / (double)fft_size;

        if (fabs(frequency_hz) > search_hz) {

            continue;

        }

        double power =
            frequency_data[bin][0] * frequency_data[bin][0] + frequency_data[bin][1] * frequency_data[bin][1];

        if (power > maximum_power) {

            maximum_power = power;
            maximum_bin = bin;

        }
    }

    double fractional_bin = 0.0;
    size_t left_bin = maximum_bin == 0U ? fft_size - 1U : maximum_bin - 1U;
    size_t right_bin = maximum_bin + 1U == fft_size ? 0U : maximum_bin + 1U;
    double left_power = frequency_data[left_bin][0] * frequency_data[left_bin][0] +
                        frequency_data[left_bin][1] * frequency_data[left_bin][1];
    double center_power = frequency_data[maximum_bin][0] * frequency_data[maximum_bin][0] +
                          frequency_data[maximum_bin][1] * frequency_data[maximum_bin][1];
    double right_power = frequency_data[right_bin][0] * frequency_data[right_bin][0] +
                         frequency_data[right_bin][1] * frequency_data[right_bin][1];
    double left_log = log(left_power + 1e-30);
    double center_log = log(center_power + 1e-30);
    double right_log = log(right_power + 1e-30);
    double denominator = left_log - 2.0 * center_log + right_log;

    if (fabs(denominator) > 1e-12) {

        fractional_bin = 0.5 * (left_log - right_log) / denominator;
        fractional_bin = ANALYSIS_limit_double(fractional_bin, -0.5, 0.5);

    }

    long long signed_peak_bin =
        maximum_bin <= fft_size / 2U ? (long long)maximum_bin : (long long)maximum_bin - (long long)fft_size;
    double fourth_power_frequency_hz =
        ((double)signed_peak_bin + fractional_bin) * Global_Analysis_Sample_Rate / (double)fft_size;
    double carrier_frequency_hz = fourth_power_frequency_hz / 4.0;

    fftw_destroy_plan(plan);
    fftw_free(time_data);
    fftw_free(frequency_data);

    return 2.0 * M_PI * carrier_frequency_hz / Global_Analysis_Sample_Rate;
}

static double ANALYSIS_constellation_estimate_spectral_center(const double *i_data, const double *q_data, size_t count,
                                                              double maximum_offset_hz) {
    /*
        Purpose: Estimates the spectral center frequency offset of the constellation input
        Returns: Estimated radians per sample
    */

    if (!i_data || !q_data || count < 64U || Global_Analysis_Sample_Rate <= 0.0) {

        return 0.0;

    }

    size_t fft_size = ANALYSIS_constellation_next_power_of_two(count);

    if (fft_size < count || fft_size > ANALYSIS_CONSTELLATION_MAX_INPUT) {

        return 0.0;

    }

    fftw_complex *time_data = fftw_malloc(sizeof(fftw_complex) * fft_size);
    fftw_complex *frequency_data = fftw_malloc(sizeof(fftw_complex) * fft_size);
    double *selected_power = malloc(fft_size * sizeof(double));

    if (!time_data || !frequency_data || !selected_power) {

        if (time_data) {

            fftw_free(time_data);

        }

        if (frequency_data) {

            fftw_free(frequency_data);

        }
        free(selected_power);
        return 0.0;

    }

    for (size_t n = 0; n < fft_size; n++) {

        if (n < count) {

            double window = count > 1U ? 0.5 - 0.5 * cos((2.0 * M_PI * (double)n) / (double)(count - 1U)) : 1.0;
            time_data[n][0] = i_data[n] * window;
            time_data[n][1] = q_data[n] * window;

        }

        else {

            time_data[n][0] = 0.0;
            time_data[n][1] = 0.0;

        }
    }

    fftw_plan plan = fftw_plan_dft_1d((int)fft_size, time_data, frequency_data, FFTW_FORWARD, FFTW_ESTIMATE);

    if (!plan) {

        fftw_free(time_data);
        fftw_free(frequency_data);
        free(selected_power);
        return 0.0;

    }

    fftw_execute(plan);

    if (maximum_offset_hz < Global_Analysis_Sample_Rate / (double)fft_size) {

        maximum_offset_hz = Global_Analysis_Sample_Rate / (double)fft_size;

    }

    if (maximum_offset_hz > Global_Analysis_Sample_Rate * 0.48) {

        maximum_offset_hz = Global_Analysis_Sample_Rate * 0.48;

    }

    size_t selected_count = 0U;

    for (size_t bin = 0; bin < fft_size; bin++) {
        long long signed_bin = bin <= fft_size / 2U ? (long long)bin : (long long)bin - (long long)fft_size;
        double frequency_hz = (double)signed_bin * Global_Analysis_Sample_Rate / (double)fft_size;

        if (fabs(frequency_hz) <= maximum_offset_hz) {

            selected_power[selected_count++] =
                frequency_data[bin][0] * frequency_data[bin][0] + frequency_data[bin][1] * frequency_data[bin][1];

        }
    }

    if (selected_count == 0U) {

        fftw_destroy_plan(plan);
        fftw_free(time_data);
        fftw_free(frequency_data);
        free(selected_power);
        return 0.0;

    }

    qsort(selected_power, selected_count, sizeof(double), ANALYSIS_constellation_double_compare);
    double noise_power = selected_power[(size_t)(0.20 * (double)(selected_count - 1U))];
    double weighted_frequency = 0.0;
    double weight_sum = 0.0;

    for (size_t bin = 0; bin < fft_size; bin++) {
        long long signed_bin = bin <= fft_size / 2U ? (long long)bin : (long long)bin - (long long)fft_size;
        double frequency_hz = (double)signed_bin * Global_Analysis_Sample_Rate / (double)fft_size;

        if (fabs(frequency_hz) > maximum_offset_hz) {

            continue;

        }

        double power =
            frequency_data[bin][0] * frequency_data[bin][0] + frequency_data[bin][1] * frequency_data[bin][1];
        double weight = power - 2.0 * noise_power;

        if (weight > 0.0) {

            weighted_frequency += frequency_hz * weight;
            weight_sum += weight;

        }
    }

    fftw_destroy_plan(plan);
    fftw_free(time_data);
    fftw_free(frequency_data);
    free(selected_power);

    if (weight_sum <= 1e-18) {

        return 0.0;

    }

    return 2.0 * M_PI * (weighted_frequency / weight_sum) / Global_Analysis_Sample_Rate;
}

typedef struct Type_Analysis_Constellation_Rate_Peak {
    double frequency_hz;
    double normalized_power;
} Type_Analysis_Constellation_Rate_Peak;

static double ANALYSIS_constellation_estimate_symbol_rate(const double *i_data, const double *q_data, size_t count,
                                                          double bandwidth_hz, double magnitude_gate) {
    /*
        Purpose: Estimates the symbol rate from the prepared constellation IQ samples
        Returns: Estimated symbol rate in hertz
    */

    if (!i_data || !q_data || count < 256U || Global_Analysis_Sample_Rate <= 0.0) {

        return 0.0;

    }

    size_t difference_count = count - 1U;
    size_t fft_size = ANALYSIS_constellation_next_power_of_two(difference_count);

    if (fft_size < difference_count || fft_size > ANALYSIS_CONSTELLATION_MAX_INPUT) {

        return 0.0;

    }

    double *difference = malloc(difference_count * sizeof(double));
    fftw_complex *time_data = fftw_malloc(sizeof(fftw_complex) * fft_size);
    fftw_complex *frequency_data = fftw_malloc(sizeof(fftw_complex) * fft_size);

    if (!difference || !time_data || !frequency_data) {

        free(difference);

        if (time_data) {

            fftw_free(time_data);

        }

        if (frequency_data) {

            fftw_free(frequency_data);

        }
        return 0.0;

    }

    double mean_difference = 0.0;

    for (size_t n = 1; n < count; n++) {
        double previous_magnitude = hypot(i_data[n - 1U], q_data[n - 1U]);
        double current_magnitude = hypot(i_data[n], q_data[n]);
        double value = 0.0;

        if (previous_magnitude >= magnitude_gate && current_magnitude >= magnitude_gate) {

            double delta_i = i_data[n] - i_data[n - 1U];
            double delta_q = q_data[n] - q_data[n - 1U];
            value = delta_i * delta_i + delta_q * delta_q;

        }

        difference[n - 1U] = value;
        mean_difference += value;
    }

    mean_difference /= (double)difference_count;

    for (size_t n = 0; n < fft_size; n++) {

        if (n < difference_count) {

            double value = difference[n] - mean_difference;

            if (n > 0U && n + 1U < difference_count) {

                value = (difference[n - 1U] + difference[n] + difference[n + 1U]) / 3.0 - mean_difference;

            }

            double window = difference_count > 1U
                                ? 0.5 - 0.5 * cos((2.0 * M_PI * (double)n) / (double)(difference_count - 1U))
                                : 1.0;
            time_data[n][0] = value * window;
            time_data[n][1] = 0.0;

        }

        else {

            time_data[n][0] = 0.0;
            time_data[n][1] = 0.0;

        }
    }

    free(difference);

    fftw_plan plan = fftw_plan_dft_1d((int)fft_size, time_data, frequency_data, FFTW_FORWARD, FFTW_ESTIMATE);

    if (!plan) {

        fftw_free(time_data);
        fftw_free(frequency_data);
        return 0.0;

    }

    fftw_execute(plan);

    double minimum_rate_hz = bandwidth_hz > 0.0 ? bandwidth_hz / 64.0 : Global_Analysis_Sample_Rate / 4096.0;
    double maximum_rate_hz = bandwidth_hz > 0.0 ? bandwidth_hz * 8.0 : Global_Analysis_Sample_Rate / 8.0;

    if (minimum_rate_hz < 100.0) {

        minimum_rate_hz = 100.0;

    }

    if (maximum_rate_hz > Global_Analysis_Sample_Rate * 0.25) {

        maximum_rate_hz = Global_Analysis_Sample_Rate * 0.25;

    }

    if (maximum_rate_hz <= minimum_rate_hz) {

        maximum_rate_hz = minimum_rate_hz * 2.0;

    }

    Type_Analysis_Constellation_Rate_Peak peaks[24];
    int peak_count = 0;
    double maximum_peak_power = 0.0;

    memset(peaks, 0, sizeof(peaks));

    for (size_t bin = 1U; bin + 1U <= fft_size / 2U; bin++) {
        double frequency_hz = (double)bin * Global_Analysis_Sample_Rate / (double)fft_size;

        if (frequency_hz < minimum_rate_hz || frequency_hz > maximum_rate_hz) {

            continue;

        }

        double power =
            frequency_data[bin][0] * frequency_data[bin][0] + frequency_data[bin][1] * frequency_data[bin][1];
        double left_power = frequency_data[bin - 1U][0] * frequency_data[bin - 1U][0] +
                            frequency_data[bin - 1U][1] * frequency_data[bin - 1U][1];
        double right_power = frequency_data[bin + 1U][0] * frequency_data[bin + 1U][0] +
                             frequency_data[bin + 1U][1] * frequency_data[bin + 1U][1];

        if (power <= left_power || power < right_power) {

            continue;

        }

        if (peak_count >= 24 && power <= peaks[23].normalized_power) {

            continue;

        }

        int insertion = peak_count;

        if (insertion > 23) {

            insertion = 23;

        }

        while (insertion > 0 && peaks[insertion - 1].normalized_power < power) {

            if (insertion < 24) {

                peaks[insertion] = peaks[insertion - 1];

            }
            insertion--;
        }

        if (insertion < 24) {

            peaks[insertion].frequency_hz = frequency_hz;
            peaks[insertion].normalized_power = power;

            if (peak_count < 24) {

                peak_count++;

            }

        }

        if (power > maximum_peak_power) {

            maximum_peak_power = power;

        }
    }

    fftw_destroy_plan(plan);
    fftw_free(time_data);
    fftw_free(frequency_data);

    if (peak_count <= 0 || maximum_peak_power <= 0.0) {

        return bandwidth_hz > 0.0 ? bandwidth_hz * 0.5 : 0.0;

    }

    for (int p = 0; p < peak_count; p++) {
        peaks[p].normalized_power /= maximum_peak_power;
    }

    double best_rate_hz = 0.0;
    double best_score = -1.0;

    for (int source = 0; source < peak_count; source++) {
        for (int divisor = 1; divisor <= 12; divisor++) {
            double candidate_rate_hz = peaks[source].frequency_hz / (double)divisor;
            double samples_per_symbol = Global_Analysis_Sample_Rate / candidate_rate_hz;

            if (candidate_rate_hz < 100.0 || samples_per_symbol < 2.0 || samples_per_symbol > 2048.0) {

                continue;

            }

            double score = 0.0;

            for (int p = 0; p < peak_count; p++) {
                int harmonic = (int)llround(peaks[p].frequency_hz / candidate_rate_hz);

                if (harmonic < 1 || harmonic > 16) {

                    continue;

                }

                double relative_error =
                    fabs(peaks[p].frequency_hz - (double)harmonic * candidate_rate_hz) / candidate_rate_hz;

                if (relative_error < 0.03) {

                    score += peaks[p].normalized_power / pow((double)harmonic, 0.35) *
                             exp(-pow(relative_error / 0.012, 2.0));

                }
            }

            score *= 1.0 + 0.03 * log(candidate_rate_hz);

            if (score > best_score) {

                best_score = score;
                best_rate_hz = candidate_rate_hz;

            }
        }
    }

    return best_rate_hz;
}

static int ANALYSIS_constellation_find_center_offset(const double *i_data, const double *q_data, size_t count,
                                                     size_t guard, int samples_per_symbol, int *relative_center_offset,
                                                     double *transition_concentration) {
    /*
        Purpose: Finds the sampling-center offset within a symbol period from transition concentration
        Returns: Success status
    */

    if (!i_data || !q_data || samples_per_symbol < 2 || count <= guard * 2U + 4U || !relative_center_offset) {

        return 0;

    }

    double *histogram = calloc((size_t)samples_per_symbol, sizeof(double));
    int *histogram_count = calloc((size_t)samples_per_symbol, sizeof(int));
    double *smoothed = calloc((size_t)samples_per_symbol, sizeof(double));

    if (!histogram || !histogram_count || !smoothed) {

        free(histogram);
        free(histogram_count);
        free(smoothed);
        return 0;

    }

    size_t start = guard > 1U ? guard : 1U;
    size_t end = count - guard;

    for (size_t n = start; n < end; n++) {
        double delta_i = i_data[n] - i_data[n - 1U];
        double delta_q = q_data[n] - q_data[n - 1U];
        int phase = (int)(n % (size_t)samples_per_symbol);

        histogram[phase] += delta_i * delta_i + delta_q * delta_q;
        histogram_count[phase]++;
    }

    for (int phase = 0; phase < samples_per_symbol; phase++) {

        if (histogram_count[phase] > 0) {

            histogram[phase] /= (double)histogram_count[phase];

        }
    }

    int smoothing_radius = samples_per_symbol / 100;

    if (smoothing_radius < 1) {

        smoothing_radius = 1;

    }

    if (smoothing_radius > 6) {

        smoothing_radius = 6;

    }

    double mean_smoothed = 0.0;
    double maximum_smoothed = -1.0;
    int boundary_phase = 0;

    for (int phase = 0; phase < samples_per_symbol; phase++) {
        for (int offset = -smoothing_radius; offset <= smoothing_radius; offset++) {
            int index = phase + offset;

            while (index < 0) {
                index += samples_per_symbol;
            }

            while (index >= samples_per_symbol) {
                index -= samples_per_symbol;
            }

            smoothed[phase] += histogram[index];
        }

        mean_smoothed += smoothed[phase];

        if (smoothed[phase] > maximum_smoothed) {

            maximum_smoothed = smoothed[phase];
            boundary_phase = phase;

        }
    }

    mean_smoothed /= (double)samples_per_symbol;

    int center_phase = (boundary_phase + samples_per_symbol / 2) % samples_per_symbol;
    int guard_phase = (int)(guard % (size_t)samples_per_symbol);
    int relative_offset = center_phase - guard_phase;

    while (relative_offset < 0) {
        relative_offset += samples_per_symbol;
    }

    while (relative_offset >= samples_per_symbol) {
        relative_offset -= samples_per_symbol;
    }

    *relative_center_offset = relative_offset;

    if (transition_concentration) {

        *transition_concentration = maximum_smoothed / (mean_smoothed + 1e-18);

    }

    free(histogram);
    free(histogram_count);
    free(smoothed);
    return 1;
}

static int ANALYSIS_constellation_find_symbol_timing_v2(const double *i_data, const double *q_data, size_t count,
                                                        double bandwidth_hz, int family, int psk_order,
                                                        double magnitude_gate, int *best_samples_per_symbol,
                                                        int *best_offset) {
    /*
        Purpose: Performs the refined symbol timing search for the selected modulation family
        Returns: Success status
    */

    if (!i_data || !q_data || count < 256U || !best_samples_per_symbol || !best_offset ||
        Global_Analysis_Sample_Rate <= 0.0) {

        return 0;

    }

    double effective_bandwidth = bandwidth_hz;

    if (effective_bandwidth < Global_Analysis_Sample_Rate / 4096.0) {

        effective_bandwidth = Global_Analysis_Sample_Rate / 4096.0;

    }

    /*
     * The file stores one interleaved signed complex16 IQ pair per sample:
     * I0,Q0,I1,Q1,... .  Global_Analysis_Sample_Rate is therefore the number
     * of complete IQ pairs per second, not the number of int16 values.
     *
     * Search the symbol period directly in samples-per-symbol.  The previous
     * implementation first estimated a rate from the FFT of transition
     * energy. Repeating training patterns can put a stronger line at a
     * sub-harmonic of the real symbol rate, which caused valid PSK/QAM samples
     * to be taken between symbols and produced circles or dense clouds.
     * Folding transition energy modulo each candidate period identifies the
     * actual symbol boundaries without making that sub-harmonic assumption.
     */
    double minimum_rate_hz = effective_bandwidth / 64.0;
    double maximum_rate_hz = effective_bandwidth * 2.0;

    if (minimum_rate_hz < 100.0) {

        minimum_rate_hz = 100.0;

    }

    if (maximum_rate_hz > Global_Analysis_Sample_Rate * 0.5) {

        maximum_rate_hz = Global_Analysis_Sample_Rate * 0.5;

    }

    if (maximum_rate_hz <= minimum_rate_hz) {

        maximum_rate_hz = minimum_rate_hz * 2.0;

    }

    int minimum_sps = (int)floor(Global_Analysis_Sample_Rate / maximum_rate_hz) - 2;
    int maximum_sps = (int)ceil(Global_Analysis_Sample_Rate / minimum_rate_hz) + 2;

    if (minimum_sps < 2) {

        minimum_sps = 2;

    }

    if (minimum_sps > 2048) {

        minimum_sps = 2048;

    }

    if (maximum_sps > 2048) {

        maximum_sps = 2048;

    }

    if (maximum_sps < minimum_sps) {

        maximum_sps = minimum_sps;

    }

    size_t guard = count / 40U;

    if (guard < 128U) {

        guard = 128U;

    }

    if (guard * 2U >= count) {

        guard = 0U;

    }

    size_t timing_start = guard > 1U ? guard : 1U;
    size_t timing_end = count - guard;
    const size_t maximum_timing_samples = 65536U;

    if (timing_end > timing_start + maximum_timing_samples) {

        timing_end = timing_start + maximum_timing_samples;

    }

    if (timing_end <= timing_start + 32U) {

        return 0;

    }

    size_t transition_count = timing_end - timing_start;
    double *transition_energy = malloc(transition_count * sizeof(double));
    double *histogram = calloc((size_t)maximum_sps, sizeof(double));
    int *histogram_count = calloc((size_t)maximum_sps, sizeof(int));
    double *smoothed = calloc((size_t)maximum_sps, sizeof(double));

    if (!transition_energy || !histogram || !histogram_count || !smoothed) {

        free(transition_energy);
        free(histogram);
        free(histogram_count);
        free(smoothed);
        return 0;

    }

    for (size_t index = 0; index < transition_count; index++) {
        size_t n = timing_start + index;
        double previous_magnitude = hypot(i_data[n - 1U], q_data[n - 1U]);
        double current_magnitude = hypot(i_data[n], q_data[n]);

        if (previous_magnitude < magnitude_gate || current_magnitude < magnitude_gate) {

            transition_energy[index] = 0.0;
            continue;

        }

        double delta_i = i_data[n] - i_data[n - 1U];
        double delta_q = q_data[n] - q_data[n - 1U];
        transition_energy[index] = delta_i * delta_i + delta_q * delta_q;
    }

    Type_Analysis_Constellation_Point candidate[384];
    double best_score = -1.0;

    for (int sps = minimum_sps; sps <= maximum_sps; sps++) {
        memset(histogram, 0, (size_t)sps * sizeof(double));
        memset(histogram_count, 0, (size_t)sps * sizeof(int));
        memset(smoothed, 0, (size_t)sps * sizeof(double));

        int phase = (int)(timing_start % (size_t)sps);

        for (size_t index = 0; index < transition_count; index++) {
            histogram[phase] += transition_energy[index];
            histogram_count[phase]++;

            phase++;

            if (phase >= sps) {

                phase = 0;

            }
        }

        for (phase = 0; phase < sps; phase++) {

            if (histogram_count[phase] > 0) {

                histogram[phase] /= (double)histogram_count[phase];

            }
        }

        int smoothing_radius = sps / 100;

        if (smoothing_radius < 1) {

            smoothing_radius = 1;

        }

        if (smoothing_radius > 6) {

            smoothing_radius = 6;

        }

        double mean_smoothed = 0.0;
        double maximum_smoothed = -1.0;
        int boundary_phase = 0;

        for (phase = 0; phase < sps; phase++) {
            for (int delta = -smoothing_radius; delta <= smoothing_radius; delta++) {
                int folded_phase = phase + delta;

                while (folded_phase < 0) {
                    folded_phase += sps;
                }

                while (folded_phase >= sps) {
                    folded_phase -= sps;
                }

                smoothed[phase] += histogram[folded_phase];
            }

            mean_smoothed += smoothed[phase];

            if (smoothed[phase] > maximum_smoothed) {

                maximum_smoothed = smoothed[phase];
                boundary_phase = phase;

            }
        }

        mean_smoothed /= (double)sps;

        if (mean_smoothed <= 1e-18 || maximum_smoothed <= 0.0) {

            continue;

        }

        double concentration = maximum_smoothed / mean_smoothed;
        int center_phase = (boundary_phase + sps / 2) % sps;
        int guard_phase = (int)(guard % (size_t)sps);
        int offset = center_phase - guard_phase;

        while (offset < 0) {
            offset += sps;
        }

        while (offset >= sps) {
            offset -= sps;
        }

        int candidate_count =
            ANALYSIS_constellation_collect_candidate(i_data, q_data, count, guard, sps, offset, candidate, 384);
        double cluster_score = 0.0;

        if (family == ANALYSIS_CONSTELLATION_MODE_PSK) {

            cluster_score = ANALYSIS_constellation_psk_score(candidate, candidate_count, psk_order);

        }

        else if (family == ANALYSIS_CONSTELLATION_MODE_QAM) {

            cluster_score = ANALYSIS_constellation_qam_score(candidate, candidate_count);

        }

        else {

            cluster_score = ANALYSIS_constellation_ask_score(candidate, candidate_count);

        }

        if (cluster_score <= 0.0) {

            continue;

        }

        /*
         * A real symbol period concentrates transition energy at one folded
         * phase.  Multiples can also concentrate transitions, so prefer the
         * shortest period that still produces clean modulation-family
         * clusters.  This selects 125 samples/symbol for a 2.5 MS/s capture of
         * a 20 ksymbol/s signal instead of 250, 375, or 500.
         */
        double timing_excess = concentration - 1.0;

        if (timing_excess < 0.0001) {

            timing_excess = 0.0001;

        }

        double score = cluster_score * timing_excess / (double)sps;

        if (score > best_score) {

            best_score = score;
            *best_samples_per_symbol = sps;
            *best_offset = offset;

        }
    }

    free(transition_energy);
    free(histogram);
    free(histogram_count);
    free(smoothed);

    return best_score > 0.0;
}

static double ANALYSIS_constellation_psk_residual_frequency(const Type_Analysis_Constellation_Point *points, int count,
                                                            int order, int samples_per_symbol) {
    /*
        Purpose: Estimates residual PSK frequency error from symbol-spaced constellation points
        Returns: Estimated radians per sample
    */

    if (!points || count < 8 || order < 1 || samples_per_symbol < 1) {

        return 0.0;

    }

    double sum_i = 0.0;
    double sum_q = 0.0;
    double previous_i = 0.0;
    double previous_q = 0.0;
    int have_previous = 0;

    for (int p = 0; p < count; p++) {
        double magnitude = hypot(points[p].i, points[p].q);

        if (magnitude < 1e-12) {

            have_previous = 0;
            continue;

        }

        double powered_i = 0.0;
        double powered_q = 0.0;
        ANALYSIS_constellation_complex_power(points[p].i / magnitude, points[p].q / magnitude, order, &powered_i,
                                             &powered_q);

        if (have_previous) {

            sum_i += powered_i * previous_i + powered_q * previous_q;
            sum_q += powered_q * previous_i - powered_i * previous_q;

        }

        previous_i = powered_i;
        previous_q = powered_q;
        have_previous = 1;
    }

    return hypot(sum_i, sum_q) > 1e-12 ? atan2(sum_q, sum_i) / ((double)order * (double)samples_per_symbol) : 0.0;
}

static double ANALYSIS_constellation_qam_residual_frequency(const Type_Analysis_Constellation_Point *points, int count,
                                                            int samples_per_symbol, double symbol_rate_hz) {
    /*
        Purpose: Estimates residual QAM frequency error from symbol-spaced constellation points
        Returns: Estimated radians per sample
    */

    const int block_symbols = 32;

    if (!points || count < block_symbols * 4 || samples_per_symbol < 1 || symbol_rate_hz <= 0.0) {

        return 0.0;

    }

    int block_count = count / block_symbols;
    double previous_i = 0.0;
    double previous_q = 0.0;
    int have_previous = 0;
    double correlation_i = 0.0;
    double correlation_q = 0.0;
    double correlation_power = 0.0;

    for (int block = 0; block < block_count; block++) {
        double block_i = 0.0;
        double block_q = 0.0;

        for (int symbol = 0; symbol < block_symbols; symbol++) {
            const Type_Analysis_Constellation_Point *point = &points[block * block_symbols + symbol];
            double powered_i = 0.0;
            double powered_q = 0.0;

            ANALYSIS_constellation_complex_power(point->i, point->q, 4, &powered_i, &powered_q);
            block_i += powered_i;
            block_q += powered_q;
        }

        block_i /= (double)block_symbols;
        block_q /= (double)block_symbols;

        if (have_previous) {

            correlation_i += block_i * previous_i + block_q * previous_q;
            correlation_q += block_q * previous_i - block_i * previous_q;
            correlation_power += hypot(block_i, block_q) * hypot(previous_i, previous_q);

        }

        previous_i = block_i;
        previous_q = block_q;
        have_previous = 1;
    }

    double coherence = correlation_power > 1e-18 ? hypot(correlation_i, correlation_q) / correlation_power : 0.0;

    if (coherence < 0.12) {

        return 0.0;

    }

    double radians_per_sample =
        atan2(correlation_q, correlation_i) / (4.0 * (double)block_symbols * (double)samples_per_symbol);
    double residual_hz = radians_per_sample * Global_Analysis_Sample_Rate / (2.0 * M_PI);
    double maximum_residual_hz = symbol_rate_hz / 128.0;

    if (maximum_residual_hz < 20.0) {

        maximum_residual_hz = 20.0;

    }

    if (fabs(residual_hz) > maximum_residual_hz) {

        return 0.0;

    }

    return radians_per_sample;
}

static void ANALYSIS_constellation_build_linear_family(double *i_data, double *q_data, size_t count,
                                                       double bandwidth_hz, int family, int selected_psk_order) {
    /*
        Purpose: Builds the corrected and symbol-timed constellation for PSK, QAM, or ASK/OOK signals
        Returns: No value
    */

    if (!i_data || !q_data || count < 256U || Global_Analysis_Sample_Rate <= 0.0) {

        return;

    }

    double power = 0.0;

    for (size_t n = 0; n < count; n++) {
        power += i_data[n] * i_data[n] + q_data[n] * q_data[n];
    }

    double rms = sqrt(power / (double)count);
    double gate = rms * 0.15;
    int psk_order = selected_psk_order;

    if (psk_order != ANALYSIS_CONSTELLATION_PSK_BPSK && psk_order != ANALYSIS_CONSTELLATION_PSK_QPSK &&
        psk_order != ANALYSIS_CONSTELLATION_PSK_8PSK) {

        psk_order = ANALYSIS_CONSTELLATION_PSK_BPSK;

    }

    double maximum_offset_hz = bandwidth_hz * 0.75;

    if (maximum_offset_hz < Global_Analysis_Sample_Rate / 4096.0) {

        maximum_offset_hz = Global_Analysis_Sample_Rate / 4096.0;

    }

    double frequency = 0.0;

    if (family == ANALYSIS_CONSTELLATION_MODE_PSK) {

        frequency = ANALYSIS_constellation_estimate_psk_frequency_fft(i_data, q_data, count, psk_order, gate,
                                                                      maximum_offset_hz);

    }

    else if (family == ANALYSIS_CONSTELLATION_MODE_QAM) {

        frequency = ANALYSIS_constellation_estimate_qam_frequency_fft(i_data, q_data, count, gate, maximum_offset_hz);

    }

    else {

        frequency = ANALYSIS_constellation_estimate_spectral_center(i_data, q_data, count, maximum_offset_hz);

    }

    ANALYSIS_constellation_apply_frequency_correction(i_data, q_data, count, frequency);

    int samples_per_symbol = 0;
    int offset = 0;

    if (!ANALYSIS_constellation_find_symbol_timing_v2(i_data, q_data, count, bandwidth_hz, family, psk_order, gate,
                                                      &samples_per_symbol, &offset)) {

        return;

    }

    size_t guard = count / 40U;

    if (guard < 128U) {

        guard = 128U;

    }

    if (guard * 2U >= count) {

        guard = 0U;

    }

    Type_Analysis_Constellation_Point output[ANALYSIS_MAX_CONST_POINTS];
    int output_count = ANALYSIS_constellation_collect_candidate(i_data, q_data, count, guard, samples_per_symbol,
                                                                offset, output, ANALYSIS_MAX_CONST_POINTS);

    if (output_count < 4) {

        return;

    }

    double residual_frequency = 0.0;

    if (family == ANALYSIS_CONSTELLATION_MODE_PSK) {

        residual_frequency =
            ANALYSIS_constellation_psk_residual_frequency(output, output_count, psk_order, samples_per_symbol);

    }

    else if (family == ANALYSIS_CONSTELLATION_MODE_QAM) {

        double symbol_rate_hz = Global_Analysis_Sample_Rate / (double)samples_per_symbol;
        residual_frequency =
            ANALYSIS_constellation_qam_residual_frequency(output, output_count, samples_per_symbol, symbol_rate_hz);

    }

    else {

        residual_frequency = ANALYSIS_constellation_psk_residual_frequency(output, output_count, 1, samples_per_symbol);

    }

    if (fabs(residual_frequency) > 1e-15) {

        ANALYSIS_constellation_apply_frequency_correction(i_data, q_data, count, residual_frequency);
        output_count = ANALYSIS_constellation_collect_candidate(i_data, q_data, count, guard, samples_per_symbol,
                                                                offset, output, ANALYSIS_MAX_CONST_POINTS);

    }

    if (output_count < 4) {

        return;

    }

    double rotation_phase = 0.0;

    if (family == ANALYSIS_CONSTELLATION_MODE_PSK) {

        double sum_i = 0.0;
        double sum_q = 0.0;

        for (int p = 0; p < output_count; p++) {
            double radius = hypot(output[p].i, output[p].q);

            if (radius > gate) {

                double powered_i = 0.0;
                double powered_q = 0.0;
                ANALYSIS_constellation_complex_power(output[p].i / radius, output[p].q / radius, psk_order, &powered_i,
                                                     &powered_q);
                sum_i += powered_i;
                sum_q += powered_q;

            }
        }

        rotation_phase = atan2(sum_q, sum_i) / (double)psk_order;

    }

    else if (family == ANALYSIS_CONSTELLATION_MODE_QAM) {

        double sum_i = 0.0;
        double sum_q = 0.0;

        for (int p = 0; p < output_count; p++) {
            double powered_i = 0.0;
            double powered_q = 0.0;
            ANALYSIS_constellation_complex_power(output[p].i, output[p].q, 4, &powered_i, &powered_q);
            sum_i += powered_i;
            sum_q += powered_q;
        }

        rotation_phase = ANALYSIS_wrap_phase(atan2(sum_q, sum_i) - M_PI) * 0.25;

    }

    else {

        double mean_i = 0.0;
        double mean_q = 0.0;

        for (int p = 0; p < output_count; p++) {
            mean_i += output[p].i;
            mean_q += output[p].q;
        }

        mean_i /= (double)output_count;
        mean_q /= (double)output_count;

        double covariance_ii = 0.0;
        double covariance_qq = 0.0;
        double covariance_iq = 0.0;

        for (int p = 0; p < output_count; p++) {
            double centered_i = output[p].i - mean_i;
            double centered_q = output[p].q - mean_q;
            covariance_ii += centered_i * centered_i;
            covariance_qq += centered_q * centered_q;
            covariance_iq += centered_i * centered_q;
        }

        rotation_phase = 0.5 * atan2(2.0 * covariance_iq, covariance_ii - covariance_qq);

    }

    double rotation_i = cos(-rotation_phase);
    double rotation_q = sin(-rotation_phase);

    Global_Analysis_Const_Count = output_count;

    for (int p = 0; p < output_count; p++) {
        Global_Analysis_Const_I[p] = (float)(output[p].i * rotation_i - output[p].q * rotation_q);
        Global_Analysis_Const_Q[p] = (float)(output[p].i * rotation_q + output[p].q * rotation_i);
    }

    ANALYSIS_constellation_normalize_output(family == ANALYSIS_CONSTELLATION_MODE_ASK_OOK);
}

static double ANALYSIS_constellation_fsk_kmeans_1d(const double *values, size_t count, int level_count,
                                                   double *centroids, size_t *populations, int *assignments) {
    /*
        Purpose: Fits two or four ordered frequency states without collapsing distinct FSK levels
        Returns: Mean squared fitting error, or a negative value on failure
    */

    if (!values || count == 0U || level_count < 2 || level_count > 4 || !centroids || !populations) {

        return -1.0;

    }

    double *sorted = malloc(count * sizeof(double));

    if (!sorted) {

        return -1.0;

    }

    size_t finite_count = 0U;

    for (size_t n = 0U; n < count; n++) {

        if (isfinite(values[n])) {

            sorted[finite_count++] = values[n];

        }
    }

    if (finite_count < (size_t)(level_count * 4)) {

        free(sorted);
        return -1.0;

    }

    qsort(sorted, finite_count, sizeof(double), ANALYSIS_constellation_double_compare);

    size_t low_index = (size_t)(0.01 * (double)(finite_count - 1U));
    size_t high_index = (size_t)(0.99 * (double)(finite_count - 1U));
    double robust_low = sorted[low_index];
    double robust_high = sorted[high_index];

    if (!isfinite(robust_low) || !isfinite(robust_high) || robust_high <= robust_low) {

        free(sorted);
        return -1.0;

    }

    for (int level = 0; level < level_count; level++) {
        centroids[level] = robust_low + (robust_high - robust_low) * (double)level / (double)(level_count - 1);
    }

    for (int iteration = 0; iteration < 48; iteration++) {
        double sums[4] = {0.0, 0.0, 0.0, 0.0};
        size_t counts[4] = {0U, 0U, 0U, 0U};

        for (size_t n = 0U; n < count; n++) {
            double value = values[n];

            if (!isfinite(value) || value < robust_low || value > robust_high) {

                continue;

            }

            int nearest = 0;
            double nearest_distance = fabs(value - centroids[0]);

            for (int level = 1; level < level_count; level++) {
                double distance = fabs(value - centroids[level]);

                if (distance < nearest_distance) {

                    nearest = level;
                    nearest_distance = distance;

                }
            }

            sums[nearest] += value;
            counts[nearest]++;
        }

        double movement = 0.0;

        for (int level = 0; level < level_count; level++) {

            if (counts[level] == 0U) {

                continue;

            }

            double next = sums[level] / (double)counts[level];
            movement += fabs(next - centroids[level]);
            centroids[level] = next;
        }

        for (int left = 0; left < level_count - 1; left++) {
            for (int right = left + 1; right < level_count; right++) {

                if (centroids[right] < centroids[left]) {

                    double swap = centroids[left];
                    centroids[left] = centroids[right];
                    centroids[right] = swap;

                }
            }
        }

        if (movement < 1e-6) {

            break;

        }
    }

    for (int level = 0; level < level_count; level++) {
        populations[level] = 0U;
    }

    double squared_error = 0.0;
    size_t used = 0U;

    for (size_t n = 0U; n < count; n++) {
        double value = values[n];

        if (!isfinite(value) || value < robust_low || value > robust_high) {

            if (assignments) {

                assignments[n] = -1;

            }

            continue;

        }

        int nearest = 0;
        double nearest_distance = fabs(value - centroids[0]);

        for (int level = 1; level < level_count; level++) {
            double distance = fabs(value - centroids[level]);

            if (distance < nearest_distance) {

                nearest = level;
                nearest_distance = distance;

            }
        }

        if (assignments) {

            assignments[n] = nearest;

        }

        populations[nearest]++;
        squared_error += nearest_distance * nearest_distance;
        used++;
    }

    free(sorted);

    if (used == 0U) {

        return -1.0;

    }

    return squared_error / (double)used;
}

static void ANALYSIS_constellation_build_fsk_family(const double *i_data, const double *q_data, size_t count,
                                                    double bandwidth_hz) {
    /*
        Purpose: Recovers two- or four-level FSK/MSK frequency states and plots one discriminator sample per symbol
        Returns: No value
    */

    if (!i_data || !q_data || count < 64U || Global_Analysis_Sample_Rate <= 0.0) {

        return;

    }

    const double sample_rate = Global_Analysis_Sample_Rate;
    const size_t frequency_count = count - 1U;
    double *frequency_hz = malloc(frequency_count * sizeof(double));
    double *smoothed_hz = malloc(frequency_count * sizeof(double));
    double *transition_strength = calloc(frequency_count, sizeof(double));
    unsigned char *valid = calloc(frequency_count, sizeof(unsigned char));

    if (!frequency_hz || !smoothed_hz || !transition_strength || !valid) {

        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    double average_power = 0.0;

    for (size_t n = 0U; n < count; n++) {
        average_power += i_data[n] * i_data[n] + q_data[n] * q_data[n];
    }

    average_power /= (double)count;
    double magnitude_gate = sqrt(average_power) * 0.18;
    size_t valid_count = 0U;

    for (size_t n = 1U; n < count; n++) {
        double current_magnitude = hypot(i_data[n], q_data[n]);
        double previous_magnitude = hypot(i_data[n - 1U], q_data[n - 1U]);
        size_t index = n - 1U;

        if (current_magnitude < magnitude_gate || previous_magnitude < magnitude_gate) {

            frequency_hz[index] = 0.0;
            continue;

        }

        double product_i = i_data[n] * i_data[n - 1U] + q_data[n] * q_data[n - 1U];
        double product_q = q_data[n] * i_data[n - 1U] - i_data[n] * q_data[n - 1U];
        double phase_increment = atan2(product_q, product_i);

        frequency_hz[index] = phase_increment * sample_rate / (2.0 * M_PI);
        valid[index] = 1U;
        valid_count++;
    }

    if (valid_count < 32U) {

        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    double *sorted_frequency = malloc(valid_count * sizeof(double));

    if (!sorted_frequency) {

        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    size_t sorted_index = 0U;

    for (size_t n = 0U; n < frequency_count; n++) {

        if (valid[n]) {

            sorted_frequency[sorted_index++] = frequency_hz[n];

        }
    }

    qsort(sorted_frequency, valid_count, sizeof(double), ANALYSIS_constellation_double_compare);
    double fallback_center = sorted_frequency[valid_count / 2U];
    free(sorted_frequency);

    int smoothing_radius = 2;

    if (bandwidth_hz > 0.0) {

        smoothing_radius = (int)llround(sample_rate / (8.0 * bandwidth_hz));

    }

    if (smoothing_radius < 1) {

        smoothing_radius = 1;

    }

    if (smoothing_radius > 24) {

        smoothing_radius = 24;

    }

    for (size_t n = 0U; n < frequency_count; n++) {
        size_t first = n > (size_t)smoothing_radius ? n - (size_t)smoothing_radius : 0U;
        size_t last = n + (size_t)smoothing_radius + 1U;

        if (last > frequency_count) {

            last = frequency_count;

        }

        double sum = 0.0;
        size_t used = 0U;

        for (size_t sample = first; sample < last; sample++) {

            if (valid[sample]) {

                sum += frequency_hz[sample];
                used++;

            }
        }

        smoothed_hz[n] = used > 0U ? sum / (double)used : fallback_center;
    }

    double *strength_values = malloc(valid_count * sizeof(double));
    size_t strength_count = 0U;

    if (!strength_values) {

        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    for (size_t n = 1U; n < frequency_count; n++) {

        if (!valid[n] || !valid[n - 1U]) {

            continue;

        }

        double strength = fabs(smoothed_hz[n] - smoothed_hz[n - 1U]);
        transition_strength[n] = strength;

        if (strength > 0.0 && isfinite(strength)) {

            strength_values[strength_count++] = strength;

        }
    }

    if (strength_count < 8U) {

        free(strength_values);
        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    qsort(strength_values, strength_count, sizeof(double), ANALYSIS_constellation_double_compare);
    size_t threshold_index = (size_t)(0.85 * (double)(strength_count - 1U));
    double transition_threshold = strength_values[threshold_index];
    free(strength_values);

    size_t *transition_samples = malloc(valid_count * sizeof(size_t));
    double *transition_weights = malloc(valid_count * sizeof(double));
    size_t transition_count = 0U;

    if (!transition_samples || !transition_weights) {

        free(transition_samples);
        free(transition_weights);
        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    size_t minimum_peak_separation = (size_t)(2 * smoothing_radius + 1);

    if (minimum_peak_separation < 2U) {

        minimum_peak_separation = 2U;

    }

    for (size_t n = 1U; n + 1U < frequency_count; n++) {
        double strength = transition_strength[n];

        if (strength < transition_threshold || strength < transition_strength[n - 1U] ||
            strength < transition_strength[n + 1U]) {

            continue;

        }

        if (transition_count > 0U && n - transition_samples[transition_count - 1U] < minimum_peak_separation) {

            if (strength > transition_weights[transition_count - 1U]) {

                transition_samples[transition_count - 1U] = n;
                transition_weights[transition_count - 1U] = strength;

            }

            continue;

        }

        transition_samples[transition_count] = n;
        transition_weights[transition_count] = strength;
        transition_count++;
    }

    int samples_per_symbol = 0;
    double boundary_offset = 0.0;

    if (transition_count >= 4U) {

        int minimum_sps = 4;
        int maximum_sps = 2048;

        if (bandwidth_hz > 0.0) {

            minimum_sps = (int)floor(sample_rate / (2.0 * bandwidth_hz));
            maximum_sps = (int)ceil(sample_rate * 16.0 / bandwidth_hz);

        }

        if (minimum_sps < 4) {

            minimum_sps = 4;

        }

        if (maximum_sps > 2048) {

            maximum_sps = 2048;

        }

        int count_limited_maximum = (int)(frequency_count / 8U);

        if (maximum_sps > count_limited_maximum) {

            maximum_sps = count_limited_maximum;

        }

        double best_score = -1.0;

        for (int candidate_sps = minimum_sps; candidate_sps <= maximum_sps; candidate_sps++) {
            double phase_i = 0.0;
            double phase_q = 0.0;
            double weight_sum = 0.0;

            for (size_t transition = 0U; transition < transition_count; transition++) {
                double weight = transition_weights[transition];
                double phase = 2.0 * M_PI * (double)transition_samples[transition] / (double)candidate_sps;
                phase_i += weight * cos(phase);
                phase_q += weight * sin(phase);
                weight_sum += weight;
            }

            if (weight_sum <= 0.0) {

                continue;

            }

            double coherence = hypot(phase_i, phase_q) / weight_sum;
            double score = coherence + 0.01 * log((double)candidate_sps);

            if (score > best_score) {

                best_score = score;
                samples_per_symbol = candidate_sps;

            }
        }

        if (samples_per_symbol >= 4) {

            double phase_i = 0.0;
            double phase_q = 0.0;

            for (size_t transition = 0U; transition < transition_count; transition++) {
                double weight = transition_weights[transition];
                double phase = 2.0 * M_PI * (double)transition_samples[transition] / (double)samples_per_symbol;
                phase_i += weight * cos(phase);
                phase_q += weight * sin(phase);
            }

            double boundary_phase = atan2(phase_q, phase_i);

            if (boundary_phase < 0.0) {

                boundary_phase += 2.0 * M_PI;

            }

            boundary_offset = boundary_phase * (double)samples_per_symbol / (2.0 * M_PI);

        }

    }

    double *symbol_values = malloc(frequency_count * sizeof(double));
    double *symbol_transitions = malloc(frequency_count * sizeof(double));
    size_t symbol_count = 0U;

    if (!symbol_values || !symbol_transitions) {

        free(symbol_values);
        free(symbol_transitions);
        free(transition_samples);
        free(transition_weights);
        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    if (samples_per_symbol >= 4) {

        size_t run_start = 0U;

        while (run_start < frequency_count) {
            while (run_start < frequency_count && !valid[run_start]) {
                run_start++;
            }

            if (run_start >= frequency_count) {

                break;

            }

            size_t run_end = run_start;

            while (run_end < frequency_count && valid[run_end]) {
                run_end++;
            }

            double center_sample = boundary_offset + 0.5 * (double)samples_per_symbol;

            if (center_sample < (double)run_start) {

                double steps = ceil(((double)run_start - center_sample) / (double)samples_per_symbol);
                center_sample += steps * (double)samples_per_symbol;

            }

            int half_window = samples_per_symbol / 4;

            if (half_window < 2) {

                half_window = 2;

            }

            while (center_sample < (double)run_end && symbol_count < frequency_count) {
                long center_index = lround(center_sample);
                long first_start = center_index - half_window;
                long first_end = center_index;
                long second_start = center_index;
                long second_end = center_index + half_window;

                if (first_start < (long)run_start) {

                    first_start = (long)run_start;

                }

                if (second_end > (long)run_end) {

                    second_end = (long)run_end;

                }

                double first_sum = 0.0;
                double second_sum = 0.0;
                size_t first_count = 0U;
                size_t second_count = 0U;

                for (long sample = first_start; sample < first_end; sample++) {

                    if (sample >= 0 && valid[(size_t)sample]) {

                        first_sum += frequency_hz[(size_t)sample];
                        first_count++;

                    }
                }

                for (long sample = second_start; sample < second_end; sample++) {

                    if (sample >= 0 && valid[(size_t)sample]) {

                        second_sum += frequency_hz[(size_t)sample];
                        second_count++;

                    }
                }

                if (first_count > 0U && second_count > 0U) {

                    double first_average = first_sum / (double)first_count;
                    double second_average = second_sum / (double)second_count;

                    symbol_values[symbol_count] = (first_sum + second_sum) / (double)(first_count + second_count);
                    symbol_transitions[symbol_count] = second_average - first_average;
                    symbol_count++;

                }

                center_sample += (double)samples_per_symbol;
            }

            run_start = run_end + 1U;
        }

    }

    if (symbol_count < 16U) {

        symbol_count = 0U;
        size_t fallback_stride = (size_t)(2 * smoothing_radius + 1);

        if (fallback_stride < 1U) {

            fallback_stride = 1U;

        }

        for (size_t n = 0U; n < frequency_count && symbol_count < frequency_count; n += fallback_stride) {

            if (!valid[n]) {

                continue;

            }

            symbol_values[symbol_count] = smoothed_hz[n];
            symbol_transitions[symbol_count] = 0.0;
            symbol_count++;
        }

    }

    double centroids_two[4] = {0.0, 0.0, 0.0, 0.0};
    double centroids_four[4] = {0.0, 0.0, 0.0, 0.0};
    size_t populations_two[4] = {0U, 0U, 0U, 0U};
    size_t populations_four[4] = {0U, 0U, 0U, 0U};
    double error_two =
        ANALYSIS_constellation_fsk_kmeans_1d(symbol_values, symbol_count, 2, centroids_two, populations_two, NULL);
    double error_four =
        ANALYSIS_constellation_fsk_kmeans_1d(symbol_values, symbol_count, 4, centroids_four, populations_four, NULL);
    int level_count = 2;
    double *centroids = centroids_two;

    if (error_two >= 0.0 && error_four >= 0.0) {

        double gaps[3] = {centroids_four[1] - centroids_four[0], centroids_four[2] - centroids_four[1],
                          centroids_four[3] - centroids_four[2]};
        double minimum_gap = gaps[0];
        double maximum_gap = gaps[0];

        for (int gap = 1; gap < 3; gap++) {

            if (gaps[gap] < minimum_gap) {

                minimum_gap = gaps[gap];

            }

            if (gaps[gap] > maximum_gap) {

                maximum_gap = gaps[gap];

            }
        }

        size_t minimum_population = symbol_count / 50U;

        if (minimum_population < 4U) {

            minimum_population = 4U;

        }

        int populations_valid = 1;

        for (int level = 0; level < 4; level++) {

            if (populations_four[level] < minimum_population) {

                populations_valid = 0;

            }
        }

        double gap_uniformity = maximum_gap > 0.0 ? minimum_gap / maximum_gap : 0.0;

        if (populations_valid && minimum_gap > 1e-9 && gap_uniformity >= 0.35 && error_four < error_two * 0.45) {

            level_count = 4;
            centroids = centroids_four;

        }

    }

    if (error_two < 0.0 || centroids[level_count - 1] <= centroids[0]) {

        free(symbol_values);
        free(symbol_transitions);
        free(transition_samples);
        free(transition_weights);
        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    double plot_center = 0.5 * (centroids[0] + centroids[level_count - 1]);
    double half_span = 0.5 * (centroids[level_count - 1] - centroids[0]);
    double plot_scale = half_span / 0.82;
    double outer_span = centroids[level_count - 1] - centroids[0];
    double minimum_level_gap = outer_span;

    for (int level = 0; level < level_count - 1; level++) {
        double gap = centroids[level + 1] - centroids[level];

        if (gap < minimum_level_gap) {

            minimum_level_gap = gap;

        }
    }

    if (!isfinite(plot_scale) || plot_scale <= 1e-9 || minimum_level_gap <= 1e-9) {

        free(symbol_values);
        free(symbol_transitions);
        free(transition_samples);
        free(transition_weights);
        free(frequency_hz);
        free(smoothed_hz);
        free(transition_strength);
        free(valid);
        return;

    }

    size_t output_stride = symbol_count > ANALYSIS_MAX_CONST_POINTS ? symbol_count / ANALYSIS_MAX_CONST_POINTS : 1U;

    if (output_stride < 1U) {

        output_stride = 1U;

    }

    int output_count = 0;

    for (size_t symbol = 0U; symbol < symbol_count && output_count < ANALYSIS_MAX_CONST_POINTS; symbol++) {

        if ((symbol % output_stride) != 0U) {

            continue;

        }

        double value = symbol_values[symbol];
        int nearest = 0;
        double nearest_distance = fabs(value - centroids[0]);

        for (int level = 1; level < level_count; level++) {
            double distance = fabs(value - centroids[level]);

            if (distance < nearest_distance) {

                nearest = level;
                nearest_distance = distance;

            }
        }

        double local_gap = minimum_level_gap;

        if (nearest > 0) {

            double gap = centroids[nearest] - centroids[nearest - 1];

            if (gap < local_gap) {

                local_gap = gap;

            }

        }

        if (nearest + 1 < level_count) {

            double gap = centroids[nearest + 1] - centroids[nearest];

            if (gap < local_gap) {

                local_gap = gap;

            }

        }

        if (nearest_distance > 0.45 * local_gap) {

            continue;

        }

        double normalized_state = (value - plot_center) / plot_scale;
        double normalized_transition = symbol_transitions[symbol] / outer_span;

        if (!isfinite(normalized_state) || !isfinite(normalized_transition) || fabs(normalized_transition) > 0.60) {

            continue;

        }

        Global_Analysis_Const_I[output_count] = (float)ANALYSIS_limit_double(normalized_state, -0.98, 0.98);
        Global_Analysis_Const_Q[output_count] = (float)ANALYSIS_limit_double(normalized_transition, -0.55, 0.55);
        output_count++;
    }

    Global_Analysis_Const_Count = output_count;

    free(symbol_values);
    free(symbol_transitions);
    free(transition_samples);
    free(transition_weights);
    free(frequency_hz);
    free(smoothed_hz);
    free(transition_strength);
    free(valid);
}

static double ANALYSIS_constellation_ofdm_cp_score(const double *i_data, const double *q_data, size_t count,
                                                   int fft_size, int cp_size, int offset, double *phase_out) {
    /*
        Purpose: Calculates normalized cyclic-prefix correlation for an OFDM timing candidate
        Returns: Cyclic-prefix correlation score
    */

    int symbol_size = fft_size + cp_size;
    double sum_i = 0.0;
    double sum_q = 0.0;
    double power_a = 0.0;
    double power_b = 0.0;
    int blocks = 0;

    for (size_t start = (size_t)offset; start + (size_t)symbol_size <= count && blocks < 48;
         start += (size_t)symbol_size) {
        for (int k = 0; k < cp_size; k++) {
            size_t a = start + (size_t)k;
            size_t b = start + (size_t)fft_size + (size_t)k;
            sum_i += i_data[a] * i_data[b] + q_data[a] * q_data[b];
            sum_q += q_data[a] * i_data[b] - i_data[a] * q_data[b];
            power_a += i_data[a] * i_data[a] + q_data[a] * q_data[a];
            power_b += i_data[b] * i_data[b] + q_data[b] * q_data[b];
        }
        blocks++;
    }

    if (phase_out) {

        *phase_out = atan2(sum_q, sum_i);

    }

    return power_a > 1e-12 && power_b > 1e-12 ? hypot(sum_i, sum_q) / sqrt(power_a * power_b) : 0.0;
}

static void ANALYSIS_constellation_build_ofdm_family_generic(double *i_data, double *q_data, size_t count) {
    /*
        Purpose: Builds a generic OFDM constellation when no known OFDM waveform is recovered
        Returns: No value
    */

    const int fft_candidates[] = {64, 128, 256, 512, 1024, 2048};
    const int cp_divisors[] = {4, 8, 16, 32};
    double best_score = 0.0;
    double best_phase = 0.0;
    int best_fft = 0;
    int best_cp = 0;
    int best_offset = 0;

    for (size_t fft_index = 0; fft_index < sizeof(fft_candidates) / sizeof(fft_candidates[0]); fft_index++) {
        int fft_size = fft_candidates[fft_index];

        if ((size_t)(fft_size * 6) > count) {

            continue;

        }

        for (size_t cp_index = 0; cp_index < sizeof(cp_divisors) / sizeof(cp_divisors[0]); cp_index++) {
            int cp_size = fft_size / cp_divisors[cp_index];
            int symbol_size = fft_size + cp_size;
            int offset_step = symbol_size / 48;

            if (offset_step < 1) {

                offset_step = 1;

            }

            for (int offset = 0; offset < symbol_size; offset += offset_step) {
                double phase = 0.0;
                double score =
                    ANALYSIS_constellation_ofdm_cp_score(i_data, q_data, count, fft_size, cp_size, offset, &phase);

                if (score > best_score) {

                    best_score = score;
                    best_phase = phase;
                    best_fft = fft_size;
                    best_cp = cp_size;
                    best_offset = offset;

                }
            }
        }
    }

    if (best_fft == 0 || best_score < 0.08) {

        return;

    }

    double frequency = -best_phase / (double)best_fft;
    ANALYSIS_constellation_apply_frequency_correction(i_data, q_data, count, frequency);

    int symbol_size = best_fft + best_cp;
    int symbol_count = (int)((count > (size_t)best_offset ? count - (size_t)best_offset : 0U) / (size_t)symbol_size);

    if (symbol_count < 3) {

        return;

    }

    if (symbol_count > 96) {

        symbol_count = 96;

    }

    fftw_complex *time_data = fftw_malloc(sizeof(fftw_complex) * (size_t)best_fft);
    fftw_complex *frequency_data = fftw_malloc(sizeof(fftw_complex) * (size_t)best_fft);
    double *average_power = calloc((size_t)best_fft, sizeof(double));
    double *sorted_power = malloc((size_t)best_fft * sizeof(double));
    Type_Analysis_Constellation_Point *symbols =
        malloc((size_t)symbol_count * (size_t)best_fft * sizeof(Type_Analysis_Constellation_Point));

    if (!time_data || !frequency_data || !average_power || !sorted_power || !symbols) {

        if (time_data) {

            fftw_free(time_data);

        }

        if (frequency_data) {

            fftw_free(frequency_data);

        }
        free(average_power);
        free(sorted_power);
        free(symbols);
        return;

    }

    fftw_plan plan = fftw_plan_dft_1d(best_fft, time_data, frequency_data, FFTW_FORWARD, FFTW_ESTIMATE);

    if (!plan) {

        fftw_free(time_data);
        fftw_free(frequency_data);
        free(average_power);
        free(sorted_power);
        free(symbols);
        return;

    }

    for (int symbol = 0; symbol < symbol_count; symbol++) {
        size_t start = (size_t)best_offset + (size_t)symbol * (size_t)symbol_size + (size_t)best_cp;

        if (start + (size_t)best_fft > count) {

            symbol_count = symbol;
            break;

        }

        for (int n = 0; n < best_fft; n++) {
            time_data[n][0] = i_data[start + (size_t)n];
            time_data[n][1] = q_data[start + (size_t)n];
        }

        fftw_execute(plan);

        for (int bin = 0; bin < best_fft; bin++) {
            double point_i = frequency_data[bin][0] / (double)best_fft;
            double point_q = frequency_data[bin][1] / (double)best_fft;
            symbols[(size_t)symbol * (size_t)best_fft + (size_t)bin].i = point_i;
            symbols[(size_t)symbol * (size_t)best_fft + (size_t)bin].q = point_q;
            average_power[bin] += point_i * point_i + point_q * point_q;
        }
    }

    if (symbol_count < 3) {

        fftw_destroy_plan(plan);
        fftw_free(time_data);
        fftw_free(frequency_data);
        free(average_power);
        free(sorted_power);
        free(symbols);
        return;

    }

    for (int bin = 0; bin < best_fft; bin++) {
        average_power[bin] /= (double)symbol_count;
        sorted_power[bin] = average_power[bin];
    }

    qsort(sorted_power, (size_t)best_fft, sizeof(double), ANALYSIS_constellation_double_compare);
    double maximum_power = sorted_power[best_fft - 1];
    double noise_power = sorted_power[best_fft / 10];
    double threshold = noise_power * 5.0;

    if (threshold < maximum_power * 0.0001) {

        threshold = maximum_power * 0.0001;

    }

    if (threshold > maximum_power * 0.50) {

        threshold = maximum_power * 0.50;

    }

    int output_count = 0;

    for (int bin = 0; bin < best_fft && output_count < ANALYSIS_MAX_CONST_POINTS; bin++) {
        int signed_bin = bin <= best_fft / 2 ? bin : bin - best_fft;

        if (signed_bin == 0 || abs(signed_bin) >= best_fft / 2 - 1 || average_power[bin] <= threshold) {

            continue;

        }

        double second_i = 0.0;
        double second_q = 0.0;
        double fourth_i = 0.0;
        double fourth_q = 0.0;
        double bin_power = 0.0;

        int phase_samples = 0;

        for (int symbol = 0; symbol < symbol_count; symbol++) {
            Type_Analysis_Constellation_Point point = symbols[(size_t)symbol * (size_t)best_fft + (size_t)bin];
            double magnitude = hypot(point.i, point.q);

            if (magnitude > 1e-12) {

                double powered_i = 0.0;
                double powered_q = 0.0;
                double unit_i = point.i / magnitude;
                double unit_q = point.q / magnitude;

                ANALYSIS_constellation_complex_power(unit_i, unit_q, 2, &powered_i, &powered_q);
                second_i += powered_i;
                second_q += powered_q;
                ANALYSIS_constellation_complex_power(unit_i, unit_q, 4, &powered_i, &powered_q);
                fourth_i += powered_i;
                fourth_q += powered_q;
                phase_samples++;

            }

            bin_power += point.i * point.i + point.q * point.q;
        }

        double second_coherence = phase_samples > 0 ? hypot(second_i, second_q) / (double)phase_samples : 0.0;
        double fourth_coherence = phase_samples > 0 ? hypot(fourth_i, fourth_q) / (double)phase_samples : 0.0;
        int order = second_coherence >= 0.90 * fourth_coherence ? 2 : 4;
        double phase = order == 2 ? 0.5 * atan2(second_q, second_i) : 0.25 * atan2(fourth_q, fourth_i);
        double rotation_i = cos(-phase);
        double rotation_q = sin(-phase);
        double bin_rms = sqrt(bin_power / (double)symbol_count);

        if (bin_rms < 1e-12) {

            continue;

        }

        for (int symbol = 0; symbol < symbol_count && output_count < ANALYSIS_MAX_CONST_POINTS; symbol++) {
            Type_Analysis_Constellation_Point point = symbols[(size_t)symbol * (size_t)best_fft + (size_t)bin];
            Global_Analysis_Const_I[output_count] = (float)((point.i * rotation_i - point.q * rotation_q) / bin_rms);
            Global_Analysis_Const_Q[output_count] = (float)((point.i * rotation_q + point.q * rotation_i) / bin_rms);
            output_count++;
        }
    }

    Global_Analysis_Const_Count = output_count;
    ANALYSIS_constellation_normalize_output(0);

    fftw_destroy_plan(plan);
    fftw_free(time_data);
    fftw_free(frequency_data);
    free(average_power);
    free(sorted_power);
    free(symbols);
}

static int ANALYSIS_constellation_ofdm_cp_metrics(const double *i_data, const double *q_data, size_t count,
                                                  int fft_size, int cp_size, double *metrics, double *corr_i,
                                                  double *corr_q, size_t metric_count) {
    /*
        Purpose: Calculates cyclic-prefix correlation metrics across candidate OFDM symbol offsets
        Returns: Success status
    */

    if (!i_data || !q_data || !metrics || !corr_i || !corr_q || fft_size <= 0 || cp_size <= 0 || metric_count == 0 ||
        count < (size_t)(fft_size + cp_size)) {

        return 0;

    }

    double sum_i = 0.0;
    double sum_q = 0.0;
    double power_a = 0.0;
    double power_b = 0.0;

    for (int k = 0; k < cp_size; k++) {
        size_t a = (size_t)k;
        size_t b = (size_t)fft_size + (size_t)k;
        sum_i += i_data[a] * i_data[b] + q_data[a] * q_data[b];
        sum_q += q_data[a] * i_data[b] - i_data[a] * q_data[b];
        power_a += i_data[a] * i_data[a] + q_data[a] * q_data[a];
        power_b += i_data[b] * i_data[b] + q_data[b] * q_data[b];
    }

    for (size_t start = 0; start < metric_count; start++) {
        corr_i[start] = sum_i;
        corr_q[start] = sum_q;
        metrics[start] = power_a > 1e-15 && power_b > 1e-15 ? hypot(sum_i, sum_q) / sqrt(power_a * power_b) : 0.0;

        if (start + 1U >= metric_count) {

            break;

        }

        size_t remove_a = start;
        size_t remove_b = start + (size_t)fft_size;
        size_t add_a = start + (size_t)cp_size;
        size_t add_b = add_a + (size_t)fft_size;

        sum_i -= i_data[remove_a] * i_data[remove_b] + q_data[remove_a] * q_data[remove_b];
        sum_q -= q_data[remove_a] * i_data[remove_b] - i_data[remove_a] * q_data[remove_b];
        power_a -= i_data[remove_a] * i_data[remove_a] + q_data[remove_a] * q_data[remove_a];
        power_b -= i_data[remove_b] * i_data[remove_b] + q_data[remove_b] * q_data[remove_b];

        sum_i += i_data[add_a] * i_data[add_b] + q_data[add_a] * q_data[add_b];
        sum_q += q_data[add_a] * i_data[add_b] - i_data[add_a] * q_data[add_b];
        power_a += i_data[add_a] * i_data[add_a] + q_data[add_a] * q_data[add_a];
        power_b += i_data[add_b] * i_data[add_b] + q_data[add_b] * q_data[add_b];
    }

    return 1;
}

static int ANALYSIS_constellation_ofdm_has_periodic_neighbor(const size_t *peaks, int peak_count, int index,
                                                             int symbol_size, int tolerance) {
    /*
        Purpose: Checks whether an OFDM correlation peak has a neighboring peak at the expected symbol spacing
        Returns: Boolean status
    */

    size_t current = peaks[index];

    for (int p = index - 1; p >= 0; p--) {
        size_t difference = current - peaks[p];

        if (difference > (size_t)(symbol_size + tolerance)) {

            break;

        }

        if (difference + (size_t)tolerance >= (size_t)symbol_size && difference <= (size_t)(symbol_size + tolerance)) {

            return 1;

        }
    }

    for (int p = index + 1; p < peak_count; p++) {
        size_t difference = peaks[p] - current;

        if (difference > (size_t)(symbol_size + tolerance)) {

            break;

        }

        if (difference + (size_t)tolerance >= (size_t)symbol_size && difference <= (size_t)(symbol_size + tolerance)) {

            return 1;

        }
    }

    return 0;
}

static int ANALYSIS_constellation_build_known_ofdm_qpsk(double *i_data, double *q_data, size_t count) {
    /*
        Purpose: Recovers the 128-point, 32-sample-CP QPSK OFDM test waveform.
                 One fixed 160-sample timing run is used so the FFT window cannot jump
                 between neighboring cyclic-prefix peaks.  The integer-bin position is
                 selected from occupied-band edge contrast, then the fixed pilots remove
                 common phase and symbol-to-symbol phase slope before the data carriers
                 are plotted.
        Returns: 1 when the waveform was confidently recovered, otherwise 0 so the generic OFDM path can run
    */
    const int fft_size = 128;
    const int cp_size = 32;
    const int symbol_size = fft_size + cp_size;
    const int logical_carriers[48] = {-24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9,
                                      -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,  1,   2,   3,   4,   5,   6,   7,   8,
                                      9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24};
    const int data_carriers[44] = {-24, -23, -22, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9,
                                   -8,  -6,  -5,  -4,  -3,  -2,  -1,  1,   2,   3,   4,   5,   6,   8,   9,
                                   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  22,  23,  24};
    const int pilot_carriers[4] = {-21, -7, 7, 21};
    const double pilot_values[4] = {1.0, 1.0, 1.0, -1.0};

    if (!i_data || !q_data || count < (size_t)(symbol_size * 6)) {

        return 0;

    }

    size_t metric_count = count - (size_t)fft_size - (size_t)cp_size + 1U;
    double *metrics = malloc(metric_count * sizeof(double));
    double *corr_i = malloc(metric_count * sizeof(double));
    double *corr_q = malloc(metric_count * sizeof(double));

    if (!metrics || !corr_i || !corr_q) {

        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    if (!ANALYSIS_constellation_ofdm_cp_metrics(i_data, q_data, count, fft_size, cp_size, metrics, corr_i, corr_q,
                                                metric_count)) {

        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    double maximum_metric = 0.0;

    for (size_t n = 0; n < metric_count; n++) {

        if (metrics[n] > maximum_metric) {

            maximum_metric = metrics[n];

        }
    }

    if (maximum_metric < 0.45) {

        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    double timing_threshold = maximum_metric * 0.55;

    if (timing_threshold < 0.45) {

        timing_threshold = 0.45;

    }

    /*
        Search all 160 possible sample phases and retain one contiguous periodic run.
        This deliberately ignores other repeated frames separated by guard samples;
        combining independently detected peaks from those frames was what allowed the
        FFT window to move and turn QPSK subcarriers into a ring.
    */
    size_t best_run_start = 0U;
    int best_run_length = 0;
    double best_run_score = -1.0;

    for (int phase = 0; phase < symbol_size; phase++) {
        size_t run_start = 0U;
        int run_length = 0;
        double run_sum = 0.0;

        for (size_t position = (size_t)phase; position < metric_count; position += (size_t)symbol_size) {

            if (metrics[position] >= timing_threshold) {

                if (run_length == 0) {

                    run_start = position;

                }

                run_length++;
                run_sum += metrics[position];

            }

            else {

                if (run_length >= 6) {

                    double average = run_sum / (double)run_length;
                    double score = (double)run_length * average * average;

                    if (score > best_run_score ||
                        (fabs(score - best_run_score) <= 1e-12 && run_start < best_run_start)) {

                        best_run_score = score;
                        best_run_start = run_start;
                        best_run_length = run_length;

                    }

                }

                run_length = 0;
                run_sum = 0.0;

            }
        }

        if (run_length >= 6) {

            double average = run_sum / (double)run_length;
            double score = (double)run_length * average * average;

            if (score > best_run_score || (fabs(score - best_run_score) <= 1e-12 && run_start < best_run_start)) {

                best_run_score = score;
                best_run_start = run_start;
                best_run_length = run_length;

            }

        }
    }

    if (best_run_length < 6) {

        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    int maximum_symbols = ANALYSIS_MAX_CONST_POINTS / 44;

    if (maximum_symbols < 1) {

        maximum_symbols = 1;

    }

    int symbol_count = best_run_length;

    if (symbol_count > maximum_symbols) {

        symbol_count = maximum_symbols;

    }

    size_t *starts = malloc((size_t)symbol_count * sizeof(size_t));

    if (!starts) {

        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    for (int symbol = 0; symbol < symbol_count; symbol++) {
        starts[symbol] = best_run_start + (size_t)symbol * (size_t)symbol_size;
    }

    /* CP phase gives the fractional carrier offset modulo one subcarrier spacing. */
    double aggregate_i = 0.0;
    double aggregate_q = 0.0;

    for (int symbol = 0; symbol < symbol_count; symbol++) {
        aggregate_i += corr_i[starts[symbol]];
        aggregate_q += corr_q[starts[symbol]];
    }

    if (hypot(aggregate_i, aggregate_q) <= 1e-15) {

        free(starts);
        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    double frequency = -atan2(aggregate_q, aggregate_i) / (double)fft_size;
    ANALYSIS_constellation_apply_frequency_correction(i_data, q_data, count, frequency);

    fftw_complex *time_data = fftw_malloc(sizeof(fftw_complex) * (size_t)fft_size);
    fftw_complex *frequency_data = fftw_malloc(sizeof(fftw_complex) * (size_t)fft_size);
    Type_Analysis_Constellation_Point *symbols =
        malloc((size_t)symbol_count * (size_t)fft_size * sizeof(Type_Analysis_Constellation_Point));
    double average_power[128] = {0.0};

    if (!time_data || !frequency_data || !symbols) {

        if (time_data) {

            fftw_free(time_data);

        }

        if (frequency_data) {

            fftw_free(frequency_data);

        }
        free(symbols);
        free(starts);
        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    fftw_plan plan = fftw_plan_dft_1d(fft_size, time_data, frequency_data, FFTW_FORWARD, FFTW_ESTIMATE);

    if (!plan) {

        fftw_free(time_data);
        fftw_free(frequency_data);
        free(symbols);
        free(starts);
        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    int used_symbols = 0;

    for (int symbol = 0; symbol < symbol_count; symbol++) {
        size_t start = starts[symbol] + (size_t)cp_size;

        if (start + (size_t)fft_size > count) {

            break;

        }

        for (int n = 0; n < fft_size; n++) {
            time_data[n][0] = i_data[start + (size_t)n];
            time_data[n][1] = q_data[start + (size_t)n];
        }

        fftw_execute(plan);

        for (int bin = 0; bin < fft_size; bin++) {
            double point_i = frequency_data[bin][0] / (double)fft_size;
            double point_q = frequency_data[bin][1] / (double)fft_size;
            symbols[(size_t)used_symbols * (size_t)fft_size + (size_t)bin].i = point_i;
            symbols[(size_t)used_symbols * (size_t)fft_size + (size_t)bin].q = point_q;
            average_power[bin] += point_i * point_i + point_q * point_q;
        }

        used_symbols++;
    }

    symbol_count = used_symbols;

    if (symbol_count < 6) {

        fftw_destroy_plan(plan);
        fftw_free(time_data);
        fftw_free(frequency_data);
        free(symbols);
        free(starts);
        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    double total_power = 0.0;

    for (int bin = 0; bin < fft_size; bin++) {
        average_power[bin] /= (double)symbol_count;
        total_power += average_power[bin];
    }

    /*
        Find the integer subcarrier displacement by matching the occupied block and
        both empty edge regions.  Power-only matching of the occupied bins was flat
        across several shifts and could select data bins where the pilots were expected.
    */
    int best_shift = 0;
    double best_shift_score = -1.0;
    double best_shift_power = 0.0;
    double average_total_power = total_power / (double)fft_size;

    for (int shift = -fft_size / 4; shift <= fft_size / 4; shift++) {
        double active_power = 0.0;
        double guard_power = 0.0;

        for (int carrier = 0; carrier < 48; carrier++) {
            int bin = (logical_carriers[carrier] + shift) % fft_size;

            if (bin < 0) {

                bin += fft_size;

            }

            active_power += average_power[bin];
        }

        for (int carrier = -32; carrier <= -25; carrier++) {
            int bin = (carrier + shift) % fft_size;

            if (bin < 0) {

                bin += fft_size;

            }

            guard_power += average_power[bin];
        }

        for (int carrier = 25; carrier <= 32; carrier++) {
            int bin = (carrier + shift) % fft_size;

            if (bin < 0) {

                bin += fft_size;

            }

            guard_power += average_power[bin];
        }

        int dc_bin = shift % fft_size;

        if (dc_bin < 0) {

            dc_bin += fft_size;

        }

        double active_mean = active_power / 48.0;
        double guard_mean = guard_power / 16.0;
        double denominator = guard_mean + 0.25 * average_power[dc_bin] + 0.05 * average_total_power + 1e-15;
        double score = active_mean / denominator;

        if (score > best_shift_score || (fabs(score - best_shift_score) <= 1e-12 && active_power > best_shift_power)) {

            best_shift_score = score;
            best_shift_power = active_power;
            best_shift = shift;

        }
    }

    if (best_shift_power <= 1e-12 || best_shift_power < total_power * 0.30) {

        fftw_destroy_plan(plan);
        fftw_free(time_data);
        fftw_free(frequency_data);
        free(symbols);
        free(starts);
        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    /*
        Estimate a static pilot reference, then remove both common phase and the
        linear phase slope across subcarriers for each OFDM symbol.  The slope term
        corrects residual sampling/timing drift that a common-phase-only correction
        cannot remove.
    */
    double reference_i[4] = {0.0};
    double reference_q[4] = {0.0};
    int reference_symbol = 0;
    double reference_power = -1.0;

    for (int symbol = 0; symbol < symbol_count; symbol++) {
        double power = 0.0;

        for (int pilot = 0; pilot < 4; pilot++) {
            int bin = (pilot_carriers[pilot] + best_shift) % fft_size;

            if (bin < 0) {

                bin += fft_size;

            }

            Type_Analysis_Constellation_Point point = symbols[(size_t)symbol * (size_t)fft_size + (size_t)bin];
            power += point.i * point.i + point.q * point.q;
        }

        if (power > reference_power) {

            reference_power = power;
            reference_symbol = symbol;

        }
    }

    if (reference_power <= 1e-12) {

        fftw_destroy_plan(plan);
        fftw_free(time_data);
        fftw_free(frequency_data);
        free(symbols);
        free(starts);
        free(metrics);
        free(corr_i);
        free(corr_q);
        return 0;

    }

    for (int pilot = 0; pilot < 4; pilot++) {
        int bin = (pilot_carriers[pilot] + best_shift) % fft_size;

        if (bin < 0) {

            bin += fft_size;

        }

        Type_Analysis_Constellation_Point point = symbols[(size_t)reference_symbol * (size_t)fft_size + (size_t)bin];
        double corrected_i = point.i * pilot_values[pilot];
        double corrected_q = point.q * pilot_values[pilot];
        double magnitude = hypot(corrected_i, corrected_q);

        if (magnitude <= 1e-12) {

            fftw_destroy_plan(plan);
            fftw_free(time_data);
            fftw_free(frequency_data);
            free(symbols);
            free(starts);
            free(metrics);
            free(corr_i);
            free(corr_q);
            return 0;

        }

        reference_i[pilot] = corrected_i / magnitude;
        reference_q[pilot] = corrected_q / magnitude;
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int symbol = 0; symbol < symbol_count; symbol++) {
            double error_i[4] = {0.0};
            double error_q[4] = {0.0};
            int valid_pilots = 0;

            for (int pilot = 0; pilot < 4; pilot++) {
                int bin = (pilot_carriers[pilot] + best_shift) % fft_size;

                if (bin < 0) {

                    bin += fft_size;

                }

                Type_Analysis_Constellation_Point point = symbols[(size_t)symbol * (size_t)fft_size + (size_t)bin];
                double point_i = point.i * pilot_values[pilot];
                double point_q = point.q * pilot_values[pilot];
                double magnitude = hypot(point_i, point_q);

                if (magnitude <= 1e-12) {

                    continue;

                }

                point_i /= magnitude;
                point_q /= magnitude;

                error_i[pilot] = point_i * reference_i[pilot] + point_q * reference_q[pilot];
                error_q[pilot] = point_q * reference_i[pilot] - point_i * reference_q[pilot];
                valid_pilots++;
            }

            if (valid_pilots < 3) {

                continue;

            }

            double best_slope = 0.0;
            double best_coherence = -1.0;
            double best_sum_i = 0.0;
            double best_sum_q = 0.0;

            for (int slope_step = -100; slope_step <= 100; slope_step++) {
                double slope = (double)slope_step * 0.0025;
                double sum_i = 0.0;
                double sum_q = 0.0;

                for (int pilot = 0; pilot < 4; pilot++) {

                    if (hypot(error_i[pilot], error_q[pilot]) <= 1e-12) {

                        continue;

                    }

                    double angle = -slope * (double)pilot_carriers[pilot];
                    double rotation_i = cos(angle);
                    double rotation_q = sin(angle);
                    sum_i += error_i[pilot] * rotation_i - error_q[pilot] * rotation_q;
                    sum_q += error_i[pilot] * rotation_q + error_q[pilot] * rotation_i;
                }

                double coherence = hypot(sum_i, sum_q);

                if (coherence > best_coherence) {

                    best_coherence = coherence;
                    best_slope = slope;
                    best_sum_i = sum_i;
                    best_sum_q = sum_q;

                }
            }

            if (best_coherence < 0.40 * (double)valid_pilots) {

                continue;

            }

            double common_phase = atan2(best_sum_q, best_sum_i);

            for (int bin = 0; bin < fft_size; bin++) {
                int carrier = bin - best_shift;

                while (carrier > fft_size / 2) {
                    carrier -= fft_size;
                }

                while (carrier < -fft_size / 2) {
                    carrier += fft_size;
                }

                double phase = common_phase + best_slope * (double)carrier;
                double rotation_i = cos(-phase);
                double rotation_q = sin(-phase);
                Type_Analysis_Constellation_Point *point = &symbols[(size_t)symbol * (size_t)fft_size + (size_t)bin];
                double corrected_i = point->i * rotation_i - point->q * rotation_q;
                double corrected_q = point->i * rotation_q + point->q * rotation_i;
                point->i = corrected_i;
                point->q = corrected_q;
            }
        }

        for (int pilot = 0; pilot < 4; pilot++) {
            int bin = (pilot_carriers[pilot] + best_shift) % fft_size;

            if (bin < 0) {

                bin += fft_size;

            }

            double sum_i = 0.0;
            double sum_q = 0.0;

            for (int symbol = 0; symbol < symbol_count; symbol++) {
                Type_Analysis_Constellation_Point point = symbols[(size_t)symbol * (size_t)fft_size + (size_t)bin];
                double point_i = point.i * pilot_values[pilot];
                double point_q = point.q * pilot_values[pilot];
                double magnitude = hypot(point_i, point_q);

                if (magnitude <= 1e-12) {

                    continue;

                }

                sum_i += point_i / magnitude;
                sum_q += point_q / magnitude;
            }

            double magnitude = hypot(sum_i, sum_q);

            if (magnitude > 1e-12) {

                reference_i[pilot] = sum_i / magnitude;
                reference_q[pilot] = sum_q / magnitude;

            }
        }
    }

    double bin_phase[44] = {0.0};
    double bin_rms[44] = {0.0};

    for (int carrier_index = 0; carrier_index < 44; carrier_index++) {
        int bin = (data_carriers[carrier_index] + best_shift) % fft_size;

        if (bin < 0) {

            bin += fft_size;

        }

        double fourth_i = 0.0;
        double fourth_q = 0.0;
        double power = 0.0;

        for (int symbol = 0; symbol < symbol_count; symbol++) {
            Type_Analysis_Constellation_Point point = symbols[(size_t)symbol * (size_t)fft_size + (size_t)bin];
            double magnitude = hypot(point.i, point.q);

            if (magnitude > 1e-12) {

                double powered_i = 0.0;
                double powered_q = 0.0;
                ANALYSIS_constellation_complex_power(point.i / magnitude, point.q / magnitude, 4, &powered_i,
                                                     &powered_q);
                fourth_i += powered_i;
                fourth_q += powered_q;

            }

            power += point.i * point.i + point.q * point.q;
        }

        bin_phase[carrier_index] = 0.25 * atan2(fourth_q, fourth_i);
        bin_rms[carrier_index] = sqrt(power / (double)symbol_count);

        if (bin_rms[carrier_index] < 1e-12) {

            bin_rms[carrier_index] = 1.0;

        }
    }

    int output_count = 0;

    for (int symbol = 0; symbol < symbol_count && output_count < ANALYSIS_MAX_CONST_POINTS; symbol++) {
        for (int carrier_index = 0; carrier_index < 44 && output_count < ANALYSIS_MAX_CONST_POINTS; carrier_index++) {
            int bin = (data_carriers[carrier_index] + best_shift) % fft_size;

            if (bin < 0) {

                bin += fft_size;

            }

            Type_Analysis_Constellation_Point point = symbols[(size_t)symbol * (size_t)fft_size + (size_t)bin];
            double rotation_i = cos(-bin_phase[carrier_index]);
            double rotation_q = sin(-bin_phase[carrier_index]);
            double corrected_i = (point.i * rotation_i - point.q * rotation_q) / bin_rms[carrier_index];
            double corrected_q = (point.i * rotation_q + point.q * rotation_i) / bin_rms[carrier_index];

            Global_Analysis_Const_I[output_count] = (float)corrected_i;
            Global_Analysis_Const_Q[output_count] = (float)corrected_q;
            output_count++;
        }
    }

    Global_Analysis_Const_Count = output_count;
    ANALYSIS_constellation_normalize_output(0);

    fftw_destroy_plan(plan);
    fftw_free(time_data);
    fftw_free(frequency_data);
    free(symbols);
    free(starts);
    free(metrics);
    free(corr_i);
    free(corr_q);
    return output_count > 0;
}

static void ANALYSIS_constellation_build_ofdm_family(double *i_data, double *q_data, size_t count) {
    /*
        Purpose: Builds the OFDM constellation using the known-waveform recovery path or the generic fallback
        Returns: No value
    */

    if (ANALYSIS_constellation_build_known_ofdm_qpsk(i_data, q_data, count)) {

        return;

    }

    ANALYSIS_constellation_build_ofdm_family_generic(i_data, q_data, count);
}

static void ANALYSIS_build_selected_constellation(FILE *fp, int filter_active, int filter_bin_low, int filter_bin_high,
                                                  int time_filter_active, int time_col_low, int time_col_high,
                                                  int render_w) {
    /*
        Purpose: Builds and caches the constellation for the currently selected constellation mode and filters
        Returns: No value
    */

    Global_Analysis_Const_Count = 0;
    memset(Global_Analysis_Const_I, 0, sizeof(Global_Analysis_Const_I));
    memset(Global_Analysis_Const_Q, 0, sizeof(Global_Analysis_Const_Q));

    if (Global_Analysis_Constellation_Mode == ANALYSIS_CONSTELLATION_MODE_OFF) {

        return;

    }

    if (ANALYSIS_constellation_cache_matches()) {

        ANALYSIS_restore_constellation_cache();
        return;

    }

    double *i_data = NULL;
    double *q_data = NULL;
    size_t count = 0;
    double bandwidth_hz = Global_Analysis_Sample_Rate;

    if (!ANALYSIS_constellation_prepare_samples(fp, filter_active, filter_bin_low, filter_bin_high, time_filter_active,
                                                time_col_low, time_col_high, render_w, &i_data, &q_data, &count,
                                                &bandwidth_hz)) {

        ANALYSIS_store_constellation_cache();
        return;

    }

    switch (Global_Analysis_Constellation_Mode) {
    case ANALYSIS_CONSTELLATION_MODE_PSK:
    case ANALYSIS_CONSTELLATION_MODE_QAM:
    case ANALYSIS_CONSTELLATION_MODE_ASK_OOK:
        ANALYSIS_constellation_build_linear_family(i_data, q_data, count, bandwidth_hz,
                                                   Global_Analysis_Constellation_Mode,
                                                   Global_Analysis_Constellation_PSK_Order);
        break;

    case ANALYSIS_CONSTELLATION_MODE_FSK_MSK:
        ANALYSIS_constellation_build_fsk_family(i_data, q_data, count, bandwidth_hz);
        break;

    case ANALYSIS_CONSTELLATION_MODE_OFDM:
        ANALYSIS_constellation_build_ofdm_family(i_data, q_data, count);
        break;

    default:
        break;
    }

    free(i_data);
    free(q_data);
    ANALYSIS_store_constellation_cache();
}

void ANALYSIS_render_workstation_data(uint32_t *pixels, int tex_w, int tex_h) {
    /*
        Purpose: Renders magnitude, phase, and spectrogram analysis data from the loaded recording
        Returns: No value
    */

    clear_waterfall(pixels, tex_w, tex_h);
    Global_Analysis_Render_W = 0;
    memset(Global_Analysis_Mag_Line, 0, sizeof(Global_Analysis_Mag_Line));
    memset(Global_Analysis_Phase_Line, 0, sizeof(Global_Analysis_Phase_Line));
    memset(Global_Analysis_InstFreq_Line, 0, sizeof(Global_Analysis_InstFreq_Line));
    memset(Global_Analysis_PSD_Line, 0, sizeof(Global_Analysis_PSD_Line));
    memset(Global_Analysis_Const_I, 0, sizeof(Global_Analysis_Const_I));
    memset(Global_Analysis_Const_Q, 0, sizeof(Global_Analysis_Const_Q));
    memset(Global_Analysis_Noise_Column_Mask, 0, sizeof(Global_Analysis_Noise_Column_Mask));
    Global_Analysis_Const_Count = 0;

    if (Global_Analysis_IQ_Count < ANALYSIS_FFT_SIZE || Global_Analysis_Path[0] == '\0' || tex_w <= 0 || tex_h <= 0) {

        return;

    }

    int render_w = tex_w;

    if (render_w > ANALYSIS_MAX_RENDER_W) {

        render_w = ANALYSIS_MAX_RENDER_W;

    }
    Global_Analysis_Render_W = render_w;

    FILE *fp = NULL;
    size_t validated_iq_count = 0;

    if (!sec_fopen_complex16(Global_Analysis_Path, &fp, &validated_iq_count)) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Failed to reopen selected recording");
        return;

    }

    if (validated_iq_count != Global_Analysis_IQ_Count) {

        fclose(fp);
        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Selected recording changed on disk");
        return;

    }

    double window[ANALYSIS_FFT_SIZE];

    for (int i = 0; i < ANALYSIS_FFT_SIZE; i++) {
        window[i] = 0.5 - 0.5 * cos((2.0 * M_PI * (double)i) / (double)(ANALYSIS_FFT_SIZE - 1));
    }

    const size_t i16_per_block = (size_t)ANALYSIS_FFT_SIZE * 2U;
    double *db_img = malloc(sizeof(double) * (size_t)render_w * ANALYSIS_FFT_SIZE);
    fftw_complex *in = fftw_malloc(sizeof(fftw_complex) * ANALYSIS_FFT_SIZE);
    fftw_complex *out = fftw_malloc(sizeof(fftw_complex) * ANALYSIS_FFT_SIZE);
    int16_t *block = malloc(sizeof(int16_t) * i16_per_block);
    int16_t *thread_blocks = NULL;
    size_t *thread_starts = NULL;
    int multithread_loaded = 0;

    if (!db_img || !in || !out || !block) {

        free(db_img);
        free(block);

        if (in) {

            fftw_free(in);

        }

        if (out) {

            fftw_free(out);

        }
        fclose(fp);
        return;

    }

    fftw_plan plan = fftw_plan_dft_1d(ANALYSIS_FFT_SIZE, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    if (!plan) {

        free(db_img);
        free(block);
        fftw_free(in);
        fftw_free(out);
        fclose(fp);
        return;

    }

    double max_db = -300.0;
    double max_mag = 1e-12;
    double max_phase_abs = 1e-12;
    double max_inst_freq_abs = 1e-12;
    int valid_phase[ANALYSIS_MAX_RENDER_W];

    memset(valid_phase, 0, sizeof(valid_phase));

    int filter_active = Global_Analysis_Filter_Active;
    int filter_bin_low = 0;
    int filter_bin_high = ANALYSIS_FFT_SIZE - 1;
    double *filter_mag = NULL;
    double *filter_re = NULL;
    double *filter_im = NULL;
    double *filter_td_phase = NULL;
    double *filter_td_inst_freq = NULL;

    int time_filter_active = Global_Analysis_Column_Active || Global_Analysis_Column_Selecting;
    int time_col_low = 0;
    int time_col_high = render_w - 1;

    if (time_filter_active) {

        double x0 = Global_Analysis_Column_X0;
        double x1 = Global_Analysis_Column_X1;

        if (x1 < x0) {

            double tmp = x0;
            x0 = x1;
            x1 = tmp;

        }

        x0 = ANALYSIS_limit_double(x0, 0.0, 1.0);
        x1 = ANALYSIS_limit_double(x1, 0.0, 1.0);

        time_col_low = (int)(x0 * (double)(render_w - 1));
        time_col_high = (int)(x1 * (double)(render_w - 1));

        if (time_col_low < 0) {

            time_col_low = 0;

        }

        if (time_col_high >= render_w) {

            time_col_high = render_w - 1;

        }

        if (time_col_high <= time_col_low) {

            time_col_high = time_col_low + 1;

        }

        if (time_col_high >= render_w) {

            time_col_high = render_w - 1;

        }

    }

    if (filter_active) {

        double y0 = Global_Analysis_Filter_Y0;
        double y1 = Global_Analysis_Filter_Y1;

        if (y1 < y0) {

            double tmp = y0;
            y0 = y1;
            y1 = tmp;

        }

        int bin_a = (int)((1.0 - y0) * (double)(ANALYSIS_FFT_SIZE - 1));
        int bin_b = (int)((1.0 - y1) * (double)(ANALYSIS_FFT_SIZE - 1));

        filter_bin_low = bin_a < bin_b ? bin_a : bin_b;
        filter_bin_high = bin_a > bin_b ? bin_a : bin_b;

        if (filter_bin_low < 0) {

            filter_bin_low = 0;

        }

        if (filter_bin_high >= ANALYSIS_FFT_SIZE) {

            filter_bin_high = ANALYSIS_FFT_SIZE - 1;

        }

        if (filter_bin_high <= filter_bin_low) {

            filter_bin_high = filter_bin_low + 1;

        }

        filter_mag = sec_calloc_array((size_t)render_w, sizeof(double), ANALYSIS_MAX_RENDER_W);
        filter_re = sec_calloc_array((size_t)render_w, sizeof(double), ANALYSIS_MAX_RENDER_W);
        filter_im = sec_calloc_array((size_t)render_w, sizeof(double), ANALYSIS_MAX_RENDER_W);
        filter_td_phase = sec_calloc_array((size_t)render_w, sizeof(double), ANALYSIS_MAX_RENDER_W);
        filter_td_inst_freq = sec_calloc_array((size_t)render_w, sizeof(double), ANALYSIS_MAX_RENDER_W);

        if (!filter_mag || !filter_re || !filter_im || !filter_td_phase || !filter_td_inst_freq) {

            filter_active = 0;

        }

    }

    if (Global_Analysis_Multithread_Enabled && (size_t)render_w <= SIZE_MAX / i16_per_block / sizeof(int16_t)) {

        thread_starts = malloc(sizeof(size_t) * (size_t)render_w);
        thread_blocks = malloc(sizeof(int16_t) * (size_t)render_w * i16_per_block);

        if (thread_starts && thread_blocks) {

            for (int x = 0; x < render_w; x++) {
                double frac = (render_w > 1) ? (double)x / (double)(render_w - 1) : 0.0;
                size_t start = Global_Analysis_View_Start + (size_t)(frac * (double)Global_Analysis_View_Len);

                if (start + ANALYSIS_FFT_SIZE >= Global_Analysis_IQ_Count) {

                    start =
                        Global_Analysis_IQ_Count > ANALYSIS_FFT_SIZE ? Global_Analysis_IQ_Count - ANALYSIS_FFT_SIZE : 0;

                }

                thread_starts[x] = start;
            }

            multithread_loaded =
                ANALYSIS_load_iq_blocks_multithreaded(fp, thread_blocks, thread_starts, render_w, i16_per_block);

        }

        if (!multithread_loaded) {

            free(thread_blocks);
            free(thread_starts);
            thread_blocks = NULL;
            thread_starts = NULL;
            snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                     "10-thread IQ loading failed; used single-thread loading");

        }

    }

    for (int x = 0; x < render_w; x++) {
        int16_t *current_block = block;

        if (multithread_loaded) {

            current_block = thread_blocks + ((size_t)x * i16_per_block);

        }

        else {

            double frac = (render_w > 1) ? (double)x / (double)(render_w - 1) : 0.0;
            size_t start = Global_Analysis_View_Start + (size_t)(frac * (double)Global_Analysis_View_Len);

            if (start + ANALYSIS_FFT_SIZE >= Global_Analysis_IQ_Count) {

                start = Global_Analysis_IQ_Count > ANALYSIS_FFT_SIZE ? Global_Analysis_IQ_Count - ANALYSIS_FFT_SIZE : 0;

            }

            if (fseek(fp, (long)(start * 2 * sizeof(int16_t)), SEEK_SET) != 0) {

                continue;

            }

            size_t got = fread(block, sizeof(int16_t), i16_per_block, fp);

            if (got < i16_per_block) {

                memset(block + got, 0, sizeof(int16_t) * (i16_per_block - got));

            }

        }

        double sum_mag = 0.0;

        for (int k = 0; k < ANALYSIS_FFT_SIZE; k++) {
            double I = (double)current_block[k * 2] / 32768.0;
            double Q = (double)current_block[k * 2 + 1] / 32768.0;

            sum_mag += sqrt(I * I + Q * Q);

            in[k][0] = I * window[k];
            in[k][1] = Q * window[k];
        }

        double avg_mag = sum_mag / (double)ANALYSIS_FFT_SIZE;

        if (filter_active && Global_Analysis_Sample_Rate > 0.0) {

            double bin_center = ((double)filter_bin_low + (double)filter_bin_high) * 0.5;
            double center_offset_hz = (bin_center - ((double)ANALYSIS_FFT_SIZE * 0.5)) *
                                      (Global_Analysis_Sample_Rate / (double)ANALYSIS_FFT_SIZE);
            double bin_width_hz = Global_Analysis_Sample_Rate / (double)ANALYSIS_FFT_SIZE;
            double filter_width_hz = (double)(filter_bin_high - filter_bin_low + 1) * bin_width_hz;
            double cutoff_hz = filter_width_hz * 0.5;

            if (cutoff_hz < bin_width_hz) {

                cutoff_hz = bin_width_hz;

            }

            if (cutoff_hz > Global_Analysis_Sample_Rate * 0.45) {

                cutoff_hz = Global_Analysis_Sample_Rate * 0.45;

            }

            double alpha = (2.0 * M_PI * cutoff_hz) / (Global_Analysis_Sample_Rate + (2.0 * M_PI * cutoff_hz));
            double omega = 2.0 * M_PI * center_offset_hz / Global_Analysis_Sample_Rate;
            double phase_sum_i = 0.0;
            double phase_sum_q = 0.0;
            double inst_freq_sum = 0.0;
            int inst_freq_count = 0;
            double lp_i = 0.0;
            double lp_q = 0.0;
            double prev_i = 0.0;
            double prev_q = 0.0;
            size_t samples_per_column =
                render_w > 0 ? Global_Analysis_View_Len / (size_t)render_w : (size_t)ANALYSIS_FFT_SIZE;
            int inst_sample_count = (int)samples_per_column;

            if (inst_sample_count < 8) {

                inst_sample_count = 8;

            }

            if (inst_sample_count > 512) {

                inst_sample_count = 512;

            }

            if (inst_sample_count > ANALYSIS_FFT_SIZE) {

                inst_sample_count = ANALYSIS_FFT_SIZE;

            }

            for (int k = 0; k < inst_sample_count; k++) {
                double I = (double)current_block[k * 2] / 32768.0;
                double Q = (double)current_block[k * 2 + 1] / 32768.0;
                double angle = -omega * (double)k;
                double c = cos(angle);
                double s = sin(angle);
                double mix_i = I * c - Q * s;
                double mix_q = I * s + Q * c;

                if (k == 0) {

                    lp_i = mix_i;
                    lp_q = mix_q;

                }

                else {

                    lp_i += alpha * (mix_i - lp_i);
                    lp_q += alpha * (mix_q - lp_q);

                }

                phase_sum_i += lp_i;
                phase_sum_q += lp_q;

                if (k > 0) {

                    double prod_i = lp_i * prev_i + lp_q * prev_q;
                    double prod_q = lp_q * prev_i - lp_i * prev_q;
                    double dphase = ANALYSIS_wrap_phase(atan2(prod_q, prod_i));

                    inst_freq_sum += dphase * Global_Analysis_Sample_Rate / (2.0 * M_PI);
                    inst_freq_count++;

                }

                prev_i = lp_i;
                prev_q = lp_q;
            }

            filter_td_phase[x] = atan2(phase_sum_q, phase_sum_i);

            if (inst_freq_count > 0) {

                filter_td_inst_freq[x] = inst_freq_sum / (double)inst_freq_count;

            }

        }

        Global_Analysis_Mag_Line[x] = (float)avg_mag;

        /*
         * Raw whole-block phase is intentionally not plotted. It is usually
         * misleading unless a single centered carrier dominates the block.
         * Phase and instantaneous frequency are populated below only when the
         * analysis frequency filter is active.
         */

        Global_Analysis_Phase_Line[x] = 0.0f;
        Global_Analysis_InstFreq_Line[x] = 0.0f;

        if ((!time_filter_active || (x >= time_col_low && x <= time_col_high)) && avg_mag > max_mag) {

            max_mag = avg_mag;

        }

        fftw_execute(plan);

        for (int y = 0; y < ANALYSIS_FFT_SIZE; y++) {
            int shifted = (y + ANALYSIS_FFT_SIZE / 2) % ANALYSIS_FFT_SIZE;
            double I = out[shifted][0];
            double Q = out[shifted][1];
            double mag = sqrt(I * I + Q * Q) / (double)ANALYSIS_FFT_SIZE;
            double val = 20.0 * log10(mag + 1e-12) + 100.0;

            db_img[(size_t)x * ANALYSIS_FFT_SIZE + y] = val;

            if (filter_active && y >= filter_bin_low && y <= filter_bin_high) {

                double weight = mag;
                filter_mag[x] += weight;
                filter_re[x] += I;
                filter_im[x] += Q;

            }
        }

        for (int y = 0; y < ANALYSIS_FFT_SIZE; y++) {
            double display_val = ANALYSIS_spectrogram_display_db(db_img, x, y);

            if (display_val > max_db) {

                max_db = display_val;

            }
        }
    }

    max_db = -300.0;

    for (int x = 0; x < render_w; x++) {
        for (int y = 0; y < ANALYSIS_FFT_SIZE; y++) {
            double display_val = ANALYSIS_spectrogram_display_db(db_img, x, y);

            if (display_val > max_db) {

                max_db = display_val;

            }
        }
    }

    if (filter_active) {

        max_mag = 1e-12;
        max_phase_abs = 1e-12;
        max_inst_freq_abs = 1e-12;

        int bin_count = filter_bin_high - filter_bin_low + 1;

        if (bin_count < 1) {

            bin_count = 1;

        }

        for (int x = 0; x < render_w; x++) {
            double avg_mag = filter_mag[x] / (double)bin_count;

            Global_Analysis_Mag_Line[x] = (float)avg_mag;
            Global_Analysis_Phase_Line[x] = 0.0f;
            Global_Analysis_InstFreq_Line[x] = 0.0f;

            if ((!time_filter_active || (x >= time_col_low && x <= time_col_high)) && avg_mag > max_mag) {

                max_mag = avg_mag;

            }
        }

        double phase_gate = max_mag * 0.15;

        if (phase_gate < 1e-9) {

            phase_gate = 1e-9;

        }

        for (int x = 0; x < render_w; x++) {
            double avg_mag = Global_Analysis_Mag_Line[x];

            if (avg_mag >= phase_gate && (!time_filter_active || (x >= time_col_low && x <= time_col_high))) {

                valid_phase[x] = 1;

            }
        }

        /*
         * Make the phase graph useful as a general phase-deviation view.
         *
         * The raw atan2() phase is wrapped to -pi..pi and usually draws as
         * vertical jumps. After a selected band is mixed to baseband, any
         * residual center-frequency error appears as a steady linear phase
         * ramp. For display, unwrap phase across each valid burst/segment and
         * remove the best-fit linear ramp from that segment. This leaves the
         * phase changes caused by modulation while hiding the unhelpful carrier
         * rotation.
         */

        int seg_start = -1;

        while (seg_start < render_w) {
            while (seg_start < render_w && !valid_phase[seg_start]) {
                seg_start++;
            }

            if (seg_start >= render_w) {

                break;

            }

            int seg_end = seg_start;

            while (seg_end + 1 < render_w && valid_phase[seg_end + 1]) {
                seg_end++;
            }

            double unwrapped = filter_td_phase[seg_start];
            double prev_raw = filter_td_phase[seg_start];
            filter_td_phase[seg_start] = unwrapped;

            for (int x = seg_start + 1; x <= seg_end; x++) {
                double raw = filter_td_phase[x];
                unwrapped += ANALYSIS_wrap_phase(raw - prev_raw);
                filter_td_phase[x] = unwrapped;
                prev_raw = raw;
            }

            int n = seg_end - seg_start + 1;

            if (n >= 2) {

                double sum_x = 0.0;
                double sum_y = 0.0;
                double sum_xx = 0.0;
                double sum_xy = 0.0;

                for (int x = seg_start; x <= seg_end; x++) {
                    double dx = (double)(x - seg_start);
                    double y = filter_td_phase[x];

                    sum_x += dx;
                    sum_y += y;
                    sum_xx += dx * dx;
                    sum_xy += dx * y;
                }

                double denom = ((double)n * sum_xx) - (sum_x * sum_x);
                double slope = 0.0;
                double intercept = sum_y / (double)n;

                if (fabs(denom) > 1e-12) {

                    slope = (((double)n * sum_xy) - (sum_x * sum_y)) / denom;
                    intercept = (sum_y - slope * sum_x) / (double)n;

                }

                for (int x = seg_start; x <= seg_end; x++) {
                    double dx = (double)(x - seg_start);
                    filter_td_phase[x] -= intercept + slope * dx;
                }

            }

            else {

                filter_td_phase[seg_start] = 0.0;

            }

            seg_start = seg_end + 1;
        }

        double inst_freq_mean = 0.0;
        int inst_freq_mean_count = 0;

        for (int x = 0; x < render_w; x++) {

            if (valid_phase[x]) {

                inst_freq_mean += filter_td_inst_freq[x];
                inst_freq_mean_count++;

            }
        }

        if (inst_freq_mean_count > 0) {

            inst_freq_mean /= (double)inst_freq_mean_count;

        }

        for (int x = 0; x < render_w; x++) {
            double phase = 0.0;
            double inst_freq_hz = 0.0;

            if (valid_phase[x]) {

                phase = filter_td_phase[x];
                inst_freq_hz = filter_td_inst_freq[x] - inst_freq_mean;

                if (fabs(phase) > max_phase_abs) {

                    max_phase_abs = fabs(phase);

                }

                if (fabs(inst_freq_hz) > max_inst_freq_abs) {

                    max_inst_freq_abs = fabs(inst_freq_hz);

                }

            }

            Global_Analysis_Phase_Line[x] = (float)phase;
            Global_Analysis_InstFreq_Line[x] = (float)inst_freq_hz;
        }

    }

    if (time_filter_active) {

        for (int x = 0; x < render_w; x++) {

            if (x < time_col_low || x > time_col_high) {

                Global_Analysis_Mag_Line[x] = 0.0f;
                Global_Analysis_Phase_Line[x] = 0.0f;
                Global_Analysis_InstFreq_Line[x] = 0.0f;

            }
        }

    }

    /*
     * Frequency Spectrum / PSD
     *
     * The PSD now respects the active analysis selectors:
     * - The time-domain selector limits which spectrogram columns are averaged.
     * - The frequency selector limits which FFT bins are displayed.
     *
     * Bins outside the selected frequency range are hidden instead of merely
     * dimmed, so the graph represents only the selected signal region.
     */
    double psd_min_db = 300.0;
    double psd_max_db = -300.0;
    int psd_valid[ANALYSIS_MAX_RENDER_W];

    memset(psd_valid, 0, sizeof(psd_valid));

    for (int x = 0; x < render_w; x++) {
        int bin = render_w > 1 ? (int)(((double)x / (double)(render_w - 1)) * (double)(ANALYSIS_FFT_SIZE - 1))
                               : ANALYSIS_FFT_SIZE / 2;

        if (bin < 0) {

            bin = 0;

        }

        if (bin >= ANALYSIS_FFT_SIZE) {

            bin = ANALYSIS_FFT_SIZE - 1;

        }

        if (filter_active && (bin < filter_bin_low || bin > filter_bin_high)) {

            Global_Analysis_PSD_Line[x] = 0.0f;
            continue;

        }

        double sum_db = 0.0;
        int count_db = 0;

        for (int t = 0; t < render_w; t++) {

            if (time_filter_active && (t < time_col_low || t > time_col_high)) {

                continue;

            }

            sum_db += db_img[(size_t)t * ANALYSIS_FFT_SIZE + bin];
            count_db++;
        }

        if (count_db <= 0) {

            Global_Analysis_PSD_Line[x] = 0.0f;
            continue;

        }

        double avg_db = sum_db / (double)count_db;

        Global_Analysis_PSD_Line[x] = (float)avg_db;
        psd_valid[x] = 1;

        if (avg_db < psd_min_db) {

            psd_min_db = avg_db;

        }

        if (avg_db > psd_max_db) {

            psd_max_db = avg_db;

        }
    }

    double psd_range_db = psd_max_db - psd_min_db;

    if (psd_range_db < 1e-9) {

        psd_range_db = 1.0;

    }

    for (int x = 0; x < render_w; x++) {

        if (!psd_valid[x]) {

            Global_Analysis_PSD_Line[x] = 0.0f;
            continue;

        }

        Global_Analysis_PSD_Line[x] = (float)((Global_Analysis_PSD_Line[x] - psd_min_db) / psd_range_db);

        if (Global_Analysis_PSD_Line[x] < 0.0f) {

            Global_Analysis_PSD_Line[x] = 0.0f;

        }

        if (Global_Analysis_PSD_Line[x] > 1.0f) {

            Global_Analysis_PSD_Line[x] = 1.0f;

        }
    }

    for (int x = 0; x < render_w; x++) {
        Global_Analysis_Mag_Line[x] = (float)(Global_Analysis_Mag_Line[x] / max_mag);
        Global_Analysis_Phase_Line[x] = (float)(Global_Analysis_Phase_Line[x] / max_phase_abs);
        Global_Analysis_InstFreq_Line[x] = (float)(Global_Analysis_InstFreq_Line[x] / max_inst_freq_abs);

        if (Global_Analysis_Mag_Line[x] < 0.0f) {

            Global_Analysis_Mag_Line[x] = 0.0f;

        }

        if (Global_Analysis_Mag_Line[x] > 1.0f) {

            Global_Analysis_Mag_Line[x] = 1.0f;

        }

        if (Global_Analysis_Phase_Line[x] < -1.0f) {

            Global_Analysis_Phase_Line[x] = -1.0f;

        }

        if (Global_Analysis_Phase_Line[x] > 1.0f) {

            Global_Analysis_Phase_Line[x] = 1.0f;

        }

        if (Global_Analysis_InstFreq_Line[x] < -1.0f) {

            Global_Analysis_InstFreq_Line[x] = -1.0f;

        }

        if (Global_Analysis_InstFreq_Line[x] > 1.0f) {

            Global_Analysis_InstFreq_Line[x] = 1.0f;

        }
    }

    ANALYSIS_apply_noise_filter_to_rendered_lines(render_w);

    double min_db = max_db - 70.0;

    for (int x = 0; x < tex_w; x++) {
        int src_x = x;

        if (src_x >= render_w) {

            src_x = render_w - 1;

        }

        for (int py = 0; py < tex_h; py++) {
            int bin = (int)((1.0 - ((double)py / (double)tex_h)) * (double)(ANALYSIS_FFT_SIZE - 1));
            double val = ANALYSIS_spectrogram_display_db(db_img, src_x, bin);
            double norm = (val - min_db) / 70.0;

            if (filter_active && (bin < filter_bin_low || bin > filter_bin_high)) {

                norm *= 0.30;

            }

            if (time_filter_active && (src_x < time_col_low || src_x > time_col_high)) {

                norm *= 0.30;

            }

            pixels[(size_t)py * tex_w + x] = ANALYSIS_gray(norm);
        }
    }

    ANALYSIS_build_selected_constellation(fp, filter_active, filter_bin_low, filter_bin_high, time_filter_active,
                                          time_col_low, time_col_high, render_w);

    fftw_destroy_plan(plan);
    free(db_img);
    free(thread_blocks);
    free(thread_starts);
    free(block);
    free(filter_mag);
    free(filter_re);
    free(filter_im);
    free(filter_td_phase);
    free(filter_td_inst_freq);
    fftw_free(in);
    fftw_free(out);
    fclose(fp);
}

void ANALYSIS_enter_mode(const char *record_dir, uint64_t fallback_center_hz, uint32_t fallback_rec_out_rate_hz,
                         uint32_t fallback_sample_rate_hz) {
    /*
        Purpose: Enters analysis mode and prepares the file list
        Returns: No value
    */

    ANALYSIS_set_context(record_dir, fallback_center_hz, fallback_rec_out_rate_hz, fallback_sample_rate_hz);

    Global_Analysis_Mode = 1;
    Global_Analysis_Multithread_Prompt_Open = 0;
    Global_Analysis_Constellation_PSK_Prompt_Open = 0;
    ANALYSIS_close_transmit_prompts();

    if (Global_Analysis_Workspaces_Initialized) {

        Global_Analysis_Dragging = 0;
        Global_Analysis_Filter_Selecting = 0;
        Global_Analysis_Column_Selecting = 0;
        Global_Analysis_Noise_Selecting = 0;
        Global_Analysis_Noise_Key_Down = 0;
        Global_Analysis_Dirty = 1;

        set_status("Analysis workstation", (SDL_Color){220, 220, 220, 255});
        return;

    }

    Global_Analysis_Active_Workspace = 0;

    for (int i = 0; i < ANALYSIS_WORKSPACE_COUNT; i++) {
        Global_Analysis_Active_Workspace = i;
        ANALYSIS_clear_loaded_file();

        if (ANALYSIS_scan_recordings()) {

            snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                     "Found %d recording(s). Select one and press Enter.", Global_Analysis_File_Count);

        }

        else {

            Global_Analysis_Dirty = 1;

        }

        ANALYSIS_save_workspace_state(i);
    }

    Global_Analysis_Workspaces_Initialized = 1;
    Global_Analysis_Active_Workspace = 0;
    ANALYSIS_load_workspace_state(Global_Analysis_Active_Workspace);
    Global_Analysis_Dragging = 0;

    set_status("Analysis workstation", (SDL_Color){220, 220, 220, 255});
}

int ANALYSIS_is_text_entry_active(void) {
    /*
        Purpose: Checks whether the text entry is active
        Returns: Boolean status
    */

    return (Global_Analysis_Signal_Menu_Open && Global_Analysis_Signal_Active_Field != ANALYSIS_SIGNAL_FIELD_NONE) ||
           (Global_Analysis_File_Search_Open && Global_Analysis_File_Search_Active) ||
           Global_Analysis_Transmit_Auth_Prompt_Open || Global_Analysis_Transmit_Config_Prompt_Open;
}

int ANALYSIS_handle_event(SDL_Event *event, int win_w, int win_h, uint32_t *pixels, int tex_w, int tex_h,
                          SDL_Texture *waterfall_texture, uint64_t *next_waterfall_ms, Type_Active_Fields *active) {
    /*
        Purpose: Handles the event
        Returns: Handling status
    */

    if (!event || !Global_Analysis_Mode) {

        return ANALYSIS_EVENT_IGNORED;

    }

    if (Global_Analysis_Constellation_PSK_Prompt_Open) {

        ANALYSIS_handle_constellation_psk_prompt_event(event, win_w, win_h);

        if (active) {

            *active = FIELD_NONE;

        }
        return ANALYSIS_EVENT_HANDLED;

    }

    if (ANALYSIS_handle_file_search_event(event, win_w, win_h)) {

        if (active) {

            *active = FIELD_NONE;

        }
        return ANALYSIS_EVENT_HANDLED;

    }

    if (ANALYSIS_handle_signal_menu_event(event, win_w, win_h)) {

        if (active) {

            *active = FIELD_NONE;

        }
        return ANALYSIS_EVENT_HANDLED;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (active && *active == FIELD_NONE && (SDL_GetModState() & KMOD_CTRL)) {

            if (key == SDLK_RIGHT) {

                ANALYSIS_switch_workspace(1);
                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_LEFT) {

                ANALYSIS_switch_workspace(-1);
                return ANALYSIS_EVENT_HANDLED;

            }

        }

        if (active && *active == FIELD_NONE) {

            if (key == SDLK_LCTRL || key == SDLK_RCTRL) {

                Global_Analysis_Filter_Visible = 1;
                snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                         "Ctrl+drag on greyscale spectrogram to select a frequency band");
                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_n) {

                Global_Analysis_Noise_Key_Down = 1;
                snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                         "N+drag on Magnitude or Instantaneous Frequency to mark noise");
                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_ESCAPE) {

                ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);

                if (next_waterfall_ms) {

                    *next_waterfall_ms = SDL_GetTicks64();

                }
                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_q) {

                return ANALYSIS_EVENT_QUIT;

            }

            else if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {

                ANALYSIS_clear_filter();
                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_r) {

                ANALYSIS_clear_loaded_file();

                if (ANALYSIS_scan_recordings()) {

                    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                             "Found %d recording(s). Select one and press Enter.", Global_Analysis_File_Count);

                }

                else {

                    Global_Analysis_Dirty = 1;

                }

                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {

                ANALYSIS_open_selected_recording();
                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_UP) {

                ANALYSIS_select_relative(-1);
                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_DOWN) {

                ANALYSIS_select_relative(1);
                return ANALYSIS_EVENT_HANDLED;

            }

        }

        return ANALYSIS_EVENT_IGNORED;

    }

    if (event->type == SDL_KEYUP) {

        SDL_Keycode key = event->key.keysym.sym;

        if (key == SDLK_LCTRL || key == SDLK_RCTRL) {

            if (Global_Analysis_Filter_Selecting) {

                int my = 0;
                ANALYSIS_get_adjusted_mouse_state(NULL, &my);

                SDL_Rect list_rect;
                SDL_Rect spec_rect;

                ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
                (void)list_rect;
                ANALYSIS_update_filter_from_mouse(my, spec_rect);
                ANALYSIS_apply_filter_selection();
                Global_Analysis_Filter_Selecting = 0;

            }

            if (!Global_Analysis_Filter_Active) {

                Global_Analysis_Filter_Visible = 0;

            }

            return ANALYSIS_EVENT_HANDLED;

        }

        if (key == SDLK_n) {

            Global_Analysis_Noise_Key_Down = 0;

            if (Global_Analysis_Noise_Selecting) {

                int mx = 0;
                int my = 0;
                ANALYSIS_get_adjusted_mouse_state(&mx, &my);

                SDL_Rect graph_rect;

                if (ANALYSIS_noise_graph_from_point(mx, my, win_w, win_h, &graph_rect) == ANALYSIS_NOISE_GRAPH_NONE) {

                    SDL_Rect psd_rect;
                    SDL_Rect mag_rect;
                    SDL_Rect phase_rect;
                    SDL_Rect inst_rect;
                    SDL_Rect const_rect;
                    SDL_Rect spec_rect;

                    ANALYSIS_get_hover_graph_layout(win_w, win_h, &psd_rect, &mag_rect, &phase_rect, &inst_rect,
                                                    &const_rect, &spec_rect);
                    (void)psd_rect;
                    (void)phase_rect;
                    (void)const_rect;
                    (void)spec_rect;
                    graph_rect = Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_MAG ? mag_rect : inst_rect;

                }

                ANALYSIS_update_noise_selection_from_mouse(my, graph_rect);
                ANALYSIS_apply_noise_selection();

            }

            if (!Global_Analysis_Noise_Active) {

                Global_Analysis_Noise_Visible = 0;
                Global_Analysis_Noise_Graph = ANALYSIS_NOISE_GRAPH_NONE;

            }

            return ANALYSIS_EVENT_HANDLED;

        }

        return ANALYSIS_EVENT_IGNORED;

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mx = 0;
        int my = 0;
        ANALYSIS_get_adjusted_mouse_state(&mx, &my);

        SDL_Rect list_rect;
        SDL_Rect spec_rect;

        ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);

        if (point_in_rect(mx, my, list_rect)) {

            int row_h = 22;
            int visible = (list_rect.h - 82) / row_h;

            if (visible < 1) {

                visible = 1;

            }

            Global_Analysis_List_Scroll -= event->wheel.y * 3;

            if (Global_Analysis_List_Scroll < 0) {

                Global_Analysis_List_Scroll = 0;

            }

            if (Global_Analysis_List_Scroll + visible > Global_Analysis_File_Count) {

                Global_Analysis_List_Scroll = Global_Analysis_File_Count - visible;

                if (Global_Analysis_List_Scroll < 0) {

                    Global_Analysis_List_Scroll = 0;

                }

            }

        }

        else {

            double frac = (double)(mx - spec_rect.x) / (double)spec_rect.w;
            ANALYSIS_zoom_at_fraction(frac, event->wheel.y > 0);

        }

        return ANALYSIS_EVENT_HANDLED;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_RIGHT) {

        int x = event->button.x;
        int y = event->button.y;

        SDL_Rect list_rect;
        SDL_Rect spec_rect;

        ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
        (void)list_rect;

        if (Global_Analysis_Path[0] != '\0' && point_in_rect(x, y, spec_rect)) {

            ANALYSIS_set_marker_from_mouse(x, spec_rect);

        }

        return ANALYSIS_EVENT_HANDLED;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        int x = event->button.x;
        int y = event->button.y;

        if (active) {

            *active = FIELD_NONE;

        }

        SDL_Rect list_rect;
        SDL_Rect spec_rect;

        ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);

        SDL_Rect search_button = ANALYSIS_file_search_button_rect(win_w, win_h);
        SDL_Rect clear_workspace_button = ANALYSIS_clear_workspace_button_rect(win_w, win_h);
        SDL_Rect crop_button = ANALYSIS_crop_button_rect(win_w, win_h);

        if (ANALYSIS_handle_constellation_mode_click(x, y, win_w, win_h)) {

            return ANALYSIS_EVENT_HANDLED;

        }

        if (point_in_rect(x, y, search_button)) {

            ANALYSIS_open_file_search_menu();
            return ANALYSIS_EVENT_HANDLED;

        }

        if (point_in_rect(x, y, clear_workspace_button)) {

            if (Global_Analysis_Path[0] != '\0' && Global_Analysis_IQ_Count > 0) {

                ANALYSIS_clear_current_workspace();

            }
            return ANALYSIS_EVENT_HANDLED;

        }

        if (point_in_rect(x, y, crop_button)) {

            ANALYSIS_crop_current_selection(pixels, tex_w, tex_h, waterfall_texture);
            return ANALYSIS_EVENT_HANDLED;

        }

        if (point_in_rect(x, y, list_rect)) {

            int row_h = 22;
            int list_y = list_rect.y + 70;
            int visible = (list_rect.h - 82) / row_h;

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

            int idx = first + ((y - list_y) / row_h);

            if (y >= list_y && idx >= 0 && idx < Global_Analysis_File_Count) {

                Global_Analysis_Selected = idx;

                if (event->button.clicks >= 2) {

                    ANALYSIS_open_selected_recording();

                }

                else {

                    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                             "Selected %.180s | Press Enter to open", Global_Analysis_Files[Global_Analysis_Selected]);

                }

            }

        }

        else if (Global_Analysis_Path[0] != '\0' &&
                 (Global_Analysis_Noise_Key_Down || SDL_GetKeyboardState(NULL)[SDL_SCANCODE_N])) {

            SDL_Rect graph_rect;
            int noise_graph = ANALYSIS_noise_graph_from_point(x, y, win_w, win_h, &graph_rect);

            if (noise_graph != ANALYSIS_NOISE_GRAPH_NONE) {

                Global_Analysis_Noise_Visible = 1;
                Global_Analysis_Noise_Selecting = 1;
                Global_Analysis_Noise_Active = 0;
                Global_Analysis_Noise_Graph = noise_graph;
                Global_Analysis_Dragging = 0;
                Global_Analysis_Filter_Selecting = 0;
                Global_Analysis_Column_Selecting = 0;
                Global_Analysis_Noise_Y0 = ANALYSIS_noise_frac_from_mouse_y(y, graph_rect);
                Global_Analysis_Noise_Y1 = Global_Analysis_Noise_Y0;
                return ANALYSIS_EVENT_HANDLED;

            }

        }

        else if (Global_Analysis_Path[0] != '\0' && point_in_rect(x, y, spec_rect) &&
                 (SDL_GetModState() & KMOD_SHIFT)) {

            Global_Analysis_Column_Visible = 1;
            Global_Analysis_Column_Selecting = 1;
            Global_Analysis_Dragging = 0;
            Global_Analysis_Filter_Selecting = 0;
            Global_Analysis_Column_X0 = ANALYSIS_time_frac_from_mouse_x(x, spec_rect);
            Global_Analysis_Column_X1 = Global_Analysis_Column_X0;

        }

        else if (Global_Analysis_Path[0] != '\0' && point_in_rect(x, y, spec_rect) && (SDL_GetModState() & KMOD_CTRL)) {

            Global_Analysis_Filter_Visible = 1;
            Global_Analysis_Filter_Selecting = 1;
            Global_Analysis_Dragging = 0;
            Global_Analysis_Column_Selecting = 0;
            Global_Analysis_Filter_Y0 = ANALYSIS_freq_frac_from_mouse_y(y, spec_rect);
            Global_Analysis_Filter_Y1 = Global_Analysis_Filter_Y0;

        }

        else if (Global_Analysis_Path[0] != '\0' && y > list_rect.y + list_rect.h + MARGIN) {

            Global_Analysis_Dragging = 1;
            Global_Analysis_Drag_Last_X = x;

        }

        return ANALYSIS_EVENT_HANDLED;

    }

    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {

        if (Global_Analysis_Noise_Selecting) {

            SDL_Rect psd_rect;
            SDL_Rect mag_rect;
            SDL_Rect phase_rect;
            SDL_Rect inst_rect;
            SDL_Rect const_rect;
            SDL_Rect spec_rect;

            ANALYSIS_get_hover_graph_layout(win_w, win_h, &psd_rect, &mag_rect, &phase_rect, &inst_rect, &const_rect,
                                            &spec_rect);

            (void)psd_rect;
            (void)phase_rect;
            (void)const_rect;
            (void)spec_rect;

            SDL_Rect graph_rect = Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_MAG ? mag_rect : inst_rect;

            ANALYSIS_update_noise_selection_from_mouse(event->button.y, graph_rect);
            ANALYSIS_apply_noise_selection();

        }

        else if (Global_Analysis_Column_Selecting) {

            SDL_Rect list_rect;
            SDL_Rect spec_rect;

            ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
            (void)list_rect;
            ANALYSIS_update_column_selection_from_mouse(event->button.x, spec_rect);
            ANALYSIS_apply_column_selection();

        }

        else if (Global_Analysis_Filter_Selecting) {

            SDL_Rect list_rect;
            SDL_Rect spec_rect;

            ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
            (void)list_rect;
            ANALYSIS_update_filter_from_mouse(event->button.y, spec_rect);
            ANALYSIS_apply_filter_selection();

        }

        Global_Analysis_Column_Selecting = 0;
        Global_Analysis_Filter_Selecting = 0;
        Global_Analysis_Noise_Selecting = 0;
        Global_Analysis_Dragging = 0;

        return ANALYSIS_EVENT_HANDLED;

    }

    if (event->type == SDL_MOUSEMOTION) {

        if (Global_Analysis_Noise_Selecting) {

            SDL_Rect psd_rect;
            SDL_Rect mag_rect;
            SDL_Rect phase_rect;
            SDL_Rect inst_rect;
            SDL_Rect const_rect;
            SDL_Rect spec_rect;

            ANALYSIS_get_hover_graph_layout(win_w, win_h, &psd_rect, &mag_rect, &phase_rect, &inst_rect, &const_rect,
                                            &spec_rect);

            (void)psd_rect;
            (void)phase_rect;
            (void)const_rect;
            (void)spec_rect;

            SDL_Rect graph_rect = Global_Analysis_Noise_Graph == ANALYSIS_NOISE_GRAPH_MAG ? mag_rect : inst_rect;

            ANALYSIS_update_noise_selection_from_mouse(event->motion.y, graph_rect);
            return ANALYSIS_EVENT_HANDLED;

        }

        if (Global_Analysis_Column_Selecting) {

            SDL_Rect list_rect;
            SDL_Rect spec_rect;

            ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
            (void)list_rect;
            ANALYSIS_update_column_selection_from_mouse(event->motion.x, spec_rect);
            return ANALYSIS_EVENT_HANDLED;

        }

        if (Global_Analysis_Filter_Selecting) {

            SDL_Rect list_rect;
            SDL_Rect spec_rect;

            ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
            (void)list_rect;
            ANALYSIS_update_filter_from_mouse(event->motion.y, spec_rect);
            return ANALYSIS_EVENT_HANDLED;

        }

        if (Global_Analysis_Dragging) {

            int dx = event->motion.x - Global_Analysis_Drag_Last_X;
            Global_Analysis_Drag_Last_X = event->motion.x;
            ANALYSIS_drag_move_view(dx, win_w - 2 * MARGIN);
            return ANALYSIS_EVENT_HANDLED;

        }

    }

    return ANALYSIS_EVENT_IGNORED;
}
