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
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <dirent.h>

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

// Responsible for the ClassificationWorkstation
#include "ClassificationWorkstation.h"

// Responsible for the AnalysisWorkstation
#include "AnalysisWorkstation.h"

// GUI functions implemented in GUIs.c and called from this main logic file

void add_fft_line_to_waterfall(uint32_t *pixels, int tex_w, int tex_h, double *db);

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

#define REL_MIN_DB                      2.0
#define REL_MAX_DB                      22.0

#define CONTROL_PANEL_HEIGHT            95
#define AXIS_HEIGHT                     70
#define MARGIN                          20

#define PRE_RECORD_SECONDS              5
#define REC_QUEUE_SECONDS               (PRE_RECORD_SECONDS * 2)
#define REC_PUSH_CHUNK_SAMPLES          4096

#define REC_FIR_TAPS                    255

#ifndef M_PI
#define M_PI                            3.14159265358979323846
#endif

#define MAX_TRANSFER_SAMPLES            262144

#define DEFAULT_RECORD_DIR              "Recordings"

#define ANALYSIS_MAX_FILES               512
#define ANALYSIS_FFT_SIZE                2048
#define ANALYSIS_LIST_WIDTH              430
/* ANALYSIS_MAX_RENDER_W is defined in include/GUIs.h so the extern arrays match */
#define ANALYSIS_MAX_CONST_POINTS        4096


static volatile sig_atomic_t Global_Running = 1;

static pthread_mutex_t Global_Rec_Lock = PTHREAD_MUTEX_INITIALIZER;

/*

TYPE            VARIABLE                VALUE

*/

uint64_t        Global_Rec_Center_Hz    = 0;
uint64_t        Global_Center_Freq_Hz   = DEFAULT_CENTER_FREQ_HZ;
uint32_t        Global_Sample_Rate_Hz   = DEFAULT_SAMPLE_RATE_HZ;
uint32_t        Global_Display_Span_Hz  = DEFAULT_DISPLAY_SPAN_HZ;
int             Global_Amp_Enable       = DEFAULT_AMP_ENABLE;
int             Global_DC_Enable        = DEFAULT_DC_CORRECTION_ENABLE;
int             Global_Fullscreen       = 0;
int             Global_Rec              = 0;
char            Global_Status_Msg[256]  = "";

SDL_Color       Global_Status_Color     = {0, 255, 80, 255};

Type_Selector   Global_Selector         = {.X0 = 0.40,
                                           .X1 = 0.60,
                                           .enabled = 0,
                                           .dragging = 0,
                                           .resizing_left = 0,
                                           .resizing_right = 0
                                          };

/*

        TYPE            VARIABLE                VALUE

*/

static  FILE*           Global_Rec_File         = NULL;
static  uint32_t        Global_Rec_BW_Hz        = 0;
static  uint32_t        Global_Rec_Out_Rate_Hz  = 0;
static  int16_t         *Global_Rec_Pre_I       = NULL;
static  int16_t         *Global_Rec_Pre_Q       = NULL;
double*                 Global_Color_Baseline   = NULL;       
static  double          Global_DC_I             = 0.0;
static  double          Global_DC_Q             = 0.0;
static  double          Global_Rec_Phase        = 0.0;
static  double          Global_Rec_Acc_I        = 0.0;
static  double          Global_Rec_Acc_Q        = 0.0;
static  size_t          Global_Rec_Pre_Count    = 0;
static  int             Global_Rec_FIR_Pos      = 0;
static  int             Global_LNA_Gain         = DEFAULT_LNA_GAIN;
static  int             Global_VGA_Gain         = DEFAULT_VGA_GAIN;
static  int             Global_Waterfall_FPS    = DEFAULT_WATERFALL_FPS;
static  int             Global_Rows_Per_Frame   = DEFAULT_ROWS_PER_FRAME;
static  int             Global_Rec_Acc_Count    = 0;
static  int             Global_Rec_Decimation   = 1;
static  int             Global_Radio_Running    = 0;
static  int             Global_Cached_Recording = 0;
static  char            Global_Record_Dir[512]  = DEFAULT_RECORD_DIR;



static  double          Global_Rec_FIR[REC_FIR_TAPS];
static  double          Global_Rec_Hist_I[REC_FIR_TAPS];
static  double          Global_Rec_Hist_Q[REC_FIR_TAPS];
static  float           temp_I[MAX_TRANSFER_SAMPLES];
static  float           temp_Q[MAX_TRANSFER_SAMPLES];

static  Type_RingBuf    ring_buf;
static  Type_Rec_Cache  Global_Pre_Cache;
static  Type_Rec_Queue  Global_Rec_Queue;

static  pthread_t       Global_Rec_Thread;
static  int             Global_Rec_Thread_Running = 0;

// =========
// Functions
// =========

// OS Signal Handling

static void handle_sigint(int sig){

    /*

    Purpose: Handles SIGINT shutdown requests

    Return: No return

    */

    (void)sig;
    Global_Running = 0;

}

// Hard Bounds

double limit_double(double value, double low, double high){

    /*

    Purpose: Clamps a double value between lower and upper bounds

    Return: Clamped value

    */

    if (value < low) return low;

    if (value > high) return high;

    return value;

}

// Target Path Validation and Creation

static int ensure_record_dir_exists(void){

    /*

    Purpose: Ensures the recording directory exists and is usable

    Return: Directory status

    */

    struct stat st;

    if (stat(Global_Record_Dir, &st) == 0) {

        if (S_ISDIR(st.st_mode)) return 1;

        return 0;

    }

    if (mkdir(Global_Record_Dir, 0755) == 0) {

        return 1;

    }

    return 0;

}

// Selector Helpers

uint64_t selection_center_Hz(void){

    /*

    Purpose: Computes the selected recording center frequency in Hz

    Return: Center frequency

    */

    double Center_Frac = (Global_Selector.X0 + Global_Selector.X1) * 0.5;

    double Offset_Hz = (Center_Frac - 0.5) * (double)Global_Display_Span_Hz;

    double Calc_Freq = (double)Global_Center_Freq_Hz + Offset_Hz;

    if (Calc_Freq < 0.0) Calc_Freq = 0.0;

    return (uint64_t)Calc_Freq;

}

uint32_t selection_BW_Hz(void){

    /*

    Purpose: Computes the selected recording bandwidth in Hz

    Return: Bandwidth value

    */

    double BW = fabs(Global_Selector.X1 - Global_Selector.X0) * (double)Global_Display_Span_Hz;

    if (BW < 1000.0) BW = 1000.0;

    if (BW > (double)Global_Sample_Rate_Hz) BW = (double)Global_Sample_Rate_Hz;

    return (uint32_t)BW;

}

// RF Filter

static void configure_recording_filter(void) {

    /*

    Purpose: Configures the FIR filter and decimation used by recording

    Return: No return

    */

    memset(Global_Rec_FIR, 0, sizeof(Global_Rec_FIR));
    memset(Global_Rec_Hist_I, 0, sizeof(Global_Rec_Hist_I));
    memset(Global_Rec_Hist_Q, 0, sizeof(Global_Rec_Hist_Q));

    Global_Rec_FIR_Pos = 0;
    Global_Rec_Acc_Count = 0;

     // Output rate should be comfortably above selected bandwidth
     // 2.5x gives room for FIR transition

    double wanted_out_rate = (double)Global_Rec_BW_Hz * 3;

    if (wanted_out_rate < 48000.0) wanted_out_rate = 48000.0;
    
    Global_Rec_Decimation = (int)((double)Global_Sample_Rate_Hz / wanted_out_rate);

    if (Global_Rec_Decimation < 1) Global_Rec_Decimation = 1;

    Global_Rec_Out_Rate_Hz = Global_Sample_Rate_Hz / (uint32_t)Global_Rec_Decimation;

    // After shifting selected center to 0 Hz, selected bandwidth is -BW/2 to +BW/2

    double cutoff_hz = (double)Global_Rec_BW_Hz * 0.5;

    // Keep cutoff below decimated Nyquist

    double max_safe_cutoff = (double)Global_Rec_Out_Rate_Hz * 0.45;

    if (cutoff_hz > max_safe_cutoff) cutoff_hz = max_safe_cutoff;

    // Normalized cutoff relative to input sample rate

    double fc = cutoff_hz / (double)Global_Sample_Rate_Hz;

    double sum = 0.0;
    int mid = REC_FIR_TAPS / 2;

    for (int n = 0; n < REC_FIR_TAPS; n++) {

        int m = n - mid;

        double sinc;

        if (m == 0) sinc = 2.0 * fc;

        else sinc = sin(2.0 * M_PI * fc * (double)m) / (M_PI * (double)m);


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

static int pre_cache_init(Type_Rec_Cache *c, uint32_t sample_rate_hz){

    /*

    Purpose: Initializes the pre-record IQ cache

    Return: Init status

    */

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

    /*

    Purpose: Frees the pre-record IQ cache

    Return: No return

    */
    
    pthread_mutex_destroy(&c->lock);

    free(c->I);
    free(c->Q);

    memset(c, 0, sizeof(*c));

}

static int pre_cache_resize(Type_Rec_Cache *c, uint32_t sample_rate_hz){

    /*

    Purpose: Resizes the pre-record IQ cache for a new sample rate

    Return: Resize status

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

    /*

    Purpose: Writes one IQ sample into the pre-record cache

    Return: No return

    */

    if (!c->I || !c->Q || c->capacity == 0) return;

    if (I > 1.0f) I = 1.0f;
    if (I < -1.0f) I = -1.0f;

    if (Q > 1.0f) Q = 1.0f;
    if (Q < -1.0f) Q = -1.0f;

    c->I[c->write_pos] = (int16_t)(I * 32767.0f);
    c->Q[c->write_pos] = (int16_t)(Q * 32767.0f);

    c->write_pos = (c->write_pos + 1) % c->capacity;

    if(c->count < c->capacity) c->count++;

}

static size_t pre_cache_snapshot_locked(Type_Rec_Cache *c, int16_t **out_I, int16_t **out_Q){

    /*

    Purpose: Copies the current pre-record cache while already locked

    Return: Snapshot count

    */

    *out_I = NULL;
    *out_Q = NULL;

    size_t count = c->count;

    if (count == 0 || !c->I || !c->Q) return 0;

    int16_t *copy_I = malloc(sizeof(int16_t) * count);
    int16_t *copy_Q = malloc(sizeof(int16_t) * count);

    if (!copy_I || !copy_Q) {

        free(copy_I);
        free(copy_Q);
        return 0;

    }

    if(c->count < c->capacity){

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

static int rec_queue_init(Type_Rec_Queue *q, uint32_t sample_rate_hz){

    /*

    Purpose: Initializes the recording queue

    Return: Init status

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

static void rec_queue_free(Type_Rec_Queue *q){

    /*

    Purpose: Frees the recording queue

    Return: No return

    */

    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->data_cond);

    free(q->I);
    free(q->Q);

    memset(q, 0, sizeof(*q));

}

static int rec_queue_resize(Type_Rec_Queue *q, uint32_t sample_rate_hz){

    /*

    Purpose: Resizes the recording queue for a new sample rate

    Return: Resize status

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

    if (!result){

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

static size_t rec_queue_available_locked(Type_Rec_Queue *q){

    /*

    Purpose: Returns the number of queued samples while already locked

    Return: Available samples

    */

    if (q->write_pos >= q->read_pos){
        
        return q->write_pos - q->read_pos;

    }

    return q->capacity - q->read_pos + q->write_pos;

}

static void rec_queue_reset(Type_Rec_Queue *q){

    /*

    Purpose: Resets the recording queue state

    Return: No return

    */

    pthread_mutex_lock(&q->lock);

    q->read_pos = 0;
    q->write_pos = 0;
    q->stop_requested = 0;
    q->overflow = 0;

    pthread_mutex_unlock(&q->lock);

}

static size_t rec_queue_push_block(Type_Rec_Queue *q, const float *in_I, const float *in_Q,
                                    size_t count){

    /*

    Purpose: Pushes a block of IQ samples into the recording queue

    Return: Pushed samples

    */

    if (!q->I || !q->Q || q->capacity == 0) return 0;
    if (!in_I || !in_Q || count == 0) return 0;

    size_t total_pushed = 0;

    while (total_pushed < count){

        size_t chunk_count = count - total_pushed;

        if (chunk_count > REC_PUSH_CHUNK_SAMPLES){

            chunk_count = REC_PUSH_CHUNK_SAMPLES;

        }

        pthread_mutex_lock(&q->lock);

        if (q->stop_requested){
            
            pthread_mutex_unlock(&q->lock);
            break;

        }

        size_t pushed_chunk = 0;

        for (size_t n = 0; n < chunk_count; n++){

            size_t src_idx = total_pushed + n;
            size_t next = (q->write_pos + 1) % q->capacity;

            if (next == q->read_pos){

                q->overflow = 1;
                break;

            }

            float I = in_I[src_idx];
            float Q = in_Q[src_idx];

            if (I > 1.0f) I = 1.0f;
            if (I < -1.0f) I = -1.0f;

            if (Q > 1.0f) Q = 1.0f;
            if (Q < -1.0f) Q = -1.0f;

            q->I[q->write_pos] = (int16_t)(I * 32767.0f);
            q->Q[q->write_pos] = (int16_t)(Q * 32767.0f);

            q->write_pos = next;
            pushed_chunk++;

        }

        if (pushed_chunk > 0) pthread_cond_signal(&q->data_cond);

        pthread_mutex_unlock(&q->lock);

        total_pushed += pushed_chunk;

        // If queue becomes full before full chunk is pushed

        if (pushed_chunk < chunk_count){

            break;

        }

    }

    return total_pushed;

}

static size_t rec_queue_pop_block(Type_Rec_Queue *q, int16_t *out_I, int16_t *out_Q,
                                  size_t max_count){

    /*

    Purpose: Pops a block of IQ samples from the recording queue

    Return: Popped samples

    */

    pthread_mutex_lock(&q->lock);

    while (rec_queue_available_locked(q) == 0 && !q->stop_requested){

        pthread_cond_wait(&q->data_cond, &q->lock);

    }

    size_t available = rec_queue_available_locked(q);

    if (available == 0 && q->stop_requested){
    
        pthread_mutex_unlock(&q->lock);
        return 0;

    }

    if (available > max_count){

        available = max_count;

    }

    for (size_t n = 0; n < available; n++){

        out_I[n] = q->I[q->read_pos];
        out_Q[n] = q->Q[q->read_pos];

        q->read_pos = (q->read_pos + 1) % q->capacity;

    }

    pthread_mutex_unlock(&q->lock);

    return available;

}

static void rec_queue_request_stop(Type_Rec_Queue *q){

    /*

    Purpose: Requests the recording queue to stop blocking operations

    Return: No return

    */

    pthread_mutex_lock(&q->lock);

    q->stop_requested = 1;

    pthread_cond_broadcast(&q->data_cond);

    pthread_mutex_unlock(&q->lock);

}

// Recorder Helpers

static void recorder_reset_processing_state(void){

    /*

    Purpose: Resets recorder filter and mixer state

    Return: No return

    */

    Global_Rec_Phase = 0.0;
    Global_Rec_Acc_I = 0.0;
    Global_Rec_Acc_Q = 0.0;
    Global_Rec_Acc_Count = 0;
    Global_Rec_FIR_Pos = 0;

    memset(Global_Rec_Hist_I, 0, sizeof(Global_Rec_Hist_I));
    memset(Global_Rec_Hist_Q, 0, sizeof(Global_Rec_Hist_Q));

}

static void recorder_write_sample(float I, float Q){

    /*

    Purpose: Processes and writes one IQ sample to the active recording file

    Return: No return

    */

    if (!Global_Rec_File) return;

    // Shift selected center frequency to baseband

    double Freq_Offset_Hz = (double)Global_Rec_Center_Hz - (double)Global_Center_Freq_Hz;
    double Phase_Step = -2.0 * M_PI * Freq_Offset_Hz / (double)Global_Sample_Rate_Hz;

    double C = cos(Global_Rec_Phase);
    double S = sin(Global_Rec_Phase);

    double Shifted_I = I * C - Q * S;
    double Shifted_Q = I * S + Q * C;

    Global_Rec_Phase += Phase_Step;

    if (Global_Rec_Phase > M_PI) Global_Rec_Phase -= 2.0 * M_PI;

    if (Global_Rec_Phase < -M_PI) Global_Rec_Phase += 2.0 * M_PI;

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

    // Always store the newest shifted sample

    Global_Rec_Hist_I[Global_Rec_FIR_Pos] = Shifted_I;
    Global_Rec_Hist_Q[Global_Rec_FIR_Pos] = Shifted_Q;

    int newest_pos = Global_Rec_FIR_Pos;

    Global_Rec_FIR_Pos++;

    if (Global_Rec_FIR_Pos >= REC_FIR_TAPS) Global_Rec_FIR_Pos = 0;

    // Decimation gate
    // Do not run the full FIR convolution unless this input sample will produce one output sample

    Global_Rec_Acc_Count++;

    if (Global_Rec_Acc_Count < Global_Rec_Decimation) return;

    Global_Rec_Acc_Count = 0;

    // FIR convolution only on output samples

    double Filtered_I = 0.0;
    double Filtered_Q = 0.0;

    int hist_idx = newest_pos;

    for (int tap = 0; tap < REC_FIR_TAPS; tap++) {

        Filtered_I += Global_Rec_FIR[tap] * Global_Rec_Hist_I[hist_idx];
        Filtered_Q += Global_Rec_FIR[tap] * Global_Rec_Hist_Q[hist_idx];

        hist_idx--;

        if (hist_idx < 0) hist_idx = REC_FIR_TAPS - 1;

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


static void *recorder_thread_main(void *arg){

    /*

    Purpose: Drains queued IQ samples and writes the active recording file

    Return: Thread result

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

    while(1) {

        size_t popped = rec_queue_pop_block(&Global_Rec_Queue,
                                         buf_I,
                                         buf_Q,
                                         REC_PUSH_CHUNK_SAMPLES);

        if (popped == 0) break;

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


static void stop_recording(void){

    /*

    Purpose: Stops recording and drains the recording queue

    Return: No return

    */

    int thread_exists = 0;

    pthread_mutex_lock(&Global_Rec_Lock);

    if (!Global_Rec && !Global_Rec_Thread_Running){

        pthread_mutex_unlock(&Global_Rec_Lock);
        set_status("", (SDL_Color){0, 255, 80, 255});
        return;

    }

    Global_Rec = 0;
    thread_exists = Global_Rec_Thread_Running;

    pthread_mutex_unlock(&Global_Rec_Lock);

    // Finish after draining queued samples
    
    rec_queue_request_stop(&Global_Rec_Queue);

    if (thread_exists){

        pthread_join(Global_Rec_Thread, NULL);
        Global_Rec_Thread_Running = 0;

    }

    free(Global_Rec_Pre_I);
    free(Global_Rec_Pre_Q);

    Global_Rec_Pre_I = NULL;
    Global_Rec_Pre_Q = NULL;
    Global_Rec_Pre_Count = 0;

    recorder_reset_processing_state();

    if(Global_Rec_Queue.overflow){

        set_status("Recording stopped - queue overflow occurred", (SDL_Color){255, 180, 40, 255});

    }

    else {

        set_status("", (SDL_Color){0, 255, 80, 255});

    }

}

static int start_recording(void){

    /*

    Purpose: Runs the background recording writer thread

    Return: Start status

    */

    if (Global_Rec) return 1;

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

    snprintf(filename,
             sizeof(filename),
             "%s/%s_CAPTURE_%.6fMHz_BW_%.3fkHz_SR_%.3fk_Decimation_%d.complex16",
             Global_Record_Dir,
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

    free(Global_Rec_Pre_I);
    free(Global_Rec_Pre_Q);

    Global_Rec_Pre_I = NULL;
    Global_Rec_Pre_Q = NULL;
    Global_Rec_Pre_Count = 0;

    // CRITICAL FOR ENSURING MINIMAL GAP BETWEEN CACHE AND LIVE DATA

    if (Global_Cached_Recording) {

        pthread_mutex_lock(&Global_Pre_Cache.lock);

        Global_Rec_Pre_Count = pre_cache_snapshot_locked(&Global_Pre_Cache,
                                                         &Global_Rec_Pre_I,
                                                         &Global_Rec_Pre_Q);

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

    if (pthread_create(&Global_Rec_Thread, NULL, recorder_thread_main, NULL) != 0){

        pthread_mutex_lock(&Global_Rec_Lock);
        Global_Rec = 0;
        pthread_mutex_unlock(&Global_Rec_Lock);

        rec_queue_request_stop(&Global_Rec_Queue);

        if (Global_Rec_File){

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

    snprintf(msg,
             sizeof(msg),
             "RECORDING %.6f MHz - BW %.3f kHz%s",
             Global_Rec_Center_Hz / 1e6,
             Global_Rec_BW_Hz / 1e3,
             Global_Cached_Recording ? " - CACHE 5s" : "");

    set_status(msg, (SDL_Color){255, 60, 40, 255});

    return 1;

}

// Ring Buffer Helpers

static size_t ring_available_locked(Type_RingBuf* r){

    /*

    Purpose: Returns the number of samples available in the waterfall ring buffer while
             already locked

    Return: Available samples

    */
    
    if (r->write_pos >= r->read_pos) return r->write_pos - r->read_pos;

    return RING_SIZE - r->read_pos + r->write_pos;

}

static void ring_clear(Type_RingBuf *r){

    /*

    Purpose: Clears the waterfall ring buffer

    Return: No return

    */

    pthread_mutex_lock(&r->lock);

    r->write_pos = 0;
    r->read_pos = 0;

    pthread_mutex_unlock(&r->lock);
}

static void ring_write_sample(Type_RingBuf *r, float I, float Q){

    /*

    Purpose: Writes one IQ sample into the waterfall ring buffer

    Return: No return

    */
    
    r->I[r->write_pos] = I;
    r->Q[r->write_pos] = Q;

    r->write_pos = (r->write_pos + 1) % RING_SIZE;

    if (r->write_pos == r->read_pos){

        r->read_pos = (r->read_pos+1) % RING_SIZE;

    }

}

static int ring_read_block(Type_RingBuf *r, fftw_complex *in, double *window){

    /*

    Purpose: Reads one FFT block from the waterfall ring buffer

    Return: Read status

    */

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
    // That explains "+ FFT_SIZE / 2" (Use half of the older samples)

    r->read_pos = (r->read_pos + FFT_SIZE / 2) % RING_SIZE;

    pthread_mutex_unlock(&r->lock);
    return 1;

}

// RX Helper

static int rx_callback(hackrf_transfer *transfer){

    /*

    Purpose: Receives HackRF samples and routes them to cache, display, and recording paths

    Return: Callback status

    */

    const int8_t *buf = (const int8_t *)transfer->buffer;
    int sample_count = transfer->valid_length / 2;

    if (sample_count > MAX_TRANSFER_SAMPLES) {
        sample_count = MAX_TRANSFER_SAMPLES;
    }

    // Convert signed HackRF IQ once

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

    // Waterfall ring buffer
    // Lock only around ring_buf writes

    pthread_mutex_lock(&ring_buf.lock);

    for (int n = 0; n < sample_count; n++){
        ring_write_sample(&ring_buf, temp_I[n], temp_Q[n]);
    }

    pthread_mutex_unlock(&ring_buf.lock);

    // Recorder path
    // No ring_buf.lock here

    int rec_enabled = 0;

    pthread_mutex_lock(&Global_Rec_Lock);
    rec_enabled = Global_Rec;
    pthread_mutex_unlock(&Global_Rec_Lock);

    if (rec_enabled){

        size_t pushed = rec_queue_push_block(&Global_Rec_Queue, temp_I, temp_Q, (size_t)sample_count);

        if (pushed < (size_t)sample_count){
        
            Global_Rec_Queue.overflow = 1;

        }

    }

    return 0;
}

// Graphics Helper

static void compute_DB_from_FFT(fftw_complex *out, double *db){

    /*

    Purpose: Converts FFT output bins into shifted decibel values

    Return: No return

    */

    for (int sam = 0; sam < FFT_SIZE; sam++){

        int shifted_sam = (sam + FFT_SIZE / 2) % FFT_SIZE;
        
        double I = out[shifted_sam][0];
        double Q = out[shifted_sam][1];

        double magnitude = sqrt(I*I + Q*Q) / FFT_SIZE;
        db[sam] = 20.0 * log10(magnitude + 1e-12) + 100.0;

    }

}

static int parse_positive_double(const char *s, double *out) {

    /*

    Purpose: Parses a positive double value from text

    Return: Parse status

    */
    if (!s || !*s) return 0;

    char *end = NULL;
    double v = strtod(s, &end);

if (end == s || *end != '\0' || v <= 0.0) return 0;

    *out = v;
    return 1;
}

static int parse_nonnegative_int(const char *s, int *out) {

    /*

    Purpose: Parses a nonnegative integer value from text

    Return: Parse status

    */
    if (!s || !*s) return 0;

    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (end == s || *end != '\0' || v < 0 || v > 100000) return 0;

    *out = (int)v;
    return 1;
}

// Normalization Helpers

static int normalize_lna_gain(int gain) {

    /*

    Purpose: Normalizes HackRF LNA gain to a valid step

    Return: Gain value

    */
    if (gain < 0) gain = 0;
    if (gain > 40) gain = 40;
    return (gain / 8) * 8;
}

static int normalize_vga_gain(int gain) {

    /*

    Purpose: Normalizes HackRF VGA gain to a valid step

    Return: Gain value

    */
    if (gain < 0) gain = 0;
    if (gain > 62) gain = 62;
    return (gain / 2) * 2;
}

static int normalize_fps(int fps) {

    /*

    Purpose: Normalizes waterfall frame rate

    Return: FPS value

    */
    if (fps < 1) fps = 1;
    if (fps > 1000) fps = 1000;
    return fps;
}

static int normalize_rows_per_frame(int rows) {

    /*

    Purpose: Normalizes waterfall rows rendered per frame

    Return: Row count

    */
    if (rows < 1) rows = 1;
    if (rows > 64) rows = 64;
    return rows;
}

// Radio Helpers

static int stop_radio(hackrf_device *dev) {

    /*

    Purpose: Stops HackRF receive mode when active

    Return: Stop status

    */
    if (Global_Radio_Running) {
        if (hackrf_stop_rx(dev) != HACKRF_SUCCESS) return 0;
        Global_Radio_Running = 0;
    }

    return 1;
}

static int start_radio(hackrf_device *dev) {

    /*

    Purpose: Starts HackRF receive mode when inactive

    Return: Start status

    */
    if (!Global_Radio_Running) {
        if (hackrf_start_rx(dev, rx_callback, NULL) != HACKRF_SUCCESS) return 0;
        Global_Radio_Running = 1;
    }

    return 1;
}

double recommended_antenna_length_inches(uint64_t freq_hz) {

    /*

    Purpose: Computes quarter-wave antenna length in inches

    Return: Antenna length

    */

    if (freq_hz == 0) return 0.0;

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

static int apply_radio_settings(hackrf_device *dev, uint64_t Center_Hz, 
                                uint32_t Sample_Rate_Hz, uint32_t Display_Span_Hz, 
                                int LNA_Gain, int VGA_Gain, int Amp_Enable){

    /*

    Purpose: Applies center frequency, sample rate, display span, gain, and amp settings

    Return: Apply status

    */

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

    if (!rec_queue_resize(&Global_Rec_Queue, Global_Sample_Rate_Hz)) {

        set_status("Record queue resize failed", (SDL_Color){255, 60, 40, 255});
        return 0;

    }

    return start_radio(dev);

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

    /*

    Purpose: Parses GUI input boxes and applies radio settings

    Return: Apply status

    */
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


// =====================
// Command Line Handling
// =====================

static int parse_command_line_args(int argc, char **argv){

    /*

    Purpose: Parses supported command-line arguments

    Return: Parse status

    */

    int output_dir_provided = 0;

    if (argc <= 1) {

        fprintf(stderr, "Usage: %s -o record_dir [-C 0|1]\n", argv[0]);
        fprintf(stderr, "  -o record_dir   Required directory used to save and scan recordings.\n");
        fprintf(stderr, "  -C 0|1          Optional cached recording toggle: 0 disables cache, 1 enables 5-second cache.\n");
        return 0;

    }

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "-C") == 0) {

            if (i + 1 >= argc) {

                fprintf(stderr, "Missing value for -C. Use -C 0 or -C 1.\n");
                fprintf(stderr, "Usage: %s -o record_dir [-C 0|1]\n", argv[0]);
                return 0;

            }

            Global_Cached_Recording = atoi(argv[i + 1]) ? 1 : 0;
            i++;

        }

        else if (strcmp(argv[i], "-o") == 0) {

            if (i + 1 >= argc) {

                fprintf(stderr, "Missing value for -o record directory.\n");
                fprintf(stderr, "Usage: %s -o record_dir [-C 0|1]\n", argv[0]);
                return 0;

            }

            snprintf(Global_Record_Dir, sizeof(Global_Record_Dir), "%s", argv[i + 1]);
            output_dir_provided = 1;
            i++;

        }

        else {

            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s -o record_dir [-C 0|1]\n", argv[0]);
            fprintf(stderr, "  -o record_dir   Required directory used to save and scan recordings.\n");
            fprintf(stderr, "  -C 0|1          Optional cached recording toggle: 0 disables cache, 1 enables 5-second cache.\n");
            return 0;

        }

    }

    if (!output_dir_provided) {

        fprintf(stderr, "Missing required -o record directory.\n");
        fprintf(stderr, "Usage: %s -o record_dir [-C 0|1]\n", argv[0]);
        fprintf(stderr, "  -o record_dir   Required directory used to save and scan recordings.\n");
        fprintf(stderr, "  -C 0|1          Optional cached recording toggle: 0 disables cache, 1 enables 5-second cache.\n");
        return 0;

    }

    return 1;

}

// ==========
// Main Logic
// ==========

int main(int argc, char **argv){

    /*

    Purpose: Runs the RetroSpectrum application event loop

    Return: Exit status

    */

    signal(SIGINT, handle_sigint);

    if (!parse_command_line_args(argc, argv)) {

        return 1;

    }

    memset(&ring_buf, 0, sizeof(ring_buf));

    pthread_mutex_init(&ring_buf.lock, NULL);

    if (!pre_cache_init(&Global_Pre_Cache, Global_Sample_Rate_Hz)) {

        fprintf(stderr, "pre-cache allocation failed\n");
        return 1;

    }

    if (!rec_queue_init(&Global_Rec_Queue, Global_Sample_Rate_Hz)){
        fprintf(stderr, "record queue allocation failed\n");
        pre_cache_free(&Global_Pre_Cache);
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

            if (Global_Classification_Mode) {

                CLASSIFICATION_handle_event(&event, win_w, win_h);
                continue;

            }

            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;

                if (key == SDLK_g && active == FIELD_NONE) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();

                    }

                    if (Global_Analysis_Mode) {

                        ANALYSIS_exit_mode(pixels, tex_w, tex_h, waterfall_texture);
                        next_waterfall_ms = SDL_GetTicks64();

                    }

                    else {

                        ANALYSIS_enter_mode(Global_Record_Dir,
                                            Global_Center_Freq_Hz,
                                            Global_Rec_Out_Rate_Hz,
                                            Global_Sample_Rate_Hz);

                    }

                    continue;

                }

                if (key == SDLK_h && active == FIELD_NONE) {

                    if (Global_Classification_Mode) {

                        CLASSIFICATION_exit_mode();
                        set_status("", (SDL_Color){0, 255, 80, 255});

                    }

                    else {

                        if (Global_Analysis_Mode) {

                            ANALYSIS_exit_mode(pixels,
                                               tex_w,
                                               tex_h,
                                               waterfall_texture);
                            next_waterfall_ms = SDL_GetTicks64();

                        }

                        CLASSIFICATION_enter_mode(Global_Record_Dir);

                        set_status("Classification Workstation",
                                   (SDL_Color){0, 255, 80, 255});

                    }

                    continue;

                }

                if (Global_Analysis_Mode) {

                    int analysis_event_result = ANALYSIS_handle_event(&event,
                                                                      win_w,
                                                                      win_h,
                                                                      pixels,
                                                                      tex_w,
                                                                      tex_h,
                                                                      waterfall_texture,
                                                                      &next_waterfall_ms,
                                                                      &active);

                    if (analysis_event_result == ANALYSIS_EVENT_QUIT) {

                        Global_Running = 0;

                    }

                    if (analysis_event_result != ANALYSIS_EVENT_IGNORED) {

                        continue;

                    }

                }

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

            if (Global_Analysis_Mode && event.type != SDL_KEYDOWN) {

                int analysis_event_result = ANALYSIS_handle_event(&event,
                                                                  win_w,
                                                                  win_h,
                                                                  pixels,
                                                                  tex_w,
                                                                  tex_h,
                                                                  waterfall_texture,
                                                                  &next_waterfall_ms,
                                                                  &active);

                if (analysis_event_result == ANALYSIS_EVENT_QUIT) {

                    Global_Running = 0;

                }

                if (analysis_event_result != ANALYSIS_EVENT_IGNORED) {

                    continue;

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

                if (event.button.clicks == 3 && y < CONTROL_PANEL_HEIGHT) {
              
                    active = FIELD_NONE;
                    toggle_fullscreen(window_sdl);
                    continue;
                }

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

        if (!Global_Analysis_Mode && !Global_Classification_Mode && now_ms >= next_waterfall_ms) {

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

        if (!Global_Analysis_Mode && !Global_Classification_Mode) {

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

        }

        if (Global_Analysis_Mode) {

            ANALYSIS_draw_workstation(renderer,
                                      font_small,
                                      waterfall_texture,
                                      pixels,
                                      tex_w,
                                      tex_h,
                                      win_w,
                                      win_h);

            ANALYSIS_draw_workstation_overlays(renderer,
                                               font_small,
                                               waterfall_texture,
                                               tex_w,
                                               tex_h,
                                               win_w,
                                               win_h);

        }

        else if (Global_Classification_Mode) {

            CLASSIFICATION_draw_workstation(renderer,
                                            font_small,
                                            win_w,
                                            win_h);

        }

        else {

            SDL_RenderCopy(renderer, waterfall_texture, NULL, &waterfall_rect);
            draw_selection_overlay(renderer, waterfall_rect);
            draw_selector_bandwidth(renderer, font_small, waterfall_rect);
            draw_border(renderer, waterfall_rect);
            draw_frequency_axis(renderer, font_small, waterfall_rect);

        }

        if (!Global_Analysis_Mode && !Global_Classification_Mode) {

            const char *analysis_hint_text = "Press G for Analysis | H for Classification";
            int hint_text_w = 0;
            int hint_text_h = 0;

            if (font_small) {

                TTF_SizeText(font_small, analysis_hint_text, &hint_text_w, &hint_text_h);

            }

            int hint_w = hint_text_w + 18;

            if (hint_w < rec_button.w) hint_w = rec_button.w;
            if (hint_w > win_w - 2 * MARGIN) hint_w = win_w - 2 * MARGIN;

            SDL_Rect analysis_hint = {
                rec_button.x + rec_button.w - hint_w,
                rec_button.y - 34,
                hint_w,
                28
            };

            if (analysis_hint.x + analysis_hint.w > win_w - MARGIN) {

                analysis_hint.x = win_w - MARGIN - analysis_hint.w;

            }

            if (analysis_hint.x < MARGIN) analysis_hint.x = MARGIN;
            if (analysis_hint.y < 6) analysis_hint.y = 6;

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            draw_filled_rect(renderer, analysis_hint, (SDL_Color){0, 0, 0, 210});
            draw_outline_rect(renderer, analysis_hint, (SDL_Color){0, 220, 80, 210});
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            draw_text(renderer, font_small, analysis_hint_text, analysis_hint.x + 9, analysis_hint.y + 7, (SDL_Color){0, 255, 90, 255});
            draw_text(renderer, font_medium, Global_Status_Msg, win_w / 2 - 192, win_h - 36, Global_Status_Color);

        }

        if (!Global_Analysis_Mode && !Global_Classification_Mode) {

            draw_antenna_recommendation(renderer, font_small, win_w, win_h);

            draw_made_in_usa(renderer, font_medium, win_w, win_h);

        }

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
    rec_queue_free(&Global_Rec_Queue);
    pthread_mutex_destroy(&ring_buf.lock);

    return 0;

}

