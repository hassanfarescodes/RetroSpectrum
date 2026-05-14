/*
 * ============================================================================
 * File:            RetroSpectrum.c
 * Author:          Hassan Fares
 *
 * Confidential:    No
 *
 * Description:     Main logic for the RetroSpectrum application
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 *                                                                   05/04/2026
 * ============================================================================
 */

// =========
// Libraries
// =========

// Standard Libraries
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

// HackRF Library
#include <libhackrf/hackrf.h>

// FFT Library
#include <fftw3.h>

// SDL (GUI) Library
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL.h>

// ============
// Header Files
// ============

// Responsible for GUI objects
#include "GUIs.h"

// Responsible for IQ objects
#include "IQs.h"

// ======================
// Global Initializations
// ======================

/*
        GLOBAL DEFINITIONS              VALUE
*/

#define DEFAULT_CENTER_FREQ_HZ          101300000ULL
#define DEFAULT_SAMPLE_RATE_HZ          2000000U
#define DEFAULT_DISPLAY_SPAN_HZ         1000000U
#define DEFAULT_LNA_GAIN                16
#define DEFAULT_VGA_GAIN                12
#define DEFAULT_AMP_ENABLE              0
#define DEFAULT_DC_CORRECTION_ENABLE    0
#define DEFAULT_WATERFALL_FPS           60
#define DEFAULT_ROWS_PER_FRAME          4

#define MIN_WINDOW_WIDTH                1320
#define MIN_WINDOW_HEIGHT               650

#define CONTROL_PANEL_HEIGHT            95
#define AXIS_HEIGHT                     70
#define MARGIN                          20

#define REL_MIN_DB                      2.0
#define REL_MAX_DB                      22.0

#define PRE_RECORD_SECONDS              5

#define REC_FIR_TAPS                    255

#ifndef M_PI
#define M_PI                            3.14159265358979323846
#endif


static volatile sig_atomic_t Global_Running = 1;

static pthread_mutex_t Global_Rec_Lock = PTHREAD_MUTEX_INITIALIZER;

/*
        TYPE            VARIABLE                VALUE
*/

static  FILE*           Global_Rec_File         = NULL;
static  uint64_t        Global_Rec_Center_Hz    = 0;
static  uint64_t        Global_Center_Freq_Hz   = DEFAULT_CENTER_FREQ_HZ;
static  uint32_t        Global_Sample_Rate_Hz   = DEFAULT_SAMPLE_RATE_HZ;
static  uint32_t        Global_Display_Span_Hz  = DEFAULT_DISPLAY_SPAN_HZ;
static  uint32_t        Global_Rec_BW_Hz        = 0;
static  uint32_t        Global_Rec_Out_Rate_Hz  = 0;
static  double*         Global_Color_Baseline   = NULL;       
static  double          Global_DC_I             = 0.0;
static  double          Global_DC_Q             = 0.0;
static  double          Global_Rec_Phase        = 0.0;
static  double          Global_Rec_Acc_I        = 0.0;
static  double          Global_Rec_Acc_Q        = 0.0;
static  int             Global_Rec_FIR_Pos      = 0;
static  int             Global_LNA_Gain         = DEFAULT_LNA_GAIN;
static  int             Global_VGA_Gain         = DEFAULT_VGA_GAIN;
static  int             Global_Amp_Enable       = DEFAULT_AMP_ENABLE;
static  int             Global_DC_Enable        = DEFAULT_DC_CORRECTION_ENABLE;
static  int             Global_Waterfall_FPS    = DEFAULT_WATERFALL_FPS;
static  int             Global_Rows_Per_Frame   = DEFAULT_ROWS_PER_FRAME;
static  int             Global_Rec              = 0;
static  int             Global_Rec_Acc_Count    = 0;
static  int             Global_Rec_Decimation   = 1;
static  int             Global_Radio_Running    = 0;
static  char            Global_Status_Msg[256]  = "";
static  SDL_Color       Global_Status_Color     = {0, 255, 80, 255};

static  double          Global_Rec_FIR[REC_FIR_TAPS];
static  double          Global_Rec_Hist_I[REC_FIR_TAPS];
static  double          Global_Rec_Hist_Q[REC_FIR_TAPS];

static  Type_RingBuf    ring_buf;
static  Type_Rec_Cache  Global_Pre_Cache;

static  Type_Selector   Global_Selector         = { .X0 = 0.40,
                                                    .X1 = 0.60,
                                                    .enabled = 0,
                                                    .dragging = 0,
                                                    .resizing_left = 0,
                                                    .resizing_right = 0
                                                  };

// =========
// Functions
// =========

static void handle_sigint(int sig){

    (void)sig;
    Global_Running = 0;

}

static double limit_double(double value, double low, double high){

    if (value < low) return low;

    if (value > high) return high;

    return value;

}

static void set_status(const char *msg, SDL_Color color){

    snprintf(Global_Status_Msg, sizeof(Global_Status_Msg), "%s", msg);

    Global_Status_Color = color;

}

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b){

    return 0xFF000000U | ((uint32_t)r << 16) | ((uint32_t) g << 8) | b;

}

static uint64_t selection_center_Hz(void){

    double Center_Frac = (Global_Selector.X0 + Global_Selector.X1) * 0.5;

    double Offset_Hz = (Center_Frac - 0.5) * (double)Global_Display_Span_Hz;

    double Calc_Freq = (double)Global_Center_Freq_Hz + Offset_Hz;

    if (Calc_Freq < 0.0) Calc_Freq = 0.0;

    return (uint64_t)Calc_Freq;

}

static uint32_t selection_BW_Hz(void){

    double BW = fabs(Global_Selector.X1 - Global_Selector.X0) * (double)Global_Display_Span_Hz;

    if (BW < 1000.0) BW = 1000.0;

    if (BW > (double)Global_Sample_Rate_Hz) BW = (double)Global_Sample_Rate_Hz;

    return (uint32_t)BW;

}

static void stop_recording(void){

    pthread_mutex_lock(&Global_Rec_Lock);

    if (Global_Rec_File){
        fclose(Global_Rec_File);
        Global_Rec_File = NULL;
    }

    Global_Rec = 0;
    Global_Rec_Phase = 0.0;
    Global_Rec_Acc_I = 0.0;
    Global_Rec_Acc_Q = 0.0;
    Global_Rec_Acc_Count = 0;

    pthread_mutex_unlock(&Global_Rec_Lock);

    set_status("", (SDL_Color){0, 255, 80, 255});
}

static void configure_recording_filter(void) {
    memset(Global_Rec_FIR, 0, sizeof(Global_Rec_FIR));
    memset(Global_Rec_Hist_I, 0, sizeof(Global_Rec_Hist_I));
    memset(Global_Rec_Hist_Q, 0, sizeof(Global_Rec_Hist_Q));

    Global_Rec_FIR_Pos = 0;
    Global_Rec_Acc_Count = 0;

    /*
     * Output rate should be comfortably above selected bandwidth.
     * 2.5x gives room for FIR transition.
     */
    double wanted_out_rate = (double)Global_Rec_BW_Hz * 3;

    if (wanted_out_rate < 48000.0) {
        wanted_out_rate = 48000.0;
    }

    Global_Rec_Decimation = (int)((double)Global_Sample_Rate_Hz / wanted_out_rate);

    if (Global_Rec_Decimation < 1) {
        Global_Rec_Decimation = 1;
    }

    Global_Rec_Out_Rate_Hz = Global_Sample_Rate_Hz / (uint32_t)Global_Rec_Decimation;

    /*
     * After shifting selected center to 0 Hz, selected bandwidth is:
     *
     * -BW/2 to +BW/2
     */
    double cutoff_hz = (double)Global_Rec_BW_Hz * 0.5;

    /*
     * Keep cutoff below decimated Nyquist.
     */
    double max_safe_cutoff = (double)Global_Rec_Out_Rate_Hz * 0.45;

    if (cutoff_hz > max_safe_cutoff) {
        cutoff_hz = max_safe_cutoff;
    }

    /*
     * Normalized cutoff relative to input sample rate.
     */
    double fc = cutoff_hz / (double)Global_Sample_Rate_Hz;

    double sum = 0.0;
    int mid = REC_FIR_TAPS / 2;

    for (int n = 0; n < REC_FIR_TAPS; n++) {
        int m = n - mid;

        double sinc;

        if (m == 0) {
            sinc = 2.0 * fc;
        } else {
            sinc = sin(2.0 * M_PI * fc * (double)m) / (M_PI * (double)m);
        }

        /*
         * Hamming window.
         */
        double window = 0.54 - 0.46 * cos((2.0 * M_PI * (double)n) / (double)(REC_FIR_TAPS - 1));

        Global_Rec_FIR[n] = sinc * window;
        sum += Global_Rec_FIR[n];
    }

    /*
     * Normalize gain to 1.0.
     */
    if (fabs(sum) > 1e-12) {
        for (int n = 0; n < REC_FIR_TAPS; n++) {
            Global_Rec_FIR[n] /= sum;
        }
    }
}

static int pre_cache_init(Type_Rec_Cache *c, uint32_t sample_rate_hz){

    memset(c, 0, sizeof(*c));
    
    c->capacity = (size_t)sample_rate_hz * PRE_RECORD_SECONDS;

    c->I = malloc(sizeof(int16_t) * c->capacity);
    c->Q = malloc(sizeof(int16_t) * c->capacity);

    if (!c->I || !c->Q){
        free(c->I);
        free(c->Q);
        c->I = NULL;
        c->Q = NULL;
        c->capacity = 0;
        return 0;
    }

    pthread_mutex_init(&c->lock, NULL);

    return 1;

}

static void pre_cache_free(Type_Rec_Cache *c){
    
    pthread_mutex_destroy(&c->lock);

    free(c->I);
    free(c->Q);

    memset(c, 0, sizeof(*c));

}

static int pre_cache_resize(Type_Rec_Cache *c, uint32_t sample_rate_hz){

    pthread_mutex_lock(&c->lock);

    free(c->I);
    free(c->Q);

    c->capacity = (size_t)sample_rate_hz * PRE_RECORD_SECONDS;
    c->write_pos = 0;
    c->count = 0;

    c->I = malloc(sizeof(int16_t) * c->capacity);
    c->Q = malloc(sizeof(int16_t) * c->capacity);

    int status = (c->I && c->Q);

    if (!status){

        free(c->I);
        free(c->Q);
        c->I = NULL;
        c->Q = NULL;
        c->capacity = 0;
        c->write_pos = 0;
        c->count = 0;

    }

    pthread_mutex_unlock(&c->lock);

    return status;

}

static void pre_cache_write(Type_Rec_Cache *c, float I, float Q){

    if (!c->I || !c->Q || c->capacity == 0) return;

    if (I > 1.0f) I = 1.0f;
    if (I < -1.0f) I = -1.0f;

    if (Q > 1.0f) Q = 1.0f;
    if (Q < -1.0f) Q = -1.0f;

    c->I[c->write_pos] = (int16_t)(I * 32767.0f);
    c->Q[c->write_pos] = (int16_t)(Q * 32767.0f);

    c->write_pos = (c->write_pos + 1) % c->capacity;

    if(c->count < c->capacity){

        c->count++;

    }
}

static size_t pre_cache_snapshot(Type_Rec_Cache *c, int16_t **out_I, int16_t **out_Q){

    *out_I = NULL;
    *out_Q = NULL;

    pthread_mutex_lock(&c->lock);

    size_t count = c->count;

    if (count == 0 || !c->I || !c->Q){
        pthread_mutex_unlock(&c->lock);
        return 0;
    }

    int16_t *copy_I = malloc(sizeof(int16_t) * count);
    int16_t *copy_Q = malloc(sizeof(int16_t) * count);

    if (!copy_I || !copy_Q) {
        free(copy_I);
        free(copy_Q);
        pthread_mutex_unlock(&c->lock);
        return 0;
    }

    size_t start;

    if (c->count < c->capacity) start = 0;
    else start = c->write_pos;

    for (size_t n = 0; n < count; n++){

        size_t idx = (start + n) % c->capacity;

        copy_I[n] = c->I[idx];
        copy_Q[n] = c->Q[idx];

    }

    pthread_mutex_unlock(&c->lock);

    *out_I = copy_I;
    *out_Q = copy_Q;

    return count;

}

static void recorder_write_sample(float I, float Q){

    if (!Global_Rec_File) return;

    /*
     * Shift selected center frequency to baseband.
     */
    double Freq_Offset_Hz = (double)Global_Rec_Center_Hz - (double)Global_Center_Freq_Hz;
    double Phase_Step = -2.0 * M_PI * Freq_Offset_Hz / (double)Global_Sample_Rate_Hz;

    double C = cos(Global_Rec_Phase);
    double S = sin(Global_Rec_Phase);

    double Shifted_I = I * C - Q * S;
    double Shifted_Q = I * S + Q * C;

    Global_Rec_Phase += Phase_Step;

    if (Global_Rec_Phase > M_PI){
        Global_Rec_Phase -= 2.0 * M_PI;
    }

    if (Global_Rec_Phase < -M_PI){
        Global_Rec_Phase += 2.0 * M_PI;
    }

    if (Global_Rec_Decimation <= 1) {
    if (Shifted_I > 1.0) Shifted_I = 1.0;
    if (Shifted_I < -1.0) Shifted_I = -1.0;

    if (Shifted_Q > 1.0) Shifted_Q = 1.0;
    if (Shifted_Q < -1.0) Shifted_Q = -1.0;

    int16_t iq_pair[2];

    iq_pair[0] = (int16_t)(Shifted_I * 32767.0);
    iq_pair[1] = (int16_t)(Shifted_Q * 32767.0);

    fwrite(iq_pair, sizeof(int16_t), 2, Global_Rec_File);
    return;
    }

    /*
     * Always store the newest shifted sample.
     */
    Global_Rec_Hist_I[Global_Rec_FIR_Pos] = Shifted_I;
    Global_Rec_Hist_Q[Global_Rec_FIR_Pos] = Shifted_Q;

    int newest_pos = Global_Rec_FIR_Pos;

    Global_Rec_FIR_Pos++;

    if (Global_Rec_FIR_Pos >= REC_FIR_TAPS) {
        Global_Rec_FIR_Pos = 0;
    }

    /*
     * Decimation gate.
     *
     * Do not run the full FIR convolution unless this input sample
     * will produce one output sample.
     */
    Global_Rec_Acc_Count++;

    if (Global_Rec_Acc_Count < Global_Rec_Decimation) {
        return;
    }

    Global_Rec_Acc_Count = 0;

    /*
     * FIR convolution only on output samples.
     */
    double Filtered_I = 0.0;
    double Filtered_Q = 0.0;

    int hist_idx = newest_pos;

    for (int tap = 0; tap < REC_FIR_TAPS; tap++) {
        Filtered_I += Global_Rec_FIR[tap] * Global_Rec_Hist_I[hist_idx];
        Filtered_Q += Global_Rec_FIR[tap] * Global_Rec_Hist_Q[hist_idx];

        hist_idx--;

        if (hist_idx < 0) {
            hist_idx = REC_FIR_TAPS - 1;
        }
    }

    if (Filtered_I > 1.0) Filtered_I = 1.0;
    if (Filtered_I < -1.0) Filtered_I = -1.0;

    if (Filtered_Q > 1.0) Filtered_Q = 1.0;
    if (Filtered_Q < -1.0) Filtered_Q = -1.0;

    int16_t iq_pair[2];

    iq_pair[0] = (int16_t)(Filtered_I * 32767.0);
    iq_pair[1] = (int16_t)(Filtered_Q * 32767.0);

    fwrite(iq_pair, sizeof(int16_t), 2, Global_Rec_File);
}

static void recorder_process_sample(float I, float Q){

    if (!Global_Rec || !Global_Rec_File) return;

    recorder_write_sample(I, Q);
}


static int start_recording(void){

    stop_recording();

    Global_Rec_Center_Hz = selection_center_Hz();
    Global_Rec_BW_Hz = selection_BW_Hz();

    configure_recording_filter();

    char datetime_str[32];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    strftime(datetime_str, sizeof(datetime_str), "%m-%d-%Y_%H-%M-%S", tm_now);

    char filename[256];
    
    snprintf(filename,
             sizeof(filename),
             "%s_CAPTURE_%.6fMHz_BW_%.3fkHz_SR_%.3fk%d.complex16",
             datetime_str,
             Global_Rec_Center_Hz / 1e6,
             Global_Rec_BW_Hz / 1e3,
             Global_Rec_Out_Rate_Hz / 1e3,
             Global_Rec_Decimation
            );

    Global_Rec_File = fopen(filename, "wb");

    if (!Global_Rec_File){

        Global_Rec = 0;
        set_status("Record Open Failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    Global_Rec_Phase = 0.0;
    Global_Rec_Acc_I = 0.0;
    Global_Rec_Acc_Q = 0.0;
    Global_Rec_Acc_Count = 0;
    Global_Rec_FIR_Pos = 0;

    memset(Global_Rec_Hist_I, 0, sizeof(Global_Rec_Hist_I));
    memset(Global_Rec_Hist_Q, 0, sizeof(Global_Rec_Hist_Q));

    int16_t *pre_I = NULL;
    int16_t *pre_Q = NULL;

    size_t pre_count = pre_cache_snapshot(&Global_Pre_Cache, &pre_I, &pre_Q);

    if (pre_count > 0 && pre_I && pre_Q){

        for (size_t n = 0; n < pre_count; n++){

            float I = (float)pre_I[n] / 32767.0f;
            float Q = (float)pre_Q[n] / 32767.0f;

            recorder_write_sample(I, Q);

        }

    free(pre_I);
    free(pre_Q);

    }

    Global_Rec = 1;
    Global_Selector.dragging = 0;
    Global_Selector.resizing_left = 0;
    Global_Selector.resizing_right = 0;

    char msg[256];

    snprintf(msg, sizeof(msg), "RECORDING %.6f MHz - BW %.3f kHz", Global_Rec_Center_Hz / 1e6, Global_Rec_BW_Hz / 1e3);

    set_status(msg, (SDL_Color){255, 60, 40, 255});

    return 1;

}


static size_t ring_available_locked(Type_RingBuf* r){
    
    if (r->write_pos >= r->read_pos) return r->write_pos - r->read_pos;

    return RING_SIZE - r->read_pos + r->write_pos;

}

static void ring_clear(Type_RingBuf *r){

    pthread_mutex_lock(&r->lock);

    r->write_pos = 0;
    r->read_pos = 0;

    pthread_mutex_unlock(&r->lock);
}

static void ring_write_sample(Type_RingBuf *r, float I, float Q){
    
    r->I[r->write_pos] = I;
    r->Q[r->write_pos] = Q;

    r->write_pos = (r->write_pos + 1) % RING_SIZE;

    if (r->write_pos == r->read_pos){

        r->read_pos = (r->read_pos+1) % RING_SIZE;

    }

}

static int ring_read_block(Type_RingBuf *r, fftw_complex *in, double *window){

    pthread_mutex_lock(&r->lock);

if (ring_available_locked(r) < FFT_SIZE){

        pthread_mutex_unlock(&r->lock);
        return 0;

    }

    for (int sam = 0; sam < FFT_SIZE; sam++){
        size_t idx = (r->read_pos + sam) % RING_SIZE;
        in[sam][0] = r->I[idx] * window[sam];
        in[sam][1] = r->Q[idx] * window[sam];
    }

    // Hann window is being used, smoothen out any edges and visualize shorter bursts better
    // That is why we add by FFT_SIZE / 2 (Use half of the older samples)
    r->read_pos = (r->read_pos + FFT_SIZE / 2) % RING_SIZE;

    pthread_mutex_unlock(&r->lock);
    return 1;

}

#define MAX_TRANSFER_SAMPLES 262144

static float temp_I[MAX_TRANSFER_SAMPLES];
static float temp_Q[MAX_TRANSFER_SAMPLES];

static int rx_callback(hackrf_transfer *transfer){

    const int8_t *buf = (const int8_t *)transfer->buffer;
    int sample_count = transfer->valid_length / 2;

    if (sample_count > MAX_TRANSFER_SAMPLES) {
        sample_count = MAX_TRANSFER_SAMPLES;
    }

    /*
     * Convert signed HackRF IQ once.
     */

    pthread_mutex_lock(&Global_Pre_Cache.lock);

    for (int n = 0; n < sample_count; n++){

        float I = (float)buf[2 * n] / 128.0f;
        float Q = (float)buf[2 * n + 1] / 128.0f;

        if (Global_DC_Enable) {
            const double alpha = 0.0001;

            Global_DC_I += alpha * ((double)I - Global_DC_I);
            Global_DC_Q += alpha * ((double)Q - Global_DC_Q);

            I -= (float)Global_DC_I;
            Q -= (float)Global_DC_Q;
        }

        temp_I[n] = I;
        temp_Q[n] = Q;

        pre_cache_write(&Global_Pre_Cache, I, Q);
    }

    pthread_mutex_unlock(&Global_Pre_Cache.lock);

    /*
     * Waterfall ring buffer.
     * Lock only around ring_buf writes.
     */
    pthread_mutex_lock(&ring_buf.lock);

    for (int n = 0; n < sample_count; n++){
        ring_write_sample(&ring_buf, temp_I[n], temp_Q[n]);
    }

    pthread_mutex_unlock(&ring_buf.lock);

    /*
     * Recorder path.
     * No ring_buf.lock here.
     */
    if (Global_Rec) {
        pthread_mutex_lock(&Global_Rec_Lock);

        for (int n = 0; n < sample_count; n++){
            recorder_process_sample(temp_I[n], temp_Q[n]);
        }

        pthread_mutex_unlock(&Global_Rec_Lock);
    }

    return 0;
}


// AI-Assisted
static uint32_t power_to_color_relative(double rel_db, double delta_db, double peakness_db) {
    if (rel_db < REL_MIN_DB) return rgb(0, 0, 0);

    double norm = (rel_db - REL_MIN_DB) / (REL_MAX_DB - REL_MIN_DB);
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;

uint8_t r = 0, g = 0, b = 0;

    if (norm < 0.30) {
        double t = norm / 0.30;
        g = (uint8_t)(10 + 55 * t);
    } 

    else if (norm < 0.70) {
        double t = (norm - 0.30) / 0.40;
        g = (uint8_t)(65 + 135 * t);
    } 

    else {
        double t = (norm - 0.70) / 0.30;
        r = (uint8_t)(0 + 35 * t);
        g = (uint8_t)(200 + 55 * t);
        b = (uint8_t)(0 + 15 * t);
    }

    double event_strength = 0.0;

    if (delta_db > 10.0) event_strength += (delta_db - 10.0) / 30.0;
    if (peakness_db > 12.0) event_strength += (peakness_db - 12.0) / 30.0;
    if (rel_db > 34.0) event_strength += (rel_db - 34.0) / 30.0;

    if (event_strength > 1.0) event_strength = 1.0;
    if (event_strength < 0.10) event_strength = 0.0;

    if (event_strength > 0.0) {
        uint8_t er = 255;
        uint8_t eg = (uint8_t)(100 * (1.0 - event_strength));
        uint8_t eb = 0;

        r = (uint8_t)((1.0 - event_strength) * r + event_strength * er);
        g = (uint8_t)((1.0 - event_strength) * g + event_strength * eg);
        b = (uint8_t)((1.0 - event_strength) * b + event_strength * eb);
    }

    return rgb(r, g, b);
}

static void compute_DB_from_FFT(fftw_complex *out, double *db){

    for (int sam = 0; sam < FFT_SIZE; sam++){

        int shifted_sam = (sam + FFT_SIZE / 2) % FFT_SIZE;
        
        double I = out[shifted_sam][0];
        double Q = out[shifted_sam][1];

        double magnitude = sqrt(I*I + Q*Q) / FFT_SIZE;
        db[sam] = 20.0 * log10(magnitude + 1e-12) + 100.0;

    }

}

static void clear_waterfall(uint32_t *pixels, int w, int h) {
    for (int i = 0; i < w * h; i++) pixels[i] = rgb(0, 0, 0);
}

static void reset_prev_col_db(int tex_w) {
    if (!Global_Color_Baseline) return;
    for (int i = 0; i < tex_w; i++) Global_Color_Baseline[i] = -300.0;
}

static int cmp_double_for_qsort(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static void get_visible_bin_range(int *start_bin, int *end_bin) {
    double bins_per_hz = (double)FFT_SIZE / (double)Global_Sample_Rate_Hz;
    int visible_bins = (int)((double)Global_Display_Span_Hz * bins_per_hz);

    if (visible_bins < 8) visible_bins = 8;
    if (visible_bins > FFT_SIZE) visible_bins = FFT_SIZE;

    int center = FFT_SIZE / 2;

    *start_bin = center - visible_bins / 2;
    *end_bin = *start_bin + visible_bins;

    if (*start_bin < 0) {
        *start_bin = 0;
        *end_bin = visible_bins;
    }

    if (*end_bin > FFT_SIZE) {
*end_bin = FFT_SIZE;
        *start_bin = FFT_SIZE - visible_bins;
        if (*start_bin < 0) *start_bin = 0;
    }
}

static double estimate_noise_floor_median_visible(double *db, int start_bin, int end_bin){

    static double temp[FFT_SIZE];
    int count = 0;

    for (int x = start_bin; (x < end_bin) && (count < FFT_SIZE); x++){

        if(abs(x - FFT_SIZE / 2) < 2) continue;
        temp[count++] = db[x];

    }

    if (count <= 0) return -300.0;

    qsort(temp, count, sizeof(double), cmp_double_for_qsort);
    return temp[count / 2];

}

static void add_fft_line_to_waterfall(uint32_t *pixels, int tex_w, int tex_h, double *db) {
    memmove(pixels + tex_w, pixels, sizeof(uint32_t) * tex_w * (tex_h - 1));

    int visible_start = 0;
    int visible_end = FFT_SIZE;

    get_visible_bin_range(&visible_start, &visible_end);

    double noise_floor_db = estimate_noise_floor_median_visible(db, visible_start, visible_end);

    for (int x = 0; x < tex_w; x++) {
        int start_bin = visible_start + x * (visible_end - visible_start) / tex_w;
        int end_bin = visible_start + (x + 1) * (visible_end - visible_start) / tex_w;

        if (end_bin <= start_bin) end_bin = start_bin + 1;
        if (end_bin > visible_end) end_bin = visible_end;

        double max_db = -300.0;
        double sum_db = 0.0;
        int count = 0;

        /*
         * Track the top 3 bins instead of trusting one random spike.
         */
        double top1 = -300.0;
        double top2 = -300.0;
        double top3 = -300.0;

        
        for (int k = start_bin; k < end_bin; k++) {
            if (abs(k - FFT_SIZE / 2) < 2) continue;

            double v = db[k];

            if (v > top1) {
                top3 = top2;
                top2 = top1;
                top1 = v;
            } else if (v > top2) {
                top3 = top2;
                top2 = v;
            } else if (v > top3) {
                top3 = v;
            }

            if (v > max_db) max_db = v;

            sum_db += v;
            count++;
        }

        double avg_db = (count > 0) ? (sum_db / count) : max_db;

        /*
         * Smoothed peak reduces salt-and-pepper red noise.
         */

        double color_db;

        if (count >= 3) {
            color_db = (top1 + top2 + top3) / 3.0;
        } else {
            color_db = max_db;
        }

        double peakness_db = color_db - avg_db;
        double rel_db = color_db - noise_floor_db;
        double delta_db = 0.0;

        if (Global_Color_Baseline) {
            if (Global_Color_Baseline[x] < -200.0) {
                Global_Color_Baseline[x] = color_db;
            } else {
                delta_db = color_db - Global_Color_Baseline[x];
                Global_Color_Baseline[x] = 0.95 * Global_Color_Baseline[x] + 0.05 * color_db;
            }
        }

        pixels[x] = power_to_color_relative(rel_db, delta_db, peakness_db);
    }
}

static TTF_Font *load_font(int size) {
    const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        NULL
    };

    for (int i = 0; paths[i] != NULL; i++) {
        TTF_Font *font = TTF_OpenFont(paths[i], size);
        if (font) return font;
    }

    return NULL;
}

static void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    if (!font || !text || text[0] == '\0') return;

    SDL_Surface *surface = TTF_RenderText_Blended(font, text, color);
    if (!surface) return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

static void draw_filled_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

static void draw_outline_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

static void draw_made_in_usa(SDL_Renderer *renderer, TTF_Font *font, int win_w) {
    const char *msg = "Made with <3 in";

    int text_w = 0;
    int text_h = 0;

    if (font && TTF_SizeText(font, msg, &text_w, &text_h) != 0) {
        text_w = 0;
        text_h = 0;
    }

    int flag_w = 38;
    int flag_h = 22;
    int gap = 8;
    int right_pad = 34;
    int y = 14;

    int flag_x = win_w - right_pad - flag_w;
    int text_x = flag_x - gap - text_w;

    draw_text(renderer, font, msg, text_x, y + 2, (SDL_Color){0, 220, 70, 255});

    SDL_Rect flag = {flag_x, y, flag_w, flag_h};

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &flag);

    int stripe_h = flag_h / 13;
    if (stripe_h < 1) stripe_h = 1;

    for (int s = 0; s < 13; s += 2) {
        SDL_Rect stripe = {
            flag_x,
            y + s * flag_h / 13,
            flag_w,
            flag_h / 13 + 1
        };

        SDL_SetRenderDrawColor(renderer, 180, 20, 35, 255);
        SDL_RenderFillRect(renderer, &stripe);
    }

    SDL_Rect canton = {
        flag_x,
        y,
        flag_w * 2 / 5,
        flag_h * 7 / 13
    };

    SDL_SetRenderDrawColor(renderer, 20, 45, 120, 255);
    SDL_RenderFillRect(renderer, &canton);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 5; col++) {
            int px = canton.x + 3 + col * 3;
            int py = canton.y + 2 + row * 3;
            SDL_RenderDrawPoint(renderer, px, py);
        }
    }

    draw_outline_rect(renderer, flag, (SDL_Color){0, 120, 45, 255});
}

static void draw_button(
    SDL_Renderer *renderer,
    TTF_Font *font,
    SDL_Rect rect,
    const char *label,
    int active,
    int is_record_button
) {
    SDL_Color fill, border, text;

    if (is_record_button) {
        fill   = active ? (SDL_Color){130, 0, 0, 255} : (SDL_Color){0, 8, 3, 255};
        border = active ? (SDL_Color){255, 80, 60, 255} : (SDL_Color){0, 100, 40, 255};
        text   = active ? (SDL_Color){255, 130, 110, 255} : (SDL_Color){0, 180, 70, 255};
    } else {
        fill   = active ? (SDL_Color){0, 70, 25, 255} : (SDL_Color){0, 8, 3, 255};
        border = active ? (SDL_Color){255, 60, 40, 255} : (SDL_Color){0, 180, 60, 255};
        text   = active ? (SDL_Color){255, 70, 50, 255} : (SDL_Color){0, 255, 90, 255};
    }

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);

    int text_w = 0;
    int text_h = 0;
    if (font && TTF_SizeText(font, label, &text_w, &text_h) != 0) {
        text_w = 0;
        text_h = 0;
    }

    int text_x = rect.x + (rect.w - text_w) / 2;
    int text_y = rect.y + (rect.h - text_h) / 2;

    draw_text(renderer, font, label, text_x, text_y, text);
}

static int point_in_rect(int x, int y, SDL_Rect r) {
    return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
}

static int near_px(int a, int b, int tolerance) {
    return abs(a - b) <= tolerance;
}

static void append_text(char *dst, size_t dst_sz, const char *src) {
    size_t len = strlen(dst);

    while (*src && len + 1 < dst_sz) {
        char c = *src++;
        if ((c >= '0' && c <= '9') || c == '.') dst[len++] = c;
}

    dst[len] = '\0';
}

static void backspace_text(char *dst) {
    size_t len = strlen(dst);
    if (len > 0) dst[len - 1] = '\0';
}

static int parse_positive_double(const char *s, double *out) {
    if (!s || !*s) return 0;

    char *end = NULL;
    double v = strtod(s, &end);

if (end == s || *end != '\0' || v <= 0.0) return 0;

    *out = v;
    return 1;
}

static int parse_nonnegative_int(const char *s, int *out) {
    if (!s || !*s) return 0;

    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (end == s || *end != '\0' || v < 0 || v > 100000) return 0;

    *out = (int)v;
    return 1;
}

static int normalize_lna_gain(int gain) {
    if (gain < 0) gain = 0;
    if (gain > 40) gain = 40;
    return (gain / 8) * 8;
}

static int normalize_vga_gain(int gain) {
    if (gain < 0) gain = 0;
    if (gain > 62) gain = 62;
    return (gain / 2) * 2;
}

static int normalize_fps(int fps) {
    if (fps < 1) fps = 1;
    if (fps > 1000) fps = 1000;
    return fps;
}

static int normalize_rows_per_frame(int rows) {
    if (rows < 1) rows = 1;
    if (rows > 64) rows = 64;
    return rows;
}

static int stop_radio(hackrf_device *dev) {
    if (Global_Radio_Running) {
        if (hackrf_stop_rx(dev) != HACKRF_SUCCESS) return 0;
        Global_Radio_Running = 0;
    }

    return 1;
}

static int start_radio(hackrf_device *dev) {
    if (!Global_Radio_Running) {
        if (hackrf_start_rx(dev, rx_callback, NULL) != HACKRF_SUCCESS) return 0;
        Global_Radio_Running = 1;
    }

    return 1;
}

static double recommended_antenna_length_inches(uint64_t freq_hz) {
    if (freq_hz == 0) return 0.0;

    /*
     * Quarter-wave antenna length:
     *
     * wavelength = c / f
     * quarter-wave = wavelength / 4
     *
     * c ≈ 299,792,458 m/s
     *
     * Return value is in inches.
     */
    double wavelength_m = 299792458.0 / (double)freq_hz;
    double quarter_wave_m = wavelength_m / 4.0;

    return quarter_wave_m * 39.37007874;
}

static int apply_radio_settings(hackrf_device *dev, uint64_t Center_Hz, 
                                uint32_t Sample_Rate_Hz, uint32_t Display_Span_Hz, 
                                int LNA_Gain, int VGA_Gain, int Amp_Enable){

    if (Global_Rec) stop_recording();
    if (!stop_radio(dev)) return 0;

    ring_clear(&ring_buf);

    if (hackrf_set_sample_rate(dev, Sample_Rate_Hz) != HACKRF_SUCCESS) return 0;
    if (hackrf_set_freq(dev, Center_Hz) != HACKRF_SUCCESS) return 0;    

    uint32_t filter_bw = hackrf_compute_baseband_filter_bw_round_down_lt(Sample_Rate_Hz);

    if (filter_bw > 0) hackrf_set_baseband_filter_bandwidth(dev, filter_bw);

    LNA_Gain = normalize_lna_gain(LNA_Gain);
    VGA_Gain = normalize_vga_gain(VGA_Gain);

    if (hackrf_set_lna_gain(dev, (uint32_t)LNA_Gain) != HACKRF_SUCCESS) return 0;
    if (hackrf_set_vga_gain(dev, (uint32_t)VGA_Gain) != HACKRF_SUCCESS) return 0;
    if (hackrf_set_amp_enable(dev, (uint8_t)(Amp_Enable ? 1 : 0)) != HACKRF_SUCCESS) return 0;

    if (Display_Span_Hz > Sample_Rate_Hz) Display_Span_Hz = Sample_Rate_Hz;
    if (Display_Span_Hz < 1000) Display_Span_Hz = 1000; 
    
    Global_Center_Freq_Hz = Center_Hz;
    Global_Sample_Rate_Hz = Sample_Rate_Hz;
    Global_Display_Span_Hz = Display_Span_Hz;
    Global_LNA_Gain = LNA_Gain;
    Global_VGA_Gain = VGA_Gain;
    Global_Amp_Enable = Amp_Enable ? 1 : 0;

    if (!pre_cache_resize(&Global_Pre_Cache, Global_Sample_Rate_Hz)) {

        set_status("Pre-cache resize failed", (SDL_Color){255, 60, 40, 255});

    return 0;
    }

    return start_radio(dev);

}

static void draw_input_box(SDL_Renderer *renderer, TTF_Font *font, Type_Input_Box *box, int active) {
    SDL_Color border = active ? (SDL_Color){0, 255, 80, 255} : (SDL_Color){0, 100, 40, 255};
    SDL_Color fill = {0, 10, 3, 255};
    SDL_Color label = {0, 210, 70, 255};
    SDL_Color text = {0, 255, 90, 255};

    draw_text(renderer, font, box->label, box->rect.x, box->rect.y - 22, label);
    draw_filled_rect(renderer, box->rect, fill);
    draw_outline_rect(renderer, box->rect, border);
    draw_text(renderer, font, box->text, box->rect.x + 8, box->rect.y + 10, text);
}

static void draw_checkbox(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, int checked) {
    draw_outline_rect(renderer, rect, (SDL_Color){0, 255, 80, 255});

    if (checked) {
        SDL_Rect inner = {rect.x + 5, rect.y + 5, rect.w - 10, rect.h - 10};
        draw_filled_rect(renderer, inner, (SDL_Color){0, 255, 80, 255});
    }

    draw_text(renderer, font, label, rect.x + rect.w + 8, rect.y + 2, (SDL_Color){0, 220, 70, 255});
}

static void layout_controls(
    int win_w,
    Type_Input_Box *freq_box,
    Type_Input_Box *sr_box,
    Type_Input_Box *display_box,
    Type_Input_Box *lna_box,
    Type_Input_Box *vga_box,
    Type_Input_Box *fps_box,
    Type_Input_Box *rows_box,
    SDL_Rect *amp_box,
    SDL_Rect *dc_box,
    SDL_Rect *sel_button,
    SDL_Rect *rec_button
) {
    int y = 42;
    int x = MARGIN + 20;
    int box_h = 42;
    int gap = 12;

    freq_box->rect = (SDL_Rect){x, y, 145, box_h}; x += 145 + gap;
    sr_box->rect = (SDL_Rect){x, y, 145, box_h}; x += 145 + gap;
    display_box->rect = (SDL_Rect){x, y, 155, box_h}; x += 155 + gap;
    lna_box->rect = (SDL_Rect){x, y, 82, box_h}; x += 82 + gap;
    vga_box->rect = (SDL_Rect){x, y, 82, box_h}; x += 82 + gap;
    fps_box->rect = (SDL_Rect){x, y, 95, box_h}; x += 95 + gap;
    rows_box->rect = (SDL_Rect){x, y, 105, box_h};

    *amp_box = (SDL_Rect){win_w - 405, y - 4, 22, 22};
    *dc_box = (SDL_Rect){win_w - 405, y + 22, 22, 22};
    *sel_button = (SDL_Rect){win_w - 260, y, 105, box_h};
    *rec_button = (SDL_Rect){win_w - 140, y, 105, box_h};
}

static void draw_control_panel(
    SDL_Renderer *renderer,
    TTF_Font *font,
    int win_w,
    Type_Input_Box *freq_box,
    Type_Input_Box *sr_box,
    Type_Input_Box *display_box,
    Type_Input_Box *lna_box,
    Type_Input_Box *vga_box,
    Type_Input_Box *fps_box,
    Type_Input_Box *rows_box,
    SDL_Rect amp_box,
    SDL_Rect dc_box,
    SDL_Rect sel_button,
    SDL_Rect rec_button,
    Type_Active_Fields active
) {
    SDL_Rect panel = {MARGIN, 8, win_w - 2 * MARGIN, CONTROL_PANEL_HEIGHT - 8};

    draw_filled_rect(renderer, panel, (SDL_Color){0, 0, 0, 255});
    draw_outline_rect(renderer, panel, (SDL_Color){0, 90, 35, 255});

    draw_input_box(renderer, font, freq_box, active == FIELD_FREQ);
    draw_input_box(renderer, font, sr_box, active == FIELD_SR);
    draw_input_box(renderer, font, display_box, active == FIELD_DISPLAY);
    draw_input_box(renderer, font, lna_box, active == FIELD_LNA);
    draw_input_box(renderer, font, vga_box, active == FIELD_VGA);
    draw_input_box(renderer, font, fps_box, active == FIELD_FPS);
    draw_input_box(renderer, font, rows_box, active == FIELD_ROWS);

    draw_checkbox(renderer, font, amp_box, "AMPLIFY", Global_Amp_Enable);
    draw_checkbox(renderer, font, dc_box, "DC Correction", Global_DC_Enable);
    draw_button(renderer, font, sel_button, "SELECTOR", Global_Selector.enabled, 0);
    draw_button(renderer, font, rec_button, "RECORD", Global_Rec, 1);
}

static void draw_frequency_axis(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect waterfall_rect) {
    int axis_y = waterfall_rect.y + waterfall_rect.h + 14;

    SDL_SetRenderDrawColor(renderer, 0, 220, 70, 255);
    SDL_RenderDrawLine(renderer, waterfall_rect.x, axis_y, waterfall_rect.x + waterfall_rect.w, axis_y);

    double start_hz = Global_Center_Freq_Hz - Global_Display_Span_Hz / 2.0;
    double span_hz = Global_Display_Span_Hz;

    int ticks = 10;

    for (int t = 0; t <= ticks; t++) {
        double frac = (double)t / ticks;
        int x = waterfall_rect.x + (int)(frac * waterfall_rect.w);
        double freq_mhz = (start_hz + frac * span_hz) / 1e6;

        SDL_RenderDrawLine(renderer, x, axis_y - 8, x, axis_y + 8);

        char label[32];
        if (Global_Display_Span_Hz < 1000000) snprintf(label, sizeof(label), "%.4f", freq_mhz);
        else snprintf(label, sizeof(label), "%.3f", freq_mhz);

        int label_x = x - 24;

        if (label_x < waterfall_rect.x) label_x = waterfall_rect.x;
        if (label_x > waterfall_rect.x + waterfall_rect.w - 70) {
            label_x = waterfall_rect.x + waterfall_rect.w - 70;
        }

        draw_text(renderer, font, label, label_x, axis_y + 12, (SDL_Color){0, 220, 70, 255});
    }
}

static void draw_border(SDL_Renderer *renderer, SDL_Rect r) {
    draw_outline_rect(renderer, r, (SDL_Color){0, 180, 60, 255});
}

static void draw_selection_overlay(SDL_Renderer *renderer, SDL_Rect waterfall_rect) {
    if (!Global_Selector.enabled) return;

    double x0f = limit_double(Global_Selector.X0, 0.0, 1.0);
    double x1f = limit_double(Global_Selector.X1, 0.0, 1.0);

    if (x1f < x0f) {
        double tmp = x0f;
        x0f = x1f;
        x1f = tmp;
    }

    int x0 = waterfall_rect.x + (int)(x0f * waterfall_rect.w);
    int x1 = waterfall_rect.x + (int)(x1f * waterfall_rect.w);

    if (x1 <= x0) x1 = x0 + 1;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect sel_rect = {x0, waterfall_rect.y, x1 - x0, waterfall_rect.h};

    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 50);
    SDL_RenderFillRect(renderer, &sel_rect);

    SDL_SetRenderDrawColor(renderer, 230, 230, 230, 180);
    SDL_RenderDrawRect(renderer, &sel_rect);

    int mid = (x0 + x1) / 2;

    SDL_SetRenderDrawColor(renderer, 255, 25, 20, 240);
    SDL_RenderDrawLine(renderer, mid, waterfall_rect.y, mid, waterfall_rect.y + waterfall_rect.h);

    SDL_Rect left_handle = {x0 - 3, waterfall_rect.y, 6, waterfall_rect.h};
    SDL_Rect right_handle = {x1 - 3, waterfall_rect.y, 6, waterfall_rect.h};

    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 145);
    SDL_RenderFillRect(renderer, &left_handle);
    SDL_RenderFillRect(renderer, &right_handle);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void draw_selector_bandwidth(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect waterfall_rect) {
    if (!Global_Selector.enabled) return;

    uint32_t bw_hz = selection_BW_Hz();
    uint64_t center_hz = selection_center_Hz();

    char msg[128];

    if (bw_hz >= 1000000) {
        snprintf(
            msg,
            sizeof(msg),
            "Selector: %.6f MHz | BW: %.3f MHz",
            center_hz / 1e6,
            bw_hz / 1e6
        );
    } else {
        snprintf(
            msg,
            sizeof(msg),
            "Selector: %.6f MHz | BW: %.3f kHz",
            center_hz / 1e6,
            bw_hz / 1e3
        );
    }

    int text_w = 0;
    int text_h = 0;

    if (font && TTF_SizeText(font, msg, &text_w, &text_h) != 0) {
        text_w = 0;
        text_h = 0;
    }

    int x = waterfall_rect.x + 12;
    int y = waterfall_rect.y + 12;

    SDL_Rect bg = {
        x - 6,
        y - 4,
        text_w + 12,
        text_h + 8
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, bg, (SDL_Color){0, 0, 0, 180});
    draw_outline_rect(renderer, bg, (SDL_Color){0, 180, 60, 220});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    draw_text(renderer, font, msg, x, y, (SDL_Color){0, 255, 90, 255});
}

static void update_selection_from_mouse(int mouse_x, SDL_Rect waterfall_rect) {
    double frac = (double)(mouse_x - waterfall_rect.x) / (double)waterfall_rect.w;
    frac = limit_double(frac, 0.0, 1.0);

    if (Global_Selector.resizing_left) {
        Global_Selector.X0 = frac;

        if (Global_Selector.X0 > Global_Selector.X1 - 0.002) {
            Global_Selector.X0 = Global_Selector.X1 - 0.002;
        }

        if (Global_Selector.X0 < 0.0) {
            Global_Selector.X0 = 0.0;
        }
    } else if (Global_Selector.resizing_right) {
        Global_Selector.X1 = frac;

        if (Global_Selector.X1 < Global_Selector.X0 + 0.002) {
            Global_Selector.X1 = Global_Selector.X0 + 0.002;
        }

        if (Global_Selector.X1 > 1.0) {
            Global_Selector.X1 = 1.0;
        }
    } else if (Global_Selector.dragging) {
        double width = Global_Selector.X1 - Global_Selector.X0;
        double new_x0 = frac - width * 0.5;
        double new_x1 = frac + width * 0.5;

        if (new_x0 < 0.0) {
            new_x1 -= new_x0;
            new_x0 = 0.0;
        }

        if (new_x1 > 1.0) {
            double excess = new_x1 - 1.0;
            new_x0 -= excess;
            new_x1 = 1.0;
        }

        Global_Selector.X0 = limit_double(new_x0, 0.0, 1.0);
        Global_Selector.X1 = limit_double(new_x1, 0.0, 1.0);
    }
}

static void draw_antenna_recommendation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    double length_in = recommended_antenna_length_inches(Global_Center_Freq_Hz);

    char msg[96];
    snprintf(msg, sizeof(msg), "Recommended antenna length: %.2f in", length_in);

    int text_w = 0;
    int text_h = 0;

    if (font && TTF_SizeText(font, msg, &text_w, &text_h) != 0) {
        text_w = 0;
        text_h = 0;
    }

    int x = win_w - text_w - 24;
    int y = win_h - 24;

    draw_text(renderer, font, msg, x, y, (SDL_Color){0, 220, 70, 255});
}

static int apply_from_inputs(
    hackrf_device *dev,
    Type_Input_Box *freq_box,
    Type_Input_Box *sr_box,
    Type_Input_Box *display_box,
    Type_Input_Box *lna_box,
    Type_Input_Box *vga_box,
    Type_Input_Box *fps_box,
    Type_Input_Box *rows_box,
    uint32_t *waterfall_pixels,
    int tex_w,
    int tex_h
) {
    double freq_mhz = 0.0;
double sr_msps = 0.0;
    double display_mhz = 0.0;
    int lna = 0, vga = 0, fps = 0, rows = 0;

    if (!parse_positive_double(freq_box->text, &freq_mhz)) return 0;
    if (!parse_positive_double(sr_box->text, &sr_msps)) return 0;
    if (!parse_positive_double(display_box->text, &display_mhz)) return 0;
    if (!parse_nonnegative_int(lna_box->text, &lna)) return 0;
    if (!parse_nonnegative_int(vga_box->text, &vga)) return 0;
    if (!parse_nonnegative_int(fps_box->text, &fps)) return 0;
    if (!parse_nonnegative_int(rows_box->text, &rows)) return 0;

    uint64_t center_hz = (uint64_t)(freq_mhz * 1e6);
    uint32_t sample_rate_hz = (uint32_t)(sr_msps * 1e6);
    uint32_t display_span_hz = (uint32_t)(display_mhz * 1e6);

    if (sample_rate_hz < 2000000 || sample_rate_hz > 20000000) return 0;

    if (display_span_hz > sample_rate_hz) display_span_hz = sample_rate_hz;
    if (display_span_hz < 1000) display_span_hz = 1000;

    lna = normalize_lna_gain(lna);
    vga = normalize_vga_gain(vga);
    fps = normalize_fps(fps);
    rows = normalize_rows_per_frame(rows);

    if (!apply_radio_settings(dev, center_hz, sample_rate_hz, display_span_hz, lna, vga, Global_Amp_Enable)) {
        return 0;
    }

    Global_Waterfall_FPS = fps;
    Global_Rows_Per_Frame = rows;

    snprintf(freq_box->text, sizeof(freq_box->text), "%.3f", Global_Center_Freq_Hz / 1e6);
    snprintf(sr_box->text, sizeof(sr_box->text), "%.3f", Global_Sample_Rate_Hz / 1e6);
    snprintf(display_box->text, sizeof(display_box->text), "%.3f", Global_Display_Span_Hz / 1e6);
    snprintf(lna_box->text, sizeof(lna_box->text), "%d", Global_LNA_Gain);
    snprintf(vga_box->text, sizeof(vga_box->text), "%d", Global_VGA_Gain);
    snprintf(fps_box->text, sizeof(fps_box->text), "%d", Global_Waterfall_FPS);
    snprintf(rows_box->text, sizeof(rows_box->text), "%d", Global_Rows_Per_Frame);

    clear_waterfall(waterfall_pixels, tex_w, tex_h);
    reset_prev_col_db(tex_w);

    set_status("", (SDL_Color){0, 255, 80, 255});
    return 1;
}

int main(void){

    signal(SIGINT, handle_sigint);

    memset(&ring_buf, 0, sizeof(ring_buf));

    pthread_mutex_init(&ring_buf.lock, NULL);

    if (!pre_cache_init(&Global_Pre_Cache, Global_Sample_Rate_Hz)) {

        fprintf(stderr, "pre-cache allocation failed\n");
        return 1;

    }

    hackrf_device *dev = NULL;

    if(hackrf_init() != HACKRF_SUCCESS){

        fprintf(stderr, "hackrf_init failed\n");
        return 1;

    }

    if (hackrf_open(&dev) != HACKRF_SUCCESS) {
        fprintf(stderr, "hackrf_open failed\n");
        hackrf_exit();
        return 1;
    }

    if (!apply_radio_settings(dev, Global_Center_Freq_Hz, Global_Sample_Rate_Hz, 
                              Global_Display_Span_Hz, Global_LNA_Gain, Global_VGA_Gain, 
                              Global_Amp_Enable)) {

        fprintf(stderr, "initial HackRF configuration failed\n");
        hackrf_close(dev);
        hackrf_exit();
        return 1;
    }

    fftw_complex *time_domain = fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    fftw_complex *freq_domain = fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);

    double *hann_window = malloc(sizeof(double) * FFT_SIZE);
    double *db = malloc(sizeof(double) * FFT_SIZE);

    if (!time_domain || !freq_domain || !hann_window || !db) {
        fprintf(stderr, "allocation failed\n");
        stop_radio(dev);
        hackrf_close(dev);
        hackrf_exit();
        return 1;
    }

    for (int n = 0; n < FFT_SIZE; n++){
        hann_window[n] = 0.5 - 0.5 * cos((2.0 * M_PI * n) / (FFT_SIZE - 1));
    }

    fftw_plan plan = fftw_plan_dft_1d(FFT_SIZE, time_domain, freq_domain, FFTW_FORWARD, FFTW_MEASURE);

    if (!plan){

        fprintf(stderr, "fftw plan creation failed\n");
        stop_radio(dev);
        hackrf_close(dev);
        hackrf_exit();
        return 1;
    
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return 1;
    }

    SDL_Window *window_sdl = SDL_CreateWindow(
        "HackRF",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1400,
        820,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window_sdl) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetWindowMinimumSize(window_sdl, MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

    SDL_Renderer *renderer = SDL_CreateRenderer(window_sdl, -1, SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }

    int tex_w = 1120;
    int tex_h = 540;

    SDL_Texture *waterfall_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        tex_w,
        tex_h
    );

    if (!waterfall_texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 1;
    }

    uint32_t *pixels = malloc(sizeof(uint32_t) * tex_w * tex_h);
    if (!pixels) {
        fprintf(stderr, "pixel allocation failed\n");
        return 1;
    }

    clear_waterfall(pixels, tex_w, tex_h);

    Global_Color_Baseline = malloc(sizeof(double) * tex_w);
    if (!Global_Color_Baseline) {
        fprintf(stderr, "prev-column allocation failed\n");
        return 1;
    }

    reset_prev_col_db(tex_w);

    TTF_Font *font_small = load_font(14);
    TTF_Font *font_medium = load_font(16);

    SDL_StartTextInput();

    Type_Input_Box freq_box = {.label = "Center MHz", .id = FIELD_FREQ};
    snprintf(freq_box.text, sizeof(freq_box.text), "%.3f", Global_Center_Freq_Hz / 1e6);

    Type_Input_Box sr_box = {.label = "Sample MS/s", .id = FIELD_SR};
    snprintf(sr_box.text, sizeof(sr_box.text), "%.3f", Global_Sample_Rate_Hz / 1e6);

    Type_Input_Box display_box = {.label = "Display MHz", .id = FIELD_DISPLAY};
    snprintf(display_box.text, sizeof(display_box.text), "%.3f", Global_Display_Span_Hz / 1e6);

    Type_Input_Box lna_box = {.label = "LNA", .id = FIELD_LNA};
    snprintf(lna_box.text, sizeof(lna_box.text), "%d", Global_LNA_Gain);

    Type_Input_Box vga_box = {.label = "VGA", .id = FIELD_VGA};
    snprintf(vga_box.text, sizeof(vga_box.text), "%d", Global_VGA_Gain);

    Type_Input_Box fps_box = {.label = "FPS", .id = FIELD_FPS};
    snprintf(fps_box.text, sizeof(fps_box.text), "%d", Global_Waterfall_FPS);

    Type_Input_Box rows_box = {.label = "Rows/Frame", .id = FIELD_ROWS};
    snprintf(rows_box.text, sizeof(rows_box.text), "%d", Global_Rows_Per_Frame);

    Type_Active_Fields active = FIELD_NONE;
    uint64_t next_waterfall_ms = SDL_GetTicks64();


    while (Global_Running) {
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(window_sdl, &win_w, &win_h);

        SDL_Rect amp_box;
        SDL_Rect dc_box;
        SDL_Rect sel_button;
        SDL_Rect rec_button;

        layout_controls(
            win_w,
            &freq_box,
            &sr_box,
            &display_box,
            &lna_box,
            &vga_box,
            &fps_box,
            &rows_box,
            &amp_box,
            &dc_box,
            &sel_button,
            &rec_button
        );

        int waterfall_x = MARGIN;
        int waterfall_y = CONTROL_PANEL_HEIGHT + 12;
        int waterfall_w = win_w - 2 * MARGIN;
        int waterfall_h = win_h - waterfall_y - AXIS_HEIGHT - 25;

        if (waterfall_h < 100) waterfall_h = 100;

        SDL_Rect waterfall_rect = {waterfall_x, waterfall_y, waterfall_w, waterfall_h};

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) Global_Running = 0;

            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    if (active != FIELD_NONE) active = FIELD_NONE;
                    else Global_Running = 0;
                } else if (event.key.keysym.sym == SDLK_TAB) {
                    if (active == FIELD_NONE) active = FIELD_FREQ;
                    else if (active == FIELD_FREQ) active = FIELD_SR;
                    else if (active == FIELD_SR) active = FIELD_DISPLAY;
                    else if (active == FIELD_DISPLAY) active = FIELD_LNA;
                    else if (active == FIELD_LNA) active = FIELD_VGA;
                    else if (active == FIELD_VGA) active = FIELD_FPS;
                    else if (active == FIELD_FPS) active = FIELD_ROWS;
                    else active = FIELD_FREQ;
                } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    if (active == FIELD_FREQ) backspace_text(freq_box.text);
                    else if (active == FIELD_SR) backspace_text(sr_box.text);
                    else if (active == FIELD_DISPLAY) backspace_text(display_box.text);
                    else if (active == FIELD_LNA) backspace_text(lna_box.text);
                    else if (active == FIELD_VGA) backspace_text(vga_box.text);
                    else if (active == FIELD_FPS) backspace_text(fps_box.text);
                    else if (active == FIELD_ROWS) backspace_text(rows_box.text);
                } else if (
                    event.key.keysym.sym == SDLK_RETURN ||
                    event.key.keysym.sym == SDLK_KP_ENTER
                ) {
                    apply_from_inputs(
                        dev,
                        &freq_box,
                        &sr_box,
                        &display_box,
                        &lna_box,
                        &vga_box,
                        &fps_box,
                        &rows_box,
                        pixels,
                        tex_w,
                        tex_h
                    );

                    next_waterfall_ms = SDL_GetTicks64();
                } else if (event.key.keysym.sym == SDLK_q && active == FIELD_NONE) {
                    Global_Running = 0;
                }
            }

            if (event.type == SDL_TEXTINPUT) {

                if (active == FIELD_FREQ) append_text(freq_box.text, sizeof(freq_box.text), 
                                                      event.text.text);

                else if (active == FIELD_SR) append_text(sr_box.text, sizeof(sr_box.text), 
                                                         event.text.text);

                else if (active == FIELD_DISPLAY) append_text(display_box.text, 
                                                              sizeof(display_box.text), 
                                                              event.text.text);

                else if (active == FIELD_LNA) append_text(lna_box.text, sizeof(lna_box.text), 
                                                          event.text.text);

                else if (active == FIELD_VGA) append_text(vga_box.text, sizeof(vga_box.text), 
                                                          event.text.text);

                else if (active == FIELD_FPS) append_text(fps_box.text, sizeof(fps_box.text), 
                                                          event.text.text);

                else if (active == FIELD_ROWS) append_text(rows_box.text, sizeof(rows_box.text), 
                                                           event.text.text);

            }

            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int x = event.button.x;
                int y = event.button.y;

                if (point_in_rect(x, y, freq_box.rect)) active = FIELD_FREQ;
                else if (point_in_rect(x, y, sr_box.rect)) active = FIELD_SR;
                else if (point_in_rect(x, y, display_box.rect)) active = FIELD_DISPLAY;
                else if (point_in_rect(x, y, lna_box.rect)) active = FIELD_LNA;
                else if (point_in_rect(x, y, vga_box.rect)) active = FIELD_VGA;
                else if (point_in_rect(x, y, fps_box.rect)) active = FIELD_FPS;
                else if (point_in_rect(x, y, rows_box.rect)) active = FIELD_ROWS;
                else if (point_in_rect(x, y, amp_box)) {
                    Global_Amp_Enable = !Global_Amp_Enable;

                    apply_from_inputs(
                        dev,
                        &freq_box,
                        &sr_box,
                        &display_box,
                        &lna_box,
                        &vga_box,
                        &fps_box,
                        &rows_box,
                        pixels,
                        tex_w,
                        tex_h
                    );

                    next_waterfall_ms = SDL_GetTicks64();
                } 

                else if (point_in_rect(x, y, dc_box)) {
                    Global_DC_Enable = !Global_DC_Enable;
                    Global_DC_I = 0.0;
                    Global_DC_Q = 0.0;
                } 

                else if (point_in_rect(x, y, sel_button)) {
                    active = FIELD_NONE;

                    if (!Global_Rec) {
                        Global_Selector.enabled = !Global_Selector.enabled;
                    } else {
                        set_status("Selector locked while recording", (SDL_Color){255, 180, 40, 255});
                    }
                }

                else if (point_in_rect(x, y, rec_button)) {
                    active = FIELD_NONE;

                    if (Global_Rec) {
                        stop_recording();
                    } else if (Global_Selector.enabled) {
                        start_recording();
                    }
                } 

                else if (!Global_Rec && Global_Selector.enabled && point_in_rect(x, y, waterfall_rect)) {
                    active = FIELD_NONE;

                    int x0 = waterfall_rect.x + (int)(Global_Selector.X0 * waterfall_rect.w);
                    int x1 = waterfall_rect.x + (int)(Global_Selector.X1 * waterfall_rect.w);

                    if (near_px(x, x0, 8)) {
                        Global_Selector.resizing_left = 1;
                    } 
                    
                    else if (near_px(x, x1, 8)) {
                        Global_Selector.resizing_right = 1;
                    } 

                    else if (x > x0 && x < x1) {
                        Global_Selector.dragging = 1;
                    }
                } 

                else {
                    active = FIELD_NONE;
                }
            }

            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {

                Global_Selector.dragging = 0;
                Global_Selector.resizing_left = 0;
                Global_Selector.resizing_right = 0;

            }

            if (event.type == SDL_MOUSEMOTION) {
              
              if (!Global_Rec && Global_Selector.enabled && (Global_Selector.dragging || Global_Selector.resizing_left || 
                    Global_Selector.resizing_right)) {

                  update_selection_from_mouse(event.motion.x, waterfall_rect);
                }

            }

        }

        uint64_t now_ms = SDL_GetTicks64();
        uint64_t frame_interval_ms = 1000 / (uint64_t)normalize_fps(Global_Waterfall_FPS);

        if (frame_interval_ms < 1) frame_interval_ms = 1;

        if (now_ms >= next_waterfall_ms) {

            int rows_drawn = 0;
            int target_rows = normalize_rows_per_frame(Global_Rows_Per_Frame);

            while (rows_drawn < target_rows && ring_read_block(&ring_buf, time_domain, hann_window)) {
                fftw_execute(plan);
                compute_DB_from_FFT(freq_domain, db);
                add_fft_line_to_waterfall(pixels, tex_w, tex_h, db);
                rows_drawn++;
            }

            if (rows_drawn > 0) {

                SDL_UpdateTexture(waterfall_texture, NULL, pixels, tex_w * sizeof(uint32_t));

            }

            next_waterfall_ms = now_ms + frame_interval_ms;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        draw_control_panel(
            renderer,
            font_medium,
            win_w,
            &freq_box,
            &sr_box,
            &display_box,
            &lna_box,
            &vga_box,
            &fps_box,
            &rows_box,
            amp_box,
            dc_box,
            sel_button,
            rec_button,
            active
        );

        SDL_RenderCopy(renderer, waterfall_texture, NULL, &waterfall_rect);
        draw_selection_overlay(renderer, waterfall_rect);
        draw_selector_bandwidth(renderer, font_small, waterfall_rect);
        draw_border(renderer, waterfall_rect);
        draw_frequency_axis(renderer, font_small, waterfall_rect);

        draw_text(renderer, font_medium, Global_Status_Msg, MARGIN, win_h - 24, Global_Status_Color);

        draw_antenna_recommendation(renderer, font_small, win_w, win_h);

        draw_made_in_usa(renderer, font_small, win_w);

        SDL_RenderPresent(renderer);

        SDL_Delay(1);
    }

    SDL_StopTextInput();

    stop_recording();

    if (Global_Radio_Running) hackrf_stop_rx(dev);

    hackrf_close(dev);
    hackrf_exit();

    if (font_small) TTF_CloseFont(font_small);
    if (font_medium) TTF_CloseFont(font_medium);

    SDL_DestroyTexture(waterfall_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window_sdl);

    TTF_Quit();
    SDL_Quit();

    fftw_destroy_plan(plan);
    fftw_free(time_domain);
    fftw_free(freq_domain);

    free(hann_window);
    free(db);
    free(pixels);
    free(Global_Color_Baseline);

    pre_cache_free(&Global_Pre_Cache);
    pthread_mutex_destroy(&ring_buf.lock);

    return 0;

}

