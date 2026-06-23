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
#define ANALYSIS_SIGNAL_FIELD_COUNT 7
#define ANALYSIS_SIGNAL_TEXT_MAX 512
#define ANALYSIS_SIGNAL_FIELD_NONE -1
#define ANALYSIS_SIGNAL_DECIMATION_FIELD 5
#define ANALYSIS_SIGNAL_FILENAME_FIELD 6

#ifndef RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#endif

static void ANALYSIS_get_adjusted_mouse_state(int *x, int *y){
    SDL_GetMouseState(x, y);
    if (y) *y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;
}


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

static int              Global_Analysis_Signal_Menu_Open = 0;
static int              Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
static int              Global_Analysis_Signal_File_Manual_Edit = 0;
static int              Global_Analysis_Signal_File_Cursor = 0;
static int              Global_Analysis_Signal_File_Selecting = 0;
static int              Global_Analysis_Signal_File_Selection_Start = -1;
static int              Global_Analysis_Signal_File_Selection_End = -1;
static TTF_Font        *Global_Analysis_Signal_Last_Font = NULL;
static char             Global_Analysis_Signal_Menu_File[512] = "";
static char             Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FIELD_COUNT][ANALYSIS_SIGNAL_TEXT_MAX] = {
    "", "", "", "", "", "", ""
};
static SDL_Rect         Global_Analysis_Signal_Icon_Rect = {0, 8, 34, 34};
static int              Global_Analysis_Signal_Icon_Rect_Valid = 0;
static SDL_Rect         Global_Analysis_Signal_Trash_Rect = {0, 8, 34, 34};
static int              Global_Analysis_Signal_Trash_Rect_Valid = 0;
static double           Global_Analysis_Signal_Icon_Freq_Frac = 0.0;
static int              Global_Analysis_Delete_Confirm_Open = 0;
static char             Global_Analysis_Delete_Confirm_File[512] = "";
static char             Global_Analysis_Delete_Confirm_Path[1024] = "";

static const char *ANALYSIS_SIGNAL_FIELD_LABELS[ANALYSIS_SIGNAL_FIELD_COUNT] = {
    "Center Frequency MHz",
    "Bandwidth kHz",
    "Sample Rate kS/s",
    "Start Time sec",
    "End Time sec",
    "Decimation",
    "File Name"
};


typedef struct Type_Analysis_Workspace_State {

    double  column_x0;
    double  column_x1;
    double  sample_rate;
    double  center_hz;
    double  filter_y0;
    double  filter_y1;
    double  marker_time;
    size_t  iq_count;
    size_t  view_start;
    size_t  view_len;
    size_t  marker_sample;
    float   mag_line[ANALYSIS_MAX_RENDER_W];
    float   phase_line[ANALYSIS_MAX_RENDER_W];
    float   inst_freq_line[ANALYSIS_MAX_RENDER_W];
    float   psd_line[ANALYSIS_MAX_RENDER_W];
    float   const_i[ANALYSIS_MAX_CONST_POINTS];
    float   const_q[ANALYSIS_MAX_CONST_POINTS];
    int     dirty;
    int     file_count;
    int     selected;
    int     list_scroll;
    int     dragging;
    int     drag_last_x;
    int     loading;
    int     load_frame;
    int     loaded_index;
    int     render_w;
    int     const_count;
    int     filter_visible;
    int     filter_selecting;
    int     filter_active;
    int     marker_active;
    int     column_selecting;
    int     column_visible;
    int     column_active;
    int     signal_menu_open;
    int     signal_active_field;
    int     signal_file_manual_edit;
    int     signal_file_cursor;
    int     signal_file_selecting;
    int     signal_file_selection_start;
    int     signal_file_selection_end;
    char    signal_menu_file[512];
    char    signal_field_text[ANALYSIS_SIGNAL_FIELD_COUNT][ANALYSIS_SIGNAL_TEXT_MAX];
    char    status[256];
    char    path[1024];
    char    files[ANALYSIS_MAX_FILES][512];

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
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

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

    int icon_hover_keeps_frequency_label =
        (Global_Analysis_Signal_Icon_Rect_Valid &&
         point_in_rect(mouse_x, mouse_y, Global_Analysis_Signal_Icon_Rect)) ||
        (Global_Analysis_Signal_Trash_Rect_Valid &&
         point_in_rect(mouse_x, mouse_y, Global_Analysis_Signal_Trash_Rect));

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

                int label_w = 136;

                if (label_w < text_w + 14) label_w = text_w + 14;

                SDL_Rect label_bg = {
                    psd_rect.x + psd_rect.w - label_w - 192,
                    psd_rect.y - text_h - 16,
                    label_w,
                    text_h + 6
                };

                if (label_bg.y < 0) label_bg.y = psd_rect.y + 4;
                if (label_bg.x < psd_rect.x + 54) label_bg.x = psd_rect.x + 54;

                Global_Analysis_Signal_Icon_Freq_Frac = freq_frac;
                Global_Analysis_Signal_Icon_Rect_Valid = 1;
                Global_Analysis_Signal_Icon_Rect.w = 34;
                Global_Analysis_Signal_Icon_Rect.h = 34;
                Global_Analysis_Signal_Icon_Rect.x = label_bg.x - Global_Analysis_Signal_Icon_Rect.w - 12;

                Global_Analysis_Signal_Trash_Rect_Valid = 1;
                Global_Analysis_Signal_Trash_Rect.w = 34;
                Global_Analysis_Signal_Trash_Rect.h = 34;
                Global_Analysis_Signal_Trash_Rect.x = Global_Analysis_Signal_Icon_Rect.x -
                                                      Global_Analysis_Signal_Trash_Rect.w - 8;

                if (Global_Analysis_Signal_Trash_Rect.x < psd_rect.x + 4) {

                    Global_Analysis_Signal_Trash_Rect.x = psd_rect.x + 4;
                    Global_Analysis_Signal_Icon_Rect.x = Global_Analysis_Signal_Trash_Rect.x +
                                                        Global_Analysis_Signal_Trash_Rect.w + 8;
                    label_bg.x = Global_Analysis_Signal_Icon_Rect.x +
                                 Global_Analysis_Signal_Icon_Rect.w + 12;

                }

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

    else {

        Global_Analysis_Signal_Icon_Rect_Valid = 0;
        Global_Analysis_Signal_Trash_Rect_Valid = 0;

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


static int ANALYSIS_signal_menu_available(void){

    return Global_Analysis_File_Count > 0 &&
           Global_Analysis_Selected >= 0 &&
           Global_Analysis_Selected < Global_Analysis_File_Count;

}

static const char *ANALYSIS_selected_file_name(void){

    if (!ANALYSIS_signal_menu_available()) return "";

    return Global_Analysis_Files[Global_Analysis_Selected];

}

static void ANALYSIS_short_text(TTF_Font *font,
                                const char *src,
                                char *dst,
                                size_t dst_size,
                                int max_px){

    if (!dst || dst_size == 0) return;
    if (!src) src = "";

    size_t copy_len = strlen(src);

    if (copy_len >= dst_size) {

        copy_len = dst_size - 1;

    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

    if (!font || max_px <= 0) return;

    int text_w = 0;
    int text_h = 0;

    if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) return;

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

        if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) return;

        if (len >= 3) dst[len - 3] = '\0';

    }

    snprintf(dst, dst_size, "...");

}


static int ANALYSIS_draw_wrapped_text(SDL_Renderer *renderer,
                                      TTF_Font *font,
                                      const char *text,
                                      int x,
                                      int y,
                                      int max_px,
                                      int line_h,
                                      SDL_Color color){

    if (!renderer || !font || !text || max_px <= 0 || line_h <= 0) return 0;

    int len = (int)strlen(text);
    int pos = 0;
    int lines = 0;

    while (pos < len) {

        while (pos < len && text[pos] == ' ') pos++;
        if (pos >= len) break;

        int best = 1;
        int best_break = -1;

        for (int n = 1; pos + n <= len && n < 1000; n++) {

            char tmp[1024];

            if (n >= (int)sizeof(tmp)) break;

            memcpy(tmp, text + pos, (size_t)n);
            tmp[n] = '\0';

            int text_w = 0;
            int text_h = 0;

            if (TTF_SizeText(font, tmp, &text_w, &text_h) != 0) break;
            if (text_w > max_px) break;

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

        if (best >= (int)sizeof(line)) best = (int)sizeof(line) - 1;

        memcpy(line, text + pos, (size_t)best);
        line[best] = '\0';

        draw_text(renderer, font, line, x, y + (lines * line_h), color);

        pos += best;
        lines++;

    }

    return lines;

}

static void ANALYSIS_signal_refresh_filename_if_auto(void);
static void ANALYSIS_draw_centered_button_text(SDL_Renderer *renderer,
                                               TTF_Font *font,
                                               SDL_Rect rect,
                                               const char *text,
                                               SDL_Color color);

static void ANALYSIS_signal_clamp_file_cursor(void){

    int len = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);

    if (Global_Analysis_Signal_File_Cursor < 0) {

        Global_Analysis_Signal_File_Cursor = 0;

    }

    if (Global_Analysis_Signal_File_Cursor > len) {

        Global_Analysis_Signal_File_Cursor = len;

    }

}

static void ANALYSIS_signal_clear_file_selection(void){

    Global_Analysis_Signal_File_Selecting = 0;
    Global_Analysis_Signal_File_Selection_Start = -1;
    Global_Analysis_Signal_File_Selection_End = -1;

}

static int ANALYSIS_signal_file_has_selection(void){

    return Global_Analysis_Signal_File_Selection_Start >= 0 &&
           Global_Analysis_Signal_File_Selection_End >= 0 &&
           Global_Analysis_Signal_File_Selection_Start != Global_Analysis_Signal_File_Selection_End;

}

static void ANALYSIS_signal_get_file_selection_range(int *start, int *end){

    int a = Global_Analysis_Signal_File_Selection_Start;
    int b = Global_Analysis_Signal_File_Selection_End;
    int len = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);

    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (a > len) a = len;
    if (b > len) b = len;

    if (b < a) {

        int tmp = a;
        a = b;
        b = tmp;

    }

    if (start) *start = a;
    if (end) *end = b;

}

static int ANALYSIS_signal_delete_file_selection(void){

    if (!ANALYSIS_signal_file_has_selection()) return 0;

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

static int ANALYSIS_signal_text_width_range(TTF_Font *font, const char *text, int start, int end){

    if (!text || end <= start) return 0;

    int len = (int)strlen(text);

    if (start < 0) start = 0;
    if (end < start) end = start;
    if (end > len) end = len;

    int count = end - start;

    if (count <= 0) return 0;

    char tmp[1024];

    if (count >= (int)sizeof(tmp)) count = (int)sizeof(tmp) - 1;

    memcpy(tmp, text + start, (size_t)count);
    tmp[count] = '\0';

    if (font) {

        int text_w = 0;
        int text_h = 0;

        if (TTF_SizeText(font, tmp, &text_w, &text_h) == 0) return text_w;

    }

    return count * 8;

}

#define ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES 16

static int ANALYSIS_signal_filename_wrap_lines(TTF_Font *font,
                                               const char *text,
                                               int max_px,
                                               int starts[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES],
                                               int ends[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES]){

    if (!starts || !ends) return 0;

    if (!text) text = "";

    int len = (int)strlen(text);
    int pos = 0;
    int lines = 0;

    if (max_px < 8) max_px = 8;

    while (pos < len && lines < ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES) {

        int best = 1;
        int best_break = -1;

        for (int n = 1; pos + n <= len && n < 1000; n++) {

            char tmp[1024];

            if (n >= (int)sizeof(tmp)) break;

            memcpy(tmp, text + pos, (size_t)n);
            tmp[n] = '\0';

            int text_w = 0;
            int text_h = 0;

            if (font) {

                if (TTF_SizeText(font, tmp, &text_w, &text_h) != 0) break;

            }

            else {

                text_w = n * 8;

            }

            if (text_w > max_px) break;

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

static void ANALYSIS_signal_insert_file_cursor_text(const char *src){

    if (!src || src[0] == '\0') return;

    char *dst = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];

    if (ANALYSIS_signal_file_has_selection()) {

        ANALYSIS_signal_delete_file_selection();

    }

    size_t len = strlen(dst);
    size_t add = strlen(src);

    ANALYSIS_signal_clamp_file_cursor();

    if (len >= ANALYSIS_SIGNAL_TEXT_MAX - 1) return;

    if (add > (ANALYSIS_SIGNAL_TEXT_MAX - 1) - len) {

        add = (ANALYSIS_SIGNAL_TEXT_MAX - 1) - len;

    }

    memmove(dst + Global_Analysis_Signal_File_Cursor + (int)add,
            dst + Global_Analysis_Signal_File_Cursor,
            len - (size_t)Global_Analysis_Signal_File_Cursor + 1);

    memcpy(dst + Global_Analysis_Signal_File_Cursor, src, add);
    Global_Analysis_Signal_File_Cursor += (int)add;
    Global_Analysis_Signal_File_Manual_Edit = 1;
    ANALYSIS_signal_clear_file_selection();

}

static void ANALYSIS_signal_backspace_file_cursor_text(void){

    char *dst = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];

    ANALYSIS_signal_clamp_file_cursor();

    if (ANALYSIS_signal_delete_file_selection()) return;

    if (Global_Analysis_Signal_File_Cursor <= 0) return;

    size_t len = strlen(dst);

    memmove(dst + Global_Analysis_Signal_File_Cursor - 1,
            dst + Global_Analysis_Signal_File_Cursor,
            len - (size_t)Global_Analysis_Signal_File_Cursor + 1);

    Global_Analysis_Signal_File_Cursor--;
    Global_Analysis_Signal_File_Manual_Edit = 1;
    ANALYSIS_signal_clear_file_selection();

}

static void ANALYSIS_signal_delete_file_cursor_text(void){

    char *dst = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];

    ANALYSIS_signal_clamp_file_cursor();

    if (ANALYSIS_signal_delete_file_selection()) return;

    size_t len = strlen(dst);

    if (Global_Analysis_Signal_File_Cursor >= (int)len) return;

    memmove(dst + Global_Analysis_Signal_File_Cursor,
            dst + Global_Analysis_Signal_File_Cursor + 1,
            len - (size_t)Global_Analysis_Signal_File_Cursor);

    Global_Analysis_Signal_File_Manual_Edit = 1;
    ANALYSIS_signal_clear_file_selection();

}

static int ANALYSIS_signal_set_file_cursor_from_mouse(TTF_Font *font, SDL_Rect rect, int mouse_x, int mouse_y){

    const char *text = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];
    int starts[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES];
    int ends[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES];
    int text_x = rect.x + 8;
    int text_y = rect.y + 8;
    int line_h = 18;
    int max_px = rect.w - 16;
    int lines = ANALYSIS_signal_filename_wrap_lines(font, text, max_px, starts, ends);
    int line = (mouse_y - text_y) / line_h;

    if (line < 0) line = 0;
    if (line >= lines) line = lines - 1;

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

        if (i >= (int)sizeof(left) - 1) break;
        if (i + 1 >= (int)sizeof(right)) break;

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

static void ANALYSIS_signal_draw_filename_field_text(SDL_Renderer *renderer,
                                                     TTF_Font *font,
                                                     SDL_Rect rect,
                                                     int active){

    const char *src = Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD];
    SDL_Color text_color = src[0] != '\0' || active ?
                           (SDL_Color){0, 255, 90, 255} :
                           (SDL_Color){95, 130, 95, 255};
    int starts[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES];
    int ends[ANALYSIS_SIGNAL_FILENAME_WRAP_MAX_LINES];
    int line_h = 18;
    int max_lines = (rect.h - 12) / line_h;
    int lines = 0;

    if (max_lines < 1) max_lines = 1;

    if (src[0] == '\0' && !active) {

        draw_text(renderer,
                  font,
                  "Click to type",
                  rect.x + 8,
                  rect.y + 8,
                  text_color);
        return;

    }

    lines = ANALYSIS_signal_filename_wrap_lines(font,
                                                src,
                                                rect.w - 16,
                                                starts,
                                                ends);

    if (lines > max_lines) lines = max_lines;

    if (active && ANALYSIS_signal_file_has_selection()) {

        int sel_start = 0;
        int sel_end = 0;

        ANALYSIS_signal_get_file_selection_range(&sel_start, &sel_end);

        for (int i = 0; i < lines; i++) {

            int line_start = starts[i];
            int line_end = ends[i];
            int draw_start = sel_start > line_start ? sel_start : line_start;
            int draw_end = sel_end < line_end ? sel_end : line_end;

            if (draw_end <= draw_start) continue;

            int x0 = rect.x + 8 + ANALYSIS_signal_text_width_range(font, src, line_start, draw_start);
            int x1 = rect.x + 8 + ANALYSIS_signal_text_width_range(font, src, line_start, draw_end);

            if (x1 <= x0) x1 = x0 + 2;

            SDL_Rect selection_rect = {
                x0,
                rect.y + 7 + (i * line_h),
                x1 - x0,
                line_h
            };

            draw_filled_rect(renderer, selection_rect, (SDL_Color){0, 90, 255, 120});

        }

    }

    for (int i = 0; i < lines; i++) {

        char line[1024];
        int count = ends[i] - starts[i];

        if (count < 0) count = 0;
        if (count >= (int)sizeof(line)) count = (int)sizeof(line) - 1;

        memcpy(line, src + starts[i], (size_t)count);
        line[count] = '\0';

        draw_text(renderer,
                  font,
                  line,
                  rect.x + 8,
                  rect.y + 8 + (i * line_h),
                  text_color);

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

        if (cursor_line < 0) cursor_line = 0;
        if (cursor_line >= lines) cursor_line = lines - 1;

        if (lines > 0) {

            int line_start = starts[cursor_line];
            int line_end = ends[cursor_line];

            if (cursor < line_start) cursor = line_start;
            if (cursor > line_end) cursor = line_end;

            if (cursor > line_start) {

                char before[1024];
                int before_len = cursor - line_start;

                if (before_len >= (int)sizeof(before)) before_len = (int)sizeof(before) - 1;

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
        SDL_RenderDrawLine(renderer,
                           cursor_x,
                           cursor_y,
                           cursor_x,
                           cursor_y + line_h - 2);
        SDL_RenderDrawLine(renderer,
                           cursor_x + 1,
                           cursor_y,
                           cursor_x + 1,
                           cursor_y + line_h - 2);

    }

}

static void ANALYSIS_signal_append_text(const char *src){

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

    if (used >= ANALYSIS_SIGNAL_TEXT_MAX - 1) return;

    strncat(dst, src, ANALYSIS_SIGNAL_TEXT_MAX - used - 1);

    ANALYSIS_signal_refresh_filename_if_auto();

}

static void ANALYSIS_signal_backspace_text(void){

    if (Global_Analysis_Signal_Active_Field < 0 ||
        Global_Analysis_Signal_Active_Field >= ANALYSIS_SIGNAL_FIELD_COUNT) {

        return;

    }

    if (Global_Analysis_Signal_Active_Field == ANALYSIS_SIGNAL_FILENAME_FIELD) {

        ANALYSIS_signal_backspace_file_cursor_text();
        return;

    }

    char *dst = Global_Analysis_Signal_Field_Text[Global_Analysis_Signal_Active_Field];
    size_t len = strlen(dst);

    if (len > 0) dst[len - 1] = '\0';

    ANALYSIS_signal_refresh_filename_if_auto();

}

static void ANALYSIS_signal_clear_active_text(void){

    if (Global_Analysis_Signal_Active_Field < 0 ||
        Global_Analysis_Signal_Active_Field >= ANALYSIS_SIGNAL_FIELD_COUNT) {

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

static void ANALYSIS_get_signal_icon_rect(int win_w, int win_h, SDL_Rect *out){

    (void)win_h;

    if (!out) return;

    if (Global_Analysis_Signal_Icon_Rect_Valid) {

        *out = Global_Analysis_Signal_Icon_Rect;
        return;

    }

    *out = (SDL_Rect){win_w - 394, 8, 34, 34};

    if (out->x < MARGIN) out->x = MARGIN;

    Global_Analysis_Signal_Icon_Rect = *out;

}

static void ANALYSIS_get_signal_trash_rect(int win_w, int win_h, SDL_Rect *out){

    if (!out) return;

    if (Global_Analysis_Signal_Trash_Rect_Valid) {

        *out = Global_Analysis_Signal_Trash_Rect;
        return;

    }

    SDL_Rect icon_rect;
    ANALYSIS_get_signal_icon_rect(win_w, win_h, &icon_rect);

    *out = (SDL_Rect){
        icon_rect.x - icon_rect.w - 8,
        icon_rect.y,
        icon_rect.w,
        icon_rect.h
    };

    if (out->x < MARGIN) {

        out->x = icon_rect.x + icon_rect.w + 8;

    }

    Global_Analysis_Signal_Trash_Rect = *out;

}

static void ANALYSIS_get_signal_menu_rects(int win_w,
                                           int win_h,
                                           SDL_Rect *panel_rect,
                                           SDL_Rect field_rects[ANALYSIS_SIGNAL_FIELD_COUNT],
                                           SDL_Rect *save_rect,
                                           SDL_Rect *close_rect){

    int panel_w = 720;
    int panel_h = 585;

    if (panel_w > win_w - 60) panel_w = win_w - 60;
    if (panel_h > win_h - 60) panel_h = win_h - 60;

    if (panel_w < 560) panel_w = 560;
    if (panel_h < 520) panel_h = 520;

    SDL_Rect panel = {
        (win_w - panel_w) / 2,
        (win_h - panel_h) / 2,
        panel_w,
        panel_h
    };

    if (panel_rect) *panel_rect = panel;

    int left_x = panel.x + 28;
    int right_x = panel.x + (panel.w / 2) + 10;
    int top_y = panel.y + 232;
    int box_w = (panel.w - 66) / 2;
    int box_h = 38;
    int row_gap = 64;

    if (field_rects) {

        field_rects[0] = (SDL_Rect){left_x,  top_y,               box_w, box_h};
        field_rects[1] = (SDL_Rect){right_x, top_y,               box_w, box_h};
        field_rects[2] = (SDL_Rect){left_x,  top_y + row_gap,     box_w, box_h};
        field_rects[ANALYSIS_SIGNAL_DECIMATION_FIELD] = (SDL_Rect){right_x, top_y + row_gap, box_w, box_h};
        field_rects[3] = (SDL_Rect){left_x,  top_y + row_gap * 2, box_w - 82, box_h};
        field_rects[4] = (SDL_Rect){right_x, top_y + row_gap * 2, box_w - 82, box_h};
        field_rects[ANALYSIS_SIGNAL_FILENAME_FIELD] = (SDL_Rect){left_x,  top_y + row_gap * 3, panel.w - 56, 94};

    }

    if (save_rect) {

        *save_rect = (SDL_Rect){panel.x + panel.w - 252, panel.y + panel.h - 58, 116, 36};

    }

    if (close_rect) {

        *close_rect = (SDL_Rect){panel.x + panel.w - 120, panel.y + panel.h - 58, 92, 36};

    }

}

static void ANALYSIS_get_signal_marker_rects(SDL_Rect field_rects[ANALYSIS_SIGNAL_FIELD_COUNT],
                                             SDL_Rect *start_marker_rect,
                                             SDL_Rect *end_marker_rect){

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

static void ANALYSIS_signal_set_time_field_from_marker(int field_index){

    if (!Global_Analysis_Marker_Active ||
        field_index < 0 ||
        field_index >= ANALYSIS_SIGNAL_FIELD_COUNT) {

        return;

    }

    snprintf(Global_Analysis_Signal_Field_Text[field_index],
             ANALYSIS_SIGNAL_TEXT_MAX,
             "%.6f",
             Global_Analysis_Marker_Time);

    Global_Analysis_Signal_Active_Field = field_index;
    ANALYSIS_signal_clear_file_selection();
    ANALYSIS_signal_refresh_filename_if_auto();

}

static void ANALYSIS_draw_signal_marker_button(SDL_Renderer *renderer,
                                               TTF_Font *font,
                                               SDL_Rect rect,
                                               int enabled,
                                               int hover){

    if (!renderer || !font) return;

    if (enabled && hover) {

        SDL_Rect glow = {rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 38});
        draw_outline_rect(renderer, glow, (SDL_Color){0, 255, 90, 170});

    }

    draw_filled_rect(renderer,
                     rect,
                     !enabled ?
                     (SDL_Color){24, 24, 24, 255} :
                     hover ?
                     (SDL_Color){0, 55, 20, 255} :
                     (SDL_Color){0, 30, 12, 255});

    draw_outline_rect(renderer,
                      rect,
                      !enabled ?
                      (SDL_Color){82, 82, 82, 255} :
                      hover ?
                      (SDL_Color){0, 255, 90, 255} :
                      (SDL_Color){0, 150, 55, 255});

    ANALYSIS_draw_centered_button_text(renderer,
                                       font,
                                       rect,
                                       "Marker",
                                       !enabled ?
                                       (SDL_Color){110, 110, 110, 255} :
                                       (SDL_Color){0, 255, 90, 255});

}

static void ANALYSIS_signal_menu_prefill(void){

    const char *name = ANALYSIS_selected_file_name();

    if (!name || name[0] == '\0') return;

    if (strcmp(Global_Analysis_Signal_Menu_File, name) == 0) return;

    snprintf(Global_Analysis_Signal_Menu_File,
             sizeof(Global_Analysis_Signal_Menu_File),
             "%s",
             name);

    memset(Global_Analysis_Signal_Field_Text, 0, sizeof(Global_Analysis_Signal_Field_Text));

    if (Global_Analysis_Path[0] != '\0' && Global_Analysis_Sample_Rate > 0.0) {

        double center_hz = Global_Analysis_Center_Hz;
        double bw_hz = Global_Analysis_Sample_Rate;
        double sample_rate_hz = Global_Analysis_Sample_Rate;
        double start_sec = 0.0;
        double end_sec = Global_Analysis_IQ_Count > 0 ?
                         (double)Global_Analysis_IQ_Count / Global_Analysis_Sample_Rate :
                         0.0;

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

            start_sec = (double)(Global_Analysis_View_Start +
                                 (size_t)(x0 * (double)Global_Analysis_View_Len)) /
                        Global_Analysis_Sample_Rate;
            end_sec = (double)(Global_Analysis_View_Start +
                               (size_t)(x1 * (double)Global_Analysis_View_Len)) /
                      Global_Analysis_Sample_Rate;

        }

        snprintf(Global_Analysis_Signal_Field_Text[0],
                 ANALYSIS_SIGNAL_TEXT_MAX,
                 "%.6f",
                 center_hz / 1e6);
        snprintf(Global_Analysis_Signal_Field_Text[1],
                 ANALYSIS_SIGNAL_TEXT_MAX,
                 "%.3f",
                 bw_hz / 1e3);
        snprintf(Global_Analysis_Signal_Field_Text[2],
                 ANALYSIS_SIGNAL_TEXT_MAX,
                 "%.3f",
                 sample_rate_hz / 1e3);
        snprintf(Global_Analysis_Signal_Field_Text[3],
                 ANALYSIS_SIGNAL_TEXT_MAX,
                 "%.6f",
                 start_sec);
        snprintf(Global_Analysis_Signal_Field_Text[4],
                 ANALYSIS_SIGNAL_TEXT_MAX,
                 "%.6f",
                 end_sec);
        snprintf(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_DECIMATION_FIELD],
                 ANALYSIS_SIGNAL_TEXT_MAX,
                 "1");

        Global_Analysis_Signal_File_Manual_Edit = 0;
        ANALYSIS_signal_refresh_filename_if_auto();
        Global_Analysis_Signal_File_Cursor = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);
        ANALYSIS_signal_clear_file_selection();

    }

}

static void ANALYSIS_draw_thick_line(SDL_Renderer *renderer,
                                     int x0,
                                     int y0,
                                     int x1,
                                     int y1,
                                     int thickness,
                                     SDL_Color color){

    if (!renderer || thickness <= 0) return;

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

static void ANALYSIS_draw_circle_outline(SDL_Renderer *renderer,
                                         int cx,
                                         int cy,
                                         int radius,
                                         int thickness,
                                         SDL_Color color){

    if (!renderer || radius <= 0 || thickness <= 0) return;

    int segments = 48;

    for (int t = 0; t < thickness; t++) {

        double r = (double)(radius - t);

        if (r <= 0.0) break;

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

static void ANALYSIS_draw_signal_gear_shape(SDL_Renderer *renderer,
                                            SDL_Rect icon_rect,
                                            SDL_Color gear,
                                            SDL_Color cutout){

    (void)cutout;

    if (!renderer) return;

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
        double angles[4] = {
            base - 0.245,
            base - 0.115,
            base + 0.115,
            base + 0.245
        };
        double radii[4] = {11.0, 15.0, 15.0, 11.0};

        for (int j = 0; j < 4 && point_count < 32; j++) {

            points_x[point_count] = cx + (int)lrint(cos(angles[j]) * radii[j]);
            points_y[point_count] = cy + (int)lrint(sin(angles[j]) * radii[j]);
            point_count++;

        }

    }

    for (int i = 0; i < point_count; i++) {

        int j = (i + 1) % point_count;

        ANALYSIS_draw_thick_line(renderer,
                                 points_x[i],
                                 points_y[i],
                                 points_x[j],
                                 points_y[j],
                                 3,
                                 gear);

    }

    ANALYSIS_draw_circle_outline(renderer, cx, cy, 7, 3, gear);

}

static void ANALYSIS_draw_signal_settings_icon(SDL_Renderer *renderer,
                                               TTF_Font *font,
                                               int win_w,
                                               int win_h){

    if (!renderer || !font || !ANALYSIS_signal_menu_available()) return;

    SDL_Rect icon_rect;
    ANALYSIS_get_signal_icon_rect(win_w, win_h, &icon_rect);

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int hover = point_in_rect(mouse_x, mouse_y, icon_rect);

    SDL_Color bg = hover || Global_Analysis_Signal_Menu_Open ?
                   (SDL_Color){0, 40, 16, 235} :
                   (SDL_Color){0, 0, 0, 220};
    SDL_Color border = hover || Global_Analysis_Signal_Menu_Open ?
                       (SDL_Color){0, 255, 90, 255} :
                       (SDL_Color){0, 130, 50, 230};
    SDL_Color gear = hover || Global_Analysis_Signal_Menu_Open ?
                     (SDL_Color){0, 255, 90, 255} :
                     (SDL_Color){0, 185, 70, 255};

    if (hover || Global_Analysis_Signal_Menu_Open) {

        SDL_Rect glow = {icon_rect.x - 3, icon_rect.y - 3, icon_rect.w + 6, icon_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 35});
        draw_outline_rect(renderer, glow, (SDL_Color){0, 255, 90, 150});

    }

    draw_filled_rect(renderer, icon_rect, bg);
    draw_outline_rect(renderer, icon_rect, border);
    ANALYSIS_draw_signal_gear_shape(renderer, icon_rect, gear, bg);

}

static void ANALYSIS_draw_signal_trash_shape(SDL_Renderer *renderer,
                                             SDL_Rect rect,
                                             SDL_Color color){

    if (!renderer) return;

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

static void ANALYSIS_draw_signal_trash_icon(SDL_Renderer *renderer,
                                            TTF_Font *font,
                                            int win_w,
                                            int win_h){

    if (!renderer || !font || !ANALYSIS_signal_menu_available()) return;

    SDL_Rect trash_rect;
    ANALYSIS_get_signal_trash_rect(win_w, win_h, &trash_rect);

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int hover = point_in_rect(mouse_x, mouse_y, trash_rect);

    SDL_Color bg = hover || Global_Analysis_Delete_Confirm_Open ?
                   (SDL_Color){42, 0, 0, 235} :
                   (SDL_Color){0, 0, 0, 220};
    SDL_Color border = hover || Global_Analysis_Delete_Confirm_Open ?
                       (SDL_Color){255, 60, 60, 255} :
                       (SDL_Color){120, 120, 120, 230};
    SDL_Color trash = hover || Global_Analysis_Delete_Confirm_Open ?
                      (SDL_Color){255, 70, 70, 255} :
                      (SDL_Color){150, 150, 150, 255};

    if (hover || Global_Analysis_Delete_Confirm_Open) {

        SDL_Rect glow = {trash_rect.x - 3, trash_rect.y - 3, trash_rect.w + 6, trash_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){255, 40, 40, 35});
        draw_outline_rect(renderer, glow, (SDL_Color){255, 60, 60, 150});

    }

    draw_filled_rect(renderer, trash_rect, bg);
    draw_outline_rect(renderer, trash_rect, border);
    ANALYSIS_draw_signal_trash_shape(renderer, trash_rect, trash);

}

static void ANALYSIS_get_delete_confirm_rects(int win_w,
                                              int win_h,
                                              SDL_Rect *panel_rect,
                                              SDL_Rect *yes_rect,
                                              SDL_Rect *no_rect){

    int panel_w = 640;
    int panel_h = 300;

    if (panel_w > win_w - 60) panel_w = win_w - 60;
    if (panel_h > win_h - 60) panel_h = win_h - 60;
    if (panel_w < 520) panel_w = 520;
    if (panel_h < 260) panel_h = 260;

    SDL_Rect panel = {
        (win_w - panel_w) / 2,
        (win_h - panel_h) / 2,
        panel_w,
        panel_h
    };

    if (panel_rect) *panel_rect = panel;
    if (yes_rect) *yes_rect = (SDL_Rect){panel.x + panel.w - 236, panel.y + panel.h - 58, 92, 36};
    if (no_rect) *no_rect = (SDL_Rect){panel.x + panel.w - 124, panel.y + panel.h - 58, 92, 36};

}

static void ANALYSIS_draw_delete_confirm_menu(SDL_Renderer *renderer,
                                              TTF_Font *font,
                                              int win_w,
                                              int win_h){

    if (!renderer || !font || !Global_Analysis_Delete_Confirm_Open) return;

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

    draw_text(renderer,
              font,
              "Delete Recording",
              panel.x + 24,
              panel.y + 18,
              (SDL_Color){255, 80, 80, 255});

    draw_text(renderer,
              font,
              "Are you sure you want to delete the following file?",
              panel.x + 24,
              panel.y + 74,
              (SDL_Color){0, 255, 90, 255});

    ANALYSIS_draw_wrapped_text(renderer,
                               font,
                               Global_Analysis_Delete_Confirm_File,
                               panel.x + 24,
                               panel.y + 108,
                               panel.w - 48,
                               20,
                               (SDL_Color){190, 220, 190, 255});

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

    draw_filled_rect(renderer,
                     yes_rect,
                     yes_hover ?
                     (SDL_Color){78, 0, 0, 255} :
                     (SDL_Color){44, 0, 0, 255});
    draw_outline_rect(renderer,
                      yes_rect,
                      yes_hover ?
                      (SDL_Color){255, 120, 120, 255} :
                      (SDL_Color){255, 70, 70, 255});
    ANALYSIS_draw_centered_button_text(renderer,
                                       font,
                                       yes_rect,
                                       "Yes",
                                       (SDL_Color){255, 70, 70, 255});

    if (no_hover) {

        SDL_Rect glow = {no_rect.x - 3, no_rect.y - 3, no_rect.w + 6, no_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){220, 220, 220, 34});
        draw_outline_rect(renderer, glow, (SDL_Color){230, 230, 230, 170});

    }

    draw_filled_rect(renderer,
                     no_rect,
                     no_hover ?
                     (SDL_Color){34, 34, 34, 255} :
                     (SDL_Color){12, 12, 12, 255});
    draw_outline_rect(renderer,
                      no_rect,
                      no_hover ?
                      (SDL_Color){230, 230, 230, 255} :
                      (SDL_Color){130, 130, 130, 255});
    ANALYSIS_draw_centered_button_text(renderer,
                                       font,
                                       no_rect,
                                       "No",
                                       (SDL_Color){190, 190, 190, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

}

static void ANALYSIS_open_delete_confirm(void){

    const char *name = ANALYSIS_selected_file_name();

    if (!name || name[0] == '\0') return;

    snprintf(Global_Analysis_Delete_Confirm_File,
             sizeof(Global_Analysis_Delete_Confirm_File),
             "%s",
             name);
    snprintf(Global_Analysis_Delete_Confirm_Path,
             sizeof(Global_Analysis_Delete_Confirm_Path),
             "%s/%s",
             Global_Analysis_Record_Dir,
             name);
    Global_Analysis_Delete_Confirm_Open = 1;
    Global_Analysis_Signal_Menu_Open = 0;
    Global_Analysis_Signal_Active_Field = ANALYSIS_SIGNAL_FIELD_NONE;
    ANALYSIS_signal_clear_file_selection();

}

static int ANALYSIS_delete_confirmed_file(void){

    if (!Global_Analysis_Delete_Confirm_Open ||
        Global_Analysis_Delete_Confirm_Path[0] == '\0') {

        return 0;

    }

    char deleted_name[512];
    char deleted_path[1024];

    snprintf(deleted_name, sizeof(deleted_name), "%s", Global_Analysis_Delete_Confirm_File);
    snprintf(deleted_path, sizeof(deleted_path), "%s", Global_Analysis_Delete_Confirm_Path);

    if (remove(deleted_path) != 0) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "Failed to delete %.160s",
                 deleted_name);
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

    snprintf(Global_Analysis_Status,
             sizeof(Global_Analysis_Status),
             "Deleted %.160s",
             deleted_name);
    Global_Analysis_Dirty = 1;
    return 1;

}

static void ANALYSIS_handle_delete_confirm_event(SDL_Event *event, int win_w, int win_h){

    if (!event || !Global_Analysis_Delete_Confirm_Open) return;

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

static void ANALYSIS_draw_signal_menu(SDL_Renderer *renderer,
                                      TTF_Font *font,
                                      int win_w,
                                      int win_h){

    if (!renderer || !font || !Global_Analysis_Signal_Menu_Open) return;

    Global_Analysis_Signal_Last_Font = font;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 155});

    SDL_Rect panel;
    SDL_Rect field_rects[ANALYSIS_SIGNAL_FIELD_COUNT];
    SDL_Rect save_rect;
    SDL_Rect close_rect;

    ANALYSIS_get_signal_menu_rects(win_w,
                                   win_h,
                                   &panel,
                                   field_rects,
                                   &save_rect,
                                   &close_rect);

    draw_filled_rect(renderer, panel, (SDL_Color){0, 8, 3, 245});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 255, 90, 255});

    SDL_Rect title_bar = {panel.x, panel.y, panel.w, 54};
    draw_filled_rect(renderer, title_bar, (SDL_Color){0, 24, 8, 245});
    draw_outline_rect(renderer, title_bar, (SDL_Color){0, 160, 60, 230});

    draw_text(renderer,
              font,
              "Signal File Settings",
              panel.x + 24,
              panel.y + 18,
              (SDL_Color){0, 255, 90, 255});

    char file_label[ANALYSIS_SIGNAL_TEXT_MAX + 16];

    snprintf(file_label,
             sizeof(file_label),
             "Output: %s",
             Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD][0] ?
             Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD] :
             "");

    ANALYSIS_draw_wrapped_text(renderer,
                               font,
                               file_label,
                               panel.x + 24,
                               panel.y + 66,
                               panel.w - 48,
                               19,
                               (SDL_Color){190, 220, 190, 255});

    draw_text(renderer,
              font,
              "Save New creates a new .complex16 file and opens it for every graph.",
              panel.x + 24,
              panel.y + 180,
              (SDL_Color){130, 170, 130, 255});

    for (int i = 0; i < ANALYSIS_SIGNAL_FIELD_COUNT; i++) {

        SDL_Rect r = field_rects[i];
        int active = (Global_Analysis_Signal_Active_Field == i);

        draw_text(renderer,
                  font,
                  ANALYSIS_SIGNAL_FIELD_LABELS[i],
                  r.x,
                  r.y - 22,
                  (SDL_Color){0, 210, 70, 255});

        draw_filled_rect(renderer, r, (SDL_Color){0, 0, 0, 255});
        draw_outline_rect(renderer,
                          r,
                          active ?
                          (SDL_Color){0, 255, 90, 255} :
                          (SDL_Color){0, 105, 42, 230});

        if (i == ANALYSIS_SIGNAL_FILENAME_FIELD) {

            ANALYSIS_signal_draw_filename_field_text(renderer, font, r, active);

        }

        else {

            char visible_text[ANALYSIS_SIGNAL_TEXT_MAX + 4];

            if (Global_Analysis_Signal_Field_Text[i][0] != '\0') {

                ANALYSIS_short_text(font,
                                    Global_Analysis_Signal_Field_Text[i],
                                    visible_text,
                                    sizeof(visible_text),
                                    r.w - 18);

            }

            else {

                snprintf(visible_text,
                         sizeof(visible_text),
                         "%s",
                         active ? "_" : "Click to type");

            }

            if (active && Global_Analysis_Signal_Field_Text[i][0] != '\0') {

                size_t len = strlen(visible_text);

                if (len + 1 < sizeof(visible_text)) {

                    visible_text[len] = '_';
                    visible_text[len + 1] = '\0';

                }

            }

            draw_text(renderer,
                      font,
                      visible_text,
                      r.x + 8,
                      r.y + 10,
                      Global_Analysis_Signal_Field_Text[i][0] != '\0' || active ?
                      (SDL_Color){0, 255, 90, 255} :
                      (SDL_Color){95, 130, 95, 255});

        }

    }

    int mouse_x = 0;
    int mouse_y = 0;
    ANALYSIS_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    SDL_Rect start_marker_rect;
    SDL_Rect end_marker_rect;

    ANALYSIS_get_signal_marker_rects(field_rects,
                                     &start_marker_rect,
                                     &end_marker_rect);

    int marker_enabled = Global_Analysis_Marker_Active;
    int start_marker_hover = marker_enabled && point_in_rect(mouse_x, mouse_y, start_marker_rect);
    int end_marker_hover = marker_enabled && point_in_rect(mouse_x, mouse_y, end_marker_rect);

    ANALYSIS_draw_signal_marker_button(renderer,
                                       font,
                                       start_marker_rect,
                                       marker_enabled,
                                       start_marker_hover);
    ANALYSIS_draw_signal_marker_button(renderer,
                                       font,
                                       end_marker_rect,
                                       marker_enabled,
                                       end_marker_hover);

    int save_hover = point_in_rect(mouse_x, mouse_y, save_rect);
    int close_hover = point_in_rect(mouse_x, mouse_y, close_rect);

    if (save_hover) {

        SDL_Rect glow = {save_rect.x - 3, save_rect.y - 3, save_rect.w + 6, save_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 40});
        draw_outline_rect(renderer, glow, (SDL_Color){0, 255, 90, 180});

    }

    draw_filled_rect(renderer,
                     save_rect,
                     save_hover ?
                     (SDL_Color){0, 78, 28, 255} :
                     (SDL_Color){0, 48, 18, 255});
    draw_outline_rect(renderer,
                      save_rect,
                      save_hover ?
                      (SDL_Color){120, 255, 160, 255} :
                      (SDL_Color){0, 255, 90, 255});
    ANALYSIS_draw_centered_button_text(renderer,
                                       font,
                                       save_rect,
                                       "Save New",
                                       (SDL_Color){0, 255, 90, 255});

    if (close_hover) {

        SDL_Rect glow = {close_rect.x - 3, close_rect.y - 3, close_rect.w + 6, close_rect.h + 6};
        draw_filled_rect(renderer, glow, (SDL_Color){220, 220, 220, 34});
        draw_outline_rect(renderer, glow, (SDL_Color){230, 230, 230, 170});

    }

    draw_filled_rect(renderer,
                     close_rect,
                     close_hover ?
                     (SDL_Color){34, 34, 34, 255} :
                     (SDL_Color){12, 12, 12, 255});
    draw_outline_rect(renderer,
                      close_rect,
                      close_hover ?
                      (SDL_Color){230, 230, 230, 255} :
                      (SDL_Color){130, 130, 130, 255});
    ANALYSIS_draw_centered_button_text(renderer,
                                       font,
                                       close_rect,
                                       "Close",
                                       (SDL_Color){230, 230, 230, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

}


static int ANALYSIS_signal_parse_double_field(int index,
                                              const char *label,
                                              double min_value,
                                              double max_value,
                                              double *out){

    if (!out || index < 0 || index >= ANALYSIS_SIGNAL_FIELD_COUNT) return 0;

    const char *text = Global_Analysis_Signal_Field_Text[index];

    if (!text || text[0] == '\0') {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "%s is empty",
                 label);
        return 0;

    }

    char *end = NULL;
    double value = strtod(text, &end);

    if (end == text) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "%s must be numeric",
                 label);
        return 0;

    }

    while (*end == ' ' || *end == '\t') end++;

    if (*end != '\0') {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "%s has invalid characters",
                 label);
        return 0;

    }

    if (!isfinite(value) || value < min_value || value > max_value) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "%s is out of range",
                 label);
        return 0;

    }

    *out = value;
    return 1;

}

static void ANALYSIS_signal_make_base_name(const char *src, char *out, size_t out_size){

    if (!out || out_size == 0) return;

    const char *name = src ? src : "signal";
    const char *slash = strrchr(name, '/');

    if (slash) name = slash + 1;

    snprintf(out, out_size, "%s", name[0] ? name : "signal");

    const char *suffix = ".complex16";
    size_t len = strlen(out);
    size_t suffix_len = strlen(suffix);

    if (len >= suffix_len && strcmp(out + len - suffix_len, suffix) == 0) {

        out[len - suffix_len] = '\0';

    }

    for (size_t i = 0; out[i] != '\0'; i++) {

        if (out[i] == '/' || out[i] == '\\' || out[i] == ':' || out[i] == '*' ||
            out[i] == '?' || out[i] == '"' || out[i] == '<' || out[i] == '>' || out[i] == '|') {

            out[i] = '_';

        }

    }

}

static void ANALYSIS_signal_sanitize_output_filename(const char *src, char *out, size_t out_size){

    if (!out || out_size == 0) return;

    const char *name = src ? src : "signal";
    const char *slash = strrchr(name, '/');

    if (slash) name = slash + 1;

    const char *backslash = strrchr(name, '\\');

    if (backslash) name = backslash + 1;

    snprintf(out, out_size, "%s", name[0] ? name : "signal");

    for (size_t i = 0; out[i] != '\0'; i++) {

        if (out[i] == '/' || out[i] == '\\' || out[i] == ':' || out[i] == '*' ||
            out[i] == '?' || out[i] == '"' || out[i] == '<' || out[i] == '>' || out[i] == '|') {

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

static void ANALYSIS_signal_copy_component(const char *src,
                                           char *dst,
                                           size_t dst_size,
                                           size_t max_chars){

    if (!dst || dst_size == 0) return;

    if (!src || src[0] == '\0') src = "0";

    size_t len = strlen(src);

    if (len > max_chars) len = max_chars;
    if (len >= dst_size) len = dst_size - 1;

    memcpy(dst, src, len);
    dst[len] = '\0';

}

static void ANALYSIS_signal_build_live_filename(char *out, size_t out_size){

    if (!out || out_size == 0) return;

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
    ANALYSIS_signal_copy_component(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_DECIMATION_FIELD],
                                   decimation,
                                   sizeof(decimation),
                                   40);

    snprintf(raw,
             sizeof(raw),
             "%.150s_NEW_%.80ss_%.80ss_CAPTURE_%.80sMHz_BW_%.80skHz_SR_%.80sk_Decimation_%.40s.complex16",
             base,
             start,
             end,
             center,
             bw,
             sr,
             decimation);

    ANALYSIS_signal_sanitize_output_filename(raw, out, out_size);

}

static void ANALYSIS_signal_refresh_filename_if_auto(void){

    if (Global_Analysis_Signal_File_Manual_Edit) return;

    ANALYSIS_signal_build_live_filename(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD],
                                        ANALYSIS_SIGNAL_TEXT_MAX);
    Global_Analysis_Signal_File_Cursor = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);
    ANALYSIS_signal_clear_file_selection();

}

static void ANALYSIS_draw_centered_button_text(SDL_Renderer *renderer,
                                               TTF_Font *font,
                                               SDL_Rect rect,
                                               const char *text,
                                               SDL_Color color){

    if (!renderer || !font || !text) return;

    int text_w = 0;
    int text_h = 0;

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

static int ANALYSIS_signal_copy_crop(const char *src_path,
                                     const char *dst_path,
                                     size_t start_sample,
                                     size_t end_sample){

    if (!src_path || !dst_path || end_sample <= start_sample) return 0;

    FILE *src = fopen(src_path, "rb");

    if (!src) return 0;

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

        if (got == 0) break;

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

    if (!ok) remove(dst_path);

    return ok;

}

static int ANALYSIS_signal_apply_crop_settings(void){

    if (Global_Analysis_Path[0] == '\0' || Global_Analysis_IQ_Count == 0) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "Open a recording before saving a new file");
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
        !ANALYSIS_signal_parse_double_field(ANALYSIS_SIGNAL_DECIMATION_FIELD, "Decimation", 1.0, 1000000000.0, &decimation)) {

        return 0;

    }

    if (end_sec <= start_sec) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "End time must be after start time");
        return 0;

    }

    (void)decimation;

    double sample_rate_hz = sample_rate_ksps * 1000.0;
    size_t start_sample = (size_t)llround(start_sec * sample_rate_hz);
    size_t end_sample = (size_t)llround(end_sec * sample_rate_hz);

    if (start_sample >= Global_Analysis_IQ_Count) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "Start time is beyond the file length");
        return 0;

    }

    if (end_sample > Global_Analysis_IQ_Count) end_sample = Global_Analysis_IQ_Count;

    if (end_sample <= start_sample) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "Selected time range is empty");
        return 0;

    }

    char candidate[1024];
    char created_name[512];
    char base_no_suffix[512];

    if (Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD][0] == '\0') {

        ANALYSIS_signal_refresh_filename_if_auto();

    }

    ANALYSIS_signal_sanitize_output_filename(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD],
                                             created_name,
                                             sizeof(created_name));

    ANALYSIS_signal_make_base_name(created_name, base_no_suffix, sizeof(base_no_suffix));

    snprintf(candidate, sizeof(candidate), "%s/%s", Global_Analysis_Record_Dir, created_name);

    for (int i = 2; i < 1000; i++) {

        FILE *test = fopen(candidate, "rb");

        if (!test) break;

        fclose(test);

        snprintf(created_name,
                 sizeof(created_name),
                 "%.470s_v%d.complex16",
                 base_no_suffix,
                 i);
        snprintf(candidate, sizeof(candidate), "%s/%s", Global_Analysis_Record_Dir, created_name);

    }

    if (!ANALYSIS_signal_copy_crop(Global_Analysis_Path,
                                   candidate,
                                   start_sample,
                                   end_sample)) {

        snprintf(Global_Analysis_Status,
                 sizeof(Global_Analysis_Status),
                 "Failed to create new complex16 file");
        return 0;

    }

    if (ANALYSIS_scan_recordings()) {

        for (int i = 0; i < Global_Analysis_File_Count; i++) {

            if (strcmp(Global_Analysis_Files[i], created_name) == 0) {

                Global_Analysis_Selected = i;
                Global_Analysis_List_Scroll = i - 2;
                if (Global_Analysis_List_Scroll < 0) Global_Analysis_List_Scroll = 0;
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
    Global_Analysis_Dirty = 1;

    snprintf(Global_Analysis_Status,
             sizeof(Global_Analysis_Status),
             "Created new file %.160s",
             created_name);

    snprintf(Global_Analysis_Signal_Menu_File,
             sizeof(Global_Analysis_Signal_Menu_File),
             "%s",
             created_name);
    snprintf(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD],
             ANALYSIS_SIGNAL_TEXT_MAX,
             "%s",
             created_name);
    Global_Analysis_Signal_File_Cursor = (int)strlen(Global_Analysis_Signal_Field_Text[ANALYSIS_SIGNAL_FILENAME_FIELD]);
    Global_Analysis_Signal_File_Manual_Edit = 0;
    Global_Analysis_Signal_File_Selecting = 0;
    Global_Analysis_Signal_File_Selection_Start = -1;
    Global_Analysis_Signal_File_Selection_End = -1;

    return 1;

}

static int ANALYSIS_handle_signal_menu_event(SDL_Event *event, int win_w, int win_h){

    if (!event || (!ANALYSIS_signal_menu_available() && !Global_Analysis_Delete_Confirm_Open)) return 0;

    if (Global_Analysis_Delete_Confirm_Open) {

        ANALYSIS_handle_delete_confirm_event(event, win_w, win_h);
        return 1;

    }

    if (!Global_Analysis_Signal_Menu_Open) {

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

            SDL_Rect icon_rect;
            SDL_Rect trash_rect;

            ANALYSIS_get_signal_icon_rect(win_w, win_h, &icon_rect);
            ANALYSIS_get_signal_trash_rect(win_w, win_h, &trash_rect);

            if (point_in_rect(event->button.x, event->button.y, trash_rect)) {

                ANALYSIS_open_delete_confirm();
                return 1;

            }

            if (point_in_rect(event->button.x, event->button.y, icon_rect)) {

                if (Global_Analysis_Loaded_Index != Global_Analysis_Selected ||
                    Global_Analysis_Path[0] == '\0') {

                    ANALYSIS_open_selected_recording();

                }

                ANALYSIS_signal_menu_prefill();
                Global_Analysis_Signal_Menu_Open = 1;
                Global_Analysis_Signal_Active_Field = 0;
                Global_Analysis_Dragging = 0;
                Global_Analysis_Filter_Selecting = 0;
                Global_Analysis_Column_Selecting = 0;
                snprintf(Global_Analysis_Status,
                         sizeof(Global_Analysis_Status),
                         "Signal metadata menu opened");
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
            snprintf(Global_Analysis_Status,
                     sizeof(Global_Analysis_Status),
                     "Signal metadata menu closed");
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

        ANALYSIS_get_signal_menu_rects(win_w,
                                       win_h,
                                       &panel,
                                       field_rects,
                                       &save_rect,
                                       &close_rect);
        (void)panel;
        (void)save_rect;
        (void)close_rect;

        int cursor = ANALYSIS_signal_set_file_cursor_from_mouse(Global_Analysis_Signal_Last_Font,
                                                                field_rects[ANALYSIS_SIGNAL_FILENAME_FIELD],
                                                                event->motion.x,
                                                                event->motion.y);

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

        ANALYSIS_get_signal_menu_rects(win_w,
                                       win_h,
                                       &panel,
                                       field_rects,
                                       &save_rect,
                                       &close_rect);

        SDL_Rect start_marker_rect;
        SDL_Rect end_marker_rect;

        ANALYSIS_get_signal_marker_rects(field_rects,
                                         &start_marker_rect,
                                         &end_marker_rect);

        if (Global_Analysis_Marker_Active &&
            point_in_rect(event->button.x, event->button.y, start_marker_rect)) {

            ANALYSIS_signal_set_time_field_from_marker(3);
            return 1;

        }

        if (Global_Analysis_Marker_Active &&
            point_in_rect(event->button.x, event->button.y, end_marker_rect)) {

            ANALYSIS_signal_set_time_field_from_marker(4);
            return 1;

        }

        for (int i = 0; i < ANALYSIS_SIGNAL_FIELD_COUNT; i++) {

            if (point_in_rect(event->button.x, event->button.y, field_rects[i])) {

                Global_Analysis_Signal_Active_Field = i;

                if (i == ANALYSIS_SIGNAL_FILENAME_FIELD) {

                    int cursor = ANALYSIS_signal_set_file_cursor_from_mouse(Global_Analysis_Signal_Last_Font,
                                                                            field_rects[i],
                                                                            event->button.x,
                                                                            event->button.y);

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
            snprintf(Global_Analysis_Status,
                     sizeof(Global_Analysis_Status),
                     "Signal metadata menu closed");
            return 1;

        }

        if (!point_in_rect(event->button.x, event->button.y, panel)) {

            return 1;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONUP ||
        event->type == SDL_MOUSEMOTION ||
        event->type == SDL_MOUSEWHEEL ||
        event->type == SDL_KEYUP) {

        return 1;

    }

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

    ANALYSIS_draw_signal_trash_icon(renderer, font, win_w, win_h);
    ANALYSIS_draw_signal_settings_icon(renderer, font, win_w, win_h);
    ANALYSIS_draw_signal_menu(renderer, font, win_w, win_h);
    ANALYSIS_draw_delete_confirm_menu(renderer, font, win_w, win_h);

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

        window[i] = 0.5 - 0.5 * cos((2.0 * M_PI * (double)i) / (double)(ANALYSIS_FFT_SIZE - 1));

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



int ANALYSIS_is_text_entry_active(void)
{
    return Global_Analysis_Signal_Menu_Open &&
           Global_Analysis_Signal_Active_Field != ANALYSIS_SIGNAL_FIELD_NONE;
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

    if (ANALYSIS_handle_signal_menu_event(event, win_w, win_h)) {

        if (active) *active = FIELD_NONE;
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
