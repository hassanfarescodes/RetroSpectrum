#ifndef RETROSPECTRUM_AUTH_ADMIN_H
#define RETROSPECTRUM_AUTH_ADMIN_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

int AUTH_ADMIN_run(SDL_Window *window, SDL_Renderer *renderer, TTF_Font *font_small,
                   TTF_Font *font_medium, const char *authenticated_admin,
                   int bootstrap_mode);

#endif
