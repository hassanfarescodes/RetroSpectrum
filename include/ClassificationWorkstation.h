#ifndef CLASSIFICATION_WORKSTATION_H
#define CLASSIFICATION_WORKSTATION_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

extern int Global_Classification_Mode;

void CLASSIFICATION_enter_mode(const char *record_dir);
void CLASSIFICATION_exit_mode(void);
void CLASSIFICATION_prefill_from_analysis_selection(const char *file_name, double frequency_mhz,
                                                    double bandwidth_khz, double start_time,
                                                    double end_time);
int CLASSIFICATION_is_text_entry_active(void);
int CLASSIFICATION_handle_event(SDL_Event *event, int win_w, int win_h);
void CLASSIFICATION_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);

#endif
