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
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// HackRF Library
#include <libhackrf/hackrf.h>

// FFT Library
#include <fftw3.h>

// SDL (GUI) Library
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

// ============
// Header Files
// ============

// Responsible for GUI objects
#include "GUIs.h"

// Responsible for startup authentication
#include "AuthScreen.h"
#include "SecureNetwork.h"
#include "ServerIdentity.h"

// Responsible for IQ objects
#include "IQs.h"

// Responsible for the ClassificationWorkstation
#include "ClassificationWorkstation.h"

// Responsible for the CaseManagementWorkstation
#include "CaseManagementWorkstation.h"

// Responsible for the AnalysisWorkstation
#include "AnalysisWorkstation.h"

// Responsible for the DecodeWorkstation
#include "DecodeWorkstation.h"

// Responsible for the Map Dashboard
#include "MapDashboard.h"

// GUI functions implemented in GUIs.c and called from this main logic file

/* Runtime mode setters implemented by the authentication and identity modules. */
void AUTH_set_client_only_mode(int client_only);
void SERVER_IDENTITY_set_server_mode(int server_mode);

void add_fft_line_to_waterfall(uint32_t *pixels, int tex_w, int tex_h, double *db);

int ANALYSIS_export_classification_fields(char *file_name, size_t file_name_size, double *frequency_mhz,
                                          double *bandwidth_khz, double *start_time, double *end_time);

void CLASSIFICATION_prefill_from_analysis_selection(const char *file_name, double frequency_mhz, double bandwidth_khz,
                                                    double start_time, double end_time);

int CLASSIFICATION_is_text_entry_active(void);
int ANALYSIS_is_text_entry_active(void);
int DECODE_is_text_entry_active(void);

// Selects an explicit SQLCipher master-key file before authentication.
int DATABASE_CRYPTO_set_key_path(const char *path, char *error, size_t error_size);

// Reports whether this process currently owns a live secure LAN server listener.
int SECURE_NETWORK_server_is_running(void);

// Reports and consumes an authenticated remote-server connection failure.
int SECURE_NETWORK_remote_connection_lost(void);

// ======================
// Global Initializations
// ======================

/*
        GLOBAL DEFINITIONS              VALUE
*/

#define DEFAULT_CENTER_FREQ_HZ 101300000ULL
#define DEFAULT_SAMPLE_RATE_HZ 2000000U
#define DEFAULT_DISPLAY_SPAN_HZ 1000000U
#define DEFAULT_LNA_GAIN 16
#define DEFAULT_VGA_GAIN 12
#define DEFAULT_AMP_ENABLE 0
#define DEFAULT_DC_CORRECTION_ENABLE 0
#define DEFAULT_WATERFALL_FPS 60
#define DEFAULT_ROWS_PER_FRAME 4
#define SERVER_CONNECTION_CHECK_MS 250

#define MIN_WINDOW_WIDTH 1320
#define MIN_WINDOW_HEIGHT 650

#define REL_MIN_DB 2.0
#define REL_MAX_DB 22.0

#define CONTROL_PANEL_HEIGHT 95
#define AXIS_HEIGHT 70
#define MARGIN 20

#define PRE_RECORD_SECONDS 5
#define REC_QUEUE_SECONDS (PRE_RECORD_SECONDS * 2)
#define REC_PUSH_CHUNK_SAMPLES 4096

#define REC_FIR_TAPS 255

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_TRANSFER_SAMPLES 262144

#define DEFAULT_RECORD_DIR "Recordings"

#define ANALYSIS_MAX_FILES 512
#define ANALYSIS_FFT_SIZE 2048
#define ANALYSIS_LIST_WIDTH 430
/* ANALYSIS_MAX_RENDER_W is defined in include/GUIs.h so the extern arrays match
 */
#define ANALYSIS_MAX_CONST_POINTS 4096

static volatile sig_atomic_t Global_Running = 1;

static pthread_mutex_t Global_Rec_Lock = PTHREAD_MUTEX_INITIALIZER;

/*

TYPE            VARIABLE                VALUE

*/

uint64_t Global_Rec_Center_Hz = 0;
uint64_t Global_Center_Freq_Hz = DEFAULT_CENTER_FREQ_HZ;
uint32_t Global_Sample_Rate_Hz = DEFAULT_SAMPLE_RATE_HZ;
uint32_t Global_Display_Span_Hz = DEFAULT_DISPLAY_SPAN_HZ;
int Global_Amp_Enable = DEFAULT_AMP_ENABLE;
int Global_DC_Enable = DEFAULT_DC_CORRECTION_ENABLE;
int Global_Fullscreen = 0;
int Global_Rec = 0;
char Global_Status_Msg[256] = "";

SDL_Color Global_Status_Color = {0, 255, 80, 255};

Type_Selector Global_Selector = {
    .X0 = 0.40, .X1 = 0.60, .enabled = 0, .dragging = 0, .resizing_left = 0, .resizing_right = 0};

/*

        TYPE            VARIABLE                VALUE

*/

static FILE *Global_Rec_File = NULL;
static uint32_t Global_Rec_BW_Hz = 0;
static uint32_t Global_Rec_Out_Rate_Hz = 0;
static int16_t *Global_Rec_Pre_I = NULL;
static int16_t *Global_Rec_Pre_Q = NULL;
double *Global_Color_Baseline = NULL;
static double Global_DC_I = 0.0;
static double Global_DC_Q = 0.0;
static double Global_Rec_Phase = 0.0;
static double Global_Rec_Acc_I = 0.0;
static double Global_Rec_Acc_Q = 0.0;
static size_t Global_Rec_Pre_Count = 0;
static int Global_Rec_FIR_Pos = 0;
static int Global_LNA_Gain = DEFAULT_LNA_GAIN;
static int Global_VGA_Gain = DEFAULT_VGA_GAIN;
static int Global_Waterfall_FPS = DEFAULT_WATERFALL_FPS;
static int Global_Rows_Per_Frame = DEFAULT_ROWS_PER_FRAME;
static int Global_Rec_Acc_Count = 0;
static int Global_Rec_Decimation = 1;
static int Global_Radio_Running = 0;
static int Global_HackRF_Library_Initialized = 0;
static int Global_HackRF_Connected = 0;
static hackrf_device *Global_HackRF_Device = NULL;

#define RETROSPECTRUM_TX_MAX_REPEATS 100U
#define RETROSPECTRUM_TX_CONVERT_CHUNK 8192U

typedef struct {
    pthread_mutex_t lock;
    FILE *file;
    uint64_t file_size_bytes;
    uint64_t bytes_consumed;
    uint64_t center_frequency_hz;
    uint32_t sample_rate_hz;
    uint32_t requested_bandwidth_hz;
    uint32_t actual_bandwidth_hz;
    int tx_gain_db;
    unsigned int repeat_count;
    unsigned int total_passes;
    unsigned int current_pass;
    int active;
    int cancel_requested;
    int callback_finish_armed;
    int callback_done;
    int callback_failed;
    int result_ready;
    int result_succeeded;
    uint64_t saved_center_frequency_hz;
    uint32_t saved_sample_rate_hz;
    uint32_t saved_display_span_hz;
    int saved_lna_gain;
    int saved_vga_gain;
    int saved_amp_enable;
    char result_message[256];
} Type_Transmit_State;

static Type_Transmit_State Global_Transmit_State = {
    .lock = PTHREAD_MUTEX_INITIALIZER, .file = NULL, .active = 0, .result_ready = 0};

static int Global_Cached_Recording = 0;
static char Global_Record_Dir[512] = DEFAULT_RECORD_DIR;
static int Global_CLI_Mode = 0;
static int Global_Help_Requested = 0;
static char Global_Database_Key_Path[4096] = "";

enum Type_Network_Mode { NETWORK_MODE_UNSET = 0, NETWORK_MODE_SERVER, NETWORK_MODE_CLIENT };

static enum Type_Network_Mode Global_Network_Mode = NETWORK_MODE_UNSET;

static double Global_Rec_FIR[REC_FIR_TAPS];
static double Global_Rec_Hist_I[REC_FIR_TAPS];
static double Global_Rec_Hist_Q[REC_FIR_TAPS];
static float temp_I[MAX_TRANSFER_SAMPLES];
static float temp_Q[MAX_TRANSFER_SAMPLES];

static Type_RingBuf ring_buf;
static Type_Rec_Cache Global_Pre_Cache;
static Type_Rec_Queue Global_Rec_Queue;

static pthread_t Global_Rec_Thread;
static int Global_Rec_Thread_Running = 0;

// =========
// Functions
// =========

// OS Signal Handling

static void handle_sigint(int sig) {
    /*

    Purpose: Handles SIGINT shutdown requests

    Return: No return

    */

    (void)sig;
    Global_Running = 0;
}

static void draw_thick_line(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, int thickness) {
    /*
        Purpose: Draws the thick line
        Returns: No value
    */

    int half = thickness / 2;

    for (int offset = -half; offset <= half; offset++) {
        SDL_RenderDrawLine(renderer, x1 + offset, y1, x2 + offset, y2);
        SDL_RenderDrawLine(renderer, x1, y1 + offset, x2, y2 + offset);
    }
}

static void draw_cubic_cable(SDL_Renderer *renderer, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3,
                             int thickness, SDL_Color color) {
    /*
        Purpose: Draws the cubic cable
        Returns: No value
    */

    int previous_x = x0;
    int previous_y = y0;

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int step = 1; step <= 48; step++) {
        double t = (double)step / 48.0;
        double u = 1.0 - t;
        double x = (u * u * u * x0) + (3.0 * u * u * t * x1) + (3.0 * u * t * t * x2) + (t * t * t * x3);
        double y = (u * u * u * y0) + (3.0 * u * u * t * y1) + (3.0 * u * t * t * y2) + (t * t * t * y3);
        int current_x = (int)lrint(x);
        int current_y = (int)lrint(y);

        draw_thick_line(renderer, previous_x, previous_y, current_x, current_y, thickness);
        previous_x = current_x;
        previous_y = current_y;
    }
}

static void draw_hackrf_disconnected(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the HackRF disconnected indicator
        Returns: No value
    */

    const SDL_Color red = {255, 48, 48, 255};
    const SDL_Color dark_red = {145, 18, 18, 255};
    const SDL_Color dim_red = {90, 12, 12, 255};
    const char *label = "HackRF One Not Connected";
    int label_w = 0;
    int label_h = 0;
    int center_x = win_w / 2;
    int icon_top = (win_h / 2) - 142;

    /* Curved disconnected cable, modeled after a physical USB lead. */
    draw_cubic_cable(renderer, center_x, icon_top + 104, center_x + 3, icon_top + 143, center_x - 20, icon_top + 181,
                     center_x - 72, icon_top + 213, 13, dark_red);
    draw_cubic_cable(renderer, center_x, icon_top + 104, center_x + 3, icon_top + 143, center_x - 20, icon_top + 181,
                     center_x - 72, icon_top + 213, 5, red);

    /* USB-A metal end. */
    SDL_Rect plug_fill = {center_x - 23, icon_top, 46, 42};
    SDL_Rect plug_inner = {center_x - 18, icon_top + 5, 36, 32};
    SDL_SetRenderDrawColor(renderer, dim_red.r, dim_red.g, dim_red.b, dim_red.a);
    SDL_RenderFillRect(renderer, &plug_fill);
    SDL_SetRenderDrawColor(renderer, red.r, red.g, red.b, red.a);
    for (int inset = 0; inset < 4; inset++) {
        SDL_Rect outline = {plug_fill.x + inset, plug_fill.y + inset, plug_fill.w - (inset * 2),
                            plug_fill.h - (inset * 2)};
        SDL_RenderDrawRect(renderer, &outline);
    }
    SDL_SetRenderDrawColor(renderer, dark_red.r, dark_red.g, dark_red.b, dark_red.a);
    SDL_RenderFillRect(renderer, &plug_inner);

    SDL_Rect contact_left = {center_x - 14, icon_top + 13, 10, 9};
    SDL_Rect contact_right = {center_x + 4, icon_top + 13, 10, 9};
    SDL_SetRenderDrawColor(renderer, red.r, red.g, red.b, red.a);
    SDL_RenderFillRect(renderer, &contact_left);
    SDL_RenderFillRect(renderer, &contact_right);

    /* Connector housing and cable strain relief. */
    SDL_Rect housing = {center_x - 34, icon_top + 39, 68, 64};
    SDL_Rect strain_relief = {center_x - 12, icon_top + 99, 24, 17};
    SDL_SetRenderDrawColor(renderer, dim_red.r, dim_red.g, dim_red.b, dim_red.a);
    SDL_RenderFillRect(renderer, &housing);
    SDL_RenderFillRect(renderer, &strain_relief);
    SDL_SetRenderDrawColor(renderer, red.r, red.g, red.b, red.a);
    for (int inset = 0; inset < 4; inset++) {
        SDL_Rect housing_outline = {housing.x + inset, housing.y + inset, housing.w - (inset * 2),
                                    housing.h - (inset * 2)};
        SDL_RenderDrawRect(renderer, &housing_outline);
    }
    SDL_RenderDrawRect(renderer, &strain_relief);

    /* Keep the existing prominent red X over the connector. */
    draw_thick_line(renderer, center_x - 45, icon_top - 3, center_x + 45, icon_top + 105, 5);
    draw_thick_line(renderer, center_x + 45, icon_top - 3, center_x - 45, icon_top + 105, 5);

    if (font && TTF_SizeText(font, label, &label_w, &label_h) == 0) {

        draw_text(renderer, font, label, center_x - (label_w / 2), icon_top + 238, red);

    }
}

// Hard Bounds

double limit_double(double value, double low, double high) {
    /*
        Purpose: Clamps a double value between lower and upper bounds
        Returns: Clamped value
    */

    if (value < low) {

        return low;

    }

    if (value > high) {

        return high;

    }

    return value;
}

// Target Path Validation and Creation

static int ensure_record_dir_exists(void) {
    /*
        Purpose: Ensures the recording directory exists and is usable
        Returns: Directory status
    */

    struct stat st;

    if (stat(Global_Record_Dir, &st) == 0) {

        if (S_ISDIR(st.st_mode)) {

            return 1;

        }

        return 0;

    }

    if (mkdir(Global_Record_Dir, 0755) == 0) {

        return 1;

    }

    return 0;
}

// Selector Helpers

uint64_t selection_center_Hz(void) {
    /*
        Purpose: Computes the selected recording center frequency in Hz
        Returns: Center frequency
    */

    double Center_Frac = (Global_Selector.X0 + Global_Selector.X1) * 0.5;

    double Offset_Hz = (Center_Frac - 0.5) * (double)Global_Display_Span_Hz;

    double Calc_Freq = (double)Global_Center_Freq_Hz + Offset_Hz;

    if (Calc_Freq < 0.0) {

        Calc_Freq = 0.0;

    }

    return (uint64_t)Calc_Freq;
}

uint32_t selection_BW_Hz(void) {
    /*
        Purpose: Computes the selected recording bandwidth in Hz
        Returns: Bandwidth value
    */

    double BW = fabs(Global_Selector.X1 - Global_Selector.X0) * (double)Global_Display_Span_Hz;

    if (BW < 1000.0) {

        BW = 1000.0;

    }

    if (BW > (double)Global_Sample_Rate_Hz) {

        BW = (double)Global_Sample_Rate_Hz;

    }

    return (uint32_t)BW;
}

// RF Filter

static void configure_recording_filter(void) {
    /*
        Purpose: Configures the FIR filter and decimation used by recording
        Returns: No value
    */

    memset(Global_Rec_FIR, 0, sizeof(Global_Rec_FIR));
    memset(Global_Rec_Hist_I, 0, sizeof(Global_Rec_Hist_I));
    memset(Global_Rec_Hist_Q, 0, sizeof(Global_Rec_Hist_Q));

    Global_Rec_FIR_Pos = 0;
    Global_Rec_Acc_Count = 0;

    // Output rate should be comfortably above selected bandwidth
    // 2.5x gives room for FIR transition

    double wanted_out_rate = (double)Global_Rec_BW_Hz * 3;

    if (wanted_out_rate < 48000.0) {

        wanted_out_rate = 48000.0;

    }

    Global_Rec_Decimation = (int)((double)Global_Sample_Rate_Hz / wanted_out_rate);

    if (Global_Rec_Decimation < 1) {

        Global_Rec_Decimation = 1;

    }

    Global_Rec_Out_Rate_Hz = Global_Sample_Rate_Hz / (uint32_t)Global_Rec_Decimation;

    // After shifting selected center to 0 Hz, selected bandwidth is -BW/2 to
    // +BW/2

    double cutoff_hz = (double)Global_Rec_BW_Hz * 0.5;

    // Keep cutoff below decimated Nyquist

    double max_safe_cutoff = (double)Global_Rec_Out_Rate_Hz * 0.45;

    if (cutoff_hz > max_safe_cutoff) {

        cutoff_hz = max_safe_cutoff;

    }

    // Normalized cutoff relative to input sample rate

    double fc = cutoff_hz / (double)Global_Sample_Rate_Hz;

    double sum = 0.0;
    int mid = REC_FIR_TAPS / 2;

    for (int n = 0; n < REC_FIR_TAPS; n++) {
        int m = n - mid;

        double sinc;

        if (m == 0) {

            sinc = 2.0 * fc;

        }

        else {

            sinc = sin(2.0 * M_PI * fc * (double)m) / (M_PI * (double)m);

        }

        // Hamming window

        double window = 0.54 - 0.46 * cos((2.0 * M_PI * (double)n) / (double)(REC_FIR_TAPS - 1));

        Global_Rec_FIR[n] = sinc * window;
        sum += Global_Rec_FIR[n];
    }

    // Normalize gain to 1.0

    if (fabs(sum) > 1e-12) {

        for (int n = 0; n < REC_FIR_TAPS; n++) {
            Global_Rec_FIR[n] /= sum;
        }

    }
}

// Cache Helpers

static int pre_cache_init(Type_Rec_Cache *c, uint32_t sample_rate_hz) {
    /*
        Purpose: Initializes the pre-record IQ cache
        Returns: Init status
    */

    memset(c, 0, sizeof(*c));

    c->capacity = (size_t)sample_rate_hz * PRE_RECORD_SECONDS;

    c->I = malloc(sizeof(int16_t) * c->capacity);
    c->Q = malloc(sizeof(int16_t) * c->capacity);

    if (!c->I || !c->Q) {

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

static void pre_cache_free(Type_Rec_Cache *c) {
    /*
        Purpose: Frees the pre-record IQ cache
        Returns: No value
    */

    pthread_mutex_destroy(&c->lock);

    free(c->I);
    free(c->Q);

    memset(c, 0, sizeof(*c));
}

static int pre_cache_resize(Type_Rec_Cache *c, uint32_t sample_rate_hz) {
    /*
        Purpose: Resizes the pre-record IQ cache for a new sample rate
        Returns: Resize status
    */

    pthread_mutex_lock(&c->lock);

    free(c->I);
    free(c->Q);

    c->capacity = (size_t)sample_rate_hz * PRE_RECORD_SECONDS;
    c->write_pos = 0;
    c->count = 0;

    c->I = malloc(sizeof(int16_t) * c->capacity);
    c->Q = malloc(sizeof(int16_t) * c->capacity);

    int status = (c->I && c->Q);

    if (!status) {

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

static void pre_cache_write(Type_Rec_Cache *c, float I, float Q) {
    /*
        Purpose: Writes one IQ sample into the pre-record cache
        Returns: No value
    */

    if (!c->I || !c->Q || c->capacity == 0) {

        return;

    }

    if (I > 1.0f) {

        I = 1.0f;

    }

    if (I < -1.0f) {

        I = -1.0f;

    }

    if (Q > 1.0f) {

        Q = 1.0f;

    }

    if (Q < -1.0f) {

        Q = -1.0f;

    }

    c->I[c->write_pos] = (int16_t)(I * 32767.0f);
    c->Q[c->write_pos] = (int16_t)(Q * 32767.0f);

    c->write_pos = (c->write_pos + 1) % c->capacity;

    if (c->count < c->capacity) {

        c->count++;

    }
}

static size_t pre_cache_snapshot_locked(Type_Rec_Cache *c, int16_t **out_I, int16_t **out_Q) {
    /*
        Purpose: Copies the current pre-record cache while already locked
        Returns: Snapshot count
    */

    *out_I = NULL;
    *out_Q = NULL;

    size_t count = c->count;

    if (count == 0 || !c->I || !c->Q) {

        return 0;

    }

    int16_t *copy_I = malloc(sizeof(int16_t) * count);
    int16_t *copy_Q = malloc(sizeof(int16_t) * count);

    if (!copy_I || !copy_Q) {

        free(copy_I);
        free(copy_Q);
        return 0;

    }

    if (c->count < c->capacity) {

        memcpy(copy_I, c->I, sizeof(int16_t) * count);
        memcpy(copy_Q, c->Q, sizeof(int16_t) * count);

    }

    else {

        size_t start = c->write_pos;
        size_t first = c->capacity - start;
        size_t second = start;

        memcpy(copy_I, c->I + start, sizeof(int16_t) * first);
        memcpy(copy_Q, c->Q + start, sizeof(int16_t) * first);

        memcpy(copy_I + first, c->I, sizeof(int16_t) * second);
        memcpy(copy_Q + first, c->Q, sizeof(int16_t) * second);

    }

    *out_I = copy_I;
    *out_Q = copy_Q;

    return count;
}

// Queue Helpers

static int rec_queue_init(Type_Rec_Queue *q, uint32_t sample_rate_hz) {
    /*
        Purpose: Initializes the recording queue
        Returns: Init status
    */

    memset(q, 0, sizeof(*q));

    // +1 because this ring-buffer design leaves one slot empty

    q->capacity = ((size_t)sample_rate_hz * REC_QUEUE_SECONDS) + 1;

    q->I = malloc(sizeof(int16_t) * q->capacity);
    q->Q = malloc(sizeof(int16_t) * q->capacity);

    if (!q->I || !q->Q) {

        free(q->I);
        free(q->Q);
        memset(q, 0, sizeof(*q));
        return 0;

    }

    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->data_cond, NULL);

    return 1;
}

static void rec_queue_free(Type_Rec_Queue *q) {
    /*
        Purpose: Frees the recording queue
        Returns: No value
    */

    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->data_cond);

    free(q->I);
    free(q->Q);

    memset(q, 0, sizeof(*q));
}

static int rec_queue_resize(Type_Rec_Queue *q, uint32_t sample_rate_hz) {
    /*
        Purpose: Resizes the recording queue for a new sample rate
        Returns: Resize status
    */

    pthread_mutex_lock(&q->lock);

    free(q->I);
    free(q->Q);

    q->capacity = ((size_t)sample_rate_hz * REC_QUEUE_SECONDS) + 1;
    q->read_pos = 0;
    q->write_pos = 0;
    q->stop_requested = 0;
    q->overflow = 0;

    q->I = malloc(sizeof(int16_t) * q->capacity);
    q->Q = malloc(sizeof(int16_t) * q->capacity);

    int result = (q->I && q->Q);

    if (!result) {

        free(q->I);
        free(q->Q);

        q->I = NULL;
        q->Q = NULL;
        q->capacity = 0;
        q->read_pos = 0;
        q->write_pos = 0;

    }

    pthread_mutex_unlock(&q->lock);

    return result;
}

static size_t rec_queue_available_locked(Type_Rec_Queue *q) {
    /*
        Purpose: Returns the number of queued samples while already locked
        Returns: Available samples
    */

    if (q->write_pos >= q->read_pos) {

        return q->write_pos - q->read_pos;

    }

    return q->capacity - q->read_pos + q->write_pos;
}

static void rec_queue_reset(Type_Rec_Queue *q) {
    /*
        Purpose: Resets the recording queue state
        Returns: No value
    */

    pthread_mutex_lock(&q->lock);

    q->read_pos = 0;
    q->write_pos = 0;
    q->stop_requested = 0;
    q->overflow = 0;

    pthread_mutex_unlock(&q->lock);
}

static size_t rec_queue_push_block(Type_Rec_Queue *q, const float *in_I, const float *in_Q, size_t count) {
    /*
        Purpose: Pushes a block of IQ samples into the recording queue
        Returns: Pushed samples
    */

    if (!q->I || !q->Q || q->capacity == 0) {

        return 0;

    }

    if (!in_I || !in_Q || count == 0) {

        return 0;

    }

    size_t total_pushed = 0;

    while (total_pushed < count) {
        size_t chunk_count = count - total_pushed;

        if (chunk_count > REC_PUSH_CHUNK_SAMPLES) {

            chunk_count = REC_PUSH_CHUNK_SAMPLES;

        }

        pthread_mutex_lock(&q->lock);

        if (q->stop_requested) {

            pthread_mutex_unlock(&q->lock);
            break;

        }

        size_t pushed_chunk = 0;

        for (size_t n = 0; n < chunk_count; n++) {
            size_t src_idx = total_pushed + n;
            size_t next = (q->write_pos + 1) % q->capacity;

            if (next == q->read_pos) {

                q->overflow = 1;
                break;

            }

            float I = in_I[src_idx];
            float Q = in_Q[src_idx];

            if (I > 1.0f) {

                I = 1.0f;

            }

            if (I < -1.0f) {

                I = -1.0f;

            }

            if (Q > 1.0f) {

                Q = 1.0f;

            }

            if (Q < -1.0f) {

                Q = -1.0f;

            }

            q->I[q->write_pos] = (int16_t)(I * 32767.0f);
            q->Q[q->write_pos] = (int16_t)(Q * 32767.0f);

            q->write_pos = next;
            pushed_chunk++;
        }

        if (pushed_chunk > 0) {

            pthread_cond_signal(&q->data_cond);

        }

        pthread_mutex_unlock(&q->lock);

        total_pushed += pushed_chunk;

        // If queue becomes full before full chunk is pushed

        if (pushed_chunk < chunk_count) {

            break;

        }
    }

    return total_pushed;
}

static size_t rec_queue_pop_block(Type_Rec_Queue *q, int16_t *out_I, int16_t *out_Q, size_t max_count) {
    /*
        Purpose: Pops a block of IQ samples from the recording queue
        Returns: Popped samples
    */

    pthread_mutex_lock(&q->lock);

    while (rec_queue_available_locked(q) == 0 && !q->stop_requested) {
        pthread_cond_wait(&q->data_cond, &q->lock);
    }

    size_t available = rec_queue_available_locked(q);

    if (available == 0 && q->stop_requested) {

        pthread_mutex_unlock(&q->lock);
        return 0;

    }

    if (available > max_count) {

        available = max_count;

    }

    for (size_t n = 0; n < available; n++) {
        out_I[n] = q->I[q->read_pos];
        out_Q[n] = q->Q[q->read_pos];

        q->read_pos = (q->read_pos + 1) % q->capacity;
    }

    pthread_mutex_unlock(&q->lock);

    return available;
}

static void rec_queue_request_stop(Type_Rec_Queue *q) {
    /*
        Purpose: Requests the recording queue to stop blocking operations
        Returns: No value
    */

    pthread_mutex_lock(&q->lock);

    q->stop_requested = 1;

    pthread_cond_broadcast(&q->data_cond);

    pthread_mutex_unlock(&q->lock);
}

// Recorder Helpers

static void recorder_reset_processing_state(void) {
    /*
        Purpose: Resets recorder filter and mixer state
        Returns: No value
    */

    Global_Rec_Phase = 0.0;
    Global_Rec_Acc_I = 0.0;
    Global_Rec_Acc_Q = 0.0;
    Global_Rec_Acc_Count = 0;
    Global_Rec_FIR_Pos = 0;

    memset(Global_Rec_Hist_I, 0, sizeof(Global_Rec_Hist_I));
    memset(Global_Rec_Hist_Q, 0, sizeof(Global_Rec_Hist_Q));
}

static void recorder_write_sample(float I, float Q) {
    /*
        Purpose: Processes and writes one IQ sample to the active recording file
        Returns: No value
    */

    if (!Global_Rec_File) {

        return;

    }

    // Shift selected center frequency to baseband

    double Freq_Offset_Hz = (double)Global_Rec_Center_Hz - (double)Global_Center_Freq_Hz;
    double Phase_Step = -2.0 * M_PI * Freq_Offset_Hz / (double)Global_Sample_Rate_Hz;

    double C = cos(Global_Rec_Phase);
    double S = sin(Global_Rec_Phase);

    double Shifted_I = I * C - Q * S;
    double Shifted_Q = I * S + Q * C;

    Global_Rec_Phase += Phase_Step;

    if (Global_Rec_Phase > M_PI) {

        Global_Rec_Phase -= 2.0 * M_PI;

    }

    if (Global_Rec_Phase < -M_PI) {

        Global_Rec_Phase += 2.0 * M_PI;

    }

    if (Global_Rec_Decimation <= 1) {

        if (Shifted_I > 1.0) {

            Shifted_I = 1.0;

        }

        if (Shifted_I < -1.0) {

            Shifted_I = -1.0;

        }

        if (Shifted_Q > 1.0) {

            Shifted_Q = 1.0;

        }

        if (Shifted_Q < -1.0) {

            Shifted_Q = -1.0;

        }

        int16_t iq_pair[2];

        iq_pair[0] = (int16_t)(Shifted_I * 32767.0);
        iq_pair[1] = (int16_t)(Shifted_Q * 32767.0);

        fwrite(iq_pair, sizeof(int16_t), 2, Global_Rec_File);
        return;

    }

    // Always store the newest shifted sample

    Global_Rec_Hist_I[Global_Rec_FIR_Pos] = Shifted_I;
    Global_Rec_Hist_Q[Global_Rec_FIR_Pos] = Shifted_Q;

    int newest_pos = Global_Rec_FIR_Pos;

    Global_Rec_FIR_Pos++;

    if (Global_Rec_FIR_Pos >= REC_FIR_TAPS) {

        Global_Rec_FIR_Pos = 0;

    }

    // Decimation gate
    // Do not run the full FIR convolution unless this input sample will produce
    // one output sample

    Global_Rec_Acc_Count++;

    if (Global_Rec_Acc_Count < Global_Rec_Decimation) {

        return;

    }

    Global_Rec_Acc_Count = 0;

    // FIR convolution only on output samples

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

    if (Filtered_I > 1.0) {

        Filtered_I = 1.0;

    }

    if (Filtered_I < -1.0) {

        Filtered_I = -1.0;

    }

    if (Filtered_Q > 1.0) {

        Filtered_Q = 1.0;

    }

    if (Filtered_Q < -1.0) {

        Filtered_Q = -1.0;

    }

    int16_t iq_pair[2];

    iq_pair[0] = (int16_t)(Filtered_I * 32767.0);
    iq_pair[1] = (int16_t)(Filtered_Q * 32767.0);

    fwrite(iq_pair, sizeof(int16_t), 2, Global_Rec_File);
}

static void *recorder_thread_main(void *arg) {
    /*
        Purpose: Drains queued IQ samples and writes the active recording file
        Returns: Thread result
    */

    (void)arg;

    if (Global_Rec_Pre_I && Global_Rec_Pre_Q && Global_Rec_Pre_Count > 0) {

        for (size_t n = 0; n < Global_Rec_Pre_Count; n++) {
            float I = (float)Global_Rec_Pre_I[n] / 32768.0f;
            float Q = (float)Global_Rec_Pre_Q[n] / 32768.0f;

            recorder_write_sample(I, Q);
        }

        fflush(Global_Rec_File);

    }

    int16_t *buf_I = malloc(sizeof(int16_t) * REC_PUSH_CHUNK_SAMPLES);
    int16_t *buf_Q = malloc(sizeof(int16_t) * REC_PUSH_CHUNK_SAMPLES);

    if (!buf_I || !buf_Q) {

        free(buf_I);
        free(buf_Q);

        if (Global_Rec_File) {

            fclose(Global_Rec_File);
            Global_Rec_File = NULL;

        }

        return NULL;

    }

    while (1) {
        size_t popped = rec_queue_pop_block(&Global_Rec_Queue, buf_I, buf_Q, REC_PUSH_CHUNK_SAMPLES);

        if (popped == 0) {

            break;

        }

        for (size_t n = 0; n < popped; n++) {
            float I = (float)buf_I[n] / 32768.0f;
            float Q = (float)buf_Q[n] / 32768.0f;

            recorder_write_sample(I, Q);
        }
    }

    free(buf_I);
    free(buf_Q);

    if (Global_Rec_File) {

        fflush(Global_Rec_File);
        fclose(Global_Rec_File);
        Global_Rec_File = NULL;

    }

    return NULL;
}

static void stop_recording(void) {
    /*
        Purpose: Stops recording and drains the recording queue
        Returns: No value
    */

    int thread_exists = 0;

    pthread_mutex_lock(&Global_Rec_Lock);

    if (!Global_Rec && !Global_Rec_Thread_Running) {

        pthread_mutex_unlock(&Global_Rec_Lock);
        set_status("", (SDL_Color){0, 255, 80, 255});
        return;

    }

    Global_Rec = 0;
    thread_exists = Global_Rec_Thread_Running;

    pthread_mutex_unlock(&Global_Rec_Lock);

    // Finish after draining queued samples

    rec_queue_request_stop(&Global_Rec_Queue);

    if (thread_exists) {

        pthread_join(Global_Rec_Thread, NULL);
        Global_Rec_Thread_Running = 0;

    }

    free(Global_Rec_Pre_I);
    free(Global_Rec_Pre_Q);

    Global_Rec_Pre_I = NULL;
    Global_Rec_Pre_Q = NULL;
    Global_Rec_Pre_Count = 0;

    recorder_reset_processing_state();

    if (Global_Rec_Queue.overflow) {

        set_status("Recording stopped - queue overflow occurred", (SDL_Color){255, 180, 40, 255});

    }

    else {

        set_status("", (SDL_Color){0, 255, 80, 255});

    }
}

static int start_recording(void) {
    /*
        Purpose: Runs the background recording writer thread
        Returns: Start status
    */

    if (Global_Rec) {

        return 1;

    }

    Global_Rec_Center_Hz = selection_center_Hz();
    Global_Rec_BW_Hz = selection_BW_Hz();

    configure_recording_filter();
    recorder_reset_processing_state();
    rec_queue_reset(&Global_Rec_Queue);

    char datetime_str[32];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    strftime(datetime_str, sizeof(datetime_str), "%m-%d-%Y_%H-%M-%S", tm_now);

    if (!ensure_record_dir_exists()) {

        Global_Rec = 0;
        set_status("Record directory failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    char filename[1024];

    snprintf(filename, sizeof(filename), "%s/%s_CAPTURE_%.6fMHz_BW_%.3fkHz_SR_%.3fk_Decimation_%d.complex16",
             Global_Record_Dir, datetime_str, Global_Rec_Center_Hz / 1e6, Global_Rec_BW_Hz / 1e3,
             Global_Rec_Out_Rate_Hz / 1e3, Global_Rec_Decimation);

    Global_Rec_File = fopen(filename, "wb");

    if (!Global_Rec_File) {

        Global_Rec = 0;
        set_status("Record Open Failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    free(Global_Rec_Pre_I);
    free(Global_Rec_Pre_Q);

    Global_Rec_Pre_I = NULL;
    Global_Rec_Pre_Q = NULL;
    Global_Rec_Pre_Count = 0;

    // CRITICAL FOR ENSURING MINIMAL GAP BETWEEN CACHE AND LIVE DATA

    if (Global_Cached_Recording) {

        pthread_mutex_lock(&Global_Pre_Cache.lock);

        Global_Rec_Pre_Count = pre_cache_snapshot_locked(&Global_Pre_Cache, &Global_Rec_Pre_I, &Global_Rec_Pre_Q);

        pthread_mutex_lock(&Global_Rec_Lock);

        Global_Rec = 1;

        pthread_mutex_unlock(&Global_Rec_Lock);

        pthread_mutex_unlock(&Global_Pre_Cache.lock);

    }

    else {

        pthread_mutex_lock(&Global_Rec_Lock);

        Global_Rec = 1;

        pthread_mutex_unlock(&Global_Rec_Lock);

    }

    if (pthread_create(&Global_Rec_Thread, NULL, recorder_thread_main, NULL) != 0) {

        pthread_mutex_lock(&Global_Rec_Lock);
        Global_Rec = 0;
        pthread_mutex_unlock(&Global_Rec_Lock);

        rec_queue_request_stop(&Global_Rec_Queue);

        if (Global_Rec_File) {

            fclose(Global_Rec_File);
            Global_Rec_File = NULL;

        }

        free(Global_Rec_Pre_I);
        free(Global_Rec_Pre_Q);

        Global_Rec_Pre_I = NULL;
        Global_Rec_Pre_Q = NULL;
        Global_Rec_Pre_Count = 0;

        set_status("Record Thread Failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    Global_Rec_Thread_Running = 1;

    Global_Selector.dragging = 0;
    Global_Selector.resizing_left = 0;
    Global_Selector.resizing_right = 0;

    char msg[256];

    snprintf(msg, sizeof(msg), "RECORDING %.6f MHz - BW %.3f kHz%s", Global_Rec_Center_Hz / 1e6, Global_Rec_BW_Hz / 1e3,
             Global_Cached_Recording ? " - CACHE 5s" : "");

    set_status(msg, (SDL_Color){255, 60, 40, 255});

    return 1;
}

// Ring Buffer Helpers

static size_t ring_available_locked(Type_RingBuf *r) {
    /*
        Purpose: Returns the number of samples available in the waterfall ring buffer while already locked
        Returns: Available samples
    */

    if (r->write_pos >= r->read_pos) {

        return r->write_pos - r->read_pos;

    }

    return RING_SIZE - r->read_pos + r->write_pos;
}

static void ring_clear(Type_RingBuf *r) {
    /*
        Purpose: Clears the waterfall ring buffer
        Returns: No value
    */

    pthread_mutex_lock(&r->lock);

    r->write_pos = 0;
    r->read_pos = 0;

    pthread_mutex_unlock(&r->lock);
}

static void ring_write_sample(Type_RingBuf *r, float I, float Q) {
    /*
        Purpose: Writes one IQ sample into the waterfall ring buffer
        Returns: No value
    */

    r->I[r->write_pos] = I;
    r->Q[r->write_pos] = Q;

    r->write_pos = (r->write_pos + 1) % RING_SIZE;

    if (r->write_pos == r->read_pos) {

        r->read_pos = (r->read_pos + 1) % RING_SIZE;

    }
}

static int ring_read_block(Type_RingBuf *r, fftw_complex *in, double *window) {
    /*
        Purpose: Reads one FFT block from the waterfall ring buffer
        Returns: Read status
    */

    pthread_mutex_lock(&r->lock);

    if (ring_available_locked(r) < FFT_SIZE) {

        pthread_mutex_unlock(&r->lock);
        return 0;

    }

    for (int sam = 0; sam < FFT_SIZE; sam++) {
        size_t idx = (r->read_pos + sam) % RING_SIZE;
        in[sam][0] = r->I[idx] * window[sam];
        in[sam][1] = r->Q[idx] * window[sam];
    }

    // Hann window is being used, smoothen out any edges and visualize shorter
    // bursts better That explains "+ FFT_SIZE / 2" (Use half of the older
    // samples)

    r->read_pos = (r->read_pos + FFT_SIZE / 2) % RING_SIZE;

    pthread_mutex_unlock(&r->lock);
    return 1;
}

// RX Helper

static int rx_callback(hackrf_transfer *transfer) {
    /*
        Purpose: Receives HackRF samples and routes them to cache, display, and recording paths
        Returns: Callback status
    */

    const int8_t *buf = (const int8_t *)transfer->buffer;
    int sample_count = transfer->valid_length / 2;

    if (sample_count > MAX_TRANSFER_SAMPLES) {

        sample_count = MAX_TRANSFER_SAMPLES;

    }

    // Convert signed HackRF IQ once

    pthread_mutex_lock(&Global_Pre_Cache.lock);

    for (int n = 0; n < sample_count; n++) {
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

    // Waterfall ring buffer
    // Lock only around ring_buf writes

    pthread_mutex_lock(&ring_buf.lock);

    for (int n = 0; n < sample_count; n++) {
        ring_write_sample(&ring_buf, temp_I[n], temp_Q[n]);
    }

    pthread_mutex_unlock(&ring_buf.lock);

    // Recorder path
    // No ring_buf.lock here

    int rec_enabled = 0;

    pthread_mutex_lock(&Global_Rec_Lock);
    rec_enabled = Global_Rec;
    pthread_mutex_unlock(&Global_Rec_Lock);

    if (rec_enabled) {

        size_t pushed = rec_queue_push_block(&Global_Rec_Queue, temp_I, temp_Q, (size_t)sample_count);

        if (pushed < (size_t)sample_count) {

            Global_Rec_Queue.overflow = 1;

        }

    }

    return 0;
}

// Graphics Helper

static void compute_DB_from_FFT(fftw_complex *out, double *db) {
    /*
        Purpose: Converts FFT output bins into shifted decibel values
        Returns: No value
    */

    for (int sam = 0; sam < FFT_SIZE; sam++) {
        int shifted_sam = (sam + FFT_SIZE / 2) % FFT_SIZE;

        double I = out[shifted_sam][0];
        double Q = out[shifted_sam][1];

        double magnitude = sqrt(I * I + Q * Q) / FFT_SIZE;
        db[sam] = 20.0 * log10(magnitude + 1e-12) + 100.0;
    }
}

static int parse_positive_double(const char *s, double *out) {
    /*
        Purpose: Parses a positive double value from text
        Returns: Parse status
    */

    if (!s || !*s) {

        return 0;

    }

    char *end = NULL;
    double v = strtod(s, &end);

    if (end == s || *end != '\0' || v <= 0.0) {

        return 0;

    }

    *out = v;
    return 1;
}

static int parse_nonnegative_int(const char *s, int *out) {
    /*
        Purpose: Parses a nonnegative integer value from text
        Returns: Parse status
    */

    if (!s || !*s) {

        return 0;

    }

    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (end == s || *end != '\0' || v < 0 || v > 100000) {

        return 0;

    }

    *out = (int)v;
    return 1;
}

// Normalization Helpers

static int normalize_lna_gain(int gain) {
    /*
        Purpose: Normalizes HackRF LNA gain to a valid step
        Returns: Gain value
    */

    if (gain < 0) {

        gain = 0;

    }

    if (gain > 40) {

        gain = 40;

    }
    return (gain / 8) * 8;
}

static int normalize_vga_gain(int gain) {
    /*
        Purpose: Normalizes HackRF VGA gain to a valid step
        Returns: Gain value
    */

    if (gain < 0) {

        gain = 0;

    }

    if (gain > 62) {

        gain = 62;

    }
    return (gain / 2) * 2;
}

static int normalize_fps(int fps) {
    /*
        Purpose: Normalizes waterfall frame rate
        Returns: FPS value
    */

    if (fps < 1) {

        fps = 1;

    }

    if (fps > 1000) {

        fps = 1000;

    }
    return fps;
}

static int normalize_rows_per_frame(int rows) {
    /*
        Purpose: Normalizes waterfall rows rendered per frame
        Returns: Row count
    */

    if (rows < 1) {

        rows = 1;

    }

    if (rows > 64) {

        rows = 64;

    }
    return rows;
}

// Radio Helpers

static int stop_radio(hackrf_device *dev) {
    /*
        Purpose: Stops HackRF receive mode when active
        Returns: Stop status
    */

    if (!dev) {

        Global_Radio_Running = 0;
        return 1;

    }

    if (Global_Radio_Running) {

        if (hackrf_stop_rx(dev) != HACKRF_SUCCESS) {

            Global_Radio_Running = 0;
            return 0;

        }
        Global_Radio_Running = 0;

    }

    return 1;
}

static int start_radio(hackrf_device *dev) {
    /*
        Purpose: Starts HackRF receive mode when inactive
        Returns: Start status
    */

    if (!dev) {

        return 0;

    }

    if (!Global_Radio_Running) {

        if (hackrf_start_rx(dev, rx_callback, NULL) != HACKRF_SUCCESS) {

            return 0;

        }
        Global_Radio_Running = 1;

    }

    return 1;
}

double recommended_antenna_length_inches(uint64_t freq_hz) {
    /*
        Purpose: Computes quarter-wave antenna length in inches
        Returns: Antenna length
    */

    if (freq_hz == 0) {

        return 0.0;

    }

    /*
     * Quarter-wave antenna length:
     *
     * wavelength = c / f
     * quarter-wave = wavelength / 4
     *
     * c ≈ 299,792,458 m/s
     *
     * Return value is in inches
     */

    double wavelength_m = 299792458.0 / (double)freq_hz;
    double quarter_wave_m = wavelength_m / 4.0;

    return quarter_wave_m * 39.37007874;
}

static int apply_radio_settings(hackrf_device *dev, uint64_t Center_Hz, uint32_t Sample_Rate_Hz,
                                uint32_t Display_Span_Hz, int LNA_Gain, int VGA_Gain, int Amp_Enable) {
    /*
        Purpose: Applies center frequency, sample rate, display span, gain, and amp settings
        Returns: Apply status
    */

    if (!dev) {

        return 0;

    }

    if (Global_Rec) {

        stop_recording();

    }

    if (!stop_radio(dev)) {

        return 0;

    }

    ring_clear(&ring_buf);

    if (hackrf_set_sample_rate(dev, Sample_Rate_Hz) != HACKRF_SUCCESS) {

        return 0;

    }

    if (hackrf_set_freq(dev, Center_Hz) != HACKRF_SUCCESS) {

        return 0;

    }

    uint32_t filter_bw = hackrf_compute_baseband_filter_bw_round_down_lt(Sample_Rate_Hz);

    if (filter_bw > 0) {

        hackrf_set_baseband_filter_bandwidth(dev, filter_bw);

    }

    LNA_Gain = normalize_lna_gain(LNA_Gain);
    VGA_Gain = normalize_vga_gain(VGA_Gain);

    if (hackrf_set_lna_gain(dev, (uint32_t)LNA_Gain) != HACKRF_SUCCESS) {

        return 0;

    }

    if (hackrf_set_vga_gain(dev, (uint32_t)VGA_Gain) != HACKRF_SUCCESS) {

        return 0;

    }

    if (hackrf_set_amp_enable(dev, (uint8_t)(Amp_Enable ? 1 : 0)) != HACKRF_SUCCESS) {

        return 0;

    }

    if (Display_Span_Hz > Sample_Rate_Hz) {

        Display_Span_Hz = Sample_Rate_Hz;

    }

    if (Display_Span_Hz < 1000) {

        Display_Span_Hz = 1000;

    }

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

    if (!rec_queue_resize(&Global_Rec_Queue, Global_Sample_Rate_Hz)) {

        set_status("Record queue resize failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    return start_radio(dev);
}

static int retrospectrum_tx_callback(hackrf_transfer *transfer) {
    /*
        Purpose: Converts signed 16-bit interleaved IQ samples to HackRF signed 8-bit IQ data
        Returns: Zero while streaming and nonzero when the stream must stop
    */

    Type_Transmit_State *state;
    int16_t input[RETROSPECTRUM_TX_CONVERT_CHUNK];
    size_t output_offset = 0;
    size_t output_length;

    if (!transfer || !transfer->buffer || transfer->valid_length <= 0) {

        return -1;

    }

    state = transfer->tx_ctx ? (Type_Transmit_State *)transfer->tx_ctx : &Global_Transmit_State;
    output_length = (size_t)transfer->valid_length;

    pthread_mutex_lock(&state->lock);

    if (!state->active || !state->file || state->cancel_requested || state->callback_finish_armed) {

        memset(transfer->buffer, 0, output_length);
        state->callback_done = 1;
        pthread_mutex_unlock(&state->lock);
        return -1;

    }

    while (output_offset < output_length) {
        size_t wanted = output_length - output_offset;

        if (wanted > RETROSPECTRUM_TX_CONVERT_CHUNK) {

            wanted = RETROSPECTRUM_TX_CONVERT_CHUNK;

        }

        size_t got = fread(input, sizeof(int16_t), wanted, state->file);

        for (size_t i = 0; i < got; i++) {
            int sample = (int)(input[i] / 256);

            if (sample < -127) {

                sample = -127;

            }

            if (sample > 127) {

                sample = 127;

            }

            transfer->buffer[output_offset + i] = (uint8_t)(int8_t)sample;
        }

        output_offset += got;
        state->bytes_consumed += (uint64_t)got * sizeof(int16_t);

        if (got == wanted) {

            continue;

        }

        if (ferror(state->file)) {

            clearerr(state->file);
            state->callback_failed = 1;
            state->callback_finish_armed = 1;
            snprintf(state->result_message, sizeof(state->result_message),
                     "Transmission failed while reading the IQ file.");
            memset(transfer->buffer + output_offset, 0, output_length - output_offset);
            break;

        }

        if (feof(state->file)) {

            if (state->current_pass + 1U < state->total_passes) {

                state->current_pass++;
                clearerr(state->file);

                if (fseek(state->file, 0, SEEK_SET) != 0) {

                    state->callback_failed = 1;
                    state->callback_finish_armed = 1;
                    snprintf(state->result_message, sizeof(state->result_message),
                             "Transmission failed while rewinding the IQ file.");
                    memset(transfer->buffer + output_offset, 0, output_length - output_offset);
                    break;

                }

                continue;

            }

            state->bytes_consumed = state->file_size_bytes * (uint64_t)state->total_passes;
            state->callback_finish_armed = 1;
            memset(transfer->buffer + output_offset, 0, output_length - output_offset);
            break;

        }

        state->callback_failed = 1;
        state->callback_finish_armed = 1;
        snprintf(state->result_message, sizeof(state->result_message),
                 "Transmission stopped because the IQ file returned no data.");
        memset(transfer->buffer + output_offset, 0, output_length - output_offset);
        break;
    }

    pthread_mutex_unlock(&state->lock);
    return 0;
}

static void retrospectrum_close_transmit_file_locked(Type_Transmit_State *state) {
    /*
        Purpose: Closes the active transmission file while the state lock is held
        Returns: No value
    */

    if (state && state->file) {

        fclose(state->file);
        state->file = NULL;

    }
}

static int retrospectrum_restore_receive_settings(uint64_t center_frequency_hz, uint32_t sample_rate_hz,
                                                  uint32_t display_span_hz, int lna_gain, int vga_gain,
                                                  int amp_enable) {
    /*
        Purpose: Restores the HackRF receive configuration after transmission
        Returns: Restore status
    */

    if (!Global_HackRF_Device) {

        return 0;

    }

    if (!apply_radio_settings(Global_HackRF_Device, center_frequency_hz, sample_rate_hz, display_span_hz, lna_gain,
                              vga_gain, amp_enable)) {

        Global_HackRF_Connected = 0;
        Global_Radio_Running = 0;
        return 0;

    }

    Global_HackRF_Connected = 1;
    return 1;
}

static void retrospectrum_finalize_transmission(int forced_cancel, int restore_receive) {
    /*
        Purpose: Stops TX, closes the IQ file, publishes a result, and optionally restores RX
        Returns: No value
    */

    Type_Transmit_State *state = &Global_Transmit_State;
    uint64_t saved_center_frequency_hz;
    uint32_t saved_sample_rate_hz;
    uint32_t saved_display_span_hz;
    int saved_lna_gain;
    int saved_vga_gain;
    int saved_amp_enable;
    int failed;
    int canceled;
    char callback_message[256];

    pthread_mutex_lock(&state->lock);

    if (!state->active) {

        pthread_mutex_unlock(&state->lock);
        return;

    }

    if (forced_cancel) {

        state->cancel_requested = 1;

    }

    saved_center_frequency_hz = state->saved_center_frequency_hz;
    saved_sample_rate_hz = state->saved_sample_rate_hz;
    saved_display_span_hz = state->saved_display_span_hz;
    saved_lna_gain = state->saved_lna_gain;
    saved_vga_gain = state->saved_vga_gain;
    saved_amp_enable = state->saved_amp_enable;
    failed = state->callback_failed;
    canceled = state->cancel_requested;
    snprintf(callback_message, sizeof(callback_message), "%s", state->result_message);
    pthread_mutex_unlock(&state->lock);

    if (Global_HackRF_Device) {

        (void)hackrf_stop_tx(Global_HackRF_Device);

    }

    pthread_mutex_lock(&state->lock);
    retrospectrum_close_transmit_file_locked(state);
    state->active = 0;
    state->callback_done = 0;
    state->callback_finish_armed = 0;
    state->result_ready = restore_receive ? 1 : 0;
    state->result_succeeded = (!failed && !canceled) ? 1 : 0;

    if (canceled) {

        snprintf(state->result_message, sizeof(state->result_message), "Transmission canceled.");

    }

    else if (failed) {

        snprintf(state->result_message, sizeof(state->result_message), "%s",
                 callback_message[0] ? callback_message : "Transmission failed.");

    }

    else {

        snprintf(state->result_message, sizeof(state->result_message), "Transmission succeeded.");

    }

    pthread_mutex_unlock(&state->lock);

    if (restore_receive &&
        !retrospectrum_restore_receive_settings(saved_center_frequency_hz, saved_sample_rate_hz, saved_display_span_hz,
                                                saved_lna_gain, saved_vga_gain, saved_amp_enable)) {

        pthread_mutex_lock(&state->lock);
        state->result_succeeded = 0;
        snprintf(state->result_message, sizeof(state->result_message),
                 "Transmission ended, but the HackRF receive configuration could not be restored.");
        pthread_mutex_unlock(&state->lock);

    }
}

static void retrospectrum_pump_transmission(void) {
    /*
        Purpose: Finalizes a completed asynchronous HackRF transmission on the main thread
        Returns: No value
    */

    int should_finalize = 0;

    pthread_mutex_lock(&Global_Transmit_State.lock);
    should_finalize = Global_Transmit_State.active && Global_Transmit_State.callback_done;
    pthread_mutex_unlock(&Global_Transmit_State.lock);

    if (should_finalize) {

        retrospectrum_finalize_transmission(0, 1);

    }
}

int RETROSPECTRUM_start_file_transmission(const char *path, uint64_t center_frequency_hz, uint32_t sample_rate_hz,
                                          uint32_t bandwidth_hz, int tx_gain_db, unsigned int repeat_count, char *error,
                                          size_t error_size) {
    /*
        Purpose: Starts asynchronous HackRF transmission of a signed complex16 IQ file
        Returns: Start status
    */

    Type_Transmit_State *state = &Global_Transmit_State;
    FILE *file = NULL;
    long file_size = 0;
    uint32_t actual_bandwidth_hz;
    int start_result;

    if (error && error_size > 0) {

        error[0] = '\0';

    }

    if (!path || path[0] == '\0') {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Select an IQ recording before transmitting.");

        }
        return 0;

    }

    if (!Global_HackRF_Connected || !Global_HackRF_Device) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "HackRF One is not connected.");

        }
        return 0;

    }

    if (center_frequency_hz < 1000000ULL || center_frequency_hz > 6000000000ULL) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Frequency must be from 1000000 to 6000000000 Hz.");

        }
        return 0;

    }

    if (sample_rate_hz < 2000000U || sample_rate_hz > 20000000U) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Sample rate must be from 2000000 to 20000000 Sps.");

        }
        return 0;

    }

    if (bandwidth_hz < 1000000U || bandwidth_hz > sample_rate_hz) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Bandwidth must be from 1000000 Hz through the sample rate.");

        }
        return 0;

    }

    if (tx_gain_db < 0 || tx_gain_db > 47) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "TX gain must be from 0 to 47 dB.");

        }
        return 0;

    }

    if (repeat_count > RETROSPECTRUM_TX_MAX_REPEATS) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Repeat count must be from 0 to %u.", RETROSPECTRUM_TX_MAX_REPEATS);

        }
        return 0;

    }

    actual_bandwidth_hz =
        hackrf_compute_baseband_filter_bw_round_down_lt(bandwidth_hz < UINT32_MAX ? bandwidth_hz + 1U : bandwidth_hz);

    if (actual_bandwidth_hz == 0 || actual_bandwidth_hz > sample_rate_hz) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "The requested bandwidth is not supported by HackRF.");

        }
        return 0;

    }

    file = fopen(path, "rb");

    if (!file) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Unable to open the selected IQ file: %s", strerror(errno));

        }
        return 0;

    }

    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {

        fclose(file);

        if (error && error_size > 0) {

            snprintf(error, error_size, "The selected IQ file is empty or unreadable.");

        }
        return 0;

    }

    if (((uint64_t)file_size % (sizeof(int16_t) * 2U)) != 0U) {

        fclose(file);

        if (error && error_size > 0) {

            snprintf(error, error_size, "The IQ file must contain complete interleaved int16 I/Q pairs.");

        }
        return 0;

    }

    pthread_mutex_lock(&state->lock);

    if (state->active) {

        pthread_mutex_unlock(&state->lock);
        fclose(file);

        if (error && error_size > 0) {

            snprintf(error, error_size, "Another transmission is already active.");

        }
        return 0;

    }

    state->file = file;
    state->file_size_bytes = (uint64_t)file_size;
    state->bytes_consumed = 0;
    state->center_frequency_hz = center_frequency_hz;
    state->sample_rate_hz = sample_rate_hz;
    state->requested_bandwidth_hz = bandwidth_hz;
    state->actual_bandwidth_hz = actual_bandwidth_hz;
    state->tx_gain_db = tx_gain_db;
    state->repeat_count = repeat_count;
    state->total_passes = repeat_count + 1U;
    state->current_pass = 0;
    state->active = 1;
    state->cancel_requested = 0;
    state->callback_finish_armed = 0;
    state->callback_done = 0;
    state->callback_failed = 0;
    state->result_ready = 0;
    state->result_succeeded = 0;
    state->saved_center_frequency_hz = Global_Center_Freq_Hz;
    state->saved_sample_rate_hz = Global_Sample_Rate_Hz;
    state->saved_display_span_hz = Global_Display_Span_Hz;
    state->saved_lna_gain = Global_LNA_Gain;
    state->saved_vga_gain = Global_VGA_Gain;
    state->saved_amp_enable = Global_Amp_Enable;
    state->result_message[0] = '\0';
    pthread_mutex_unlock(&state->lock);

    if (Global_Rec) {

        stop_recording();

    }

    if (!stop_radio(Global_HackRF_Device)) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Unable to stop HackRF receive mode.");

        }
        goto start_failed;

    }

    if (hackrf_set_sample_rate(Global_HackRF_Device, (double)sample_rate_hz) != HACKRF_SUCCESS) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "HackRF rejected the sample rate.");

        }
        goto start_failed;

    }

    if (hackrf_set_freq(Global_HackRF_Device, center_frequency_hz) != HACKRF_SUCCESS) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "HackRF rejected the transmit frequency.");

        }
        goto start_failed;

    }

    if (hackrf_set_baseband_filter_bandwidth(Global_HackRF_Device, actual_bandwidth_hz) != HACKRF_SUCCESS) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "HackRF rejected the baseband bandwidth.");

        }
        goto start_failed;

    }

    if (hackrf_set_amp_enable(Global_HackRF_Device, 0) != HACKRF_SUCCESS) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Unable to disable the HackRF RF amplifier before TX.");

        }
        goto start_failed;

    }

    if (hackrf_set_txvga_gain(Global_HackRF_Device, (uint32_t)tx_gain_db) != HACKRF_SUCCESS) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "HackRF rejected the TX gain.");

        }
        goto start_failed;

    }

    start_result = hackrf_start_tx(Global_HackRF_Device, retrospectrum_tx_callback, state);

    if (start_result != HACKRF_SUCCESS) {

        if (error && error_size > 0) {

            snprintf(error, error_size, "Unable to start HackRF transmission (%d).", start_result);

        }
        goto start_failed;

    }

    return 1;

start_failed:
    pthread_mutex_lock(&state->lock);
    retrospectrum_close_transmit_file_locked(state);
    state->active = 0;
    state->callback_done = 0;
    state->callback_finish_armed = 0;
    pthread_mutex_unlock(&state->lock);

    (void)retrospectrum_restore_receive_settings(state->saved_center_frequency_hz, state->saved_sample_rate_hz,
                                                 state->saved_display_span_hz, state->saved_lna_gain,
                                                 state->saved_vga_gain, state->saved_amp_enable);
    return 0;
}

void RETROSPECTRUM_cancel_file_transmission(void) {
    /*
        Purpose: Requests cancellation of the current asynchronous transmission
        Returns: No value
    */

    pthread_mutex_lock(&Global_Transmit_State.lock);

    if (Global_Transmit_State.active) {

        Global_Transmit_State.cancel_requested = 1;

    }

    pthread_mutex_unlock(&Global_Transmit_State.lock);
}

int RETROSPECTRUM_get_transmission_status(double *progress, int *active, int *result_ready, int *succeeded,
                                          char *message, size_t message_size) {
    /*
        Purpose: Gets a thread-safe snapshot of the transmission state
        Returns: Whether any active or completed transmission state exists
    */

    Type_Transmit_State *state = &Global_Transmit_State;
    uint64_t total_bytes;
    int has_state;

    pthread_mutex_lock(&state->lock);
    total_bytes = state->file_size_bytes * (uint64_t)(state->total_passes ? state->total_passes : 1U);

    if (progress) {

        *progress = total_bytes > 0 ? (double)state->bytes_consumed / (double)total_bytes : 0.0;

        if (*progress < 0.0) {

            *progress = 0.0;

        }

        if (*progress > 1.0) {

            *progress = 1.0;

        }

    }

    if (active) {

        *active = state->active;

    }

    if (result_ready) {

        *result_ready = state->result_ready;

    }

    if (succeeded) {

        *succeeded = state->result_succeeded;

    }

    if (message && message_size > 0) {

        snprintf(message, message_size, "%s", state->result_message);

    }

    has_state = state->active || state->result_ready;
    pthread_mutex_unlock(&state->lock);
    return has_state;
}

void RETROSPECTRUM_acknowledge_transmission_result(void) {
    /*
        Purpose: Clears the completed transmission result after the user closes its prompt
        Returns: No value
    */

    pthread_mutex_lock(&Global_Transmit_State.lock);
    Global_Transmit_State.result_ready = 0;
    Global_Transmit_State.result_message[0] = '\0';
    pthread_mutex_unlock(&Global_Transmit_State.lock);
}

static int RETROSPECTRUM_transmission_is_active(void) {
    /*
        Purpose: Reports whether HackRF is currently in asynchronous TX mode
        Returns: Active status
    */

    int active;

    pthread_mutex_lock(&Global_Transmit_State.lock);
    active = Global_Transmit_State.active;
    pthread_mutex_unlock(&Global_Transmit_State.lock);
    return active;
}

static int apply_from_inputs(hackrf_device *dev, Type_Input_Box *freq_box, Type_Input_Box *sr_box,
                             Type_Input_Box *display_box, Type_Input_Box *lna_box, Type_Input_Box *vga_box,
                             Type_Input_Box *fps_box, Type_Input_Box *rows_box, uint32_t *waterfall_pixels, int tex_w,
                             int tex_h) {
    /*
        Purpose: Parses GUI input boxes and applies radio settings
        Returns: Apply status
    */

    double freq_mhz = 0.0;
    double sr_msps = 0.0;
    double display_mhz = 0.0;
    int lna = 0, vga = 0, fps = 0, rows = 0;

    if (!parse_positive_double(freq_box->text, &freq_mhz)) {

        return 0;

    }

    if (!parse_positive_double(sr_box->text, &sr_msps)) {

        return 0;

    }

    if (!parse_positive_double(display_box->text, &display_mhz)) {

        return 0;

    }

    if (!parse_nonnegative_int(lna_box->text, &lna)) {

        return 0;

    }

    if (!parse_nonnegative_int(vga_box->text, &vga)) {

        return 0;

    }

    if (!parse_nonnegative_int(fps_box->text, &fps)) {

        return 0;

    }

    if (!parse_nonnegative_int(rows_box->text, &rows)) {

        return 0;

    }

    uint64_t center_hz = (uint64_t)(freq_mhz * 1e6);
    uint32_t sample_rate_hz = (uint32_t)(sr_msps * 1e6);
    uint32_t display_span_hz = (uint32_t)(display_mhz * 1e6);

    if (sample_rate_hz < 2000000 || sample_rate_hz > 20000000) {

        return 0;

    }

    if (display_span_hz > sample_rate_hz) {

        display_span_hz = sample_rate_hz;

    }

    if (display_span_hz < 1000) {

        display_span_hz = 1000;

    }

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

// ==========================
// Main Text Box Cursor Helpers
// ==========================

static int main_field_index(Type_Active_Fields field) {
    switch (field) {
    case FIELD_FREQ:
        return 0;
    case FIELD_SR:
        return 1;
    case FIELD_DISPLAY:
        return 2;
    case FIELD_LNA:
        return 3;
    case FIELD_VGA:
        return 4;
    case FIELD_FPS:
        return 5;
    case FIELD_ROWS:
        return 6;
    default:
        return -1;
    }
}

static char *main_field_text_by_index(int index, Type_Input_Box *freq_box, Type_Input_Box *sr_box,
                                      Type_Input_Box *display_box, Type_Input_Box *lna_box, Type_Input_Box *vga_box,
                                      Type_Input_Box *fps_box, Type_Input_Box *rows_box, size_t *text_size) {
    /*
        Purpose: Gets main field text by index
        Returns: Text pointer
    */

    if (text_size) {

        *text_size = 0;

    }

    switch (index) {
    case 0:

        if (text_size) {

            *text_size = sizeof(freq_box->text);

        }
        return freq_box->text;
    case 1:

        if (text_size) {

            *text_size = sizeof(sr_box->text);

        }
        return sr_box->text;
    case 2:

        if (text_size) {

            *text_size = sizeof(display_box->text);

        }
        return display_box->text;
    case 3:

        if (text_size) {

            *text_size = sizeof(lna_box->text);

        }
        return lna_box->text;
    case 4:

        if (text_size) {

            *text_size = sizeof(vga_box->text);

        }
        return vga_box->text;
    case 5:

        if (text_size) {

            *text_size = sizeof(fps_box->text);

        }
        return fps_box->text;
    case 6:

        if (text_size) {

            *text_size = sizeof(rows_box->text);

        }
        return rows_box->text;
    default:
        return NULL;
    }
}

static char *main_field_text(Type_Active_Fields field, Type_Input_Box *freq_box, Type_Input_Box *sr_box,
                             Type_Input_Box *display_box, Type_Input_Box *lna_box, Type_Input_Box *vga_box,
                             Type_Input_Box *fps_box, Type_Input_Box *rows_box, size_t *text_size) {
    /*
        Purpose: Gets text for the selected main field
        Returns: Text pointer
    */

    return main_field_text_by_index(main_field_index(field), freq_box, sr_box, display_box, lna_box, vga_box, fps_box,
                                    rows_box, text_size);
}

static void main_clamp_cursor_for_text(const char *text, int *cursor) {
    /*
        Purpose: Clamps the main text cursor
        Returns: No value
    */

    if (!text || !cursor) {

        return;

    }

    int len = (int)strlen(text);

    if (*cursor < 0) {

        *cursor = 0;

    }

    if (*cursor > len) {

        *cursor = len;

    }
}

static void main_reset_input_cursors(int cursors[7], Type_Input_Box *freq_box, Type_Input_Box *sr_box,
                                     Type_Input_Box *display_box, Type_Input_Box *lna_box, Type_Input_Box *vga_box,
                                     Type_Input_Box *fps_box, Type_Input_Box *rows_box) {
    /*
        Purpose: Resets the main input cursors
        Returns: No value
    */

    Type_Input_Box *boxes[7] = {freq_box, sr_box, display_box, lna_box, vga_box, fps_box, rows_box};

    for (int i = 0; i < 7; i++) {
        cursors[i] = boxes[i] ? (int)strlen(boxes[i]->text) : 0;
    }
}

static void main_set_active_cursor_end(Type_Active_Fields field, int cursors[7], Type_Input_Box *freq_box,
                                       Type_Input_Box *sr_box, Type_Input_Box *display_box, Type_Input_Box *lna_box,
                                       Type_Input_Box *vga_box, Type_Input_Box *fps_box, Type_Input_Box *rows_box) {
    /*
        Purpose: Sets the main active cursor end
        Returns: No value
    */

    int index = main_field_index(field);

    if (index < 0 || index >= 7) {

        return;

    }

    size_t text_size = 0;
    char *text =
        main_field_text_by_index(index, freq_box, sr_box, display_box, lna_box, vga_box, fps_box, rows_box, &text_size);
    (void)text_size;

    cursors[index] = text ? (int)strlen(text) : 0;
}

static void main_insert_text_at_cursor(char *dst, size_t dst_size, int *cursor, const char *src) {
    /*
        Purpose: Inserts the main text at cursor
        Returns: No value
    */

    if (!dst || dst_size == 0 || !cursor || !src) {

        return;

    }

    main_clamp_cursor_for_text(dst, cursor);

    while (*src) {
        char c = *src++;

        if (!((c >= '0' && c <= '9') || c == '.')) {

            continue;

        }

        size_t len = strlen(dst);

        if (len + 1 >= dst_size) {

            break;

        }

        int pos = *cursor;

        if (pos < 0) {

            pos = 0;

        }

        if (pos > (int)len) {

            pos = (int)len;

        }

        memmove(dst + pos + 1, dst + pos, len - (size_t)pos + 1);
        dst[pos] = c;
        *cursor = pos + 1;
    }
}

static void main_backspace_at_cursor(char *dst, int *cursor) {
    /*
        Purpose: Removes the previous character from the main at cursor
        Returns: No value
    */

    if (!dst || !cursor) {

        return;

    }

    main_clamp_cursor_for_text(dst, cursor);

    if (*cursor <= 0) {

        return;

    }

    size_t len = strlen(dst);
    int pos = *cursor;

    memmove(dst + pos - 1, dst + pos, len - (size_t)pos + 1);
    *cursor = pos - 1;
}

static void main_move_active_cursor(Type_Active_Fields field, int cursors[7], int delta, Type_Input_Box *freq_box,
                                    Type_Input_Box *sr_box, Type_Input_Box *display_box, Type_Input_Box *lna_box,
                                    Type_Input_Box *vga_box, Type_Input_Box *fps_box, Type_Input_Box *rows_box) {
    /*
        Purpose: Moves the main active cursor
        Returns: No value
    */

    int index = main_field_index(field);

    if (index < 0 || index >= 7) {

        return;

    }

    size_t text_size = 0;
    char *text =
        main_field_text_by_index(index, freq_box, sr_box, display_box, lna_box, vga_box, fps_box, rows_box, &text_size);
    (void)text_size;

    if (!text) {

        return;

    }

    cursors[index] += delta;
    main_clamp_cursor_for_text(text, &cursors[index]);
}

static void main_make_cursor_box(Type_Input_Box *dst, const Type_Input_Box *src, int active, int cursor) {
    /*
        Purpose: Builds the main cursor box
        Returns: No value
    */

    if (!dst || !src) {

        return;

    }

    *dst = *src;

    if (!active) {

        return;

    }

    const char *text = src->text;
    int len = (int)strlen(text);

    if (cursor < 0) {

        cursor = 0;

    }

    if (cursor > len) {

        cursor = len;

    }

    size_t out_size = sizeof(dst->text);
    size_t out = 0;

    for (int i = 0; i < cursor && out + 1 < out_size; i++) {
        dst->text[out++] = text[i];
    }

    if (out + 1 < out_size) {

        dst->text[out++] = '_';

    }

    for (int i = cursor; text[i] && out + 1 < out_size; i++) {
        dst->text[out++] = text[i];
    }

    dst->text[out] = '\0';
}

// =============================
// Headless Administration Console
// =============================

#define RETROSPECTRUM_CLI_MAX_USERS 256
#define RETROSPECTRUM_CLI_LINE_MAX 512
#define RETROSPECTRUM_CLI_ERROR_MAX 384
#define RETROSPECTRUM_CLI_PASSWORD_MAX 127

static void cli_secure_zero(void *memory, size_t size) {
    /*

    Purpose: Clears sensitive CLI memory without allowing compiler removal

    Return: No value

    */

    volatile unsigned char *bytes = (volatile unsigned char *)memory;

    if (!memory) {

        return;

    }

    while (size > 0) {
        *bytes++ = 0;
        size--;
    }
}

static void cli_trim_line(char *text) {
    /*

    Purpose: Removes leading and trailing whitespace from CLI input

    Return: No value

    */

    size_t start = 0;
    size_t length;

    if (!text) {

        return;

    }

    length = strlen(text);

    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }

    while (text[start] && isspace((unsigned char)text[start])) {
        start++;
    }

    if (start > 0) {

        memmove(text, text + start, strlen(text + start) + 1);

    }
}

static int cli_read_line(const char *prompt, char *output, size_t output_size, int hidden) {
    /*

    Purpose: Reads a normal or hidden line from the controlling terminal

    Return: Read status

    */

    struct termios original;
    struct termios hidden_settings;
    int terminal_changed = 0;

    if (!output || output_size == 0) {

        return 0;

    }

    output[0] = '\0';

    if (prompt) {

        fputs(prompt, stdout);
        fflush(stdout);

    }

    if (hidden) {

        if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &original) != 0) {

            fputc('\n', stderr);
            fprintf(stderr, "Secure password input requires an interactive terminal.\n");
            return 0;

        }

        hidden_settings = original;
        hidden_settings.c_lflag &= (tcflag_t)~ECHO;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden_settings) != 0) {

            fputc('\n', stderr);
            fprintf(stderr, "Unable to disable terminal echo for secure input.\n");
            return 0;

        }
        terminal_changed = 1;

    }

    if (!fgets(output, (int)output_size, stdin)) {

        if (terminal_changed) {

            (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
            fputc('\n', stdout);

        }
        output[0] = '\0';
        return 0;

    }

    if (terminal_changed) {

        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        fputc('\n', stdout);

    }

    {
        size_t length = strlen(output);
        int complete_line = length > 0 && output[length - 1] == '\n';

        if (!complete_line && !feof(stdin)) {

            int character;

            while ((character = fgetc(stdin)) != '\n' && character != EOF) {
            }
            output[0] = '\0';
            fprintf(stderr, "Input exceeded the maximum supported length.\n");
            return 0;

        }

        while (length > 0 && (output[length - 1] == '\n' || output[length - 1] == '\r')) {
            output[--length] = '\0';
        }
    }

    if (!hidden) {

        cli_trim_line(output);

    }
    return 1;
}

static int cli_prompt_yes_no(const char *prompt, int default_value) {
    /*

    Purpose: Reads a yes-or-no response

    Return: Boolean response

    */

    char answer[32];

    while (Global_Running) {

        if (!cli_read_line(prompt, answer, sizeof(answer), 0)) {

            return default_value;

        }

        if (answer[0] == '\0') {

            return default_value;

        }

        for (size_t index = 0; answer[index]; index++) {
            answer[index] = (char)tolower((unsigned char)answer[index]);
        }

        if (strcmp(answer, "y") == 0 || strcmp(answer, "yes") == 0) {

            return 1;

        }

        if (strcmp(answer, "n") == 0 || strcmp(answer, "no") == 0) {

            return 0;

        }

        fprintf(stderr, "Enter yes or no.\n");
    }

    return default_value;
}

static int cli_prompt_password_twice(char *password, size_t password_size) {
    /*

    Purpose: Reads and confirms a password without terminal echo

    Return: Success status

    */

    char confirmation[RETROSPECTRUM_CLI_PASSWORD_MAX + 1];
    int success = 0;

    memset(confirmation, 0, sizeof(confirmation));

    if (!cli_read_line("Password: ", password, password_size, 1) ||
        !cli_read_line("Confirm password: ", confirmation, sizeof(confirmation), 1)) {

        goto cleanup;

    }

    if (strcmp(password, confirmation) != 0) {

        fprintf(stderr, "Passwords do not match.\n");
        goto cleanup;

    }

    success = 1;

cleanup:
    cli_secure_zero(confirmation, sizeof(confirmation));
    return success;
}

static const char *cli_role_name(int role) {
    /*

    Purpose: Gets the printable account role

    Return: Role name

    */

    if (role == AUTH_ROLE_ADMIN) {

        return "admin";

    }

    if (role == AUTH_ROLE_CO_ADMIN) {

        return "co-admin";

    }

    return "user";
}

static int cli_role_is_privileged(int role) {
    /*

    Purpose: Checks whether a role may use account-management commands

    Return: Boolean status

    */

    return role == AUTH_ROLE_ADMIN || role == AUTH_ROLE_CO_ADMIN;
}

static int cli_load_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count) {
    /*

    Purpose: Loads users from the local authentication database

    Return: Success status

    */

    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";

    if (!AUTH_DB_list_users(users, capacity, count, error, sizeof(error))) {

        fprintf(stderr, "Unable to load users: %s\n", error[0] ? error : "authentication database unavailable");
        return 0;

    }

    return 1;
}

static int cli_lookup_user(const char *username, Type_Auth_User_Summary *summary) {
    /*

    Purpose: Finds a user summary by exact username

    Return: Found status

    */

    Type_Auth_User_Summary users[RETROSPECTRUM_CLI_MAX_USERS];
    size_t count = 0;

    if (!username || !cli_load_users(users, RETROSPECTRUM_CLI_MAX_USERS, &count)) {

        return 0;

    }

    for (size_t index = 0; index < count; index++) {

        if (strcmp(users[index].username, username) == 0) {

            if (summary) {

                *summary = users[index];

            }
            return 1;

        }
    }

    return 0;
}

static void cli_print_users(void) {
    /*

    Purpose: Prints the authentication user table

    Return: No value

    */

    Type_Auth_User_Summary users[RETROSPECTRUM_CLI_MAX_USERS];
    size_t count = 0;

    if (!cli_load_users(users, RETROSPECTRUM_CLI_MAX_USERS, &count)) {

        return;

    }

    printf("\n%-32s %-10s %-8s %s\n", "USERNAME", "ROLE", "2FA", "CREATED");
    printf("%-32s %-10s %-8s %s\n", "--------------------------------", "----------", "--------",
           "-------------------");

    for (size_t index = 0; index < count; index++) {
        char created[32] = "unknown";
        time_t created_time = (time_t)users[index].created_at;
        struct tm *local_time = NULL;

        if (created_time > 0) {

            local_time = localtime(&created_time);

        }

        if (local_time) {

            (void)strftime(created, sizeof(created), "%Y-%m-%d %H:%M", local_time);

        }

        printf("%-32.32s %-10s %-8s %s\n", users[index].username, cli_role_name(users[index].role),
               users[index].totp_enabled ? "enabled" : "disabled", created);
    }

    printf("\n%zu account%s\n\n", count, count == 1 ? "" : "s");
}

static int cli_enroll_totp(unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES]) {
    /*

    Purpose: Generates and verifies a TOTP secret before it is stored

    Return: Success status

    */

    char base32[128] = "";
    char code[16] = "";

    if (!AUTH_TOTP_generate_secret(secret) || !AUTH_TOTP_base32(secret, base32, sizeof(base32))) {

        fprintf(stderr, "Unable to generate a secure 2FA secret.\n");
        return 0;

    }

    printf("\nAdd this secret to the user's authenticator application:\n%s\n", base32);
    printf("Type: TOTP    Digits: 6    Period: 30 seconds\n\n");

    if (!cli_read_line("Current 2FA code: ", code, sizeof(code), 0) || !AUTH_TOTP_verify(secret, code)) {

        fprintf(stderr, "The 2FA code was not valid. No 2FA change was saved.\n");
        cli_secure_zero(code, sizeof(code));
        return 0;

    }

    cli_secure_zero(code, sizeof(code));
    return 1;
}

static int cli_bootstrap_primary_admin(void) {
    /*

    Purpose: Creates the primary administrator when the database has no users

    Return: Success status

    */

    char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char password[RETROSPECTRUM_CLI_PASSWORD_MAX + 1] = "";
    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";
    unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES];
    int enable_totp = 0;
    int success = 0;

    memset(secret, 0, sizeof(secret));

    printf("No authentication accounts exist. Create the primary administrator.\n\n");

    if (!cli_read_line("Administrator username: ", username, sizeof(username), 0) || username[0] == '\0' ||
        !cli_prompt_password_twice(password, sizeof(password))) {

        goto cleanup;

    }

    enable_totp = cli_prompt_yes_no("Enable 2FA now? [Y/n]: ", 1);

    if (enable_totp && !cli_enroll_totp(secret)) {

        goto cleanup;

    }

    if (!AUTH_DB_create_user(username, password, enable_totp, 1, enable_totp ? secret : NULL, NULL, error,
                             sizeof(error))) {

        fprintf(stderr, "Unable to create the primary administrator: %s\n",
                error[0] ? error : "authentication operation failed");
        goto cleanup;

    }

    printf("Primary administrator '%s' created.\n\n", username);
    success = 1;

cleanup:
    cli_secure_zero(password, sizeof(password));
    cli_secure_zero(secret, sizeof(secret));
    return success;
}

static int cli_authenticate(char *authenticated_username, size_t username_size, int *authenticated_role) {
    /*

    Purpose: Authenticates one local CLI session with optional TOTP

    Return: Success status

    */

    char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char password[RETROSPECTRUM_CLI_PASSWORD_MAX + 1] = "";
    char totp[16] = "";
    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";
    Type_Auth_User_Summary summary;
    int privileged = 0;
    int result;
    int success = 0;

    memset(&summary, 0, sizeof(summary));

    if (!cli_read_line("Username: ", username, sizeof(username), 0) || username[0] == '\0' ||
        !cli_read_line("Password: ", password, sizeof(password), 1)) {

        goto cleanup;

    }

    result = AUTH_SERVER_authenticate(username, password, NULL, "local-cli", &privileged, error, sizeof(error));

    if (result == AUTH_SERVER_RESULT_TOTP_REQUIRED) {

        if (!cli_read_line("2FA code: ", totp, sizeof(totp), 0)) {

            goto cleanup;

        }

        error[0] = '\0';
        result = AUTH_SERVER_authenticate(username, password, totp, "local-cli", &privileged, error, sizeof(error));

    }

    if (result != AUTH_SERVER_RESULT_SUCCESS) {

        fprintf(stderr, "Login failed: %s\n", error[0] ? error : "invalid credentials");
        goto cleanup;

    }

    if (!cli_lookup_user(username, &summary)) {

        fprintf(stderr, "Login succeeded, but the account role could not be loaded.\n");
        goto cleanup;

    }

    snprintf(authenticated_username, username_size, "%s", username);
    *authenticated_role = summary.role;
    success = 1;

cleanup:
    cli_secure_zero(password, sizeof(password));
    cli_secure_zero(totp, sizeof(totp));
    return success;
}

static int cli_read_target_username(char *username, size_t username_size, const char *argument) {
    /*

    Purpose: Resolves a username from a command argument or an interactive prompt

    Return: Success status

    */

    if (argument && argument[0]) {

        snprintf(username, username_size, "%s", argument);
        return 1;

    }

    return cli_read_line("Target username: ", username, username_size, 0) && username[0] != '\0';
}

static void cli_create_user(const char *acting_admin) {
    /*

    Purpose: Creates a user and optionally promotes it to co-administrator

    Return: No value

    */

    char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char password[RETROSPECTRUM_CLI_PASSWORD_MAX + 1] = "";
    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";
    unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES];
    int enable_totp;
    int make_co_admin;

    memset(secret, 0, sizeof(secret));

    if (!cli_read_line("New username: ", username, sizeof(username), 0) || username[0] == '\0' ||
        !cli_prompt_password_twice(password, sizeof(password))) {

        goto cleanup;

    }

    enable_totp = cli_prompt_yes_no("Enable 2FA? [y/N]: ", 0);
    make_co_admin = cli_prompt_yes_no("Create as co-admin? [y/N]: ", 0);

    if (enable_totp && !cli_enroll_totp(secret)) {

        goto cleanup;

    }

    if (!AUTH_DB_create_user(username, password, enable_totp, 0, enable_totp ? secret : NULL, acting_admin, error,
                             sizeof(error))) {

        fprintf(stderr, "Unable to create user: %s\n", error[0] ? error : "authentication operation failed");
        goto cleanup;

    }

    printf("User '%s' created.\n", username);

    if (make_co_admin) {

        error[0] = '\0';

        if (!AUTH_DB_set_role(username, AUTH_ROLE_CO_ADMIN, acting_admin, error, sizeof(error))) {

            fprintf(stderr, "The account was created as a user, but promotion failed: %s\n",
                    error[0] ? error : "role update failed");

        }

        else {

            printf("User '%s' promoted to co-admin.\n", username);

        }

    }

cleanup:
    cli_secure_zero(password, sizeof(password));
    cli_secure_zero(secret, sizeof(secret));
}

static void cli_reset_password(const char *acting_admin, const char *argument) {
    /*

    Purpose: Resets an account password

    Return: No value

    */

    char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char password[RETROSPECTRUM_CLI_PASSWORD_MAX + 1] = "";
    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";

    if (!cli_read_target_username(username, sizeof(username), argument) ||
        !cli_prompt_password_twice(password, sizeof(password))) {

        goto cleanup;

    }

    if (!AUTH_DB_reset_password(username, password, acting_admin, error, sizeof(error))) {

        fprintf(stderr, "Unable to reset password: %s\n", error[0] ? error : "authentication operation failed");
        goto cleanup;

    }

    printf("Password reset for '%s'.\n", username);

cleanup:
    cli_secure_zero(password, sizeof(password));
}

static void cli_enable_totp(const char *acting_admin, const char *argument) {
    /*

    Purpose: Enables or replaces TOTP for an account

    Return: No value

    */

    char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";
    unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES];

    memset(secret, 0, sizeof(secret));

    if (!cli_read_target_username(username, sizeof(username), argument) || !cli_enroll_totp(secret)) {

        goto cleanup;

    }

    if (!AUTH_DB_set_totp(username, secret, acting_admin, error, sizeof(error))) {

        fprintf(stderr, "Unable to enable 2FA: %s\n", error[0] ? error : "authentication operation failed");
        goto cleanup;

    }

    printf("2FA enabled for '%s'.\n", username);

cleanup:
    cli_secure_zero(secret, sizeof(secret));
}

static void cli_disable_totp(const char *acting_admin, const char *argument) {
    /*

    Purpose: Disables TOTP for an account

    Return: No value

    */

    char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";

    if (!cli_read_target_username(username, sizeof(username), argument)) {

        return;

    }

    if (!cli_prompt_yes_no("Disable 2FA for this account? [y/N]: ", 0)) {

        return;

    }

    if (!AUTH_DB_remove_totp(username, acting_admin, error, sizeof(error))) {

        fprintf(stderr, "Unable to disable 2FA: %s\n", error[0] ? error : "authentication operation failed");
        return;

    }

    printf("2FA disabled for '%s'.\n", username);
}

static void cli_set_role(const char *acting_admin, const char *username_argument, const char *role_argument) {
    /*

    Purpose: Sets a non-primary account to user or co-admin

    Return: No value

    */

    char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char role_text[32] = "";
    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";
    int role;

    if (!cli_read_target_username(username, sizeof(username), username_argument)) {

        return;

    }

    if (role_argument && role_argument[0]) {

        snprintf(role_text, sizeof(role_text), "%s", role_argument);

    }

    else if (!cli_read_line("Role (user/co-admin): ", role_text, sizeof(role_text), 0)) {

        return;

    }

    for (size_t index = 0; role_text[index]; index++) {
        role_text[index] = (char)tolower((unsigned char)role_text[index]);
    }

    if (strcmp(role_text, "user") == 0) {

        role = AUTH_ROLE_USER;

    }

    else if (strcmp(role_text, "co-admin") == 0 || strcmp(role_text, "coadmin") == 0) {

        role = AUTH_ROLE_CO_ADMIN;

    }

    else {

        fprintf(stderr, "Role must be 'user' or 'co-admin'.\n");
        return;

    }

    if (!AUTH_DB_set_role(username, role, acting_admin, error, sizeof(error))) {

        fprintf(stderr, "Unable to update role: %s\n", error[0] ? error : "authentication operation failed");
        return;

    }

    printf("Role for '%s' set to %s.\n", username, cli_role_name(role));
}

static void cli_delete_user(const char *acting_admin, const char *argument) {
    /*

    Purpose: Deletes a non-primary account after exact-name confirmation

    Return: No value

    */

    char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char confirmation[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
    char error[RETROSPECTRUM_CLI_ERROR_MAX] = "";

    if (!cli_read_target_username(username, sizeof(username), argument)) {

        return;

    }

    printf("Type '%s' to confirm deletion.\n", username);

    if (!cli_read_line("Confirmation: ", confirmation, sizeof(confirmation), 0) ||
        strcmp(username, confirmation) != 0) {

        fprintf(stderr, "Deletion cancelled.\n");
        return;

    }

    if (!AUTH_DB_delete_user(username, acting_admin, error, sizeof(error))) {

        fprintf(stderr, "Unable to delete user: %s\n", error[0] ? error : "authentication operation failed");
        return;

    }

    printf("User '%s' deleted.\n", username);
}

static void cli_print_privileged_commands(void) {
    /*

    Purpose: Prints administrator and co-administrator commands

    Return: No value

    */

    printf("Available commands:\n");
    printf("  status                        Display centralized server status\n");
    printf("  users                         Display all user accounts\n");
    printf("  create-user                   Create a user or co-admin interactively\n");
    printf("  reset-password [username]     Reset an account password\n");
    printf("  enable-2fa [username]         Enable or replace account 2FA\n");
    printf("  disable-2fa [username]        Disable account 2FA\n");
    printf("  set-role [username] [role]    Set role to user or co-admin\n");
    printf("  delete-user [username]        Delete a non-primary account\n");
    printf("  whoami                        Display the authenticated account\n");
    printf("  help                          Print this command list\n");
    printf("  logout                        End this session and return to login\n");
    printf("  exit                          Close the CLI\n\n");
}

static void cli_print_user_commands(void) {
    /*

    Purpose: Prints commands available to an ordinary user

    Return: No value

    */

    printf("Available commands:\n");
    printf("  logout                        End this session and return to login\n");
    printf("  exit                          Close the CLI\n\n");
}

static int cli_run_session(const char *username, int role) {
    /*

    Purpose: Runs one authenticated CLI command session

    Return: One to log out, zero to exit the process

    */

    char line[RETROSPECTRUM_CLI_LINE_MAX];

    printf("\nLogged in as %s (%s).\n\n", username, cli_role_name(role));

    if (cli_role_is_privileged(role)) {

        cli_print_privileged_commands();

    }

    else {

        cli_print_user_commands();

    }

    while (Global_Running) {
        char *command;
        char *argument_one;
        char *argument_two;

        if (!cli_read_line("retrospectrum> ", line, sizeof(line), 0)) {

            return 0;

        }

        command = strtok(line, " \t");

        if (!command) {

            continue;

        }

        argument_one = strtok(NULL, " \t");
        argument_two = strtok(NULL, " \t");

        if (strcmp(command, "logout") == 0) {

            printf("Logged out.\n\n");
            return 1;

        }

        if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {

            return 0;

        }

        if (!cli_role_is_privileged(role)) {

            if (strcmp(command, "help") == 0) {

                cli_print_user_commands();

            }

            else {

                fprintf(stderr, "Ordinary users may only log out or exit this CLI.\n");

            }
            continue;

        }

        if (strcmp(command, "status") == 0) {

            printf("%s\n", SECURE_NETWORK_server_is_running() ? "online" : "offline");

        }

        else if (strcmp(command, "users") == 0 || strcmp(command, "list-users") == 0) {

            cli_print_users();

        }

        else if (strcmp(command, "create-user") == 0) {

            cli_create_user(username);

        }

        else if (strcmp(command, "reset-password") == 0) {

            cli_reset_password(username, argument_one);

        }

        else if (strcmp(command, "enable-2fa") == 0) {

            cli_enable_totp(username, argument_one);

        }

        else if (strcmp(command, "disable-2fa") == 0) {

            cli_disable_totp(username, argument_one);

        }

        else if (strcmp(command, "set-role") == 0) {

            cli_set_role(username, argument_one, argument_two);

        }

        else if (strcmp(command, "delete-user") == 0) {

            cli_delete_user(username, argument_one);

        }

        else if (strcmp(command, "whoami") == 0) {

            printf("%s (%s)\n", username, cli_role_name(role));

        }

        else if (strcmp(command, "help") == 0) {

            cli_print_privileged_commands();

        }

        else if (strcmp(command, "clear") == 0) {

            fputs("\033[2J\033[H", stdout);
            fflush(stdout);

        }

        else {

            fprintf(stderr, "Unknown command. Type 'help' to list commands.\n");

        }
    }

    return 0;
}

static int run_management_cli(void) {
    /*

    Purpose: Runs RetroSpectrum in headless account-management mode

    Return: Process exit status

    */

    Type_Auth_User_Summary users[RETROSPECTRUM_CLI_MAX_USERS];
    size_t user_count = 0;

    printf("RetroSpectrum Server Administration CLI\n");
    printf("=======================================\n\n");

    if (!cli_load_users(users, RETROSPECTRUM_CLI_MAX_USERS, &user_count)) {

        return 1;

    }

    if (user_count == 0 && !cli_bootstrap_primary_admin()) {

        return 1;

    }

    while (Global_Running) {
        char username[AUTH_PUBLIC_USERNAME_MAX + 1] = "";
        int role = AUTH_ROLE_USER;

        if (!cli_authenticate(username, sizeof(username), &role)) {

            if (!Global_Running || feof(stdin)) {

                break;

            }

            printf("\n");
            continue;

        }

        if (!cli_run_session(username, role)) {

            break;

        }
    }

    printf("Administration CLI closed.\n");
    return 0;
}

// =====================
// Command Line Handling
// =====================

static void print_command_line_usage(const char *program_name, FILE *stream) {
    /*

    Purpose: Prints supported command-line arguments

    Return: No value

    */

    fprintf(stream, "Usage:\n");
    fprintf(stream, "  %s -S -o record_dir [--database-key absolute_path]\n", program_name);
    fprintf(stream, "  %s -C -o record_dir\n", program_name);
    fprintf(stream, "  %s -S --cli [--database-key absolute_path]\n\n", program_name);
    fprintf(stream, "Options:\n");
    fprintf(stream, "  -S, --server           Run as the centralized server and listen on TCP 47742.\n");
    fprintf(stream, "  -C, --client           Run as an endpoint client without starting a server.\n");
    fprintf(stream, "  -o record_dir          Required recording directory in GUI mode.\n");
    fprintf(stream, "  --cli, --admin-cli     Run the headless server administration CLI.\n");
    fprintf(stream, "  --database-key path    Select an existing 32-byte SQLCipher master-key file.\n");
    fprintf(stream, "  -h, --help             Show this help text.\n");
}

static int parse_command_line_args(int argc, char **argv) {
    /*

    Purpose: Parses supported command-line arguments

    Return: Parse status

    */

    int output_dir_provided = 0;

    if (argc <= 1) {

        print_command_line_usage(argv[0], stderr);
        return 0;

    }

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "-o") == 0) {

            if (i + 1 >= argc) {

                fprintf(stderr, "Missing value for -o record directory.\n");
                print_command_line_usage(argv[0], stderr);
                return 0;

            }

            if (snprintf(Global_Record_Dir, sizeof(Global_Record_Dir), "%s", argv[i + 1]) >=
                (int)sizeof(Global_Record_Dir)) {

                fprintf(stderr, "The recording directory path is too long.\n");
                return 0;

            }
            output_dir_provided = 1;
            i++;

        }

        else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--server") == 0) {

            if (Global_Network_Mode == NETWORK_MODE_CLIENT) {

                fprintf(stderr, "-S/--server and -C/--client cannot be used together.\n");
                return 0;

            }
            Global_Network_Mode = NETWORK_MODE_SERVER;

        }

        else if (strcmp(argv[i], "-C") == 0 || strcmp(argv[i], "--client") == 0) {

            if (Global_Network_Mode == NETWORK_MODE_SERVER) {

                fprintf(stderr, "-S/--server and -C/--client cannot be used together.\n");
                return 0;

            }
            Global_Network_Mode = NETWORK_MODE_CLIENT;

        }

        else if (strcmp(argv[i], "--cli") == 0 || strcmp(argv[i], "--admin-cli") == 0 || strcmp(argv[i], "-c") == 0) {

            Global_CLI_Mode = 1;

        }

        else if (strcmp(argv[i], "--database-key") == 0) {

            if (i + 1 >= argc) {

                fprintf(stderr, "Missing value for --database-key.\n");
                print_command_line_usage(argv[0], stderr);
                return 0;

            }

            if (snprintf(Global_Database_Key_Path, sizeof(Global_Database_Key_Path), "%s", argv[i + 1]) >=
                (int)sizeof(Global_Database_Key_Path)) {

                fprintf(stderr, "The database key-file path is too long.\n");
                return 0;

            }
            i++;

        }

        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {

            Global_Help_Requested = 1;
            print_command_line_usage(argv[0], stdout);
            return 0;

        }

        else {

            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_command_line_usage(argv[0], stderr);
            return 0;

        }
    }

    if (Global_CLI_Mode) {

        if (Global_Network_Mode == NETWORK_MODE_CLIENT) {

            fprintf(stderr, "The administration CLI can only run in server mode. Use -S --cli.\n");
            return 0;

        }

        if (Global_Network_Mode == NETWORK_MODE_UNSET) {

            Global_Network_Mode = NETWORK_MODE_SERVER;

        }

    }

    else {

        if (Global_Network_Mode == NETWORK_MODE_UNSET) {

            fprintf(stderr, "Select exactly one runtime mode: -S for server or -C for client.\n");
            print_command_line_usage(argv[0], stderr);
            return 0;

        }

        if (!output_dir_provided) {

            fprintf(stderr, "Missing required -o record directory for GUI mode.\n");
            print_command_line_usage(argv[0], stderr);
            return 0;

        }

    }

    return 1;
}

// ==========
// Main Logic
// ==========

int main(int argc, char **argv) {
    /*

    Purpose: Runs the RetroSpectrum application event loop

    Return: Exit status

    */

    signal(SIGINT, handle_sigint);

    if (!parse_command_line_args(argc, argv)) {

        return Global_Help_Requested ? 0 : 1;

    }

    SERVER_IDENTITY_set_server_mode(Global_Network_Mode == NETWORK_MODE_SERVER);
    AUTH_set_client_only_mode(Global_Network_Mode == NETWORK_MODE_CLIENT);

    if (Global_Database_Key_Path[0]) {

        char database_key_error[RETROSPECTRUM_CLI_ERROR_MAX] = "";

        if (!DATABASE_CRYPTO_set_key_path(Global_Database_Key_Path, database_key_error, sizeof(database_key_error))) {

            fprintf(stderr, "Unable to use database key file: %s\n",
                    database_key_error[0] ? database_key_error : "key validation failed");
            return 1;

        }

    }

    if (Global_CLI_Mode) {

        char secure_network_error[RETROSPECTRUM_CLI_ERROR_MAX] = "";
        int cli_exit_status;

        if (!SERVER_IDENTITY_start()) {

            fprintf(stderr, "Unable to initialize the cryptographic RetroSpectrum server identity.\n");

        }

        else if (!SECURE_NETWORK_start_server(secure_network_error, sizeof(secure_network_error))) {

            fprintf(stderr, "Secure LAN server unavailable: %s\n",
                    secure_network_error[0] ? secure_network_error : "server startup failed");

        }

        cli_exit_status = run_management_cli();

        SECURE_NETWORK_stop_server();
        SERVER_IDENTITY_stop();
        return cli_exit_status;

    }

    memset(&ring_buf, 0, sizeof(ring_buf));

    pthread_mutex_init(&ring_buf.lock, NULL);

    if (!pre_cache_init(&Global_Pre_Cache, Global_Sample_Rate_Hz)) {

        fprintf(stderr, "pre-cache allocation failed\n");
        return 1;

    }

    if (!rec_queue_init(&Global_Rec_Queue, Global_Sample_Rate_Hz)) {

        fprintf(stderr, "record queue allocation failed\n");
        pre_cache_free(&Global_Pre_Cache);
        return 1;

    }

    hackrf_device *dev = NULL;

    int hackrf_init_result = hackrf_init();

    if (hackrf_init_result == HACKRF_SUCCESS) {

        Global_HackRF_Library_Initialized = 1;

        int hackrf_open_result = hackrf_open(&dev);

        if (hackrf_open_result == HACKRF_SUCCESS && dev) {

            Global_HackRF_Device = dev;

            if (apply_radio_settings(dev, Global_Center_Freq_Hz, Global_Sample_Rate_Hz, Global_Display_Span_Hz,
                                     Global_LNA_Gain, Global_VGA_Gain, Global_Amp_Enable)) {

                Global_HackRF_Connected = 1;

            }

            else {

                fprintf(stderr, "HackRF One detected but initial configuration failed; SDR tab disabled.\n");
                stop_radio(dev);
                hackrf_close(dev);
                dev = NULL;
                Global_HackRF_Device = NULL;

            }

        }

        else {

            fprintf(stderr, "HackRF One not connected; SDR tab disabled.\n");
            dev = NULL;
            Global_HackRF_Device = NULL;

        }

    }

    else {

        fprintf(stderr, "HackRF library initialization failed; SDR tab disabled.\n");

    }

    fftw_complex *time_domain = fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    fftw_complex *freq_domain = fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);

    double *hann_window = malloc(sizeof(double) * FFT_SIZE);
    double *db = malloc(sizeof(double) * FFT_SIZE);

    if (!time_domain || !freq_domain || !hann_window || !db) {

        fprintf(stderr, "allocation failed\n");

        if (dev) {

            stop_radio(dev);
            hackrf_close(dev);
            dev = NULL;
            Global_HackRF_Device = NULL;

        }

        if (Global_HackRF_Library_Initialized) {

            hackrf_exit();
            Global_HackRF_Library_Initialized = 0;

        }
        return 1;

    }

    for (int n = 0; n < FFT_SIZE; n++) {
        hann_window[n] = 0.5 - 0.5 * cos((2.0 * M_PI * n) / (FFT_SIZE - 1));
    }

    fftw_plan plan = fftw_plan_dft_1d(FFT_SIZE, time_domain, freq_domain, FFTW_FORWARD, FFTW_MEASURE);

    if (!plan) {

        fprintf(stderr, "fftw plan creation failed\n");

        if (dev) {

            stop_radio(dev);
            hackrf_close(dev);
            dev = NULL;
            Global_HackRF_Device = NULL;

        }

        if (Global_HackRF_Library_Initialized) {

            hackrf_exit();
            Global_HackRF_Library_Initialized = 0;

        }
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

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {

        fprintf(stderr, "IMG_Init PNG warning: %s\n", IMG_GetError());

    }

    SDL_Window *window_sdl = SDL_CreateWindow("RetroSpectrum", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400,
                                              820, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

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

    SDL_Texture *waterfall_texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);

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

    if (!SERVER_IDENTITY_start()) {

        fprintf(stderr, "Unable to initialize the cryptographic RetroSpectrum identity service.\n");
        Global_Running = 0;

    }

    else if (Global_Network_Mode == NETWORK_MODE_SERVER) {

        char secure_network_error[256] = "";

        if (!SECURE_NETWORK_start_server(secure_network_error, sizeof(secure_network_error))) {

            fprintf(stderr, "Secure LAN server unavailable: %s\n", secure_network_error);

        }

    }

    else {

        fprintf(stderr, "RetroSpectrum client mode: no local server listener was started.\n");

    }

    if (Global_Running) {

        if (!AUTH_run(window_sdl, renderer, font_small, font_medium)) {

            Global_Running = 0;

        }

        else {

            fprintf(stderr, "RetroSpectrum server: %s\n", AUTH_get_server_name());

        }

    }

    Type_Dashboard_State dashboard;

    if (!dashboard_init(&dashboard, "world_map.bin")) {

        set_status("Dashboard map not loaded: world_map.bin", (SDL_Color){255, 180, 40, 255});

    }

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
    int main_input_cursors[7];

    main_reset_input_cursors(main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box, &vga_box, &fps_box,
                             &rows_box);

    uint64_t next_waterfall_ms = SDL_GetTicks64();
    uint64_t next_hackrf_health_ms = SDL_GetTicks64() + 1000;
    uint64_t next_server_connection_check_ms = SDL_GetTicks64() + SERVER_CONNECTION_CHECK_MS;

    while (Global_Running) {
        int logout_requested = 0;
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(window_sdl, &win_w, &win_h);

        int station_win_h = win_h - RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

        if (station_win_h < 240) {

            station_win_h = 240;

        }

        SDL_Rect amp_box;
        SDL_Rect dc_box;
        SDL_Rect cache_box;
        SDL_Rect sel_button;
        SDL_Rect rec_button;

        layout_controls(win_w, &freq_box, &sr_box, &display_box, &lna_box, &vga_box, &fps_box, &rows_box, &amp_box,
                        &dc_box, &sel_button, &rec_button);

        cache_box = amp_box;
        amp_box.y = 12;
        cache_box.y = 40;
        dc_box.y = 68;

        int waterfall_x = MARGIN;
        int waterfall_y = CONTROL_PANEL_HEIGHT + 12;
        int waterfall_w = win_w - 2 * MARGIN;
        int waterfall_h = station_win_h - waterfall_y - AXIS_HEIGHT - 25;

        if (waterfall_h < 100) {

            waterfall_h = 100;

        }

        SDL_Rect waterfall_rect = {waterfall_x, waterfall_y, waterfall_w, waterfall_h};

        SDL_Event event;

        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_QUIT) {

                Global_Running = 0;
                break;

            }

            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {

                SDL_Keycode key = event.key.keysym.sym;
                SDL_Keymod modifiers = (SDL_Keymod)event.key.keysym.mod;

                if (key == SDLK_ESCAPE) {

                    Global_Running = 0;
                    break;

                }

                if (key == SDLK_l && (modifiers & KMOD_CTRL) != 0) {

                    logout_requested = 1;
                    break;

                }

                if (key == SDLK_F11) {

                    toggle_fullscreen(window_sdl);
                    continue;

                }

            }

            int text_entry_active =
                (active != FIELD_NONE) ||
                (dashboard.enabled && (dashboard.case_desc_editing || dashboard.case_search_active)) ||
                (Global_Classification_Mode && CLASSIFICATION_is_text_entry_active()) ||
                (Global_CaseManagement_Mode && CASE_MANAGEMENT_is_text_entry_active()) ||
                (Global_Decode_Mode && DECODE_is_text_entry_active()) ||
                (Global_Analysis_Mode && ANALYSIS_is_text_entry_active());

            int top_tab_event = dashboard_handle_top_tab_event(&dashboard, &event, win_w, text_entry_active);

            if (top_tab_event != DASHBOARD_EVENT_NONE) {

                if (top_tab_event == DASHBOARD_EVENT_MAP) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 1;
                    dashboard.current_tab = DASHBOARD_EVENT_MAP;
                    set_status("Dashboard", (SDL_Color){0, 255, 80, 255});

                }

                else if (top_tab_event == DASHBOARD_EVENT_RETROSPECTRUM) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_RETROSPECTRUM;
                    set_status("RetroSpectrum Workstation", (SDL_Color){0, 255, 80, 255});

                }

                else if (top_tab_event == DASHBOARD_EVENT_ANALYSIS) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (!Global_Analysis_Mode) {

                        ANALYSIS_enter_mode(Global_Record_Dir, Global_Center_Freq_Hz, Global_Rec_Out_Rate_Hz,
                                            Global_Sample_Rate_Hz);

                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_ANALYSIS;

                }

                else if (top_tab_event == DASHBOARD_EVENT_CLASSIFICATION) {

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    if (!Global_Classification_Mode) {

                        CLASSIFICATION_enter_mode(Global_Record_Dir);

                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_CLASSIFICATION;
                    set_status("Classification Workstation", (SDL_Color){0, 255, 80, 255});

                }

                else if (top_tab_event == DASHBOARD_EVENT_DECODE) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    if (!Global_Decode_Mode) {

                        DECODE_enter_mode(Global_Record_Dir);

                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_DECODE;
                    set_status("Decode Workstation", (SDL_Color){0, 255, 80, 255});

                }

                else if (top_tab_event == DASHBOARD_EVENT_CASE_MANAGEMENT) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    if (!Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_enter_mode(Global_Record_Dir);

                    }
                    active = FIELD_NONE;
                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_CASE_MANAGEMENT;
                    set_status("Case Management Workstation", (SDL_Color){0, 255, 80, 255});

                }
                continue;

            }

            if (!dashboard.enabled) {

                if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {

                    if (event.button.y < RETROSPECTRUM_DASHBOARD_TAB_BAR_H) {

                        continue;

                    }
                    event.button.y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

                }

                else if (event.type == SDL_MOUSEMOTION) {

                    if (event.motion.y < RETROSPECTRUM_DASHBOARD_TAB_BAR_H) {

                        continue;

                    }
                    event.motion.y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

                }

            }

            if (dashboard.enabled) {

                int dashboard_event_result = dashboard_handle_event(&dashboard, &event, win_w, win_h);

                if (dashboard_event_result == DASHBOARD_EVENT_QUIT) {

                    Global_Running = 0;

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_RETROSPECTRUM) {

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_RETROSPECTRUM;
                    Global_Analysis_Mode = 0;
                    Global_Classification_Mode = 0;

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }
                    set_status("RetroSpectrum Workstation", (SDL_Color){0, 255, 80, 255});

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_ANALYSIS) {

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_ANALYSIS;
                    Global_Classification_Mode = 0;

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }

                    ANALYSIS_enter_mode(Global_Record_Dir, Global_Center_Freq_Hz, Global_Rec_Out_Rate_Hz,
                                        Global_Sample_Rate_Hz);

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_CLASSIFICATION) {

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_CLASSIFICATION;
                    Global_Analysis_Mode = 0;

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }
                    CLASSIFICATION_enter_mode(Global_Record_Dir);
                    set_status("Classification Workstation", (SDL_Color){0, 255, 80, 255});

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_DECODE) {

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_DECODE;

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }
                    DECODE_enter_mode(Global_Record_Dir);
                    set_status("Decode Workstation", (SDL_Color){0, 255, 80, 255});

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_CASE_MANAGEMENT) {

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_CASE_MANAGEMENT;

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }
                    CASE_MANAGEMENT_enter_mode(Global_Record_Dir);
                    set_status("Case Management Workstation", (SDL_Color){0, 255, 80, 255});

                }

                else if (dashboard_event_result == DASHBOARD_EVENT_MAP) {

                    dashboard.current_tab = DASHBOARD_EVENT_MAP;

                }

                continue;

            }

            if (Global_Decode_Mode) {

                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d && (SDL_GetModState() & KMOD_CTRL)) {

                    DECODE_exit_mode();
                    dashboard.enabled = 1;
                    dashboard.current_tab = DASHBOARD_EVENT_MAP;
                    set_status("Dashboard", (SDL_Color){0, 255, 80, 255});
                    continue;

                }

                DECODE_handle_event(&event, win_w, station_win_h);
                continue;

            }

            if (Global_CaseManagement_Mode) {

                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d && (SDL_GetModState() & KMOD_CTRL)) {

                    CASE_MANAGEMENT_exit_mode();
                    dashboard.enabled = 1;
                    dashboard.current_tab = DASHBOARD_EVENT_MAP;
                    set_status("Dashboard", (SDL_Color){0, 255, 80, 255});
                    continue;

                }

                CASE_MANAGEMENT_handle_event(&event, win_w, station_win_h);
                continue;

            }

            if (Global_Classification_Mode) {

                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_d && (SDL_GetModState() & KMOD_CTRL)) {

                    CLASSIFICATION_exit_mode();
                    dashboard.enabled = 1;
                    dashboard.current_tab = DASHBOARD_EVENT_MAP;
                    set_status("Dashboard", (SDL_Color){0, 255, 80, 255});
                    continue;

                }

                int classification_event_result = CLASSIFICATION_handle_event(&event, win_w, station_win_h);

                if (classification_event_result == 2) {

                    CLASSIFICATION_exit_mode();
                    ANALYSIS_enter_mode(Global_Record_Dir, Global_Center_Freq_Hz, Global_Rec_Out_Rate_Hz,
                                        Global_Sample_Rate_Hz);

                }

                continue;

            }

            if (event.type == SDL_KEYDOWN) {

                SDL_Keycode key = event.key.keysym.sym;

                if (key == SDLK_d && active == FIELD_NONE && (SDL_GetModState() & KMOD_CTRL)) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_CaseManagement_Mode) {

                        CASE_MANAGEMENT_exit_mode();

                    }

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    dashboard.enabled = 1;
                    dashboard.current_tab = DASHBOARD_EVENT_MAP;
                    set_status("Dashboard", (SDL_Color){0, 255, 80, 255});
                    continue;

                }

                if (Global_Analysis_Mode) {

                    int analysis_event_result =
                        ANALYSIS_handle_event(&event, win_w, station_win_h, pixels, tex_w, tex_h, waterfall_texture,
                                              &next_waterfall_ms, &active);

                    if (analysis_event_result == ANALYSIS_EVENT_QUIT) {

                        Global_Running = 0;

                    }

                    if (analysis_event_result != ANALYSIS_EVENT_IGNORED) {

                        continue;

                    }

                }

                if (key == SDLK_g && active == FIELD_NONE) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_Decode_Mode) {

                        DECODE_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    else {

                        ANALYSIS_enter_mode(Global_Record_Dir, Global_Center_Freq_Hz, Global_Rec_Out_Rate_Hz,
                                            Global_Sample_Rate_Hz);

                    }

                    dashboard.enabled = 0;
                    dashboard.current_tab = DASHBOARD_EVENT_ANALYSIS;

                    continue;

                }

                if (key == SDLK_h && active == FIELD_NONE) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();
                        dashboard.enabled = 0;
                        dashboard.current_tab = DASHBOARD_EVENT_RETROSPECTRUM;
                        set_status("", (SDL_Color){0, 255, 80, 255});

                    }

                    else {

                        if (Global_Decode_Mode) {

                            DECODE_exit_mode();

                        }

                        if (Global_Analysis_Mode) {

                            ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                            next_waterfall_ms = SDL_GetTicks64();

                        }

                        CLASSIFICATION_enter_mode(Global_Record_Dir);
                        dashboard.enabled = 0;
                        dashboard.current_tab = DASHBOARD_EVENT_CLASSIFICATION;

                        set_status("Classification Workstation", (SDL_Color){0, 255, 80, 255});

                    }

                    continue;

                }

                if (key == SDLK_c && active == FIELD_NONE && Global_Analysis_Mode && !(SDL_GetModState() & KMOD_CTRL)) {

                    char export_file_name[512];
                    double export_frequency_mhz = 0.0;
                    double export_bandwidth_khz = 0.0;
                    double export_start_time = 0.0;
                    double export_end_time = 0.0;

                    if (ANALYSIS_export_classification_fields(export_file_name, sizeof(export_file_name),
                                                              &export_frequency_mhz, &export_bandwidth_khz,
                                                              &export_start_time, &export_end_time)) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                        CLASSIFICATION_enter_mode(Global_Record_Dir);
                        dashboard.enabled = 0;
                        dashboard.current_tab = DASHBOARD_EVENT_CLASSIFICATION;
                        CLASSIFICATION_prefill_from_analysis_selection(export_file_name, export_frequency_mhz,
                                                                       export_bandwidth_khz, export_start_time,
                                                                       export_end_time);

                        set_status("Classification Workstation", (SDL_Color){0, 255, 80, 255});

                    }

                    continue;

                }

                if (Global_Analysis_Mode) {

                    int analysis_event_result =
                        ANALYSIS_handle_event(&event, win_w, station_win_h, pixels, tex_w, tex_h, waterfall_texture,
                                              &next_waterfall_ms, &active);

                    if (analysis_event_result == ANALYSIS_EVENT_QUIT) {

                        Global_Running = 0;

                    }

                    if (analysis_event_result != ANALYSIS_EVENT_IGNORED) {

                        continue;

                    }

                }

                if (!Global_HackRF_Connected && !dashboard.enabled && !Global_Analysis_Mode &&
                    !Global_Classification_Mode && !Global_Decode_Mode && !Global_CaseManagement_Mode) {

                    active = FIELD_NONE;
                    continue;

                }

                if (event.key.keysym.sym == SDLK_ESCAPE) {

                    if (active != FIELD_NONE) {

                        active = FIELD_NONE;

                    }

                    else {

                        Global_Running = 0;

                    }

                }

                else if (event.key.keysym.sym == SDLK_TAB) {

                    if (active == FIELD_NONE) {

                        active = FIELD_FREQ;

                    }

                    else if (active == FIELD_FREQ) {

                        active = FIELD_SR;

                    }

                    else if (active == FIELD_SR) {

                        active = FIELD_DISPLAY;

                    }

                    else if (active == FIELD_DISPLAY) {

                        active = FIELD_LNA;

                    }

                    else if (active == FIELD_LNA) {

                        active = FIELD_VGA;

                    }

                    else if (active == FIELD_VGA) {

                        active = FIELD_FPS;

                    }

                    else if (active == FIELD_FPS) {

                        active = FIELD_ROWS;

                    }

                    else {

                        active = FIELD_FREQ;

                    }

                    main_set_active_cursor_end(active, main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box,
                                               &vga_box, &fps_box, &rows_box);

                }

                else if (event.key.keysym.sym == SDLK_LEFT && active != FIELD_NONE) {

                    main_move_active_cursor(active, main_input_cursors, -1, &freq_box, &sr_box, &display_box, &lna_box,
                                            &vga_box, &fps_box, &rows_box);

                }

                else if (event.key.keysym.sym == SDLK_RIGHT && active != FIELD_NONE) {

                    main_move_active_cursor(active, main_input_cursors, 1, &freq_box, &sr_box, &display_box, &lna_box,
                                            &vga_box, &fps_box, &rows_box);

                }

                else if (event.key.keysym.sym == SDLK_BACKSPACE) {

                    size_t text_size = 0;
                    int index = main_field_index(active);
                    char *text = main_field_text(active, &freq_box, &sr_box, &display_box, &lna_box, &vga_box, &fps_box,
                                                 &rows_box, &text_size);
                    (void)text_size;

                    if (index >= 0 && index < 7 && text) {

                        main_backspace_at_cursor(text, &main_input_cursors[index]);

                    }

                }

                else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {

                    apply_from_inputs(dev, &freq_box, &sr_box, &display_box, &lna_box, &vga_box, &fps_box, &rows_box,
                                      pixels, tex_w, tex_h);

                    main_reset_input_cursors(main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box, &vga_box,
                                             &fps_box, &rows_box);

                    next_waterfall_ms = SDL_GetTicks64();

                }

                else if (event.key.keysym.sym == SDLK_q && active == FIELD_NONE) {

                    Global_Running = 0;

                }

            }

            if (Global_Analysis_Mode && event.type != SDL_KEYDOWN) {

                int analysis_event_result = ANALYSIS_handle_event(&event, win_w, station_win_h, pixels, tex_w, tex_h,
                                                                  waterfall_texture, &next_waterfall_ms, &active);

                if (analysis_event_result == ANALYSIS_EVENT_QUIT) {

                    Global_Running = 0;

                }

                if (analysis_event_result != ANALYSIS_EVENT_IGNORED) {

                    continue;

                }

            }

            if (!Global_HackRF_Connected && !dashboard.enabled && !Global_Analysis_Mode &&
                !Global_Classification_Mode && !Global_Decode_Mode && !Global_CaseManagement_Mode) {

                active = FIELD_NONE;
                continue;

            }

            if (event.type == SDL_TEXTINPUT) {

                size_t text_size = 0;
                int index = main_field_index(active);
                char *text = main_field_text(active, &freq_box, &sr_box, &display_box, &lna_box, &vga_box, &fps_box,
                                             &rows_box, &text_size);

                if (index >= 0 && index < 7 && text) {

                    main_insert_text_at_cursor(text, text_size, &main_input_cursors[index], event.text.text);

                }

            }

            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {

                int x = event.button.x;
                int y = event.button.y;

                if (point_in_rect(x, y, freq_box.rect)) {

                    active = FIELD_FREQ;
                    main_set_active_cursor_end(active, main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box,
                                               &vga_box, &fps_box, &rows_box);

                }

                else if (point_in_rect(x, y, sr_box.rect)) {

                    active = FIELD_SR;
                    main_set_active_cursor_end(active, main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box,
                                               &vga_box, &fps_box, &rows_box);

                }

                else if (point_in_rect(x, y, display_box.rect)) {

                    active = FIELD_DISPLAY;
                    main_set_active_cursor_end(active, main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box,
                                               &vga_box, &fps_box, &rows_box);

                }

                else if (point_in_rect(x, y, lna_box.rect)) {

                    active = FIELD_LNA;
                    main_set_active_cursor_end(active, main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box,
                                               &vga_box, &fps_box, &rows_box);

                }

                else if (point_in_rect(x, y, vga_box.rect)) {

                    active = FIELD_VGA;
                    main_set_active_cursor_end(active, main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box,
                                               &vga_box, &fps_box, &rows_box);

                }

                else if (point_in_rect(x, y, fps_box.rect)) {

                    active = FIELD_FPS;
                    main_set_active_cursor_end(active, main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box,
                                               &vga_box, &fps_box, &rows_box);

                }

                else if (point_in_rect(x, y, rows_box.rect)) {

                    active = FIELD_ROWS;
                    main_set_active_cursor_end(active, main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box,
                                               &vga_box, &fps_box, &rows_box);

                }

                else if (point_in_rect(x, y, cache_box)) {

                    active = FIELD_NONE;
                    Global_Cached_Recording = !Global_Cached_Recording;

                    set_status(Global_Cached_Recording ? "Cached recording enabled" : "Cached recording disabled",
                               Global_Cached_Recording ? (SDL_Color){0, 255, 90, 255}
                                                       : (SDL_Color){150, 150, 150, 255});

                }

                else if (point_in_rect(x, y, amp_box)) {

                    Global_Amp_Enable = !Global_Amp_Enable;

                    apply_from_inputs(dev, &freq_box, &sr_box, &display_box, &lna_box, &vga_box, &fps_box, &rows_box,
                                      pixels, tex_w, tex_h);

                    main_reset_input_cursors(main_input_cursors, &freq_box, &sr_box, &display_box, &lna_box, &vga_box,
                                             &fps_box, &rows_box);

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

                    }

                    else {

                        set_status("Selector locked while recording", (SDL_Color){255, 180, 40, 255});

                    }

                }

                else if (point_in_rect(x, y, rec_button)) {

                    active = FIELD_NONE;

                    if (Global_Rec) {

                        stop_recording();

                    }

                    else if (Global_Selector.enabled) {

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

                if (!Global_Rec && Global_Selector.enabled &&
                    (Global_Selector.dragging || Global_Selector.resizing_left || Global_Selector.resizing_right)) {

                    update_selection_from_mouse(event.motion.x, waterfall_rect);

                }

            }
        }

        uint64_t now_ms = SDL_GetTicks64();

        if (now_ms >= next_server_connection_check_ms) {

            if (SECURE_NETWORK_remote_connection_lost()) {

                fprintf(stderr, "Remote server connection lost; ending the authenticated session.\n");
                logout_requested = 1;

            }
            next_server_connection_check_ms = now_ms + SERVER_CONNECTION_CHECK_MS;

        }

        retrospectrum_pump_transmission();

        if (Global_HackRF_Connected && dev && !RETROSPECTRUM_transmission_is_active() &&
            now_ms >= next_hackrf_health_ms) {

            int streaming_state = hackrf_is_streaming(dev);

            if (streaming_state != HACKRF_TRUE) {

                if (Global_Rec) {

                    stop_recording();

                }

                Global_HackRF_Connected = 0;
                Global_Radio_Running = 0;
                Global_Selector.enabled = 0;
                Global_Selector.dragging = 0;
                Global_Selector.resizing_left = 0;
                Global_Selector.resizing_right = 0;
                active = FIELD_NONE;

                hackrf_close(dev);
                dev = NULL;
                Global_HackRF_Device = NULL;

                fprintf(stderr, "HackRF One disconnected; SDR tab disabled.\n");

            }

            next_hackrf_health_ms = now_ms + 1000;

        }

        uint64_t frame_interval_ms = 1000 / (uint64_t)normalize_fps(Global_Waterfall_FPS);

        if (frame_interval_ms < 1) {

            frame_interval_ms = 1;

        }

        if (Global_HackRF_Connected && !dashboard.enabled && !Global_Analysis_Mode && !Global_Classification_Mode &&
            !Global_Decode_Mode && !Global_CaseManagement_Mode && now_ms >= next_waterfall_ms) {

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
        SDL_RenderSetViewport(renderer, NULL);

        if (!dashboard.enabled) {

            SDL_Rect station_viewport = {0, RETROSPECTRUM_DASHBOARD_TAB_BAR_H, win_w, station_win_h};
            SDL_RenderSetViewport(renderer, &station_viewport);

        }

        if (Global_HackRF_Connected && !dashboard.enabled && !Global_Analysis_Mode && !Global_Classification_Mode &&
            !Global_Decode_Mode && !Global_CaseManagement_Mode) {

            Type_Input_Box draw_freq_box;
            Type_Input_Box draw_sr_box;
            Type_Input_Box draw_display_box;
            Type_Input_Box draw_lna_box;
            Type_Input_Box draw_vga_box;
            Type_Input_Box draw_fps_box;
            Type_Input_Box draw_rows_box;

            main_make_cursor_box(&draw_freq_box, &freq_box, active == FIELD_FREQ, main_input_cursors[0]);
            main_make_cursor_box(&draw_sr_box, &sr_box, active == FIELD_SR, main_input_cursors[1]);
            main_make_cursor_box(&draw_display_box, &display_box, active == FIELD_DISPLAY, main_input_cursors[2]);
            main_make_cursor_box(&draw_lna_box, &lna_box, active == FIELD_LNA, main_input_cursors[3]);
            main_make_cursor_box(&draw_vga_box, &vga_box, active == FIELD_VGA, main_input_cursors[4]);
            main_make_cursor_box(&draw_fps_box, &fps_box, active == FIELD_FPS, main_input_cursors[5]);
            main_make_cursor_box(&draw_rows_box, &rows_box, active == FIELD_ROWS, main_input_cursors[6]);

            draw_control_panel(renderer, font_medium, win_w, &draw_freq_box, &draw_sr_box, &draw_display_box,
                               &draw_lna_box, &draw_vga_box, &draw_fps_box, &draw_rows_box, amp_box, dc_box, sel_button,
                               rec_button, active);

            draw_checkbox(renderer, font_medium, cache_box, "Cache 5 sec", Global_Cached_Recording);

        }

        if (!Global_Running) {

            break;

        }

        if (logout_requested) {

            if (RETROSPECTRUM_transmission_is_active()) {

                retrospectrum_finalize_transmission(1, 1);

            }

            active = FIELD_NONE;

            if (Global_Classification_Mode) {

                CLASSIFICATION_exit_mode();

            }

            if (Global_CaseManagement_Mode) {

                CASE_MANAGEMENT_exit_mode();

            }

            if (Global_Decode_Mode) {

                DECODE_exit_mode();

            }

            if (Global_Analysis_Mode) {

                ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);

            }

            if (Global_Rec) {

                stop_recording();

            }

            Global_Selector.dragging = 0;
            Global_Selector.resizing_left = 0;
            Global_Selector.resizing_right = 0;

            SECURE_NETWORK_disconnect();
            dashboard_shutdown();

            if (!dashboard_init(&dashboard, "world_map.bin")) {

                set_status("Dashboard map not loaded: world_map.bin", (SDL_Color){255, 180, 40, 255});

            }

            if (!AUTH_run(window_sdl, renderer, font_small, font_medium)) {

                Global_Running = 0;
                break;

            }

            fprintf(stderr, "RetroSpectrum server: %s\n", AUTH_get_server_name());
            next_waterfall_ms = SDL_GetTicks64();
            next_server_connection_check_ms = SDL_GetTicks64() + SERVER_CONNECTION_CHECK_MS;
            continue;

        }

        if (dashboard.enabled) {

            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);

            dashboard_draw(&dashboard, renderer, font_small, font_medium, win_w, win_h, mouse_x, mouse_y);

        }

        else if (Global_Analysis_Mode) {

            ANALYSIS_draw_workstation(renderer, font_small, waterfall_texture, pixels, tex_w, tex_h, win_w,
                                      station_win_h);

            ANALYSIS_draw_workstation_overlays(renderer, font_small, waterfall_texture, tex_w, tex_h, win_w,
                                               station_win_h);

        }

        else if (Global_Classification_Mode) {

            CLASSIFICATION_draw_workstation(renderer, font_small, win_w, station_win_h);

        }

        else if (Global_Decode_Mode) {

            DECODE_draw_workstation(renderer, font_small, win_w, station_win_h);

        }

        else if (Global_CaseManagement_Mode) {

            CASE_MANAGEMENT_draw_workstation(renderer, font_small, win_w, station_win_h);

        }

        else if (Global_HackRF_Connected) {

            SDL_RenderCopy(renderer, waterfall_texture, NULL, &waterfall_rect);
            draw_selection_overlay(renderer, waterfall_rect);
            draw_selector_bandwidth(renderer, font_small, waterfall_rect);
            draw_border(renderer, waterfall_rect);
            draw_frequency_axis(renderer, font_small, waterfall_rect);

        }

        else {

            draw_hackrf_disconnected(renderer, font_medium, win_w, station_win_h);

        }

        if (Global_HackRF_Connected && !dashboard.enabled && !Global_Analysis_Mode && !Global_Classification_Mode &&
            !Global_Decode_Mode && !Global_CaseManagement_Mode) {

            int status_w = 0;
            int status_h = 0;

            if (font_medium && TTF_SizeText(font_medium, Global_Status_Msg, &status_w, &status_h) != 0) {

                status_w = 0;
                status_h = 0;

            }

            draw_text(renderer, font_medium, Global_Status_Msg, (win_w - status_w) / 2, station_win_h - 36,
                      Global_Status_Color);

        }

        if (Global_HackRF_Connected && !dashboard.enabled && !Global_Analysis_Mode && !Global_Classification_Mode &&
            !Global_Decode_Mode && !Global_CaseManagement_Mode) {

            draw_antenna_recommendation(renderer, font_small, win_w, station_win_h);

            draw_made_in_usa(renderer, font_medium, win_w, station_win_h);

        }

        SDL_RenderSetViewport(renderer, NULL);

        int tab_mouse_x = 0;
        int tab_mouse_y = 0;
        SDL_GetMouseState(&tab_mouse_x, &tab_mouse_y);
        dashboard_draw_top_bar(renderer, font_small, font_medium, win_w, tab_mouse_x, tab_mouse_y,
                               dashboard.current_tab);

        SDL_RenderPresent(renderer);

        SDL_Delay(1);
    }

    SDL_StopTextInput();
    SECURE_NETWORK_stop_server();
    SERVER_IDENTITY_stop();

    if (Global_Decode_Mode) {

        DECODE_exit_mode();

    }

    if (Global_CaseManagement_Mode) {

        CASE_MANAGEMENT_exit_mode();

    }

    dashboard_shutdown();

    if (RETROSPECTRUM_transmission_is_active()) {

        retrospectrum_finalize_transmission(1, 0);

    }

    stop_recording();

    if (dev) {

        if (Global_Radio_Running) {

            hackrf_stop_rx(dev);
            Global_Radio_Running = 0;

        }

        hackrf_close(dev);
        dev = NULL;
        Global_HackRF_Device = NULL;

    }

    if (Global_HackRF_Library_Initialized) {

        hackrf_exit();
        Global_HackRF_Library_Initialized = 0;

    }

    if (font_small) {

        TTF_CloseFont(font_small);

    }

    if (font_medium) {

        TTF_CloseFont(font_medium);

    }

    SDL_DestroyTexture(waterfall_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window_sdl);

    IMG_Quit();
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
    rec_queue_free(&Global_Rec_Queue);
    pthread_mutex_destroy(&ring_buf.lock);

    return 0;
}
