#define _GNU_SOURCE
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
#include "DataStore.h"
#include "GUIs.h"

#include <SDL2/SDL_image.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__GNUC__) || defined(__clang__)
#define RETROSPECTRUM_UNUSED __attribute__((unused))
#else
#define RETROSPECTRUM_UNUSED
#endif

#define CLASSIFICATION_MAX_FILES 512
#define CLASSIFICATION_MAX_PATH 1024
#define CLASSIFICATION_MAX_TEXT 512
#define CLASSIFICATION_MAX_FILE_PATH (CLASSIFICATION_MAX_PATH + 512 + 2)
#define CLASSIFICATION_MAX_CSV_NAME 768
#define CLASSIFICATION_ROW_HEIGHT 24
#define CLASSIFICATION_MARGIN 20
#define CLASSIFICATION_DROPDOWN_NONE -1
#define CLASSIFICATION_DROPDOWN_OPTION_H 28
#define CLASSIFICATION_DROPDOWN_MAX_VISIBLE 9
#define CLASSIFICATION_NOTES_LINE_H 19
#define CLASSIFICATION_COUNTRY_MAX_VISIBLE 7
#define CLASSIFICATION_COUNTRY_OPTION_H 42
#define CLASSIFICATION_CASE_MAX_VISIBLE 7
#define CLASSIFICATION_CASE_OPTION_H 32
#define CLASSIFICATION_FILE_SEARCH_TEXT_MAX 256
#define CLASSIFICATION_FILE_SEARCH_ROW_H 34

#ifndef RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#endif

static void CLASSIFICATION_get_adjusted_mouse_state(int *x, int *y) {
    /*
        Purpose: Gets the adjusted mouse state
        Returns: No value
    */

    SDL_GetMouseState(x, y);

    if (y) {

        *y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

    }
}

enum {
    CLASSIFICATION_FIELD_NONE = -1,
    CLASSIFICATION_FIELD_CASE_NUMBER = 0,
    CLASSIFICATION_FIELD_SIGNAL_NAME,
    CLASSIFICATION_FIELD_FREQUENCY_MHZ,
    CLASSIFICATION_FIELD_BANDWIDTH,
    CLASSIFICATION_FIELD_START_TIME,
    CLASSIFICATION_FIELD_END_TIME,
    CLASSIFICATION_FIELD_CALCULATED_MODULATION,
    CLASSIFICATION_FIELD_SIGNAL_CLASS,
    CLASSIFICATION_FIELD_COUNTRY,
    CLASSIFICATION_FIELD_LATITUDE,
    CLASSIFICATION_FIELD_LONGITUDE,
    CLASSIFICATION_FIELD_NOTES,
    CLASSIFICATION_FIELD_FILE_NAME,
    CLASSIFICATION_FIELD_COUNT
};

int Global_Classification_Mode = 0;

static char Global_Classification_Record_Dir[CLASSIFICATION_MAX_PATH] = "Recordings";
static char Global_Classification_Files[CLASSIFICATION_MAX_FILES][512];
static int Global_Classification_File_Count = 0;
static int Global_Classification_Selected_File = 0;
static int Global_Classification_File_Scroll = 0;
static int Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
static int Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
static int Global_Classification_Dropdown_Scroll = 0;
static int Global_Classification_Dropdown_Hover = -1;
static int Global_Classification_Country_Scroll = 0;
static int Global_Classification_Country_Hover = -1;
static char Global_Classification_Case_Options[CLASSIFICATION_MAX_FILES][128];
static int Global_Classification_Case_Count = 0;
static int Global_Classification_Case_Scroll = 0;
static int Global_Classification_Case_Hover = -1;
static int Global_Classification_Notes_Cursor = 0;
static int Global_Classification_Notes_Selecting = 0;
static int Global_Classification_Notes_Selection_Start = -1;
static int Global_Classification_Notes_Selection_End = -1;
static TTF_Font *Global_Classification_Notes_Font = NULL;
static int Global_Classification_Notes_Wrap_Px = 0;
static int Global_Classification_File_Search_Open = 0;
static int Global_Classification_File_Search_Active = 0;
static int Global_Classification_File_Search_Cursor = 0;
static int Global_Classification_File_Search_Scroll = 0;
static int Global_Classification_File_Search_Hover = -1;
static char Global_Classification_File_Search_Text[CLASSIFICATION_FILE_SEARCH_TEXT_MAX] = "";
static char Global_Classification_Status[512] = "Press R to scan recordings";
static char Global_Classification_Save_Message[512] = "";
static Uint64 Global_Classification_Save_Message_Time = 0;

static int CLASSIFICATION_name_compare(const void *a, const void *b);
static int CLASSIFICATION_handle_file_search_event(SDL_Event *event, int win_w, int win_h);
static void CLASSIFICATION_draw_file_search_button(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static void CLASSIFICATION_draw_file_search_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static SDL_Rect CLASSIFICATION_file_search_button_rect(int win_w, int win_h);
static void CLASSIFICATION_open_file_search_menu(void);

static char Global_Classification_Field_Text[CLASSIFICATION_FIELD_COUNT][CLASSIFICATION_MAX_TEXT] = {
    "", "", "", "", "", "", "Unknown", "Unknown", "", "", "", "", ""};

static const char *CLASSIFICATION_FIELD_LABELS[CLASSIFICATION_FIELD_COUNT] = {
    "Case #",       "Signal Name", "Frequency MHz", "Bandwidth", "Start Time", "End Time", "Calculated Modulation",
    "Signal Class", "Country",     "Latitude",      "Longitude", "Notes",      "File Name"};

static const char *CLASSIFICATION_MODULATION_OPTIONS[] = {
    "Unknown",    "AM-like",    "ASK-like",  "OOK-like",     "FM-like",    "FSK-like",         "GFSK-like",
    "MSK-like",   "GMSK-like",  "PSK-like",  "BPSK-like",    "QPSK-like",  "8PSK-like",        "QAM-like",
    "16QAM-like", "64QAM-like", "OFDM-like", "DSSS-like",    "FHSS-like",  "Chirp-like",       "CSS / LoRa-like",
    "Pulse-like", "PPM-like",   "PWM-like",  "CW / Carrier", "Noise-like", "Wideband Digital", "Narrowband Digital"};

static const char *CLASSIFICATION_SIGNAL_CLASS_OPTIONS[] = {"Unknown",
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
                                                            "Test Signal"};

typedef struct Type_Classification_Country_Option {
    const char *name;
    const char *alpha2;
} Type_Classification_Country_Option;

static const Type_Classification_Country_Option CLASSIFICATION_COUNTRIES[] = {
    {"Afghanistan", "af"},
    {"Albania", "al"},
    {"Algeria", "dz"},
    {"American Samoa", "as"},
    {"Andorra", "ad"},
    {"Angola", "ao"},
    {"Anguilla", "ai"},
    {"Antarctica", "aq"},
    {"Antigua and Barbuda", "ag"},
    {"Argentina", "ar"},
    {"Armenia", "am"},
    {"Aruba", "aw"},
    {"Australia", "au"},
    {"Austria", "at"},
    {"Azerbaijan", "az"},
    {"Bahamas", "bs"},
    {"Bahrain", "bh"},
    {"Bangladesh", "bd"},
    {"Barbados", "bb"},
    {"Belarus", "by"},
    {"Belgium", "be"},
    {"Belize", "bz"},
    {"Benin", "bj"},
    {"Bermuda", "bm"},
    {"Bhutan", "bt"},
    {"Bolivia", "bo"},
    {"Bonaire, Sint Eustatius and Saba", "bq"},
    {"Bosnia and Herzegovina", "ba"},
    {"Botswana", "bw"},
    {"Bouvet Island", "bv"},
    {"Brazil", "br"},
    {"British Indian Ocean Territory", "io"},
    {"Brunei", "bn"},
    {"Bulgaria", "bg"},
    {"Burkina Faso", "bf"},
    {"Burundi", "bi"},
    {"Cambodia", "kh"},
    {"Cameroon", "cm"},
    {"Canada", "ca"},
    {"Cape Verde", "cv"},
    {"Cayman Islands", "ky"},
    {"Central African Republic", "cf"},
    {"Chad", "td"},
    {"Chile", "cl"},
    {"China", "cn"},
    {"Christmas Island", "cx"},
    {"Cocos (Keeling) Islands", "cc"},
    {"Colombia", "co"},
    {"Comoros", "km"},
    {"Cook Islands", "ck"},
    {"Costa Rica", "cr"},
    {"Cote dIvoire", "ci"},
    {"Croatia", "hr"},
    {"Cuba", "cu"},
    {"Curaçao", "cw"},
    {"Cyprus", "cy"},
    {"Czech Republic", "cz"},
    {"Democratic Republic of the Congo", "cd"},
    {"Denmark", "dk"},
    {"Djibouti", "dj"},
    {"Dominica", "dm"},
    {"Dominican Republic", "do"},
    {"Ecuador", "ec"},
    {"Egypt", "eg"},
    {"El Salvador", "sv"},
    {"Equatorial Guinea", "gq"},
    {"Eritrea", "er"},
    {"Estonia", "ee"},
    {"Eswatini", "sz"},
    {"Ethiopia", "et"},
    {"Falkland Islands (Malvinas)", "fk"},
    {"Faroe Islands", "fo"},
    {"Fiji", "fj"},
    {"Finland", "fi"},
    {"France", "fr"},
    {"French Guiana", "gf"},
    {"French Polynesia", "pf"},
    {"French Southern Territories", "tf"},
    {"Gabon", "ga"},
    {"Gambia", "gm"},
    {"Georgia", "ge"},
    {"Germany", "de"},
    {"Ghana", "gh"},
    {"Gibraltar", "gi"},
    {"Greece", "gr"},
    {"Greenland", "gl"},
    {"Grenada", "gd"},
    {"Guadeloupe", "gp"},
    {"Guam", "gu"},
    {"Guatemala", "gt"},
    {"Guernsey", "gg"},
    {"Guinea", "gn"},
    {"Guinea-Bissau", "gw"},
    {"Guyana", "gy"},
    {"Haiti", "ht"},
    {"Heard Island and McDonald Islands", "hm"},
    {"Honduras", "hn"},
    {"Hong Kong", "hk"},
    {"Hungary", "hu"},
    {"Iceland", "is"},
    {"India", "in"},
    {"Indonesia", "id"},
    {"Iran", "ir"},
    {"Iraq", "iq"},
    {"Ireland", "ie"},
    {"Isle of Man", "im"},
    {"Israel", "il"},
    {"Italy", "it"},
    {"Jamaica", "jm"},
    {"Japan", "jp"},
    {"Jersey", "je"},
    {"Jordan", "jo"},
    {"Kazakhstan", "kz"},
    {"Kenya", "ke"},
    {"Kiribati", "ki"},
    {"Kuwait", "kw"},
    {"Kyrgyzstan", "kg"},
    {"Laos", "la"},
    {"Latvia", "lv"},
    {"Lebanon", "lb"},
    {"Lesotho", "ls"},
    {"Liberia", "lr"},
    {"Libya", "ly"},
    {"Liechtenstein", "li"},
    {"Lithuania", "lt"},
    {"Luxembourg", "lu"},
    {"Macao", "mo"},
    {"Madagascar", "mg"},
    {"Malawi", "mw"},
    {"Malaysia", "my"},
    {"Maldives", "mv"},
    {"Mali", "ml"},
    {"Malta", "mt"},
    {"Marshall Islands", "mh"},
    {"Martinique", "mq"},
    {"Mauritania", "mr"},
    {"Mauritius", "mu"},
    {"Mayotte", "yt"},
    {"Mexico", "mx"},
    {"Micronesia", "fm"},
    {"Moldova", "md"},
    {"Monaco", "mc"},
    {"Mongolia", "mn"},
    {"Montenegro", "me"},
    {"Montserrat", "ms"},
    {"Morocco", "ma"},
    {"Mozambique", "mz"},
    {"Myanmar", "mm"},
    {"Namibia", "na"},
    {"Nauru", "nr"},
    {"Nepal", "np"},
    {"Netherlands", "nl"},
    {"New Caledonia", "nc"},
    {"New Zealand", "nz"},
    {"Nicaragua", "ni"},
    {"Niger", "ne"},
    {"Nigeria", "ng"},
    {"Niue", "nu"},
    {"Norfolk Island", "nf"},
    {"North Korea", "kp"},
    {"North Macedonia", "mk"},
    {"Northern Mariana Islands", "mp"},
    {"Norway", "no"},
    {"Oman", "om"},
    {"Pakistan", "pk"},
    {"Palau", "pw"},
    {"Palestine", "ps"},
    {"Panama", "pa"},
    {"Papua New Guinea", "pg"},
    {"Paraguay", "py"},
    {"Peru", "pe"},
    {"Philippines", "ph"},
    {"Pitcairn", "pn"},
    {"Poland", "pl"},
    {"Portugal", "pt"},
    {"Puerto Rico", "pr"},
    {"Qatar", "qa"},
    {"Republic of the Congo", "cg"},
    {"Romania", "ro"},
    {"Russia", "ru"},
    {"Rwanda", "rw"},
    {"Réunion", "re"},
    {"Saint Barthélemy", "bl"},
    {"Saint Helena, Ascension and Tristan da Cunha", "sh"},
    {"Saint Kitts and Nevis", "kn"},
    {"Saint Lucia", "lc"},
    {"Saint Martin (French part)", "mf"},
    {"Saint Pierre and Miquelon", "pm"},
    {"Saint Vincent and the Grenadines", "vc"},
    {"Samoa", "ws"},
    {"San Marino", "sm"},
    {"Sao Tome and Principe", "st"},
    {"Saudi Arabia", "sa"},
    {"Senegal", "sn"},
    {"Serbia", "rs"},
    {"Seychelles", "sc"},
    {"Sierra Leone", "sl"},
    {"Singapore", "sg"},
    {"Sint Maarten (Dutch part)", "sx"},
    {"Slovakia", "sk"},
    {"Slovenia", "si"},
    {"Solomon Islands", "sb"},
    {"Somalia", "so"},
    {"South Africa", "za"},
    {"South Georgia and the South Sandwich Islands", "gs"},
    {"South Korea", "kr"},
    {"South Sudan", "ss"},
    {"Spain", "es"},
    {"Sri Lanka", "lk"},
    {"Sudan", "sd"},
    {"Suriname", "sr"},
    {"Svalbard and Jan Mayen", "sj"},
    {"Sweden", "se"},
    {"Switzerland", "ch"},
    {"Syria", "sy"},
    {"Taiwan", "tw"},
    {"Tajikistan", "tj"},
    {"Tanzania", "tz"},
    {"Thailand", "th"},
    {"Timor-Leste", "tl"},
    {"Togo", "tg"},
    {"Tokelau", "tk"},
    {"Tonga", "to"},
    {"Trinidad and Tobago", "tt"},
    {"Tunisia", "tn"},
    {"Turkmenistan", "tm"},
    {"Turks and Caicos Islands", "tc"},
    {"Tuvalu", "tv"},
    {"Türkiye", "tr"},
    {"Uganda", "ug"},
    {"Ukraine", "ua"},
    {"United Arab Emirates", "ae"},
    {"United Kingdom", "gb"},
    {"United States", "us"},
    {"United States Minor Outlying Islands", "um"},
    {"Uruguay", "uy"},
    {"Uzbekistan", "uz"},
    {"Vanuatu", "vu"},
    {"Vatican City", "va"},
    {"Venezuela", "ve"},
    {"Vietnam", "vn"},
    {"Virgin Islands, British", "vg"},
    {"Virgin Islands, U.S.", "vi"},
    {"Wallis and Futuna", "wf"},
    {"Western Sahara", "eh"},
    {"Yemen", "ye"},
    {"Zambia", "zm"},
    {"Zimbabwe", "zw"},
    {"Åland Islands", "ax"},
};

static SDL_Texture *Global_Classification_Flag_Texture = NULL;
static char Global_Classification_Flag_Alpha2[8] = "";

static int CLASSIFICATION_country_count(void) {
    /*
        Purpose: Counts the country
        Returns: Item count
    */

    return (int)(sizeof(CLASSIFICATION_COUNTRIES) / sizeof(CLASSIFICATION_COUNTRIES[0]));
}

static int CLASSIFICATION_char_lower(int c) {
    /*
        Purpose: Converts a character to lowercase
        Returns: Lowercase character
    */

    return tolower((unsigned char)c);
}

static int CLASSIFICATION_text_contains_ci(const char *haystack, const char *needle) {
    /*
        Purpose: Checks whether the text case-insensitively contains a value
        Returns: Boolean status
    */

    if (!haystack || !needle) {

        return 0;

    }

    if (!needle[0]) {

        return 1;

    }

    for (const char *h = haystack; *h; h++) {
        const char *a = h;
        const char *b = needle;

        while (*a && *b && CLASSIFICATION_char_lower(*a) == CLASSIFICATION_char_lower(*b)) {
            a++;
            b++;
        }

        if (!*b) {

            return 1;

        }
    }

    return 0;
}

static int CLASSIFICATION_text_equals_ci(const char *a, const char *b) {
    /*
        Purpose: Checks whether the text case-insensitively values are equal
        Returns: Boolean status
    */

    if (!a || !b) {

        return 0;

    }

    while (*a && *b) {

        if (CLASSIFICATION_char_lower(*a) != CLASSIFICATION_char_lower(*b)) {

            return 0;

        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static void CLASSIFICATION_trim_text(char *text) {
    /*
        Purpose: Trims the text
        Returns: No value
    */

    if (!text) {

        return;

    }

    char *start = text;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {

        memmove(text, start, strlen(start) + 1);

    }

    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[len - 1] = '\0';
        len--;
    }
}

static int CLASSIFICATION_case_option_exists(const char *case_number) {
    /*
        Purpose: Checks whether the case option exists
        Returns: Boolean status
    */

    if (!case_number || !case_number[0]) {

        return 1;

    }

    for (int i = 0; i < Global_Classification_Case_Count; i++) {

        if (CLASSIFICATION_text_equals_ci(Global_Classification_Case_Options[i], case_number)) {

            return 1;

        }
    }

    return 0;
}

static void CLASSIFICATION_add_case_option(const char *case_number) {
    /*
        Purpose: Adds the case option
        Returns: No value
    */

    if (!case_number || !case_number[0]) {

        return;

    }

    if (Global_Classification_Case_Count >= CLASSIFICATION_MAX_FILES) {

        return;

    }

    if (CLASSIFICATION_case_option_exists(case_number)) {

        return;

    }

    snprintf(Global_Classification_Case_Options[Global_Classification_Case_Count],
             sizeof(Global_Classification_Case_Options[Global_Classification_Case_Count]), "%s", case_number);
    Global_Classification_Case_Count++;
}

static void CLASSIFICATION_scan_case_files(void) {
    /*
        Purpose: Scans the case files
        Returns: No value
    */

    static Type_DataStore_Document_Summary stored[CLASSIFICATION_MAX_FILES];
    size_t stored_count = 0;
    char database_error[256] = "";

    Global_Classification_Case_Count = 0;
    Global_Classification_Case_Scroll = 0;
    Global_Classification_Case_Hover = -1;

    if (!DATASTORE_list_documents(DATASTORE_KIND_CLASSIFICATION, stored, sizeof(stored) / sizeof(stored[0]),
                                  &stored_count, database_error, sizeof(database_error))) {

        snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
                 "Unable to list database classifications: %.180s", database_error);
        return;

    }

    for (size_t i = 0; i < stored_count && Global_Classification_Case_Count < CLASSIFICATION_MAX_FILES; i++) {

        if (stored[i].case_number[0]) {

            CLASSIFICATION_add_case_option(stored[i].case_number);

        }
    }

    stored_count = 0;
    database_error[0] = '\0';

    if (DATASTORE_list_documents(DATASTORE_KIND_CASE_MANAGEMENT, stored, sizeof(stored) / sizeof(stored[0]),
                                 &stored_count, database_error, sizeof(database_error))) {

        for (size_t i = 0; i < stored_count && Global_Classification_Case_Count < CLASSIFICATION_MAX_FILES; i++) {

            if (stored[i].case_number[0]) {

                CLASSIFICATION_add_case_option(stored[i].case_number);

            }
        }

    }

    qsort(Global_Classification_Case_Options, (size_t)Global_Classification_Case_Count,
          sizeof(Global_Classification_Case_Options[0]), CLASSIFICATION_name_compare);
}

static int CLASSIFICATION_build_case_matches(int *matches, int max_matches) {
    /*
        Purpose: Checks whether the build case matches the requested data
        Returns: Boolean status
    */

    if (!matches || max_matches <= 0) {

        return 0;

    }

    const char *query = Global_Classification_Field_Text[CLASSIFICATION_FIELD_CASE_NUMBER];
    int out = 0;

    for (int i = 0; i < Global_Classification_Case_Count && out < max_matches; i++) {

        if (!query || !query[0] || CLASSIFICATION_text_contains_ci(Global_Classification_Case_Options[i], query)) {

            matches[out++] = i;

        }
    }

    return out;
}

static void CLASSIFICATION_select_case(int case_index) {
    /*
        Purpose: Selects the case
        Returns: No value
    */

    if (case_index < 0 || case_index >= Global_Classification_Case_Count) {

        return;

    }

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_CASE_NUMBER], CLASSIFICATION_MAX_TEXT, "%s",
             Global_Classification_Case_Options[case_index]);

    Global_Classification_Case_Scroll = 0;
    Global_Classification_Case_Hover = -1;
    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
}

static const Type_Classification_Country_Option *CLASSIFICATION_find_country_exact(const char *text) {
    /*
        Purpose: Finds the country exact
        Returns: Matching option pointer
    */

    if (!text || !text[0]) {

        return NULL;

    }

    int count = CLASSIFICATION_country_count();
    for (int i = 0; i < count; i++) {

        if (CLASSIFICATION_text_equals_ci(text, CLASSIFICATION_COUNTRIES[i].name)) {

            return &CLASSIFICATION_COUNTRIES[i];

        }

        if (CLASSIFICATION_text_equals_ci(text, CLASSIFICATION_COUNTRIES[i].alpha2)) {

            return &CLASSIFICATION_COUNTRIES[i];

        }
    }

    return NULL;
}

static int CLASSIFICATION_build_country_matches(int *matches, int max_matches) {
    /*
        Purpose: Checks whether the build country matches the requested data
        Returns: Boolean status
    */

    if (!matches || max_matches <= 0) {

        return 0;

    }

    const char *query = Global_Classification_Field_Text[CLASSIFICATION_FIELD_COUNTRY];
    int count = CLASSIFICATION_country_count();
    int out = 0;

    for (int i = 0; i < count && out < max_matches; i++) {

        if (!query || !query[0] || CLASSIFICATION_text_contains_ci(CLASSIFICATION_COUNTRIES[i].name, query) ||
            CLASSIFICATION_text_contains_ci(CLASSIFICATION_COUNTRIES[i].alpha2, query)) {

            matches[out++] = i;

        }
    }

    return out;
}

static void CLASSIFICATION_select_country(int country_index) {
    /*
        Purpose: Selects the country
        Returns: No value
    */

    if (country_index < 0 || country_index >= CLASSIFICATION_country_count()) {

        return;

    }

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_COUNTRY], CLASSIFICATION_MAX_TEXT, "%s",
             CLASSIFICATION_COUNTRIES[country_index].name);

    Global_Classification_Country_Scroll = 0;
    Global_Classification_Country_Hover = -1;
    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
}

static SDL_Texture *CLASSIFICATION_get_flag_texture(SDL_Renderer *renderer, const char *alpha2) {
    /*
        Purpose: Gets the flag texture
        Returns: Texture pointer
    */

    if (!renderer || !alpha2 || !alpha2[0]) {

        return NULL;

    }

    if (Global_Classification_Flag_Texture && strcmp(Global_Classification_Flag_Alpha2, alpha2) == 0) {

        return Global_Classification_Flag_Texture;

    }

    if (Global_Classification_Flag_Texture) {

        SDL_DestroyTexture(Global_Classification_Flag_Texture);
        Global_Classification_Flag_Texture = NULL;
        Global_Classification_Flag_Alpha2[0] = '\0';

    }

    char path[64];
    snprintf(path, sizeof(path), "flags/%s.png", alpha2);

    Global_Classification_Flag_Texture = IMG_LoadTexture(renderer, path);

    if (Global_Classification_Flag_Texture) {

        snprintf(Global_Classification_Flag_Alpha2, sizeof(Global_Classification_Flag_Alpha2), "%s", alpha2);

    }

    return Global_Classification_Flag_Texture;
}

static void CLASSIFICATION_draw_flag_box(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *alpha2) {
    /*
        Purpose: Draws the flag box
        Returns: No value
    */

    draw_filled_rect(renderer, rect, (SDL_Color){0, 0, 0, 255});
    draw_outline_rect(renderer, rect, (SDL_Color){0, 150, 60, 255});

    SDL_Texture *flag = CLASSIFICATION_get_flag_texture(renderer, alpha2);

    if (flag) {

        SDL_RenderCopy(renderer, flag, NULL, &rect);
        draw_outline_rect(renderer, rect, (SDL_Color){0, 150, 60, 255});
        return;

    }

    if (alpha2 && alpha2[0]) {

        char code[8];
        snprintf(code, sizeof(code), "%s", alpha2);
        for (int i = 0; code[i]; i++) {
            code[i] = (char)toupper((unsigned char)code[i]);
        }
        draw_text(renderer, font, code, rect.x + 6, rect.y + 7, (SDL_Color){0, 255, 90, 255});

    }
}

static int CLASSIFICATION_is_dropdown_field(int field) {
    /*
        Purpose: Checks whether the dropdown is field
        Returns: Boolean status
    */

    return field == CLASSIFICATION_FIELD_CALCULATED_MODULATION || field == CLASSIFICATION_FIELD_SIGNAL_CLASS;
}

static int CLASSIFICATION_option_count_for_field(int field) {
    /*
        Purpose: Counts option options for a field
        Returns: Item count
    */

    if (field == CLASSIFICATION_FIELD_CALCULATED_MODULATION) {

        return (int)(sizeof(CLASSIFICATION_MODULATION_OPTIONS) / sizeof(CLASSIFICATION_MODULATION_OPTIONS[0]));

    }

    if (field == CLASSIFICATION_FIELD_SIGNAL_CLASS) {

        return (int)(sizeof(CLASSIFICATION_SIGNAL_CLASS_OPTIONS) / sizeof(CLASSIFICATION_SIGNAL_CLASS_OPTIONS[0]));

    }

    return 0;
}

static const char *CLASSIFICATION_option_for_field(int field, int index) {
    /*
        Purpose: Gets the option for field
        Returns: Text pointer
    */

    if (field == CLASSIFICATION_FIELD_CALCULATED_MODULATION) {

        int count = CLASSIFICATION_option_count_for_field(field);

        if (index >= 0 && index < count) {

            return CLASSIFICATION_MODULATION_OPTIONS[index];

        }

    }

    if (field == CLASSIFICATION_FIELD_SIGNAL_CLASS) {

        int count = CLASSIFICATION_option_count_for_field(field);

        if (index >= 0 && index < count) {

            return CLASSIFICATION_SIGNAL_CLASS_OPTIONS[index];

        }

    }

    return "";
}

static void CLASSIFICATION_clamp_dropdown_scroll(int field) {
    /*
        Purpose: Clamps the dropdown scroll
        Returns: No value
    */

    int count = CLASSIFICATION_option_count_for_field(field);
    int max_scroll = count - CLASSIFICATION_DROPDOWN_MAX_VISIBLE;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Classification_Dropdown_Scroll < 0) {

        Global_Classification_Dropdown_Scroll = 0;

    }

    if (Global_Classification_Dropdown_Scroll > max_scroll) {

        Global_Classification_Dropdown_Scroll = max_scroll;

    }
}

static int CLASSIFICATION_dropdown_visible_count(int field) {
    /*
        Purpose: Counts the dropdown visible
        Returns: Item count
    */

    int count = CLASSIFICATION_option_count_for_field(field);
    int visible = count;

    if (visible > CLASSIFICATION_DROPDOWN_MAX_VISIBLE) {

        visible = CLASSIFICATION_DROPDOWN_MAX_VISIBLE;

    }

    if (visible < 1) {

        visible = 1;

    }

    return visible;
}

static int CLASSIFICATION_name_compare(const void *a, const void *b) {
    /*
        Purpose: Compares the name
        Returns: Sort order
    */

    const char *sa = (const char *)a;
    const char *sb = (const char *)b;
    return strcmp(sa, sb);
}

static int CLASSIFICATION_is_complex16_file(const char *name) {
    /*
        Purpose: Checks whether the complex16 is file
        Returns: Boolean status
    */

    size_t len = strlen(name);
    const char *suffix = ".complex16";
    size_t suffix_len = strlen(suffix);

    if (len < suffix_len) {

        return 0;

    }
    return strcmp(name + len - suffix_len, suffix) == 0;
}

static void CLASSIFICATION_append_text(char *dst, size_t dst_size, const char *src) {
    /*
        Purpose: Appends the text
        Returns: No value
    */

    if (!dst || !src || dst_size == 0) {

        return;

    }

    size_t used = strlen(dst);

    if (used >= dst_size - 1) {

        return;

    }

    strncat(dst, src, dst_size - used - 1);
}

static void CLASSIFICATION_backspace_text(char *dst) {
    /*
        Purpose: Removes the previous character from the text
        Returns: No value
    */

    if (!dst) {

        return;

    }

    size_t len = strlen(dst);

    if (len > 0) {

        dst[len - 1] = '\0';

    }
}

static int CLASSIFICATION_text_range_width(TTF_Font *font, const char *text, size_t start, size_t end) {
    /*
        Purpose: Calculates the text range width
        Returns: Text width
    */

    char buf[CLASSIFICATION_MAX_TEXT + 8];
    int w = 0;
    int h = 0;

    if (!text || end <= start) {

        return 0;

    }

    if (end - start >= sizeof(buf)) {

        end = start + sizeof(buf) - 1;

    }

    memcpy(buf, text + start, end - start);
    buf[end - start] = '\0';

    if (!font || TTF_SizeText(font, buf, &w, &h) != 0) {

        return (int)(end - start) * 8;

    }

    return w;
}

static void CLASSIFICATION_clamp_notes_cursor(void) {
    /*
        Purpose: Clamps the notes cursor
        Returns: No value
    */

    int len = (int)strlen(Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES]);

    if (Global_Classification_Notes_Cursor < 0) {

        Global_Classification_Notes_Cursor = 0;

    }

    if (Global_Classification_Notes_Cursor > len) {

        Global_Classification_Notes_Cursor = len;

    }
}

static void CLASSIFICATION_clear_notes_selection(void) {
    /*
        Purpose: Clears the notes selection
        Returns: No value
    */

    Global_Classification_Notes_Selecting = 0;
    Global_Classification_Notes_Selection_Start = -1;
    Global_Classification_Notes_Selection_End = -1;
}

static int CLASSIFICATION_notes_selection_range(int *a, int *b) {
    /*
        Purpose: Gets the notes selection range
        Returns: Success status
    */

    int s = Global_Classification_Notes_Selection_Start;
    int e = Global_Classification_Notes_Selection_End;

    if (s < 0 || e < 0 || s == e) {

        return 0;

    }

    if (s > e) {

        int tmp = s;
        s = e;
        e = tmp;

    }

    if (a) {

        *a = s;

    }

    if (b) {

        *b = e;

    }
    return 1;
}

static int CLASSIFICATION_delete_notes_selection(void) {
    /*
        Purpose: Deletes the notes selection
        Returns: Success status
    */

    char *dst = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];
    int a = 0;
    int b = 0;
    int len;

    if (!CLASSIFICATION_notes_selection_range(&a, &b)) {

        return 0;

    }

    len = (int)strlen(dst);

    if (a < 0) {

        a = 0;

    }

    if (b > len) {

        b = len;

    }

    if (a >= b) {

        CLASSIFICATION_clear_notes_selection();
        return 0;

    }

    memmove(dst + a, dst + b, (size_t)(len - b) + 1U);
    Global_Classification_Notes_Cursor = a;
    CLASSIFICATION_clear_notes_selection();
    return 1;
}

static void CLASSIFICATION_auto_wrap_notes_text(void);

static void CLASSIFICATION_insert_notes_text(const char *src) {
    /*
        Purpose: Inserts the notes text
        Returns: No value
    */

    char *dst = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];

    if (!src) {

        return;

    }

    CLASSIFICATION_clamp_notes_cursor();
    CLASSIFICATION_delete_notes_selection();
    CLASSIFICATION_clamp_notes_cursor();

    size_t len = strlen(dst);
    size_t add = strlen(src);

    if (add == 0 || len >= CLASSIFICATION_MAX_TEXT - 1) {

        return;

    }

    if (add > (CLASSIFICATION_MAX_TEXT - 1) - len) {

        add = (CLASSIFICATION_MAX_TEXT - 1) - len;

    }

    memmove(dst + Global_Classification_Notes_Cursor + add, dst + Global_Classification_Notes_Cursor,
            len - (size_t)Global_Classification_Notes_Cursor + 1);

    memcpy(dst + Global_Classification_Notes_Cursor, src, add);
    Global_Classification_Notes_Cursor += (int)add;
    CLASSIFICATION_auto_wrap_notes_text();
}

static void CLASSIFICATION_auto_wrap_notes_text(void) {
    /*
        Purpose: Wraps classification notes automatically
        Returns: No value
    */

    char *text = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];
    size_t len = strlen(text);
    size_t line_start = 0;
    int max_px = Global_Classification_Notes_Wrap_Px;

    if (max_px < 16) {

        max_px = 520;

    }

    while (line_start < len) {
        size_t line_end = line_start;
        size_t segment_start = line_start;

        while (line_end < len && text[line_end] != '\n') {
            line_end++;
        }

        while (line_end > segment_start && CLASSIFICATION_text_range_width(Global_Classification_Notes_Font, text,
                                                                           segment_start, line_end) > max_px) {
            size_t fit = segment_start + 1;
            size_t break_pos;
            int found_space = 0;

            for (size_t i = segment_start + 1; i <= line_end; i++) {

                if (CLASSIFICATION_text_range_width(Global_Classification_Notes_Font, text, segment_start, i) <=
                    max_px) {

                    fit = i;

                }

                else {

                    break;

                }
            }

            if (fit <= segment_start) {

                fit = segment_start + 1;

            }

            if (fit > line_end) {

                fit = line_end;

            }
            break_pos = fit;

            for (size_t i = fit; i > segment_start; i--) {

                if (text[i] == ' ' || text[i] == '\t') {

                    break_pos = i;
                    found_space = 1;
                    break;

                }
            }

            if (found_space) {

                text[break_pos] = '\n';
                segment_start = break_pos + 1;

            }

            else {

                if (len + 1 >= CLASSIFICATION_MAX_TEXT) {

                    break;

                }
                memmove(text + break_pos + 1, text + break_pos, len - break_pos + 1);
                text[break_pos] = '\n';
                len++;
                line_end++;

                if (Global_Classification_Notes_Cursor >= (int)break_pos) {

                    Global_Classification_Notes_Cursor++;

                }
                segment_start = break_pos + 1;

            }
        }

        if (line_end >= len) {

            break;

        }
        line_start = line_end + 1;
    }

    CLASSIFICATION_clamp_notes_cursor();
}

static void CLASSIFICATION_paste_notes_text(void) {
    /*
        Purpose: Pastes text into the notes text
        Returns: No value
    */

    char *clip = SDL_GetClipboardText();

    if (clip) {

        CLASSIFICATION_insert_notes_text(clip);
        SDL_free(clip);

    }
}

static void CLASSIFICATION_backspace_notes_text(void) {
    /*
        Purpose: Removes the previous character from the notes text
        Returns: No value
    */

    char *dst = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];

    if (CLASSIFICATION_delete_notes_selection()) {

        return;

    }

    CLASSIFICATION_clamp_notes_cursor();

    if (Global_Classification_Notes_Cursor <= 0) {

        return;

    }

    size_t len = strlen(dst);

    memmove(dst + Global_Classification_Notes_Cursor - 1, dst + Global_Classification_Notes_Cursor,
            len - (size_t)Global_Classification_Notes_Cursor + 1);

    Global_Classification_Notes_Cursor--;
}

static void CLASSIFICATION_delete_notes_text(void) {
    /*
        Purpose: Deletes the notes text
        Returns: No value
    */

    char *dst = Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES];

    if (CLASSIFICATION_delete_notes_selection()) {

        return;

    }

    CLASSIFICATION_clamp_notes_cursor();

    size_t len = strlen(dst);

    if (Global_Classification_Notes_Cursor >= (int)len) {

        return;

    }

    memmove(dst + Global_Classification_Notes_Cursor, dst + Global_Classification_Notes_Cursor + 1,
            len - (size_t)Global_Classification_Notes_Cursor);
}

static int CLASSIFICATION_notes_build_lines(int starts[128], int ends[128]) {
    /*
        Purpose: Builds the notes lines
        Returns: Success status
    */

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

        if (end >= len) {

            break;

        }

        start = end + 1;
    }

    if (line_count < 1) {

        starts[0] = 0;
        ends[0] = 0;
        line_count = 1;

    }

    return line_count;
}

static void CLASSIFICATION_notes_move_horizontal(int direction) {
    /*
        Purpose: Moves the notes horizontal
        Returns: No value
    */

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

static void CLASSIFICATION_notes_move_vertical(int direction) {
    /*
        Purpose: Moves the notes vertical
        Returns: No value
    */

    int starts[128];
    int ends[128];
    int line_count = CLASSIFICATION_notes_build_lines(starts, ends);

    CLASSIFICATION_clamp_notes_cursor();

    int current_line = 0;

    for (int i = 0; i < line_count; i++) {

        if (Global_Classification_Notes_Cursor >= starts[i] && Global_Classification_Notes_Cursor <= ends[i]) {

            current_line = i;
            break;

        }

        if (i + 1 < line_count && Global_Classification_Notes_Cursor > ends[i] &&
            Global_Classification_Notes_Cursor < starts[i + 1]) {

            current_line = i;
            break;

        }
    }

    int target_line = current_line + direction;

    if (target_line < 0 || target_line >= line_count) {

        return;

    }

    /* Up/down intentionally jump to the end of the target line. */
    Global_Classification_Notes_Cursor = ends[target_line];
    CLASSIFICATION_clamp_notes_cursor();
}

static void CLASSIFICATION_set_notes_cursor_from_mouse(SDL_Rect rect, int mouse_x, int mouse_y) {
    /*
        Purpose: Sets the notes cursor from mouse
        Returns: No value
    */

    int starts[128];
    int ends[128];
    int line_count = CLASSIFICATION_notes_build_lines(starts, ends);
    int max_lines = (rect.h - 12) / CLASSIFICATION_NOTES_LINE_H;

    if (max_lines < 1) {

        max_lines = 1;

    }

    int first_line = 0;

    if (line_count > max_lines) {

        first_line = line_count - max_lines;

    }

    int visible_line = (mouse_y - (rect.y + 7)) / CLASSIFICATION_NOTES_LINE_H;

    if (visible_line < 0) {

        visible_line = 0;

    }

    if (visible_line >= max_lines) {

        visible_line = max_lines - 1;

    }

    int line = first_line + visible_line;

    if (line < 0) {

        line = 0;

    }

    if (line >= line_count) {

        line = line_count - 1;

    }

    int line_len = ends[line] - starts[line];
    int text_x = rect.x + 9;
    int rel_x = mouse_x - text_x;
    int column = 0;

    for (int i = 0; i <= line_len; i++) {
        int w0 = CLASSIFICATION_text_range_width(Global_Classification_Notes_Font,
                                                 Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES],
                                                 (size_t)starts[line], (size_t)(starts[line] + i));
        int w1 = w0;

        if (i < line_len) {

            w1 = CLASSIFICATION_text_range_width(Global_Classification_Notes_Font,
                                                 Global_Classification_Field_Text[CLASSIFICATION_FIELD_NOTES],
                                                 (size_t)starts[line], (size_t)(starts[line] + i + 1));

        }

        if (i == line_len || rel_x < (w0 + w1) / 2) {

            column = i;
            break;

        }
    }

    if (column < 0) {

        column = 0;

    }

    if (column > line_len) {

        column = line_len;

    }

    Global_Classification_Notes_Cursor = starts[line] + column;
    CLASSIFICATION_clamp_notes_cursor();
}

static void CLASSIFICATION_start_notes_selection(void) {
    /*
        Purpose: Starts the notes selection
        Returns: No value
    */

    CLASSIFICATION_clamp_notes_cursor();
    Global_Classification_Notes_Selecting = 1;
    Global_Classification_Notes_Selection_Start = Global_Classification_Notes_Cursor;
    Global_Classification_Notes_Selection_End = Global_Classification_Notes_Cursor;
}

static void CLASSIFICATION_update_notes_selection(void) {
    /*
        Purpose: Updates the notes selection
        Returns: No value
    */

    CLASSIFICATION_clamp_notes_cursor();

    if (Global_Classification_Notes_Selection_Start < 0) {

        Global_Classification_Notes_Selection_Start = Global_Classification_Notes_Cursor;

    }
    Global_Classification_Notes_Selection_End = Global_Classification_Notes_Cursor;
}

static void CLASSIFICATION_short_text(TTF_Font *font, const char *src, char *dst, size_t dst_size, int max_px) {
    /*
        Purpose: Shortens text for display
        Returns: No value
    */

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src) {

        src = "";

    }
    snprintf(dst, dst_size, "%s", src);

    if (!font || max_px <= 0) {

        return;

    }

    int text_w = 0;
    int text_h = 0;

    if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) {

        return;

    }

    size_t len = strlen(dst);

    while (len > 4) {
        len--;
        dst[len] = '\0';
        snprintf(dst + len - 3, dst_size - len + 3, "...");

        if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) {

            return;

        }

        if (len > 3) {

            dst[len - 3] = '\0';

        }
    }

    snprintf(dst, dst_size, "...");
}

static void CLASSIFICATION_draw_multiline_notes(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *text,
                                                int active) {
    /*
        Purpose: Draws the multiline notes
        Returns: No value
    */

    if (!renderer || !font) {

        return;

    }

    const char *src = text ? text : "";
    char local[CLASSIFICATION_MAX_TEXT + 8];

    if (src[0]) {

        snprintf(local, sizeof(local), "%.*s", CLASSIFICATION_MAX_TEXT - 1, src);

    }

    else {

        local[0] = '\0';

    }

    int line_h = CLASSIFICATION_NOTES_LINE_H;
    int max_lines = (rect.h - 12) / line_h;

    if (max_lines < 1) {

        max_lines = 1;

    }

    const char *line_starts[128];
    int line_count = 0;

    if (src[0]) {

        line_starts[line_count++] = local;

        for (char *p = local; *p && line_count < 128; p++) {

            if (*p == '\n') {

                *p = '\0';
                line_starts[line_count++] = p + 1;

            }
        }

    }

    else {

        line_starts[line_count++] = active ? "" : "Click to type";

    }

    int first_line = 0;

    if (line_count > max_lines) {

        first_line = line_count - max_lines;

    }

    if (active) {

        int sel_a = 0;
        int sel_b = 0;

        if (CLASSIFICATION_notes_selection_range(&sel_a, &sel_b)) {

            int starts[128];
            int ends[128];
            int raw_line_count = CLASSIFICATION_notes_build_lines(starts, ends);
            int raw_first_line = 0;

            if (raw_line_count > max_lines) {

                raw_first_line = raw_line_count - max_lines;

            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

            for (int line = raw_first_line; line < raw_line_count && line < raw_first_line + max_lines; line++) {
                int line_a = starts[line];
                int line_b = ends[line];
                int a = sel_a > line_a ? sel_a : line_a;
                int b = sel_b < line_b ? sel_b : line_b;

                if (a < b) {

                    int x0 = rect.x + 9 + CLASSIFICATION_text_range_width(font, src, (size_t)line_a, (size_t)a);
                    int x1 = rect.x + 9 + CLASSIFICATION_text_range_width(font, src, (size_t)line_a, (size_t)b);
                    int y0 = rect.y + 7 + ((line - raw_first_line) * line_h);
                    SDL_Rect hi = {x0, y0, x1 - x0, line_h};
                    draw_filled_rect(renderer, hi, (SDL_Color){0, 100, 220, 105});

                }
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        }

    }

    int y = rect.y + 7;

    for (int i = first_line; i < line_count; i++) {
        char short_line[CLASSIFICATION_MAX_TEXT + 16];

        CLASSIFICATION_short_text(font, line_starts[i], short_line, sizeof(short_line), rect.w - 18);

        draw_text(renderer, font, short_line, rect.x + 9, y,
                  src[0] || active ? (SDL_Color){230, 230, 230, 255} : (SDL_Color){120, 150, 130, 255});

        y += line_h;

        if (y + line_h > rect.y + rect.h) {

            break;

        }
    }

    if (active && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {

        int starts[128];
        int ends[128];
        int raw_line_count = CLASSIFICATION_notes_build_lines(starts, ends);
        int cursor = Global_Classification_Notes_Cursor;
        int src_len = (int)strlen(src);

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > src_len) {

            cursor = src_len;

        }

        int cursor_line = 0;
        for (int i = 0; i < raw_line_count; i++) {

            if (cursor >= starts[i] && cursor <= ends[i]) {

                cursor_line = i;
                break;

            }

            if (i == raw_line_count - 1 && cursor > ends[i]) {

                cursor_line = i;

            }
        }

        int raw_first_line = 0;

        if (raw_line_count > max_lines) {

            raw_first_line = raw_line_count - max_lines;

        }

        if (cursor_line >= raw_first_line && cursor_line < raw_first_line + max_lines) {

            int line_start = starts[cursor_line];
            int line_end = ends[cursor_line];

            if (cursor < line_start) {

                cursor = line_start;

            }

            if (cursor > line_end) {

                cursor = line_end;

            }

            int text_w = 0;
            int text_h = 0;

            if (cursor > line_start) {

                char before[CLASSIFICATION_MAX_TEXT + 8];
                int before_len = cursor - line_start;

                if (before_len >= (int)sizeof(before)) {

                    before_len = (int)sizeof(before) - 1;

                }
                memcpy(before, src + line_start, (size_t)before_len);
                before[before_len] = '\0';

                if (TTF_SizeText(font, before, &text_w, &text_h) != 0) {

                    text_w = before_len * 8;

                }

            }

            int cx = rect.x + 9 + text_w;
            int cy0 = rect.y + 7 + ((cursor_line - raw_first_line) * line_h);
            int cy1 = cy0 + line_h - 2;

            if (cx < rect.x + 9) {

                cx = rect.x + 9;

            }

            if (cx > rect.x + rect.w - 9) {

                cx = rect.x + rect.w - 9;

            }

            SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
            SDL_RenderDrawLine(renderer, cx, cy0, cx, cy1);
            SDL_RenderDrawLine(renderer, cx + 1, cy0, cx + 1, cy1);

        }

    }
}

static void CLASSIFICATION_get_layout(int win_w, int win_h, SDL_Rect *file_rect, SDL_Rect *form_rect,
                                      SDL_Rect field_rects[CLASSIFICATION_FIELD_COUNT], SDL_Rect *save_rect) {
    /*
        Purpose: Gets the layout
        Returns: No value
    */

    int gap = 24;
    int top = CLASSIFICATION_MARGIN + 58;
    int usable_w = win_w - (2 * CLASSIFICATION_MARGIN);
    int usable_h = win_h - top - CLASSIFICATION_MARGIN;

    if (usable_w < 360) {

        usable_w = 360;

    }

    if (usable_h < 420) {

        usable_h = 420;

    }

    /* Top 20%: recording file selector. Bottom area: existing classification
     * fields. */
    int list_h = (usable_h * 20) / 100;

    if (list_h < 108) {

        list_h = 108;

    }

    if (list_h > 190) {

        list_h = 190;

    }

    SDL_Rect local_file = {CLASSIFICATION_MARGIN, top, usable_w, list_h};
    SDL_Rect local_form = {CLASSIFICATION_MARGIN, top + list_h + gap, usable_w, usable_h - list_h - gap};

    int label_w = 220;
    int field_h = 30;
    int row_gap = 6;
    int required_h = 48;

    for (int i = 0; i < CLASSIFICATION_FIELD_COUNT; i++) {
        int h = field_h;

        if (i == CLASSIFICATION_FIELD_COUNTRY) {

            h = 40;

        }

        if (i == CLASSIFICATION_FIELD_NOTES) {

            h = 96;

        }
        required_h += h + row_gap;
    }

    required_h += 92;

    if (local_form.h < required_h) {

        local_form.h = required_h;

    }

    if (file_rect) {

        *file_rect = local_file;

    }

    if (form_rect) {

        *form_rect = local_form;

    }

    if (field_rects) {

        int x = local_form.x + label_w + 20;
        int y = local_form.y + 48;
        int w = local_form.w - label_w - 40;

        if (w < 180) {

            w = 180;

        }

        for (int i = 0; i < CLASSIFICATION_FIELD_COUNT; i++) {
            int h = field_h;

            if (i == CLASSIFICATION_FIELD_COUNTRY) {

                h = 40;

            }

            if (i == CLASSIFICATION_FIELD_NOTES) {

                h = 96;

            }

            field_rects[i] = (SDL_Rect){x, y, w, h};
            y += h + row_gap;
        }

    }

    if (save_rect) {

        *save_rect = (SDL_Rect){local_form.x + local_form.w - 170, local_form.y + local_form.h - 68, 150, 42};

    }
}

static void CLASSIFICATION_csv_escape(FILE *fp, const char *text) {
    /*
        Purpose: Escapes the CSV
        Returns: No value
    */

    fputc('"', fp);

    if (text) {

        for (const char *p = text; *p; p++) {

            if (*p == '"') {

                fputc('"', fp);
                fputc('"', fp);

            }

            else if (*p == '\n') {

                fputc('\\', fp);
                fputc('n', fp);

            }

            else if (*p == '\r') {

                fputc('\\', fp);
                fputc('r', fp);

            }

            else {

                fputc(*p, fp);

            }
        }

    }

    fputc('"', fp);
}

static void CLASSIFICATION_parse_file_metadata(const char *name, double *frequency_mhz, double *bandwidth_khz,
                                               double *start_time, double *end_time) {
    /*
        Purpose: Parses the file metadata
        Returns: No value
    */

    double mhz = 0.0;
    double bw_khz = 0.0;
    double sr_khz = 0.0;
    double duration_sec = 0.0;

    if (frequency_mhz) {

        *frequency_mhz = 0.0;

    }

    if (bandwidth_khz) {

        *bandwidth_khz = 0.0;

    }

    if (start_time) {

        *start_time = 0.0;

    }

    if (end_time) {

        *end_time = 0.0;

    }

    const char *cap = strstr(name, "_CAPTURE_");

    if (cap && sscanf(cap, "_CAPTURE_%lfMHz", &mhz) == 1 && mhz > 0.0) {

        if (frequency_mhz) {

            *frequency_mhz = mhz;

        }

    }

    const char *bw = strstr(name, "_BW_");

    if (bw && sscanf(bw, "_BW_%lfk", &bw_khz) == 1 && bw_khz > 0.0) {

        if (bandwidth_khz) {

            *bandwidth_khz = bw_khz;

        }

    }

    const char *sr = strstr(name, "_SR_");

    if (sr) {

        sscanf(sr, "_SR_%lfk", &sr_khz);

    }

    if (sr_khz > 0.0) {

        char path[CLASSIFICATION_MAX_FILE_PATH];
        int written = snprintf(path, sizeof(path), "%s/%s", Global_Classification_Record_Dir, name);

        if (written < 0 || (size_t)written >= sizeof(path)) {

            return;

        }

        struct stat st;

        if (stat(path, &st) == 0 && st.st_size > 0) {

            double iq_count = (double)st.st_size / (double)(sizeof(int16_t) * 2);
            duration_sec = iq_count / (sr_khz * 1000.0);

        }

    }

    if (end_time) {

        *end_time = duration_sec;

    }
}

static void CLASSIFICATION_load_selected_file_into_fields(void) {
    /*
        Purpose: Loads the selected file into fields
        Returns: No value
    */

    if (Global_Classification_File_Count <= 0) {

        return;

    }

    const char *file_name = Global_Classification_Files[Global_Classification_Selected_File];
    double frequency_mhz = 0.0;
    double bandwidth_khz = 0.0;
    double start_time = 0.0;
    double end_time = 0.0;

    CLASSIFICATION_parse_file_metadata(file_name, &frequency_mhz, &bandwidth_khz, &start_time, &end_time);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_FREQUENCY_MHZ], CLASSIFICATION_MAX_TEXT, "%.6f",
             frequency_mhz);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_BANDWIDTH], CLASSIFICATION_MAX_TEXT, "%.3f kHz",
             bandwidth_khz);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_START_TIME], CLASSIFICATION_MAX_TEXT, "%.6f",
             start_time);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_END_TIME], CLASSIFICATION_MAX_TEXT, "%.6f",
             end_time);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_FILE_NAME], CLASSIFICATION_MAX_TEXT, "%s",
             file_name);
}

void CLASSIFICATION_prefill_from_analysis_selection(const char *file_name, double frequency_mhz, double bandwidth_khz,
                                                    double start_time, double end_time) {
    /*
        Purpose: Prefills the from analysis selection
        Returns: No value
    */

    if (file_name && file_name[0]) {

        snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_FILE_NAME], CLASSIFICATION_MAX_TEXT, "%s",
                 file_name);

        for (int i = 0; i < Global_Classification_File_Count; i++) {

            if (strcmp(Global_Classification_Files[i], file_name) == 0) {

                Global_Classification_Selected_File = i;
                Global_Classification_File_Scroll = i - 4;

                if (Global_Classification_File_Scroll < 0) {

                    Global_Classification_File_Scroll = 0;

                }
                break;

            }
        }

    }

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_FREQUENCY_MHZ], CLASSIFICATION_MAX_TEXT, "%.6f",
             frequency_mhz);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_BANDWIDTH], CLASSIFICATION_MAX_TEXT, "%.3f kHz",
             bandwidth_khz);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_START_TIME], CLASSIFICATION_MAX_TEXT, "%.6f",
             start_time);

    snprintf(Global_Classification_Field_Text[CLASSIFICATION_FIELD_END_TIME], CLASSIFICATION_MAX_TEXT, "%.6f",
             end_time);

    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
    Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
    Global_Classification_Dropdown_Scroll = 0;
    Global_Classification_Dropdown_Hover = -1;
    CLASSIFICATION_clamp_notes_cursor();

    snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
             "Exported analysis selection. Fill Case #, Signal Name, "
             "Modulation/Class, Country, Latitude/Longitude, Notes, then Save.");
}

static int CLASSIFICATION_scan_recordings(void) {
    /*
        Purpose: Scans the recordings
        Returns: Success status
    */

    DIR *dir = opendir(Global_Classification_Record_Dir);
    Global_Classification_File_Count = 0;

    if (!dir) {

        snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
                 "Could not open recording directory: %.220s", Global_Classification_Record_Dir);
        return 0;

    }

    struct dirent *entry = NULL;

    while ((entry = readdir(dir)) != NULL && Global_Classification_File_Count < CLASSIFICATION_MAX_FILES) {

        if (!CLASSIFICATION_is_complex16_file(entry->d_name)) {

            continue;

        }

        snprintf(Global_Classification_Files[Global_Classification_File_Count],
                 sizeof(Global_Classification_Files[Global_Classification_File_Count]), "%s", entry->d_name);
        Global_Classification_File_Count++;
    }

    closedir(dir);

    qsort(Global_Classification_Files, (size_t)Global_Classification_File_Count, sizeof(Global_Classification_Files[0]),
          CLASSIFICATION_name_compare);

    if (Global_Classification_File_Count <= 0) {

        Global_Classification_Selected_File = 0;
        Global_Classification_File_Scroll = 0;
        snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
                 "No .complex16 recordings found in %.220s", Global_Classification_Record_Dir);
        return 0;

    }

    if (Global_Classification_Selected_File < 0) {

        Global_Classification_Selected_File = 0;

    }

    if (Global_Classification_Selected_File >= Global_Classification_File_Count) {

        Global_Classification_Selected_File = Global_Classification_File_Count - 1;

    }

    CLASSIFICATION_load_selected_file_into_fields();

    snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
             "Found %d recording(s). Click fields to type, or click "
             "Modulation/Class to select.",
             Global_Classification_File_Count);
    return 1;
}

static int CLASSIFICATION_file_search_matches(const char *name) {
    /*
        Purpose: Checks whether the file search matches the requested data
        Returns: Boolean status
    */

    char hay[512];
    char needle[CLASSIFICATION_FILE_SEARCH_TEXT_MAX];
    size_t i;

    if (!name) {

        name = "";

    }

    if (Global_Classification_File_Search_Text[0] == '\0') {

        return 1;

    }

    for (i = 0; i + 1 < sizeof(hay) && name[i]; i++) {
        hay[i] = (char)tolower((unsigned char)name[i]);
    }
    hay[i] = '\0';

    for (i = 0; i + 1 < sizeof(needle) && Global_Classification_File_Search_Text[i]; i++) {
        needle[i] = (char)tolower((unsigned char)Global_Classification_File_Search_Text[i]);
    }
    needle[i] = '\0';

    return strstr(hay, needle) != NULL;
}

static int CLASSIFICATION_file_search_filtered_count(void) {
    /*
        Purpose: Counts filtered file search results
        Returns: Item count
    */

    int count = 0;

    for (int i = 0; i < Global_Classification_File_Count; i++) {

        if (CLASSIFICATION_file_search_matches(Global_Classification_Files[i])) {

            count++;

        }
    }

    return count;
}

static int CLASSIFICATION_file_search_filtered_index_at(int filtered_index) {
    /*
        Purpose: Gets the file search filtered index at a position
        Returns: Item index
    */

    int seen = 0;

    if (filtered_index < 0) {

        return -1;

    }

    for (int i = 0; i < Global_Classification_File_Count; i++) {

        if (!CLASSIFICATION_file_search_matches(Global_Classification_Files[i])) {

            continue;

        }

        if (seen == filtered_index) {

            return i;

        }
        seen++;
    }

    return -1;
}

static SDL_Rect CLASSIFICATION_file_search_popup_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the file search popup rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {(win_w - 1050) / 2, (win_h - 740) / 2, 1050, 740};

    if (r.x < CLASSIFICATION_MARGIN) {

        r.x = CLASSIFICATION_MARGIN;

    }

    if (r.y < CLASSIFICATION_MARGIN) {

        r.y = CLASSIFICATION_MARGIN;

    }

    if (r.w > win_w - 2 * CLASSIFICATION_MARGIN) {

        r.w = win_w - 2 * CLASSIFICATION_MARGIN;

    }

    if (r.h > win_h - 2 * CLASSIFICATION_MARGIN) {

        r.h = win_h - 2 * CLASSIFICATION_MARGIN;

    }

    if (r.w < 320) {

        r.w = 320;

    }

    if (r.h < 260) {

        r.h = 260;

    }
    return r;
}

static SDL_Rect CLASSIFICATION_file_search_input_rect(SDL_Rect popup) {
    /*
        Purpose: Computes the file search input rectangle
        Returns: Computed rectangle
    */

    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = {close_btn.x - 292, popup.y + 14, 276, 30};

    if (search.x < popup.x + 180) {

        search.x = popup.x + 180;
        search.w = close_btn.x - search.x - 16;

    }

    if (search.w < 120) {

        search.w = 120;

    }
    return search;
}

static SDL_Rect CLASSIFICATION_file_search_button_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the file search button rectangle
        Returns: Computed rectangle
    */

    SDL_Rect file_rect;
    CLASSIFICATION_get_layout(win_w, win_h, &file_rect, NULL, NULL, NULL);

    SDL_Rect button = {file_rect.x + file_rect.w - 178, file_rect.y + 8, 166, 28};

    if (button.x < file_rect.x + 12) {

        button.x = file_rect.x + 12;

    }

    if (button.w > file_rect.w - 24) {

        button.w = file_rect.w - 24;

    }
    return button;
}

static void CLASSIFICATION_file_search_clamp_scroll(void) {
    /*
        Purpose: Clamps the file search scroll
        Returns: No value
    */

    int filtered_count = CLASSIFICATION_file_search_filtered_count();
    int visible = 14;
    int max_scroll = filtered_count - visible;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Classification_File_Search_Scroll < 0) {

        Global_Classification_File_Search_Scroll = 0;

    }

    if (Global_Classification_File_Search_Scroll > max_scroll) {

        Global_Classification_File_Search_Scroll = max_scroll;

    }
}

static void CLASSIFICATION_open_file_search_menu(void) {
    /*
        Purpose: Opens the file search menu
        Returns: No value
    */

    if (Global_Classification_File_Count <= 0) {

        CLASSIFICATION_scan_recordings();

    }

    Global_Classification_File_Search_Open = 1;
    Global_Classification_File_Search_Active = 1;
    Global_Classification_File_Search_Hover = -1;
    Global_Classification_File_Search_Text[0] = '\0';
    Global_Classification_File_Search_Cursor = 0;
    Global_Classification_File_Search_Scroll = 0;
    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
    Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
    Global_Classification_Dropdown_Hover = -1;
    CLASSIFICATION_clear_notes_selection();

    snprintf(Global_Classification_Status, sizeof(Global_Classification_Status), "Filename search menu opened");
}

static void CLASSIFICATION_close_file_search_menu(void) {
    /*
        Purpose: Closes the file search menu
        Returns: No value
    */

    Global_Classification_File_Search_Open = 0;
    Global_Classification_File_Search_Active = 0;
    Global_Classification_File_Search_Hover = -1;
}

static void CLASSIFICATION_file_search_select_index(int index) {
    /*
        Purpose: Selects the file search index
        Returns: No value
    */

    if (index < 0 || index >= Global_Classification_File_Count) {

        return;

    }

    Global_Classification_Selected_File = index;
    Global_Classification_File_Scroll = Global_Classification_Selected_File - 2;

    if (Global_Classification_File_Scroll < 0) {

        Global_Classification_File_Scroll = 0;

    }

    CLASSIFICATION_load_selected_file_into_fields();
    CLASSIFICATION_close_file_search_menu();

    snprintf(Global_Classification_Status, sizeof(Global_Classification_Status), "Selected %.220s",
             Global_Classification_Files[Global_Classification_Selected_File]);
}

static void CLASSIFICATION_file_search_insert_text(const char *text) {
    /*
        Purpose: Inserts the file search text
        Returns: No value
    */

    if (!text || text[0] == '\0') {

        return;

    }

    int len = (int)strlen(Global_Classification_File_Search_Text);
    int add = (int)strlen(text);

    if (Global_Classification_File_Search_Cursor < 0) {

        Global_Classification_File_Search_Cursor = 0;

    }

    if (Global_Classification_File_Search_Cursor > len) {

        Global_Classification_File_Search_Cursor = len;

    }

    if (len + add >= CLASSIFICATION_FILE_SEARCH_TEXT_MAX) {

        add = CLASSIFICATION_FILE_SEARCH_TEXT_MAX - len - 1;

    }

    if (add <= 0) {

        return;

    }

    memmove(Global_Classification_File_Search_Text + Global_Classification_File_Search_Cursor + add,
            Global_Classification_File_Search_Text + Global_Classification_File_Search_Cursor,
            (size_t)(len - Global_Classification_File_Search_Cursor + 1));

    memcpy(Global_Classification_File_Search_Text + Global_Classification_File_Search_Cursor, text, (size_t)add);

    Global_Classification_File_Search_Cursor += add;
    Global_Classification_File_Search_Scroll = 0;
}

static void CLASSIFICATION_file_search_backspace(void) {
    /*
        Purpose: Removes the previous character from the file search
        Returns: No value
    */

    int len = (int)strlen(Global_Classification_File_Search_Text);

    if (Global_Classification_File_Search_Cursor <= 0 || len <= 0) {

        return;

    }

    if (Global_Classification_File_Search_Cursor > len) {

        Global_Classification_File_Search_Cursor = len;

    }

    memmove(Global_Classification_File_Search_Text + Global_Classification_File_Search_Cursor - 1,
            Global_Classification_File_Search_Text + Global_Classification_File_Search_Cursor,
            (size_t)(len - Global_Classification_File_Search_Cursor + 1));

    Global_Classification_File_Search_Cursor--;
    Global_Classification_File_Search_Scroll = 0;
}

static void CLASSIFICATION_file_search_delete(void) {
    /*
        Purpose: Deletes the file search
        Returns: No value
    */

    int len = (int)strlen(Global_Classification_File_Search_Text);

    if (Global_Classification_File_Search_Cursor < 0) {

        Global_Classification_File_Search_Cursor = 0;

    }

    if (Global_Classification_File_Search_Cursor >= len) {

        return;

    }

    memmove(Global_Classification_File_Search_Text + Global_Classification_File_Search_Cursor,
            Global_Classification_File_Search_Text + Global_Classification_File_Search_Cursor + 1,
            (size_t)(len - Global_Classification_File_Search_Cursor));

    Global_Classification_File_Search_Scroll = 0;
}

static void CLASSIFICATION_draw_modal_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label,
                                             int hovered) {
    /*
        Purpose: Draws the modal button
        Returns: No value
    */

    SDL_Color fill = hovered ? (SDL_Color){0, 44, 16, 255} : (SDL_Color){0, 8, 3, 255};
    SDL_Color border = hovered ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 150, 60, 255};
    SDL_Color text = hovered ? (SDL_Color){235, 255, 240, 255} : (SDL_Color){0, 255, 90, 255};

    if (hovered) {

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_Rect glow = {rect.x - 4, rect.y - 4, rect.w + 8, rect.h + 8};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 38});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    }

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);

    int tw = 0;
    int th = 0;

    if (font && label && TTF_SizeText(font, label, &tw, &th) != 0) {

        tw = 0;
        th = 0;

    }

    draw_text(renderer, font, label, rect.x + (rect.w - tw) / 2, rect.y + (rect.h - th) / 2, text);
}

static int CLASSIFICATION_handle_file_search_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the file search event
        Returns: Handling status
    */

    if (!event || !Global_Classification_File_Search_Open) {

        return 0;

    }

    SDL_Rect popup = CLASSIFICATION_file_search_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = CLASSIFICATION_file_search_input_rect(popup);
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};

    if (event->type == SDL_TEXTINPUT) {

        if (Global_Classification_File_Search_Active) {

            CLASSIFICATION_file_search_insert_text(event->text.text);

        }
        return 1;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;
        int len = (int)strlen(Global_Classification_File_Search_Text);

        if (key == SDLK_ESCAPE) {

            CLASSIFICATION_close_file_search_menu();
            return 1;

        }

        if (key == SDLK_BACKSPACE) {

            CLASSIFICATION_file_search_backspace();
            return 1;

        }

        if (key == SDLK_DELETE) {

            CLASSIFICATION_file_search_delete();
            return 1;

        }

        if (key == SDLK_LEFT) {

            if (Global_Classification_File_Search_Cursor > 0) {

                Global_Classification_File_Search_Cursor--;

            }
            return 1;

        }

        if (key == SDLK_RIGHT) {

            if (Global_Classification_File_Search_Cursor < len) {

                Global_Classification_File_Search_Cursor++;

            }
            return 1;

        }

        if (key == SDLK_HOME) {

            Global_Classification_File_Search_Cursor = 0;
            return 1;

        }

        if (key == SDLK_END) {

            Global_Classification_File_Search_Cursor = len;
            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            int index = CLASSIFICATION_file_search_filtered_index_at(Global_Classification_File_Search_Scroll);

            if (index >= 0) {

                CLASSIFICATION_file_search_select_index(index);

            }
            return 1;

        }

        if (key == SDLK_DOWN) {

            Global_Classification_File_Search_Scroll++;
            CLASSIFICATION_file_search_clamp_scroll();
            return 1;

        }

        if (key == SDLK_UP) {

            Global_Classification_File_Search_Scroll--;
            CLASSIFICATION_file_search_clamp_scroll();
            return 1;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mx = 0;
        int my = 0;
        CLASSIFICATION_get_adjusted_mouse_state(&mx, &my);

        if (point_in_rect(mx, my, list)) {

            Global_Classification_File_Search_Scroll -= event->wheel.y * 3;
            CLASSIFICATION_file_search_clamp_scroll();

        }

        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        int mx = event->button.x;
        int my = event->button.y;

        if (!point_in_rect(mx, my, popup) || point_in_rect(mx, my, close_btn)) {

            CLASSIFICATION_close_file_search_menu();
            return 1;

        }

        if (point_in_rect(mx, my, search)) {

            Global_Classification_File_Search_Active = 1;
            return 1;

        }

        Global_Classification_File_Search_Active = 0;

        if (point_in_rect(mx, my, list)) {

            int row = (my - list.y - 4) / CLASSIFICATION_FILE_SEARCH_ROW_H;
            int visible = list.h / CLASSIFICATION_FILE_SEARCH_ROW_H;

            if (visible < 1) {

                visible = 1;

            }

            if (visible > 14) {

                visible = 14;

            }

            if (row >= 0 && row < visible) {

                int filtered_index = Global_Classification_File_Search_Scroll + row;
                int index = CLASSIFICATION_file_search_filtered_index_at(filtered_index);

                if (index >= 0) {

                    CLASSIFICATION_file_search_select_index(index);

                }

            }

            return 1;

        }

        return 1;

    }

    return 1;
}

static void CLASSIFICATION_draw_file_search_button(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the file search button
        Returns: No value
    */

    if (!renderer || !font) {

        return;

    }

    int mx = 0;
    int my = 0;
    CLASSIFICATION_get_adjusted_mouse_state(&mx, &my);

    SDL_Rect button = CLASSIFICATION_file_search_button_rect(win_w, win_h);

    CLASSIFICATION_draw_modal_button(renderer, font, button, "Open Search Menu", point_in_rect(mx, my, button));
}

static void CLASSIFICATION_draw_file_search_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the file search popup
        Returns: No value
    */

    if (!renderer || !font || !Global_Classification_File_Search_Open) {

        return;

    }

    SDL_Rect popup = CLASSIFICATION_file_search_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = CLASSIFICATION_file_search_input_rect(popup);
    SDL_Rect current_rect = {popup.x + 18, popup.y + 62, popup.w - 36, 42};
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};
    int mx = 0;
    int my = 0;
    int filtered_count = CLASSIFICATION_file_search_filtered_count();

    CLASSIFICATION_get_adjusted_mouse_state(&mx, &my);
    CLASSIFICATION_file_search_clamp_scroll();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 155});

    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, (SDL_Color){0, 255, 90, 255});
    SDL_Rect inner = {popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8};
    draw_outline_rect(renderer, inner, (SDL_Color){0, 150, 60, 255});

    draw_text(renderer, font, "FILENAME SEARCH", popup.x + 18, popup.y + 20, (SDL_Color){0, 255, 90, 255});

    CLASSIFICATION_draw_modal_button(renderer, font, close_btn, "Close", point_in_rect(mx, my, close_btn));

    draw_filled_rect(renderer, search,
                     Global_Classification_File_Search_Active ? (SDL_Color){0, 20, 8, 255} : (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, search,
                      Global_Classification_File_Search_Active ? (SDL_Color){0, 255, 90, 255}
                                                               : (SDL_Color){0, 150, 60, 255});

    if (Global_Classification_File_Search_Text[0]) {

        draw_text(renderer, font, Global_Classification_File_Search_Text, search.x + 10, search.y + 8,
                  (SDL_Color){0, 255, 90, 255});

    }

    else {

        draw_text(renderer, font, "Search file", search.x + 10, search.y + 8, (SDL_Color){0, 155, 65, 255});

    }

    if (Global_Classification_File_Search_Active && ((SDL_GetTicks64() / 450ULL) % 2ULL) == 0ULL) {

        int tw = 0;
        int th = 0;
        char prefix[CLASSIFICATION_FILE_SEARCH_TEXT_MAX];
        int cursor = Global_Classification_File_Search_Cursor;
        int len = (int)strlen(Global_Classification_File_Search_Text);

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > len) {

            cursor = len;

        }
        snprintf(prefix, sizeof(prefix), "%.*s", cursor, Global_Classification_File_Search_Text);

        if (font && TTF_SizeText(font, prefix, &tw, &th) != 0) {

            tw = cursor * 8;

        }

        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
        SDL_RenderDrawLine(renderer, search.x + 10 + tw, search.y + 6, search.x + 10 + tw, search.y + search.h - 6);
        SDL_RenderDrawLine(renderer, search.x + 11 + tw, search.y + 6, search.x + 11 + tw, search.y + search.h - 6);

    }

    draw_text(renderer, font, "Currently selected", current_rect.x, current_rect.y - 18, (SDL_Color){0, 155, 65, 255});
    draw_filled_rect(renderer, current_rect, (SDL_Color){0, 20, 8, 255});
    draw_outline_rect(renderer, current_rect, (SDL_Color){0, 255, 90, 255});

    {
        char short_name[512];
        const char *current = "(none selected)";

        if (Global_Classification_File_Count > 0 && Global_Classification_Selected_File >= 0 &&
            Global_Classification_Selected_File < Global_Classification_File_Count) {

            current = Global_Classification_Files[Global_Classification_Selected_File];

        }

        CLASSIFICATION_short_text(font, current, short_name, sizeof(short_name), current_rect.w - 20);

        draw_text(renderer, font, short_name, current_rect.x + 10, current_rect.y + 12,
                  current[0] == '(' ? (SDL_Color){0, 155, 65, 255} : (SDL_Color){0, 255, 90, 255});
    }

    draw_filled_rect(renderer, list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, list, (SDL_Color){0, 150, 60, 255});

    if (Global_Classification_File_Count <= 0) {

        char empty_msg[640];
        snprintf(empty_msg, sizeof(empty_msg), "No .complex16 files found in %s/", Global_Classification_Record_Dir);
        draw_text(renderer, font, empty_msg, list.x + 12, list.y + 14, (SDL_Color){255, 180, 40, 255});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    if (filtered_count <= 0) {

        draw_text(renderer, font, "No files match the search.", list.x + 12, list.y + 14,
                  (SDL_Color){255, 180, 40, 255});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    int visible = list.h / CLASSIFICATION_FILE_SEARCH_ROW_H;

    if (visible > 14) {

        visible = 14;

    }

    if (visible < 1) {

        visible = 1;

    }

    Global_Classification_File_Search_Hover = -1;

    if (point_in_rect(mx, my, list)) {

        int row = (my - list.y - 4) / CLASSIFICATION_FILE_SEARCH_ROW_H;
        int filtered_index = Global_Classification_File_Search_Scroll + row;
        int index = CLASSIFICATION_file_search_filtered_index_at(filtered_index);

        if (row >= 0 && row < visible && index >= 0 && index < Global_Classification_File_Count) {

            Global_Classification_File_Search_Hover = index;

        }

    }

    for (int row = 0; row < visible; row++) {
        int filtered_index = Global_Classification_File_Search_Scroll + row;
        int index = CLASSIFICATION_file_search_filtered_index_at(filtered_index);
        SDL_Rect item = {list.x + 4, list.y + 4 + row * CLASSIFICATION_FILE_SEARCH_ROW_H, list.w - 8,
                         CLASSIFICATION_FILE_SEARCH_ROW_H - 3};

        if (index < 0 || index >= Global_Classification_File_Count) {

            break;

        }

        int hovered = index == Global_Classification_File_Search_Hover;
        int selected = index == Global_Classification_Selected_File;
        char short_name[512];

        if (hovered) {

            draw_filled_rect(renderer, item, (SDL_Color){0, 44, 16, 255});
            SDL_Rect halo = {item.x - 2, item.y - 2, item.w + 4, item.h + 4};
            draw_outline_rect(renderer, halo, (SDL_Color){0, 255, 90, 255});

        }

        else if (selected) {

            draw_filled_rect(renderer, item, (SDL_Color){15, 85, 45, 245});

        }

        draw_outline_rect(renderer, item,
                          hovered    ? (SDL_Color){0, 255, 90, 255}
                          : selected ? (SDL_Color){0, 220, 80, 255}
                                     : (SDL_Color){0, 130, 55, 255});

        CLASSIFICATION_short_text(font, Global_Classification_Files[index], short_name, sizeof(short_name),
                                  item.w - 20);

        draw_text(renderer, font, short_name, item.x + 10, item.y + 8,
                  hovered    ? (SDL_Color){235, 255, 240, 255}
                  : selected ? (SDL_Color){255, 255, 255, 255}
                             : (SDL_Color){0, 255, 90, 255});
    }

    char count_label[128];

    if (Global_Classification_File_Search_Text[0]) {

        snprintf(count_label, sizeof(count_label), "%d of %d files", filtered_count, Global_Classification_File_Count);

    }

    else {

        snprintf(count_label, sizeof(count_label), "%d files", Global_Classification_File_Count);

    }

    draw_text(renderer, font, count_label, popup.x + 18, popup.y + popup.h - 24, (SDL_Color){0, 155, 65, 255});

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void CLASSIFICATION_make_filename_safe(const char *src, char *dst, size_t dst_size) {
    /*
        Purpose: Sanitizes a classification filename
        Returns: No value
    */

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src || !src[0]) {

        src = "UNNAMED_SIGNAL";

    }

    size_t j = 0;

    for (size_t i = 0; src[i] && j + 1 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {

            dst[j++] = (char)c;

        }

        else if (c == '-' || c == '_') {

            dst[j++] = (char)c;

        }

        else if (c == ' ' || c == '.' || c == '/' || c == ':' || c == '\\') {

            if (j > 0 && dst[j - 1] != '_') {

                dst[j++] = '_';

            }

        }
    }

    while (j > 0 && dst[j - 1] == '_') {
        j--;
    }

    if (j == 0) {

        snprintf(dst, dst_size, "UNNAMED_SIGNAL");
        return;

    }

    dst[j] = '\0';
}

static void RETROSPECTRUM_UNUSED CLASSIFICATION_get_signal_datetime(char *out, size_t out_size) {
    /*
        Purpose: Gets the signal date and time
        Returns: No value
    */

    if (!out || out_size == 0) {

        return;

    }

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

    if (len >= out_size) {

        len = out_size - 1;

    }

    memcpy(out, file_name, len);
    out[len] = '\0';

    for (size_t i = 0; out[i]; i++) {
        char c = out[i];

        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {

            out[i] = '_';

        }
    }
}

static int CLASSIFICATION_append_csv_row(void) {
    /*
        Purpose: Appends the CSV row
        Returns: Success status
    */

    char safe_case[CLASSIFICATION_MAX_TEXT];
    char csv_name[CLASSIFICATION_MAX_CSV_NAME];
    char database_error[256] = "";
    unsigned char *existing = NULL;
    size_t existing_size = 0;
    int found = 0;
    char *updated = NULL;
    size_t updated_size = 0;
    FILE *fp = NULL;

    Global_Classification_Save_Message[0] = '\0';
    Global_Classification_Save_Message_Time = 0;
    Global_Classification_File_Search_Open = 0;
    Global_Classification_File_Search_Active = 0;
    Global_Classification_File_Search_Cursor = 0;
    Global_Classification_File_Search_Scroll = 0;
    Global_Classification_File_Search_Hover = -1;
    Global_Classification_File_Search_Text[0] = '\0';

    CLASSIFICATION_make_filename_safe(Global_Classification_Field_Text[CLASSIFICATION_FIELD_CASE_NUMBER], safe_case,
                                      sizeof(safe_case));

    {
        int written = snprintf(csv_name, sizeof(csv_name), "CASE_%.*s.csv", CLASSIFICATION_MAX_TEXT - 1, safe_case);

        if (written < 0 || (size_t)written >= sizeof(csv_name)) {

            snprintf(Global_Classification_Status, sizeof(Global_Classification_Status), "Case record name too long");
            return 0;

        }
    }

    if (!DATASTORE_load_content(DATASTORE_KIND_CLASSIFICATION, csv_name, &existing, &existing_size, &found,
                                database_error, sizeof(database_error))) {

        snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
                 "Failed to load classification from database: %.180s", database_error);
        return 0;

    }

    fp = open_memstream(&updated, &updated_size);

    if (!fp) {

        DATASTORE_free_content(existing, existing_size);
        snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
                 "Unable to create classification record in memory");
        return 0;

    }

    if (found && existing_size > 0) {

        if (fwrite(existing, 1, existing_size, fp) != existing_size) {

            fclose(fp);
            DATASTORE_free_content(existing, existing_size);
            free(updated);
            snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
                     "Unable to extend classification record");
            return 0;

        }

        if (existing[existing_size - 1] != '\n') {

            fputc('\n', fp);

        }

    }

    else {

        fprintf(fp, "case_number,signal_name,frequency_mhz,bandwidth,start_time,"
                    "end_time,calculated_modulation,signal_class,country,latitude,"
                    "longitude,notes,file_name\n");

    }

    for (int i = 0; i < CLASSIFICATION_FIELD_COUNT; i++) {

        if (i > 0) {

            fputc(',', fp);

        }
        CLASSIFICATION_csv_escape(fp, Global_Classification_Field_Text[i]);
    }
    fputc('\n', fp);

    if (fclose(fp) != 0) {

        DATASTORE_free_content(existing, existing_size);
        free(updated);
        snprintf(Global_Classification_Status, sizeof(Global_Classification_Status),
                 "Failed to finalize classification record");
        return 0;

    }
    fp = NULL;
    DATASTORE_free_content(existing, existing_size);
    existing = NULL;

    if (!DATASTORE_save_content(DATASTORE_KIND_CLASSIFICATION, csv_name,
                                Global_Classification_Field_Text[CLASSIFICATION_FIELD_CASE_NUMBER], updated,
                                updated_size, database_error, sizeof(database_error))) {

        DATASTORE_free_content((unsigned char *)updated, updated_size);
        snprintf(Global_Classification_Status, sizeof(Global_Classification_Status), "Database save failed: %.210s",
                 database_error);
        return 0;

    }

    DATASTORE_free_content((unsigned char *)updated, updated_size);

    snprintf(Global_Classification_Status, sizeof(Global_Classification_Status), "Classification saved to database");
    snprintf(Global_Classification_Save_Message, sizeof(Global_Classification_Save_Message),
             "Case signal saved to database successfully");
    Global_Classification_Save_Message_Time = SDL_GetTicks64();

    CLASSIFICATION_scan_case_files();
    return 1;
}

int CLASSIFICATION_is_text_entry_active(void) {
    /*
        Purpose: Checks whether the text entry is active
        Returns: Boolean status
    */

    return Global_Classification_Active_Field != CLASSIFICATION_FIELD_NONE || Global_Classification_File_Search_Open;
}

void CLASSIFICATION_enter_mode(const char *record_dir) {
    /*
        Purpose: Enters the classification mode
        Returns: No value
    */

    Global_Classification_Mode = 1;
    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
    Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
    Global_Classification_Dropdown_Scroll = 0;
    Global_Classification_Dropdown_Hover = -1;
    Global_Classification_Country_Scroll = 0;
    Global_Classification_Country_Hover = -1;
    Global_Classification_Case_Scroll = 0;
    Global_Classification_Case_Hover = -1;
    Global_Classification_Notes_Cursor = 0;
    Global_Classification_Save_Message[0] = '\0';
    Global_Classification_Save_Message_Time = 0;

    if (record_dir && record_dir[0]) {

        snprintf(Global_Classification_Record_Dir, sizeof(Global_Classification_Record_Dir), "%s", record_dir);

    }

    SDL_StartTextInput();
    CLASSIFICATION_scan_recordings();
    CLASSIFICATION_scan_case_files();
}

void CLASSIFICATION_exit_mode(void) {
    /*
        Purpose: Exits the classification mode
        Returns: No value
    */

    Global_Classification_Mode = 0;
    Global_Classification_Active_Field = CLASSIFICATION_FIELD_NONE;
    Global_Classification_Open_Dropdown = CLASSIFICATION_DROPDOWN_NONE;
    Global_Classification_Dropdown_Scroll = 0;
    Global_Classification_Dropdown_Hover = -1;
    Global_Classification_Country_Scroll = 0;
    Global_Classification_Country_Hover = -1;
    Global_Classification_Case_Scroll = 0;
    Global_Classification_Case_Hover = -1;
    Global_Classification_Notes_Cursor = 0;
    Global_Classification_File_Search_Open = 0;
    Global_Classification_File_Search_Active = 0;
    Global_Classification_File_Search_Hover = -1;
    /* Keep SDL text input enabled for the main/interception workstation. */
}

int CLASSIFICATION_handle_event(SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the event
        Returns: Handling status
    */

    if (!event || !Global_Classification_Mode) {

        return 0;

    }

    if (CLASSIFICATION_handle_file_search_event(event, win_w, win_h)) {

        return 1;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;

        if (Global_Classification_Active_Field != CLASSIFICATION_FIELD_NONE) {

            SDL_Keymod mod = SDL_GetModState();

            if ((mod & KMOD_CTRL) && key == SDLK_v &&
                Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {

                CLASSIFICATION_paste_notes_text();
                return 1;

            }

            if (key == SDLK_BACKSPACE) {

                if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {

                    CLASSIFICATION_backspace_notes_text();

                }

                else {

                    CLASSIFICATION_backspace_text(Global_Classification_Field_Text[Global_Classification_Active_Field]);

                    if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_COUNTRY) {

                        Global_Classification_Country_Scroll = 0;
                        Global_Classification_Country_Hover = -1;

                    }

                    if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_CASE_NUMBER) {

                        Global_Classification_Case_Scroll = 0;
                        Global_Classification_Case_Hover = -1;

                    }

                }

            }

            else if (key == SDLK_DELETE) {

                if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {

                    CLASSIFICATION_delete_notes_text();

                }

                else {

                    Global_Classification_Field_Text[Global_Classification_Active_Field][0] = '\0';

                    if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_COUNTRY) {

                        Global_Classification_Country_Scroll = 0;
                        Global_Classification_Country_Hover = -1;

                    }

                    if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_CASE_NUMBER) {

                        Global_Classification_Case_Scroll = 0;
                        Global_Classification_Case_Hover = -1;

                    }

                }

            }

            else if (key == SDLK_LEFT && Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {

                CLASSIFICATION_notes_move_horizontal(-1);
                CLASSIFICATION_clear_notes_selection();

            }

            else if (key == SDLK_RIGHT && Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {

                CLASSIFICATION_notes_move_horizontal(1);
                CLASSIFICATION_clear_notes_selection();

            }

            else if (key == SDLK_UP && Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {

                CLASSIFICATION_notes_move_vertical(-1);
                CLASSIFICATION_clear_notes_selection();

            }

            else if (key == SDLK_DOWN && Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {

                CLASSIFICATION_notes_move_vertical(1);
                CLASSIFICATION_clear_notes_selection();

            }

            else if (key == SDLK_TAB) {

                Global_Classification_Active_Field++;

                if (Global_Classification_Active_Field >= CLASSIFICATION_FIELD_COUNT) {

                    Global_Classification_Active_Field = CLASSIFICATION_FIELD_CASE_NUMBER;

                }

                while (CLASSIFICATION_is_dropdown_field(Global_Classification_Active_Field)) {
                    Global_Classification_Active_Field++;

                    if (Global_Classification_Active_Field >= CLASSIFICATION_FIELD_COUNT) {

                        Global_Classification_Active_Field = CLASSIFICATION_FIELD_CASE_NUMBER;

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

        if (key == SDLK_g) {

            return 2;

        }

        if (key == SDLK_q) {

            return 1;

        }

        if (key == SDLK_r) {

            CLASSIFICATION_scan_recordings();
            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {

            CLASSIFICATION_append_csv_row();
            return 1;

        }

        if (key == SDLK_TAB) {

            Global_Classification_Active_Field = CLASSIFICATION_FIELD_CASE_NUMBER;
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
                                           CLASSIFICATION_MAX_TEXT, event->text.text);

                if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_COUNTRY) {

                    Global_Classification_Country_Scroll = 0;
                    Global_Classification_Country_Hover = -1;

                }

                if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_CASE_NUMBER) {

                    Global_Classification_Case_Scroll = 0;
                    Global_Classification_Case_Hover = -1;

                }

            }

        }
        return 1;

    }

    if (event->type == SDL_MOUSEMOTION && Global_Classification_Notes_Selecting &&
        Global_Classification_Active_Field == CLASSIFICATION_FIELD_NOTES) {

        SDL_Rect field_rects[CLASSIFICATION_FIELD_COUNT];
        CLASSIFICATION_get_layout(win_w, win_h, NULL, NULL, field_rects, NULL);
        CLASSIFICATION_set_notes_cursor_from_mouse(field_rects[CLASSIFICATION_FIELD_NOTES], event->motion.x,
                                                   event->motion.y);
        CLASSIFICATION_update_notes_selection();
        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {

        if (Global_Classification_Notes_Selecting) {

            Global_Classification_Notes_Selecting = 0;
            return 1;

        }

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mx = 0;
        int my = 0;
        CLASSIFICATION_get_adjusted_mouse_state(&mx, &my);

        SDL_Rect file_rect;
        SDL_Rect field_rects[CLASSIFICATION_FIELD_COUNT];
        CLASSIFICATION_get_layout(win_w, win_h, &file_rect, NULL, field_rects, NULL);

        if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_CASE_NUMBER) {

            int matches[CLASSIFICATION_MAX_FILES];
            int count = CLASSIFICATION_build_case_matches(matches, CLASSIFICATION_MAX_FILES);
            int max_scroll = count - CLASSIFICATION_CASE_MAX_VISIBLE;

            if (max_scroll < 0) {

                max_scroll = 0;

            }

            SDL_Rect case_base = field_rects[CLASSIFICATION_FIELD_CASE_NUMBER];
            SDL_Rect case_dd = {case_base.x, case_base.y + case_base.h, case_base.w,
                                CLASSIFICATION_CASE_MAX_VISIBLE * CLASSIFICATION_CASE_OPTION_H};

            if (point_in_rect(mx, my, case_base) || point_in_rect(mx, my, case_dd)) {

                Global_Classification_Case_Scroll -= event->wheel.y * 3;

                if (Global_Classification_Case_Scroll < 0) {

                    Global_Classification_Case_Scroll = 0;

                }

                if (Global_Classification_Case_Scroll > max_scroll) {

                    Global_Classification_Case_Scroll = max_scroll;

                }
                return 1;

            }

        }

        if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_COUNTRY) {

            int matches[512];
            int count = CLASSIFICATION_build_country_matches(matches, 512);
            int max_scroll = count - CLASSIFICATION_COUNTRY_MAX_VISIBLE;

            if (max_scroll < 0) {

                max_scroll = 0;

            }

            if (point_in_rect(mx, my, field_rects[CLASSIFICATION_FIELD_COUNTRY])) {

                Global_Classification_Country_Scroll -= event->wheel.y * 3;

                if (Global_Classification_Country_Scroll < 0) {

                    Global_Classification_Country_Scroll = 0;

                }

                if (Global_Classification_Country_Scroll > max_scroll) {

                    Global_Classification_Country_Scroll = max_scroll;

                }
                return 1;

            }

            SDL_Rect country_base = field_rects[CLASSIFICATION_FIELD_COUNTRY];
            SDL_Rect country_dd = {country_base.x, country_base.y + country_base.h, country_base.w,
                                   CLASSIFICATION_COUNTRY_MAX_VISIBLE * CLASSIFICATION_COUNTRY_OPTION_H};

            if (point_in_rect(mx, my, country_dd)) {

                Global_Classification_Country_Scroll -= event->wheel.y * 3;

                if (Global_Classification_Country_Scroll < 0) {

                    Global_Classification_Country_Scroll = 0;

                }

                if (Global_Classification_Country_Scroll > max_scroll) {

                    Global_Classification_Country_Scroll = max_scroll;

                }
                return 1;

            }

        }

        if (Global_Classification_Open_Dropdown != CLASSIFICATION_DROPDOWN_NONE) {

            int dropdown_field = Global_Classification_Open_Dropdown;
            SDL_Rect base = field_rects[dropdown_field];
            int visible = CLASSIFICATION_dropdown_visible_count(dropdown_field);
            SDL_Rect dropdown_rect = {base.x, base.y + base.h, base.w, visible * CLASSIFICATION_DROPDOWN_OPTION_H};

            if (point_in_rect(mx, my, dropdown_rect) || point_in_rect(mx, my, base)) {

                Global_Classification_Dropdown_Scroll -= event->wheel.y * 3;
                CLASSIFICATION_clamp_dropdown_scroll(dropdown_field);
                return 1;

            }

        }

        if (point_in_rect(mx, my, file_rect)) {

            int visible = (file_rect.h - 58) / CLASSIFICATION_ROW_HEIGHT;

            if (visible < 1) {

                visible = 1;

            }

            Global_Classification_File_Scroll -= event->wheel.y * 3;

            if (Global_Classification_File_Scroll < 0) {

                Global_Classification_File_Scroll = 0;

            }

            if (Global_Classification_File_Scroll + visible > Global_Classification_File_Count) {

                Global_Classification_File_Scroll = Global_Classification_File_Count - visible;

                if (Global_Classification_File_Scroll < 0) {

                    Global_Classification_File_Scroll = 0;

                }

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

        if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_CASE_NUMBER) {

            int matches[CLASSIFICATION_MAX_FILES];
            int count = CLASSIFICATION_build_case_matches(matches, CLASSIFICATION_MAX_FILES);

            if (Global_Classification_Case_Scroll < 0) {

                Global_Classification_Case_Scroll = 0;

            }

            if (Global_Classification_Case_Scroll > count - CLASSIFICATION_CASE_MAX_VISIBLE) {

                Global_Classification_Case_Scroll = count - CLASSIFICATION_CASE_MAX_VISIBLE;

            }

            if (Global_Classification_Case_Scroll < 0) {

                Global_Classification_Case_Scroll = 0;

            }

            SDL_Rect base = field_rects[CLASSIFICATION_FIELD_CASE_NUMBER];
            int visible = count - Global_Classification_Case_Scroll;

            if (visible > CLASSIFICATION_CASE_MAX_VISIBLE) {

                visible = CLASSIFICATION_CASE_MAX_VISIBLE;

            }

            if (visible < 0) {

                visible = 0;

            }

            for (int i = 0; i < visible; i++) {
                int match_index = Global_Classification_Case_Scroll + i;
                SDL_Rect option_rect = {base.x, base.y + base.h + (i * CLASSIFICATION_CASE_OPTION_H), base.w,
                                        CLASSIFICATION_CASE_OPTION_H};

                if (match_index >= 0 && match_index < count && point_in_rect(x, y, option_rect)) {

                    CLASSIFICATION_select_case(matches[match_index]);
                    return 1;

                }
            }

        }

        if (Global_Classification_Active_Field == CLASSIFICATION_FIELD_COUNTRY) {

            int matches[512];
            int count = CLASSIFICATION_build_country_matches(matches, 512);

            if (Global_Classification_Country_Scroll < 0) {

                Global_Classification_Country_Scroll = 0;

            }

            if (Global_Classification_Country_Scroll > count - CLASSIFICATION_COUNTRY_MAX_VISIBLE) {

                Global_Classification_Country_Scroll = count - CLASSIFICATION_COUNTRY_MAX_VISIBLE;

            }

            if (Global_Classification_Country_Scroll < 0) {

                Global_Classification_Country_Scroll = 0;

            }

            SDL_Rect base = field_rects[CLASSIFICATION_FIELD_COUNTRY];
            int visible = count - Global_Classification_Country_Scroll;

            if (visible > CLASSIFICATION_COUNTRY_MAX_VISIBLE) {

                visible = CLASSIFICATION_COUNTRY_MAX_VISIBLE;

            }

            if (visible < 0) {

                visible = 0;

            }

            for (int i = 0; i < visible; i++) {
                int match_index = Global_Classification_Country_Scroll + i;
                SDL_Rect option_rect = {base.x, base.y + base.h + (i * CLASSIFICATION_COUNTRY_OPTION_H), base.w,
                                        CLASSIFICATION_COUNTRY_OPTION_H};

                if (match_index >= 0 && match_index < count && point_in_rect(x, y, option_rect)) {

                    CLASSIFICATION_select_country(matches[match_index]);
                    return 1;

                }
            }

        }

        if (Global_Classification_Open_Dropdown != CLASSIFICATION_DROPDOWN_NONE) {

            int dropdown_field = Global_Classification_Open_Dropdown;
            int count = CLASSIFICATION_option_count_for_field(dropdown_field);
            int visible = CLASSIFICATION_dropdown_visible_count(dropdown_field);
            SDL_Rect base = field_rects[dropdown_field];

            CLASSIFICATION_clamp_dropdown_scroll(dropdown_field);

            for (int i = 0; i < visible; i++) {
                int option_index = Global_Classification_Dropdown_Scroll + i;
                SDL_Rect option_rect = {base.x, base.y + base.h + (i * CLASSIFICATION_DROPDOWN_OPTION_H), base.w,
                                        CLASSIFICATION_DROPDOWN_OPTION_H};

                if (option_index >= count) {

                    break;

                }

                if (point_in_rect(x, y, option_rect)) {

                    snprintf(Global_Classification_Field_Text[dropdown_field], CLASSIFICATION_MAX_TEXT, "%s",
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

        SDL_Rect search_button = CLASSIFICATION_file_search_button_rect(win_w, win_h);

        if (point_in_rect(x, y, search_button)) {

            CLASSIFICATION_open_file_search_menu();
            return 1;

        }

        if (point_in_rect(x, y, file_rect)) {

            int row_y = file_rect.y + 44;
            int idx = Global_Classification_File_Scroll + ((y - row_y) / CLASSIFICATION_ROW_HEIGHT);

            if (y >= row_y && idx >= 0 && idx < Global_Classification_File_Count) {

                Global_Classification_Selected_File = idx;
                Global_Classification_Save_Message[0] = '\0';
                Global_Classification_Save_Message_Time = 0;
                CLASSIFICATION_load_selected_file_into_fields();
                snprintf(Global_Classification_Status, sizeof(Global_Classification_Status), "Selected %.220s",
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

                    if (i == CLASSIFICATION_FIELD_CASE_NUMBER) {

                        CLASSIFICATION_scan_case_files();
                        Global_Classification_Case_Scroll = 0;
                        Global_Classification_Case_Hover = -1;

                    }

                    if (i == CLASSIFICATION_FIELD_NOTES) {

                        CLASSIFICATION_set_notes_cursor_from_mouse(field_rects[i], x, y);
                        CLASSIFICATION_start_notes_selection();

                    }

                    else {

                        CLASSIFICATION_clear_notes_selection();

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

static void CLASSIFICATION_draw_panel(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *title) {
    /*
        Purpose: Draws the panel
        Returns: No value
    */

    draw_filled_rect(renderer, rect, (SDL_Color){0, 0, 0, 215});
    draw_outline_rect(renderer, rect, (SDL_Color){0, 150, 70, 255});
    draw_text(renderer, font, title, rect.x + 10, rect.y + 12, (SDL_Color){0, 255, 90, 255});
}

static void CLASSIFICATION_draw_selectable_row(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect row, const char *text,
                                               int is_selected) {
    /*
        Purpose: Draws the selectable row
        Returns: No value
    */

    if (is_selected) {

        draw_filled_rect(renderer, row, (SDL_Color){20, 80, 45, 220});

    }

    char short_text[512];
    CLASSIFICATION_short_text(font, text, short_text, sizeof(short_text), row.w - 12);

    draw_text(renderer, font, short_text, row.x + 6, row.y + 4,
              is_selected ? (SDL_Color){230, 230, 230, 255} : (SDL_Color){150, 150, 150, 255});
}

static void CLASSIFICATION_draw_input_field(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label,
                                            const char *text, int active, int dropdown_field, int field_index) {
    /*
        Purpose: Draws the input field
        Returns: No value
    */

    draw_text(renderer, font, label, rect.x - 226, rect.y + 9,
              active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 180, 70, 255});

    draw_filled_rect(renderer, rect, (SDL_Color){0, 8, 3, 255});
    draw_outline_rect(renderer, rect, active ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 120, 50, 255});

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

        Global_Classification_Notes_Font = font;
        Global_Classification_Notes_Wrap_Px = rect.w - 18;
        CLASSIFICATION_draw_multiline_notes(renderer, font, rect, text, active);
        return;

    }

    char short_value[CLASSIFICATION_MAX_TEXT + 64];

    int text_max_w = rect.w - 34;

    if (field_index == CLASSIFICATION_FIELD_COUNTRY) {

        text_max_w -= 78;

    }

    CLASSIFICATION_short_text(font, shown, short_value, sizeof(short_value), text_max_w);

    draw_text(renderer, font, short_value, rect.x + 9, rect.y + 9,
              (text && text[0]) ? (SDL_Color){230, 230, 230, 255} : (SDL_Color){120, 150, 130, 255});

    if (field_index == CLASSIFICATION_FIELD_COUNTRY) {

        const Type_Classification_Country_Option *country = CLASSIFICATION_find_country_exact(text);
        SDL_Rect flag_rect = {rect.x + rect.w - 70, rect.y + 5, 60, rect.h - 10};

        if (country) {

            CLASSIFICATION_draw_flag_box(renderer, font, flag_rect, country->alpha2);

        }

        else {

            draw_outline_rect(renderer, flag_rect, (SDL_Color){0, 100, 45, 255});

        }

    }

    if (dropdown_field) {

        draw_text(renderer, font, "v", rect.x + rect.w - 22, rect.y + 9, (SDL_Color){0, 220, 80, 255});

    }
}

static void CLASSIFICATION_draw_dropdown(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect field_rect, int field) {
    /*
        Purpose: Draws the dropdown
        Returns: No value
    */

    int count = CLASSIFICATION_option_count_for_field(field);

    if (count <= 0) {

        return;

    }

    CLASSIFICATION_clamp_dropdown_scroll(field);

    int visible = CLASSIFICATION_dropdown_visible_count(field);

    int mouse_x = 0;
    int mouse_y = 0;
    CLASSIFICATION_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_Rect dropdown_bg = {field_rect.x, field_rect.y + field_rect.h, field_rect.w,
                            visible * CLASSIFICATION_DROPDOWN_OPTION_H};

    draw_filled_rect(renderer, dropdown_bg, (SDL_Color){0, 0, 0, 245});
    draw_outline_rect(renderer, dropdown_bg, (SDL_Color){0, 180, 70, 255});

    Global_Classification_Dropdown_Hover = -1;

    for (int i = 0; i < visible; i++) {
        int option_index = Global_Classification_Dropdown_Scroll + i;

        if (option_index >= count) {

            break;

        }

        SDL_Rect option_rect = {field_rect.x, field_rect.y + field_rect.h + (i * CLASSIFICATION_DROPDOWN_OPTION_H),
                                field_rect.w, CLASSIFICATION_DROPDOWN_OPTION_H};

        int selected =
            strcmp(Global_Classification_Field_Text[field], CLASSIFICATION_option_for_field(field, option_index)) == 0;
        int hovered = point_in_rect(mouse_x, mouse_y, option_rect);

        if (hovered) {

            Global_Classification_Dropdown_Hover = option_index;

            SDL_Rect glow_outer = {option_rect.x - 3, option_rect.y - 2, option_rect.w + 6, option_rect.h + 4};

            draw_filled_rect(renderer, glow_outer, (SDL_Color){0, 255, 90, 38});

        }

        draw_filled_rect(renderer, option_rect,
                         hovered    ? (SDL_Color){0, 70, 30, 250}
                         : selected ? (SDL_Color){15, 85, 45, 245}
                                    : (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer, option_rect,
                          hovered    ? (SDL_Color){0, 255, 90, 255}
                          : selected ? (SDL_Color){0, 220, 80, 255}
                                     : (SDL_Color){0, 130, 55, 255});
        draw_text(renderer, font, CLASSIFICATION_option_for_field(field, option_index), option_rect.x + 9,
                  option_rect.y + 6,
                  hovered    ? (SDL_Color){235, 255, 240, 255}
                  : selected ? (SDL_Color){255, 255, 255, 255}
                             : (SDL_Color){190, 220, 195, 255});
    }

    if (count > visible) {

        int track_h = dropdown_bg.h - 8;
        int scroll_x = dropdown_bg.x + dropdown_bg.w - 8;
        int scroll_y = dropdown_bg.y + 4;
        int scroll_w = 4;

        int thumb_h = (visible * track_h) / count;

        if (thumb_h < 18) {

            thumb_h = 18;

        }

        if (thumb_h > track_h) {

            thumb_h = track_h;

        }

        int max_scroll = count - visible;
        int thumb_y = scroll_y;

        if (max_scroll > 0) {

            thumb_y = scroll_y + (Global_Classification_Dropdown_Scroll * (track_h - thumb_h)) / max_scroll;

        }

        SDL_Rect track = {scroll_x, scroll_y, scroll_w, track_h};
        SDL_Rect thumb = {scroll_x - 1, thumb_y, scroll_w + 2, thumb_h};

        draw_filled_rect(renderer, track, (SDL_Color){0, 60, 25, 180});
        draw_filled_rect(renderer, thumb, (SDL_Color){0, 255, 90, 220});

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void CLASSIFICATION_draw_case_suggestions(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect field_rect) {
    /*
        Purpose: Draws the case suggestions
        Returns: No value
    */

    if (Global_Classification_Active_Field != CLASSIFICATION_FIELD_CASE_NUMBER) {

        return;

    }

    int matches[CLASSIFICATION_MAX_FILES];
    int count = CLASSIFICATION_build_case_matches(matches, CLASSIFICATION_MAX_FILES);

    if (count <= 0) {

        return;

    }

    int max_scroll = count - CLASSIFICATION_CASE_MAX_VISIBLE;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Classification_Case_Scroll < 0) {

        Global_Classification_Case_Scroll = 0;

    }

    if (Global_Classification_Case_Scroll > max_scroll) {

        Global_Classification_Case_Scroll = max_scroll;

    }

    int visible = count - Global_Classification_Case_Scroll;

    if (visible > CLASSIFICATION_CASE_MAX_VISIBLE) {

        visible = CLASSIFICATION_CASE_MAX_VISIBLE;

    }

    int mouse_x = 0;
    int mouse_y = 0;
    CLASSIFICATION_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    SDL_Rect bg = {field_rect.x, field_rect.y + field_rect.h, field_rect.w, visible * CLASSIFICATION_CASE_OPTION_H};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, bg, (SDL_Color){0, 0, 0, 245});
    draw_outline_rect(renderer, bg, (SDL_Color){0, 180, 70, 255});

    Global_Classification_Case_Hover = -1;

    for (int i = 0; i < visible; i++) {
        int match_pos = Global_Classification_Case_Scroll + i;

        if (match_pos < 0 || match_pos >= count) {

            continue;

        }

        int case_index = matches[match_pos];

        if (case_index < 0 || case_index >= Global_Classification_Case_Count) {

            continue;

        }

        SDL_Rect row = {field_rect.x, field_rect.y + field_rect.h + (i * CLASSIFICATION_CASE_OPTION_H), field_rect.w,
                        CLASSIFICATION_CASE_OPTION_H};

        int hovered = point_in_rect(mouse_x, mouse_y, row);

        if (hovered) {

            Global_Classification_Case_Hover = case_index;

        }

        draw_filled_rect(renderer, row, hovered ? (SDL_Color){0, 70, 30, 250} : (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer, row, hovered ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 130, 55, 255});

        char short_case[CLASSIFICATION_MAX_TEXT];
        CLASSIFICATION_short_text(font, Global_Classification_Case_Options[case_index], short_case, sizeof(short_case),
                                  row.w - 20);
        draw_text(renderer, font, short_case, row.x + 9, row.y + 6,
                  hovered ? (SDL_Color){235, 255, 240, 255} : (SDL_Color){190, 220, 195, 255});
    }

    if (count > visible) {

        int track_h = bg.h - 8;
        int scroll_x = bg.x + bg.w - 8;
        int scroll_y = bg.y + 4;
        int scroll_w = 4;
        int thumb_h = (visible * track_h) / count;

        if (thumb_h < 18) {

            thumb_h = 18;

        }

        if (thumb_h > track_h) {

            thumb_h = track_h;

        }

        int max = count - visible;
        int thumb_y = scroll_y;

        if (max > 0) {

            thumb_y = scroll_y + (Global_Classification_Case_Scroll * (track_h - thumb_h)) / max;

        }

        draw_filled_rect(renderer, (SDL_Rect){scroll_x, scroll_y, scroll_w, track_h}, (SDL_Color){0, 60, 25, 180});
        draw_filled_rect(renderer, (SDL_Rect){scroll_x - 1, thumb_y, scroll_w + 2, thumb_h},
                         (SDL_Color){0, 255, 90, 220});

    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void CLASSIFICATION_draw_country_suggestions(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect field_rect) {
    /*
        Purpose: Draws the country suggestions
        Returns: No value
    */

    if (Global_Classification_Active_Field != CLASSIFICATION_FIELD_COUNTRY) {

        return;

    }

    int matches[512];
    int count = CLASSIFICATION_build_country_matches(matches, 512);

    if (count <= 0) {

        return;

    }

    int max_scroll = count - CLASSIFICATION_COUNTRY_MAX_VISIBLE;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Classification_Country_Scroll < 0) {

        Global_Classification_Country_Scroll = 0;

    }

    if (Global_Classification_Country_Scroll > max_scroll) {

        Global_Classification_Country_Scroll = max_scroll;

    }

    int visible = count - Global_Classification_Country_Scroll;

    if (visible > CLASSIFICATION_COUNTRY_MAX_VISIBLE) {

        visible = CLASSIFICATION_COUNTRY_MAX_VISIBLE;

    }

    int mouse_x = 0;
    int mouse_y = 0;
    CLASSIFICATION_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    SDL_Rect bg = {field_rect.x, field_rect.y + field_rect.h, field_rect.w, visible * CLASSIFICATION_COUNTRY_OPTION_H};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, bg, (SDL_Color){0, 0, 0, 245});
    draw_outline_rect(renderer, bg, (SDL_Color){0, 180, 70, 255});

    Global_Classification_Country_Hover = -1;

    for (int i = 0; i < visible; i++) {
        int match_pos = Global_Classification_Country_Scroll + i;

        if (match_pos < 0 || match_pos >= count) {

            continue;

        }

        int country_index = matches[match_pos];
        const Type_Classification_Country_Option *country = &CLASSIFICATION_COUNTRIES[country_index];

        SDL_Rect row = {field_rect.x, field_rect.y + field_rect.h + (i * CLASSIFICATION_COUNTRY_OPTION_H), field_rect.w,
                        CLASSIFICATION_COUNTRY_OPTION_H};

        int hovered = point_in_rect(mouse_x, mouse_y, row);

        if (hovered) {

            Global_Classification_Country_Hover = country_index;

        }

        draw_filled_rect(renderer, row, hovered ? (SDL_Color){0, 70, 30, 250} : (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer, row, hovered ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 130, 55, 255});

        SDL_Rect flag_rect = {row.x + row.w - 70, row.y + 6, 60, row.h - 12};
        CLASSIFICATION_draw_flag_box(renderer, font, flag_rect, country->alpha2);

        char short_name[CLASSIFICATION_MAX_TEXT];
        CLASSIFICATION_short_text(font, country->name, short_name, sizeof(short_name), row.w - 70);
        draw_text(renderer, font, short_name, row.x + 9, row.y + 7,
                  hovered ? (SDL_Color){235, 255, 240, 255} : (SDL_Color){190, 220, 195, 255});
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void CLASSIFICATION_draw_save_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, int hovered) {
    /*
        Purpose: Draws the save button
        Returns: No value
    */

    SDL_Color fill = hovered ? (SDL_Color){0, 85, 32, 255} : (SDL_Color){0, 28, 10, 255};

    SDL_Color border = hovered ? (SDL_Color){0, 255, 90, 255} : (SDL_Color){0, 180, 60, 255};

    SDL_Color text = hovered ? (SDL_Color){235, 255, 240, 255} : (SDL_Color){0, 255, 90, 255};

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

    if (font && TTF_SizeText(font, "Save Case", &text_w, &text_h) != 0) {

        text_w = 0;
        text_h = 0;

    }

    draw_text(renderer, font, "Save Case", rect.x + (rect.w - text_w) / 2, rect.y + (rect.h - text_h) / 2, text);
}

void CLASSIFICATION_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the workstation
        Returns: No value
    */

    if (!renderer || !font) {

        return;

    }

    SDL_Rect full = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, full, (SDL_Color){0, 0, 0, 255});

    SDL_Rect file_rect;
    SDL_Rect form_rect;
    SDL_Rect field_rects[CLASSIFICATION_FIELD_COUNT];
    SDL_Rect save_rect;
    CLASSIFICATION_get_layout(win_w, win_h, &file_rect, &form_rect, field_rects, &save_rect);

    draw_text(renderer, font,
              "Signal Classification Workstation  |  H/Esc exits unless typing  "
              "|  R rescans  |  Enter saves when not typing",
              CLASSIFICATION_MARGIN, CLASSIFICATION_MARGIN, (SDL_Color){0, 255, 90, 255});

    draw_text(renderer, font, Global_Classification_Status, CLASSIFICATION_MARGIN, CLASSIFICATION_MARGIN + 26,
              (SDL_Color){150, 150, 150, 255});

    CLASSIFICATION_draw_panel(renderer, font, file_rect, "Recording Files");
    CLASSIFICATION_draw_panel(renderer, font, form_rect, "Signal Classification Fields");
    CLASSIFICATION_draw_file_search_button(renderer, font, win_w, win_h);

    int visible_files = (file_rect.h - 58) / CLASSIFICATION_ROW_HEIGHT;

    if (visible_files < 1) {

        visible_files = 1;

    }

    if (Global_Classification_File_Scroll + visible_files > Global_Classification_File_Count) {

        Global_Classification_File_Scroll = Global_Classification_File_Count - visible_files;

        if (Global_Classification_File_Scroll < 0) {

            Global_Classification_File_Scroll = 0;

        }

    }

    for (int i = 0; i < visible_files; i++) {
        int idx = Global_Classification_File_Scroll + i;

        if (idx >= Global_Classification_File_Count) {

            break;

        }

        SDL_Rect row = {file_rect.x + 6, file_rect.y + 44 + (i * CLASSIFICATION_ROW_HEIGHT), file_rect.w - 12,
                        CLASSIFICATION_ROW_HEIGHT - 2};

        CLASSIFICATION_draw_selectable_row(renderer, font, row, Global_Classification_Files[idx],
                                           idx == Global_Classification_Selected_File);
    }

    for (int i = 0; i < CLASSIFICATION_FIELD_COUNT; i++) {
        CLASSIFICATION_draw_input_field(
            renderer, font, field_rects[i], CLASSIFICATION_FIELD_LABELS[i], Global_Classification_Field_Text[i],
            i == Global_Classification_Active_Field || i == Global_Classification_Open_Dropdown,
            CLASSIFICATION_is_dropdown_field(i), i);
    }

    draw_text(renderer, font, "Database record: CASE_<Case #>.csv  |  same Case # appends multiple signals",
              form_rect.x + 10, form_rect.y - 25, (SDL_Color){0, 255, 90, 255});

    if (Global_Classification_Open_Dropdown != CLASSIFICATION_DROPDOWN_NONE) {

        CLASSIFICATION_draw_dropdown(renderer, font, field_rects[Global_Classification_Open_Dropdown],
                                     Global_Classification_Open_Dropdown);

    }

    CLASSIFICATION_draw_case_suggestions(renderer, font, field_rects[CLASSIFICATION_FIELD_CASE_NUMBER]);

    CLASSIFICATION_draw_country_suggestions(renderer, font, field_rects[CLASSIFICATION_FIELD_COUNTRY]);

    int mouse_x = 0;
    int mouse_y = 0;
    CLASSIFICATION_get_adjusted_mouse_state(&mouse_x, &mouse_y);

    int save_hovered = point_in_rect(mouse_x, mouse_y, save_rect);

    CLASSIFICATION_draw_save_button(renderer, font, save_rect, save_hovered);

    if (Global_Classification_Save_Message[0]) {

        Uint64 now = SDL_GetTicks64();

        if (Global_Classification_Save_Message_Time == 0 || now - Global_Classification_Save_Message_Time <= 3000) {

            int msg_w = 0;
            int msg_h = 0;

            if (font && TTF_SizeText(font, Global_Classification_Save_Message, &msg_w, &msg_h) != 0) {

                msg_w = 0;
                msg_h = 0;

            }

            int msg_x = save_rect.x + (save_rect.w - msg_w) / 2;
            int msg_y = save_rect.y - msg_h - 10;

            if (msg_x < form_rect.x + 16) {

                msg_x = form_rect.x + 16;

            }

            if (msg_x + msg_w > form_rect.x + form_rect.w - 16) {

                msg_x = form_rect.x + form_rect.w - 16 - msg_w;

            }

            if (msg_y < form_rect.y + 36) {

                msg_y = form_rect.y + 36;

            }

            draw_text(renderer, font, Global_Classification_Save_Message, msg_x, msg_y, (SDL_Color){0, 255, 90, 255});

        }

        else {

            Global_Classification_Save_Message[0] = '\0';
            Global_Classification_Save_Message_Time = 0;

        }

    }

    CLASSIFICATION_draw_file_search_popup(renderer, font, win_w, win_h);
}
