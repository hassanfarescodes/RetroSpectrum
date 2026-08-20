#ifndef ANALYSIS_WORKSTATION_H
#define ANALYSIS_WORKSTATION_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stddef.h>
#include <stdint.h>

#include "GUIs.h"

#define ANALYSIS_EVENT_IGNORED 0
#define ANALYSIS_EVENT_HANDLED 1
#define ANALYSIS_EVENT_QUIT 2

extern int Global_Analysis_Mode;
extern int Global_Analysis_Dirty;
extern int Global_Analysis_File_Count;
extern int Global_Analysis_Selected;
extern int Global_Analysis_List_Scroll;
extern int Global_Analysis_Dragging;
extern int Global_Analysis_Drag_Last_X;
extern int Global_Analysis_Loading;
extern int Global_Analysis_Load_Frame;
extern int Global_Analysis_Loaded_Index;
extern int Global_Analysis_Render_W;
extern size_t Global_Analysis_IQ_Count;
extern size_t Global_Analysis_View_Start;
extern size_t Global_Analysis_View_Len;
extern double Global_Analysis_Sample_Rate;
extern double Global_Analysis_Center_Hz;
extern char Global_Analysis_Path[1024];
extern int Global_Analysis_Const_Count;
extern int Global_Analysis_Filter_Visible;
extern int Global_Analysis_Filter_Selecting;
extern int Global_Analysis_Filter_Active;
extern double Global_Analysis_Filter_Y0;
extern double Global_Analysis_Filter_Y1;
extern int Global_Analysis_Marker_Active;
extern size_t Global_Analysis_Marker_Sample;
extern double Global_Analysis_Marker_Time;
extern int Global_Analysis_Column_Selecting;
extern int Global_Analysis_Column_Visible;
extern int Global_Analysis_Column_Active;
extern double Global_Analysis_Column_X0;
extern double Global_Analysis_Column_X1;
extern char Global_Analysis_Status[256];
extern char Global_Analysis_Files[512][512];
extern float Global_Analysis_Mag_Line[ANALYSIS_MAX_RENDER_W];
extern float Global_Analysis_Phase_Line[ANALYSIS_MAX_RENDER_W];
extern float Global_Analysis_InstFreq_Line[ANALYSIS_MAX_RENDER_W];
extern float Global_Analysis_PSD_Line[ANALYSIS_MAX_RENDER_W];
extern float Global_Analysis_Const_I[4096];
extern float Global_Analysis_Const_Q[4096];

void ANALYSIS_enter_mode(const char *record_dir, uint64_t fallback_center_hz, uint32_t fallback_rec_out_rate_hz,
                         uint32_t fallback_sample_rate_hz);

void ANALYSIS_exit_mode(uint32_t *pixels, int tex_w, int tex_h, SDL_Texture *texture);

int ANALYSIS_handle_event(SDL_Event *event, int win_w, int win_h, uint32_t *pixels, int tex_w, int tex_h,
                          SDL_Texture *waterfall_texture, uint64_t *next_waterfall_ms, Type_Active_Fields *active);

void ANALYSIS_render_workstation_data(uint32_t *pixels, int tex_w, int tex_h);

int ANALYSIS_get_recording_workspace(const char *file_name);

int ANALYSIS_get_available_workspace_count(void); 

int ANALYSIS_export_recording_to_workspace(const char *record_dir, const char *file_name, uint64_t fallback_center_hz,
                                           uint32_t fallback_rec_out_rate_hz, uint32_t fallback_sample_rate_hz,
                                           int *workspace_number, char *error, size_t error_size);

int ANALYSIS_export_classification_fields(char *file_name, size_t file_name_size, double *frequency_mhz,
                                          double *bandwidth_khz, double *start_time,
                                          double *end_time);

int ANALYSIS_is_text_entry_active(void);

void ANALYSIS_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, SDL_Texture *texture, uint32_t *pixels,
                               int tex_w, int tex_h, int win_w, int win_h);

void ANALYSIS_draw_workstation_overlays(SDL_Renderer *renderer, TTF_Font *font, SDL_Texture *texture, int tex_w,
                                        int tex_h, int win_w, int win_h);

#endif
