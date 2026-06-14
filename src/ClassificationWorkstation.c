/*
 * ============================================================================
 * File:            ClassificationWorkstation.c
 * Author:          Hassan Fares
 *
 * Confidential:    No
 *
 * Description:     Simple signal classification workstation for RetroSpectrum.
 *                  Builds CSV rows from manually entered classification fields.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include "ClassificationWorkstation.h"
#include "GUIs.h"

#include <sys/stat.h>
#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CLASSIFICATION_MAX_FILES          512
#define CLASSIFICATION_MAX_PATH           1024
#define CLASSIFICATION_MAX_TEXT           512
#define CLASSIFICATION_MAX_FILE_PATH      (CLASSIFICATION_MAX_PATH + 512 + 2)
#define CLASSIFICATION_MAX_CSV_NAME       768
#define CLASSIFICATION_MAX_CSV_PATH       (CLASSIFICATION_MAX_CSV_NAME + 64)
#define CLASSIFICATION_ROW_HEIGHT         24
#define CLASSIFICATION_MARGIN             20
#define CLASSIFICATION_OUTPUT_DIR         "Classification"
#define CLASSIFICATION_DROPDOWN_NONE      -1
#define CLASSIFICATION_DROPDOWN_OPTION_H 28
#define CLASSIFICATION_DROPDOWN_MAX_VISIBLE 9
#define CLASSIFICATION_NOTES_LINE_H       19

enum {
    CLASSIFICATION_FIELD_NONE = -1,
    CLASSIFICATION_FIELD_SIGNAL_NAME = 0,
    CLASSIFICATION_FIELD_FREQUENCY_MHZ,
    CLASSIFICATION_FIELD_BANDWIDTH,
    CLASSIFICATION_FIELD_START_TIME,
    CLASSIFICATION_FIELD_END_TIME,
    CLASSIFICATION_FIELD_CALCULATED_MODULATION,
    CLASSIFICATION_FIELD_SIGNAL_CLASS,
    CLASSIFICATION_FIELD_NOTES,
    CLASSIFICATION_FIELD_FILE_NAME,
    CLASSIFICATION_FIELD_COUNT
};

int Global_Classification_Mode = 0;

static char Global_Classification_Record_Dir[CLASSIFICATION_MAX_PATH] = "Recordings";
static char Global_Classification_Files[CLASSIFICATION_MAX_FILES][512];
static int  Global_Classification_File_Count = 0;
static int  Global_Classification_Selected_File = 0;
static int  Global_Classification_File_Scroll = 0;
static int  Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
static int  Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
static int  Global_Classification_Dropdown_Scroll = 0;
static int  Global_Classification_Dropdown_Hover = -1;
static int  Global_Classification_Notes_Cursor = 0;
static char Global_Classification_Status[512] = "Press R to scan recordings";
static char Global_Classification_Save_Message[512] = "";
static Uint64 Global_Classification_Save_Message_Time = 0;

static char Global_Classification_Field_Text[CLASSIFICATION_FIELD_COUNT][CLASSIFICATION_MAX_TEXT] = {
    "", "", "", "", "", "Unknown", "Unknown", "", ""
};

static const char *CLASSIFICATION_FIELD_LABELS[CLASSIFICATION_FIELD_COUNT] = {
    "Signal Name",
    "Frequency MHz",
    "Bandwidth",
    "Start Time",
    "End Time",
    "Calculated Modulation",
    "Signal Class",
    "Notes",
    "File Name"
};

static const char *CLASSIFICATION_MODULATION_OPTIONS[] = {
    "Unknown",
    "AM-like",
    "ASK-like",
    "OOK-like",
    "FM-like",
    "FSK-like",
    "GFSK-like",
    "MSK-like",
    "GMSK-like",
    "PSK-like",
    "BPSK-like",
    "QPSK-like",
    "8PSK-like",
    "QAM-like",
    "16QAM-like",
    "64QAM-like",
    "OFDM-like",
    "DSSS-like",
    "FHSS-like",
    "Chirp-like",
    "CSS / LoRa-like",
    "Pulse-like",
    "PPM-like",
    "PWM-like",
    "CW / Carrier",
    "Noise-like",
    "Wideband Digital",
    "Narrowband Digital"
};

static const char *CLASSIFICATION_SIGNAL_CLASS_OPTIONS[] = {
    "Unknown",
    "Unknown Digital",
    "Unknown Analog",
    "Remote / ISM-like",
    "Telemetry-like",
    "Sensor-like",
    "Keyfob / Remote-like",
    "Utility Meter-like",
    "LoRa-like",
    "BLE-like",
    "Bluetooth Classic-like",
    "Wi-Fi-like",
    "Zigbee / 802.15.4-like",
    "Z-Wave-like",
    "Pager-like",
    "Narrowband FM-like",
    "Analog Voice-like",
    "Digital Voice-like",
    "P25-like",
    "DMR-like",
    "ADS-B-like",
    "AIS-like",
    "GPS-like",
    "Satellite-like",
    "Radar-like",
    "Continuous Carrier",
    "Noise / RFI-like",
    "Test Signal"
};

static int CLASSIFICATION_is_dropdown_field(int field)
{
    return field == CLASSIFICATION_FIELD_CALCULATED_MODULATION ||
           field == CLASSIFICATION_FIELD_SIGNAL_CLASS;
}

static int CLASSIFICATION_option_count_for_field(int field)
{
    if (field == CLASSIFICATION_FIELD_CALCULATED_MODULATION) {
        return (int)(sizeof(CLASSIFICATION_MODULATION_OPTIONS) / sizeof(CLASSIFICATION_MODULATION_OPTIONS[0]));
    }

    if (field == CLASSIFICATION_FIELD_SIGNAL_CLASS) {
        return (int)(sizeof(CLASSIFICATION_SIGNAL_CLASS_OPTIONS) / sizeof(CLASSIFICATION_SIGNAL_CLASS_OPTIONS[0]));
    }

    return 0;
}

static const char *CLASSIFICATION_option_for_field(int field, int index)
{
    if (field == CLASSIFICATION_FIELD_CALCULATED_MODULATION) {
        int count = CLASSIFICATION_option_count_for_field(field);
        if (index >= 0 && index < count) return CLASSIFICATION_MODULATION_OPTIONS[index];
    }

    if (field == CLASSIFICATION_FIELD_SIGNAL_CLASS) {
        int count = CLASSIFICATION_option_count_for_field(field);
        if (index >= 0 && index < count) return CLASSIFICATION_SIGNAL_CLASS_OPTIONS[index];
    }

    return "";
}

static void CLASSIFICATION_clamp_dropdown_scroll(int field)
{
    int count = CLASSIFICATION_option_count_for_field(field);
    int max_scroll = count - CLASSIFICATION_DROPDOWN_MAX_VISIBLE;

    if (max_scroll < 0) max_scroll = 0;

    if (Global_Classification_Dropdown_Scroll < 0) {
        Global_Classification_Dropdown_Scroll = 0;
    }

    if (Global_Classification_Dropdown_Scroll > max_scroll) {
        Global_Classification_Dropdown_Scroll = max_scroll;
    }
}

static int CLASSIFICATION_dropdown_visible_count(int field)
{
    int count = CLASSIFICATION_option_count_for_field(field);
    int visible = count;

    if (visible > CLASSIFICATION_DROPDOWN_MAX_VISIBLE) {
        visible = CLASSIFICATION_DROPDOWN_MAX_VISIBLE;
    }

    if (visible < 1) visible = 1;

    return visible;
}


static int CLASSIFICATION_name_compare(const void *a, const void *b)
{
    const char *sa = (const char *)a;
    const char *sb = (const char *)b;
    return strcmp(sa, sb);
}

static int CLASSIFICATION_is_complex16_file(const char *name)
{
    size_t len = strlen(name);
    const char *suffix = ".complex16";
    size_t suffix_len = strlen(suffix);

    if (len < suffix_len) return 0;
    return strcmp(name + len - suffix_len, suffix) == 0;
}

static void CLASSIFICATION_append_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || !src || dst_size == 0) return;

    size_t used = strlen(dst);
    if (used >= dst_size - 1) return;

    strncat(dst, src, dst_size - used - 1);
}

static void CLASSIFICATION_backspace_text(char *dst)
{
    if (!dst) return;

    size_t len = strlen(dst);
    if (len > 0) dst[len - 1] = '\0';
}

static void CLASSIFICATION_clamp_notes_cursor(void)
{
    int len = (int)strlen(Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES]);

    if (Global_Classification_Notes_Cursor < 0) {
        Global_Classification_Notes_Cursor = 0;
    }

    if (Global_Classification_Notes_Cursor > len) {
        Global_Classification_Notes_Cursor = len;
    }
}

static void CLASSIFICATION_insert_notes_text(const char *src)
{
    char *dst = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];

    if (!src) return;

    CLASSIFICATION_clamp_notes_cursor();

    size_t len = strlen(dst);
    size_t add = strlen(src);

    if (add == 0 || len >= CLASSIFICATION_MAX_TEXT - 1) return;

    if (add > (CLASSIFICATION_MAX_TEXT - 1) - len) {
        add = (CLASSIFICATION_MAX_TEXT - 1) - len;
    }

    memmove(dst + Global_Classification_Notes_Cursor + add,
            dst + Global_Classification_Notes_Cursor,
            len - (size_t)Global_Classification_Notes_Cursor + 1);

    memcpy(dst + Global_Classification_Notes_Cursor, src, add);
    Global_Classification_Notes_Cursor += (int)add;
}

static void CLASSIFICATION_backspace_notes_text(void)
{
    char *dst = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];

    CLASSIFICATION_clamp_notes_cursor();

    if (Global_Classification_Notes_Cursor <= 0) return;

    size_t len = strlen(dst);

    memmove(dst + Global_Classification_Notes_Cursor - 1,
            dst + Global_Classification_Notes_Cursor,
            len - (size_t)Global_Classification_Notes_Cursor + 1);

    Global_Classification_Notes_Cursor--;
}

static void CLASSIFICATION_delete_notes_text(void)
{
    char *dst = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];

    CLASSIFICATION_clamp_notes_cursor();

    size_t len = strlen(dst);

    if (Global_Classification_Notes_Cursor >= (int)len) return;

    memmove(dst + Global_Classification_Notes_Cursor,
            dst + Global_Classification_Notes_Cursor + 1,
            len - (size_t)Global_Classification_Notes_Cursor);
}

static int CLASSIFICATION_notes_build_lines(int starts[128], int ends[128])
{
    const char *text = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];
    int len = (int)strlen(text);
    int line_count = 0;
    int start = 0;

    while (line_count < 128) {
        int end = start;

        while (end < len && text[end] != '\n') {
            end++;
        }

        starts[line_count] = start;
        ends[line_count] = end;
        line_count++;

        if (end >= len) break;

        start = end + 1;
    }

    if (line_count < 1) {
        starts[0] = 0;
        ends[0] = 0;
        line_count = 1;
    }

    return line_count;
}

static void CLASSIFICATION_notes_move_horizontal(int direction)
{
    CLASSIFICATION_clamp_notes_cursor();

    if (direction < 0) {
        if (Global_Classification_Notes_Cursor > 0) {
            Global_Classification_Notes_Cursor--;
        }
    }
    else if (direction > 0) {
        int len = (int)strlen(Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES]);

        if (Global_Classification_Notes_Cursor < len) {
            Global_Classification_Notes_Cursor++;
        }
    }
}

static void CLASSIFICATION_notes_move_vertical(int direction)
{
    int starts[128];
    int ends[128];
    int line_count = CLASSIFICATION_notes_build_lines(starts, ends);

    CLASSIFICATION_clamp_notes_cursor();

    int current_line = 0;

    for (int i = 0; i < line_count; i++) {
        if (Global_Classification_Notes_Cursor >= starts[i] &&
            Global_Classification_Notes_Cursor <= ends[i]) {
            current_line = i;
            break;
        }

        if (i + 1 < line_count &&
            Global_Classification_Notes_Cursor > ends[i] &&
            Global_Classification_Notes_Cursor < starts[i + 1]) {
            current_line = i;
            break;
        }
    }

    int target_line = current_line + direction;

    if (target_line < 0 || target_line >= line_count) return;

    /* Up/down intentionally jump to the end of the target line. */
    Global_Classification_Notes_Cursor = ends[target_line];
    CLASSIFICATION_clamp_notes_cursor();
}

static void CLASSIFICATION_set_notes_cursor_from_mouse(SDL_Rect rect, int mouse_x, int mouse_y)
{
    int starts[128];
    int ends[128];
    int line_count = CLASSIFICATION_notes_build_lines(starts, ends);
    int max_lines = (rect.h - 12) / CLASSIFICATION_NOTES_LINE_H;

    if (max_lines < 1) max_lines = 1;

    int first_line = 0;

    if (line_count > max_lines) {
        first_line = line_count - max_lines;
    }

    int visible_line = (mouse_y - (rect.y + 7)) / CLASSIFICATION_NOTES_LINE_H;

    if (visible_line < 0) visible_line = 0;
    if (visible_line >= max_lines) visible_line = max_lines - 1;

    int line = first_line + visible_line;

    if (line < 0) line = 0;
    if (line >= line_count) line = line_count - 1;

    int line_len = ends[line] - starts[line];
    int text_x = rect.x + 9;
    int usable_w = rect.w - 18;
    int column = 0;

    if (line_len > 0 && usable_w > 0) {
        int rel_x = mouse_x - text_x;
        int approx_char_w = 8;

        if (line_len > 0 && line_len * approx_char_w > usable_w) {
            approx_char_w = usable_w / line_len;
            if (approx_char_w < 1) approx_char_w = 1;
        }

        column = (rel_x + (approx_char_w / 2)) / approx_char_w;
    }

    if (column < 0) column = 0;
    if (column > line_len) column = line_len;

    Global_Classification_Notes_Cursor = starts[line] + column;
    CLASSIFICATION_clamp_notes_cursor();
}

static void CLASSIFICATION_short_text(TTF_Font *font,
                                      const char *src,
                                      char *dst,
                                      size_t dst_size,
                                      int max_px)
{
    if (!dst || dst_size == 0) return;

    if (!src) src = "";
    snprintf(dst, dst_size, "%s", src);

    if (!font || max_px <= 0) return;

    int text_w = 0;
    int text_h = 0;

    if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) return;

    size_t len = strlen(dst);

    while (len > 4) {
        len--;
        dst[len] = '\0';
        snprintf(dst + len - 3, dst_size - len + 3, "...");

        if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) return;

        if (len > 3) dst[len - 3] = '\0';
    }

    snprintf(dst, dst_size, "...");
}


static void CLASSIFICATION_draw_multiline_notes(SDL_Renderer *renderer,
                                                TTF_Font *font,
                                                SDL_Rect rect,
                                                const char *text,
                                                int active)
{
    if (!renderer || !font) return;

    const char *src = text ? text : "";
    char local[CLASSIFICATION_MAX_TEXT + 8];

    if (src[0]) {
        int src_len = (int)strlen(src);
        int cursor = Global_Classification_Notes_Cursor;

        if (cursor < 0) cursor = 0;
        if (cursor > src_len) cursor = src_len;

        if (active) {
            snprintf(local,
                     sizeof(local),
                     "%.*s_%s",
                     cursor,
                     src,
                     src + cursor);
        }
        else {
            snprintf(local, sizeof(local), "%.*s", CLASSIFICATION_MAX_TEXT - 1, src);
        }
    }
    else {
        snprintf(local, sizeof(local), "%s", active ? "_" : "Click to type");
    }

    int line_h = CLASSIFICATION_NOTES_LINE_H;
    int max_lines = (rect.h - 12) / line_h;

    if (max_lines < 1) max_lines = 1;

    const char *line_starts[128];
    int line_count = 0;

    line_starts[line_count++] = local;

    for (char *p = local; *p && line_count < 128; p++) {
        if (*p == '\n') {
            *p = '\0';
            line_starts[line_count++] = p + 1;
        }
    }

    int first_line = 0;

    if (line_count > max_lines) {
        first_line = line_count - max_lines;
    }

    int y = rect.y + 7;

    for (int i = first_line; i < line_count; i++) {
        char short_line[CLASSIFICATION_MAX_TEXT + 16];

        CLASSIFICATION_short_text(font,
                                  line_starts[i],
                                  short_line,
                                  sizeof(short_line),
                                  rect.w - 18);

        draw_text(renderer,
                  font,
                  short_line,
                  rect.x + 9,
                  y,
                  src[0] || active ?
                  (SDL_Color){230, 230, 230, 255} :
                  (SDL_Color){120, 150, 130, 255});

        y += line_h;

        if (y + line_h > rect.y + rect.h) break;
    }
}

static void CLASSIFICATION_get_layout(int win_w,
                                      int win_h,
                                      SDL_Rect *file_rect,
                                      SDL_Rect *form_rect,
                                      SDL_Rect field_rects[CLASSIFICATION_FIELD_COUNT],
                                      SDL_Rect *save_rect)
{
    int gap = 14;
    int top = CLASSIFICATION_MARGIN + 58;
    int usable_w = win_w - (2 * CLASSIFICATION_MARGIN);
    int usable_h = win_h - top - CLASSIFICATION_MARGIN;

    /* Left half: recording file selector. Right half: classification fields. */
    int list_w = (usable_w - gap) / 2;
    int form_w = usable_w - list_w - gap;

    if (usable_h < 300) usable_h = 300;

    SDL_Rect local_file = {CLASSIFICATION_MARGIN, top, list_w, usable_h};
    SDL_Rect local_form = {CLASSIFICATION_MARGIN + list_w + gap, top, form_w, usable_h};

    if (file_rect) *file_rect = local_file;
    if (form_rect) *form_rect = local_form;

    if (field_rects) {
        int label_w = 230;
        int field_h = 36;
        int row_gap = 12;
        int x = local_form.x + label_w + 20;
        int y = local_form.y + 50;
        int w = local_form.w - label_w - 40;

        if (w < 180) w = 180;

        for (int i = 0; i < CLASSIFICATION_FIELD_COUNT; i++) {
            int h = field_h;
            if (i == CLASSIFICATION_FIELD_NOTES) h = 140;

            field_rects[i] = (SDL_Rect){x, y, w, h};
            y += h + row_gap;
        }
    }

    if (save_rect) {
        *save_rect = (SDL_Rect){local_form.x + local_form.w - 170,
                                local_form.y + local_form.h - 68,
                                150,
                                42};
    }
}

static int CLASSIFICATION_ensure_output_dir(void)
{
    struct stat st;

    if (stat(CLASSIFICATION_OUTPUT_DIR, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    return mkdir(CLASSIFICATION_OUTPUT_DIR, 0755) == 0;
}

static void CLASSIFICATION_csv_escape(FILE *fp, const char *text)
{
    fputc('"', fp);

    if (text) {
        for (const char *p = text; *p; p++) {
            if (*p == '"') fputc('"', fp);
            fputc(*p, fp);
        }
    }

    fputc('"', fp);
}

static void CLASSIFICATION_parse_file_metadata(const char *name,
                                               double *frequency_mhz,
                                               double *bandwidth_khz,
                                               double *start_time,
                                               double *end_time)
{
    double mhz = 0.0;
    double bw_khz = 0.0;
    double sr_khz = 0.0;
    double duration_sec = 0.0;

    if (frequency_mhz) *frequency_mhz = 0.0;
    if (bandwidth_khz) *bandwidth_khz = 0.0;
    if (start_time) *start_time = 0.0;
    if (end_time) *end_time = 0.0;

    const char *cap = strstr(name, "_CAPTURE_");
    if (cap && sscanf(cap, "_CAPTURE_%lfMHz", &mhz) == 1 && mhz > 0.0) {
        if (frequency_mhz) *frequency_mhz = mhz;
    }

    const char *bw = strstr(name, "_BW_");
    if (bw && sscanf(bw, "_BW_%lfk", &bw_khz) == 1 && bw_khz > 0.0) {
        if (bandwidth_khz) *bandwidth_khz = bw_khz;
    }

    const char *sr = strstr(name, "_SR_");
    if (sr) sscanf(sr, "_SR_%lfk", &sr_khz);

    if (sr_khz > 0.0) {
        char path[CLASSIFICATION_MAX_FILE_PATH];
        int written = snprintf(path,
                               sizeof(path),
                               "%s/%s",
                               Global_Classification_Record_Dir,
                               name);

        if (written < 0 || (size_t)written >= sizeof(path)) {
            return;
        }

        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
            double iq_count = (double)st.st_size / (double)(sizeof(int16_t) * 2);
            duration_sec = iq_count / (sr_khz * 1000.0);
        }
    }

    if (end_time) *end_time = duration_sec;
}

static void CLASSIFICATION_load_selected_file_into_fields(void)
{
    if (Global_Classification_File_Count <= 0) return;

    const char *file_name = Global_Classification_Files[Global_Classification_Selected_File];
    double frequency_mhz = 0.0;
    double bandwidth_khz = 0.0;
    double start_time = 0.0;
    double end_time = 0.0;

    CLASSIFICATION_parse_file_metadata(file_name,
                                       &frequency_mhz,
                                       &bandwidth_khz,
                                       &start_time,
                                       &end_time);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_FREQUENCY_MHZ],
             CLASSIFICATION_MAX_TEXT,
             "%.6f",
             frequency_mhz);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_BANDWIDTH],
             CLASSIFICATION_MAX_TEXT,
             "%.3f kHz",
             bandwidth_khz);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_START_TIME],
             CLASSIFICATION_MAX_TEXT,
             "%.6f",
             start_time);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_END_TIME],
             CLASSIFICATION_MAX_TEXT,
             "%.6f",
             end_time);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_FILE_NAME],
             CLASSIFICATION_MAX_TEXT,
             "%s",
             file_name);
}

static int CLASSIFICATION_scan_recordings(void)
{
    DIR *dir = opendir(Global_Classification_Record_Dir);
    Global_Classification_File_Count = 0;

    if (!dir) {
        snprintf(Global_Classification_Status,
                 sizeof(Global_Classification_Status),
                 "Could not open recording directory: %.220s",
                 Global_Classification_Record_Dir);
        return 0;
    }

    struct dirent *entry = NULL;

    while ((entry = readdir(dir)) != NULL &&
           Global_Classification_File_Count < CLASSIFICATION_MAX_FILES) {
        if (!CLASSIFICATION_is_complex16_file(entry->d_name)) continue;

        snprintf(Global_Classification_Files[Global_Classification_File_Count],
                 sizeof(Global_Classification_Files[Global_Classification_File_Count]),
                 "%s",
                 entry->d_name);
        Global_Classification_File_Count++;
    }

    closedir(dir);

    qsort(Global_Classification_Files,
          (size_t)Global_Classification_File_Count,
          sizeof(Global_Classification_Files[0]),
          CLASSIFICATION_name_compare);

    if (Global_Classification_File_Count <= 0) {
        Global_Classification_Selected_File = 0;
        Global_Classification_File_Scroll = 0;
        snprintf(Global_Classification_Status,
                 sizeof(Global_Classification_Status),
                 "No .complex16 recordings found in %.220s",
                 Global_Classification_Record_Dir);
        return 0;
    }

    if (Global_Classification_Selected_File < 0) Global_Classification_Selected_File = 0;
    if (Global_Classification_Selected_File >= Global_Classification_File_Count) {
        Global_Classification_Selected_File = Global_Classification_File_Count - 1;
    }

    CLASSIFICATION_load_selected_file_into_fields();

    snprintf(Global_Classification_Status,
             sizeof(Global_Classification_Status),
             "Found %d recording(s). Click fields to type, or click Modulation/Class to select.",
             Global_Classification_File_Count);
    return 1;
}

static void CLASSIFICATION_make_filename_safe(const char *src, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) return;

    if (!src || !src[0]) src = "UNNAMED_SIGNAL";

    size_t j = 0;

    for (size_t i = 0; src[i] && j + 1 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];

        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) {
            dst[j++] = (char)c;
        }
        else if (c == '-' || c == '_') {
            dst[j++] = (char)c;
        }
        else if (c == ' ' || c == '.' || c == '/' || c == ':' || c == '\\') {
            if (j > 0 && dst[j - 1] != '_') dst[j++] = '_';
        }
    }

    while (j > 0 && dst[j - 1] == '_') j--;

    if (j == 0) {
        snprintf(dst, dst_size, "UNNAMED_SIGNAL");
        return;
    }

    dst[j] = '\0';
}

static void CLASSIFICATION_get_signal_datetime(char *out, size_t out_size)
{
    if (!out || out_size == 0) return;

    const char *file_name = Global_Classification_Field_Text[CLASSIFICATION_FIELD_FILE_NAME];

    if (!file_name || !file_name[0]) {
        snprintf(out, out_size, "UNKNOWN_SIGNAL_TIME");
        return;
    }

    const char *capture = strstr(file_name, "_CAPTURE_");

    if (!capture || capture == file_name) {
        snprintf(out, out_size, "UNKNOWN_SIGNAL_TIME");
        return;
    }

    size_t len = (size_t)(capture - file_name);

    if (len >= out_size) len = out_size - 1;

    memcpy(out, file_name, len);
    out[len] = '\0';

    for (size_t i = 0; out[i]; i++) {
        char c = out[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '-' || c == '_')) {
            out[i] = '_';
        }
    }
}

static int CLASSIFICATION_append_csv_row(void)
{
    Global_Classification_Save_Message[0] = '\0';
    Global_Classification_Save_Message_Time = 0;

    if (!CLASSIFICATION_ensure_output_dir()) {
        snprintf(Global_Classification_Status,
                 sizeof(Global_Classification_Status),
                 "Failed to create Classification directory");
        return 0;
    }

    char safe_signal_name[CLASSIFICATION_MAX_TEXT];
    char signal_datetime[128];
    char csv_name[CLASSIFICATION_MAX_CSV_NAME];
    char csv_path[CLASSIFICATION_MAX_CSV_PATH];

    CLASSIFICATION_make_filename_safe(Global_Classification_Field_Text[CLASSIFICATION_FIELD_SIGNAL_NAME],
                                      safe_signal_name,
                                      sizeof(safe_signal_name));

    CLASSIFICATION_get_signal_datetime(signal_datetime, sizeof(signal_datetime));

    int csv_name_written = snprintf(csv_name,
                                    sizeof(csv_name),
                                    "SIGNAL_%.*s_CLASSIFICATION_%.*s.csv",
                                    CLASSIFICATION_MAX_TEXT - 1,
                                    safe_signal_name,
                                    127,
                                    signal_datetime);

    if (csv_name_written < 0 || (size_t)csv_name_written >= sizeof(csv_name)) {
        snprintf(Global_Classification_Status,
                 sizeof(Global_Classification_Status),
                 "Classification CSV name too long");
        return 0;
    }

    int csv_path_written = snprintf(csv_path,
                                    sizeof(csv_path),
                                    "%s/%s",
                                    CLASSIFICATION_OUTPUT_DIR,
                                    csv_name);

    if (csv_path_written < 0 || (size_t)csv_path_written >= sizeof(csv_path)) {
        snprintf(Global_Classification_Status,
                 sizeof(Global_Classification_Status),
                 "Classification CSV path too long");
        return 0;
    }

    FILE *fp = fopen(csv_path, "w");
    if (!fp) {
        snprintf(Global_Classification_Status,
                 sizeof(Global_Classification_Status),
                 "Failed to open Classification CSV");
        return 0;
    }

    fprintf(fp,
            "signal_name,frequency_mhz,bandwidth,start_time,end_time,calculated_modulation,signal_class,notes,file_name\n");

    for (int i = 0; i < CLASSIFICATION_FIELD_COUNT; i++) {
        if (i > 0) fputc(',', fp);
        CLASSIFICATION_csv_escape(fp, Global_Classification_Field_Text[i]);
    }

    fputc('\n', fp);
    fclose(fp);

    snprintf(Global_Classification_Status,
             sizeof(Global_Classification_Status),
             "Classification saved");

    snprintf(Global_Classification_Save_Message,
             sizeof(Global_Classification_Save_Message),
             "Classification saved successfully");

    Global_Classification_Save_Message_Time = SDL_GetTicks64();

    return 1;
}

void CLASSIFICATION_enter_mode(const char *record_dir)
{
    Global_Classification_Mode = 1;
    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
    Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
    Global_Classification_Dropdown_Scroll = 0;
    Global_Classification_Dropdown_Hover = -1;
    Global_Classification_Notes_Cursor = 0;
    Global_Classification_Save_Message[0] = '\0';
    Global_Classification_Save_Message_Time = 0;

    if (record_dir && record_dir[0]) {
        snprintf(Global_Classification_Record_Dir,
                 sizeof(Global_Classification_Record_Dir),
                 "%s",
                 record_dir);
    }

    SDL_StartTextInput();
    CLASSIFICATION_scan_recordings();
}

void CLASSIFICATION_exit_mode(void)
{
    Global_Classification_Mode = 0;
    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
    Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
    Global_Classification_Dropdown_Scroll = 0;
    Global_Classification_Dropdown_Hover = -1;
    Global_Classification_Notes_Cursor = 0;
    /* Keep SDL text input enabled for the main/interception workstation. */
}

int CLASSIFICATION_handle_event(SDL_Event *event, int win_w, int win_h)
{
    if (!event || !Global_Classification_Mode) return 0;

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;

        if (Global_Classification_Active_Field != CLASSIFICATION_FIELD_NONE) {
            if (key == SDLK_BACKSPACE) {
                if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {
                    CLASSIFICATION_backspace_notes_text();
                }
                else {
                    CLASSIFICATION_backspace_text(Global_Classification_Field_Text[Global_Classification_Active_Field]);
                }
            }
            else if (key == SDLK_DELETE) {
                if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {
                    CLASSIFICATION_delete_notes_text();
                }
                else {
                    Global_Classification_Field_Text[Global_Classification_Active_Field][0] = '\0';
                }
            }
            else if (key == SDLK_LEFT && Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {
                CLASSIFICATION_notes_move_horizontal(-1);
            }
            else if (key == SDLK_RIGHT && Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {
                CLASSIFICATION_notes_move_horizontal(1);
            }
            else if (key == SDLK_UP && Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {
                CLASSIFICATION_notes_move_vertical(-1);
            }
            else if (key == SDLK_DOWN && Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {
                CLASSIFICATION_notes_move_vertical(1);
            }
            else if (key == SDLK_TAB) {
                Global_Classification_Active_Field++;
                if (Global_Classification_Active_Field >= CLASSIFICATION_FIELD_COUNT) {
                    Global_Classification_Active_Field = CLASSIFICATION_FIELD_SIGNAL_NAME;
                }

                while (CLASSIFICATION_is_dropdown_field(Global_Classification_Active_Field)) {
                    Global_Classification_Active_Field++;
                    if (Global_Classification_Active_Field >= CLASSIFICATION_FIELD_COUNT) {
                        Global_Classification_Active_Field = CLASSIFICATION_FIELD_SIGNAL_NAME;
                    }
                }
            }
            else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {
                    CLASSIFICATION_insert_notes_text("\n");
                }
                else {
                    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
                }
            }

            return 1;
        }

        if (Global_Classification_Open_Dropdown != CLASSIFICATION_DROPDOWN_NONE) {
            if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
                Global_Classification_Dropdown_Scroll = 0;
                Global_Classification_Dropdown_Hover = -1;
            }
            return 1;
        }

        if (key == SDLK_ESCAPE || key == SDLK_h) {
            CLASSIFICATION_exit_mode();
            return 1;
        }

        if (key == SDLK_g) return 2;

        if (key == SDLK_q) return 1;

        if (key == SDLK_r) {
            CLASSIFICATION_scan_recordings();
            return 1;
        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
            CLASSIFICATION_append_csv_row();
            return 1;
        }

        if (key == SDLK_TAB) {
            Global_Classification_Active_Field = CLASSIFICATION_FIELD_SIGNAL_NAME;
            return 1;
        }

        if (key == SDLK_UP && Global_Classification_File_Count > 0) {
            Global_Classification_Selected_File--;
            if (Global_Classification_Selected_File < 0) {
                Global_Classification_Selected_File = Global_Classification_File_Count - 1;
            }
            CLASSIFICATION_load_selected_file_into_fields();
            return 1;
        }

        if (key == SDLK_DOWN && Global_Classification_File_Count > 0) {
            Global_Classification_Selected_File++;
            if (Global_Classification_Selected_File >= Global_Classification_File_Count) {
                Global_Classification_Selected_File = 0;
            }
            CLASSIFICATION_load_selected_file_into_fields();
            return 1;
        }

        return 1;
    }

    if (event->type == SDL_TEXTINPUT) {
        if (Global_Classification_Active_Field != CLASSIFICATION_FIELD_NONE &&
            !CLASSIFICATION_is_dropdown_field(Global_Classification_Active_Field)) {
            if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {
                CLASSIFICATION_insert_notes_text(event->text.text);
            }
            else {
                CLASSIFICATION_append_text(Global_Classification_Field_Text[Global_Classification_Active_Field],
                                           CLASSIFICATION_MAX_TEXT,
                                           event->text.text);
            }
        }
        return 1;
    }

    if (event->type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);

        SDL_Rect file_rect;
        SDL_Rect field_rects[CLASSIFICATION_FIELD_COUNT];
        CLASSIFICATION_get_layout(win_w, win_h, &file_rect, NULL, field_rects, NULL);

        if (Global_Classification_Open_Dropdown != CLASSIFICATION_DROPDOWN_NONE) {
            int dropdown_field = Global_Classification_Open_Dropdown;
            SDL_Rect base = field_rects[dropdown_field];
            int visible = CLASSIFICATION_dropdown_visible_count(dropdown_field);
            SDL_Rect dropdown_rect = {base.x,
                                      base.y + base.h,
                                      base.w,
                                      visible * CLASSIFICATION_DROPDOWN_OPTION_H};

            if (point_in_rect(mx, my, dropdown_rect) || point_in_rect(mx, my, base)) {
                Global_Classification_Dropdown_Scroll -= event->wheel.y * 3;
                CLASSIFICATION_clamp_dropdown_scroll(dropdown_field);
                return 1;
            }
        }

        if (point_in_rect(mx, my, file_rect)) {
            int visible = (file_rect.h - 58) / CLASSIFICATION_ROW_HEIGHT;
            if (visible < 1) visible = 1;

            Global_Classification_File_Scroll -= event->wheel.y * 3;
            if (Global_Classification_File_Scroll < 0) Global_Classification_File_Scroll = 0;
            if (Global_Classification_File_Scroll + visible > Global_Classification_File_Count) {
                Global_Classification_File_Scroll = Global_Classification_File_Count - visible;
                if (Global_Classification_File_Scroll < 0) Global_Classification_File_Scroll = 0;
            }
        }

        return 1;
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x = event->button.x;
        int y = event->button.y;

        SDL_Rect file_rect;
        SDL_Rect form_rect;
        SDL_Rect field_rects[CLASSIFICATION_FIELD_COUNT];
        SDL_Rect save_rect;
        CLASSIFICATION_get_layout(win_w, win_h, &file_rect, &form_rect, field_rects, &save_rect);
        (void)form_rect;

        if (Global_Classification_Open_Dropdown != CLASSIFICATION_DROPDOWN_NONE) {
            int dropdown_field = Global_Classification_Open_Dropdown;
            int count = CLASSIFICATION_option_count_for_field(dropdown_field);
            int visible = CLASSIFICATION_dropdown_visible_count(dropdown_field);
            SDL_Rect base = field_rects[dropdown_field];

            CLASSIFICATION_clamp_dropdown_scroll(dropdown_field);

            for (int i = 0; i < visible; i++) {
                int option_index = Global_Classification_Dropdown_Scroll + i;
                SDL_Rect option_rect = {base.x,
                                        base.y + base.h + (i * CLASSIFICATION_DROPDOWN_OPTION_H),
                                        base.w,
                                        CLASSIFICATION_DROPDOWN_OPTION_H};

                if (option_index >= count) break;

                if (point_in_rect(x, y, option_rect)) {
                    snprintf(Global_Classification_Field_Text[dropdown_field],
                             CLASSIFICATION_MAX_TEXT,
                             "%s",
                             CLASSIFICATION_option_for_field(dropdown_field, option_index));
                    Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
                    Global_Classification_Dropdown_Scroll = 0;
                    Global_Classification_Dropdown_Hover = -1;
                    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
                    return 1;
                }
            }

            Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
            Global_Classification_Dropdown_Scroll = 0;
            Global_Classification_Dropdown_Hover = -1;
        }

        Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;

        if (point_in_rect(x, y, file_rect)) {
            int row_y = file_rect.y + 44;
            int idx = Global_Classification_File_Scroll + ((y - row_y) / CLASSIFICATION_ROW_HEIGHT);

            if (y >= row_y && idx >= 0 && idx < Global_Classification_File_Count) {
                Global_Classification_Selected_File = idx;
                Global_Classification_Save_Message[0] = '\0';
                Global_Classification_Save_Message_Time = 0;
                CLASSIFICATION_load_selected_file_into_fields();
                snprintf(Global_Classification_Status,
                         sizeof(Global_Classification_Status),
                         "Selected %.220s",
                         Global_Classification_Files[Global_Classification_Selected_File]);
            }
            return 1;
        }

        for (int i = 0; i < CLASSIFICATION_FIELD_COUNT; i++) {
            if (point_in_rect(x, y, field_rects[i])) {
                if (CLASSIFICATION_is_dropdown_field(i)) {
                    if (Global_Classification_Open_Dropdown != i) {
                        Global_Classification_Dropdown_Scroll = 0;
                    }
                    Global_Classification_Open_Dropdown = i;
                    Global_Classification_Dropdown_Hover = -1;
                    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
                }
                else {
                    Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
                    Global_Classification_Active_Field = i;

                    if (i == CLASSIFICATION_FIELD_NOTES) {
                        CLASSIFICATION_set_notes_cursor_from_mouse(field_rects[i], x, y);
                    }
                }
                return 1;
            }
        }

        if (point_in_rect(x, y, save_rect)) {
            CLASSIFICATION_append_csv_row();
            return 1;
        }

        return 1;
    }

    return 1;
}

static void CLASSIFICATION_draw_panel(SDL_Renderer *renderer,
                                      TTF_Font *font,
                                      SDL_Rect rect,
                                      const char *title)
{
    draw_filled_rect(renderer, rect, (SDL_Color){0, 0, 0, 215});
    draw_outline_rect(renderer, rect, (SDL_Color){0, 150, 70, 255});
    draw_text(renderer, font, title, rect.x + 10, rect.y + 12, (SDL_Color){0, 255, 90, 255});
}

static void CLASSIFICATION_draw_selectable_row(SDL_Renderer *renderer,
                                               TTF_Font *font,
                                               SDL_Rect row,
                                               const char *text,
                                               int is_selected)
{
    if (is_selected) {
        draw_filled_rect(renderer, row, (SDL_Color){20, 80, 45, 220});
    }

    char short_text[512];
    CLASSIFICATION_short_text(font, text, short_text, sizeof(short_text), row.w - 12);

    draw_text(renderer,
              font,
              short_text,
              row.x + 6,
              row.y + 4,
              is_selected ? (SDL_Color){230, 230, 230, 255} : (SDL_Color){150, 150, 150, 255});
}

static void CLASSIFICATION_draw_input_field(SDL_Renderer *renderer,
                                            TTF_Font *font,
                                            SDL_Rect rect,
                                            const char *label,
                                            const char *text,
                                            int active,
                                            int dropdown_field,
                                            int field_index)
{
    draw_text(renderer,
              font,
              label,
              rect.x - 240,
              rect.y + 9,
              active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 180, 70, 255});

    draw_filled_rect(renderer, rect, (SDL_Color){0, 8, 3, 255});
    draw_outline_rect(renderer,
                      rect,
                      active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 120, 50, 255});

    char shown[CLASSIFICATION_MAX_TEXT + 64];

    if (text && text[0]) {
        snprintf(shown, sizeof(shown), "%.*s%s", CLASSIFICATION_MAX_TEXT - 2, text, active ? "_" : "");
    }
    else if (dropdown_field) {
        snprintf(shown, sizeof(shown), "Click to select");
    }
    else {
        snprintf(shown, sizeof(shown), "%s", active ? "_" : "Click to type");
    }

    if (field_index == CLASSIFICATION_FIELD_NOTES) {
        CLASSIFICATION_draw_multiline_notes(renderer, font, rect, text, active);
        return;
    }

    char short_value[CLASSIFICATION_MAX_TEXT + 64];

    CLASSIFICATION_short_text(font, shown, short_value, sizeof(short_value), rect.w - 34);

    draw_text(renderer,
              font,
              short_value,
              rect.x + 9,
              rect.y + 9,
              (text && text[0]) ?
              (SDL_Color){230, 230, 230, 255} :
              (SDL_Color){120, 150, 130, 255});

    if (dropdown_field) {
        draw_text(renderer,
                  font,
                  "v",
                  rect.x + rect.w - 22,
                  rect.y + 9,
                  (SDL_Color){0, 220, 80, 255});
    }
}

static void CLASSIFICATION_draw_dropdown(SDL_Renderer *renderer,
                                         TTF_Font *font,
                                         SDL_Rect field_rect,
                                         int field)
{
    int count = CLASSIFICATION_option_count_for_field(field);
    if (count <= 0) return;

    CLASSIFICATION_clamp_dropdown_scroll(field);

    int visible = CLASSIFICATION_dropdown_visible_count(field);

    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect dropdown_bg = {
        field_rect.x,
        field_rect.y + field_rect.h,
        field_rect.w,
        visible * CLASSIFICATION_DROPDOWN_OPTION_H
    };

    draw_filled_rect(renderer, dropdown_bg, (SDL_Color){0, 0, 0, 245});
    draw_outline_rect(renderer, dropdown_bg, (SDL_Color){0, 180, 70, 255});

    Global_Classification_Dropdown_Hover = -1;

    for (int i = 0; i < visible; i++) {
        int option_index = Global_Classification_Dropdown_Scroll + i;
        if (option_index >= count) break;

        SDL_Rect option_rect = {field_rect.x,
                                field_rect.y + field_rect.h + (i * CLASSIFICATION_DROPDOWN_OPTION_H),
                                field_rect.w,
                                CLASSIFICATION_DROPDOWN_OPTION_H};

        int selected = strcmp(Global_Classification_Field_Text[field],
                              CLASSIFICATION_option_for_field(field, option_index)) == 0;
        int hovered = point_in_rect(mouse_x, mouse_y, option_rect);

        if (hovered) {
            Global_Classification_Dropdown_Hover = option_index;

            SDL_Rect glow_outer = {
                option_rect.x - 3,
                option_rect.y - 2,
                option_rect.w + 6,
                option_rect.h + 4
            };

            draw_filled_rect(renderer, glow_outer, (SDL_Color){0, 255, 90, 38});
        }

        draw_filled_rect(renderer,
                         option_rect,
                         hovered ?
                         (SDL_Color){0, 70, 30, 250} :
                         selected ?
                         (SDL_Color){15, 85, 45, 245} :
                         (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer,
                          option_rect,
                          hovered ?
                          (SDL_Color){0, 255, 90, 255} :
                          selected ?
                          (SDL_Color){0, 220, 80, 255} :
                          (SDL_Color){0, 130, 55, 255});
        draw_text(renderer,
                  font,
                  CLASSIFICATION_option_for_field(field, option_index),
                  option_rect.x + 9,
                  option_rect.y + 6,
                  hovered ?
                  (SDL_Color){235, 255, 240, 255} :
                  selected ?
                  (SDL_Color){255, 255, 255, 255} :
                  (SDL_Color){190, 220, 195, 255});
    }

    if (count > visible) {
        int track_h = dropdown_bg.h - 8;
        int scroll_x = dropdown_bg.x + dropdown_bg.w - 8;
        int scroll_y = dropdown_bg.y + 4;
        int scroll_w = 4;

        int thumb_h = (visible * track_h) / count;
        if (thumb_h < 18) thumb_h = 18;
        if (thumb_h > track_h) thumb_h = track_h;

        int max_scroll = count - visible;
        int thumb_y = scroll_y;

        if (max_scroll > 0) {
            thumb_y = scroll_y +
                      (Global_Classification_Dropdown_Scroll * (track_h - thumb_h)) /
                      max_scroll;
        }

        SDL_Rect track = {scroll_x, scroll_y, scroll_w, track_h};
        SDL_Rect thumb = {scroll_x - 1, thumb_y, scroll_w + 2, thumb_h};

        draw_filled_rect(renderer, track, (SDL_Color){0, 60, 25, 180});
        draw_filled_rect(renderer, thumb, (SDL_Color){0, 255, 90, 220});
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void CLASSIFICATION_draw_save_button(SDL_Renderer *renderer,
                                            TTF_Font *font,
                                            SDL_Rect rect,
                                            int hovered)
{
    SDL_Color fill = hovered ?
                     (SDL_Color){0, 85, 32, 255} :
                     (SDL_Color){0, 28, 10, 255};

    SDL_Color border = hovered ?
                       (SDL_Color){0, 255, 90, 255} :
                       (SDL_Color){0, 180, 60, 255};

    SDL_Color text = hovered ?
                     (SDL_Color){235, 255, 240, 255} :
                     (SDL_Color){0, 255, 90, 255};

    if (hovered) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        SDL_Rect glow_outer = {rect.x - 8, rect.y - 8, rect.w + 16, rect.h + 16};
        SDL_Rect glow_inner = {rect.x - 4, rect.y - 4, rect.w + 8, rect.h + 8};

        draw_filled_rect(renderer, glow_outer, (SDL_Color){0, 255, 90, 32});
        draw_filled_rect(renderer, glow_inner, (SDL_Color){0, 255, 90, 55});

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);

    int text_w = 0;
    int text_h = 0;

    if (font && TTF_SizeText(font, "Save CSV", &text_w, &text_h) != 0) {
        text_w = 0;
        text_h = 0;
    }

    draw_text(renderer,
              font,
              "Save CSV",
              rect.x + (rect.w - text_w) / 2,
              rect.y + (rect.h - text_h) / 2,
              text);
}

void CLASSIFICATION_draw_workstation(SDL_Renderer *renderer,
                                     TTF_Font *font,
                                     int win_w,
                                     int win_h)
{
    if (!renderer || !font) return;

    SDL_Rect full = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, full, (SDL_Color){0, 0, 0, 255});

    SDL_Rect file_rect;
    SDL_Rect form_rect;
    SDL_Rect field_rects[CLASSIFICATION_FIELD_COUNT];
    SDL_Rect save_rect;
    CLASSIFICATION_get_layout(win_w, win_h, &file_rect, &form_rect, field_rects, &save_rect);

    draw_text(renderer,
              font,
              "Signal Classification Workstation  |  H/Esc exits unless typing  |  R rescans  |  Enter saves when not typing",
              CLASSIFICATION_MARGIN,
              CLASSIFICATION_MARGIN,
              (SDL_Color){0, 255, 90, 255});

    draw_text(renderer,
              font,
              Global_Classification_Status,
              CLASSIFICATION_MARGIN,
              CLASSIFICATION_MARGIN + 26,
              (SDL_Color){150, 150, 150, 255});

    CLASSIFICATION_draw_panel(renderer, font, file_rect, "Recording Files");
    CLASSIFICATION_draw_panel(renderer, font, form_rect, "Signal Classification Fields");

    int visible_files = (file_rect.h - 58) / CLASSIFICATION_ROW_HEIGHT;
    if (visible_files < 1) visible_files = 1;

    if (Global_Classification_File_Scroll + visible_files > Global_Classification_File_Count) {
        Global_Classification_File_Scroll = Global_Classification_File_Count - visible_files;
        if (Global_Classification_File_Scroll < 0) Global_Classification_File_Scroll = 0;
    }

    for (int i = 0; i < visible_files; i++) {
        int idx = Global_Classification_File_Scroll + i;
        if (idx >= Global_Classification_File_Count) break;

        SDL_Rect row = {file_rect.x + 6,
                        file_rect.y + 44 + (i * CLASSIFICATION_ROW_HEIGHT),
                        file_rect.w - 12,
                        CLASSIFICATION_ROW_HEIGHT - 2};

        CLASSIFICATION_draw_selectable_row(renderer,
                                           font,
                                           row,
                                           Global_Classification_Files[idx],
                                           idx == Global_Classification_Selected_File);
    }

    for (int i = 0; i < CLASSIFICATION_FIELD_COUNT; i++) {
        CLASSIFICATION_draw_input_field(renderer,
                                        font,
                                        field_rects[i],
                                        CLASSIFICATION_FIELD_LABELS[i],
                                        Global_Classification_Field_Text[i],
                                        i == Global_Classification_Active_Field ||
                                        i == Global_Classification_Open_Dropdown,
                                        CLASSIFICATION_is_dropdown_field(i),
                                        i);
    }

    draw_text(renderer,
              font,
              "Output: Classification/SIGNAL_<Signal Name>_CLASSIFICATION_<Signal Date Time>.csv",
              form_rect.x + 16,
              save_rect.y - 32,
              (SDL_Color){0, 255, 90, 255});

    if (Global_Classification_Open_Dropdown != CLASSIFICATION_DROPDOWN_NONE) {
        CLASSIFICATION_draw_dropdown(renderer,
                                     font,
                                     field_rects[Global_Classification_Open_Dropdown],
                                     Global_Classification_Open_Dropdown);
    }

    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    int save_hovered = point_in_rect(mouse_x, mouse_y, save_rect);

    CLASSIFICATION_draw_save_button(renderer, font, save_rect, save_hovered);

    if (Global_Classification_Save_Message[0]) {
        Uint64 now = SDL_GetTicks64();

        if (Global_Classification_Save_Message_Time == 0 ||
            now - Global_Classification_Save_Message_Time <= 3000) {

            int msg_w = 0;
            int msg_h = 0;

            if (font && TTF_SizeText(font,
                                     Global_Classification_Save_Message,
                                     &msg_w,
                                     &msg_h) != 0) {
                msg_w = 0;
                msg_h = 0;
            }

            int msg_x = save_rect.x + (save_rect.w - msg_w) / 2;
            int msg_y = save_rect.y - msg_h - 10;

            if (msg_x < form_rect.x + 16) msg_x = form_rect.x + 16;
            if (msg_x + msg_w > form_rect.x + form_rect.w - 16) {
                msg_x = form_rect.x + form_rect.w - 16 - msg_w;
            }
            if (msg_y < form_rect.y + 36) msg_y = form_rect.y + 36;

            draw_text(renderer,
                      font,
                      Global_Classification_Save_Message,
                      msg_x,
                      msg_y,
                      (SDL_Color){0, 255, 90, 255});
        }

        else {
            Global_Classification_Save_Message[0] = '\0';
            Global_Classification_Save_Message_Time = 0;
        }
    }
}
