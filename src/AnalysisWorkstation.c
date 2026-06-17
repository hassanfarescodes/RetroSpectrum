/*
 * ============================================================================
 * File:            AnalysisWorkstation.c
 * Author:          Hassan Fares
 *
 * Description:     Analysis Workstation logic for RetroSpectrum.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fftw3.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "GUIs.h"
#include "AnalysisWorkstation.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MARGIN 20
#define ANALYSIS_MAX_FILES 512
#define ANALYSIS_FFT_SIZE 2048
#define ANALYSIS_MAX_CONST_POINTS 4096
#define ANALYSIS_WORKSPACE_COUNT 5

static char Global_Analysis_Record_Dir[512] = "Recordings";
static uint64_t Global_Analysis_Fallback_Center_Hz = 0;
static uint32_t Global_Analysis_Fallback_Rec_Out_Rate_Hz = 0;
static uint32_t Global_Analysis_Fallback_Sample_Rate_Hz = 0;

static double ANALYSIS_limit_double(double value, double low, double high){

    if (value < low) return low;
    if (value > high) return high;
    return value;

}

static void ANALYSIS_set_context(const char *record_dir,
                                 uint64_t fallback_center_hz,
                                 uint32_t fallback_rec_out_rate_hz,
                                 uint32_t fallback_sample_rate_hz){

    if (record_dir && record_dir[0] != '\0') {

        snprintf(Global_Analysis_Record_Dir,
                 sizeof(Global_Analysis_Record_Dir),
                 "%s",
                 record_dir);

    }

    Global_Analysis_Fallback_Center_Hz = fallback_center_hz;
    Global_Analysis_Fallback_Rec_Out_Rate_Hz = fallback_rec_out_rate_hz;
    Global_Analysis_Fallback_Sample_Rate_Hz = fallback_sample_rate_hz;

}

int                     Global_Analysis_Mode            = 0;
int                     Global_Analysis_Dirty           = 0;
int                     Global_Analysis_File_Count      = 0;
int                     Global_Analysis_Selected        = 0;
int                     Global_Analysis_List_Scroll     = 0;
int                     Global_Analysis_Dragging        = 0;
int                     Global_Analysis_Drag_Last_X     = 0;
int                     Global_Analysis_Loading         = 0;
int                     Global_Analysis_Load_Frame      = 0;
int                     Global_Analysis_Loaded_Index    = -1;
int                     Global_Analysis_Render_W        = 0;
size_t                  Global_Analysis_IQ_Count        = 0;
size_t                  Global_Analysis_View_Start      = 0;
size_t                  Global_Analysis_View_Len        = 0;
double                  Global_Analysis_Sample_Rate     = 0.0;
double                  Global_Analysis_Center_Hz       = 0.0;
char                    Global_Analysis_Path[1024]      = "";
int                     Global_Analysis_Const_Count     = 0;
int                     Global_Analysis_Filter_Visible  = 0;
int                     Global_Analysis_Filter_Selecting= 0;
int                     Global_Analysis_Filter_Active   = 0;
double                  Global_Analysis_Filter_Y0       = 0.40;
double                  Global_Analysis_Filter_Y1       = 0.60;
int                     Global_Analysis_Marker_Active   = 0;
size_t                  Global_Analysis_Marker_Sample   = 0;
double                  Global_Analysis_Marker_Time     = 0.0;
int                     Global_Analysis_Column_Selecting = 0;
int                     Global_Analysis_Column_Visible  = 0;
int                     Global_Analysis_Column_Active   = 0;
double                  Global_Analysis_Column_X0       = 0.0;
double                  Global_Analysis_Column_X1       = 0.0;
char                    Global_Analysis_Status[256]     = "Press R to scan recordings";
char                    Global_Analysis_Files[ANALYSIS_MAX_FILES][512];
float                   Global_Analysis_Mag_Line[ANALYSIS_MAX_RENDER_W];
float                   Global_Analysis_Phase_Line[ANALYSIS_MAX_RENDER_W];
float                   Global_Analysis_InstFreq_Line[ANALYSIS_MAX_RENDER_W];
float                   Global_Analysis_PSD_Line[ANALYSIS_MAX_RENDER_W];
float                   Global_Analysis_Const_I[ANALYSIS_MAX_CONST_POINTS];
float                   Global_Analysis_Const_Q[ANALYSIS_MAX_CONST_POINTS];


typedef struct Type_Analysis_Workspace_State {
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
    size_t iq_count;
    size_t view_start;
    size_t view_len;
    double sample_rate;
    double center_hz;
    char path[1024];
    int const_count;
    int filter_visible;
    int filter_selecting;
    int filter_active;
    double filter_y0;
    double filter_y1;
    int marker_active;
    size_t marker_sample;
    double marker_time;
    int column_selecting;
    int column_visible;
    int column_active;
    double column_x0;
    double column_x1;
    char status[256];
    char files[ANALYSIS_MAX_FILES][512];
    float mag_line[ANALYSIS_MAX_RENDER_W];
    float phase_line[ANALYSIS_MAX_RENDER_W];
    float inst_freq_line[ANALYSIS_MAX_RENDER_W];
    float psd_line[ANALYSIS_MAX_RENDER_W];
    float const_i[ANALYSIS_MAX_CONST_POINTS];
    float const_q[ANALYSIS_MAX_CONST_POINTS];
} Type_Analysis_Workspace_State;

static Type_Analysis_Workspace_State Global_Analysis_Workspaces[ANALYSIS_WORKSPACE_COUNT];
static int Global_Analysis_Active_Workspace = 0;
static int Global_Analysis_Workspaces_Initialized = 0;

static void ANALYSIS_save_workspace_state(int index){

    if (index < 0 || index >= ANALYSIS_WORKSPACE_COUNT) return;

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
    ws->filter_visible = Global_Analysis_Filter_Visible;
    ws->filter_selecting = Global_Analysis_Filter_Selecting;
    ws->filter_active = Global_Analysis_Filter_Active;
    ws->filter_y0 = Global_Analysis_Filter_Y0;
    ws->filter_y1 = Global_Analysis_Filter_Y1;
    ws->marker_active = Global_Analysis_Marker_Active;
    ws->marker_sample = Global_Analysis_Marker_Sample;
    ws->marker_time = Global_Analysis_Marker_Time;
    ws->column_selecting = Global_Analysis_Column_Selecting;
    ws->column_visible = Global_Analysis_Column_Visible;
    ws->column_active = Global_Analysis_Column_Active;
    ws->column_x0 = Global_Analysis_Column_X0;
    ws->column_x1 = Global_Analysis_Column_X1;
    snprintf(ws->status, sizeof(ws->status), "%s", Global_Analysis_Status);
    memcpy(ws->files, Global_Analysis_Files, sizeof(ws->files));
    memcpy(ws->mag_line, Global_Analysis_Mag_Line, sizeof(ws->mag_line));
    memcpy(ws->phase_line, Global_Analysis_Phase_Line, sizeof(ws->phase_line));
    memcpy(ws->inst_freq_line, Global_Analysis_InstFreq_Line, sizeof(ws->inst_freq_line));
    memcpy(ws->psd_line, Global_Analysis_PSD_Line, sizeof(ws->psd_line));
    memcpy(ws->const_i, Global_Analysis_Const_I, sizeof(ws->const_i));
    memcpy(ws->const_q, Global_Analysis_Const_Q, sizeof(ws->const_q));

}

static void ANALYSIS_load_workspace_state(int index){

    if (index < 0 || index >= ANALYSIS_WORKSPACE_COUNT) return;

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
    Global_Analysis_Filter_Visible = ws->filter_visible;
    Global_Analysis_Filter_Selecting = ws->filter_selecting;
    Global_Analysis_Filter_Active = ws->filter_active;
    Global_Analysis_Filter_Y0 = ws->filter_y0;
    Global_Analysis_Filter_Y1 = ws->filter_y1;
    Global_Analysis_Marker_Active = ws->marker_active;
    Global_Analysis_Marker_Sample = ws->marker_sample;
    Global_Analysis_Marker_Time = ws->marker_time;
    Global_Analysis_Column_Selecting = ws->column_selecting;
    Global_Analysis_Column_Visible = ws->column_visible;
    Global_Analysis_Column_Active = ws->column_active;
    Global_Analysis_Column_X0 = ws->column_x0;
    Global_Analysis_Column_X1 = ws->column_x1;
    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "%s", ws->status);
    memcpy(Global_Analysis_Files, ws->files, sizeof(Global_Analysis_Files));
    memcpy(Global_Analysis_Mag_Line, ws->mag_line, sizeof(Global_Analysis_Mag_Line));
    memcpy(Global_Analysis_Phase_Line, ws->phase_line, sizeof(Global_Analysis_Phase_Line));
    memcpy(Global_Analysis_InstFreq_Line, ws->inst_freq_line, sizeof(Global_Analysis_InstFreq_Line));
    memcpy(Global_Analysis_PSD_Line, ws->psd_line, sizeof(Global_Analysis_PSD_Line));
    memcpy(Global_Analysis_Const_I, ws->const_i, sizeof(Global_Analysis_Const_I));
    memcpy(Global_Analysis_Const_Q, ws->const_q, sizeof(Global_Analysis_Const_Q));

}

static void ANALYSIS_switch_workspace(int delta){

    if (delta == 0) return;

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
    Global_Analysis_Dirty = 1;

}

// ==========================
// Greyscale Recordings Viewer
// ==========================

static int ANALYSIS_name_compare(const void *a, const void *b){

    /*

    Purpose: Compares analysis recording file names for sorting

    Return: Sort order

    */

    const char *sa = (const char *)a;
    const char *sb = (const char *)b;

    return strcmp(sa, sb);

}

static int ANALYSIS_is_complex16_file(const char *name){

    /*

    Purpose: Checks whether a filename has the complex16 recording suffix

    Return: Match status

    */

    size_t len = strlen(name);
    const char *suffix = ".complex16";
    size_t suffix_len = strlen(suffix);

    if (len < suffix_len) return 0;

    return strcmp(name + len - suffix_len, suffix) == 0;

}

static void ANALYSIS_clear_loaded_file(void){

    /*

    Purpose: Clears the currently loaded analysis recording state

    Return: No return

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
    Global_Analysis_Const_Count = 0;
    Global_Analysis_Filter_Visible = 0;
    Global_Analysis_Filter_Selecting = 0;
    Global_Analysis_Filter_Active = 0;
    Global_Analysis_Filter_Y0 = 0.40;
    Global_Analysis_Filter_Y1 = 0.60;
    Global_Analysis_Marker_Active = 0;
    Global_Analysis_Marker_Sample = 0;
    Global_Analysis_Marker_Time = 0.0;
    Global_Analysis_Column_Selecting = 0;
    Global_Analysis_Column_Visible = 0;
    Global_Analysis_Column_Active = 0;
    Global_Analysis_Column_X0 = 0.0;
    Global_Analysis_Column_X1 = 0.0;
    Global_Analysis_Dirty = 1;

}

static void ANALYSIS_parse_recording_metadata(const char *name){

    /*

    Purpose: Parses sample rate and center frequency from a recording filename

    Return: No return

    */

    Global_Analysis_Center_Hz = (double)Global_Analysis_Fallback_Center_Hz;
    Global_Analysis_Sample_Rate = (double)((Global_Analysis_Fallback_Rec_Out_Rate_Hz > 0) ? Global_Analysis_Fallback_Rec_Out_Rate_Hz : Global_Analysis_Fallback_Sample_Rate_Hz);

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

static int ANALYSIS_scan_recordings(void){

    /*

    Purpose: Scans the recording directory for complex16 files

    Return: Scan status

    */

    DIR *dir = opendir(Global_Analysis_Record_Dir);

    Global_Analysis_File_Count = 0;

    if (!dir) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                 "Could not open recording directory: %.180s", Global_Analysis_Record_Dir);
        ANALYSIS_clear_loaded_file();
        return 0;

    }

    struct dirent *entry = NULL;

    while ((entry = readdir(dir)) != NULL && Global_Analysis_File_Count < ANALYSIS_MAX_FILES) {

        if (!ANALYSIS_is_complex16_file(entry->d_name)) continue;

        snprintf(Global_Analysis_Files[Global_Analysis_File_Count],
                 sizeof(Global_Analysis_Files[Global_Analysis_File_Count]),
                 "%s", entry->d_name);
        Global_Analysis_File_Count++;

    }

    closedir(dir);

    qsort(Global_Analysis_Files,
          (size_t)Global_Analysis_File_Count,
          sizeof(Global_Analysis_Files[0]),
          ANALYSIS_name_compare);

    if (Global_Analysis_File_Count <= 0) {

        Global_Analysis_Selected = 0;
        Global_Analysis_List_Scroll = 0;
        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                 "No .complex16 recordings found in %.180s", Global_Analysis_Record_Dir);
        ANALYSIS_clear_loaded_file();
        return 0;

    }

    if (Global_Analysis_Selected < 0) Global_Analysis_Selected = 0;

    if (Global_Analysis_Selected >= Global_Analysis_File_Count) {

        Global_Analysis_Selected = Global_Analysis_File_Count - 1;

    }

    if (Global_Analysis_List_Scroll < 0) Global_Analysis_List_Scroll = 0;

    if (Global_Analysis_List_Scroll >= Global_Analysis_File_Count) {

        Global_Analysis_List_Scroll = Global_Analysis_File_Count - 1;

    }

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Found %d recording(s) in %.180s", Global_Analysis_File_Count, Global_Analysis_Record_Dir);

    return 1;

}

static int ANALYSIS_open_selected_recording(void){

    /*

    Purpose: Opens the selected recording for analysis

    Return: Open status

    */

    if (Global_Analysis_File_Count <= 0) return 0;

    Global_Analysis_Loading = 1;
    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Loading %.180s",
             Global_Analysis_Files[Global_Analysis_Selected]);

    char path[1024];

    snprintf(path, sizeof(path), "%s/%s", Global_Analysis_Record_Dir,
             Global_Analysis_Files[Global_Analysis_Selected]);

    FILE *fp = fopen(path, "rb");

    if (!fp) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                 "Failed to open %.180s", Global_Analysis_Files[Global_Analysis_Selected]);
        ANALYSIS_clear_loaded_file();
        Global_Analysis_Loading = 0;
        return 0;

    }

    if (fseek(fp, 0, SEEK_END) != 0) {

        fclose(fp);
        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Failed to seek recording");
        ANALYSIS_clear_loaded_file();
        Global_Analysis_Loading = 0;
        return 0;

    }

    long bytes = ftell(fp);
    fclose(fp);

    if (bytes <= 0) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status), "Recording is empty");
        ANALYSIS_clear_loaded_file();
        Global_Analysis_Loading = 0;
        return 0;

    }

    size_t count_i16 = (size_t)bytes / sizeof(int16_t);

    if (count_i16 % 2) count_i16--;

    snprintf(Global_Analysis_Path, sizeof(Global_Analysis_Path), "%s", path);

    Global_Analysis_Loaded_Index = Global_Analysis_Selected;
    Global_Analysis_IQ_Count = count_i16 / 2;
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

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Opened %.180s | %.6f sec",
             Global_Analysis_Files[Global_Analysis_Selected],
             Global_Analysis_Sample_Rate > 0.0 ?
             (double)Global_Analysis_IQ_Count / Global_Analysis_Sample_Rate : 0.0);

    Global_Analysis_Dirty = 1;

    return 1;

}

static void ANALYSIS_select_relative(int delta){

    /*

    Purpose: Moves the selected analysis file by a relative offset

    Return: No return

    */

    if (Global_Analysis_File_Count <= 0) return;

    Global_Analysis_Selected += delta;

    if (Global_Analysis_Selected < 0) {

        Global_Analysis_Selected = Global_Analysis_File_Count - 1;

    }

    if (Global_Analysis_Selected >= Global_Analysis_File_Count) {

        Global_Analysis_Selected = 0;

    }

    Global_Analysis_List_Scroll = Global_Analysis_Selected - 2;

    if (Global_Analysis_List_Scroll < 0) Global_Analysis_List_Scroll = 0;

    snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
             "Selected %.180s | Press Enter to open",
             Global_Analysis_Files[Global_Analysis_Selected]);

}

static void ANALYSIS_zoom_at_fraction(double frac, int zoom_in){

    /*

    Purpose: Zooms the analysis view around a fractional cursor position

    Return: No return

    */

    if (Global_Analysis_IQ_Count == 0 || Global_Analysis_Path[0] == '\0') return;

    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;

    size_t cursor_sample = Global_Analysis_View_Start +
                           (size_t)(frac * (double)Global_Analysis_View_Len);

    size_t min_len = ANALYSIS_FFT_SIZE * 8;

    if (min_len > Global_Analysis_IQ_Count) min_len = Global_Analysis_IQ_Count;

    if (zoom_in) {

        Global_Analysis_View_Len /= 2;

        if (Global_Analysis_View_Len < min_len) Global_Analysis_View_Len = min_len;

    }

    else {

        Global_Analysis_View_Len *= 2;

        if (Global_Analysis_View_Len > Global_Analysis_IQ_Count) {

            Global_Analysis_View_Len = Global_Analysis_IQ_Count;

        }

    }

    size_t anchor = (size_t)(frac * (double)Global_Analysis_View_Len);

    if (cursor_sample > anchor) Global_Analysis_View_Start = cursor_sample - anchor;
    else Global_Analysis_View_Start = 0;

    if (Global_Analysis_View_Start + Global_Analysis_View_Len > Global_Analysis_IQ_Count) {

        Global_Analysis_View_Start = Global_Analysis_IQ_Count > Global_Analysis_View_Len ?
                                     Global_Analysis_IQ_Count - Global_Analysis_View_Len : 0;

    }

    Global_Analysis_Dirty = 1;

}

static void ANALYSIS_drag_move_view(int dx, int graph_w){

    /*

    Purpose: Moves the analysis view according to horizontal mouse drag distance

    Return: No return

    */

    if (Global_Analysis_IQ_Count == 0 ||
        Global_Analysis_Path[0] == '\0' ||
        graph_w <= 0 ||
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

        Global_Analysis_View_Start = Global_Analysis_View_Start > step ?
                                     Global_Analysis_View_Start - step : 0;

    }

    else if (delta_samples > 0) {

        size_t step = (size_t)delta_samples;
        size_t max_start = Global_Analysis_IQ_Count > Global_Analysis_View_Len ?
                           Global_Analysis_IQ_Count - Global_Analysis_View_Len : 0;

        if (Global_Analysis_View_Start + step < max_start) {

            Global_Analysis_View_Start += step;

        }

        else {

            Global_Analysis_View_Start = max_start;

        }

    }

    Global_Analysis_Dirty = 1;

}


static void ANALYSIS_get_layout(int win_w,
                                int win_h,
                                SDL_Rect *list_rect,
                                SDL_Rect *spec_rect){

    /*

    Purpose: Computes analysis workstation rectangles used by input handling

    Return: No return

    */

    int selector_h = (int)((double)win_h * 0.22);

    if (selector_h < 130) selector_h = 130;

    int gap = 10;
    int title_h = 22;
    int col_gap = 12;
    int panel_h = selector_h - MARGIN;
    int work_w = win_w - 2 * MARGIN;
    int half_w = (work_w - col_gap) / 2;

    if (half_w < 60) half_w = work_w / 2;

    SDL_Rect local_list = {
        MARGIN,
        MARGIN,
        half_w,
        panel_h
    };

    int work_x = MARGIN;
    int work_y = local_list.y + local_list.h + MARGIN;

    int top_row_h = panel_h - title_h;
    int mid_row_h = top_row_h;

    if (top_row_h < 70) top_row_h = 70;
    if (mid_row_h < 70) mid_row_h = 70;

    int mag_y = work_y + title_h;
    int mid_title_y = mag_y + top_row_h + gap;
    int inst_y = mid_title_y + title_h;
    int spec_title_y = inst_y + mid_row_h + gap;

    SDL_Rect local_spec = {
        work_x,
        spec_title_y + title_h,
        work_w,
        win_h - (spec_title_y + title_h) - MARGIN
    };

    if (local_spec.h < 110) local_spec.h = 110;

    if (list_rect) *list_rect = local_list;
    if (spec_rect) *spec_rect = local_spec;

}


static void ANALYSIS_get_hover_graph_layout(int win_w,
                                            int win_h,
                                            SDL_Rect *psd_rect,
                                            SDL_Rect *mag_rect,
                                            SDL_Rect *phase_rect,
                                            SDL_Rect *inst_rect,
                                            SDL_Rect *const_rect,
                                            SDL_Rect *spec_rect){

    /*

    Purpose: Computes the visible analysis graph rectangles for hover-sync lines

    Return: No return

    */

    int selector_h = (int)((double)win_h * 0.22);

    if (selector_h < 130) selector_h = 130;

    int gap = 10;
    int title_h = 22;
    int col_gap = 12;
    int panel_h = selector_h - MARGIN;
    int work_x = MARGIN;
    int work_w = win_w - 2 * MARGIN;

    if (work_w < 100) return;

    int half_w = (work_w - col_gap) / 2;

    if (half_w < 60) half_w = work_w / 2;

    SDL_Rect list_rect = {
        MARGIN,
        MARGIN,
        half_w,
        panel_h
    };

    SDL_Rect local_psd = {
        MARGIN + half_w + col_gap,
        MARGIN + title_h,
        work_w - half_w - col_gap,
        panel_h - title_h
    };

    if (local_psd.h < 70) local_psd.h = 70;

    int work_y = list_rect.y + list_rect.h + MARGIN;
    int work_h = win_h - work_y - MARGIN;

    int top_row_h = local_psd.h;
    int mid_row_h = local_psd.h;
    int spec_h = work_h - top_row_h - mid_row_h - (gap * 2) - (title_h * 3);

    if (top_row_h < 70) top_row_h = 70;
    if (mid_row_h < 70) mid_row_h = 70;
    if (spec_h < 110) spec_h = 110;

    int top_title_y = work_y;

    SDL_Rect local_mag = {
        work_x,
        top_title_y + title_h,
        half_w,
        top_row_h
    };

    SDL_Rect local_phase = {
        work_x + half_w + col_gap,
        top_title_y + title_h,
        work_w - half_w - col_gap,
        top_row_h
    };

    int mid_title_y = local_mag.y + local_mag.h + gap;

    SDL_Rect local_inst = {
        work_x,
        mid_title_y + title_h,
        half_w,
        mid_row_h
    };

    SDL_Rect local_const = {
        work_x + half_w + col_gap,
        mid_title_y + title_h,
        work_w - half_w - col_gap,
        mid_row_h
    };

    int spec_title_y = local_inst.y + local_inst.h + gap;

    SDL_Rect local_spec = {
        work_x,
        spec_title_y + title_h,
        work_w,
        win_h - (spec_title_y + title_h) - MARGIN
    };

    if (local_spec.h < 110) local_spec.h = 110;

    if (psd_rect) *psd_rect = local_psd;
    if (mag_rect) *mag_rect = local_mag;
    if (phase_rect) *phase_rect = local_phase;
    if (inst_rect) *inst_rect = local_inst;
    if (const_rect) *const_rect = local_const;
    if (spec_rect) *spec_rect = local_spec;

}

static void ANALYSIS_draw_hover_sync_line(SDL_Renderer *renderer,
                                          TTF_Font *font,
                                          int win_w,
                                          int win_h){

    /*

    Purpose: Draws synchronized hover markers across related analysis views

    Return: No return

    */

    if (!renderer || Global_Analysis_Path[0] == '\0') return;

    SDL_Rect psd_rect;
    SDL_Rect mag_rect;
    SDL_Rect phase_rect;
    SDL_Rect inst_rect;
    SDL_Rect const_rect;
    SDL_Rect spec_rect;

    ANALYSIS_get_hover_graph_layout(win_w,
                                    win_h,
                                    &psd_rect,
                                    &mag_rect,
                                    &phase_rect,
                                    &inst_rect,
                                    &const_rect,
                                    &spec_rect);
    (void)const_rect;

    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    SDL_Rect time_rects[4] = {
        mag_rect,
        phase_rect,
        inst_rect,
        spec_rect
    };

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

            if (r.w <= 0 || r.h <= 0) continue;

            int line_x = r.x + (int)(time_frac * (double)r.w);

            if (line_x < r.x) line_x = r.x;
            if (line_x > r.x + r.w - 1) line_x = r.x + r.w - 1;

            SDL_RenderDrawLine(renderer, line_x, r.y, line_x, r.y + r.h);

        }

    }

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

    if (found_freq_hover) {

        freq_frac = ANALYSIS_limit_double(freq_frac, 0.0, 1.0);

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 210);

        if (spec_rect.w > 0 && spec_rect.h > 0) {

            int line_y = spec_rect.y + (int)(freq_frac * (double)spec_rect.h);

            if (line_y < spec_rect.y) line_y = spec_rect.y;
            if (line_y > spec_rect.y + spec_rect.h - 1) line_y = spec_rect.y + spec_rect.h - 1;

            SDL_RenderDrawLine(renderer, spec_rect.x, line_y, spec_rect.x + spec_rect.w, line_y);

        }

        if (psd_rect.w > 0 && psd_rect.h > 0) {

            int line_x = psd_rect.x + (int)((1.0 - freq_frac) * (double)psd_rect.w);

            if (line_x < psd_rect.x) line_x = psd_rect.x;
            if (line_x > psd_rect.x + psd_rect.w - 1) line_x = psd_rect.x + psd_rect.w - 1;

            SDL_RenderDrawLine(renderer, line_x, psd_rect.y, line_x, psd_rect.y + psd_rect.h);

            if (font) {

                double hover_freq_hz = Global_Analysis_Center_Hz +
                                       ((0.5 - freq_frac) * Global_Analysis_Sample_Rate);

                char freq_label[96];

                snprintf(freq_label,
                         sizeof(freq_label),
                         "%.6f MHz",
                         hover_freq_hz / 1e6);

                int text_w = 0;
                int text_h = 0;

                if (TTF_SizeText(font, freq_label, &text_w, &text_h) != 0) {

                    text_w = 0;
                    text_h = 0;

                }

                SDL_Rect label_bg = {
                    psd_rect.x + psd_rect.w - text_w - 192,//18
                    psd_rect.y - text_h - 16,//8
                    text_w + 14,
                    text_h + 6
                };

                if (label_bg.y < 0) label_bg.y = psd_rect.y + 4;
                if (label_bg.x < psd_rect.x + 4) label_bg.x = psd_rect.x + 4;

                draw_filled_rect(renderer, label_bg, (SDL_Color){0, 0, 0, 210});
                draw_outline_rect(renderer, label_bg, (SDL_Color){0, 170, 255, 220});
                draw_text(renderer,
                          font,
                          freq_label,
                          label_bg.x + 7,//7
                          label_bg.y + 3,
                          (SDL_Color){0, 200, 255, 255});

            }

        }

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

}

static double ANALYSIS_freq_frac_from_mouse_y(int mouse_y, SDL_Rect spec_rect){

    /*

    Purpose: Converts a spectrogram mouse Y coordinate into a normalized frequency fraction

    Return: Frequency fraction

    */

    if (spec_rect.h <= 0) return 0.0;

    double frac = (double)(mouse_y - spec_rect.y) / (double)spec_rect.h;

    return ANALYSIS_limit_double(frac, 0.0, 1.0);

}

static void ANALYSIS_update_filter_from_mouse(int mouse_y, SDL_Rect spec_rect){

    /*

    Purpose: Updates the analysis frequency filter selector from mouse movement

    Return: No return

    */

    Global_Analysis_Filter_Y1 = ANALYSIS_freq_frac_from_mouse_y(mouse_y, spec_rect);

}

static void ANALYSIS_apply_filter_selection(void){

    /*

    Purpose: Applies the analysis frequency filter selector and requests a replot

    Return: No return

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
    Global_Analysis_Dirty = 1;

    double top = Global_Analysis_Filter_Y0;
    double bottom = Global_Analysis_Filter_Y1;
    double center_y = (top + bottom) * 0.5;
    double bw_frac = fabs(bottom - top);
    double offset_hz = (0.5 - center_y) * Global_Analysis_Sample_Rate;
    double center_hz = Global_Analysis_Center_Hz + offset_hz;
    double bw_hz = bw_frac * Global_Analysis_Sample_Rate;

    snprintf(Global_Analysis_Status,
             sizeof(Global_Analysis_Status),
             "Filter %.6f MHz | BW %.3f kHz | Backspace clears",
             center_hz / 1e6,
             bw_hz / 1e3);

}

static void ANALYSIS_clear_filter(void){

    /*

    Purpose: Clears the analysis frequency filter selector and requests a replot

    Return: No return

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

    Global_Analysis_Dirty = 1;

    snprintf(Global_Analysis_Status,
             sizeof(Global_Analysis_Status),
             "Analysis frequency filter and marker cleared");

}


static double ANALYSIS_frequency_from_spec_frac(double frac){

    /*

    Purpose: Converts a greyscale spectrogram vertical fraction into RF frequency

    Return: Frequency in Hz

    */

    frac = ANALYSIS_limit_double(frac, 0.0, 1.0);

    double offset_hz = (0.5 - frac) * Global_Analysis_Sample_Rate;

    return Global_Analysis_Center_Hz + offset_hz;

}

static void ANALYSIS_get_filter_label(char *out, size_t out_size){

    /*

    Purpose: Builds the visible frequency label for the analysis frequency filter

    Return: No return

    */

    if (!out || out_size == 0) return;

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

    snprintf(out,
             out_size,
             "Filtered center %.6f MHz | BW %.3f kHz",
             center_hz / 1e6,
             bw_hz / 1e3);

}

static void ANALYSIS_set_marker_from_mouse(int mouse_x, SDL_Rect spec_rect){

    /*

    Purpose: Places the analysis time marker from a greyscale spectrogram click

    Return: No return

    */

    if (Global_Analysis_IQ_Count == 0 ||
        Global_Analysis_Path[0] == '\0' ||
        spec_rect.w <= 0 ||
        Global_Analysis_Sample_Rate <= 0.0) {

        return;

    }

    double frac = (double)(mouse_x - spec_rect.x) / (double)spec_rect.w;

    frac = ANALYSIS_limit_double(frac, 0.0, 1.0);

    size_t marker_sample = Global_Analysis_View_Start +
                           (size_t)(frac * (double)Global_Analysis_View_Len);

    if (marker_sample >= Global_Analysis_IQ_Count) {

        marker_sample = Global_Analysis_IQ_Count - 1;

    }

    Global_Analysis_Marker_Active = 1;
    Global_Analysis_Marker_Sample = marker_sample;
    Global_Analysis_Marker_Time = (double)marker_sample / Global_Analysis_Sample_Rate;

    snprintf(Global_Analysis_Status,
             sizeof(Global_Analysis_Status),
             "Marker %.6f sec | right-click greyscale spectrogram to move",
             Global_Analysis_Marker_Time);

}

static double ANALYSIS_time_frac_from_mouse_x(int mouse_x, SDL_Rect spec_rect){

    /*

    Purpose: Converts a spectrogram mouse X coordinate into a normalized time fraction

    Return: Time fraction

    */

    if (spec_rect.w <= 0) return 0.0;

    double frac = (double)(mouse_x - spec_rect.x) / (double)spec_rect.w;

    return ANALYSIS_limit_double(frac, 0.0, 1.0);

}

static void ANALYSIS_update_column_selection_from_mouse(int mouse_x, SDL_Rect spec_rect){

    /*

    Purpose: Updates the analysis time-column selector from mouse movement

    Return: No return

    */

    Global_Analysis_Column_X1 = ANALYSIS_time_frac_from_mouse_x(mouse_x, spec_rect);

}

static void ANALYSIS_apply_column_selection(void){

    /*

    Purpose: Applies the analysis time-column selector and requests a replot

    Return: No return

    */

    if (Global_Analysis_IQ_Count == 0 ||
        Global_Analysis_Path[0] == '\0' ||
        Global_Analysis_View_Len == 0) {

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
    Global_Analysis_Dirty = 1;

    double start_sec = Global_Analysis_Sample_Rate > 0.0 ?
                       (double)(Global_Analysis_View_Start +
                                (size_t)(Global_Analysis_Column_X0 * (double)Global_Analysis_View_Len)) /
                       Global_Analysis_Sample_Rate :
                       0.0;
    double end_sec = Global_Analysis_Sample_Rate > 0.0 ?
                     (double)(Global_Analysis_View_Start +
                              (size_t)(Global_Analysis_Column_X1 * (double)Global_Analysis_View_Len)) /
                     Global_Analysis_Sample_Rate :
                     0.0;

    snprintf(Global_Analysis_Status,
             sizeof(Global_Analysis_Status),
             "Time filter %.6f sec to %.6f sec | Backspace clears",
             start_sec,
             end_sec);

}


int ANALYSIS_export_classification_fields(char *file_name,
                                          size_t file_name_size,
                                          double *frequency_mhz,
                                          double *bandwidth_khz,
                                          double *start_time,
                                          double *end_time){

    /*

    Purpose: Exports the currently loaded analysis selection as classification fields

    Return: Export status

    */

    if (!file_name || file_name_size == 0 ||
        !frequency_mhz || !bandwidth_khz || !start_time || !end_time) {

        return 0;

    }

    file_name[0] = '\0';
    *frequency_mhz = 0.0;
    *bandwidth_khz = 0.0;
    *start_time = 0.0;
    *end_time = 0.0;

    if (Global_Analysis_IQ_Count == 0 ||
        Global_Analysis_View_Len == 0 ||
        Global_Analysis_Path[0] == '\0' ||
        Global_Analysis_Sample_Rate <= 0.0) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "Open a recording before exporting to classification");
        return 0;

    }

    const char *name = Global_Analysis_Path;

    for (const char *p = Global_Analysis_Path; *p; p++) {

        if (*p == '/' || *p == '\\') {

            name = p + 1;

        }

    }

    if (Global_Analysis_Loaded_Index >= 0 &&
        Global_Analysis_Loaded_Index < Global_Analysis_File_Count &&
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

    size_t start_sample = Global_Analysis_View_Start +
                          (size_t)(x0 * (double)Global_Analysis_View_Len);
    size_t end_sample = Global_Analysis_View_Start +
                        (size_t)(x1 * (double)Global_Analysis_View_Len);

    if (start_sample > Global_Analysis_IQ_Count) start_sample = Global_Analysis_IQ_Count;
    if (end_sample > Global_Analysis_IQ_Count) end_sample = Global_Analysis_IQ_Count;

    if (end_sample < start_sample) {

        size_t tmp = start_sample;
        start_sample = end_sample;
        end_sample = tmp;

    }

    *frequency_mhz = center_hz / 1e6;
    *bandwidth_khz = bw_hz / 1e3;
    *start_time = (double)start_sample / Global_Analysis_Sample_Rate;
    *end_time = (double)end_sample / Global_Analysis_Sample_Rate;

    snprintf(Global_Analysis_Status,
             sizeof(Global_Analysis_Status),
             "Exported selection to classification fields");

    return 1;

}

void ANALYSIS_draw_workstation_overlays(SDL_Renderer *renderer,
                                               TTF_Font *font,
                                               SDL_Texture *texture,
                                               int tex_w,
                                               int tex_h,
                                               int win_w,
                                               int win_h){

    /*

    Purpose: Draws analysis-only filter frequency and time marker overlays

    Return: No return

    */

    if (!renderer || !font || !Global_Analysis_Mode) return;

    SDL_Rect list_rect;
    SDL_Rect spec_rect;

    ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);
    (void)list_rect;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    char workspace_label[64];
    snprintf(workspace_label,
             sizeof(workspace_label),
             "Workspace %d/%d",
             Global_Analysis_Active_Workspace + 1,
             ANALYSIS_WORKSPACE_COUNT);

    SDL_Rect workspace_bg = {
        win_w - 150,
        8,
        130,
        24
    };

    if (workspace_bg.x < MARGIN) workspace_bg.x = MARGIN;

    draw_filled_rect(renderer, workspace_bg, (SDL_Color){0, 0, 0, 210});
    draw_outline_rect(renderer, workspace_bg, (SDL_Color){120, 120, 120, 220});
    draw_text(renderer,
              font,
              workspace_label,
              workspace_bg.x + 8,
              workspace_bg.y + 5,
              (SDL_Color){230, 230, 230, 255});

    if (texture && tex_w > 0 && tex_h > 0 && spec_rect.w > 0 && spec_rect.h > 0) {

        int clear_h = 42;

        if (clear_h > spec_rect.h) clear_h = spec_rect.h;

        SDL_Rect src_clear = {
            0,
            0,
            tex_w,
            (clear_h * tex_h) / spec_rect.h
        };

        SDL_Rect dst_clear = {
            spec_rect.x,
            spec_rect.y,
            spec_rect.w,
            clear_h
        };

        if (src_clear.h < 1) src_clear.h = 1;
        if (src_clear.h > tex_h) src_clear.h = tex_h;

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

        double time_start = Global_Analysis_Sample_Rate > 0.0 ?
                            (double)Global_Analysis_View_Start / Global_Analysis_Sample_Rate :
                            0.0;
        double time_end = Global_Analysis_Sample_Rate > 0.0 ?
                          (double)(Global_Analysis_View_Start + Global_Analysis_View_Len) /
                          Global_Analysis_Sample_Rate :
                          0.0;

        char status_label[256];

        snprintf(status_label,
                 sizeof(status_label),
                 "%.6f MHz | %.6f sec to %.6f sec | %.3f kS/s",
                 display_freq_hz / 1e6,
                 time_start,
                 time_end,
                 Global_Analysis_Sample_Rate / 1e3);

        SDL_Rect status_bg = {
            spec_rect.x + 500,
            spec_rect.y - 30,
            500,
            24
        };

        if (status_bg.w < 220) {
            status_bg.x = spec_rect.x + 4;
            status_bg.w = spec_rect.x - 8;
        }

        draw_filled_rect(renderer, status_bg, (SDL_Color){0, 0, 0, 210});
        draw_outline_rect(renderer, status_bg, (SDL_Color){90, 90, 90, 220});
        draw_text(renderer,
                  font,
                  status_label,
                  status_bg.x + 7,
                  status_bg.y + 5,
                  (SDL_Color){230, 230, 230, 255});

    }

    if ((Global_Analysis_Filter_Active || Global_Analysis_Filter_Selecting) &&
        Global_Analysis_Path[0] != '\0') {

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

        if (select_y0 < spec_rect.y) select_y0 = spec_rect.y;
        if (select_y1 > spec_rect.y + spec_rect.h) select_y1 = spec_rect.y + spec_rect.h;

        /*
         * Keep the frequency selector visible even when the selected band is
         * very small or dragged against the top/bottom edge of the greyscale
         * spectrogram. Hit-testing already worked; this only fixes rendering.
         */
        if (select_y1 <= select_y0) select_y1 = select_y0 + 1;
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

        SDL_Rect filter_rect = {
            spec_rect.x,
            select_y0,
            spec_rect.w,
            select_y1 - select_y0
        };

        draw_filled_rect(renderer, filter_rect, (SDL_Color){220, 220, 220, 50});
        draw_outline_rect(renderer, filter_rect, (SDL_Color){230, 230, 230, 180});

        SDL_SetRenderDrawColor(renderer, 230, 230, 230, 180);
        SDL_RenderDrawLine(renderer, spec_rect.x, select_y0, spec_rect.x + spec_rect.w, select_y0);
        SDL_RenderDrawLine(renderer, spec_rect.x, select_y1, spec_rect.x + spec_rect.w, select_y1);

        char filter_label[160];

        ANALYSIS_get_filter_label(filter_label, sizeof(filter_label));

        SDL_Rect label_bg = {
            spec_rect.x + 4,
            spec_rect.y - 30,
            430,
            24
        };

        draw_filled_rect(renderer, label_bg, (SDL_Color){0, 0, 0, 210});
        draw_outline_rect(renderer, label_bg, (SDL_Color){0, 220, 80, 220});
        draw_text(renderer,
                  font,
                  filter_label,
                  label_bg.x + 7,
                  label_bg.y + 5,
                  (SDL_Color){0, 255, 90, 255});

    }

    if (Global_Analysis_Column_Visible &&
        Global_Analysis_Path[0] != '\0') {

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

        if (select_x0 < spec_rect.x) select_x0 = spec_rect.x;
        if (select_x1 > spec_rect.x + spec_rect.w) select_x1 = spec_rect.x + spec_rect.w;
        if (select_x1 <= select_x0) select_x1 = select_x0 + 1;

        SDL_Rect column_rect = {
            select_x0,
            spec_rect.y,
            select_x1 - select_x0,
            spec_rect.h
        };

        draw_filled_rect(renderer, column_rect, (SDL_Color){255, 255, 0, 45});
        draw_outline_rect(renderer, column_rect, (SDL_Color){255, 255, 0, 230});

        double start_sec = Global_Analysis_Sample_Rate > 0.0 ?
                           (double)(Global_Analysis_View_Start +
                                    (size_t)(x0 * (double)Global_Analysis_View_Len)) / Global_Analysis_Sample_Rate :
                           0.0;
        double end_sec = Global_Analysis_Sample_Rate > 0.0 ?
                         (double)(Global_Analysis_View_Start +
                                  (size_t)(x1 * (double)Global_Analysis_View_Len)) / Global_Analysis_Sample_Rate :
                         0.0;

        char column_label[128];

        snprintf(column_label,
                 sizeof(column_label),
                 "%.6f s - %.6f s     (%.6f s)",
                 start_sec,
                 end_sec,
                 end_sec - start_sec);

        SDL_Rect column_bg = {
            spec_rect.x + spec_rect.w - 225 - 192,
            spec_rect.y - 30,
            310,//240
            24
        };

        if (column_bg.x < spec_rect.x) column_bg.x = spec_rect.x + 4;

        draw_filled_rect(renderer, column_bg, (SDL_Color){0, 0, 0, 210});
        draw_outline_rect(renderer, column_bg, (SDL_Color){255, 255, 0, 220});
        draw_text(renderer,
                  font,
                  column_label,
                  column_bg.x + 7,
                  column_bg.y + 5,
                  (SDL_Color){255, 255, 0, 255});

    }

    if (Global_Analysis_Marker_Active &&
        Global_Analysis_Path[0] != '\0' &&
        Global_Analysis_View_Len > 0 &&
        Global_Analysis_Marker_Sample >= Global_Analysis_View_Start &&
        Global_Analysis_Marker_Sample <= Global_Analysis_View_Start + Global_Analysis_View_Len) {

        double marker_frac = (double)(Global_Analysis_Marker_Sample - Global_Analysis_View_Start) /
                             (double)Global_Analysis_View_Len;

        int marker_x = spec_rect.x + (int)(marker_frac * (double)spec_rect.w);

        if (marker_x < spec_rect.x) marker_x = spec_rect.x;
        if (marker_x > spec_rect.x + spec_rect.w - 1) marker_x = spec_rect.x + spec_rect.w - 1;

        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 230);
        SDL_RenderDrawLine(renderer, marker_x, spec_rect.y, marker_x, spec_rect.y + spec_rect.h);

        char marker_label[96];

        snprintf(marker_label,
                 sizeof(marker_label),
                 "Marker %.6f s",
                 Global_Analysis_Marker_Time);

        SDL_Rect marker_bg = {
            marker_x + 6,
            spec_rect.y + 6,
            150,
            24
        };

        if (marker_bg.x + marker_bg.w > spec_rect.x + spec_rect.w - 4) {

            marker_bg.x = spec_rect.x + spec_rect.w - marker_bg.w - 4;

        }

        if (marker_bg.x < spec_rect.x + 4) marker_bg.x = spec_rect.x + 4;

        draw_filled_rect(renderer, marker_bg, (SDL_Color){0, 0, 0, 210});
        draw_outline_rect(renderer, marker_bg, (SDL_Color){255, 255, 0, 220});
        draw_text(renderer,
                  font,
                  marker_label,
                  marker_bg.x + 7,
                  marker_bg.y + 5,
                  (SDL_Color){255, 255, 0, 255});

    }

    ANALYSIS_draw_hover_sync_line(renderer, font, win_w, win_h);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

}

static double ANALYSIS_wrap_phase(double value){

    /*

    Purpose: Wraps phase into the -pi to pi range

    Return: Wrapped phase

    */

    while (value > M_PI) value -= 2.0 * M_PI;
    while (value < -M_PI) value += 2.0 * M_PI;

    return value;

}

static uint32_t ANALYSIS_gray(double v){

    /*

    Purpose: Maps a normalized value to a grayscale pixel color

    Return: RGB color

    */

    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;

    uint8_t c = (uint8_t)(v * 255.0);

    return rgb(c, c, c);

}

void ANALYSIS_render_workstation_data(uint32_t *pixels, int tex_w, int tex_h){

    /*

    Purpose: Renders magnitude, phase, and spectrogram analysis data from the loaded
             recording

    Return: No return

    */

    clear_waterfall(pixels, tex_w, tex_h);
    Global_Analysis_Render_W = 0;
    memset(Global_Analysis_Mag_Line, 0, sizeof(Global_Analysis_Mag_Line));
    memset(Global_Analysis_Phase_Line, 0, sizeof(Global_Analysis_Phase_Line));
    memset(Global_Analysis_InstFreq_Line, 0, sizeof(Global_Analysis_InstFreq_Line));
    memset(Global_Analysis_PSD_Line, 0, sizeof(Global_Analysis_PSD_Line));
    memset(Global_Analysis_Const_I, 0, sizeof(Global_Analysis_Const_I));
    memset(Global_Analysis_Const_Q, 0, sizeof(Global_Analysis_Const_Q));
    Global_Analysis_Const_Count = 0;

    if (Global_Analysis_IQ_Count < ANALYSIS_FFT_SIZE ||
        Global_Analysis_Path[0] == '\0' ||
        tex_w <= 0 || tex_h <= 0) {

        return;

    }

    int render_w = tex_w;
    if (render_w > ANALYSIS_MAX_RENDER_W) render_w = ANALYSIS_MAX_RENDER_W;
    Global_Analysis_Render_W = render_w;

    FILE *fp = fopen(Global_Analysis_Path, "rb");

    if (!fp) {

        snprintf(Global_Analysis_Status, sizeof(Global_Analysis_Status),
                 "Failed to reopen selected recording");
        return;

    }

    double window[ANALYSIS_FFT_SIZE];

    for (int i = 0; i < ANALYSIS_FFT_SIZE; i++) {

        window[i] = 0.5 - 0.5 * cos((2.0 * M_PI * (double)i) /
                                    (double)(ANALYSIS_FFT_SIZE - 1));

    }

    double *db_img = malloc(sizeof(double) * (size_t)render_w * ANALYSIS_FFT_SIZE);
    fftw_complex *in = fftw_malloc(sizeof(fftw_complex) * ANALYSIS_FFT_SIZE);
    fftw_complex *out = fftw_malloc(sizeof(fftw_complex) * ANALYSIS_FFT_SIZE);
    int16_t *block = malloc(sizeof(int16_t) * ANALYSIS_FFT_SIZE * 2);

    if (!db_img || !in || !out || !block) {

        free(db_img);
        free(block);
        if (in) fftw_free(in);
        if (out) fftw_free(out);
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

        if (time_col_low < 0) time_col_low = 0;
        if (time_col_high >= render_w) time_col_high = render_w - 1;
        if (time_col_high <= time_col_low) time_col_high = time_col_low + 1;
        if (time_col_high >= render_w) time_col_high = render_w - 1;

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

        if (filter_bin_low < 0) filter_bin_low = 0;
        if (filter_bin_high >= ANALYSIS_FFT_SIZE) filter_bin_high = ANALYSIS_FFT_SIZE - 1;
        if (filter_bin_high <= filter_bin_low) filter_bin_high = filter_bin_low + 1;

        filter_mag = calloc((size_t)render_w, sizeof(double));
        filter_re = calloc((size_t)render_w, sizeof(double));
        filter_im = calloc((size_t)render_w, sizeof(double));
        filter_td_phase = calloc((size_t)render_w, sizeof(double));
        filter_td_inst_freq = calloc((size_t)render_w, sizeof(double));

        if (!filter_mag || !filter_re || !filter_im ||
            !filter_td_phase || !filter_td_inst_freq) {
            filter_active = 0;
        }

    }

    for (int x = 0; x < render_w; x++) {

        double frac = (render_w > 1) ? (double)x / (double)(render_w - 1) : 0.0;
        size_t start = Global_Analysis_View_Start +
                       (size_t)(frac * (double)Global_Analysis_View_Len);

        if (start + ANALYSIS_FFT_SIZE >= Global_Analysis_IQ_Count) {

            start = Global_Analysis_IQ_Count > ANALYSIS_FFT_SIZE ?
                    Global_Analysis_IQ_Count - ANALYSIS_FFT_SIZE : 0;

        }

        if (fseek(fp, (long)(start * 2 * sizeof(int16_t)), SEEK_SET) != 0) {

            continue;

        }

        size_t got = fread(block, sizeof(int16_t), ANALYSIS_FFT_SIZE * 2, fp);

        if (got < ANALYSIS_FFT_SIZE * 2) {

            memset(block + got, 0, sizeof(int16_t) * ((ANALYSIS_FFT_SIZE * 2) - got));

        }

        double sum_mag = 0.0;

        for (int k = 0; k < ANALYSIS_FFT_SIZE; k++) {

            double I = (double)block[k * 2] / 32768.0;
            double Q = (double)block[k * 2 + 1] / 32768.0;

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

            if (cutoff_hz < bin_width_hz) cutoff_hz = bin_width_hz;
            if (cutoff_hz > Global_Analysis_Sample_Rate * 0.45) cutoff_hz = Global_Analysis_Sample_Rate * 0.45;

            double alpha = (2.0 * M_PI * cutoff_hz) /
                           (Global_Analysis_Sample_Rate + (2.0 * M_PI * cutoff_hz));
            double omega = 2.0 * M_PI * center_offset_hz / Global_Analysis_Sample_Rate;
            double phase_sum_i = 0.0;
            double phase_sum_q = 0.0;
            double inst_freq_sum = 0.0;
            int    inst_freq_count = 0;
            double lp_i = 0.0;
            double lp_q = 0.0;
            double prev_i = 0.0;
            double prev_q = 0.0;
            size_t samples_per_column = render_w > 0 ?
                                        Global_Analysis_View_Len / (size_t)render_w :
                                        (size_t)ANALYSIS_FFT_SIZE;
            int inst_sample_count = (int)samples_per_column;

            if (inst_sample_count < 8) inst_sample_count = 8;
            if (inst_sample_count > 512) inst_sample_count = 512;
            if (inst_sample_count > ANALYSIS_FFT_SIZE) inst_sample_count = ANALYSIS_FFT_SIZE;

            for (int k = 0; k < inst_sample_count; k++) {

                double I = (double)block[k * 2] / 32768.0;
                double Q = (double)block[k * 2 + 1] / 32768.0;
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

        if ((!time_filter_active || (x >= time_col_low && x <= time_col_high)) &&
            avg_mag > max_mag) max_mag = avg_mag;

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

            if (val > max_db) max_db = val;

        }

    }

    if (filter_active) {

        max_mag = 1e-12;
        max_phase_abs = 1e-12;
        max_inst_freq_abs = 1e-12;

        int bin_count = filter_bin_high - filter_bin_low + 1;
        if (bin_count < 1) bin_count = 1;

        for (int x = 0; x < render_w; x++) {

            double avg_mag = filter_mag[x] / (double)bin_count;

            Global_Analysis_Mag_Line[x] = (float)avg_mag;
            Global_Analysis_Phase_Line[x] = 0.0f;
            Global_Analysis_InstFreq_Line[x] = 0.0f;

            if ((!time_filter_active || (x >= time_col_low && x <= time_col_high)) &&
                avg_mag > max_mag) max_mag = avg_mag;

        }

        double phase_gate = max_mag * 0.15;

        if (phase_gate < 1e-9) {

            phase_gate = 1e-9;

        }

        for (int x = 0; x < render_w; x++) {

            double avg_mag = Global_Analysis_Mag_Line[x];

            if (avg_mag >= phase_gate &&
                (!time_filter_active || (x >= time_col_low && x <= time_col_high))) {

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

            while (seg_start < render_w && !valid_phase[seg_start]) seg_start++;
            if (seg_start >= render_w) break;

            int seg_end = seg_start;

            while (seg_end + 1 < render_w && valid_phase[seg_end + 1]) seg_end++;

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

                if (fabs(phase) > max_phase_abs) max_phase_abs = fabs(phase);
                if (fabs(inst_freq_hz) > max_inst_freq_abs) max_inst_freq_abs = fabs(inst_freq_hz);

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
     * Bins outside the selected frequency range are hidden instead of merely dimmed,
     * so the graph represents only the selected signal region.
     */
    double psd_min_db = 300.0;
    double psd_max_db = -300.0;
    int psd_valid[ANALYSIS_MAX_RENDER_W];

    memset(psd_valid, 0, sizeof(psd_valid));

    for (int x = 0; x < render_w; x++) {

        int bin = render_w > 1 ?
                  (int)(((double)x / (double)(render_w - 1)) * (double)(ANALYSIS_FFT_SIZE - 1)) :
                  ANALYSIS_FFT_SIZE / 2;

        if (bin < 0) bin = 0;
        if (bin >= ANALYSIS_FFT_SIZE) bin = ANALYSIS_FFT_SIZE - 1;

        if (filter_active && (bin < filter_bin_low || bin > filter_bin_high)) {

            Global_Analysis_PSD_Line[x] = 0.0f;
            continue;

        }

        double sum_db = 0.0;
        int count_db = 0;

        for (int t = 0; t < render_w; t++) {

            if (time_filter_active && (t < time_col_low || t > time_col_high)) continue;

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

        if (avg_db < psd_min_db) psd_min_db = avg_db;
        if (avg_db > psd_max_db) psd_max_db = avg_db;

    }

    double psd_range_db = psd_max_db - psd_min_db;

    if (psd_range_db < 1e-9) psd_range_db = 1.0;

    for (int x = 0; x < render_w; x++) {

        if (!psd_valid[x]) {

            Global_Analysis_PSD_Line[x] = 0.0f;
            continue;

        }

        Global_Analysis_PSD_Line[x] = (float)((Global_Analysis_PSD_Line[x] - psd_min_db) / psd_range_db);

        if (Global_Analysis_PSD_Line[x] < 0.0f) Global_Analysis_PSD_Line[x] = 0.0f;
        if (Global_Analysis_PSD_Line[x] > 1.0f) Global_Analysis_PSD_Line[x] = 1.0f;

    }

    for (int x = 0; x < render_w; x++) {

        Global_Analysis_Mag_Line[x] = (float)(Global_Analysis_Mag_Line[x] / max_mag);
        Global_Analysis_Phase_Line[x] = (float)(Global_Analysis_Phase_Line[x] / max_phase_abs);
        Global_Analysis_InstFreq_Line[x] = (float)(Global_Analysis_InstFreq_Line[x] / max_inst_freq_abs);

        if (Global_Analysis_Mag_Line[x] < 0.0f) Global_Analysis_Mag_Line[x] = 0.0f;
        if (Global_Analysis_Mag_Line[x] > 1.0f) Global_Analysis_Mag_Line[x] = 1.0f;
        if (Global_Analysis_Phase_Line[x] < -1.0f) Global_Analysis_Phase_Line[x] = -1.0f;
        if (Global_Analysis_Phase_Line[x] > 1.0f) Global_Analysis_Phase_Line[x] = 1.0f;
        if (Global_Analysis_InstFreq_Line[x] < -1.0f) Global_Analysis_InstFreq_Line[x] = -1.0f;
        if (Global_Analysis_InstFreq_Line[x] > 1.0f) Global_Analysis_InstFreq_Line[x] = 1.0f;

    }

    double min_db = max_db - 70.0;

    for (int x = 0; x < tex_w; x++) {

        int src_x = x;
        if (src_x >= render_w) src_x = render_w - 1;

        for (int py = 0; py < tex_h; py++) {

            int bin = (int)((1.0 - ((double)py / (double)tex_h)) *
                            (double)(ANALYSIS_FFT_SIZE - 1));
            double val = db_img[(size_t)src_x * ANALYSIS_FFT_SIZE + bin];
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

    if (filter_active && render_w > 0) {

        int const_count = render_w;
        if (const_count > ANALYSIS_MAX_CONST_POINTS) const_count = ANALYSIS_MAX_CONST_POINTS;

        Global_Analysis_Const_Count = const_count;

        double max_abs = 1e-12;

        for (int x = 0; x < render_w; x++) {
            if (time_filter_active && (x < time_col_low || x > time_col_high)) continue;
            double a = sqrt(filter_re[x] * filter_re[x] + filter_im[x] * filter_im[x]);
            if (a > max_abs) max_abs = a;
        }

        for (int p = 0; p < const_count; p++) {

            int x = 0;

            if (time_filter_active) {

                x = const_count > 1 ?
                    time_col_low + (int)(((double)p / (double)(const_count - 1)) *
                                         (double)(time_col_high - time_col_low)) :
                    time_col_low;

            }

            else {

                x = const_count > 1 ?
                    (int)(((double)p / (double)(const_count - 1)) * (double)(render_w - 1)) :
                    0;

            }

            Global_Analysis_Const_I[p] = (float)(filter_re[x] / max_abs);
            Global_Analysis_Const_Q[p] = (float)(filter_im[x] / max_abs);

        }

    }

    else if (Global_Analysis_IQ_Count > 0 && Global_Analysis_View_Len > 0) {

        int const_count = ANALYSIS_MAX_CONST_POINTS;

        size_t const_view_start = Global_Analysis_View_Start;
        size_t const_view_len = Global_Analysis_View_Len;

        if (time_filter_active) {

            size_t time_start = Global_Analysis_View_Start +
                                (size_t)((double)time_col_low / (double)(render_w - 1) *
                                         (double)Global_Analysis_View_Len);
            size_t time_end = Global_Analysis_View_Start +
                              (size_t)((double)time_col_high / (double)(render_w - 1) *
                                       (double)Global_Analysis_View_Len);

            if (time_start >= Global_Analysis_IQ_Count) time_start = Global_Analysis_IQ_Count - 1;
            if (time_end >= Global_Analysis_IQ_Count) time_end = Global_Analysis_IQ_Count - 1;
            if (time_end <= time_start) time_end = time_start + 1;
            if (time_end > Global_Analysis_IQ_Count) time_end = Global_Analysis_IQ_Count;

            const_view_start = time_start;
            const_view_len = time_end - time_start;

        }

        if ((size_t)const_count > const_view_len) {

            const_count = (int)const_view_len;

        }

        if (const_count < 0) const_count = 0;

        Global_Analysis_Const_Count = const_count;

        for (int p = 0; p < const_count; p++) {

            size_t sample_index = const_view_start;

            if (const_count > 1) {

                sample_index += (size_t)(((double)p / (double)(const_count - 1)) *
                                         (double)(const_view_len - 1));

            }

            if (sample_index >= Global_Analysis_IQ_Count) {

                sample_index = Global_Analysis_IQ_Count - 1;

            }

            int16_t iq_pair[2] = {0, 0};

            if (fseek(fp, (long)(sample_index * 2 * sizeof(int16_t)), SEEK_SET) == 0 &&
                fread(iq_pair, sizeof(int16_t), 2, fp) == 2) {

                Global_Analysis_Const_I[p] = (float)((double)iq_pair[0] / 32768.0);
                Global_Analysis_Const_Q[p] = (float)((double)iq_pair[1] / 32768.0);

            }

            else {

                Global_Analysis_Const_I[p] = 0.0f;
                Global_Analysis_Const_Q[p] = 0.0f;

            }

        }

    }

    fftw_destroy_plan(plan);
    free(db_img);
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

void ANALYSIS_enter_mode(const char *record_dir,
                         uint64_t fallback_center_hz,
                         uint32_t fallback_rec_out_rate_hz,
                         uint32_t fallback_sample_rate_hz){

    /*

    Purpose: Enters analysis mode and prepares the file list

    Return: No return

    */

    ANALYSIS_set_context(record_dir,
                         fallback_center_hz,
                         fallback_rec_out_rate_hz,
                         fallback_sample_rate_hz);

    Global_Analysis_Mode = 1;

    if (Global_Analysis_Workspaces_Initialized) {

        Global_Analysis_Dragging = 0;
        Global_Analysis_Filter_Selecting = 0;
        Global_Analysis_Column_Selecting = 0;
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
                     "Found %d recording(s). Select one and press Enter.",
                     Global_Analysis_File_Count);

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



int ANALYSIS_handle_event(SDL_Event *event,
                          int win_w,
                          int win_h,
                          uint32_t *pixels,
                          int tex_w,
                          int tex_h,
                          SDL_Texture *waterfall_texture,
                          uint64_t *next_waterfall_ms,
                          Type_Active_Fields *active){

    if (!event || !Global_Analysis_Mode) return ANALYSIS_EVENT_IGNORED;

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
                snprintf(Global_Analysis_Status,
                         sizeof(Global_Analysis_Status),
                         "Ctrl+drag on greyscale spectrogram to select a frequency band");
                return ANALYSIS_EVENT_HANDLED;

            }

            else if (key == SDLK_ESCAPE) {

                ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                if (next_waterfall_ms) *next_waterfall_ms = SDL_GetTicks64();
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

                    snprintf(Global_Analysis_Status,
                             sizeof(Global_Analysis_Status),
                             "Found %d recording(s). Select one and press Enter.",
                             Global_Analysis_File_Count);

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
                SDL_GetMouseState(NULL, &my);

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

        return ANALYSIS_EVENT_IGNORED;

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);

        SDL_Rect list_rect;
        SDL_Rect spec_rect;

        ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);

        if (point_in_rect(mx, my, list_rect)) {

            int row_h = 22;
            int visible = (list_rect.h - 82) / row_h;
            if (visible < 1) visible = 1;

            Global_Analysis_List_Scroll -= event->wheel.y * 3;

            if (Global_Analysis_List_Scroll < 0) Global_Analysis_List_Scroll = 0;

            if (Global_Analysis_List_Scroll + visible > Global_Analysis_File_Count) {

                Global_Analysis_List_Scroll = Global_Analysis_File_Count - visible;
                if (Global_Analysis_List_Scroll < 0) Global_Analysis_List_Scroll = 0;

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

        if (active) *active = FIELD_NONE;

        SDL_Rect list_rect;
        SDL_Rect spec_rect;

        ANALYSIS_get_layout(win_w, win_h, &list_rect, &spec_rect);

        if (point_in_rect(x, y, list_rect)) {

            int row_h = 22;
            int list_y = list_rect.y + 70;
            int visible = (list_rect.h - 82) / row_h;
            if (visible < 1) visible = 1;

            int first = Global_Analysis_List_Scroll;
            if (first < 0) first = 0;

            if (first + visible > Global_Analysis_File_Count) {

                first = Global_Analysis_File_Count - visible;
                if (first < 0) first = 0;

            }

            Global_Analysis_List_Scroll = first;

            int idx = first + ((y - list_y) / row_h);

            if (y >= list_y && idx >= 0 && idx < Global_Analysis_File_Count) {

                Global_Analysis_Selected = idx;

                if (event->button.clicks >= 2) ANALYSIS_open_selected_recording();
                else {

                    snprintf(Global_Analysis_Status,
                             sizeof(Global_Analysis_Status),
                             "Selected %.180s | Press Enter to open",
                             Global_Analysis_Files[Global_Analysis_Selected]);

                }

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

        else if (Global_Analysis_Path[0] != '\0' && point_in_rect(x, y, spec_rect) &&
                 (SDL_GetModState() & KMOD_CTRL)) {

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

        if (Global_Analysis_Column_Selecting) {

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
        Global_Analysis_Dragging = 0;

        return ANALYSIS_EVENT_HANDLED;

    }

    if (event->type == SDL_MOUSEMOTION) {

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
