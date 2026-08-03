#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * File:            CorrelationWorkstation.c
 * Author:          Hassan Fares
 *
 * Confidential:    No
 *
 * Description:     Correlation workstation for cached transmission detection,
 *                  fixed-length RF signature extraction, and similarity ranking
 *                  across complex16 recordings.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include "CorrelationWorkstation.h"
#include "GUIs.h"
#include "SecureFunctions.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fftw3.h>

/* Kept here so this source also builds when older headers omit these APIs. */
int DATASTORE_save_content(const char *document_kind, const char *document_name, const char *case_number,
                           const void *content, size_t content_size, char *error, size_t error_size);
int DATASTORE_load_content(const char *document_kind, const char *document_name, unsigned char **content,
                           size_t *content_size, int *found, char *error, size_t error_size);
void DATASTORE_free_content(unsigned char *content, size_t content_size);
int DATASTORE_delete_content(const char *document_kind, const char *document_name, int *deleted, char *error,
                             size_t error_size);
int ANALYSIS_get_recording_workspace(const char *file_name);
int ANALYSIS_get_available_workspace_count(void);
int ANALYSIS_export_recording_to_workspace(const char *record_dir, const char *file_name, uint64_t fallback_center_hz,
                                           uint32_t fallback_rec_out_rate_hz, uint32_t fallback_sample_rate_hz,
                                           int *workspace_number, char *error, size_t error_size);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#endif

#define CORRELATION_MAX_FILES 512
#define CORRELATION_MAX_PATH 1024
#define CORRELATION_MAX_NAME 512
#define CORRELATION_MAX_SIGNATURES_PER_FILE 32
#define CORRELATION_MAX_RESULTS CORRELATION_MAX_FILES
#define CORRELATION_CACHE_VERSION 5U
#define CORRELATION_CACHE_NAME ".retrospectrum_correlation_cache_v5.bin"
#define CORRELATION_LEGACY_CACHE_NAME ".retrospectrum_correlation_cache_v4.bin"
#define CORRELATION_OLDER_CACHE_NAME ".retrospectrum_correlation_cache_v3.bin"
#define CORRELATION_OLDEST_CACHE_NAME ".retrospectrum_correlation_cache_v2.bin"
#define CORRELATION_CACHE_MAGIC "RSCORR5"
#define CORRELATION_DATASTORE_KIND "correlation"
#define CORRELATION_DATASTORE_DOCUMENT "__correlation_signature_cache_v5"
#define CORRELATION_LEGACY_DATASTORE_DOCUMENT "__correlation_signature_cache_v4"
#define CORRELATION_OLDER_DATASTORE_DOCUMENT "__correlation_signature_cache_v3"
#define CORRELATION_CACHE_CHECKPOINT_FILES 5
#define CORRELATION_DETECTION_WINDOW_SEC 0.002
#define CORRELATION_BASELINE_TIME_CONSTANT_SEC 1.0
#define CORRELATION_START_DB 8.0
#define CORRELATION_END_DB 4.0
#define CORRELATION_END_HOLD_SEC 0.050
#define CORRELATION_PRETRIGGER_SEC 0.050
#define CORRELATION_MIN_BURST_SEC 0.004
#define CORRELATION_OBW_FFT_SIZE 4096
#define CORRELATION_OBW_MAX_BLOCKS 32
#define CORRELATION_LIST_ROW_H 36
#define CORRELATION_RESULT_ROW_H 82
#define CORRELATION_PANEL_MARGIN 18
#define CORRELATION_FILE_SEARCH_TEXT_MAX 128
#define CORRELATION_FILE_SEARCH_ROW_H 38
#define CORRELATION_WEIGHT_TEXT_MAX 24
#define CORRELATION_MIN_TREND_POINTS 32
#define CORRELATION_MAX_TREND_POINTS CORRELATION_TREND_POINTS
#define CORRELATION_DEFAULT_TREND_POINTS 256
#define CORRELATION_MAX_LAG_PERCENT 0.125
#define CORRELATION_HIDDEN_YIELD_MS 4U
#define CORRELATION_HIDDEN_DETECTION_BATCH 8U
#define CORRELATION_HIDDEN_SIGNATURE_BATCH 2U
#define CORRELATION_HIDDEN_NCC_LAG_BATCH 8U
#define CORRELATION_HIDDEN_FILE_DELAY_MS 12U

/* Default score scalars; users can edit them in the Signature Engine bar. */
#define CORRELATION_DEFAULT_MAGNITUDE_WEIGHT 0.10
#define CORRELATION_DEFAULT_FREQUENCY_WEIGHT 0.80
#define CORRELATION_DEFAULT_BANDWIDTH_WEIGHT 0.10

typedef struct Type_Correlation_Detected_Range {
    size_t start_sample;
    size_t end_sample;
} Type_Correlation_Detected_Range;

typedef struct Type_Correlation_Cache_Entry {
    char file_name[CORRELATION_MAX_NAME];
    uint64_t file_size;
    int64_t modified_time;
    uint32_t signature_count;
    TransmissionSignature signatures[CORRELATION_MAX_SIGNATURES_PER_FILE];
} Type_Correlation_Cache_Entry;

typedef struct Type_Correlation_Cache_Header {
    char magic[8];
    uint32_t version;
    uint32_t entry_count;
    uint32_t trend_points;
} Type_Correlation_Cache_Header;

typedef struct Type_Correlation_Cache_Entry_Disk {
    char file_name[CORRELATION_MAX_NAME];
    uint64_t file_size;
    int64_t modified_time;
    uint32_t signature_count;
} Type_Correlation_Cache_Entry_Disk;

typedef struct Type_Correlation_Signature_Disk {
    double start_time;
    double duration;
    double center_frequency;
    double occupied_bandwidth;
    double peak_power;
    double average_power;
} Type_Correlation_Signature_Disk;

typedef struct Type_Correlation_Result {
    char file_name[CORRELATION_MAX_NAME];
    int signature_index;
    double score;
    double magnitude_score;
    double frequency_score;
    double bandwidth_score;
    TransmissionSignature signature;
} Type_Correlation_Result;

typedef struct Type_Correlation_Work_Item {
    char file_name[CORRELATION_MAX_NAME];
    char path[CORRELATION_MAX_PATH];
    uint64_t file_size;
    int64_t modified_time;
    double sample_rate;
    double center_frequency;
} Type_Correlation_Work_Item;

int Global_Correlation_Mode = 0;

static char Global_Correlation_Record_Dir[CORRELATION_MAX_PATH] = "Recordings";
static char Global_Correlation_Files[CORRELATION_MAX_FILES][CORRELATION_MAX_NAME];
static int Global_Correlation_File_Count = 0;
static int Global_Correlation_Selected_File = -1;
static int Global_Correlation_File_Scroll = 0;
static int Global_Correlation_Result_Scroll = 0;
static int Global_Correlation_Selected_Result = -1;

static double Global_Correlation_Magnitude_Weight = CORRELATION_DEFAULT_MAGNITUDE_WEIGHT;
static double Global_Correlation_Frequency_Weight = CORRELATION_DEFAULT_FREQUENCY_WEIGHT;
static double Global_Correlation_Bandwidth_Weight = CORRELATION_DEFAULT_BANDWIDTH_WEIGHT;
static char Global_Correlation_Magnitude_Weight_Text[CORRELATION_WEIGHT_TEXT_MAX] = "0.10";
static char Global_Correlation_Frequency_Weight_Text[CORRELATION_WEIGHT_TEXT_MAX] = "0.80";
static char Global_Correlation_Bandwidth_Weight_Text[CORRELATION_WEIGHT_TEXT_MAX] = "0.10";
static int Global_Correlation_Trend_Points = CORRELATION_DEFAULT_TREND_POINTS;
static char Global_Correlation_Trend_Points_Text[CORRELATION_WEIGHT_TEXT_MAX] = "256";
static int Global_Correlation_Active_Weight_Field = 0;
static int Global_Correlation_Weight_Cursor = 0;
static int Global_Correlation_Weight_Replace_On_Type = 0;
static int Global_Correlation_Clear_Confirm = 0;

static int Global_Correlation_File_Search_Open = 0;
static int Global_Correlation_File_Search_Active = 0;
static int Global_Correlation_File_Search_Cursor = 0;
static int Global_Correlation_File_Search_Scroll = 0;
static int Global_Correlation_File_Search_Hover = -1;
static char Global_Correlation_File_Search_Text[CORRELATION_FILE_SEARCH_TEXT_MAX] = "";

static uint64_t Global_Correlation_Fallback_Center_Hz = 0;
static uint32_t Global_Correlation_Fallback_Record_Rate_Hz = 0;
static uint32_t Global_Correlation_Fallback_Sample_Rate_Hz = 0;

static Type_Correlation_Cache_Entry Global_Correlation_Cache[CORRELATION_MAX_FILES];
static int Global_Correlation_Cache_Count = 0;
static Type_Correlation_Result Global_Correlation_Results[CORRELATION_MAX_RESULTS];
static int Global_Correlation_Result_Count = 0;
static TransmissionSignature Global_Correlation_Query_Signature;
static int Global_Correlation_Query_Signature_Valid = 0;

static volatile int Global_Correlation_Working = 0;
static volatile int Global_Correlation_Cancel = 0;
static volatile int Global_Correlation_Progress = 0;
static volatile int Global_Correlation_Progress_Total = 0;
static volatile int Global_Correlation_Background_Throttle = 0;
static int Global_Correlation_Worker_Low_Priority = 0;
static int Global_Correlation_Initialized = 0;
static int Global_Correlation_Server_Cache_Attempted = 0;
static SDL_Thread *Global_Correlation_Thread = NULL;
static char Global_Correlation_Status[512] = "Select a recording, then compare it against the cached signal database.";
static char Global_Correlation_Cache_Sync_Error[256] = "";

static SDL_Color Correlation_BG = {0, 0, 0, 255};
static SDL_Color Correlation_Panel = {0, 10, 4, 245};
static SDL_Color Correlation_Panel_2 = {0, 18, 8, 255};
static SDL_Color Correlation_Border = {0, 150, 60, 255};
static SDL_Color Correlation_Border_Hi = {0, 255, 90, 255};
static SDL_Color Correlation_Text = {0, 255, 90, 255};
static SDL_Color Correlation_Muted = {0, 155, 65, 255};
static SDL_Color Correlation_Warn = {255, 180, 40, 255};
static SDL_Color Correlation_Blue = {70, 190, 255, 255};

static int correlation_point_in_rect(int x, int y, SDL_Rect rect) {
    /*
        Purpose: Checks whether a point lies inside a rectangle
        Returns: Boolean status
    */

    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

static double correlation_clamp_double(double value, double low, double high) {
    /*
        Purpose: Clamps a floating-point value
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

static void correlation_set_status(const char *text) {
    /*
        Purpose: Updates the workstation status text
        Returns: No value
    */

    if (!text) {

        return;

    }

    snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status), "%s", text);
}

static void correlation_worker_checkpoint(Uint32 hidden_delay_ms) {
    /*
        Purpose: Keeps Compare All below the SDL/UI thread at all times
        Returns: No value
    */

    if (!Global_Correlation_Worker_Low_Priority) {

        (void)SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        Global_Correlation_Worker_Low_Priority = 1;

    }

    if (hidden_delay_ms > 0U) {

        /* Hidden work uses the full delay; visible work still yields briefly. */
        SDL_Delay(Global_Correlation_Background_Throttle ? hidden_delay_ms : 1U);

    }
}

static int correlation_name_compare(const void *a, const void *b) {
    /*
        Purpose: Sorts recording names
        Returns: Comparison result
    */

    return strcasecmp((const char *)a, (const char *)b);
}

static int correlation_result_compare(const void *a, const void *b) {
    /*
        Purpose: Sorts matches from highest to lowest score
        Returns: Comparison result
    */

    const Type_Correlation_Result *left = (const Type_Correlation_Result *)a;
    const Type_Correlation_Result *right = (const Type_Correlation_Result *)b;

    if (left->score < right->score) {

        return 1;

    }

    if (left->score > right->score) {

        return -1;

    }

    return strcasecmp(left->file_name, right->file_name);
}

static int correlation_double_compare(const void *a, const void *b) {
    /*
        Purpose: Sorts floating-point values
        Returns: Comparison result
    */

    double left = *(const double *)a;
    double right = *(const double *)b;

    if (left < right) {

        return -1;

    }

    if (left > right) {

        return 1;

    }

    return 0;
}

static int correlation_has_iq_extension(const char *name) {
    /*
        Purpose: Checks whether a file has a supported IQ suffix
        Returns: Boolean status
    */

    const char *dot;

    if (!name) {

        return 0;

    }

    dot = strrchr(name, '.');

    if (!dot) {

        return 0;

    }

    return strcmp(dot, ".complex16") == 0;
}

static void correlation_parse_recording_metadata(const char *name, double *sample_rate, double *center_frequency) {
    /*
        Purpose: Parses sample rate and center frequency from a recording name
        Returns: No value
    */

    double parsed_sample_rate = Global_Correlation_Fallback_Record_Rate_Hz > 0
                                    ? (double)Global_Correlation_Fallback_Record_Rate_Hz
                                    : (double)Global_Correlation_Fallback_Sample_Rate_Hz;
    double parsed_center = (double)Global_Correlation_Fallback_Center_Hz;
    const char *capture;
    const char *rate;

    if (!name) {

        return;

    }

    capture = strstr(name, "_CAPTURE_");

    if (capture) {

        double mhz = 0.0;

        if (sscanf(capture, "_CAPTURE_%lfMHz", &mhz) == 1 && mhz > 0.0) {

            parsed_center = mhz * 1e6;

        }

    }

    rate = strstr(name, "_SR_");

    if (rate) {

        double khz = 0.0;

        if (sscanf(rate, "_SR_%lfk", &khz) == 1 && khz > 0.0) {

            parsed_sample_rate = khz * 1000.0;

        }

    }

    if (parsed_sample_rate <= 0.0) {

        parsed_sample_rate = 1.0;

    }

    if (sample_rate) {

        *sample_rate = parsed_sample_rate;

    }

    if (center_frequency) {

        *center_frequency = parsed_center;

    }
}

static int correlation_scan_recordings(void) {
    /*
        Purpose: Scans the recording directory for IQ files
        Returns: Scan status
    */

    DIR *dir;
    struct dirent *entry;
    int previous_selection = Global_Correlation_Selected_File;

    Global_Correlation_File_Count = 0;
    Global_Correlation_File_Scroll = 0;

    dir = opendir(Global_Correlation_Record_Dir);

    if (!dir) {

        correlation_set_status("Could not open the recording directory.");
        Global_Correlation_Selected_File = -1;
        return 0;

    }

    while ((entry = readdir(dir)) != NULL && Global_Correlation_File_Count < CORRELATION_MAX_FILES) {
        char path[CORRELATION_MAX_PATH];
        struct stat st;

        if (entry->d_name[0] == '.' || !correlation_has_iq_extension(entry->d_name)) {

            continue;

        }

        if (snprintf(path, sizeof(path), "%s/%s", Global_Correlation_Record_Dir, entry->d_name) < 0) {

            continue;

        }

        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 4) {

            continue;

        }

        snprintf(Global_Correlation_Files[Global_Correlation_File_Count],
                 sizeof(Global_Correlation_Files[Global_Correlation_File_Count]), "%s", entry->d_name);
        Global_Correlation_File_Count++;
    }

    closedir(dir);

    qsort(Global_Correlation_Files, (size_t)Global_Correlation_File_Count, sizeof(Global_Correlation_Files[0]),
          correlation_name_compare);

    if (Global_Correlation_File_Count <= 0) {

        Global_Correlation_Selected_File = -1;
        correlation_set_status("No supported complex16 recordings were found.");
        return 0;

    }

    if (previous_selection >= 0 && previous_selection < Global_Correlation_File_Count) {

        Global_Correlation_Selected_File = previous_selection;

    }

    else {

        Global_Correlation_Selected_File = 0;

    }

    snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
             "Found %d recording(s). Select a query signal and press Compare All.", Global_Correlation_File_Count);
    return 1;
}

static void correlation_cache_path(char *path, size_t path_size) {
    /*
        Purpose: Builds the persistent cache path
        Returns: No value
    */

    if (!path || path_size == 0) {

        return;

    }

    snprintf(path, path_size, "%s/%s", Global_Correlation_Record_Dir, CORRELATION_CACHE_NAME);
}

static int correlation_parse_cache_buffer(const unsigned char *buffer, size_t buffer_size) {
    /*
        Purpose: Validates and loads the compact correlation cache
        Returns: Load status
    */

    Type_Correlation_Cache_Header header;
    const unsigned char *cursor;
    const unsigned char *end;
    size_t trend_bytes;

    if (!buffer || buffer_size < sizeof(header)) {

        return 0;

    }

    memcpy(&header, buffer, sizeof(header));

    if (memcmp(header.magic, CORRELATION_CACHE_MAGIC, strlen(CORRELATION_CACHE_MAGIC)) != 0 ||
        header.version != CORRELATION_CACHE_VERSION || header.entry_count > CORRELATION_MAX_FILES ||
        header.trend_points != (uint32_t)Global_Correlation_Trend_Points) {

        return 0;

    }

    if (header.trend_points == 0 || header.trend_points > SIZE_MAX / (3U * sizeof(float))) {

        return 0;

    }

    trend_bytes = (size_t)header.trend_points * sizeof(float);
    cursor = buffer + sizeof(header);
    end = buffer + buffer_size;
    /* Only populated entries are touched; avoid faulting the entire maximum cache into RAM. */
    Global_Correlation_Cache_Count = 0;

    for (uint32_t entry_index = 0; entry_index < header.entry_count; entry_index++) {
        Type_Correlation_Cache_Entry_Disk disk_entry;
        Type_Correlation_Cache_Entry *entry;

        if ((size_t)(end - cursor) < sizeof(disk_entry)) {

            return 0;

        }

        memcpy(&disk_entry, cursor, sizeof(disk_entry));
        cursor += sizeof(disk_entry);

        if (disk_entry.signature_count > CORRELATION_MAX_SIGNATURES_PER_FILE) {

            return 0;

        }

        entry = &Global_Correlation_Cache[entry_index];
        snprintf(entry->file_name, sizeof(entry->file_name), "%s", disk_entry.file_name);
        entry->file_size = disk_entry.file_size;
        entry->modified_time = disk_entry.modified_time;
        entry->signature_count = disk_entry.signature_count;

        for (uint32_t signature_index = 0; signature_index < disk_entry.signature_count; signature_index++) {
            Type_Correlation_Signature_Disk disk_signature;
            TransmissionSignature *signature = &entry->signatures[signature_index];
            size_t required = sizeof(disk_signature) + 3U * trend_bytes;

            if ((size_t)(end - cursor) < required) {

                Global_Correlation_Cache_Count = 0;
                return 0;

            }

            memcpy(&disk_signature, cursor, sizeof(disk_signature));
            cursor += sizeof(disk_signature);
            memset(signature, 0, sizeof(*signature));
            signature->start_time = disk_signature.start_time;
            signature->duration = disk_signature.duration;
            signature->center_frequency = disk_signature.center_frequency;
            signature->occupied_bandwidth = disk_signature.occupied_bandwidth;
            signature->peak_power = disk_signature.peak_power;
            signature->average_power = disk_signature.average_power;
            memcpy(signature->magnitude_trend, cursor, trend_bytes);
            cursor += trend_bytes;
            memcpy(signature->frequency_trend, cursor, trend_bytes);
            cursor += trend_bytes;
            memcpy(signature->phase_trend, cursor, trend_bytes);
            cursor += trend_bytes;
        }
    }

    if (cursor != end) {

        Global_Correlation_Cache_Count = 0;
        return 0;

    }

    Global_Correlation_Cache_Count = (int)header.entry_count;
    return 1;
}

static unsigned char *correlation_serialize_cache(size_t *serialized_size) {
    /*
        Purpose: Serializes only populated signatures and active trend points
        Returns: Allocated compact buffer, or NULL
    */

    Type_Correlation_Cache_Header header;
    unsigned char *buffer;
    unsigned char *cursor;
    size_t trend_bytes;
    size_t total_size = sizeof(header);

    if (!serialized_size || Global_Correlation_Trend_Points < 1) {

        return NULL;

    }

    *serialized_size = 0;
    trend_bytes = (size_t)Global_Correlation_Trend_Points * sizeof(float);

    for (int entry_index = 0; entry_index < Global_Correlation_Cache_Count; entry_index++) {
        uint32_t signature_count = Global_Correlation_Cache[entry_index].signature_count;
        size_t signature_size = sizeof(Type_Correlation_Signature_Disk) + 3U * trend_bytes;
        size_t entry_size;

        if (signature_count > CORRELATION_MAX_SIGNATURES_PER_FILE ||
            signature_count > (SIZE_MAX - sizeof(Type_Correlation_Cache_Entry_Disk)) / signature_size) {

            return NULL;

        }

        entry_size = sizeof(Type_Correlation_Cache_Entry_Disk) + (size_t)signature_count * signature_size;

        if (total_size > SIZE_MAX - entry_size) {

            return NULL;

        }
        total_size += entry_size;
    }

    buffer = (unsigned char *)malloc(total_size);

    if (!buffer) {

        return NULL;

    }

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, CORRELATION_CACHE_MAGIC, strlen(CORRELATION_CACHE_MAGIC));
    header.version = CORRELATION_CACHE_VERSION;
    header.entry_count = (uint32_t)Global_Correlation_Cache_Count;
    header.trend_points = (uint32_t)Global_Correlation_Trend_Points;
    memcpy(buffer, &header, sizeof(header));
    cursor = buffer + sizeof(header);

    for (int entry_index = 0; entry_index < Global_Correlation_Cache_Count; entry_index++) {
        Type_Correlation_Cache_Entry *entry = &Global_Correlation_Cache[entry_index];
        Type_Correlation_Cache_Entry_Disk disk_entry;

        memset(&disk_entry, 0, sizeof(disk_entry));
        snprintf(disk_entry.file_name, sizeof(disk_entry.file_name), "%s", entry->file_name);
        disk_entry.file_size = entry->file_size;
        disk_entry.modified_time = entry->modified_time;
        disk_entry.signature_count = entry->signature_count;
        memcpy(cursor, &disk_entry, sizeof(disk_entry));
        cursor += sizeof(disk_entry);

        for (uint32_t signature_index = 0; signature_index < entry->signature_count; signature_index++) {
            TransmissionSignature *signature = &entry->signatures[signature_index];
            Type_Correlation_Signature_Disk disk_signature;

            disk_signature.start_time = signature->start_time;
            disk_signature.duration = signature->duration;
            disk_signature.center_frequency = signature->center_frequency;
            disk_signature.occupied_bandwidth = signature->occupied_bandwidth;
            disk_signature.peak_power = signature->peak_power;
            disk_signature.average_power = signature->average_power;
            memcpy(cursor, &disk_signature, sizeof(disk_signature));
            cursor += sizeof(disk_signature);
            memcpy(cursor, signature->magnitude_trend, trend_bytes);
            cursor += trend_bytes;
            memcpy(cursor, signature->frequency_trend, trend_bytes);
            cursor += trend_bytes;
            memcpy(cursor, signature->phase_trend, trend_bytes);
            cursor += trend_bytes;
        }
    }

    *serialized_size = total_size;
    return buffer;
}

static void correlation_load_local_cache(void) {
    /*
        Purpose: Loads the local compact cache without blocking on the server
        Returns: No value
    */

    char path[CORRELATION_MAX_PATH];
    FILE *fp = NULL;
    unsigned char *local_content = NULL;
    long local_size = 0;

    Global_Correlation_Cache_Count = 0;
    correlation_cache_path(path, sizeof(path));
    fp = fopen(path, "rb");

    if (!fp) {

        return;

    }

    if (fseek(fp, 0, SEEK_END) != 0) {

        fclose(fp);
        return;

    }

    local_size = ftell(fp);

    if (local_size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {

        fclose(fp);
        return;

    }

    local_content = (unsigned char *)malloc((size_t)local_size);

    if (!local_content) {

        fclose(fp);
        return;

    }

    if (fread(local_content, 1, (size_t)local_size, fp) == (size_t)local_size) {

        (void)correlation_parse_cache_buffer(local_content, (size_t)local_size);

    }

    free(local_content);
    fclose(fp);
}

static void correlation_load_server_cache_once(void) {
    /*
        Purpose: Fetches the shared cache once on the worker thread, never during tab entry
        Returns: No value
    */

    unsigned char *server_content = NULL;
    size_t server_content_size = 0;
    int found = 0;
    char error[256] = "";

    if (Global_Correlation_Server_Cache_Attempted || Global_Correlation_Cancel) {

        return;

    }

    Global_Correlation_Server_Cache_Attempted = 1;
    correlation_set_status("Loading the shared signature cache in the background...");

    if (DATASTORE_load_content(CORRELATION_DATASTORE_KIND, CORRELATION_DATASTORE_DOCUMENT, &server_content,
                               &server_content_size, &found, error, sizeof(error)) &&
        found) {

        (void)correlation_parse_cache_buffer(server_content, server_content_size);

    }

    if (server_content) {

        DATASTORE_free_content(server_content, server_content_size);

    }

    if (error[0]) {

        snprintf(Global_Correlation_Cache_Sync_Error, sizeof(Global_Correlation_Cache_Sync_Error), "%s", error);

    }
}

static int correlation_save_cache(void) {
    /*
        Purpose: Checkpoints correlation metadata locally and to the authenticated server
        Returns: Save status
    */

    char path[CORRELATION_MAX_PATH];
    char temporary_path[CORRELATION_MAX_PATH];
    char error[256] = "";
    unsigned char *serialized;
    size_t serialized_size = 0;
    FILE *fp;
    int local_ok = 0;
    int server_ok = 0;

    Global_Correlation_Cache_Sync_Error[0] = '\0';
    serialized = correlation_serialize_cache(&serialized_size);

    if (!serialized) {

        snprintf(Global_Correlation_Cache_Sync_Error, sizeof(Global_Correlation_Cache_Sync_Error),
                 "Unable to allocate the cache checkpoint buffer.");
        return 0;

    }

    correlation_cache_path(path, sizeof(path));

    if (snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path) >= 0) {

        fp = fopen(temporary_path, "wb");

        if (fp) {

            int write_ok = fwrite(serialized, 1, serialized_size, fp) == serialized_size;
            int flush_ok = write_ok && fflush(fp) == 0;
            int close_ok = fclose(fp) == 0;

            if (write_ok && flush_ok && close_ok && rename(temporary_path, path) == 0) {

                local_ok = 1;

            }

            else {

                remove(temporary_path);

            }

        }

    }

    server_ok = DATASTORE_save_content(CORRELATION_DATASTORE_KIND, CORRELATION_DATASTORE_DOCUMENT, "", serialized,
                                       serialized_size, error, sizeof(error));

    if (!server_ok) {

        snprintf(Global_Correlation_Cache_Sync_Error, sizeof(Global_Correlation_Cache_Sync_Error), "%s",
                 error[0] ? error : "Unable to save correlation metadata to the server.");

    }

    else if (!local_ok) {

        snprintf(Global_Correlation_Cache_Sync_Error, sizeof(Global_Correlation_Cache_Sync_Error),
                 "Server checkpoint succeeded, but the local backup could not be written.");

    }

    free(serialized);
    return local_ok && server_ok;
}

static int correlation_find_cache_entry(const char *file_name) {
    /*
        Purpose: Finds a cached recording entry
        Returns: Cache index or -1
    */

    for (int i = 0; i < Global_Correlation_Cache_Count; i++) {

        if (strcmp(Global_Correlation_Cache[i].file_name, file_name) == 0) {

            return i;

        }
    }

    return -1;
}

static void correlation_prune_cache(void) {
    /*
        Purpose: Removes cache entries for recordings that no longer exist
        Returns: No value
    */

    int write_index = 0;

    for (int cache_index = 0; cache_index < Global_Correlation_Cache_Count; cache_index++) {
        int found = 0;

        for (int file_index = 0; file_index < Global_Correlation_File_Count; file_index++) {

            if (strcmp(Global_Correlation_Cache[cache_index].file_name, Global_Correlation_Files[file_index]) == 0) {

                found = 1;
                break;

            }
        }

        if (found) {

            if (write_index != cache_index) {

                Global_Correlation_Cache[write_index] = Global_Correlation_Cache[cache_index];

            }

            write_index++;

        }
    }

    Global_Correlation_Cache_Count = write_index;
}

static void correlation_normalize_trend(float values[CORRELATION_TREND_POINTS], int point_count) {
    /*
        Purpose: Z-score normalizes the active portion of a trend
        Returns: No value
    */

    double mean = 0.0;
    double variance = 0.0;

    if (!values || point_count < 1 || point_count > CORRELATION_TREND_POINTS) {

        return;

    }

    for (int i = 0; i < point_count; i++) {
        mean += values[i];
    }

    mean /= (double)point_count;

    for (int i = 0; i < point_count; i++) {
        double centered = (double)values[i] - mean;
        variance += centered * centered;
    }

    variance /= (double)point_count;

    if (variance <= 1e-18) {

        memset(values, 0, sizeof(float) * (size_t)point_count);
        return;

    }

    double standard_deviation = sqrt(variance);

    for (int i = 0; i < point_count; i++) {
        values[i] = (float)(((double)values[i] - mean) / standard_deviation);
    }
}

static int correlation_detect_ranges(FILE *fp, size_t iq_count, double sample_rate,
                                     Type_Correlation_Detected_Range ranges[CORRELATION_MAX_SIGNATURES_PER_FILE]) {
    /*
        Purpose: Detects transmissions using short-window power and hysteresis
        Returns: Number of retained transmission ranges
    */

    size_t window_samples;
    size_t window_count;
    int16_t *samples;
    double *power;
    double *initial_values;
    size_t initial_count;
    double baseline;
    double alpha;
    double start_ratio = pow(10.0, CORRELATION_START_DB / 10.0);
    double end_ratio = pow(10.0, CORRELATION_END_DB / 10.0);
    int low_windows_required;
    int pretrigger_windows;
    int active = 0;
    size_t start_window = 0;
    int low_window_count = 0;
    int range_count = 0;

    if (!fp || iq_count == 0 || sample_rate <= 0.0) {

        return 0;

    }

    window_samples = (size_t)llround(sample_rate * CORRELATION_DETECTION_WINDOW_SEC);

    if (window_samples < 256) {

        window_samples = 256;

    }

    if (window_samples > 8192) {

        window_samples = 8192;

    }

    window_count = (iq_count + window_samples - 1) / window_samples;

    if (window_count == 0 || window_count > SIZE_MAX / sizeof(double)) {

        return 0;

    }

    samples = (int16_t *)malloc(window_samples * 2U * sizeof(int16_t));
    power = (double *)calloc(window_count, sizeof(double));

    if (!samples || !power) {

        free(samples);
        free(power);
        return 0;

    }

    if (fseeko(fp, 0, SEEK_SET) != 0) {

        free(samples);
        free(power);
        return 0;

    }

    for (size_t window = 0; window < window_count; window++) {
        size_t remaining = iq_count - window * window_samples;
        size_t wanted = remaining < window_samples ? remaining : window_samples;
        size_t read_count;
        double sum = 0.0;

        if (Global_Correlation_Cancel) {

            free(samples);
            free(power);
            return 0;

        }

        read_count = fread(samples, sizeof(int16_t) * 2U, wanted, fp);

        if (read_count != wanted) {

            free(samples);
            free(power);
            return 0;

        }

        for (size_t i = 0; i < wanted; i++) {
            double real = (double)samples[2U * i] / 32768.0;
            double imag = (double)samples[2U * i + 1U] / 32768.0;
            sum += real * real + imag * imag;
        }

        power[window] = wanted > 0 ? sum / (double)wanted : 0.0;

        if ((window + 1U) % CORRELATION_HIDDEN_DETECTION_BATCH == 0U) {

            correlation_worker_checkpoint(CORRELATION_HIDDEN_YIELD_MS);

        }
    }

    initial_count = window_count < 500 ? window_count : 500;
    initial_values = (double *)malloc(initial_count * sizeof(double));

    if (!initial_values) {

        free(samples);
        free(power);
        return 0;

    }

    memcpy(initial_values, power, initial_count * sizeof(double));
    qsort(initial_values, initial_count, sizeof(double), correlation_double_compare);
    baseline = initial_values[(initial_count - 1U) / 5U];
    free(initial_values);

    if (baseline < 1e-15) {

        baseline = 1e-15;

    }

    alpha = exp(-((double)window_samples / sample_rate) / CORRELATION_BASELINE_TIME_CONSTANT_SEC);
    low_windows_required = (int)ceil(CORRELATION_END_HOLD_SEC * sample_rate / (double)window_samples);
    pretrigger_windows = (int)ceil(CORRELATION_PRETRIGGER_SEC * sample_rate / (double)window_samples);

    if (low_windows_required < 1) {

        low_windows_required = 1;

    }

    for (size_t window = 0; window < window_count; window++) {
        double current = power[window];

        if (!active) {

            if (current > baseline * start_ratio) {

                active = 1;
                low_window_count = 0;
                start_window = window > (size_t)pretrigger_windows ? window - (size_t)pretrigger_windows : 0;

            }

            else {

                baseline = alpha * baseline + (1.0 - alpha) * current;

            }

        }

        else {

            if (current < baseline * end_ratio) {

                low_window_count++;

            }

            else {

                low_window_count = 0;

            }

            if (low_window_count >= low_windows_required) {

                size_t end_window = window + 1U - (size_t)low_window_count;
                size_t start_sample = start_window * window_samples;
                size_t end_sample = end_window * window_samples;

                if (end_sample > iq_count) {

                    end_sample = iq_count;

                }

                if (end_sample > start_sample &&
                    (double)(end_sample - start_sample) / sample_rate >= CORRELATION_MIN_BURST_SEC &&
                    range_count < CORRELATION_MAX_SIGNATURES_PER_FILE) {

                    ranges[range_count].start_sample = start_sample;
                    ranges[range_count].end_sample = end_sample;
                    range_count++;

                }

                active = 0;
                low_window_count = 0;
                baseline = alpha * baseline + (1.0 - alpha) * current;

            }

        }
    }

    if (active && range_count < CORRELATION_MAX_SIGNATURES_PER_FILE) {

        size_t start_sample = start_window * window_samples;

        if (iq_count > start_sample && (double)(iq_count - start_sample) / sample_rate >= CORRELATION_MIN_BURST_SEC) {

            ranges[range_count].start_sample = start_sample;
            ranges[range_count].end_sample = iq_count;
            range_count++;

        }

    }

    free(samples);
    free(power);

    if (range_count == 0 && iq_count > 0) {

        ranges[0].start_sample = 0;
        ranges[0].end_sample = iq_count;
        range_count = 1;

    }

    return range_count;
}

static double correlation_estimate_occupied_bandwidth(FILE *fp, size_t start_sample, size_t end_sample,
                                                      double sample_rate) {
    /*
        Purpose: Estimates 99-percent occupied bandwidth using averaged FFT power
        Returns: Occupied bandwidth in Hz
    */

    fftw_complex *input = NULL;
    fftw_complex *output = NULL;
    fftw_plan plan = NULL;
    double *spectrum = NULL;
    int16_t *samples = NULL;
    size_t segment_samples;
    size_t block_step;
    int block_count = 0;
    double bandwidth = 0.0;

    if (!fp || end_sample <= start_sample || sample_rate <= 0.0) {

        return 0.0;

    }

    segment_samples = end_sample - start_sample;
    input = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * CORRELATION_OBW_FFT_SIZE);
    output = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * CORRELATION_OBW_FFT_SIZE);
    spectrum = (double *)calloc(CORRELATION_OBW_FFT_SIZE, sizeof(double));
    samples = (int16_t *)malloc(CORRELATION_OBW_FFT_SIZE * 2U * sizeof(int16_t));

    if (!input || !output || !spectrum || !samples) {

        goto cleanup;

    }

    plan = fftw_plan_dft_1d(CORRELATION_OBW_FFT_SIZE, input, output, FFTW_FORWARD, FFTW_ESTIMATE);

    if (!plan) {

        goto cleanup;

    }

    if (segment_samples <= CORRELATION_OBW_FFT_SIZE) {

        block_step = 1;

    }

    else {

        block_step = (segment_samples - CORRELATION_OBW_FFT_SIZE) /
                     (size_t)(CORRELATION_OBW_MAX_BLOCKS > 1 ? CORRELATION_OBW_MAX_BLOCKS - 1 : 1);

        if (block_step < 1) {

            block_step = 1;

        }

    }

    for (size_t offset = 0;
         offset < segment_samples && block_count < CORRELATION_OBW_MAX_BLOCKS && !Global_Correlation_Cancel;
         offset += block_step) {
        size_t absolute = start_sample + offset;
        size_t available = end_sample - absolute;
        size_t wanted = available < CORRELATION_OBW_FFT_SIZE ? available : CORRELATION_OBW_FFT_SIZE;

        if (wanted < 64 || absolute > (size_t)(INT64_MAX / 4)) {

            break;

        }

        if (fseeko(fp, (off_t)(absolute * 4U), SEEK_SET) != 0 ||
            fread(samples, sizeof(int16_t) * 2U, wanted, fp) != wanted) {

            break;

        }

        for (int i = 0; i < CORRELATION_OBW_FFT_SIZE; i++) {
            double window = 0.0;
            double real = 0.0;
            double imag = 0.0;

            if ((size_t)i < wanted) {

                window = 0.5 - 0.5 * cos((2.0 * M_PI * (double)i) / (double)(CORRELATION_OBW_FFT_SIZE - 1));
                real = (double)samples[2U * (size_t)i] / 32768.0;
                imag = (double)samples[2U * (size_t)i + 1U] / 32768.0;

            }

            input[i][0] = real * window;
            input[i][1] = imag * window;
        }

        fftw_execute(plan);

        for (int i = 0; i < CORRELATION_OBW_FFT_SIZE; i++) {
            int shifted = (i + CORRELATION_OBW_FFT_SIZE / 2) % CORRELATION_OBW_FFT_SIZE;
            double real = output[i][0];
            double imag = output[i][1];
            spectrum[shifted] += real * real + imag * imag;
        }

        block_count++;
        correlation_worker_checkpoint(CORRELATION_HIDDEN_YIELD_MS);

        if (segment_samples <= CORRELATION_OBW_FFT_SIZE) {

            break;

        }
    }

    if (block_count > 0) {

        double total = 0.0;
        double lower_target;
        double upper_target;
        double cumulative = 0.0;
        int lower_bin = 0;
        int upper_bin = CORRELATION_OBW_FFT_SIZE - 1;
        int lower_found = 0;

        for (int i = 0; i < CORRELATION_OBW_FFT_SIZE; i++) {
            total += spectrum[i];
        }

        lower_target = total * 0.005;
        upper_target = total * 0.995;

        if (total > DBL_MIN) {

            for (int i = 0; i < CORRELATION_OBW_FFT_SIZE; i++) {
                cumulative += spectrum[i];

                if (!lower_found && cumulative >= lower_target) {

                    lower_bin = i;
                    lower_found = 1;

                }

                if (cumulative >= upper_target) {

                    upper_bin = i;
                    break;

                }
            }

            bandwidth = ((double)(upper_bin - lower_bin + 1) / (double)CORRELATION_OBW_FFT_SIZE) * sample_rate;

        }

    }

cleanup:

    if (plan) {

        fftw_destroy_plan(plan);

    }

    if (input) {

        fftw_free(input);

    }

    if (output) {

        fftw_free(output);

    }

    free(spectrum);
    free(samples);
    return bandwidth;
}

static int correlation_build_signature(FILE *fp, size_t iq_count, double sample_rate, double center_frequency,
                                       Type_Correlation_Detected_Range range, TransmissionSignature *signature) {
    /*
        Purpose: Builds one fixed-length transmission signature
        Returns: Build status
    */

    int16_t *samples = NULL;
    double magnitude_sum[CORRELATION_TREND_POINTS] = {0.0};
    double frequency_sum[CORRELATION_TREND_POINTS] = {0.0};
    double phase_sum[CORRELATION_TREND_POINTS] = {0.0};
    size_t magnitude_count[CORRELATION_TREND_POINTS] = {0};
    size_t frequency_count[CORRELATION_TREND_POINTS] = {0};
    size_t phase_count[CORRELATION_TREND_POINTS] = {0};
    size_t segment_samples;
    size_t processed = 0;
    size_t processed_chunks = 0;
    double total_power = 0.0;
    double peak_power = 0.0;
    double frequency_mean = 0.0;
    int frequency_points = 0;
    double previous_real = 0.0;
    double previous_imag = 0.0;
    double previous_phase = 0.0;
    double unwrapped_phase = 0.0;
    int have_previous = 0;
    const size_t chunk_samples = 8192;

    if (!fp || !signature || range.end_sample <= range.start_sample || range.end_sample > iq_count ||
        sample_rate <= 0.0 || range.start_sample > (size_t)(INT64_MAX / 4)) {

        return 0;

    }

    memset(signature, 0, sizeof(*signature));
    segment_samples = range.end_sample - range.start_sample;
    signature->start_time = (double)range.start_sample / sample_rate;
    signature->duration = (double)segment_samples / sample_rate;
    signature->center_frequency = center_frequency;
    samples = (int16_t *)malloc(chunk_samples * 2U * sizeof(int16_t));

    if (!samples || fseeko(fp, (off_t)(range.start_sample * 4U), SEEK_SET) != 0) {

        free(samples);
        return 0;

    }

    while (processed < segment_samples) {
        size_t wanted = segment_samples - processed;
        size_t read_count;

        if (wanted > chunk_samples) {

            wanted = chunk_samples;

        }

        if (Global_Correlation_Cancel) {

            free(samples);
            return 0;

        }

        read_count = fread(samples, sizeof(int16_t) * 2U, wanted, fp);

        if (read_count != wanted) {

            free(samples);
            return 0;

        }

        for (size_t i = 0; i < wanted; i++) {
            size_t relative_index = processed + i;
            int point = (int)((relative_index * (size_t)Global_Correlation_Trend_Points) / segment_samples);
            double real = (double)samples[2U * i] / 32768.0;
            double imag = (double)samples[2U * i + 1U] / 32768.0;
            double magnitude = hypot(real, imag);
            double power = real * real + imag * imag;

            if (point >= Global_Correlation_Trend_Points) {

                point = Global_Correlation_Trend_Points - 1;

            }

            magnitude_sum[point] += magnitude;
            magnitude_count[point]++;
            total_power += power;

            if (power > peak_power) {

                peak_power = power;

            }

            {
                double phase = atan2(imag, real);

                if (have_previous) {

                    double product_real = real * previous_real + imag * previous_imag;
                    double product_imag = imag * previous_real - real * previous_imag;
                    double instantaneous_frequency = (sample_rate / (2.0 * M_PI)) * atan2(product_imag, product_real);
                    double phase_delta = phase - previous_phase;

                    while (phase_delta > M_PI) {
                        phase_delta -= 2.0 * M_PI;
                    }

                    while (phase_delta < -M_PI) {
                        phase_delta += 2.0 * M_PI;
                    }

                    unwrapped_phase += phase_delta;
                    frequency_sum[point] += instantaneous_frequency;
                    frequency_count[point]++;

                }

                else {

                    unwrapped_phase = phase;

                }

                phase_sum[point] += unwrapped_phase;
                phase_count[point]++;
                previous_phase = phase;
            }

            previous_real = real;
            previous_imag = imag;
            have_previous = 1;
        }

        processed += wanted;
        processed_chunks++;

        if (processed_chunks % CORRELATION_HIDDEN_SIGNATURE_BATCH == 0U) {

            correlation_worker_checkpoint(CORRELATION_HIDDEN_YIELD_MS);

        }
    }

    free(samples);

    for (int point = 0; point < Global_Correlation_Trend_Points; point++) {
        signature->magnitude_trend[point] =
            magnitude_count[point] > 0 ? (float)(magnitude_sum[point] / (double)magnitude_count[point]) : 0.0f;
        signature->frequency_trend[point] =
            frequency_count[point] > 0 ? (float)(frequency_sum[point] / (double)frequency_count[point]) : 0.0f;
        signature->phase_trend[point] =
            phase_count[point] > 0 ? (float)(phase_sum[point] / (double)phase_count[point]) : 0.0f;

        if (frequency_count[point] > 0) {

            frequency_mean += signature->frequency_trend[point];
            frequency_points++;

        }
    }

    if (frequency_points > 0) {

        frequency_mean /= (double)frequency_points;

    }

    for (int point = 0; point < Global_Correlation_Trend_Points; point++) {
        double phase_position =
            Global_Correlation_Trend_Points > 1 ? (double)point / (double)(Global_Correlation_Trend_Points - 1) : 0.0;
        double phase_line =
            (double)signature->phase_trend[0] +
            ((double)signature->phase_trend[Global_Correlation_Trend_Points - 1] - (double)signature->phase_trend[0]) *
                phase_position;

        signature->frequency_trend[point] = (float)((double)signature->frequency_trend[point] - frequency_mean);
        /* Remove constant carrier rotation so the graph shows phase-shape changes. */
        signature->phase_trend[point] = (float)((double)signature->phase_trend[point] - phase_line);
    }

    correlation_normalize_trend(signature->magnitude_trend, Global_Correlation_Trend_Points);
    correlation_normalize_trend(signature->frequency_trend, Global_Correlation_Trend_Points);
    correlation_normalize_trend(signature->phase_trend, Global_Correlation_Trend_Points);
    signature->peak_power = peak_power;
    signature->average_power = segment_samples > 0 ? total_power / (double)segment_samples : 0.0;
    signature->occupied_bandwidth =
        correlation_estimate_occupied_bandwidth(fp, range.start_sample, range.end_sample, sample_rate);

    return !Global_Correlation_Cancel;
}

static int correlation_extract_file_signatures(const Type_Correlation_Work_Item *item,
                                               Type_Correlation_Cache_Entry *entry) {
    /*
        Purpose: Detects and extracts all retained signatures for one recording
        Returns: Extraction status
    */

    FILE *fp = NULL;
    size_t iq_count = 0;
    Type_Correlation_Detected_Range ranges[CORRELATION_MAX_SIGNATURES_PER_FILE];
    int range_count;

    if (!item || !entry) {

        return 0;

    }

    if (!sec_fopen_complex16(item->path, &fp, &iq_count)) {

        return 0;

    }

    range_count = correlation_detect_ranges(fp, iq_count, item->sample_rate, ranges);

    if (range_count <= 0) {

        fclose(fp);
        return 0;

    }

    memset(entry, 0, sizeof(*entry));
    snprintf(entry->file_name, sizeof(entry->file_name), "%s", item->file_name);
    entry->file_size = item->file_size;
    entry->modified_time = item->modified_time;

    for (int i = 0; i < range_count && i < CORRELATION_MAX_SIGNATURES_PER_FILE; i++) {

        if (correlation_build_signature(fp, iq_count, item->sample_rate, item->center_frequency, ranges[i],
                                        &entry->signatures[entry->signature_count])) {

            entry->signature_count++;

        }

        if (Global_Correlation_Cancel) {

            break;

        }
    }

    fclose(fp);
    return entry->signature_count > 0;
}

static double correlation_maximum_normalized_cross_correlation(const float left[CORRELATION_TREND_POINTS],
                                                               const float right[CORRELATION_TREND_POINTS]) {
    /*
        Purpose: Computes maximum normalized cross-correlation over bounded lag
        Returns: Similarity from zero to one
    */

    double best = 0.0;
    double full_left_energy = 0.0;
    double full_right_energy = 0.0;
    int point_count = Global_Correlation_Trend_Points;
    int maximum_lag;

    if (point_count < CORRELATION_MIN_TREND_POINTS) {

        point_count = CORRELATION_MIN_TREND_POINTS;

    }

    if (point_count > CORRELATION_MAX_TREND_POINTS) {

        point_count = CORRELATION_MAX_TREND_POINTS;

    }

    maximum_lag = (int)((double)point_count * CORRELATION_MAX_LAG_PERCENT);

    if (maximum_lag < 1) {

        maximum_lag = 1;

    }

    for (int i = 0; i < point_count; i++) {
        full_left_energy += (double)left[i] * (double)left[i];
        full_right_energy += (double)right[i] * (double)right[i];
    }

    if (full_left_energy <= DBL_MIN && full_right_energy <= DBL_MIN) {

        return 1.0;

    }

    if (full_left_energy <= DBL_MIN || full_right_energy <= DBL_MIN) {

        return 0.0;

    }

    for (int lag = -maximum_lag; lag <= maximum_lag; lag++) {
        double numerator = 0.0;
        double left_energy = 0.0;
        double right_energy = 0.0;
        int count = 0;

        for (int i = 0; i < point_count; i++) {
            int j = i + lag;

            if (j < 0 || j >= point_count) {

                continue;

            }

            numerator += (double)left[i] * (double)right[j];
            left_energy += (double)left[i] * (double)left[i];
            right_energy += (double)right[j] * (double)right[j];
            count++;
        }

        if (count >= point_count / 2 && left_energy > DBL_MIN && right_energy > DBL_MIN) {

            double score = numerator / sqrt(left_energy * right_energy);

            if (score > best) {

                best = score;

            }

        }

        if (((unsigned int)(lag + maximum_lag + 1) % CORRELATION_HIDDEN_NCC_LAG_BATCH) == 0U) {

            correlation_worker_checkpoint(CORRELATION_HIDDEN_YIELD_MS);

        }
    }

    return correlation_clamp_double(best, 0.0, 1.0);
}

static double correlation_bandwidth_similarity(double left, double right) {
    /*
        Purpose: Compares occupied bandwidth values
        Returns: Similarity from zero to one
    */

    double maximum = left > right ? left : right;
    double minimum = left < right ? left : right;

    if (maximum <= 1.0) {

        return maximum == minimum ? 1.0 : 0.0;

    }

    return correlation_clamp_double(minimum / maximum, 0.0, 1.0);
}

static double correlation_signature_energy(const TransmissionSignature *signature) {
    /*
        Purpose: Ranks candidate query bursts by approximate captured energy
        Returns: Energy score
    */

    if (!signature) {

        return 0.0;

    }

    return signature->average_power * signature->duration;
}

static int correlation_prepare_work_item(int file_index, Type_Correlation_Work_Item *item) {
    /*
        Purpose: Builds file metadata used by the worker
        Returns: Preparation status
    */

    struct stat st;

    if (!item || file_index < 0 || file_index >= Global_Correlation_File_Count) {

        return 0;

    }

    memset(item, 0, sizeof(*item));
    snprintf(item->file_name, sizeof(item->file_name), "%s", Global_Correlation_Files[file_index]);

    if (snprintf(item->path, sizeof(item->path), "%s/%s", Global_Correlation_Record_Dir,
                 Global_Correlation_Files[file_index]) < 0) {

        return 0;

    }

    if (stat(item->path, &st) != 0 || st.st_size < 4) {

        return 0;

    }

    item->file_size = (uint64_t)st.st_size;
    item->modified_time = (int64_t)st.st_mtime;
    correlation_parse_recording_metadata(item->file_name, &item->sample_rate, &item->center_frequency);
    return item->sample_rate > 0.0;
}

static int correlation_get_or_build_entry(const Type_Correlation_Work_Item *item) {
    /*
        Purpose: Reuses a valid cache entry or extracts a replacement
        Returns: Cache index or -1
    */

    int index;
    Type_Correlation_Cache_Entry replacement;

    if (!item) {

        return -1;

    }

    index = correlation_find_cache_entry(item->file_name);

    if (index >= 0 && Global_Correlation_Cache[index].file_size == item->file_size &&
        Global_Correlation_Cache[index].modified_time == item->modified_time &&
        Global_Correlation_Cache[index].signature_count > 0) {

        return index;

    }

    if (!correlation_extract_file_signatures(item, &replacement)) {

        return -1;

    }

    if (index >= 0) {

        Global_Correlation_Cache[index] = replacement;
        return index;

    }

    if (Global_Correlation_Cache_Count >= CORRELATION_MAX_FILES) {

        return -1;

    }

    Global_Correlation_Cache[Global_Correlation_Cache_Count] = replacement;
    Global_Correlation_Cache_Count++;
    return Global_Correlation_Cache_Count - 1;
}

static double correlation_calculate_score(double magnitude_score, double frequency_score, double bandwidth_score) {
    /*
        Purpose: Combines component similarities using user-entered score scalars
        Returns: Normalized score from zero to one
    */

    double weight_sum =
        Global_Correlation_Magnitude_Weight + Global_Correlation_Frequency_Weight + Global_Correlation_Bandwidth_Weight;

    if (weight_sum <= DBL_MIN) {

        return 0.0;

    }

    return correlation_clamp_double((Global_Correlation_Magnitude_Weight * magnitude_score +
                                     Global_Correlation_Frequency_Weight * frequency_score +
                                     Global_Correlation_Bandwidth_Weight * bandwidth_score) /
                                        weight_sum,
                                    0.0, 1.0);
}

static void correlation_rescore_results(void) {
    /*
        Purpose: Recalculates and re-sorts existing results after score scalar changes
        Returns: No value
    */

    for (int i = 0; i < Global_Correlation_Result_Count; i++) {
        Global_Correlation_Results[i].score = correlation_calculate_score(
            Global_Correlation_Results[i].magnitude_score, Global_Correlation_Results[i].frequency_score,
            Global_Correlation_Results[i].bandwidth_score);
    }

    qsort(Global_Correlation_Results, (size_t)Global_Correlation_Result_Count, sizeof(Global_Correlation_Results[0]),
          correlation_result_compare);
    Global_Correlation_Selected_Result = Global_Correlation_Result_Count > 0 ? 0 : -1;
    Global_Correlation_Result_Scroll = 0;
}

static int correlation_worker(void *unused) {
    /*
        Purpose: Builds cached signatures and ranks every recording
        Returns: Worker status
    */

    Type_Correlation_Work_Item query_item;
    int query_cache_index;
    int best_query_signature = 0;
    double best_query_energy = -1.0;
    int result_count = 0;
    int unsaved_cache_files = 0;
    int checkpoint_failed = 0;
    int query_needed_build = 0;

    (void)unused;

    Global_Correlation_Worker_Low_Priority = 0;
    correlation_worker_checkpoint(0U);
    Global_Correlation_Result_Count = 0;
    Global_Correlation_Query_Signature_Valid = 0;
    Global_Correlation_Progress = 0;
    Global_Correlation_Progress_Total = Global_Correlation_File_Count;
    correlation_load_server_cache_once();
    correlation_prune_cache();

    if (!correlation_prepare_work_item(Global_Correlation_Selected_File, &query_item)) {

        correlation_set_status("The selected query recording could not be opened.");
        Global_Correlation_Working = 0;
        return 0;

    }

    snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status), "Extracting query signature from %.180s",
             query_item.file_name);
    query_cache_index = correlation_find_cache_entry(query_item.file_name);
    query_needed_build = query_cache_index < 0 ||
                         Global_Correlation_Cache[query_cache_index].file_size != query_item.file_size ||
                         Global_Correlation_Cache[query_cache_index].modified_time != query_item.modified_time ||
                         Global_Correlation_Cache[query_cache_index].signature_count == 0;

    query_cache_index = correlation_get_or_build_entry(&query_item);

    if (query_cache_index < 0 || Global_Correlation_Cancel) {

        correlation_set_status(Global_Correlation_Cancel ? "Correlation stopped before the query signature completed."
                                                         : "No usable transmission was found in the query recording.");
        Global_Correlation_Working = 0;
        return 0;

    }

    if (query_needed_build) {

        unsaved_cache_files++;

    }

    for (uint32_t i = 0; i < Global_Correlation_Cache[query_cache_index].signature_count; i++) {
        double energy = correlation_signature_energy(&Global_Correlation_Cache[query_cache_index].signatures[i]);

        if (energy > best_query_energy) {

            best_query_energy = energy;
            best_query_signature = (int)i;

        }
    }

    Global_Correlation_Query_Signature = Global_Correlation_Cache[query_cache_index].signatures[best_query_signature];
    Global_Correlation_Query_Signature_Valid = 1;

    for (int file_index = 0; file_index < Global_Correlation_File_Count && !Global_Correlation_Cancel; file_index++) {
        Type_Correlation_Work_Item item;
        int cache_index;
        Type_Correlation_Result best_result;
        int have_result = 0;
        int old_cache_index;
        int entry_needed_build;

        Global_Correlation_Progress = file_index + 1;

        if (file_index == Global_Correlation_Selected_File) {

            continue;

        }

        if (!correlation_prepare_work_item(file_index, &item)) {

            continue;

        }

        snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status), "Comparing %d/%d: %.180s",
                 file_index + 1, Global_Correlation_File_Count, item.file_name);
        old_cache_index = correlation_find_cache_entry(item.file_name);
        entry_needed_build = old_cache_index < 0 ||
                             Global_Correlation_Cache[old_cache_index].file_size != item.file_size ||
                             Global_Correlation_Cache[old_cache_index].modified_time != item.modified_time ||
                             Global_Correlation_Cache[old_cache_index].signature_count == 0;

        cache_index = correlation_get_or_build_entry(&item);

        if (cache_index < 0) {

            continue;

        }

        if (entry_needed_build) {

            unsaved_cache_files++;

        }

        memset(&best_result, 0, sizeof(best_result));
        snprintf(best_result.file_name, sizeof(best_result.file_name), "%s", item.file_name);

        for (uint32_t signature_index = 0; signature_index < Global_Correlation_Cache[cache_index].signature_count;
             signature_index++) {
            TransmissionSignature *stored = &Global_Correlation_Cache[cache_index].signatures[signature_index];
            double magnitude_score = correlation_maximum_normalized_cross_correlation(
                Global_Correlation_Query_Signature.magnitude_trend, stored->magnitude_trend);
            double frequency_score = correlation_maximum_normalized_cross_correlation(
                Global_Correlation_Query_Signature.frequency_trend, stored->frequency_trend);
            double bandwidth_score = correlation_bandwidth_similarity(
                Global_Correlation_Query_Signature.occupied_bandwidth, stored->occupied_bandwidth);
            double score = correlation_calculate_score(magnitude_score, frequency_score, bandwidth_score);

            correlation_worker_checkpoint(CORRELATION_HIDDEN_YIELD_MS);

            if (!have_result || score > best_result.score) {

                best_result.signature_index = (int)signature_index;
                best_result.score = score;
                best_result.magnitude_score = magnitude_score;
                best_result.frequency_score = frequency_score;
                best_result.bandwidth_score = bandwidth_score;
                best_result.signature = *stored;
                have_result = 1;

            }
        }

        if (have_result && result_count < CORRELATION_MAX_RESULTS) {

            Global_Correlation_Results[result_count++] = best_result;

        }

        if (unsaved_cache_files >= CORRELATION_CACHE_CHECKPOINT_FILES) {

            snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                     "Saving server cache checkpoint after %d completed files...", CORRELATION_CACHE_CHECKPOINT_FILES);

            if (!correlation_save_cache()) {

                checkpoint_failed = 1;

            }

            unsaved_cache_files = 0;

        }

        correlation_worker_checkpoint(CORRELATION_HIDDEN_FILE_DELAY_MS);
    }

    Global_Correlation_Progress = Global_Correlation_File_Count;

    /*
     * Persist every final partial batch, including when Stop was requested, so
     * completed metadata is not lost just because fewer than five files remain.
     */

    if (unsaved_cache_files > 0) {

        if (!correlation_save_cache()) {

            checkpoint_failed = 1;

        }

    }

    if (Global_Correlation_Cancel) {

        Global_Correlation_Query_Signature_Valid = 0;
        Global_Correlation_Result_Count = 0;
        snprintf(
            Global_Correlation_Status, sizeof(Global_Correlation_Status),
            checkpoint_failed
                ? "Correlation stopped. Completed metadata was checkpointed where possible; cache sync reported: %.220s"
                : "Correlation stopped. Completed metadata was saved to the cache.",
            Global_Correlation_Cache_Sync_Error);

    }

    else {

        qsort(Global_Correlation_Results, (size_t)result_count, sizeof(Global_Correlation_Results[0]),
              correlation_result_compare);
        Global_Correlation_Result_Count = result_count;
        Global_Correlation_Selected_Result = result_count > 0 ? 0 : -1;
        Global_Correlation_Result_Scroll = 0;
        {
            double weight_sum = Global_Correlation_Magnitude_Weight + Global_Correlation_Frequency_Weight +
                                Global_Correlation_Bandwidth_Weight;
            double magnitude_percent =
                weight_sum > DBL_MIN ? 100.0 * Global_Correlation_Magnitude_Weight / weight_sum : 0.0;
            double frequency_percent =
                weight_sum > DBL_MIN ? 100.0 * Global_Correlation_Frequency_Weight / weight_sum : 0.0;
            double bandwidth_percent =
                weight_sum > DBL_MIN ? 100.0 * Global_Correlation_Bandwidth_Weight / weight_sum : 0.0;

            if (checkpoint_failed) {

                snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                         "Correlation complete: %d ranked. Weights %.1f/%.1f/%.1f%%. Cache sync reported: %.180s",
                         result_count, magnitude_percent, frequency_percent, bandwidth_percent,
                         Global_Correlation_Cache_Sync_Error);

            }

            else {

                snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                         "Correlation complete: %d recording(s) ranked. Weights: magnitude %.1f%%, frequency %.1f%%, "
                         "bandwidth %.1f%%.",
                         result_count, magnitude_percent, frequency_percent, bandwidth_percent);

            }
        }

    }

    Global_Correlation_Working = 0;
    return 0;
}

static void correlation_start_worker(void) {
    /*
        Purpose: Starts a background comparison
        Returns: No value
    */

    if (Global_Correlation_Working || Global_Correlation_Selected_File < 0 ||
        Global_Correlation_Selected_File >= Global_Correlation_File_Count) {

        return;

    }

    if (Global_Correlation_Thread) {

        SDL_WaitThread(Global_Correlation_Thread, NULL);
        Global_Correlation_Thread = NULL;

    }

    Global_Correlation_Cancel = 0;
    Global_Correlation_Working = 1;
    Global_Correlation_Thread = SDL_CreateThread(correlation_worker, "CorrelationWorker", NULL);

    if (!Global_Correlation_Thread) {

        Global_Correlation_Working = 0;
        correlation_set_status("Unable to start the correlation worker thread.");

    }
}

static void correlation_clear_cache(void) {
    /*
        Purpose: Removes all persisted and in-memory correlation metadata
        Returns: No value
    */

    char path[CORRELATION_MAX_PATH];
    char error[256] = "";
    int deleted = 0;
    int server_ok;

    if (Global_Correlation_Working) {

        return;

    }

    correlation_cache_path(path, sizeof(path));
    remove(path);

    if (snprintf(path, sizeof(path), "%s/%s", Global_Correlation_Record_Dir, CORRELATION_LEGACY_CACHE_NAME) > 0) {

        remove(path);

    }

    if (snprintf(path, sizeof(path), "%s/%s", Global_Correlation_Record_Dir, CORRELATION_OLDER_CACHE_NAME) > 0) {

        remove(path);

    }

    if (snprintf(path, sizeof(path), "%s/%s", Global_Correlation_Record_Dir, CORRELATION_OLDEST_CACHE_NAME) > 0) {

        remove(path);

    }

    server_ok = DATASTORE_delete_content(CORRELATION_DATASTORE_KIND, CORRELATION_DATASTORE_DOCUMENT, &deleted, error,
                                         sizeof(error));
    {
        int legacy_deleted = 0;
        char legacy_error[256] = "";

        (void)DATASTORE_delete_content(CORRELATION_DATASTORE_KIND, CORRELATION_LEGACY_DATASTORE_DOCUMENT,
                                       &legacy_deleted, legacy_error, sizeof(legacy_error));
        (void)DATASTORE_delete_content(CORRELATION_DATASTORE_KIND, CORRELATION_OLDER_DATASTORE_DOCUMENT,
                                       &legacy_deleted, legacy_error, sizeof(legacy_error));
    }
    Global_Correlation_Server_Cache_Attempted = 1;

    Global_Correlation_Cache_Count = 0;
    Global_Correlation_Result_Count = 0;
    Global_Correlation_Query_Signature_Valid = 0;
    Global_Correlation_Selected_Result = -1;

    if (!server_ok) {

        snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                 "Local correlation cache cleared, but the server cache could not be deleted: %.220s",
                 error[0] ? error : "unknown server error");

    }

    else {

        correlation_set_status(
            "Correlation cache cleared locally and on the server. Signatures will be rebuilt on the next comparison.");

    }
}

static void correlation_get_layout(int win_w, int win_h, SDL_Rect *status, SDL_Rect *engine, SDL_Rect *recordings,
                                   SDL_Rect *results, SDL_Rect *graphs) {
    /*
        Purpose: Computes the full-width stacked correlation layout
        Returns: No value
    */

    int available_w = win_w - 2 * CORRELATION_PANEL_MARGIN;
    int gap = 12;
    int engine_h = 88;
    int recordings_h = 218;
    int graphs_h = 166;
    int engine_y = 12;
    int recordings_y = engine_y + engine_h + gap;
    int graphs_y = win_h - CORRELATION_PANEL_MARGIN - graphs_h;
    int results_y = recordings_y + recordings_h + gap;
    int results_h = graphs_y - gap - results_y;

    if (available_w < 320) {

        available_w = 320;

    }

    if (results_h < 150) {

        recordings_h = 190;
        graphs_h = 132;
        recordings_y = engine_y + engine_h + gap;
        graphs_y = win_h - CORRELATION_PANEL_MARGIN - graphs_h;
        results_y = recordings_y + recordings_h + gap;
        results_h = graphs_y - gap - results_y;

    }

    if (results_h < 96) {

        results_h = 96;

    }

    if (status) {

        *status = (SDL_Rect){0, 0, 0, 0};

    }

    if (engine) {

        *engine = (SDL_Rect){CORRELATION_PANEL_MARGIN, engine_y, available_w, engine_h};

    }

    if (recordings) {

        *recordings = (SDL_Rect){CORRELATION_PANEL_MARGIN, recordings_y, available_w, recordings_h};

    }

    if (results) {

        *results = (SDL_Rect){CORRELATION_PANEL_MARGIN, results_y, available_w, results_h};

    }

    if (graphs) {

        *graphs = (SDL_Rect){CORRELATION_PANEL_MARGIN, graphs_y, available_w, graphs_h};

    }
}

static void correlation_get_engine_controls(SDL_Rect engine, SDL_Rect *magnitude_box, SDL_Rect *frequency_box,
                                            SDL_Rect *bandwidth_box, SDL_Rect *resolution_box, SDL_Rect *compare_button,
                                            SDL_Rect *stop_button, SDL_Rect *rescan_button, SDL_Rect *clear_button) {
    /*
        Purpose: Computes responsive horizontal Signature Engine controls
        Returns: No value
    */

    int box_y = engine.y + 44;
    int right = engine.x + engine.w - 12;
    int clear_w = 170;
    int rescan_w = 128;
    int stop_w = 112;
    int compare_w = 180;
    int button_gap = 10;
    SDL_Rect local_clear;
    SDL_Rect local_rescan;
    SDL_Rect local_stop;
    SDL_Rect local_compare;
    int start = engine.x + 12;
    int end;
    int available;
    int group_gap = 48;
    int label_gap = 12;
    int box_w = 66;
    int resolution_w = 78;
    int required = 30 + label_gap + box_w + group_gap + 34 + label_gap + box_w + group_gap + 20 + label_gap + box_w +
                   group_gap + 76 + label_gap + resolution_w;
    int x;

    if (engine.w < 1250) {

        clear_w = 150;
        rescan_w = 112;
        stop_w = 96;
        compare_w = 156;
        group_gap = 38;

    }

    if (engine.w < 1000) {

        clear_w = 120;
        rescan_w = 88;
        stop_w = 82;
        compare_w = 126;
        button_gap = 7;
        group_gap = 22;
        label_gap = 8;
        box_w = 50;
        resolution_w = 58;

    }

    local_clear = (SDL_Rect){right - clear_w, box_y, clear_w, 34};
    local_rescan = (SDL_Rect){local_clear.x - button_gap - rescan_w, box_y, rescan_w, 34};
    local_stop = (SDL_Rect){local_rescan.x - button_gap - stop_w, box_y, stop_w, 34};
    local_compare = (SDL_Rect){local_stop.x - button_gap - compare_w, box_y, compare_w, 34};
    end = local_compare.x - 20;
    available = end - start;

    if (available < required) {

        group_gap = 26;
        label_gap = 10;
        box_w = 54;
        resolution_w = 64;

    }

    x = start + 30 + label_gap;

    if (magnitude_box) {

        *magnitude_box = (SDL_Rect){x, box_y, box_w, 34};

    }

    x += box_w + group_gap + 34 + label_gap;

    if (frequency_box) {

        *frequency_box = (SDL_Rect){x, box_y, box_w, 34};

    }

    x += box_w + group_gap + 20 + label_gap;

    if (bandwidth_box) {

        *bandwidth_box = (SDL_Rect){x, box_y, box_w, 34};

    }

    x += box_w + group_gap + 76 + label_gap;

    if (resolution_box) {

        *resolution_box = (SDL_Rect){x, box_y, resolution_w, 34};

    }

    if (compare_button) {

        *compare_button = local_compare;

    }

    if (stop_button) {

        *stop_button = local_stop;

    }

    if (rescan_button) {

        *rescan_button = local_rescan;

    }

    if (clear_button) {

        *clear_button = local_clear;

    }
}

static void correlation_get_clear_cache_dialog(int win_w, int win_h, SDL_Rect *dialog, SDL_Rect *confirm_button,
                                               SDL_Rect *cancel_button) {
    /*
        Purpose: Computes the centered destructive-action confirmation dialog
        Returns: No value
    */

    int dialog_w = win_w < 620 ? win_w - 48 : 560;
    int dialog_h = 210;
    SDL_Rect local_dialog;

    if (dialog_w < 320) {

        dialog_w = 320;

    }

    local_dialog = (SDL_Rect){(win_w - dialog_w) / 2, (win_h - dialog_h) / 2, dialog_w, dialog_h};

    if (dialog) {

        *dialog = local_dialog;

    }

    if (confirm_button) {

        *confirm_button =
            (SDL_Rect){local_dialog.x + local_dialog.w - 174, local_dialog.y + local_dialog.h - 52, 150, 34};

    }

    if (cancel_button) {

        *cancel_button = (SDL_Rect){local_dialog.x + 24, local_dialog.y + local_dialog.h - 52, 120, 34};

    }
}

static void correlation_draw_centered_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect,
                                           SDL_Color color) {
    /*
        Purpose: Draws centered text
        Returns: No value
    */

    int width = 0;
    int height = 0;

    if (!renderer || !font || !text) {

        return;

    }

    if (TTF_SizeText(font, text, &width, &height) != 0) {

        width = 0;
        height = 0;

    }

    draw_text(renderer, font, text, rect.x + (rect.w - width) / 2, rect.y + (rect.h - height) / 2, color);
}

static void correlation_draw_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label,
                                    int hovered, int disabled) {
    /*
        Purpose: Draws a workstation button
        Returns: No value
    */

    SDL_Color border = disabled ? Correlation_Muted : (hovered ? Correlation_Border_Hi : Correlation_Border);
    SDL_Color text = disabled ? Correlation_Muted : (hovered ? Correlation_Text : Correlation_Muted);

    draw_filled_rect(renderer, rect, disabled ? (SDL_Color){0, 8, 3, 255} : Correlation_Panel_2);
    draw_outline_rect(renderer, rect, border);
    correlation_draw_centered_text(renderer, font, label, rect, text);
}

static void correlation_get_result_controls(SDL_Rect row_rect, SDL_Rect *export_button, SDL_Rect *score_box) {
    /*
        Purpose: Computes the per-result Analysis export button and similarity box
        Returns: No value
    */

    int export_w = row_rect.w >= 900 ? 154 : 126;
    SDL_Rect local_score = {row_rect.x + row_rect.w - 118, row_rect.y + 9, 106, row_rect.h - 18};
    SDL_Rect local_export = {local_score.x - 10 - export_w, row_rect.y + (row_rect.h - 34) / 2, export_w, 34};

    if (export_button) {

        *export_button = local_export;

    }

    if (score_box) {

        *score_box = local_score;

    }
}

static int correlation_text_width(TTF_Font *font, const char *text) {
    /*
        Purpose: Measures one text line
        Returns: Pixel width
    */

    int width = 0;
    int height = 0;

    if (!font || !text || TTF_SizeText(font, text, &width, &height) != 0) {

        return 0;

    }

    return width;
}

static int correlation_draw_wrapped_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect,
                                         SDL_Color color, int max_lines) {
    /*
        Purpose: Draws text with dynamic word wrapping for the current panel width
        Returns: Number of rendered lines
    */

    char buffer[2048];
    char line[1024];
    char *cursor;
    int line_height = TTF_FontHeight(font) + 3;
    int line_count = 0;

    if (!renderer || !font || !text || rect.w <= 4 || rect.h <= 4) {

        return 0;

    }

    snprintf(buffer, sizeof(buffer), "%s", text);
    line[0] = '\0';
    cursor = buffer;

    while (*cursor && (max_lines <= 0 || line_count < max_lines)) {
        char word[512];
        size_t word_len = 0;
        char candidate[1024];

        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }

        if (*cursor == '\n') {

            if (line[0]) {

                draw_text(renderer, font, line, rect.x, rect.y + line_count * line_height, color);
                line_count++;
                line[0] = '\0';

            }

            cursor++;
            continue;

        }

        while (cursor[word_len] && cursor[word_len] != ' ' && cursor[word_len] != '\t' && cursor[word_len] != '\n' &&
               word_len + 1 < sizeof(word)) {
            word[word_len] = cursor[word_len];
            word_len++;
        }

        word[word_len] = '\0';
        cursor += word_len;

        if (word[0] == '\0') {

            continue;

        }

        if (line[0]) {

            size_t line_length = strnlen(line, sizeof(candidate) - 1);
            size_t remaining;
            size_t copy_length;

            memcpy(candidate, line, line_length);
            candidate[line_length] = '\0';

            if (line_length + 1 < sizeof(candidate)) {

                candidate[line_length++] = ' ';
                candidate[line_length] = '\0';

            }

            remaining = sizeof(candidate) - line_length - 1;
            copy_length = strnlen(word, remaining);
            memcpy(candidate + line_length, word, copy_length);
            candidate[line_length + copy_length] = '\0';

        }

        else {

            snprintf(candidate, sizeof(candidate), "%s", word);

        }

        if (correlation_text_width(font, candidate) <= rect.w || line[0] == '\0') {

            snprintf(line, sizeof(line), "%s", candidate);

        }

        else {

            draw_text(renderer, font, line, rect.x, rect.y + line_count * line_height, color);
            line_count++;

            if (max_lines > 0 && line_count >= max_lines) {

                break;

            }

            snprintf(line, sizeof(line), "%s", word);

        }
    }

    if (line[0] && (max_lines <= 0 || line_count < max_lines)) {

        draw_text(renderer, font, line, rect.x, rect.y + line_count * line_height, color);
        line_count++;

    }

    return line_count;
}

static void correlation_draw_trend_line(SDL_Renderer *renderer, SDL_Rect rect,
                                        const float trend[CORRELATION_TREND_POINTS], SDL_Color color) {
    /*
        Purpose: Draws a normalized trend line without replacing the graph background
        Returns: No value
    */

    double maximum = 0.0;
    int point_count = Global_Correlation_Trend_Points;

    if (!renderer || !trend || rect.w < 2 || rect.h < 2 || point_count < 2) {

        return;

    }

    for (int i = 0; i < point_count; i++) {
        double absolute = fabs((double)trend[i]);

        if (absolute > maximum) {

            maximum = absolute;

        }
    }

    if (maximum < 1e-9) {

        maximum = 1.0;

    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int i = 1; i < point_count; i++) {
        int x0 = rect.x + ((i - 1) * (rect.w - 1)) / (point_count - 1);
        int x1 = rect.x + (i * (rect.w - 1)) / (point_count - 1);
        int y0 = rect.y + rect.h / 2 - (int)(((double)trend[i - 1] / maximum) * (double)(rect.h / 2 - 3));
        int y1 = rect.y + rect.h / 2 - (int)(((double)trend[i] / maximum) * (double)(rect.h / 2 - 3));

        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
    }
}

static void correlation_draw_trend_pair(SDL_Renderer *renderer, SDL_Rect rect,
                                        const float query[CORRELATION_TREND_POINTS], const float *selected,
                                        SDL_Color query_color, SDL_Color selected_color) {
    /*
        Purpose: Draws query and selected-result trends in one graph
        Returns: No value
    */

    draw_filled_rect(renderer, rect, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, rect, Correlation_Border);

    if (query) {

        correlation_draw_trend_line(renderer, rect, query, query_color);

    }

    if (selected) {

        correlation_draw_trend_line(renderer, rect, selected, selected_color);

    }
}

static void correlation_short_text(TTF_Font *font, const char *source, char *destination, size_t destination_size,
                                   int maximum_width) {
    /*
        Purpose: Truncates text to fit a pixel width
        Returns: No value
    */

    size_t length;

    if (!destination || destination_size == 0) {

        return;

    }

    snprintf(destination, destination_size, "%s", source ? source : "");

    if (!font || correlation_text_width(font, destination) <= maximum_width) {

        return;

    }

    length = strlen(destination);

    while (length > 3) {
        destination[length - 1] = '\0';
        length--;

        if (length + 4 < destination_size) {

            destination[length] = '.';
            destination[length + 1] = '.';
            destination[length + 2] = '.';
            destination[length + 3] = '\0';

        }

        if (correlation_text_width(font, destination) <= maximum_width) {

            return;

        }

        destination[length] = '\0';
    }
}

static SDL_Color correlation_interpolate_color(SDL_Color left, SDL_Color right, double amount) {
    /*
        Purpose: Interpolates between two result colors
        Returns: Interpolated color
    */

    amount = correlation_clamp_double(amount, 0.0, 1.0);

    return (SDL_Color){(Uint8)((double)left.r + ((double)right.r - (double)left.r) * amount),
                       (Uint8)((double)left.g + ((double)right.g - (double)left.g) * amount),
                       (Uint8)((double)left.b + ((double)right.b - (double)left.b) * amount), 255};
}

static SDL_Color correlation_score_color(double score) {
    /*
        Purpose: Produces the continuous red-yellow-blue-green score gradient
        Returns: Score-box color
    */

    SDL_Color red = {210, 45, 45, 255};
    SDL_Color yellow = {235, 200, 35, 255};
    SDL_Color blue = {35, 125, 225, 255};
    SDL_Color green = {30, 180, 85, 255};
    double percent = correlation_clamp_double(score, 0.0, 1.0) * 100.0;

    if (percent <= 50.0) {

        return red;

    }

    if (percent <= 70.0) {

        return correlation_interpolate_color(red, yellow, (percent - 50.0) / 20.0);

    }

    if (percent <= 90.0) {

        return correlation_interpolate_color(yellow, blue, (percent - 70.0) / 20.0);

    }

    return correlation_interpolate_color(blue, green, (percent - 90.0) / 10.0);
}

static char *correlation_active_weight_buffer(void) {
    /*
        Purpose: Returns the currently active Signature Engine input buffer
        Returns: Editable buffer or NULL
    */

    if (Global_Correlation_Active_Weight_Field == 1) {

        return Global_Correlation_Magnitude_Weight_Text;

    }

    if (Global_Correlation_Active_Weight_Field == 2) {

        return Global_Correlation_Frequency_Weight_Text;

    }

    if (Global_Correlation_Active_Weight_Field == 3) {

        return Global_Correlation_Bandwidth_Weight_Text;

    }

    if (Global_Correlation_Active_Weight_Field == 4) {

        return Global_Correlation_Trend_Points_Text;

    }

    return NULL;
}

static void correlation_sync_text_input(void) {
    /*
        Purpose: Synchronizes SDL text input with correlation text controls
        Returns: No value
    */

    if ((Global_Correlation_File_Search_Open && Global_Correlation_File_Search_Active) ||
        Global_Correlation_Active_Weight_Field != 0) {

        SDL_StartTextInput();

    }

    else {

        SDL_StopTextInput();

    }
}

static int correlation_parse_nonnegative_scalar(const char *text, double *value) {
    /*
        Purpose: Parses one finite nonnegative score scalar
        Returns: Parse status
    */

    char *end = NULL;
    double parsed;

    if (!text || !text[0] || !value) {

        return 0;

    }

    errno = 0;
    parsed = strtod(text, &end);

    if (errno != 0 || end == text || !isfinite(parsed) || parsed < 0.0) {

        return 0;

    }

    while (*end && isspace((unsigned char)*end)) {
        end++;
    }

    if (*end != '\0') {

        return 0;

    }

    *value = parsed;
    return 1;
}

static int correlation_parse_trend_points(const char *text, int *point_count) {
    /*
        Purpose: Parses and validates the adjustable trend resolution
        Returns: Parse status
    */

    char *end = NULL;
    long parsed;

    if (!text || !text[0] || !point_count) {

        return 0;

    }

    errno = 0;
    parsed = strtol(text, &end, 10);

    if (errno != 0 || end == text) {

        return 0;

    }

    while (*end && isspace((unsigned char)*end)) {
        end++;
    }

    if (*end != '\0' || parsed < CORRELATION_MIN_TREND_POINTS || parsed > CORRELATION_MAX_TREND_POINTS) {

        return 0;

    }

    *point_count = (int)parsed;
    return 1;
}

static void correlation_restore_weight_text(void) {
    /*
        Purpose: Restores Signature Engine fields from the last accepted values
        Returns: No value
    */

    snprintf(Global_Correlation_Magnitude_Weight_Text, sizeof(Global_Correlation_Magnitude_Weight_Text), "%.6g",
             Global_Correlation_Magnitude_Weight);
    snprintf(Global_Correlation_Frequency_Weight_Text, sizeof(Global_Correlation_Frequency_Weight_Text), "%.6g",
             Global_Correlation_Frequency_Weight);
    snprintf(Global_Correlation_Bandwidth_Weight_Text, sizeof(Global_Correlation_Bandwidth_Weight_Text), "%.6g",
             Global_Correlation_Bandwidth_Weight);
    snprintf(Global_Correlation_Trend_Points_Text, sizeof(Global_Correlation_Trend_Points_Text), "%d",
             Global_Correlation_Trend_Points);
}

static int correlation_apply_weight_inputs(void) {
    /*
        Purpose: Applies score scalars and trend resolution from the engine bar
        Returns: Apply status
    */

    double magnitude;
    double frequency;
    double bandwidth;
    double total;
    int trend_points;
    int resolution_changed;

    if (!correlation_parse_nonnegative_scalar(Global_Correlation_Magnitude_Weight_Text, &magnitude) ||
        !correlation_parse_nonnegative_scalar(Global_Correlation_Frequency_Weight_Text, &frequency) ||
        !correlation_parse_nonnegative_scalar(Global_Correlation_Bandwidth_Weight_Text, &bandwidth)) {

        correlation_restore_weight_text();
        correlation_set_status("Score scalars must be finite nonnegative numbers.");
        return 0;

    }

    if (!correlation_parse_trend_points(Global_Correlation_Trend_Points_Text, &trend_points)) {

        correlation_restore_weight_text();
        correlation_set_status("Trend resolution must be an integer from 32 to 1024 points.");
        return 0;

    }

    total = magnitude + frequency + bandwidth;

    if (total <= DBL_MIN) {

        correlation_restore_weight_text();
        correlation_set_status("At least one score scalar must be greater than zero.");
        return 0;

    }

    resolution_changed = trend_points != Global_Correlation_Trend_Points;
    Global_Correlation_Magnitude_Weight = magnitude;
    Global_Correlation_Frequency_Weight = frequency;
    Global_Correlation_Bandwidth_Weight = bandwidth;
    Global_Correlation_Trend_Points = trend_points;

    if (resolution_changed) {

        /*
            Signatures at different resolutions cannot be compared directly.
            Drop only the in-memory metadata so the selected resolution is rebuilt safely.
        */
        Global_Correlation_Cache_Count = 0;
        Global_Correlation_Result_Count = 0;
        Global_Correlation_Query_Signature_Valid = 0;
        Global_Correlation_Selected_Result = -1;
        Global_Correlation_Result_Scroll = 0;
        snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                 "Trend resolution set to %d points. Cached signatures will be rebuilt on the next comparison.",
                 trend_points);

    }

    else {

        correlation_rescore_results();
        snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                 "Score scalars applied: magnitude %.1f%%, frequency %.1f%%, bandwidth %.1f%%.",
                 100.0 * magnitude / total, 100.0 * frequency / total, 100.0 * bandwidth / total);

    }

    return 1;
}

static void correlation_weight_insert_text(const char *text) {
    /*
        Purpose: Inserts validated numeric text into the active engine field
        Returns: No value
    */

    char *buffer = correlation_active_weight_buffer();
    char filtered[CORRELATION_WEIGHT_TEXT_MAX];
    int filtered_count = 0;
    int length;
    int add;
    int have_decimal;
    int integer_only = Global_Correlation_Active_Weight_Field == 4;

    if (!buffer || !text) {

        return;

    }

    if (Global_Correlation_Weight_Replace_On_Type) {

        buffer[0] = '\0';
        Global_Correlation_Weight_Cursor = 0;
        Global_Correlation_Weight_Replace_On_Type = 0;

    }

    have_decimal = strchr(buffer, '.') != NULL;

    for (int i = 0; text[i] && filtered_count + 1 < (int)sizeof(filtered); i++) {

        if (isdigit((unsigned char)text[i])) {

            filtered[filtered_count++] = text[i];

        }

        else if (!integer_only && text[i] == '.' && !have_decimal) {

            filtered[filtered_count++] = text[i];
            have_decimal = 1;

        }
    }

    filtered[filtered_count] = '\0';
    length = (int)strlen(buffer);
    add = filtered_count;

    if (Global_Correlation_Weight_Cursor < 0) {

        Global_Correlation_Weight_Cursor = 0;

    }

    if (Global_Correlation_Weight_Cursor > length) {

        Global_Correlation_Weight_Cursor = length;

    }

    if (length + add >= CORRELATION_WEIGHT_TEXT_MAX) {

        add = CORRELATION_WEIGHT_TEXT_MAX - length - 1;

    }

    if (add <= 0) {

        return;

    }

    memmove(buffer + Global_Correlation_Weight_Cursor + add, buffer + Global_Correlation_Weight_Cursor,
            (size_t)(length - Global_Correlation_Weight_Cursor + 1));
    memcpy(buffer + Global_Correlation_Weight_Cursor, filtered, (size_t)add);
    Global_Correlation_Weight_Cursor += add;
}

static void correlation_weight_backspace(void) {
    /*
        Purpose: Removes the character before the engine-field cursor
        Returns: No value
    */

    char *buffer = correlation_active_weight_buffer();
    int length;

    if (!buffer) {

        return;

    }

    length = (int)strlen(buffer);

    if (Global_Correlation_Weight_Replace_On_Type) {

        buffer[0] = '\0';
        Global_Correlation_Weight_Cursor = 0;
        Global_Correlation_Weight_Replace_On_Type = 0;
        return;

    }

    if (Global_Correlation_Weight_Cursor <= 0 || length <= 0) {

        return;

    }

    memmove(buffer + Global_Correlation_Weight_Cursor - 1, buffer + Global_Correlation_Weight_Cursor,
            (size_t)(length - Global_Correlation_Weight_Cursor + 1));
    Global_Correlation_Weight_Cursor--;
}

static void correlation_weight_delete(void) {
    /*
        Purpose: Removes the character at the engine-field cursor
        Returns: No value
    */

    char *buffer = correlation_active_weight_buffer();
    int length;

    if (!buffer) {

        return;

    }

    length = (int)strlen(buffer);

    if (Global_Correlation_Weight_Replace_On_Type) {

        buffer[0] = '\0';
        Global_Correlation_Weight_Cursor = 0;
        Global_Correlation_Weight_Replace_On_Type = 0;
        return;

    }

    if (Global_Correlation_Weight_Cursor < 0 || Global_Correlation_Weight_Cursor >= length) {

        return;

    }

    memmove(buffer + Global_Correlation_Weight_Cursor, buffer + Global_Correlation_Weight_Cursor + 1,
            (size_t)(length - Global_Correlation_Weight_Cursor));
}

static void correlation_activate_weight_field(int field) {
    /*
        Purpose: Activates one Signature Engine text box
        Returns: No value
    */

    char *buffer;

    if (field < 1 || field > 4 || Global_Correlation_Working || Global_Correlation_Clear_Confirm) {

        return;

    }

    if (Global_Correlation_Active_Weight_Field != 0 && Global_Correlation_Active_Weight_Field != field) {

        (void)correlation_apply_weight_inputs();

    }

    Global_Correlation_Active_Weight_Field = field;
    buffer = correlation_active_weight_buffer();
    Global_Correlation_Weight_Cursor = buffer ? (int)strlen(buffer) : 0;
    Global_Correlation_Weight_Replace_On_Type = 1;
    correlation_sync_text_input();
}

static int correlation_file_search_matches(const char *name) {
    /*
        Purpose: Checks whether a recording matches the search text
        Returns: Boolean status
    */

    char hay[CORRELATION_MAX_NAME];
    char needle[CORRELATION_FILE_SEARCH_TEXT_MAX];
    size_t i;

    if (!name) {

        name = "";

    }

    if (Global_Correlation_File_Search_Text[0] == '\0') {

        return 1;

    }

    for (i = 0; i + 1 < sizeof(hay) && name[i]; i++) {
        hay[i] = (char)tolower((unsigned char)name[i]);
    }

    hay[i] = '\0';

    for (i = 0; i + 1 < sizeof(needle) && Global_Correlation_File_Search_Text[i]; i++) {
        needle[i] = (char)tolower((unsigned char)Global_Correlation_File_Search_Text[i]);
    }

    needle[i] = '\0';
    return strstr(hay, needle) != NULL;
}

static int correlation_file_search_filtered_count(void) {
    /*
        Purpose: Counts recordings matching the file search
        Returns: Match count
    */

    int count = 0;

    for (int i = 0; i < Global_Correlation_File_Count; i++) {

        if (correlation_file_search_matches(Global_Correlation_Files[i])) {

            count++;

        }
    }

    return count;
}

static int correlation_file_search_filtered_index_at(int filtered_index) {
    /*
        Purpose: Maps a filtered search row to the recording index
        Returns: Recording index or -1
    */

    int seen = 0;

    if (filtered_index < 0) {

        return -1;

    }

    for (int i = 0; i < Global_Correlation_File_Count; i++) {

        if (!correlation_file_search_matches(Global_Correlation_Files[i])) {

            continue;

        }

        if (seen == filtered_index) {

            return i;

        }

        seen++;
    }

    return -1;
}

static SDL_Rect correlation_file_search_popup_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the centered filename-search popup
        Returns: Popup rectangle
    */

    SDL_Rect rect = {(win_w - 1020) / 2, (win_h - 700) / 2, 1020, 700};

    if (rect.x < CORRELATION_PANEL_MARGIN) {

        rect.x = CORRELATION_PANEL_MARGIN;

    }

    if (rect.y < CORRELATION_PANEL_MARGIN) {

        rect.y = CORRELATION_PANEL_MARGIN;

    }

    if (rect.w > win_w - 2 * CORRELATION_PANEL_MARGIN) {

        rect.w = win_w - 2 * CORRELATION_PANEL_MARGIN;

    }

    if (rect.h > win_h - 2 * CORRELATION_PANEL_MARGIN) {

        rect.h = win_h - 2 * CORRELATION_PANEL_MARGIN;

    }

    if (rect.w < 320) {

        rect.w = 320;

    }

    if (rect.h < 260) {

        rect.h = 260;

    }

    return rect;
}

static SDL_Rect correlation_file_search_input_rect(SDL_Rect popup) {
    /*
        Purpose: Computes the filename-search text box
        Returns: Input rectangle
    */

    SDL_Rect close_button = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect input = {close_button.x - 292, popup.y + 14, 276, 30};

    if (input.x < popup.x + 180) {

        input.x = popup.x + 180;
        input.w = close_button.x - input.x - 16;

    }

    if (input.w < 120) {

        input.w = 120;

    }

    return input;
}

static void correlation_file_search_clamp_scroll(void) {
    /*
        Purpose: Clamps the filename-search scroll position
        Returns: No value
    */

    int maximum = correlation_file_search_filtered_count() - 14;

    if (maximum < 0) {

        maximum = 0;

    }

    if (Global_Correlation_File_Search_Scroll < 0) {

        Global_Correlation_File_Search_Scroll = 0;

    }

    if (Global_Correlation_File_Search_Scroll > maximum) {

        Global_Correlation_File_Search_Scroll = maximum;

    }
}

static void correlation_open_file_search(void) {
    /*
        Purpose: Opens the filename-search menu
        Returns: No value
    */

    if (Global_Correlation_File_Count <= 0) {

        correlation_scan_recordings();

    }

    Global_Correlation_File_Search_Open = 1;
    Global_Correlation_File_Search_Active = 1;
    Global_Correlation_File_Search_Cursor = 0;
    Global_Correlation_File_Search_Scroll = 0;
    Global_Correlation_File_Search_Hover = -1;
    Global_Correlation_File_Search_Text[0] = '\0';
    Global_Correlation_Active_Weight_Field = 0;
    correlation_sync_text_input();
    correlation_set_status("Filename search menu opened.");
}

static void correlation_close_file_search(void) {
    /*
        Purpose: Closes the filename-search menu
        Returns: No value
    */

    Global_Correlation_File_Search_Open = 0;
    Global_Correlation_File_Search_Active = 0;
    Global_Correlation_File_Search_Hover = -1;
    correlation_sync_text_input();
}

static void correlation_select_recording(int index) {
    /*
        Purpose: Selects a query recording and clears stale comparison results
        Returns: No value
    */

    if (index < 0 || index >= Global_Correlation_File_Count || Global_Correlation_Working) {

        return;

    }

    Global_Correlation_Selected_File = index;
    Global_Correlation_Result_Count = 0;
    Global_Correlation_Query_Signature_Valid = 0;
    Global_Correlation_Selected_Result = -1;
    Global_Correlation_Result_Scroll = 0;
    Global_Correlation_Clear_Confirm = 0;
    snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
             "Selected %.200s. Press Compare All to rank matching transmissions.", Global_Correlation_Files[index]);
}

static void correlation_file_search_insert_text(const char *text) {
    /*
        Purpose: Inserts text into the filename-search input
        Returns: No value
    */

    int length;
    int add;

    if (!text || !text[0]) {

        return;

    }

    length = (int)strlen(Global_Correlation_File_Search_Text);
    add = (int)strlen(text);

    if (Global_Correlation_File_Search_Cursor < 0) {

        Global_Correlation_File_Search_Cursor = 0;

    }

    if (Global_Correlation_File_Search_Cursor > length) {

        Global_Correlation_File_Search_Cursor = length;

    }

    if (length + add >= CORRELATION_FILE_SEARCH_TEXT_MAX) {

        add = CORRELATION_FILE_SEARCH_TEXT_MAX - length - 1;

    }

    if (add <= 0) {

        return;

    }

    memmove(Global_Correlation_File_Search_Text + Global_Correlation_File_Search_Cursor + add,
            Global_Correlation_File_Search_Text + Global_Correlation_File_Search_Cursor,
            (size_t)(length - Global_Correlation_File_Search_Cursor + 1));
    memcpy(Global_Correlation_File_Search_Text + Global_Correlation_File_Search_Cursor, text, (size_t)add);
    Global_Correlation_File_Search_Cursor += add;
    Global_Correlation_File_Search_Scroll = 0;
}

static void correlation_file_search_backspace(void) {
    /*
        Purpose: Removes the previous filename-search character
        Returns: No value
    */

    int length = (int)strlen(Global_Correlation_File_Search_Text);

    if (Global_Correlation_File_Search_Cursor <= 0 || length <= 0) {

        return;

    }

    memmove(Global_Correlation_File_Search_Text + Global_Correlation_File_Search_Cursor - 1,
            Global_Correlation_File_Search_Text + Global_Correlation_File_Search_Cursor,
            (size_t)(length - Global_Correlation_File_Search_Cursor + 1));
    Global_Correlation_File_Search_Cursor--;
    Global_Correlation_File_Search_Scroll = 0;
}

static void correlation_file_search_delete(void) {
    /*
        Purpose: Deletes the filename-search character at the cursor
        Returns: No value
    */

    int length = (int)strlen(Global_Correlation_File_Search_Text);

    if (Global_Correlation_File_Search_Cursor < 0 || Global_Correlation_File_Search_Cursor >= length) {

        return;

    }

    memmove(Global_Correlation_File_Search_Text + Global_Correlation_File_Search_Cursor,
            Global_Correlation_File_Search_Text + Global_Correlation_File_Search_Cursor + 1,
            (size_t)(length - Global_Correlation_File_Search_Cursor));
    Global_Correlation_File_Search_Scroll = 0;
}

static int correlation_handle_file_search_event(const SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the modal filename-search menu
        Returns: Handling status
    */

    SDL_Rect popup;
    SDL_Rect close_button;
    SDL_Rect search;
    SDL_Rect list;

    if (!event || !Global_Correlation_File_Search_Open) {

        return 0;

    }

    popup = correlation_file_search_popup_rect(win_w, win_h);
    close_button = (SDL_Rect){popup.x + popup.w - 86, popup.y + 14, 68, 30};
    search = correlation_file_search_input_rect(popup);
    list = (SDL_Rect){popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};

    if (event->type == SDL_TEXTINPUT && Global_Correlation_File_Search_Active) {

        correlation_file_search_insert_text(event->text.text);
        correlation_file_search_clamp_scroll();
        return 1;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;
        int length = (int)strlen(Global_Correlation_File_Search_Text);

        if (key == SDLK_ESCAPE) {

            correlation_close_file_search();
            return 1;

        }

        if (key == SDLK_BACKSPACE && Global_Correlation_File_Search_Active) {

            correlation_file_search_backspace();
            correlation_file_search_clamp_scroll();
            return 1;

        }

        if (key == SDLK_DELETE && Global_Correlation_File_Search_Active) {

            correlation_file_search_delete();
            correlation_file_search_clamp_scroll();
            return 1;

        }

        if (key == SDLK_LEFT && Global_Correlation_File_Search_Cursor > 0) {

            Global_Correlation_File_Search_Cursor--;
            return 1;

        }

        if (key == SDLK_RIGHT && Global_Correlation_File_Search_Cursor < length) {

            Global_Correlation_File_Search_Cursor++;
            return 1;

        }

        if (key == SDLK_HOME) {

            Global_Correlation_File_Search_Cursor = 0;
            return 1;

        }

        if (key == SDLK_END) {

            Global_Correlation_File_Search_Cursor = length;
            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            int index = correlation_file_search_filtered_index_at(Global_Correlation_File_Search_Scroll);

            if (index >= 0) {

                correlation_select_recording(index);
                correlation_close_file_search();

            }

            return 1;

        }

        if (key == SDLK_DOWN) {

            Global_Correlation_File_Search_Scroll++;
            correlation_file_search_clamp_scroll();
            return 1;

        }

        if (key == SDLK_UP) {

            Global_Correlation_File_Search_Scroll--;
            correlation_file_search_clamp_scroll();
            return 1;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mouse_x = 0;
        int mouse_y = 0;

        SDL_GetMouseState(&mouse_x, &mouse_y);
        mouse_y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

        if (correlation_point_in_rect(mouse_x, mouse_y, list)) {

            Global_Correlation_File_Search_Scroll -= event->wheel.y * 3;
            correlation_file_search_clamp_scroll();

        }

        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        int mouse_x = event->button.x;
        int mouse_y = event->button.y;

        if (!correlation_point_in_rect(mouse_x, mouse_y, popup) ||
            correlation_point_in_rect(mouse_x, mouse_y, close_button)) {

            correlation_close_file_search();
            return 1;

        }

        if (correlation_point_in_rect(mouse_x, mouse_y, search)) {

            Global_Correlation_File_Search_Active = 1;
            correlation_sync_text_input();
            return 1;

        }

        Global_Correlation_File_Search_Active = 0;
        correlation_sync_text_input();

        if (correlation_point_in_rect(mouse_x, mouse_y, list)) {

            int row = (mouse_y - list.y - 4) / CORRELATION_FILE_SEARCH_ROW_H;
            int visible = list.h / CORRELATION_FILE_SEARCH_ROW_H;

            if (visible > 14) {

                visible = 14;

            }

            if (row >= 0 && row < visible) {

                int filtered_index = Global_Correlation_File_Search_Scroll + row;
                int index = correlation_file_search_filtered_index_at(filtered_index);

                if (index >= 0) {

                    correlation_select_recording(index);
                    correlation_close_file_search();

                }

            }

            return 1;

        }

        return 1;

    }

    return 1;
}

static void correlation_draw_file_search_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the Analysis-style filename-search popup
        Returns: No value
    */

    SDL_Rect popup;
    SDL_Rect close_button;
    SDL_Rect search;
    SDL_Rect current_rect;
    SDL_Rect list;
    int mouse_x = 0;
    int mouse_y = 0;
    int filtered_count;
    int visible;

    if (!renderer || !font || !Global_Correlation_File_Search_Open) {

        return;

    }

    popup = correlation_file_search_popup_rect(win_w, win_h);
    close_button = (SDL_Rect){popup.x + popup.w - 86, popup.y + 14, 68, 30};
    search = correlation_file_search_input_rect(popup);
    current_rect = (SDL_Rect){popup.x + 18, popup.y + 62, popup.w - 36, 42};
    list = (SDL_Rect){popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};
    filtered_count = correlation_file_search_filtered_count();

    SDL_GetMouseState(&mouse_x, &mouse_y);
    mouse_y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;
    correlation_file_search_clamp_scroll();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, (SDL_Rect){0, 0, win_w, win_h}, (SDL_Color){0, 0, 0, 165});
    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, Correlation_Border_Hi);
    draw_outline_rect(renderer, (SDL_Rect){popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8}, Correlation_Border);
    draw_text(renderer, font, "FILENAME SEARCH", popup.x + 18, popup.y + 20, Correlation_Text);
    correlation_draw_button(renderer, font, close_button, "Close",
                            correlation_point_in_rect(mouse_x, mouse_y, close_button), 0);

    draw_filled_rect(renderer, search,
                     Global_Correlation_File_Search_Active ? (SDL_Color){0, 20, 8, 255} : (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, search,
                      Global_Correlation_File_Search_Active ? Correlation_Border_Hi : Correlation_Border);

    if (Global_Correlation_File_Search_Text[0]) {

        draw_text(renderer, font, Global_Correlation_File_Search_Text, search.x + 10, search.y + 8, Correlation_Text);

    }

    else {

        draw_text(renderer, font, "Search file", search.x + 10, search.y + 8, Correlation_Muted);

    }

    if (Global_Correlation_File_Search_Active && ((SDL_GetTicks64() / 450ULL) % 2ULL) == 0ULL) {

        char prefix[CORRELATION_FILE_SEARCH_TEXT_MAX];
        int width = 0;
        int height = 0;
        int cursor = Global_Correlation_File_Search_Cursor;
        int length = (int)strlen(Global_Correlation_File_Search_Text);

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > length) {

            cursor = length;

        }

        snprintf(prefix, sizeof(prefix), "%.*s", cursor, Global_Correlation_File_Search_Text);
        (void)TTF_SizeText(font, prefix, &width, &height);
        SDL_SetRenderDrawColor(renderer, Correlation_Blue.r, Correlation_Blue.g, Correlation_Blue.b, 255);
        SDL_RenderDrawLine(renderer, search.x + 10 + width, search.y + 6, search.x + 10 + width,
                           search.y + search.h - 6);

    }

    draw_text(renderer, font, "Currently selected", current_rect.x, current_rect.y - 18, Correlation_Muted);
    draw_filled_rect(renderer, current_rect, (SDL_Color){0, 20, 8, 255});
    draw_outline_rect(renderer, current_rect, Correlation_Border_Hi);

    {
        char short_name[CORRELATION_MAX_NAME];
        const char *current =
            Global_Correlation_Selected_File >= 0 && Global_Correlation_Selected_File < Global_Correlation_File_Count
                ? Global_Correlation_Files[Global_Correlation_Selected_File]
                : "(none selected)";

        correlation_short_text(font, current, short_name, sizeof(short_name), current_rect.w - 20);
        draw_text(renderer, font, short_name, current_rect.x + 10, current_rect.y + 12,
                  current[0] == '(' ? Correlation_Muted : Correlation_Text);
    }

    draw_filled_rect(renderer, list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, list, Correlation_Border);

    if (Global_Correlation_File_Count <= 0) {

        draw_text(renderer, font, "No .complex16 recordings found.", list.x + 12, list.y + 14, Correlation_Warn);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    if (filtered_count <= 0) {

        draw_text(renderer, font, "No files match the search.", list.x + 12, list.y + 14, Correlation_Warn);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    visible = list.h / CORRELATION_FILE_SEARCH_ROW_H;

    if (visible > 14) {

        visible = 14;

    }

    if (visible < 1) {

        visible = 1;

    }

    Global_Correlation_File_Search_Hover = -1;

    if (correlation_point_in_rect(mouse_x, mouse_y, list)) {

        int row = (mouse_y - list.y - 4) / CORRELATION_FILE_SEARCH_ROW_H;
        int index = correlation_file_search_filtered_index_at(Global_Correlation_File_Search_Scroll + row);

        if (row >= 0 && row < visible && index >= 0) {

            Global_Correlation_File_Search_Hover = index;

        }

    }

    for (int row = 0; row < visible; row++) {
        int index = correlation_file_search_filtered_index_at(Global_Correlation_File_Search_Scroll + row);
        SDL_Rect item = {list.x + 4, list.y + 4 + row * CORRELATION_FILE_SEARCH_ROW_H, list.w - 8,
                         CORRELATION_FILE_SEARCH_ROW_H - 3};
        int hovered;
        int selected;
        char short_name[CORRELATION_MAX_NAME];

        if (index < 0 || index >= Global_Correlation_File_Count) {

            break;

        }

        hovered = index == Global_Correlation_File_Search_Hover;
        selected = index == Global_Correlation_Selected_File;

        if (hovered) {

            draw_filled_rect(renderer, item, (SDL_Color){0, 44, 16, 255});

        }

        else if (selected) {

            draw_filled_rect(renderer, item, (SDL_Color){15, 85, 45, 245});

        }

        draw_outline_rect(renderer, item, hovered || selected ? Correlation_Border_Hi : Correlation_Border);
        correlation_short_text(font, Global_Correlation_Files[index], short_name, sizeof(short_name), item.w - 20);
        draw_text(renderer, font, short_name, item.x + 10, item.y + 8,
                  hovered || selected ? Correlation_Text : Correlation_Muted);
    }

    {
        char count_text[96];

        snprintf(count_text, sizeof(count_text), Global_Correlation_File_Search_Text[0] ? "%d of %d files" : "%d files",
                 filtered_count, Global_Correlation_File_Count);
        draw_text(renderer, font, count_text, popup.x + 18, popup.y + popup.h - 24, Correlation_Muted);
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void CORRELATION_enter_mode(const char *record_dir, unsigned long long fallback_center_hz,
                            unsigned int fallback_record_rate_hz, unsigned int fallback_sample_rate_hz) {
    /*
        Purpose: Shows the correlation workstation without blocking the UI thread
        Returns: No value
    */

    int directory_changed = 0;

    Global_Correlation_Mode = 1;
    Global_Correlation_Background_Throttle = 0;
    Global_Correlation_Clear_Confirm = 0;
    Global_Correlation_Active_Weight_Field = 0;
    Global_Correlation_File_Search_Open = 0;
    Global_Correlation_File_Search_Active = 0;
    Global_Correlation_Fallback_Center_Hz = (uint64_t)fallback_center_hz;
    Global_Correlation_Fallback_Record_Rate_Hz = fallback_record_rate_hz;
    Global_Correlation_Fallback_Sample_Rate_Hz = fallback_sample_rate_hz;

    if (record_dir && record_dir[0] && strcmp(Global_Correlation_Record_Dir, record_dir) != 0) {

        directory_changed = 1;
        snprintf(Global_Correlation_Record_Dir, sizeof(Global_Correlation_Record_Dir), "%s", record_dir);

    }

    if (directory_changed && !Global_Correlation_Working) {

        Global_Correlation_Initialized = 0;
        Global_Correlation_Server_Cache_Attempted = 0;
        Global_Correlation_Cache_Count = 0;
        Global_Correlation_Result_Count = 0;
        Global_Correlation_Query_Signature_Valid = 0;

    }

    if (Global_Correlation_Working) {

        correlation_sync_text_input();
        return;

    }

    /*
     * Initialize once per recording directory. Server cache access is deferred
     * to the worker so opening this tab never waits on a network round trip.
     */

    if (!Global_Correlation_Initialized) {

        Global_Correlation_Cache_Sync_Error[0] = '\0';
        correlation_load_local_cache();
        correlation_scan_recordings();
        correlation_prune_cache();
        Global_Correlation_Initialized = 1;

    }

    correlation_sync_text_input();
}

void CORRELATION_exit_mode(void) {
    /*
        Purpose: Hides the correlation workstation without stopping Compare All
        Returns: No value
    */

    Global_Correlation_Mode = 0;
    Global_Correlation_Background_Throttle = 1;
    Global_Correlation_Active_Weight_Field = 0;
    Global_Correlation_File_Search_Open = 0;
    Global_Correlation_File_Search_Active = 0;
    Global_Correlation_Clear_Confirm = 0;
    correlation_sync_text_input();
}

void CORRELATION_shutdown(void) {
    /*
        Purpose: Stops and joins the background worker during logout or application shutdown
        Returns: No value
    */

    Global_Correlation_Mode = 0;
    Global_Correlation_Background_Throttle = 0;
    Global_Correlation_Active_Weight_Field = 0;
    Global_Correlation_File_Search_Open = 0;
    Global_Correlation_File_Search_Active = 0;
    Global_Correlation_Clear_Confirm = 0;
    correlation_sync_text_input();

    if (Global_Correlation_Working) {

        Global_Correlation_Cancel = 1;

    }

    if (Global_Correlation_Thread) {

        SDL_WaitThread(Global_Correlation_Thread, NULL);
        Global_Correlation_Thread = NULL;

    }

    Global_Correlation_Working = 0;
    Global_Correlation_Cancel = 0;
    Global_Correlation_Initialized = 0;
    Global_Correlation_Server_Cache_Attempted = 0;
    Global_Correlation_Cache_Count = 0;
    Global_Correlation_Result_Count = 0;
    Global_Correlation_Query_Signature_Valid = 0;
}

int CORRELATION_is_text_entry_active(void) {
    /*
        Purpose: Reports whether correlation owns text input
        Returns: Boolean status
    */

    return (Global_Correlation_File_Search_Open && Global_Correlation_File_Search_Active) ||
           Global_Correlation_Active_Weight_Field != 0;
}

int CORRELATION_handle_event(const SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles correlation workstation input
        Returns: Handling status
    */

    SDL_Rect status;
    SDL_Rect engine;
    SDL_Rect recordings;
    SDL_Rect results;
    SDL_Rect graphs;
    SDL_Rect file_list;
    SDL_Rect search_button;
    SDL_Rect magnitude_box;
    SDL_Rect frequency_box;
    SDL_Rect bandwidth_box;
    SDL_Rect resolution_box;
    SDL_Rect compare_button;
    SDL_Rect stop_button;
    SDL_Rect rescan_button;
    SDL_Rect clear_button;
    SDL_Rect clear_dialog;
    SDL_Rect clear_confirm_button;
    SDL_Rect clear_cancel_button;
    int visible_file_rows;
    int visible_result_rows;

    if (!event || !Global_Correlation_Mode) {

        return 0;

    }

    if (Global_Correlation_File_Search_Open) {

        return correlation_handle_file_search_event(event, win_w, win_h);

    }

    correlation_get_layout(win_w, win_h, &status, &engine, &recordings, &results, &graphs);
    (void)status;
    (void)graphs;
    file_list = (SDL_Rect){recordings.x + 8, recordings.y + 44, recordings.w - 16, recordings.h - 52};
    search_button = (SDL_Rect){recordings.x + recordings.w - 178, recordings.y + 8, 166, 28};
    correlation_get_engine_controls(engine, &magnitude_box, &frequency_box, &bandwidth_box, &resolution_box,
                                    &compare_button, &stop_button, &rescan_button, &clear_button);
    visible_file_rows = file_list.h / CORRELATION_LIST_ROW_H;
    visible_result_rows = (results.h - 54) / CORRELATION_RESULT_ROW_H;

    if (visible_file_rows < 1) {

        visible_file_rows = 1;

    }

    if (visible_result_rows < 1) {

        visible_result_rows = 1;

    }

    correlation_get_clear_cache_dialog(win_w, win_h, &clear_dialog, &clear_confirm_button, &clear_cancel_button);

    if (Global_Correlation_Clear_Confirm) {

        if (event->type == SDL_KEYDOWN) {

            SDL_Keycode key = event->key.keysym.sym;

            if (key == SDLK_ESCAPE) {

                Global_Correlation_Clear_Confirm = 0;
                correlation_set_status("Cache clearing cancelled.");
                return 1;

            }

            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

                correlation_clear_cache();
                Global_Correlation_Clear_Confirm = 0;
                return 1;

            }

        }

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

            int x = event->button.x;
            int y = event->button.y;

            if (correlation_point_in_rect(x, y, clear_confirm_button)) {

                correlation_clear_cache();
                Global_Correlation_Clear_Confirm = 0;
                return 1;

            }

            if (correlation_point_in_rect(x, y, clear_cancel_button)) {

                Global_Correlation_Clear_Confirm = 0;
                correlation_set_status("Cache clearing cancelled.");
                return 1;

            }

            return 1;

        }

        return 1;

    }

    if (event->type == SDL_TEXTINPUT && Global_Correlation_Active_Weight_Field != 0) {

        correlation_weight_insert_text(event->text.text);
        return 1;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (Global_Correlation_Active_Weight_Field != 0) {

            char *buffer = correlation_active_weight_buffer();
            int length = buffer ? (int)strlen(buffer) : 0;

            if (key == SDLK_ESCAPE) {

                correlation_restore_weight_text();
                Global_Correlation_Active_Weight_Field = 0;
                Global_Correlation_Weight_Replace_On_Type = 0;
                correlation_sync_text_input();
                return 1;

            }

            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

                (void)correlation_apply_weight_inputs();
                Global_Correlation_Active_Weight_Field = 0;
                Global_Correlation_Weight_Replace_On_Type = 0;
                correlation_sync_text_input();
                return 1;

            }

            if (key == SDLK_TAB) {

                int next = Global_Correlation_Active_Weight_Field % 4 + 1;

                (void)correlation_apply_weight_inputs();
                Global_Correlation_Active_Weight_Field = 0;
                Global_Correlation_Weight_Replace_On_Type = 0;
                correlation_activate_weight_field(next);
                return 1;

            }

            if (key == SDLK_BACKSPACE) {

                correlation_weight_backspace();
                return 1;

            }

            if (key == SDLK_DELETE) {

                correlation_weight_delete();
                return 1;

            }

            if (key == SDLK_LEFT && Global_Correlation_Weight_Cursor > 0) {

                Global_Correlation_Weight_Replace_On_Type = 0;
                Global_Correlation_Weight_Cursor--;
                return 1;

            }

            if (key == SDLK_RIGHT && Global_Correlation_Weight_Cursor < length) {

                Global_Correlation_Weight_Replace_On_Type = 0;
                Global_Correlation_Weight_Cursor++;
                return 1;

            }

            if (key == SDLK_HOME) {

                Global_Correlation_Weight_Replace_On_Type = 0;
                Global_Correlation_Weight_Cursor = 0;
                return 1;

            }

            if (key == SDLK_END) {

                Global_Correlation_Weight_Replace_On_Type = 0;
                Global_Correlation_Weight_Cursor = length;
                return 1;

            }

            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            correlation_start_worker();
            return 1;

        }

        if (key == SDLK_r && !Global_Correlation_Working) {

            correlation_scan_recordings();
            Global_Correlation_Clear_Confirm = 0;
            return 1;

        }

        if (key == SDLK_f && !Global_Correlation_Working) {

            correlation_open_file_search();
            return 1;

        }

        if (key == SDLK_UP && Global_Correlation_File_Count > 0 && !Global_Correlation_Working) {

            Global_Correlation_Selected_File--;

            if (Global_Correlation_Selected_File < 0) {

                Global_Correlation_Selected_File = Global_Correlation_File_Count - 1;

            }

            if (Global_Correlation_Selected_File < Global_Correlation_File_Scroll) {

                Global_Correlation_File_Scroll = Global_Correlation_Selected_File;

            }

            return 1;

        }

        if (key == SDLK_DOWN && Global_Correlation_File_Count > 0 && !Global_Correlation_Working) {

            Global_Correlation_Selected_File++;

            if (Global_Correlation_Selected_File >= Global_Correlation_File_Count) {

                Global_Correlation_Selected_File = 0;

            }

            if (Global_Correlation_Selected_File >= Global_Correlation_File_Scroll + visible_file_rows) {

                Global_Correlation_File_Scroll = Global_Correlation_Selected_File - visible_file_rows + 1;

            }

            return 1;

        }

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mouse_x = 0;
        int mouse_y = 0;

        SDL_GetMouseState(&mouse_x, &mouse_y);
        mouse_y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

        if (correlation_point_in_rect(mouse_x, mouse_y, file_list)) {

            Global_Correlation_File_Scroll -= event->wheel.y * 3;

            if (Global_Correlation_File_Scroll < 0) {

                Global_Correlation_File_Scroll = 0;

            }

            if (Global_Correlation_File_Scroll > Global_Correlation_File_Count - visible_file_rows) {

                Global_Correlation_File_Scroll = Global_Correlation_File_Count - visible_file_rows;

            }

            if (Global_Correlation_File_Scroll < 0) {

                Global_Correlation_File_Scroll = 0;

            }

            return 1;

        }

        if (correlation_point_in_rect(mouse_x, mouse_y, results)) {

            Global_Correlation_Result_Scroll -= event->wheel.y * 2;

            if (Global_Correlation_Result_Scroll < 0) {

                Global_Correlation_Result_Scroll = 0;

            }

            if (Global_Correlation_Result_Scroll > Global_Correlation_Result_Count - visible_result_rows) {

                Global_Correlation_Result_Scroll = Global_Correlation_Result_Count - visible_result_rows;

            }

            if (Global_Correlation_Result_Scroll < 0) {

                Global_Correlation_Result_Scroll = 0;

            }

            return 1;

        }

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        int x = event->button.x;
        int y = event->button.y;
        int clicked_weight = 0;

        if (correlation_point_in_rect(x, y, magnitude_box)) {

            clicked_weight = 1;

        }

        else if (correlation_point_in_rect(x, y, frequency_box)) {

            clicked_weight = 2;

        }

        else if (correlation_point_in_rect(x, y, bandwidth_box)) {

            clicked_weight = 3;

        }

        else if (correlation_point_in_rect(x, y, resolution_box)) {

            clicked_weight = 4;

        }

        if (clicked_weight != 0) {

            correlation_activate_weight_field(clicked_weight);
            return 1;

        }

        if (Global_Correlation_Active_Weight_Field != 0) {

            (void)correlation_apply_weight_inputs();
            Global_Correlation_Active_Weight_Field = 0;
            Global_Correlation_Weight_Replace_On_Type = 0;
            correlation_sync_text_input();

        }

        if (correlation_point_in_rect(x, y, compare_button) && !Global_Correlation_Working) {

            Global_Correlation_Clear_Confirm = 0;
            correlation_start_worker();
            return 1;

        }

        if (correlation_point_in_rect(x, y, stop_button) && Global_Correlation_Working && !Global_Correlation_Cancel) {

            Global_Correlation_Cancel = 1;
            correlation_set_status(
                "Stopping after the current metadata operation and saving the completed cache batch...");
            return 1;

        }

        if (correlation_point_in_rect(x, y, rescan_button) && !Global_Correlation_Working) {

            Global_Correlation_Clear_Confirm = 0;
            correlation_scan_recordings();
            return 1;

        }

        if (correlation_point_in_rect(x, y, clear_button) && !Global_Correlation_Working) {

            Global_Correlation_Active_Weight_Field = 0;
            Global_Correlation_Weight_Replace_On_Type = 0;
            Global_Correlation_Clear_Confirm = 1;
            correlation_sync_text_input();
            return 1;

        }

        if (correlation_point_in_rect(x, y, search_button) && !Global_Correlation_Working) {

            Global_Correlation_Clear_Confirm = 0;
            correlation_open_file_search();
            return 1;

        }

        if (correlation_point_in_rect(x, y, file_list) && !Global_Correlation_Working) {

            int row = (y - file_list.y - 4) / CORRELATION_LIST_ROW_H;
            int index = Global_Correlation_File_Scroll + row;

            if (index >= 0 && index < Global_Correlation_File_Count) {

                correlation_select_recording(index);

            }

            return 1;

        }

        if (correlation_point_in_rect(x, y, results)) {

            int row;
            int index;

            if (y < results.y + 48) {

                return 1;

            }

            row = (y - (results.y + 48)) / CORRELATION_RESULT_ROW_H;
            index = Global_Correlation_Result_Scroll + row;

            if (index >= 0 && index < Global_Correlation_Result_Count) {

                SDL_Rect row_rect = {results.x + 8, results.y + 48 + row * CORRELATION_RESULT_ROW_H, results.w - 16,
                                     CORRELATION_RESULT_ROW_H - 4};
                SDL_Rect export_button;
                SDL_Rect score_box;

                correlation_get_result_controls(row_rect, &export_button, &score_box);
                (void)score_box;
                Global_Correlation_Selected_Result = index;

                if (correlation_point_in_rect(x, y, export_button)) {

                    Type_Correlation_Result *result = &Global_Correlation_Results[index];
                    int workspace = ANALYSIS_get_recording_workspace(result->file_name);
                    char error[256] = "";

                    if (workspace > 0) {

                        snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                                 "%.180s is already loaded in Analysis workspace %d.", result->file_name, workspace);
                        return 1;

                    }

                    if (ANALYSIS_get_available_workspace_count() <= 0) {

                        correlation_set_status("All five Analysis workspaces are occupied. Clear one in Analysis "
                                               "before exporting another signal.");
                        return 1;

                    }

                    if (ANALYSIS_export_recording_to_workspace(
                            Global_Correlation_Record_Dir, result->file_name, Global_Correlation_Fallback_Center_Hz,
                            Global_Correlation_Fallback_Record_Rate_Hz, Global_Correlation_Fallback_Sample_Rate_Hz,
                            &workspace, error, sizeof(error))) {

                        snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                                 "Exported %.180s to Analysis workspace %d.", result->file_name, workspace);

                    }

                    else {

                        snprintf(Global_Correlation_Status, sizeof(Global_Correlation_Status),
                                 "Analysis export failed: %.220s", error[0] ? error : "unknown error");

                    }

                    return 1;

                }

            }

            return 1;

        }

    }

    return 0;
}

void CORRELATION_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the correlation workstation
        Returns: No value
    */

    SDL_Rect status;
    SDL_Rect engine;
    SDL_Rect recordings;
    SDL_Rect results;
    SDL_Rect graphs;
    SDL_Rect file_list;
    SDL_Rect search_button;
    SDL_Rect magnitude_box;
    SDL_Rect frequency_box;
    SDL_Rect bandwidth_box;
    SDL_Rect resolution_box;
    SDL_Rect compare_button;
    SDL_Rect stop_button;
    SDL_Rect rescan_button;
    SDL_Rect clear_button;
    SDL_Rect clear_dialog;
    SDL_Rect clear_confirm_button;
    SDL_Rect clear_cancel_button;
    int mouse_x = 0;
    int mouse_y = 0;
    int visible_file_rows;
    int visible_result_rows;

    if (!renderer || !font) {

        return;

    }

    correlation_get_layout(win_w, win_h, &status, &engine, &recordings, &results, &graphs);
    file_list = (SDL_Rect){recordings.x + 8, recordings.y + 44, recordings.w - 16, recordings.h - 52};
    search_button = (SDL_Rect){recordings.x + recordings.w - 178, recordings.y + 8, 166, 28};
    correlation_get_engine_controls(engine, &magnitude_box, &frequency_box, &bandwidth_box, &resolution_box,
                                    &compare_button, &stop_button, &rescan_button, &clear_button);
    correlation_get_clear_cache_dialog(win_w, win_h, &clear_dialog, &clear_confirm_button, &clear_cancel_button);
    (void)status;

    SDL_GetMouseState(&mouse_x, &mouse_y);
    mouse_y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

    SDL_SetRenderDrawColor(renderer, Correlation_BG.r, Correlation_BG.g, Correlation_BG.b, Correlation_BG.a);
    SDL_RenderClear(renderer);

    draw_filled_rect(renderer, engine, Correlation_Panel);
    draw_outline_rect(renderer, engine, Correlation_Border);
    draw_text(renderer, font, "SIGNATURE ENGINE", engine.x + 12, engine.y + 12, Correlation_Text);

    {
        char short_status[512];

        correlation_short_text(font, Global_Correlation_Status, short_status, sizeof(short_status), engine.w - 190);
        draw_text(renderer, font, short_status, engine.x + 174, engine.y + 12,
                  Global_Correlation_Working ? Correlation_Text : Correlation_Muted);
    }

    if (Global_Correlation_Working && Global_Correlation_Progress_Total > 0) {

        SDL_Rect progress_background = {engine.x + 174, engine.y + 31, engine.w - 190, 6};
        SDL_Rect progress_fill = progress_background;
        double progress = (double)Global_Correlation_Progress / (double)Global_Correlation_Progress_Total;

        progress_fill.w = (int)((double)progress_background.w * correlation_clamp_double(progress, 0.0, 1.0));
        draw_filled_rect(renderer, progress_background, (SDL_Color){0, 30, 12, 255});
        draw_filled_rect(renderer, progress_fill, Correlation_Border_Hi);

    }

    draw_text(renderer, font, "Mag", magnitude_box.x - 42, magnitude_box.y + 8, Correlation_Muted);
    draw_text(renderer, font, "Freq", frequency_box.x - 46, frequency_box.y + 8, Correlation_Muted);
    draw_text(renderer, font, "BW", bandwidth_box.x - 32, bandwidth_box.y + 8, Correlation_Muted);
    draw_text(renderer, font, "Resolution", resolution_box.x - 88, resolution_box.y + 8, Correlation_Muted);

    {
        SDL_Rect boxes[4] = {magnitude_box, frequency_box, bandwidth_box, resolution_box};
        const char *texts[4] = {Global_Correlation_Magnitude_Weight_Text, Global_Correlation_Frequency_Weight_Text,
                                Global_Correlation_Bandwidth_Weight_Text, Global_Correlation_Trend_Points_Text};

        for (int i = 0; i < 4; i++) {
            int active = Global_Correlation_Active_Weight_Field == i + 1;

            draw_filled_rect(renderer, boxes[i], active ? (SDL_Color){0, 25, 10, 255} : (SDL_Color){0, 5, 2, 255});
            draw_outline_rect(renderer, boxes[i], active ? Correlation_Border_Hi : Correlation_Border);
            draw_text(renderer, font, texts[i], boxes[i].x + 8, boxes[i].y + 7,
                      active ? Correlation_Text : Correlation_Muted);

            if (active && ((SDL_GetTicks64() / 450ULL) % 2ULL) == 0ULL) {

                char prefix[CORRELATION_WEIGHT_TEXT_MAX];
                int width = 0;
                int height = 0;
                int cursor = Global_Correlation_Weight_Cursor;
                int length = (int)strlen(texts[i]);

                if (cursor < 0) {

                    cursor = 0;

                }

                if (cursor > length) {

                    cursor = length;

                }

                snprintf(prefix, sizeof(prefix), "%.*s", cursor, texts[i]);
                (void)TTF_SizeText(font, prefix, &width, &height);
                SDL_SetRenderDrawColor(renderer, Correlation_Blue.r, Correlation_Blue.g, Correlation_Blue.b, 255);
                SDL_RenderDrawLine(renderer, boxes[i].x + 8 + width, boxes[i].y + 5, boxes[i].x + 8 + width,
                                   boxes[i].y + boxes[i].h - 5);

            }
        }
    }

    correlation_draw_button(renderer, font, compare_button,
                            Global_Correlation_Working ? "Processing..." : "Compare All",
                            correlation_point_in_rect(mouse_x, mouse_y, compare_button),
                            Global_Correlation_Working || Global_Correlation_Selected_File < 0);
    correlation_draw_button(renderer, font, stop_button, Global_Correlation_Cancel ? "Stopping..." : "Stop",
                            correlation_point_in_rect(mouse_x, mouse_y, stop_button),
                            !Global_Correlation_Working || Global_Correlation_Cancel);
    correlation_draw_button(renderer, font, rescan_button, "Rescan",
                            correlation_point_in_rect(mouse_x, mouse_y, rescan_button), Global_Correlation_Working);
    correlation_draw_button(renderer, font, clear_button, "Clear Cache",
                            correlation_point_in_rect(mouse_x, mouse_y, clear_button), Global_Correlation_Working);

    draw_filled_rect(renderer, recordings, Correlation_Panel);
    draw_outline_rect(renderer, recordings, Correlation_Border);
    draw_text(renderer, font, "QUERY RECORDING", recordings.x + 12, recordings.y + 14, Correlation_Text);

    {
        char selected_line[CORRELATION_MAX_NAME + 32];
        char short_selected[CORRELATION_MAX_NAME + 32];
        const char *selected =
            Global_Correlation_Selected_File >= 0 && Global_Correlation_Selected_File < Global_Correlation_File_Count
                ? Global_Correlation_Files[Global_Correlation_Selected_File]
                : "None";

        snprintf(selected_line, sizeof(selected_line), "Selected: %s", selected);
        correlation_short_text(font, selected_line, short_selected, sizeof(short_selected), recordings.w - 470);
        draw_text(renderer, font, short_selected, recordings.x + 205, recordings.y + 14, Correlation_Muted);
    }

    correlation_draw_button(renderer, font, search_button, "Open Search Menu",
                            correlation_point_in_rect(mouse_x, mouse_y, search_button), Global_Correlation_Working);
    draw_filled_rect(renderer, file_list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, file_list, Correlation_Border);

    visible_file_rows = file_list.h / CORRELATION_LIST_ROW_H;

    if (visible_file_rows < 1) {

        visible_file_rows = 1;

    }

    if (Global_Correlation_File_Count <= 0) {

        correlation_draw_wrapped_text(renderer, font, "No supported IQ recordings found.",
                                      (SDL_Rect){file_list.x + 8, file_list.y + 10, file_list.w - 16, 60},
                                      Correlation_Warn, 3);

    }

    else {

        for (int row = 0; row < visible_file_rows; row++) {
            int index = Global_Correlation_File_Scroll + row;
            SDL_Rect row_rect = {file_list.x + 4, file_list.y + 4 + row * CORRELATION_LIST_ROW_H, file_list.w - 8,
                                 CORRELATION_LIST_ROW_H - 3};
            char short_name[CORRELATION_MAX_NAME];

            if (index >= Global_Correlation_File_Count) {

                break;

            }

            if (index == Global_Correlation_Selected_File) {

                draw_filled_rect(renderer, row_rect, (SDL_Color){0, 44, 16, 255});
                draw_outline_rect(renderer, row_rect, Correlation_Border_Hi);

            }

            else if (correlation_point_in_rect(mouse_x, mouse_y, row_rect)) {

                draw_filled_rect(renderer, row_rect, (SDL_Color){0, 24, 9, 255});
                draw_outline_rect(renderer, row_rect, Correlation_Border);

            }

            correlation_short_text(font, Global_Correlation_Files[index], short_name, sizeof(short_name),
                                   row_rect.w - 16);
            draw_text(renderer, font, short_name, row_rect.x + 8, row_rect.y + 8,
                      index == Global_Correlation_Selected_File ? Correlation_Text : Correlation_Muted);
        }

    }

    draw_filled_rect(renderer, results, Correlation_Panel);
    draw_outline_rect(renderer, results, Correlation_Border);
    draw_text(renderer, font, "SIMILARITY RESULTS", results.x + 12, results.y + 14, Correlation_Text);
    draw_text(renderer, font, "Highest similarity first", results.x + 180, results.y + 14, Correlation_Muted);

    visible_result_rows = (results.h - 54) / CORRELATION_RESULT_ROW_H;

    if (visible_result_rows < 1) {

        visible_result_rows = 1;

    }

    if (Global_Correlation_Result_Count <= 0) {

        correlation_draw_wrapped_text(
            renderer, font,
            Global_Correlation_Working ? "Signatures are being extracted and compared."
                                       : "No results yet. Select a query and press Compare All.",
            (SDL_Rect){results.x + 12, results.y + 50, results.w - 24, 80}, Correlation_Warn, 4);

    }

    else {

        for (int row = 0; row < visible_result_rows; row++) {
            int index = Global_Correlation_Result_Scroll + row;
            Type_Correlation_Result *result;
            SDL_Rect row_rect;
            SDL_Rect export_button;
            SDL_Rect score_box;
            char component_line[256];
            char timing_line[256];
            char short_name[CORRELATION_MAX_NAME];
            SDL_Color score_color;
            SDL_Color score_text_color;
            int luminance;
            int analysis_workspace;
            int analysis_available;
            int text_width;
            char export_label[64];

            if (index >= Global_Correlation_Result_Count) {

                break;

            }

            result = &Global_Correlation_Results[index];
            row_rect = (SDL_Rect){results.x + 8, results.y + 48 + row * CORRELATION_RESULT_ROW_H, results.w - 16,
                                  CORRELATION_RESULT_ROW_H - 4};
            correlation_get_result_controls(row_rect, &export_button, &score_box);
            analysis_workspace = ANALYSIS_get_recording_workspace(result->file_name);
            analysis_available = ANALYSIS_get_available_workspace_count();
            text_width = export_button.x - row_rect.x - 16;

            if (text_width < 120) {

                text_width = 120;

            }

            if (index == Global_Correlation_Selected_Result) {

                draw_filled_rect(renderer, row_rect, (SDL_Color){0, 44, 16, 255});
                draw_outline_rect(renderer, row_rect, Correlation_Border_Hi);

            }

            else if (correlation_point_in_rect(mouse_x, mouse_y, row_rect)) {

                draw_filled_rect(renderer, row_rect, (SDL_Color){0, 24, 9, 255});
                draw_outline_rect(renderer, row_rect, Correlation_Border);

            }

            correlation_short_text(font, result->file_name, short_name, sizeof(short_name), text_width);
            draw_text(renderer, font, short_name, row_rect.x + 8, row_rect.y + 7,
                      index == Global_Correlation_Selected_Result ? Correlation_Text : Correlation_Muted);
            snprintf(component_line, sizeof(component_line), "Frequency %.1f%% | Magnitude %.1f%% | Bandwidth %.1f%%",
                     result->frequency_score * 100.0, result->magnitude_score * 100.0, result->bandwidth_score * 100.0);
            {
                char short_components[256];

                correlation_short_text(font, component_line, short_components, sizeof(short_components), text_width);
                draw_text(renderer, font, short_components, row_rect.x + 8, row_rect.y + 31, Correlation_Muted);
            }
            snprintf(timing_line, sizeof(timing_line), "Burst %.3f s at %.3f s | BW %.1f kHz",
                     result->signature.duration, result->signature.start_time,
                     result->signature.occupied_bandwidth / 1000.0);
            {
                char short_timing[256];

                correlation_short_text(font, timing_line, short_timing, sizeof(short_timing), text_width);
                draw_text(renderer, font, short_timing, row_rect.x + 8, row_rect.y + 54, Correlation_Muted);
            }

            if (analysis_workspace > 0) {

                snprintf(export_label, sizeof(export_label), "Analysis W%d", analysis_workspace);

            }

            else if (analysis_available <= 0) {

                snprintf(export_label, sizeof(export_label), "Analysis Full");

            }

            else {

                snprintf(export_label, sizeof(export_label), export_button.w >= 145 ? "Export to Analysis" : "Export");

            }

            correlation_draw_button(renderer, font, export_button, export_label,
                                    correlation_point_in_rect(mouse_x, mouse_y, export_button),
                                    analysis_workspace > 0 || analysis_available <= 0);

            score_color = correlation_score_color(result->score);
            luminance = (int)score_color.r * 299 + (int)score_color.g * 587 + (int)score_color.b * 114;
            score_text_color = luminance > 145000 ? (SDL_Color){0, 0, 0, 255} : (SDL_Color){255, 255, 255, 255};
            draw_filled_rect(renderer, score_box, score_color);
            draw_outline_rect(renderer, score_box, (SDL_Color){235, 255, 240, 255});

            {
                char score_text[32];

                snprintf(score_text, sizeof(score_text), "%.1f%%", result->score * 100.0);
                correlation_draw_centered_text(renderer, font, score_text, score_box, score_text_color);
            }
        }

    }

    draw_filled_rect(renderer, graphs, Correlation_Panel);
    draw_outline_rect(renderer, graphs, Correlation_Border);

    {
        int gap = 12;
        int graph_w = (graphs.w - 24 - 2 * gap) / 3;
        SDL_Rect magnitude_graph = {graphs.x + 8, graphs.y + 30, graph_w, graphs.h - 38};
        SDL_Rect frequency_graph = {magnitude_graph.x + graph_w + gap, magnitude_graph.y, graph_w, magnitude_graph.h};
        SDL_Rect phase_graph = {frequency_graph.x + graph_w + gap, magnitude_graph.y,
                                graphs.x + graphs.w - 8 - (frequency_graph.x + graph_w + gap), magnitude_graph.h};
        const TransmissionSignature *selected_signature =
            Global_Correlation_Selected_Result >= 0 &&
                    Global_Correlation_Selected_Result < Global_Correlation_Result_Count
                ? &Global_Correlation_Results[Global_Correlation_Selected_Result].signature
                : NULL;
        double weight_total = Global_Correlation_Magnitude_Weight + Global_Correlation_Frequency_Weight +
                              Global_Correlation_Bandwidth_Weight;
        char magnitude_label[64];
        char frequency_label[64];

        snprintf(magnitude_label, sizeof(magnitude_label), "Magnitude trend (%.1f%%)",
                 weight_total > DBL_MIN ? 100.0 * Global_Correlation_Magnitude_Weight / weight_total : 0.0);
        snprintf(frequency_label, sizeof(frequency_label), "Frequency trend (%.1f%%)",
                 weight_total > DBL_MIN ? 100.0 * Global_Correlation_Frequency_Weight / weight_total : 0.0);
        draw_text(renderer, font, magnitude_label, magnitude_graph.x, graphs.y + 8, Correlation_Muted);
        draw_text(renderer, font, frequency_label, frequency_graph.x, graphs.y + 8, Correlation_Muted);
        draw_text(renderer, font, "Phase trend (visual only)", phase_graph.x, graphs.y + 8, Correlation_Muted);

        if (Global_Correlation_Query_Signature_Valid) {

            correlation_draw_trend_pair(renderer, magnitude_graph, Global_Correlation_Query_Signature.magnitude_trend,
                                        selected_signature ? selected_signature->magnitude_trend : NULL,
                                        Correlation_Text, Correlation_Blue);
            correlation_draw_trend_pair(renderer, frequency_graph, Global_Correlation_Query_Signature.frequency_trend,
                                        selected_signature ? selected_signature->frequency_trend : NULL,
                                        Correlation_Text, Correlation_Blue);
            correlation_draw_trend_pair(renderer, phase_graph, Global_Correlation_Query_Signature.phase_trend,
                                        selected_signature ? selected_signature->phase_trend : NULL, Correlation_Text,
                                        Correlation_Blue);

        }

        else {

            draw_filled_rect(renderer, magnitude_graph, (SDL_Color){0, 5, 2, 255});
            draw_outline_rect(renderer, magnitude_graph, Correlation_Border);
            draw_filled_rect(renderer, frequency_graph, (SDL_Color){0, 5, 2, 255});
            draw_outline_rect(renderer, frequency_graph, Correlation_Border);
            draw_filled_rect(renderer, phase_graph, (SDL_Color){0, 5, 2, 255});
            draw_outline_rect(renderer, phase_graph, Correlation_Border);

        }

        draw_text(renderer, font, "Query: green | Selected result: blue", graphs.x + graphs.w - 300, graphs.y + 8,
                  Correlation_Muted);
    }

    correlation_draw_file_search_popup(renderer, font, win_w, win_h);

    if (Global_Correlation_Clear_Confirm) {

        SDL_BlendMode previous_blend_mode = SDL_BLENDMODE_NONE;
        SDL_Color destructive = {210, 45, 45, 255};

        (void)SDL_GetRenderDrawBlendMode(renderer, &previous_blend_mode);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        draw_filled_rect(renderer, (SDL_Rect){0, 0, win_w, win_h}, (SDL_Color){0, 0, 0, 190});
        SDL_SetRenderDrawBlendMode(renderer, previous_blend_mode);

        draw_filled_rect(renderer, clear_dialog, (SDL_Color){12, 14, 13, 255});
        draw_outline_rect(renderer, clear_dialog, destructive);
        draw_text(renderer, font, "CLEAR CORRELATION CACHE?", clear_dialog.x + 24, clear_dialog.y + 22, destructive);
        correlation_draw_wrapped_text(
            renderer, font,
            "Cached transmission signatures and extracted correlation metadata will be permanently deleted.",
            (SDL_Rect){clear_dialog.x + 24, clear_dialog.y + 60, clear_dialog.w - 48, 48}, Correlation_Text, 2);
        correlation_draw_wrapped_text(
            renderer, font,
            "Original recording files will not be removed. The metadata will be rebuilt during the next comparison.",
            (SDL_Rect){clear_dialog.x + 24, clear_dialog.y + 112, clear_dialog.w - 48, 42}, Correlation_Muted, 2);

        correlation_draw_button(renderer, font, clear_cancel_button, "Cancel",
                                correlation_point_in_rect(mouse_x, mouse_y, clear_cancel_button), 0);
        draw_filled_rect(renderer, clear_confirm_button,
                         correlation_point_in_rect(mouse_x, mouse_y, clear_confirm_button)
                             ? (SDL_Color){120, 20, 20, 255}
                             : (SDL_Color){75, 12, 12, 255});
        draw_outline_rect(renderer, clear_confirm_button, destructive);
        correlation_draw_centered_text(renderer, font, "Clear Cache", clear_confirm_button,
                                       (SDL_Color){255, 235, 235, 255});

    }
}
