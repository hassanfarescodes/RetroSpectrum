/*
 * ============================================================================
 * File:            CaseManagementWorkstation.h
 * Author:          Hassan Fares
 *
 * Description:     Case management block-graph workstation for RetroSpectrum.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#ifndef CASE_MANAGEMENT_WORKSTATION_H
#define CASE_MANAGEMENT_WORKSTATION_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

extern int Global_CaseManagement_Mode;

void CASE_MANAGEMENT_enter_mode(const char *record_dir);
void CASE_MANAGEMENT_exit_mode(void);
int CASE_MANAGEMENT_is_text_entry_active(void);
int CASE_MANAGEMENT_handle_event(const SDL_Event *event, int win_w, int win_h);
void CASE_MANAGEMENT_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);

#endif
