#ifndef GUIS_H
#define GUIS_H

#include <SDL2/SDL.h>

// Track active settings
typedef enum {
    FIELD_NONE = 0,
    FIELD_FREQ,
    FIELD_SR,
    FIELD_DISPLAY,
    FIELD_LNA,
    FIELD_VGA,
    FIELD_FPS,
    FIELD_ROWS
} Type_Active_Fields;

// GUI Rectangle
typedef struct {
  SDL_Rect rect;
  char text[32];
  const char *label;
  Type_Active_Fields id;
} Type_Input_Box;

// Selector window
typedef struct {
  double X0;
  double X1;
  int enabled;
  int dragging;
  int resizing_left;
  int resizing_right;
} Type_Selector;

#endif
