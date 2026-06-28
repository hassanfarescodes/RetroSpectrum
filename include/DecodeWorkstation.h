/*
 * ============================================================================
 * File:            DecodeWorkstation.h
 * Author:          Hassan Fares
 *
 * Description:     Decode workstation interface for RetroSpectrum.
 *                  Uses liquid-dsp to demodulate .complex16 recordings into
 *                  visible binary symbol/bit streams.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#ifndef DECODE_WORKSTATION_H
#define DECODE_WORKSTATION_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

extern int Global_Decode_Mode;

void DECODE_enter_mode(const char *record_dir);
void DECODE_exit_mode(void);
int  DECODE_is_text_entry_active(void);
int  DECODE_handle_event(const SDL_Event *event, int win_w, int win_h);
void DECODE_draw_workstation(SDL_Renderer *renderer,
                             TTF_Font *font,
                             int win_w,
                             int win_h);

#endif
