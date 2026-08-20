#ifndef RETROSPECTRUM_CORRELATION_WORKSTATION_H
#define RETROSPECTRUM_CORRELATION_WORKSTATION_H

#include <stddef.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define CORRELATION_TREND_POINTS 1024

typedef struct TransmissionSignature {
    double start_time;
    double duration;
    double center_frequency;
    double occupied_bandwidth;
    double peak_power;
    double average_power;

    float magnitude_trend[CORRELATION_TREND_POINTS];
    float frequency_trend[CORRELATION_TREND_POINTS];
    float phase_trend[CORRELATION_TREND_POINTS];
} TransmissionSignature;

extern int Global_Correlation_Mode;

void CORRELATION_enter_mode(const char *record_dir, unsigned long long fallback_center_hz,
                            unsigned int fallback_record_rate_hz, unsigned int fallback_sample_rate_hz);
void CORRELATION_exit_mode(void);
void CORRELATION_shutdown(void);
int CORRELATION_is_text_entry_active(void);
int CORRELATION_handle_event(const SDL_Event *event, int win_w, int win_h);
void CORRELATION_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);

#endif
