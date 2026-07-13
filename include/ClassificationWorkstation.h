#ifndef CLASSIFICATION_WORKSTATION_H
#define CLASSIFICATION_WORKSTATION_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

extern int Global_Classification_Mode;

void CLASSIFICATION_enter_mode(const char *record_dir);
void CLASSIFICATION_exit_mode(void);
int CLASSIFICATION_handle_event(SDL_Event *event, int win_w, int win_h);
void CLASSIFICATION_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);

#endif
