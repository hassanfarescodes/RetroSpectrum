/*
 * ============================================================================
 * File:            CaseManagementWorkstation.c
 * Author:          Hassan Fares
 *
 * Description:     Case management block-graph workstation for RetroSpectrum.
 *                  Cases are represented as GNU Radio-style blocks that can be
 *                  created, moved, edited, linked together, and scheduled.
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include "CaseManagementWorkstation.h"
#include "GUIs.h"

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <dirent.h>

#ifndef SDL_MAJOR_VERSION
extern char *SDL_GetClipboardText(void);
extern void SDL_free(void *mem);
#endif

#define CASE_MGMT_MAX_BLOCKS        128
#define CASE_MGMT_MAX_LINKS         256
#define CASE_MGMT_TEXT_MAX          192
#define CASE_MGMT_DESCRIPTION_MAX   1024
#define CASE_MGMT_SOURCE_FILE_MAX   512
#define CASE_MGMT_DATE_MAX          16
#define CASE_MGMT_SAVE_DIR          "CaseManagement"
#define CASE_MGMT_BLOCKS_CSV        "CaseManagement/CASE_BLOCKS.csv"
#define CASE_MGMT_LINKS_CSV         "CaseManagement/CASE_LINKS.csv"
#define CASE_MGMT_BLOCK_W           270
#define CASE_MGMT_BLOCK_H           126
#define CASE_MGMT_CONNECTOR_SIZE    10
#define CASE_MGMT_FIELD_COUNT       10
#define CASE_MGMT_FIELD_NONE       -1
#define CASE_MGMT_MARGIN            20
#define CASE_MGMT_TOOLBAR_H         46
#define CASE_MGMT_EDITOR_W          340
#define CASE_MGMT_STATUS_COUNT      5
#define CASE_MGMT_STATUS_OPTION_H   28
#define CASE_MGMT_CALENDAR_H        250
#define CASE_MGMT_SOURCE_MAX_FILES  512
#define CASE_MGMT_SOURCE_VISIBLE    18
#define CASE_MGMT_CASE_MAX_VISIBLE   6
#define CASE_MGMT_CASE_OPTION_H      30
#define CASE_MGMT_COUNTRY_MAX_VISIBLE 6
#define CASE_MGMT_COUNTRY_OPTION_H   30
#define CASE_MGMT_CLASSIFICATION_DIR "Classification"
#define CASE_MGMT_SOURCE_ROW_H      30
#define CASE_MGMT_MIN_ZOOM          0.45
#define CASE_MGMT_MAX_ZOOM          1.60

#ifndef SDLK_v
#define SDLK_v 'v'
#endif

#ifndef SDLK_d
#define SDLK_d 'd'
#endif

#ifndef SDL_BUTTON_RIGHT
#define SDL_BUTTON_RIGHT 3
#endif

#ifndef SDL_BUTTON_MIDDLE
#define SDL_BUTTON_MIDDLE 2
#endif

#ifndef KMOD_SHIFT
#define KMOD_SHIFT 0x0003
#endif

#ifndef RETROSPECTRUM_DASHBOARD_TAB_BAR_H
#define RETROSPECTRUM_DASHBOARD_TAB_BAR_H 56
#endif

int Global_CaseManagement_Mode = 0;

typedef struct Type_Case_Block {
    int id;
    int x;
    int y;
    char case_number[128];
    char country[128];
    char task[CASE_MGMT_TEXT_MAX];
    char assigned_to[CASE_MGMT_TEXT_MAX];
    char start_date[CASE_MGMT_DATE_MAX];
    char end_date[CASE_MGMT_DATE_MAX];
    char status[CASE_MGMT_TEXT_MAX];
    char priority[16];
    char source_file[CASE_MGMT_SOURCE_FILE_MAX];
    char description[CASE_MGMT_DESCRIPTION_MAX];
} Type_Case_Block;

typedef struct Type_Case_Link {
    int from_id;
    int to_id;
    int from_side;
    int to_side;
} Type_Case_Link;

enum {
    CASE_MGMT_FIELD_CASE_NUMBER = 0,
    CASE_MGMT_FIELD_COUNTRY,
    CASE_MGMT_FIELD_TASK,
    CASE_MGMT_FIELD_USER,
    CASE_MGMT_FIELD_START_DATE,
    CASE_MGMT_FIELD_END_DATE,
    CASE_MGMT_FIELD_STATUS,
    CASE_MGMT_FIELD_PRIORITY,
    CASE_MGMT_FIELD_SOURCE_FILE,
    CASE_MGMT_FIELD_DESCRIPTION
};

enum {
    CASE_MGMT_SIDE_LEFT = 0,
    CASE_MGMT_SIDE_RIGHT = 1,
    CASE_MGMT_SIDE_TOP = 2,
    CASE_MGMT_SIDE_BOTTOM = 3
};

static Type_Case_Block Global_Case_Blocks[CASE_MGMT_MAX_BLOCKS];
static Type_Case_Link  Global_Case_Links[CASE_MGMT_MAX_LINKS];
static int Global_Case_Block_Count = 0;
static int Global_Case_Link_Count = 0;
static int Global_Case_Selected = -1;
static int Global_Case_Selected_Link = -1;
static int Global_Case_Selected_Blocks[CASE_MGMT_MAX_BLOCKS];
static int Global_Case_Box_Selecting = 0;
static int Global_Case_Box_Start_X = 0;
static int Global_Case_Box_Start_Y = 0;
static int Global_Case_Box_End_X = 0;
static int Global_Case_Box_End_Y = 0;
static int Global_Case_Drag_Last_World_X = 0;
static int Global_Case_Drag_Last_World_Y = 0;
static int Global_Case_Next_Id = 1;
static int Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
static int Global_Case_Field_Cursor[CASE_MGMT_FIELD_COUNT] = {0, 0, 0, 0, 0};
static int Global_Case_Dragging = 0;
static int Global_Case_Drag_Off_X = 0;
static int Global_Case_Drag_Off_Y = 0;
static int Global_Case_Panning = 0;
static int Global_Case_Pan_Last_X = 0;
static int Global_Case_Pan_Last_Y = 0;
static int Global_Case_Link_Dragging = 0;
static int Global_Case_Link_Drag_Start_Index = -1;
static int Global_Case_Link_Drag_Start_Side = -1;
static int Global_Case_Link_Drag_Mouse_X = 0;
static int Global_Case_Link_Drag_Mouse_Y = 0;
static int Global_Case_Link_Drag_Target_Index = -1;
static int Global_Case_Link_Drag_Target_Side = -1;
static int Global_Case_Link_Mode = 0;
static int Global_Case_Link_Source = -1;
static int Global_Case_Status_Dropdown_Open = 0;
static int Global_Case_Status_Dropdown_Hover = -1;
static int Global_Case_Source_Popup_Open = 0;
static int Global_Case_Source_Hover = -1;
static int Global_Case_Source_Scroll = 0;
static int Global_Case_Source_File_Count = 0;
static char Global_Case_Source_Files[CASE_MGMT_SOURCE_MAX_FILES][CASE_MGMT_SOURCE_FILE_MAX];
static char Global_Case_Source_Search[128] = "";
static int Global_Case_Source_Search_Cursor = 0;
static int Global_Case_Source_Search_Active = 0;
static char Global_Case_Case_Options[CASE_MGMT_SOURCE_MAX_FILES][128];
static int Global_Case_Case_Count = 0;
static int Global_Case_Case_Scroll = 0;
static int Global_Case_Case_Hover = -1;
static int Global_Case_Case_Dropdown_Open = 0;
static int Global_Case_Country_Scroll = 0;
static int Global_Case_Country_Hover = -1;
static int Global_Case_Country_Dropdown_Open = 0;
static char Global_Case_Record_Dir[512] = "Recordings";
static int Global_Case_Description_Popup_Open = 0;
static int Global_Case_Description_Popup_Scroll = 0;
static int Global_Case_Description_Selecting = 0;
static int Global_Case_Description_Selection_Start = -1;
static int Global_Case_Description_Selection_End = -1;
static TTF_Font *Global_Case_Description_Font = NULL;
static int Global_Case_Description_Wrap_Px = 0;
static int Global_Case_Calendar_Open = 0;
static int Global_Case_Calendar_Field = CASE_MGMT_FIELD_NONE;
static int Global_Case_Calendar_Month = 1;
static int Global_Case_Calendar_Year = 2026;
static int Global_Case_View_Initialized = 0;
static double Global_Case_Zoom = 1.0;
static double Global_Case_View_X = 0.0;
static double Global_Case_View_Y = 0.0;
static char Global_Case_Status[256] = "N: new block | Drag canvas: pan | Wheel: zoom | Drag connector: link | CTRL+S: save";
static Uint64 Global_Case_Status_Time = 0;

static SDL_Color Case_BG        = {0,   0,   0,   255};
static SDL_Color Case_Panel     = {0,   10,  4,   245};
static SDL_Color Case_Panel_2   = {0,   18,  8,   255};
static SDL_Color Case_Border    = {0,   150, 60,  255};
static SDL_Color Case_Border_Hi = {0,   255, 90,  255};
static SDL_Color Case_Text      = {0,   255, 90,  255};
static SDL_Color Case_Muted     = {0,   155, 65,  255};
static SDL_Color Case_Warn      = {255, 180, 40,  255};
static SDL_Color Case_Red       = {255, 75,  55,  255};
static SDL_Color Case_Blue      = {70,  190, 255, 255};

static void case_copy_text(char *dst, size_t dst_size, const char *src);
static int case_description_delete_selection(void);
static void case_description_clear_selection(void);
static void case_clear_block_selection(void);
static void case_select_only_block(int index);
static void case_sync_primary_selection(void);
static int case_selected_block_count(void);
static int case_is_block_selected(int index);
static char *case_selected_field_text(int field);

static const char *CASE_MGMT_STATUS_OPTIONS[CASE_MGMT_STATUS_COUNT] = {
    "Todo",
    "In Progress",
    "Review",
    "Done",
    "Blocked"
};

typedef struct Type_Case_Country_Option {
    const char *name;
    const char *alpha2;
} Type_Case_Country_Option;

static const Type_Case_Country_Option CASE_MGMT_COUNTRIES[] = {
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

static int case_country_count(void){
    return (int)(sizeof(CASE_MGMT_COUNTRIES) / sizeof(CASE_MGMT_COUNTRIES[0]));
}

static void case_get_adjusted_mouse_state(int *x, int *y){
    SDL_GetMouseState(x, y);
    if (y) *y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;
}

static int case_is_complex16_file(const char *name){
    size_t len;
    const char *suffix = ".complex16";
    size_t suffix_len = strlen(suffix);

    if (!name) return 0;
    len = strlen(name);
    if (len < suffix_len) return 0;
    return strcmp(name + len - suffix_len, suffix) == 0;
}

static int case_name_compare(const void *a, const void *b){
    const char *sa = (const char *)a;
    const char *sb = (const char *)b;
    return strcmp(sa, sb);
}

static void case_scan_source_files(void){
    DIR *dir = opendir(Global_Case_Record_Dir);
    struct dirent *entry;

    Global_Case_Source_File_Count = 0;
    Global_Case_Source_Scroll = 0;
    Global_Case_Source_Hover = -1;

    if (!dir) return;

    while ((entry = readdir(dir)) != NULL &&
           Global_Case_Source_File_Count < CASE_MGMT_SOURCE_MAX_FILES) {
        if (entry->d_name[0] == '.') continue;
        if (!case_is_complex16_file(entry->d_name)) continue;

        case_copy_text(Global_Case_Source_Files[Global_Case_Source_File_Count],
                       sizeof(Global_Case_Source_Files[Global_Case_Source_File_Count]),
                       entry->d_name);
        Global_Case_Source_File_Count++;
    }

    closedir(dir);

    if (Global_Case_Source_File_Count > 1) {
        qsort(Global_Case_Source_Files,
              (size_t)Global_Case_Source_File_Count,
              sizeof(Global_Case_Source_Files[0]),
              case_name_compare);
    }
}

static int case_point_in_rect(int x, int y, SDL_Rect r){
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void case_copy_text(char *dst, size_t dst_size, const char *src){
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_size, "%s", src);
}

static double case_limit_double(double value, double low, double high){
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void case_set_status(const char *status, SDL_Color color){
    (void)color;
    case_copy_text(Global_Case_Status, sizeof(Global_Case_Status), status);
    Global_Case_Status_Time = SDL_GetTicks64();
}

static void case_draw_text_centered(SDL_Renderer *renderer,
                                    TTF_Font *font,
                                    const char *text,
                                    SDL_Rect rect,
                                    SDL_Color color){
    int tw = 0;
    int th = 0;

    if (!font || !text) return;
    if (TTF_SizeText(font, text, &tw, &th) != 0) {
        tw = 0;
        th = 0;
    }

    draw_text(renderer,
              font,
              text,
              rect.x + (rect.w - tw) / 2,
              rect.y + (rect.h - th) / 2,
              color);
}

static void case_draw_button(SDL_Renderer *renderer,
                             TTF_Font *font,
                             SDL_Rect rect,
                             const char *label,
                             int active,
                             int hovered,
                             int danger){
    int hot = active || hovered;
    SDL_Color fill = hot ? (SDL_Color){0, 32, 13, 255} : Case_Panel;
    SDL_Color border = danger ? Case_Red : (hot ? Case_Border_Hi : Case_Border);
    SDL_Color text = danger ? Case_Red : (hot ? Case_Text : Case_Muted);

    if (hot) {
        SDL_Rect halo_outer = {rect.x - 7, rect.y - 7, rect.w + 14, rect.h + 14};
        SDL_Rect halo_mid   = {rect.x - 5, rect.y - 5, rect.w + 10, rect.h + 10};
        SDL_Rect halo_inner = {rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6};
        draw_outline_rect(renderer, halo_outer, (SDL_Color){0, 60, 24, 255});
        draw_outline_rect(renderer, halo_mid,   (SDL_Color){0, 120, 48, 255});
        draw_outline_rect(renderer, halo_inner, border);
    }

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);

    if (hot) {
        SDL_Rect inner = {rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6};
        draw_outline_rect(renderer, inner, border);
    }

    case_draw_text_centered(renderer, font, label, rect, text);
}

static void case_shorten(const char *src, char *dst, size_t dst_size, size_t max_chars){
    size_t len = 0;

    if (!dst || dst_size == 0) return;
    if (!src) src = "";

    while (src[len] && len < max_chars && len + 1 < dst_size) {
        dst[len] = src[len];
        len++;
    }

    if (src[len] && len + 4 < dst_size && max_chars > 3) {
        dst[len++] = '.';
        dst[len++] = '.';
        dst[len++] = '.';
    }

    dst[len] = '\0';
}

static SDL_Rect case_block_screen_rect(int index, SDL_Rect canvas);
static int case_find_block_index_by_id(int id){
    for (int i = 0; i < Global_Case_Block_Count; i++) {
        if (Global_Case_Blocks[i].id == id) return i;
    }
    return -1;
}


static void case_clear_block_selection(void){
    memset(Global_Case_Selected_Blocks, 0, sizeof(Global_Case_Selected_Blocks));
    Global_Case_Selected = -1;
}

static int case_is_block_selected(int index){
    if (index < 0 || index >= Global_Case_Block_Count) return 0;
    return Global_Case_Selected_Blocks[index] != 0;
}

static int case_selected_block_count(void){
    int count = 0;
    for (int i = 0; i < Global_Case_Block_Count; i++) {
        if (Global_Case_Selected_Blocks[i]) count++;
    }
    return count;
}

static void case_sync_primary_selection(void){
    if (Global_Case_Selected >= 0 &&
        Global_Case_Selected < Global_Case_Block_Count &&
        Global_Case_Selected_Blocks[Global_Case_Selected]) {
        return;
    }

    Global_Case_Selected = -1;
    for (int i = 0; i < Global_Case_Block_Count; i++) {
        if (Global_Case_Selected_Blocks[i]) {
            Global_Case_Selected = i;
            return;
        }
    }
}

static void case_select_only_block(int index){
    case_clear_block_selection();
    if (index >= 0 && index < Global_Case_Block_Count) {
        Global_Case_Selected_Blocks[index] = 1;
        Global_Case_Selected = index;
    }
}

static void case_toggle_block_selection(int index){
    if (index < 0 || index >= Global_Case_Block_Count) return;
    Global_Case_Selected_Blocks[index] = !Global_Case_Selected_Blocks[index];
    case_sync_primary_selection();
}

static int case_rects_intersect(SDL_Rect a, SDL_Rect b){
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

static SDL_Rect case_make_normalized_rect(int x0, int y0, int x1, int y1){
    SDL_Rect r;
    r.x = x0 < x1 ? x0 : x1;
    r.y = y0 < y1 ? y0 : y1;
    r.w = abs(x1 - x0);
    r.h = abs(y1 - y0);
    return r;
}

static void case_select_blocks_in_rect(SDL_Rect canvas, SDL_Rect selection, int additive){
    int matched = 0;
    if (!additive) case_clear_block_selection();

    for (int i = 0; i < Global_Case_Block_Count; i++) {
        SDL_Rect br = case_block_screen_rect(i, canvas);
        if (case_rects_intersect(selection, br)) {
            Global_Case_Selected_Blocks[i] = 1;
            matched++;
        }
    }

    case_sync_primary_selection();
    Global_Case_Selected_Link = -1;
    if (matched > 1) case_set_status("Selected multiple blocks", Case_Text);
    else if (matched == 1) case_set_status("Selected block", Case_Text);
    else if (!additive) case_set_status("Cleared block selection", Case_Muted);
}

static SDL_Rect case_canvas_rect(int win_w, int win_h){
    SDL_Rect rect = {
        CASE_MGMT_MARGIN,
        CASE_MGMT_MARGIN + CASE_MGMT_TOOLBAR_H + 10,
        win_w - CASE_MGMT_EDITOR_W - (CASE_MGMT_MARGIN * 3),
        win_h - (CASE_MGMT_MARGIN * 3) - CASE_MGMT_TOOLBAR_H - 18
    };

    if (rect.w < 480) rect.w = win_w - (CASE_MGMT_MARGIN * 2);
    if (rect.h < 260) rect.h = 260;
    return rect;
}

static SDL_Rect case_editor_rect(int win_w, int win_h){
    SDL_Rect rect = {
        win_w - CASE_MGMT_EDITOR_W - CASE_MGMT_MARGIN,
        CASE_MGMT_MARGIN + CASE_MGMT_TOOLBAR_H + 10,
        CASE_MGMT_EDITOR_W,
        win_h - (CASE_MGMT_MARGIN * 3) - CASE_MGMT_TOOLBAR_H - 18
    };

    if (rect.x < CASE_MGMT_MARGIN + 480) rect.x = win_w + 1000;
    if (rect.h < 260) rect.h = 260;
    return rect;
}

static void case_ensure_view(SDL_Rect canvas){
    if (Global_Case_View_Initialized) return;
    Global_Case_View_X = (double)canvas.x;
    Global_Case_View_Y = (double)canvas.y;
    Global_Case_View_Initialized = 1;
}

static int case_world_to_screen_x(SDL_Rect canvas, int world_x){
    return canvas.x + (int)(((double)world_x - Global_Case_View_X) * Global_Case_Zoom);
}

static int case_world_to_screen_y(SDL_Rect canvas, int world_y){
    return canvas.y + (int)(((double)world_y - Global_Case_View_Y) * Global_Case_Zoom);
}

static int case_screen_to_world_x(SDL_Rect canvas, int screen_x){
    return (int)(Global_Case_View_X + ((double)screen_x - (double)canvas.x) / Global_Case_Zoom);
}

static int case_screen_to_world_y(SDL_Rect canvas, int screen_y){
    return (int)(Global_Case_View_Y + ((double)screen_y - (double)canvas.y) / Global_Case_Zoom);
}

static SDL_Rect case_block_world_rect(int index){
    SDL_Rect r = {
        Global_Case_Blocks[index].x,
        Global_Case_Blocks[index].y,
        CASE_MGMT_BLOCK_W,
        CASE_MGMT_BLOCK_H
    };
    return r;
}

static int case_block_at(int x, int y, SDL_Rect canvas);
static int case_link_at(int x, int y, SDL_Rect canvas);

static SDL_Rect case_block_screen_rect(int index, SDL_Rect canvas){
    SDL_Rect r = {
        case_world_to_screen_x(canvas, Global_Case_Blocks[index].x),
        case_world_to_screen_y(canvas, Global_Case_Blocks[index].y),
        (int)((double)CASE_MGMT_BLOCK_W * Global_Case_Zoom),
        (int)((double)CASE_MGMT_BLOCK_H * Global_Case_Zoom)
    };

    if (r.w < 70) r.w = 70;
    if (r.h < 42) r.h = 42;
    return r;
}

static int case_connector_px(void){
    int connector = (int)((double)CASE_MGMT_CONNECTOR_SIZE * Global_Case_Zoom);
    if (connector < 6) connector = 6;
    if (connector > 14) connector = 14;
    return connector;
}

static SDL_Rect case_block_endpoint_rect(int index, SDL_Rect canvas, int side, int generous){
    SDL_Rect b = case_block_screen_rect(index, canvas);
    int connector = case_connector_px();
    int hit = generous ? 30 : connector;
    int cx = b.x;
    int cy = b.y + b.h / 2;

    if (side == CASE_MGMT_SIDE_RIGHT) {
        cx = b.x + b.w;
        cy = b.y + b.h / 2;
    }
    else if (side == CASE_MGMT_SIDE_TOP) {
        cx = b.x + b.w / 2;
        cy = b.y;
    }
    else if (side == CASE_MGMT_SIDE_BOTTOM) {
        cx = b.x + b.w / 2;
        cy = b.y + b.h;
    }

    SDL_Rect r = {cx - hit / 2, cy - hit / 2, hit, hit};
    return r;
}

static void case_endpoint_center(int index, SDL_Rect canvas, int side, int *x, int *y){
    SDL_Rect b = case_block_screen_rect(index, canvas);
    int cx = b.x;
    int cy = b.y + b.h / 2;

    if (side == CASE_MGMT_SIDE_RIGHT) {
        cx = b.x + b.w;
        cy = b.y + b.h / 2;
    }
    else if (side == CASE_MGMT_SIDE_TOP) {
        cx = b.x + b.w / 2;
        cy = b.y;
    }
    else if (side == CASE_MGMT_SIDE_BOTTOM) {
        cx = b.x + b.w / 2;
        cy = b.y + b.h;
    }

    if (x) *x = cx;
    if (y) *y = cy;
}

static int case_endpoint_at(int x, int y, SDL_Rect canvas, int *side_out){
    for (int i = Global_Case_Block_Count - 1; i >= 0; i--) {
        for (int side = 0; side < 4; side++) {
            SDL_Rect r = case_block_endpoint_rect(i, canvas, side, 1);
            if (case_point_in_rect(x, y, r)) {
                if (side_out) *side_out = side;
                return i;
            }
        }
    }
    return -1;
}

static int case_nearest_endpoint(int x, int y, SDL_Rect canvas, int exclude_index, int *side_out){
    int best_index = -1;
    int best_side = -1;
    int best_dist2 = 42 * 42;

    for (int i = 0; i < Global_Case_Block_Count; i++) {
        if (i == exclude_index) continue;
        for (int side = 0; side < 4; side++) {
            int ex = 0;
            int ey = 0;
            int dx;
            int dy;
            int dist2;
            case_endpoint_center(i, canvas, side, &ex, &ey);
            dx = x - ex;
            dy = y - ey;
            dist2 = dx * dx + dy * dy;
            if (dist2 < best_dist2) {
                best_dist2 = dist2;
                best_index = i;
                best_side = side;
            }
        }
    }

    if (best_index < 0) {
        int block = case_block_at(x, y, canvas);
        if (block >= 0 && block != exclude_index) {
            best_index = block;
            best_side = CASE_MGMT_SIDE_LEFT;
        }
    }

    if (side_out) *side_out = best_side;
    return best_index;
}

static int case_block_at(int x, int y, SDL_Rect canvas){
    for (int i = Global_Case_Block_Count - 1; i >= 0; i--) {
        if (case_point_in_rect(x, y, case_block_screen_rect(i, canvas))) return i;
    }
    return -1;
}

static SDL_Color case_status_color(const char *status){
    if (!status) status = "";
    if (strcmp(status, "Done") == 0) return (SDL_Color){0, 255, 90, 255};
    if (strcmp(status, "In Progress") == 0) return Case_Blue;
    if (strcmp(status, "Blocked") == 0) return Case_Red;
    if (strcmp(status, "Review") == 0) return Case_Warn;
    return Case_Muted;
}

static void case_make_timeline_text(const Type_Case_Block *b, char *out, size_t out_size){
    if (!out || out_size == 0) return;
    if (!b) {
        out[0] = '\0';
        return;
    }

    if (b->start_date[0] && b->end_date[0]) {
        snprintf(out, out_size, "%s - %s", b->start_date, b->end_date);
    }
    else if (b->start_date[0]) {
        snprintf(out, out_size, "%s - TBD", b->start_date);
    }
    else if (b->end_date[0]) {
        snprintf(out, out_size, "TBD - %s", b->end_date);
    }
    else {
        snprintf(out, out_size, "TBD");
    }
}

static void case_seed_default_blocks(void){
    if (Global_Case_Block_Count > 0) return;

    Type_Case_Block a = {0};
    a.id = Global_Case_Next_Id++;
    a.x = 70;
    a.y = 110;
    case_copy_text(a.task, sizeof(a.task), "Open case / define objective");
    case_copy_text(a.assigned_to, sizeof(a.assigned_to), "Analyst 1");
    case_copy_text(a.start_date, sizeof(a.start_date), "06/25/2026");
    case_copy_text(a.end_date, sizeof(a.end_date), "06/25/2026");
    case_copy_text(a.status, sizeof(a.status), "Todo");
    case_copy_text(a.priority, sizeof(a.priority), "3");
    a.source_file[0] = '\0';
    case_copy_text(a.description, sizeof(a.description), "Define the case objective and initial collection requirements.");
    Global_Case_Blocks[Global_Case_Block_Count++] = a;

    Type_Case_Block b = {0};
    b.id = Global_Case_Next_Id++;
    b.x = 390;
    b.y = 110;
    case_copy_text(b.task, sizeof(b.task), "Review signals and evidence");
    case_copy_text(b.assigned_to, sizeof(b.assigned_to), "RF Analyst");
    case_copy_text(b.start_date, sizeof(b.start_date), "06/25/2026");
    case_copy_text(b.end_date, sizeof(b.end_date), "06/26/2026");
    case_copy_text(b.status, sizeof(b.status), "In Progress");
    case_copy_text(b.priority, sizeof(b.priority), "2");
    b.source_file[0] = '\0';
    case_copy_text(b.description, sizeof(b.description), "Review captured source material and extract relevant evidence.");
    Global_Case_Blocks[Global_Case_Block_Count++] = b;

    Type_Case_Block c = {0};
    c.id = Global_Case_Next_Id++;
    c.x = 710;
    c.y = 110;
    case_copy_text(c.task, sizeof(c.task), "Write assessment");
    case_copy_text(c.assigned_to, sizeof(c.assigned_to), "Lead");
    case_copy_text(c.start_date, sizeof(c.start_date), "06/27/2026");
    case_copy_text(c.end_date, sizeof(c.end_date), "06/27/2026");
    case_copy_text(c.status, sizeof(c.status), "Review");
    case_copy_text(c.priority, sizeof(c.priority), "1");
    c.source_file[0] = '\0';
    case_copy_text(c.description, sizeof(c.description), "Prepare the assessment and summarize findings.");
    Global_Case_Blocks[Global_Case_Block_Count++] = c;

    Global_Case_Links[Global_Case_Link_Count++] = (Type_Case_Link){a.id, b.id, CASE_MGMT_SIDE_RIGHT, CASE_MGMT_SIDE_LEFT};
    Global_Case_Links[Global_Case_Link_Count++] = (Type_Case_Link){b.id, c.id, CASE_MGMT_SIDE_RIGHT, CASE_MGMT_SIDE_LEFT};
    case_select_only_block(0);
}

static int case_ensure_dir(void){
    struct stat st;

    if (stat(CASE_MGMT_SAVE_DIR, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    return mkdir(CASE_MGMT_SAVE_DIR, 0755) == 0;
}

static void case_csv_write_field(FILE *fp, const char *text){
    fputc('"', fp);
    if (!text) text = "";
    for (const char *p = text; *p; p++) {
        if (*p == '"') fputc('"', fp);
        fputc(*p, fp);
    }
    fputc('"', fp);
}

static void case_csv_write_multiline_field(FILE *fp, const char *text){
    fputc('"', fp);
    if (!text) text = "";
    for (const char *p = text; *p; p++) {
        if (*p == '"') fputc('"', fp);
        if (*p == '\n') {
            fputc('\\', fp);
            fputc('n', fp);
        }
        else if (*p != '\r') {
            fputc(*p, fp);
        }
    }
    fputc('"', fp);
}

static void case_unescape_multiline(char *text){
    char *src;
    char *dst;

    if (!text) return;

    src = text;
    dst = text;
    while (*src) {
        if (src[0] == '\\' && src[1] == 'n') {
            *dst++ = '\n';
            src += 2;
        }
        else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void case_save(void){
    if (!case_ensure_dir()) {
        case_set_status("Could not create CaseManagement directory", Case_Red);
        return;
    }

    FILE *blocks = fopen(CASE_MGMT_BLOCKS_CSV, "w");
    if (!blocks) {
        case_set_status("Could not write CASE_BLOCKS.csv", Case_Red);
        return;
    }

    fprintf(blocks, "id,x,y,case_number,country,task,assigned_to,start_date,end_date,status,priority,source_file,description\n");
    for (int i = 0; i < Global_Case_Block_Count; i++) {
        Type_Case_Block *b = &Global_Case_Blocks[i];
        fprintf(blocks, "%d,%d,%d,", b->id, b->x, b->y);
        case_csv_write_field(blocks, b->case_number);
        fputc(',', blocks);
        case_csv_write_field(blocks, b->country);
        fputc(',', blocks);
        case_csv_write_field(blocks, b->task);
        fputc(',', blocks);
        case_csv_write_field(blocks, b->assigned_to);
        fputc(',', blocks);
        case_csv_write_field(blocks, b->start_date);
        fputc(',', blocks);
        case_csv_write_field(blocks, b->end_date);
        fputc(',', blocks);
        case_csv_write_field(blocks, b->status);
        fputc(',', blocks);
        case_csv_write_field(blocks, b->priority);
        fputc(',', blocks);
        case_csv_write_field(blocks, b->source_file);
        fputc(',', blocks);
        case_csv_write_multiline_field(blocks, b->description);
        fputc('\n', blocks);
    }
    fclose(blocks);

    FILE *links = fopen(CASE_MGMT_LINKS_CSV, "w");
    if (!links) {
        case_set_status("Could not write CASE_LINKS.csv", Case_Red);
        return;
    }

    fprintf(links, "from_id,to_id,from_side,to_side\n");
    for (int i = 0; i < Global_Case_Link_Count; i++) {
        fprintf(links, "%d,%d,%d,%d\n",
                Global_Case_Links[i].from_id,
                Global_Case_Links[i].to_id,
                Global_Case_Links[i].from_side,
                Global_Case_Links[i].to_side);
    }
    fclose(links);

    case_set_status("Case graph saved", Case_Text);
}

static char *case_read_csv_field(char **cursor, char *out, size_t out_size){
    size_t n = 0;
    char *p;

    if (!cursor || !*cursor || !out || out_size == 0) return NULL;
    p = *cursor;

    if (*p == '"') {
        p++;
        while (*p) {
            if (*p == '"' && p[1] == '"') {
                if (n + 1 < out_size) out[n++] = '"';
                p += 2;
                continue;
            }
            if (*p == '"') {
                p++;
                break;
            }
            if (n + 1 < out_size) out[n++] = *p;
            p++;
        }
        while (*p && *p != ',') p++;
    }
    else {
        while (*p && *p != ',' && *p != '\n' && *p != '\r') {
            if (n + 1 < out_size) out[n++] = *p;
            p++;
        }
    }

    out[n] = '\0';
    if (*p == ',') p++;
    *cursor = p;
    return out;
}



static int case_text_equals_ci(const char *a, const char *b){
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int case_text_contains_ci(const char *haystack, const char *needle){
    char hay[512];
    char ndl[256];
    size_t i;

    if (!haystack) haystack = "";
    if (!needle || !needle[0]) return 1;

    for (i = 0; i + 1 < sizeof(hay) && haystack[i]; i++) hay[i] = (char)tolower((unsigned char)haystack[i]);
    hay[i] = '\0';
    for (i = 0; i + 1 < sizeof(ndl) && needle[i]; i++) ndl[i] = (char)tolower((unsigned char)needle[i]);
    ndl[i] = '\0';
    return strstr(hay, ndl) != NULL;
}

static void case_trim_text(char *text){
    size_t len;
    char *start;
    if (!text) return;
    start = text;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != text) memmove(text, start, strlen(start) + 1);
    len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) text[--len] = '\0';
}

static int case_csv_first_field(const char *line, char *dst, size_t dst_size){
    char tmp[4096];
    char *cursor = tmp;
    if (!line || !dst || dst_size == 0) return 0;
    snprintf(tmp, sizeof(tmp), "%s", line);
    case_read_csv_field(&cursor, dst, dst_size);
    case_trim_text(dst);
    return dst[0] != '\0';
}

static int case_case_name_from_filename(const char *name, char *dst, size_t dst_size){
    const char *prefix = "CASE_";
    const char *suffix = ".csv";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t len;

    if (!name || !dst || dst_size == 0) return 0;
    dst[0] = '\0';
    len = strlen(name);
    if (strncmp(name, prefix, prefix_len) != 0) return 0;
    if (strcmp(name, "CASE_DESCRIPTIONS.csv") == 0) return 0;
    if (len <= prefix_len + suffix_len) return 0;
    if (strcmp(name + len - suffix_len, suffix) != 0) return 0;

    size_t n = len - prefix_len - suffix_len;
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, name + prefix_len, n);
    dst[n] = '\0';
    case_trim_text(dst);
    return dst[0] != '\0';
}

static int case_read_case_number_from_csv(const char *path, char *dst, size_t dst_size){
    FILE *fp;
    char line[4096];
    if (!path || !dst || dst_size == 0) return 0;
    fp = fopen(path, "r");
    if (!fp) return 0;
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }
    while (fgets(line, sizeof(line), fp)) {
        if (case_csv_first_field(line, dst, dst_size)) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

static int case_case_option_exists(const char *case_number){
    if (!case_number || !case_number[0]) return 1;
    for (int i = 0; i < Global_Case_Case_Count; i++) {
        if (case_text_equals_ci(Global_Case_Case_Options[i], case_number)) return 1;
    }
    return 0;
}

static void case_add_case_option(const char *case_number){
    if (!case_number || !case_number[0]) return;
    if (Global_Case_Case_Count >= CASE_MGMT_SOURCE_MAX_FILES) return;
    if (case_case_option_exists(case_number)) return;
    snprintf(Global_Case_Case_Options[Global_Case_Case_Count],
             sizeof(Global_Case_Case_Options[Global_Case_Case_Count]),
             "%s",
             case_number);
    Global_Case_Case_Count++;
}

static void case_scan_case_files(void){
    Global_Case_Case_Count = 0;
    Global_Case_Case_Scroll = 0;
    Global_Case_Case_Hover = -1;

    DIR *dir = opendir(CASE_MGMT_CLASSIFICATION_DIR);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && Global_Case_Case_Count < CASE_MGMT_SOURCE_MAX_FILES) {
        char fallback_case[128];
        char csv_case[128];
        char path[768];
        if (!case_case_name_from_filename(entry->d_name, fallback_case, sizeof(fallback_case))) continue;
        snprintf(path, sizeof(path), "%s/%s", CASE_MGMT_CLASSIFICATION_DIR, entry->d_name);
        if (case_read_case_number_from_csv(path, csv_case, sizeof(csv_case))) {
            case_add_case_option(csv_case);
        }
        else {
            case_add_case_option(fallback_case);
        }
    }
    closedir(dir);
    qsort(Global_Case_Case_Options,
          (size_t)Global_Case_Case_Count,
          sizeof(Global_Case_Case_Options[0]),
          case_name_compare);
}

static int case_build_case_matches(int *matches, int max_matches){
    char *query = case_selected_field_text(CASE_MGMT_FIELD_CASE_NUMBER);
    int out = 0;
    if (!matches || max_matches <= 0) return 0;
    for (int i = 0; i < Global_Case_Case_Count && out < max_matches; i++) {
        if (!query || !query[0] || case_text_contains_ci(Global_Case_Case_Options[i], query)) {
            matches[out++] = i;
        }
    }
    return out;
}

static int case_build_country_matches(int *matches, int max_matches){
    char *query = case_selected_field_text(CASE_MGMT_FIELD_COUNTRY);
    int out = 0;
    int count = case_country_count();
    if (!matches || max_matches <= 0) return 0;
    for (int i = 0; i < count && out < max_matches; i++) {
        if (!query || !query[0] || case_text_contains_ci(CASE_MGMT_COUNTRIES[i].name, query)) {
            matches[out++] = i;
        }
    }
    return out;
}

static void case_select_case_option(int index){
    char *dst;
    if (index < 0 || index >= Global_Case_Case_Count) return;
    dst = case_selected_field_text(CASE_MGMT_FIELD_CASE_NUMBER);
    if (dst) {
        case_copy_text(dst, 128, Global_Case_Case_Options[index]);
        Global_Case_Field_Cursor[CASE_MGMT_FIELD_CASE_NUMBER] = (int)strlen(dst);
    }
    Global_Case_Case_Dropdown_Open = 0;
    Global_Case_Case_Hover = -1;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
}

static void case_select_country_option(int index){
    char *dst;
    if (index < 0 || index >= case_country_count()) return;
    dst = case_selected_field_text(CASE_MGMT_FIELD_COUNTRY);
    if (dst) {
        case_copy_text(dst, 128, CASE_MGMT_COUNTRIES[index].name);
        Global_Case_Field_Cursor[CASE_MGMT_FIELD_COUNTRY] = (int)strlen(dst);
    }
    Global_Case_Country_Dropdown_Open = 0;
    Global_Case_Country_Hover = -1;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
}

static void case_load(void){
    FILE *blocks = fopen(CASE_MGMT_BLOCKS_CSV, "r");
    char line[4096];
    char header[2048];
    int old_timeline_format = 0;
    int has_source_file = 0;
    int has_description = 0;
    int has_priority = 0;
    int has_case_number = 0;
    int has_country = 0;

    if (!blocks) {
        case_set_status("No saved case graph found", Case_Warn);
        return;
    }

    Global_Case_Block_Count = 0;
    Global_Case_Link_Count = 0;
    case_clear_block_selection();
    Global_Case_Selected_Link = -1;
    Global_Case_Next_Id = 1;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Case_Dropdown_Open = 0;
    Global_Case_Country_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;

    if (fgets(header, sizeof(header), blocks)) {
        old_timeline_format = strstr(header, "timeline") != NULL && strstr(header, "start_date") == NULL;
        has_source_file = strstr(header, "source_file") != NULL;
        has_description = strstr(header, "description") != NULL;
        has_priority = strstr(header, "priority") != NULL;
        has_case_number = strstr(header, "case_number") != NULL;
        has_country = strstr(header, "country") != NULL;
    }

    while (fgets(line, sizeof(line), blocks) && Global_Case_Block_Count < CASE_MGMT_MAX_BLOCKS) {
        Type_Case_Block b;
        char id_text[64];
        char x_text[64];
        char y_text[64];
        char timeline_text[CASE_MGMT_TEXT_MAX];
        char *p = line;
        memset(&b, 0, sizeof(b));
        memset(timeline_text, 0, sizeof(timeline_text));

        case_read_csv_field(&p, id_text, sizeof(id_text));
        case_read_csv_field(&p, x_text, sizeof(x_text));
        case_read_csv_field(&p, y_text, sizeof(y_text));
        if (has_case_number) {
            case_read_csv_field(&p, b.case_number, sizeof(b.case_number));
        }
        if (has_country) {
            case_read_csv_field(&p, b.country, sizeof(b.country));
        }
        case_read_csv_field(&p, b.task, sizeof(b.task));
        case_read_csv_field(&p, b.assigned_to, sizeof(b.assigned_to));

        if (old_timeline_format) {
            case_read_csv_field(&p, timeline_text, sizeof(timeline_text));
            case_read_csv_field(&p, b.status, sizeof(b.status));
            case_copy_text(b.start_date, sizeof(b.start_date), timeline_text);
            b.end_date[0] = '\0';
        }
        else {
            case_read_csv_field(&p, b.start_date, sizeof(b.start_date));
            case_read_csv_field(&p, b.end_date, sizeof(b.end_date));
            case_read_csv_field(&p, b.status, sizeof(b.status));
            if (has_priority) {
                case_read_csv_field(&p, b.priority, sizeof(b.priority));
            }
            if (has_source_file) {
                case_read_csv_field(&p, b.source_file, sizeof(b.source_file));
            }
            if (has_description) {
                case_read_csv_field(&p, b.description, sizeof(b.description));
                case_unescape_multiline(b.description);
            }
        }

        b.id = atoi(id_text);
        b.x = atoi(x_text);
        b.y = atoi(y_text);
        if (b.id <= 0) b.id = Global_Case_Next_Id;
        if (b.id >= Global_Case_Next_Id) Global_Case_Next_Id = b.id + 1;
        if (b.status[0] == '\0') case_copy_text(b.status, sizeof(b.status), "Todo");
        if (b.priority[0] == '\0') case_copy_text(b.priority, sizeof(b.priority), "3");
        Global_Case_Blocks[Global_Case_Block_Count++] = b;
    }
    fclose(blocks);

    FILE *links = fopen(CASE_MGMT_LINKS_CSV, "r");
    if (links) {
        fgets(line, sizeof(line), links);
        while (fgets(line, sizeof(line), links) && Global_Case_Link_Count < CASE_MGMT_MAX_LINKS) {
            int from_id = 0;
            int to_id = 0;
            int from_side = CASE_MGMT_SIDE_RIGHT;
            int to_side = CASE_MGMT_SIDE_LEFT;
            int parsed = sscanf(line, "%d,%d,%d,%d", &from_id, &to_id, &from_side, &to_side);
            if (parsed >= 2 &&
                case_find_block_index_by_id(from_id) >= 0 &&
                case_find_block_index_by_id(to_id) >= 0) {
                if (from_side < 0 || from_side > 3) from_side = CASE_MGMT_SIDE_RIGHT;
                if (to_side < 0 || to_side > 3) to_side = CASE_MGMT_SIDE_LEFT;
                Global_Case_Links[Global_Case_Link_Count++] = (Type_Case_Link){from_id, to_id, from_side, to_side};
            }
        }
        fclose(links);
    }

    if (Global_Case_Block_Count > 0) case_select_only_block(0);
    case_set_status("Case graph loaded", Case_Text);
}

static void case_add_block(SDL_Rect canvas){
    if (Global_Case_Block_Count >= CASE_MGMT_MAX_BLOCKS) {
        case_set_status("Maximum block count reached", Case_Warn);
        return;
    }

    case_ensure_view(canvas);

    int n = Global_Case_Block_Count;
    Type_Case_Block b;
    memset(&b, 0, sizeof(b));
    b.id = Global_Case_Next_Id++;

    int screen_x = canvas.x + 46 + ((n * 34) % (canvas.w > 360 ? canvas.w - 330 : 120));
    int screen_y = canvas.y + 44 + ((n * 52) % (canvas.h > 190 ? canvas.h - 170 : 120));
    b.x = case_screen_to_world_x(canvas, screen_x);
    b.y = case_screen_to_world_y(canvas, screen_y);

    b.case_number[0] = '\0';
    b.country[0] = '\0';
    case_copy_text(b.task, sizeof(b.task), "New task");
    case_copy_text(b.assigned_to, sizeof(b.assigned_to), "Unassigned");
    b.start_date[0] = '\0';
    b.end_date[0] = '\0';
    case_copy_text(b.status, sizeof(b.status), "Todo");
    case_copy_text(b.priority, sizeof(b.priority), "3");
    b.source_file[0] = '\0';
    b.description[0] = '\0';

    Global_Case_Blocks[Global_Case_Block_Count++] = b;
    case_select_only_block(Global_Case_Block_Count - 1);
    Global_Case_Selected_Link = -1;
    Global_Case_Active_Field = CASE_MGMT_FIELD_TASK;
    Global_Case_Field_Cursor[CASE_MGMT_FIELD_TASK] = (int)strlen(b.task);
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;
    case_set_status("Created case block", Case_Text);
}

static void case_duplicate_selected_block(SDL_Rect canvas){
    int selected_count = case_selected_block_count();
    int old_ids[CASE_MGMT_MAX_BLOCKS];
    int new_ids[CASE_MGMT_MAX_BLOCKS];
    int new_indices[CASE_MGMT_MAX_BLOCKS];
    int map_count = 0;
    int original_link_count = Global_Case_Link_Count;

    if (selected_count == 0 && Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count) {
        Global_Case_Selected_Blocks[Global_Case_Selected] = 1;
        selected_count = 1;
    }

    if (selected_count == 0) {
        case_set_status("Select one or more blocks before duplicating", Case_Warn);
        return;
    }

    if (Global_Case_Block_Count + selected_count > CASE_MGMT_MAX_BLOCKS) {
        case_set_status("Not enough block slots to duplicate selection", Case_Warn);
        return;
    }

    case_ensure_view(canvas);

    for (int i = 0; i < Global_Case_Block_Count; i++) {
        if (!Global_Case_Selected_Blocks[i]) continue;

        Type_Case_Block copy = Global_Case_Blocks[i];
        old_ids[map_count] = copy.id;
        copy.id = Global_Case_Next_Id++;
        copy.x += 38;
        copy.y += 38;
        new_ids[map_count] = copy.id;
        new_indices[map_count] = Global_Case_Block_Count;
        Global_Case_Blocks[Global_Case_Block_Count++] = copy;
        map_count++;
    }

    for (int i = 0; i < original_link_count && Global_Case_Link_Count < CASE_MGMT_MAX_LINKS; i++) {
        Type_Case_Link link = Global_Case_Links[i];
        int new_from = -1;
        int new_to = -1;

        for (int m = 0; m < map_count; m++) {
            if (old_ids[m] == link.from_id) new_from = new_ids[m];
            if (old_ids[m] == link.to_id) new_to = new_ids[m];
        }

        if (new_from >= 0 && new_to >= 0) {
            Global_Case_Links[Global_Case_Link_Count++] =
                (Type_Case_Link){new_from, new_to, link.from_side, link.to_side};
        }
    }

    case_clear_block_selection();
    for (int i = 0; i < map_count; i++) {
        if (new_indices[i] >= 0 && new_indices[i] < Global_Case_Block_Count) {
            Global_Case_Selected_Blocks[new_indices[i]] = 1;
        }
    }
    case_sync_primary_selection();
    Global_Case_Selected_Link = -1;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Case_Dropdown_Open = 0;
    Global_Case_Country_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;
    Global_Case_Source_Popup_Open = 0;
    Global_Case_Description_Popup_Open = 0;
    case_description_clear_selection();

    if (map_count > 1) case_set_status("Duplicated selected block group", Case_Text);
    else case_set_status("Duplicated selected block", Case_Text);
}

static int case_link_exists(int from_id, int to_id){
    for (int i = 0; i < Global_Case_Link_Count; i++) {
        if (Global_Case_Links[i].from_id == from_id && Global_Case_Links[i].to_id == to_id) return 1;
    }
    return 0;
}

static void case_add_link(int from_index, int to_index, int from_side, int to_side){
    if (from_index < 0 || to_index < 0 || from_index == to_index) return;
    if (Global_Case_Link_Count >= CASE_MGMT_MAX_LINKS) {
        case_set_status("Maximum link count reached", Case_Warn);
        return;
    }

    int from_id = Global_Case_Blocks[from_index].id;
    int to_id = Global_Case_Blocks[to_index].id;
    if (case_link_exists(from_id, to_id)) {
        case_set_status("Link already exists", Case_Warn);
        return;
    }

    if (from_side < 0 || from_side > 3) from_side = CASE_MGMT_SIDE_RIGHT;
    if (to_side < 0 || to_side > 3) to_side = CASE_MGMT_SIDE_LEFT;
    Global_Case_Links[Global_Case_Link_Count++] = (Type_Case_Link){from_id, to_id, from_side, to_side};
    Global_Case_Selected_Link = Global_Case_Link_Count - 1;
    Global_Case_Selected = -1;
    case_set_status("Linked blocks", Case_Text);
}

static int case_id_is_marked_for_removal(int id, const int *remove_ids, int remove_count){
    for (int i = 0; i < remove_count; i++) {
        if (remove_ids[i] == id) return 1;
    }
    return 0;
}

static void case_delete_selected(void){
    int remove_ids[CASE_MGMT_MAX_BLOCKS];
    int remove_count = 0;

    if (Global_Case_Selected_Link >= 0 && Global_Case_Selected_Link < Global_Case_Link_Count) {
        for (int i = Global_Case_Selected_Link; i + 1 < Global_Case_Link_Count; i++) {
            Global_Case_Links[i] = Global_Case_Links[i + 1];
        }
        Global_Case_Link_Count--;
        Global_Case_Selected_Link = -1;
        case_set_status("Deleted selected link", Case_Text);
        return;
    }

    for (int i = 0; i < Global_Case_Block_Count; i++) {
        if (Global_Case_Selected_Blocks[i]) remove_ids[remove_count++] = Global_Case_Blocks[i].id;
    }

    if (remove_count == 0 && Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count) {
        remove_ids[remove_count++] = Global_Case_Blocks[Global_Case_Selected].id;
    }

    if (remove_count == 0) return;

    int write = 0;
    for (int i = 0; i < Global_Case_Block_Count; i++) {
        if (!case_id_is_marked_for_removal(Global_Case_Blocks[i].id, remove_ids, remove_count)) {
            Global_Case_Blocks[write++] = Global_Case_Blocks[i];
        }
    }
    Global_Case_Block_Count = write;

    write = 0;
    for (int i = 0; i < Global_Case_Link_Count; i++) {
        if (!case_id_is_marked_for_removal(Global_Case_Links[i].from_id, remove_ids, remove_count) &&
            !case_id_is_marked_for_removal(Global_Case_Links[i].to_id, remove_ids, remove_count)) {
            Global_Case_Links[write++] = Global_Case_Links[i];
        }
    }
    Global_Case_Link_Count = write;

    case_clear_block_selection();
    if (Global_Case_Block_Count > 0) case_select_only_block(Global_Case_Block_Count - 1);
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
    Global_Case_Link_Source = -1;
    Global_Case_Selected_Link = -1;
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;

    if (remove_count > 1) case_set_status("Deleted selected block group", Case_Text);
    else case_set_status("Deleted selected block", Case_Text);
}

static char *case_selected_field_text(int field){
    if (Global_Case_Selected < 0 || Global_Case_Selected >= Global_Case_Block_Count) return NULL;
    Type_Case_Block *b = &Global_Case_Blocks[Global_Case_Selected];

    if (field == CASE_MGMT_FIELD_CASE_NUMBER) return b->case_number;
    if (field == CASE_MGMT_FIELD_COUNTRY) return b->country;
    if (field == CASE_MGMT_FIELD_TASK) return b->task;
    if (field == CASE_MGMT_FIELD_USER) return b->assigned_to;
    if (field == CASE_MGMT_FIELD_START_DATE) return b->start_date;
    if (field == CASE_MGMT_FIELD_END_DATE) return b->end_date;
    if (field == CASE_MGMT_FIELD_STATUS) return b->status;
    if (field == CASE_MGMT_FIELD_PRIORITY) return b->priority;
    if (field == CASE_MGMT_FIELD_SOURCE_FILE) return b->source_file;
    if (field == CASE_MGMT_FIELD_DESCRIPTION) return b->description;
    return NULL;
}

static int case_field_max_len(int field){
    if (field == CASE_MGMT_FIELD_START_DATE || field == CASE_MGMT_FIELD_END_DATE) return 10;
    if (field == CASE_MGMT_FIELD_PRIORITY) return 1;
    if (field == CASE_MGMT_FIELD_DESCRIPTION) return CASE_MGMT_DESCRIPTION_MAX - 1;
    if (field == CASE_MGMT_FIELD_SOURCE_FILE || field == CASE_MGMT_FIELD_STATUS) return 0;
    return CASE_MGMT_TEXT_MAX - 1;
}

static size_t case_field_storage_size(int field){
    if (field == CASE_MGMT_FIELD_DESCRIPTION) return CASE_MGMT_DESCRIPTION_MAX;
    if (field == CASE_MGMT_FIELD_SOURCE_FILE) return CASE_MGMT_SOURCE_FILE_MAX;
    if (field == CASE_MGMT_FIELD_CASE_NUMBER || field == CASE_MGMT_FIELD_COUNTRY) return 128;
    if (field == CASE_MGMT_FIELD_PRIORITY) return 16;
    return CASE_MGMT_TEXT_MAX;
}

static int case_text_allowed_for_field(int field, char c){
    if (field == CASE_MGMT_FIELD_START_DATE || field == CASE_MGMT_FIELD_END_DATE) {
        return isdigit((unsigned char)c) || c == '/';
    }
    if (field == CASE_MGMT_FIELD_STATUS || field == CASE_MGMT_FIELD_SOURCE_FILE) return 0;
    if (field == CASE_MGMT_FIELD_PRIORITY) return c >= '1' && c <= '5';
    if (field == CASE_MGMT_FIELD_DESCRIPTION) return (c >= 32 && c <= 126) || c == '\n';
    return c >= 32 && c <= 126;
}


static int case_description_range_width(TTF_Font *font, const char *text, size_t start, size_t end){
    char buf[CASE_MGMT_DESCRIPTION_MAX + 8];
    int w = 0;
    int h = 0;

    if (!text || end <= start) return 0;
    if (end - start >= sizeof(buf)) end = start + sizeof(buf) - 1;

    memcpy(buf, text + start, end - start);
    buf[end - start] = '\0';

    if (!font || TTF_SizeText(font, buf, &w, &h) != 0) {
        return (int)(end - start) * 8;
    }

    return w;
}

static void case_auto_wrap_description_text(char *text, int *cursor){
    size_t len;
    size_t line_start;
    int max_px = Global_Case_Description_Wrap_Px;

    if (!text) return;
    if (max_px < 16) max_px = 520;

    len = strlen(text);
    line_start = 0;

    while (line_start < len) {
        size_t line_end = line_start;
        size_t segment_start = line_start;

        while (line_end < len && text[line_end] != '\n') {
            line_end++;
        }

        while (line_end > segment_start &&
               case_description_range_width(Global_Case_Description_Font, text, segment_start, line_end) > max_px) {
            size_t fit = segment_start + 1;
            size_t break_pos;
            int found_space = 0;

            for (size_t i = segment_start + 1; i <= line_end; i++) {
                if (case_description_range_width(Global_Case_Description_Font, text, segment_start, i) <= max_px) {
                    fit = i;
                }
                else {
                    break;
                }
            }

            if (fit <= segment_start) fit = segment_start + 1;
            if (fit > line_end) fit = line_end;
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
                if (len + 1 >= CASE_MGMT_DESCRIPTION_MAX) break;
                memmove(text + break_pos + 1,
                        text + break_pos,
                        len - break_pos + 1);
                text[break_pos] = '\n';
                len++;
                line_end++;
                if (cursor && *cursor >= (int)break_pos) {
                    (*cursor)++;
                }
                segment_start = break_pos + 1;
            }
        }

        if (line_end >= len) break;
        line_start = line_end + 1;
    }

    if (cursor) {
        int text_len = (int)strlen(text);
        if (*cursor < 0) *cursor = 0;
        if (*cursor > text_len) *cursor = text_len;
    }
}

static void case_insert_text(char *dst, int *cursor, const char *src, int field){
    size_t len;
    char filtered[2048];
    size_t add = 0;
    int max_len;

    if (!dst || !cursor || !src) return;
    max_len = case_field_max_len(field);

    if (field == CASE_MGMT_FIELD_DESCRIPTION) {
        case_description_delete_selection();
    }

    for (const char *p = src; *p && add + 1 < sizeof(filtered); p++) {
        if (case_text_allowed_for_field(field, *p)) filtered[add++] = *p;
    }
    filtered[add] = '\0';
    if (add == 0) return;

    len = strlen(dst);
    if ((int)(len + add) > max_len) add = (size_t)(max_len - (int)len);
    if (add == 0 || len + add >= case_field_storage_size(field)) return;
    if (*cursor < 0) *cursor = 0;
    if ((size_t)*cursor > len) *cursor = (int)len;

    memmove(dst + *cursor + add, dst + *cursor, len - (size_t)*cursor + 1);
    memcpy(dst + *cursor, filtered, add);
    *cursor += (int)add;

    if (field == CASE_MGMT_FIELD_DESCRIPTION) {
        case_auto_wrap_description_text(dst, cursor);
    }
}

static void case_backspace(char *dst, int *cursor){
    size_t len;

    if (dst == case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION) &&
        case_description_delete_selection()) return;
    if (!dst || !cursor) return;
    len = strlen(dst);
    if (*cursor <= 0 || len == 0) return;
    if ((size_t)*cursor > len) *cursor = (int)len;
    memmove(dst + *cursor - 1, dst + *cursor, len - (size_t)*cursor + 1);
    (*cursor)--;
}

static void case_delete_at_cursor(char *dst, int *cursor){
    size_t len;

    if (dst == case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION) &&
        case_description_delete_selection()) return;
    if (!dst || !cursor) return;
    len = strlen(dst);
    if (*cursor < 0) *cursor = 0;
    if ((size_t)*cursor >= len) return;
    memmove(dst + *cursor, dst + *cursor + 1, len - (size_t)*cursor);
}


static void case_description_clear_selection(void){
    Global_Case_Description_Selection_Start = -1;
    Global_Case_Description_Selection_End = -1;
    Global_Case_Description_Selecting = 0;
}

static int case_description_selection_range(int *a, int *b){
    int s = Global_Case_Description_Selection_Start;
    int e = Global_Case_Description_Selection_End;
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int len = text ? (int)strlen(text) : 0;

    if (s < 0 || e < 0 || s == e) return 0;
    if (s > e) { int t = s; s = e; e = t; }
    if (s < 0) s = 0;
    if (e < 0) e = 0;
    if (s > len) s = len;
    if (e > len) e = len;
    if (s == e) return 0;
    if (a) *a = s;
    if (b) *b = e;
    return 1;
}

static int case_description_delete_selection(void){
    int a = 0;
    int b = 0;
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    size_t len;

    if (!text || !case_description_selection_range(&a, &b)) return 0;
    len = strlen(text);
    if (a < 0) a = 0;
    if (b < a) b = a;
    if ((size_t)b > len) b = (int)len;
    memmove(text + a, text + b, len - (size_t)b + 1);
    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = a;
    case_description_clear_selection();
    return 1;
}

static void case_description_start_selection_at_cursor(void){
    int cursor = Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int len = text ? (int)strlen(text) : 0;
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = cursor;
    Global_Case_Description_Selecting = 1;
    Global_Case_Description_Selection_Start = cursor;
    Global_Case_Description_Selection_End = cursor;
}

static void case_description_update_selection_to_cursor(void){
    int cursor = Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int len = text ? (int)strlen(text) : 0;
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = cursor;
    if (Global_Case_Description_Selection_Start < 0) {
        Global_Case_Description_Selection_Start = cursor;
    }
    Global_Case_Description_Selection_End = cursor;
}

static int case_description_build_lines(const char *text, int starts[128], int ends[128]){
    int len = text ? (int)strlen(text) : 0;
    int line_count = 0;
    int start = 0;

    while (line_count < 128) {
        int end = start;
        while (end < len && text[end] != '\n') end++;
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

static void case_description_move_horizontal(int direction){
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int *cursor = &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
    int len = text ? (int)strlen(text) : 0;

    if (*cursor < 0) *cursor = 0;
    if (*cursor > len) *cursor = len;

    if (direction < 0 && *cursor > 0) (*cursor)--;
    if (direction > 0 && *cursor < len) (*cursor)++;
}

static void case_description_move_vertical(int direction){
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int starts[128];
    int ends[128];
    int line_count = case_description_build_lines(text, starts, ends);
    int *cursor = &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
    int current_line = 0;

    for (int i = 0; i < line_count; i++) {
        if (*cursor >= starts[i] && *cursor <= ends[i]) {
            current_line = i;
            break;
        }
    }

    int target = current_line + direction;
    if (target < 0 || target >= line_count) return;
    *cursor = ends[target];
}

static void case_set_description_cursor_from_mouse(SDL_Rect rect, int mouse_x, int mouse_y){
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int starts[128];
    int ends[128];
    int line_h = 19;
    int max_lines = (rect.h - 12) / line_h;
    int line_count = case_description_build_lines(text, starts, ends);
    int first_line = 0;

    if (max_lines < 1) max_lines = 1;
    if (line_count > max_lines) first_line = line_count - max_lines;

    int visible_line = (mouse_y - (rect.y + 7)) / line_h;
    if (visible_line < 0) visible_line = 0;
    if (visible_line >= max_lines) visible_line = max_lines - 1;

    int line = first_line + visible_line;
    if (line < 0) line = 0;
    if (line >= line_count) line = line_count - 1;

    int line_len = ends[line] - starts[line];
    int rel_x = mouse_x - (rect.x + 9);
    int column = 0;

    for (int i = 0; i <= line_len; i++) {
        int w0 = case_description_range_width(Global_Case_Description_Font, text, (size_t)starts[line], (size_t)(starts[line] + i));
        int w1 = w0;
        if (i < line_len) {
            w1 = case_description_range_width(Global_Case_Description_Font, text, (size_t)starts[line], (size_t)(starts[line] + i + 1));
        }
        if (i == line_len || rel_x < (w0 + w1) / 2) {
            column = i;
            break;
        }
    }

    if (column < 0) column = 0;
    if (column > line_len) column = line_len;

    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = starts[line] + column;
}

static void case_cycle_status(void){
    char *status = case_selected_field_text(CASE_MGMT_FIELD_STATUS);
    if (!status) return;

    for (int i = 0; i < CASE_MGMT_STATUS_COUNT; i++) {
        if (strcmp(status, CASE_MGMT_STATUS_OPTIONS[i]) == 0) {
            case_copy_text(status,
                           CASE_MGMT_TEXT_MAX,
                           CASE_MGMT_STATUS_OPTIONS[(i + 1) % CASE_MGMT_STATUS_COUNT]);
            return;
        }
    }

    case_copy_text(status, CASE_MGMT_TEXT_MAX, CASE_MGMT_STATUS_OPTIONS[0]);
}

static int case_days_in_month(int month, int year){
    static const int days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2) {
        int leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    if (month < 1 || month > 12) return 30;
    return days[month - 1];
}

static int case_first_weekday(int month, int year){
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_mday = 1;
    t.tm_mon = month - 1;
    t.tm_year = year - 1900;
    t.tm_isdst = -1;
    if (mktime(&t) == (time_t)-1) return 0;
    return t.tm_wday;
}

static void case_today_month_year(int *month, int *year){
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt) {
        if (month) *month = lt->tm_mon + 1;
        if (year) *year = lt->tm_year + 1900;
    }
    else {
        if (month) *month = 1;
        if (year) *year = 2026;
    }
}

static int case_parse_mmddyyyy(const char *text, int *month, int *day, int *year){
    int m = 0;
    int d = 0;
    int y = 0;

    if (!text || sscanf(text, "%d/%d/%d", &m, &d, &y) != 3) return 0;
    if (y < 100) y += (y >= 70) ? 1900 : 2000;
    if (m < 1 || m > 12) return 0;
    if (d < 1 || d > case_days_in_month(m, y)) return 0;

    if (month) *month = m;
    if (day) *day = d;
    if (year) *year = y;
    return 1;
}

static void case_open_calendar_for_field(int field){
    char *text;
    int month = 0;
    int day = 0;
    int year = 0;

    if (field != CASE_MGMT_FIELD_START_DATE && field != CASE_MGMT_FIELD_END_DATE) return;

    text = case_selected_field_text(field);
    if (!case_parse_mmddyyyy(text, &month, &day, &year)) {
        case_today_month_year(&month, &year);
    }

    (void)day;
    Global_Case_Calendar_Open = 1;
    Global_Case_Calendar_Field = field;
    Global_Case_Calendar_Month = month;
    Global_Case_Calendar_Year = year;
    Global_Case_Status_Dropdown_Open = 0;
}

static void case_shift_calendar_month(int delta){
    Global_Case_Calendar_Month += delta;
    while (Global_Case_Calendar_Month < 1) {
        Global_Case_Calendar_Month += 12;
        Global_Case_Calendar_Year--;
    }
    while (Global_Case_Calendar_Month > 12) {
        Global_Case_Calendar_Month -= 12;
        Global_Case_Calendar_Year++;
    }
}

static void case_set_calendar_day(int day){
    char *text = case_selected_field_text(Global_Case_Calendar_Field);
    if (!text || day < 1) return;
    int month = Global_Case_Calendar_Month;
    int year = Global_Case_Calendar_Year;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day > 31) day = 31;
    if (year < 0) year = 0;
    if (year > 9999) year = 9999;
    snprintf(text,
             CASE_MGMT_DATE_MAX,
             "%02d/%02d/%04d",
             month,
             day,
             year);
    Global_Case_Field_Cursor[Global_Case_Calendar_Field] = (int)strlen(text);
    Global_Case_Calendar_Open = 0;
}


static void case_paste_description_from_clipboard(void){
    char *clip = SDL_GetClipboardText();
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    if (clip && text) {
        case_insert_text(text,
                         &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION],
                         clip,
                         CASE_MGMT_FIELD_DESCRIPTION);
    }
    if (clip) SDL_free(clip);
}

void CASE_MANAGEMENT_enter_mode(const char *record_dir){
    if (record_dir && record_dir[0] != '\0') {
        case_copy_text(Global_Case_Record_Dir, sizeof(Global_Case_Record_Dir), record_dir);
    }
    Global_CaseManagement_Mode = 1;
    SDL_StartTextInput();
    case_seed_default_blocks();
    case_set_status("Case Management Workstation", Case_Text);
}

void CASE_MANAGEMENT_exit_mode(void){
    Global_CaseManagement_Mode = 0;
    Global_Case_Dragging = 0;
    Global_Case_Panning = 0;
    Global_Case_Link_Dragging = 0;
    Global_Case_Box_Selecting = 0;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
    Global_Case_Link_Source = -1;
    Global_Case_Link_Mode = 0;
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Case_Dropdown_Open = 0;
    Global_Case_Country_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;
    Global_Case_Source_Popup_Open = 0;
    Global_Case_Description_Popup_Open = 0;
}

int CASE_MANAGEMENT_is_text_entry_active(void){
    return Global_CaseManagement_Mode &&
           (Global_Case_Active_Field != CASE_MGMT_FIELD_NONE ||
            (Global_Case_Source_Popup_Open && Global_Case_Source_Search_Active));
}

static void case_toolbar_rects(int win_w,
                               SDL_Rect *new_btn,
                               SDL_Rect *link_btn,
                               SDL_Rect *save_btn,
                               SDL_Rect *load_btn){
    int x = CASE_MGMT_MARGIN;
    int y = CASE_MGMT_MARGIN;
    int h = 34;

    (void)win_w;
    if (new_btn)  *new_btn  = (SDL_Rect){x, y, 108, h};
    x += 118;
    if (link_btn) *link_btn = (SDL_Rect){x, y, 120, h};
    x += 130;
    if (save_btn) *save_btn = (SDL_Rect){x, y, 90, h};
    x += 100;
    if (load_btn) *load_btn = (SDL_Rect){x, y, 90, h};
}

static void case_editor_field_rects(SDL_Rect editor,
                                    SDL_Rect fields[CASE_MGMT_FIELD_COUNT],
                                    SDL_Rect *duplicate_btn,
                                    SDL_Rect *delete_btn){
    int x = editor.x + 16;
    int y = editor.y + 104;
    int w = editor.w - 32;
    int normal_h = 31;
    int normal_gap = 58;
    int extra_description_gap = 24;
    int action_y = editor.y + editor.h - 42;
    int desc_h;

    for (int i = 0; i < CASE_MGMT_FIELD_COUNT; i++) {
        if (i == CASE_MGMT_FIELD_DESCRIPTION) {
            y += extra_description_gap;
            desc_h = action_y - y - 18;
            if (desc_h < 104) desc_h = 104;
            if (desc_h > 180) desc_h = 180;
            fields[i] = (SDL_Rect){x, y, w, desc_h};
            y += desc_h + 12;
        }
        else {
            fields[i] = (SDL_Rect){x, y, w, normal_h};
            y += normal_gap;
        }
    }

    if (duplicate_btn) *duplicate_btn = (SDL_Rect){editor.x + 16, action_y, 118, 34};
    if (delete_btn) *delete_btn = (SDL_Rect){editor.x + editor.w - 126, action_y, 110, 34};
}

static SDL_Rect case_field_hit_rect(SDL_Rect field){
    SDL_Rect r = {field.x, field.y - 24, field.w, field.h + 24};
    return r;
}

static SDL_Rect case_description_open_button_rect(SDL_Rect field){
    SDL_Rect r = {field.x + field.w - 70, field.y - 24, 70, 20};
    return r;
}

static SDL_Rect case_status_dropdown_rect(SDL_Rect status_field){
    SDL_Rect r = {
        status_field.x,
        status_field.y + status_field.h + 4,
        status_field.w,
        CASE_MGMT_STATUS_COUNT * CASE_MGMT_STATUS_OPTION_H
    };
    return r;
}

static SDL_Rect case_calendar_rect(SDL_Rect field){
    SDL_Rect r = {
        field.x,
        field.y + field.h + 4,
        field.w,
        CASE_MGMT_CALENDAR_H
    };
    return r;
}

static SDL_Rect case_source_popup_rect(int win_w, int win_h){
    SDL_Rect r = {
        (win_w - 1050) / 2,
        (win_h - 740) / 2,
        1050,
        740
    };

    if (r.x < CASE_MGMT_MARGIN) r.x = CASE_MGMT_MARGIN;
    if (r.y < CASE_MGMT_MARGIN) r.y = CASE_MGMT_MARGIN;
    if (r.w > win_w - 2 * CASE_MGMT_MARGIN) r.w = win_w - 2 * CASE_MGMT_MARGIN;
    if (r.h > win_h - 2 * CASE_MGMT_MARGIN) r.h = win_h - 2 * CASE_MGMT_MARGIN;
    if (r.w < 320) r.w = 320;
    if (r.h < 260) r.h = 260;
    return r;
}

static SDL_Rect case_description_popup_rect(int win_w, int win_h){
    SDL_Rect r = {
        (win_w - 760) / 2,
        (win_h - 520) / 2,
        760,
        520
    };

    if (r.x < CASE_MGMT_MARGIN) r.x = CASE_MGMT_MARGIN;
    if (r.y < CASE_MGMT_MARGIN) r.y = CASE_MGMT_MARGIN;
    if (r.w > win_w - 2 * CASE_MGMT_MARGIN) r.w = win_w - 2 * CASE_MGMT_MARGIN;
    if (r.h > win_h - 2 * CASE_MGMT_MARGIN) r.h = win_h - 2 * CASE_MGMT_MARGIN;
    if (r.w < 360) r.w = 360;
    if (r.h < 300) r.h = 300;
    return r;
}

static void case_clamp_description_scroll(SDL_Rect text_rect){
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int starts[128];
    int ends[128];
    int line_count = case_description_build_lines(text, starts, ends);
    int max_lines = (text_rect.h - 12) / 19;
    int max_scroll;

    if (max_lines < 1) max_lines = 1;
    max_scroll = line_count - max_lines;
    if (max_scroll < 0) max_scroll = 0;
    if (Global_Case_Description_Popup_Scroll < 0) Global_Case_Description_Popup_Scroll = 0;
    if (Global_Case_Description_Popup_Scroll > max_scroll) Global_Case_Description_Popup_Scroll = max_scroll;
}

static void case_set_description_cursor_from_mouse_scrolled(SDL_Rect rect,
                                                            int mouse_x,
                                                            int mouse_y,
                                                            int first_line){
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int starts[128];
    int ends[128];
    int line_h = 19;
    int max_lines = (rect.h - 12) / line_h;
    int line_count = case_description_build_lines(text, starts, ends);

    if (max_lines < 1) max_lines = 1;
    if (first_line < 0) first_line = 0;
    if (first_line > line_count - 1) first_line = line_count - 1;

    int visible_line = (mouse_y - (rect.y + 7)) / line_h;
    if (visible_line < 0) visible_line = 0;
    if (visible_line >= max_lines) visible_line = max_lines - 1;

    int line = first_line + visible_line;
    if (line < 0) line = 0;
    if (line >= line_count) line = line_count - 1;

    int line_len = ends[line] - starts[line];
    int rel_x = mouse_x - (rect.x + 9);
    int column = 0;

    for (int i = 0; i <= line_len; i++) {
        int w0 = case_description_range_width(Global_Case_Description_Font, text, (size_t)starts[line], (size_t)(starts[line] + i));
        int w1 = w0;
        if (i < line_len) {
            w1 = case_description_range_width(Global_Case_Description_Font, text, (size_t)starts[line], (size_t)(starts[line] + i + 1));
        }
        if (i == line_len || rel_x < (w0 + w1) / 2) {
            column = i;
            break;
        }
    }

    if (column < 0) column = 0;
    if (column > line_len) column = line_len;

    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = starts[line] + column;
}

static int case_source_name_matches_search(const char *name){
    char hay[CASE_MGMT_SOURCE_FILE_MAX];
    char needle[sizeof(Global_Case_Source_Search)];
    size_t i;

    if (!name) name = "";
    if (Global_Case_Source_Search[0] == '\0') return 1;

    for (i = 0; i + 1 < sizeof(hay) && name[i]; i++) {
        hay[i] = (char)tolower((unsigned char)name[i]);
    }
    hay[i] = '\0';

    for (i = 0; i + 1 < sizeof(needle) && Global_Case_Source_Search[i]; i++) {
        needle[i] = (char)tolower((unsigned char)Global_Case_Source_Search[i]);
    }
    needle[i] = '\0';

    return strstr(hay, needle) != NULL;
}

static int case_source_filtered_count(void){
    int count = 0;
    for (int i = 0; i < Global_Case_Source_File_Count; i++) {
        if (case_source_name_matches_search(Global_Case_Source_Files[i])) count++;
    }
    return count;
}

static int case_source_filtered_index_at(int filtered_index){
    int seen = 0;
    if (filtered_index < 0) return -1;
    for (int i = 0; i < Global_Case_Source_File_Count; i++) {
        if (!case_source_name_matches_search(Global_Case_Source_Files[i])) continue;
        if (seen == filtered_index) return i;
        seen++;
    }
    return -1;
}

static SDL_Rect case_source_search_rect(SDL_Rect popup){
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = {close_btn.x - 292, popup.y + 14, 276, 30};
    if (search.x < popup.x + 140) {
        search.x = popup.x + 140;
        search.w = close_btn.x - search.x - 16;
    }
    if (search.w < 120) search.w = 120;
    return search;
}

static void case_clamp_source_scroll(void){
    int visible = CASE_MGMT_SOURCE_VISIBLE;
    int filtered_count = case_source_filtered_count();
    int max_scroll = filtered_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (Global_Case_Source_Scroll < 0) Global_Case_Source_Scroll = 0;
    if (Global_Case_Source_Scroll > max_scroll) Global_Case_Source_Scroll = max_scroll;
}

static int case_handle_source_popup_click(int mx, int my, int win_w, int win_h){
    SDL_Rect popup = case_source_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = case_source_search_rect(popup);
    SDL_Rect current_rect = {popup.x + 18, popup.y + 62, popup.w - 36, 42};
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};
    (void)current_rect;

    if (!Global_Case_Source_Popup_Open) return 0;

    if (!case_point_in_rect(mx, my, popup)) {
        Global_Case_Source_Popup_Open = 0;
        Global_Case_Source_Search_Active = 0;
        return 1;
    }

    if (case_point_in_rect(mx, my, close_btn)) {
        Global_Case_Source_Popup_Open = 0;
        Global_Case_Source_Search_Active = 0;
        return 1;
    }

    if (case_point_in_rect(mx, my, search)) {
        Global_Case_Source_Search_Active = 1;
        Global_Case_Source_Search_Cursor = (int)strlen(Global_Case_Source_Search);
        return 1;
    }

    Global_Case_Source_Search_Active = 0;

    if (case_point_in_rect(mx, my, list) && Global_Case_Source_File_Count > 0) {
        int row = (my - list.y) / CASE_MGMT_SOURCE_ROW_H;
        int filtered_index = Global_Case_Source_Scroll + row;
        int source_index = case_source_filtered_index_at(filtered_index);
        int max_visible = list.h / CASE_MGMT_SOURCE_ROW_H;
        if (row >= 0 && row < max_visible && source_index >= 0 && source_index < Global_Case_Source_File_Count) {
            char *source = case_selected_field_text(CASE_MGMT_FIELD_SOURCE_FILE);
            if (source) {
                case_copy_text(source, CASE_MGMT_SOURCE_FILE_MAX, Global_Case_Source_Files[source_index]);
                Global_Case_Field_Cursor[CASE_MGMT_FIELD_SOURCE_FILE] = (int)strlen(source);
            }
            Global_Case_Source_Popup_Open = 0;
            Global_Case_Source_Search_Active = 0;
            case_set_status("Source file selected", Case_Text);
            return 1;
        }
    }

    return 1;
}

static int case_handle_status_dropdown_click(int mx, int my, SDL_Rect status_field){
    SDL_Rect menu = case_status_dropdown_rect(status_field);
    if (!Global_Case_Status_Dropdown_Open) return 0;

    if (!case_point_in_rect(mx, my, menu)) {
        Global_Case_Status_Dropdown_Open = 0;
        return 0;
    }

    int index = (my - menu.y) / CASE_MGMT_STATUS_OPTION_H;
    if (index >= 0 && index < CASE_MGMT_STATUS_COUNT) {
        char *status = case_selected_field_text(CASE_MGMT_FIELD_STATUS);
        if (status) case_copy_text(status, CASE_MGMT_TEXT_MAX, CASE_MGMT_STATUS_OPTIONS[index]);
        Global_Case_Status_Dropdown_Open = 0;
        Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
        return 1;
    }

    return 0;
}

static int case_handle_calendar_click(int mx, int my, SDL_Rect date_field){
    SDL_Rect cal = case_calendar_rect(date_field);
    if (!Global_Case_Calendar_Open) return 0;

    if (!case_point_in_rect(mx, my, cal)) {
        Global_Case_Calendar_Open = 0;
        return 0;
    }

    SDL_Rect prev = {cal.x + 10, cal.y + 8, 28, 24};
    SDL_Rect next = {cal.x + cal.w - 38, cal.y + 8, 28, 24};
    if (case_point_in_rect(mx, my, prev)) {
        case_shift_calendar_month(-1);
        return 1;
    }
    if (case_point_in_rect(mx, my, next)) {
        case_shift_calendar_month(1);
        return 1;
    }

    int grid_x = cal.x + 8;
    int grid_y = cal.y + 58;
    int cell_w = (cal.w - 16) / 7;
    int cell_h = 26;
    int first = case_first_weekday(Global_Case_Calendar_Month, Global_Case_Calendar_Year);
    int days = case_days_in_month(Global_Case_Calendar_Month, Global_Case_Calendar_Year);

    if (my >= grid_y) {
        int col = (mx - grid_x) / cell_w;
        int row = (my - grid_y) / cell_h;
        if (col >= 0 && col < 7 && row >= 0 && row < 6) {
            int day = row * 7 + col - first + 1;
            if (day >= 1 && day <= days) {
                case_set_calendar_day(day);
                return 1;
            }
        }
    }

    return 1;
}



static SDL_Rect case_case_dropdown_rect(SDL_Rect field, int visible){
    SDL_Rect r = {field.x, field.y + field.h + 4, field.w, visible * CASE_MGMT_CASE_OPTION_H};
    return r;
}

static SDL_Rect case_country_dropdown_rect(SDL_Rect field, int visible){
    SDL_Rect r = {field.x, field.y + field.h + 4, field.w, visible * CASE_MGMT_COUNTRY_OPTION_H};
    return r;
}

static int case_handle_case_dropdown_click(int mx, int my, SDL_Rect field){
    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count;
    int visible;
    SDL_Rect menu;

    if (!Global_Case_Case_Dropdown_Open) return 0;
    count = case_build_case_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
    if (count <= 0) {
        Global_Case_Case_Dropdown_Open = 0;
        return 0;
    }

    visible = count - Global_Case_Case_Scroll;
    if (visible > CASE_MGMT_CASE_MAX_VISIBLE) visible = CASE_MGMT_CASE_MAX_VISIBLE;
    if (visible < 0) visible = 0;
    menu = case_case_dropdown_rect(field, visible);

    if (!case_point_in_rect(mx, my, menu)) {
        Global_Case_Case_Dropdown_Open = 0;
        return 0;
    }

    int row = (my - menu.y) / CASE_MGMT_CASE_OPTION_H;
    int pos = Global_Case_Case_Scroll + row;
    if (row >= 0 && row < visible && pos >= 0 && pos < count) {
        case_select_case_option(matches[pos]);
        case_set_status("Case # selected", Case_Text);
        return 1;
    }
    return 1;
}

static int case_handle_country_dropdown_click(int mx, int my, SDL_Rect field){
    int matches[512];
    int count;
    int visible;
    SDL_Rect menu;

    if (!Global_Case_Country_Dropdown_Open) return 0;
    count = case_build_country_matches(matches, (int)(sizeof(matches) / sizeof(matches[0])));
    if (count <= 0) {
        Global_Case_Country_Dropdown_Open = 0;
        return 0;
    }

    visible = count - Global_Case_Country_Scroll;
    if (visible > CASE_MGMT_COUNTRY_MAX_VISIBLE) visible = CASE_MGMT_COUNTRY_MAX_VISIBLE;
    if (visible < 0) visible = 0;
    menu = case_country_dropdown_rect(field, visible);

    if (!case_point_in_rect(mx, my, menu)) {
        Global_Case_Country_Dropdown_Open = 0;
        return 0;
    }

    int row = (my - menu.y) / CASE_MGMT_COUNTRY_OPTION_H;
    int pos = Global_Case_Country_Scroll + row;
    if (row >= 0 && row < visible && pos >= 0 && pos < count) {
        case_select_country_option(matches[pos]);
        case_set_status("Country selected", Case_Text);
        return 1;
    }
    return 1;
}

static void case_clamp_case_scroll(void){
    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count = case_build_case_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
    int max_scroll = count - CASE_MGMT_CASE_MAX_VISIBLE;
    if (max_scroll < 0) max_scroll = 0;
    if (Global_Case_Case_Scroll < 0) Global_Case_Case_Scroll = 0;
    if (Global_Case_Case_Scroll > max_scroll) Global_Case_Case_Scroll = max_scroll;
}

static void case_clamp_country_scroll(void){
    int matches[512];
    int count = case_build_country_matches(matches, (int)(sizeof(matches) / sizeof(matches[0])));
    int max_scroll = count - CASE_MGMT_COUNTRY_MAX_VISIBLE;
    if (max_scroll < 0) max_scroll = 0;
    if (Global_Case_Country_Scroll < 0) Global_Case_Country_Scroll = 0;
    if (Global_Case_Country_Scroll > max_scroll) Global_Case_Country_Scroll = max_scroll;
}

int CASE_MANAGEMENT_handle_event(const SDL_Event *event, int win_w, int win_h){
    SDL_Rect canvas;
    SDL_Rect editor;
    SDL_Rect new_btn;
    SDL_Rect link_btn;
    SDL_Rect save_btn;
    SDL_Rect load_btn;
    SDL_Rect fields[CASE_MGMT_FIELD_COUNT];
    SDL_Rect duplicate_btn;
    SDL_Rect delete_btn;

    if (!event || !Global_CaseManagement_Mode) return 0;

    canvas = case_canvas_rect(win_w, win_h);
    editor = case_editor_rect(win_w, win_h);
    case_ensure_view(canvas);
    case_toolbar_rects(win_w, &new_btn, &link_btn, &save_btn, &load_btn);
    case_editor_field_rects(editor, fields, &duplicate_btn, &delete_btn);

    if (Global_Case_Description_Popup_Open) {
        SDL_Rect popup = case_description_popup_rect(win_w, win_h);
        SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
        SDL_Rect text_rect = {popup.x + 18, popup.y + 58, popup.w - 36, popup.h - 88};
        char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);

        if (event->type == SDL_TEXTINPUT) {
            if (text) case_insert_text(text,
                                      &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION],
                                      event->text.text,
                                      CASE_MGMT_FIELD_DESCRIPTION);
            case_clamp_description_scroll(text_rect);
            return 1;
        }

        if (event->type == SDL_MOUSEWHEEL) {
            Global_Case_Description_Popup_Scroll -= event->wheel.y;
            case_clamp_description_scroll(text_rect);
            return 1;
        }

        if (event->type == SDL_KEYDOWN) {
            SDL_Keycode key = event->key.keysym.sym;
            SDL_Keymod mod = SDL_GetModState();
            if ((mod & KMOD_CTRL) && key == SDLK_v) {
                case_paste_description_from_clipboard();
                case_clamp_description_scroll(text_rect);
                return 1;
            }
            if (key == SDLK_ESCAPE) {
                Global_Case_Description_Popup_Open = 0;
                return 1;
            }
            if (key == SDLK_BACKSPACE) {
                if (text) case_backspace(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION]);
                return 1;
            }
            if (key == SDLK_DELETE) {
                if (text) case_delete_at_cursor(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION]);
                return 1;
            }
            if (key == SDLK_LEFT) {
                case_description_move_horizontal(-1);
                return 1;
            }
            if (key == SDLK_RIGHT) {
                case_description_move_horizontal(1);
                return 1;
            }
            if (key == SDLK_UP) {
                case_description_move_vertical(-1);
                return 1;
            }
            if (key == SDLK_DOWN) {
                case_description_move_vertical(1);
                return 1;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (text) case_insert_text(text,
                                          &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION],
                                          "\n",
                                          CASE_MGMT_FIELD_DESCRIPTION);
                return 1;
            }
            return 1;
        }

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
            int mx = event->button.x;
            int my = event->button.y;
            if (case_point_in_rect(mx, my, close_btn)) {
                Global_Case_Description_Popup_Open = 0;
                case_description_clear_selection();
                return 1;
            }
            if (case_point_in_rect(mx, my, text_rect)) {
                Global_Case_Active_Field = CASE_MGMT_FIELD_DESCRIPTION;
                case_set_description_cursor_from_mouse_scrolled(text_rect,
                                                                mx,
                                                                my,
                                                                Global_Case_Description_Popup_Scroll);
                case_description_start_selection_at_cursor();
                return 1;
            }
            if (!case_point_in_rect(mx, my, popup)) {
                Global_Case_Description_Popup_Open = 0;
                case_description_clear_selection();
                return 1;
            }
            return 1;
        }

        if (event->type == SDL_MOUSEMOTION && Global_Case_Description_Selecting) {
            int mx = event->motion.x;
            int my = event->motion.y;
            if (case_point_in_rect(mx, my, text_rect)) {
                case_set_description_cursor_from_mouse_scrolled(text_rect,
                                                                mx,
                                                                my,
                                                                Global_Case_Description_Popup_Scroll);
                case_description_update_selection_to_cursor();
            }
            return 1;
        }

        if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
            Global_Case_Description_Selecting = 0;
            return 1;
        }

        return 1;
    }

    if (Global_Case_Source_Popup_Open) {
        if (event->type == SDL_TEXTINPUT && Global_Case_Source_Search_Active) {
            size_t len = strlen(Global_Case_Source_Search);
            const char *src = event->text.text;
            while (*src && len + 1 < sizeof(Global_Case_Source_Search)) {
                char c = *src++;
                if (c >= 32 && c <= 126) {
                    int cursor = Global_Case_Source_Search_Cursor;
                    if (cursor < 0) cursor = 0;
                    if (cursor > (int)len) cursor = (int)len;
                    memmove(Global_Case_Source_Search + cursor + 1,
                            Global_Case_Source_Search + cursor,
                            len - (size_t)cursor + 1);
                    Global_Case_Source_Search[cursor] = c;
                    Global_Case_Source_Search_Cursor = cursor + 1;
                    len++;
                }
            }
            Global_Case_Source_Scroll = 0;
            case_clamp_source_scroll();
            return 1;
        }

        if (event->type == SDL_KEYDOWN) {
            SDL_Keycode key = event->key.keysym.sym;
            if (Global_Case_Source_Search_Active) {
                int len = (int)strlen(Global_Case_Source_Search);
                if (key == SDLK_ESCAPE) {
                    Global_Case_Source_Search_Active = 0;
                    return 1;
                }
                if (key == SDLK_BACKSPACE) {
                    int cursor = Global_Case_Source_Search_Cursor;
                    if (cursor > 0 && len > 0) {
                        if (cursor > len) cursor = len;
                        memmove(Global_Case_Source_Search + cursor - 1,
                                Global_Case_Source_Search + cursor,
                                len - cursor + 1);
                        Global_Case_Source_Search_Cursor = cursor - 1;
                        Global_Case_Source_Scroll = 0;
                    }
                    case_clamp_source_scroll();
                    return 1;
                }
                if (key == SDLK_DELETE) {
                    int cursor = Global_Case_Source_Search_Cursor;
                    if (cursor < 0) cursor = 0;
                    if (cursor < len) {
                        memmove(Global_Case_Source_Search + cursor,
                                Global_Case_Source_Search + cursor + 1,
                                len - cursor);
                        Global_Case_Source_Scroll = 0;
                    }
                    case_clamp_source_scroll();
                    return 1;
                }
                if (key == SDLK_LEFT) {
                    if (Global_Case_Source_Search_Cursor > 0) Global_Case_Source_Search_Cursor--;
                    return 1;
                }
                if (key == SDLK_RIGHT) {
                    if (Global_Case_Source_Search_Cursor < len) Global_Case_Source_Search_Cursor++;
                    return 1;
                }
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    Global_Case_Source_Search_Active = 0;
                    return 1;
                }
            }
            if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                Global_Case_Source_Popup_Open = 0;
                Global_Case_Source_Search_Active = 0;
                return 1;
            }
            if (key == SDLK_r) {
                case_scan_source_files();
                return 1;
            }
            if (key == SDLK_UP) {
                Global_Case_Source_Scroll--;
                case_clamp_source_scroll();
                return 1;
            }
            if (key == SDLK_DOWN) {
                Global_Case_Source_Scroll++;
                case_clamp_source_scroll();
                return 1;
            }
            return 1;
        }

        if (event->type == SDL_MOUSEWHEEL) {
            Global_Case_Source_Scroll -= event->wheel.y;
            case_clamp_source_scroll();
            return 1;
        }

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
            int mx = event->button.x;
            int my = event->button.y;
            return case_handle_source_popup_click(mx, my, win_w, win_h);
        }

        if (event->type == SDL_MOUSEMOTION) {
            return 1;
        }
    }

    if (event->type == SDL_TEXTINPUT && Global_Case_Active_Field != CASE_MGMT_FIELD_NONE) {
        if (Global_Case_Active_Field == CASE_MGMT_FIELD_STATUS ||
            Global_Case_Active_Field == CASE_MGMT_FIELD_SOURCE_FILE) return 1;
        char *text = case_selected_field_text(Global_Case_Active_Field);
        if (text) case_insert_text(text, &Global_Case_Field_Cursor[Global_Case_Active_Field], event->text.text, Global_Case_Active_Field);
        if (Global_Case_Active_Field == CASE_MGMT_FIELD_CASE_NUMBER) {
            Global_Case_Case_Dropdown_Open = 1;
            Global_Case_Case_Scroll = 0;
            case_clamp_case_scroll();
        }
        if (Global_Case_Active_Field == CASE_MGMT_FIELD_COUNTRY) {
            Global_Case_Country_Dropdown_Open = 1;
            Global_Case_Country_Scroll = 0;
            case_clamp_country_scroll();
        }
        return 1;
    }

    if (event->type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        case_get_adjusted_mouse_state(&mx, &my);
        if (Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count) {
            if (Global_Case_Case_Dropdown_Open) {
                int matches[CASE_MGMT_SOURCE_MAX_FILES];
                int count = case_build_case_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
                int visible = count - Global_Case_Case_Scroll;
                if (visible > CASE_MGMT_CASE_MAX_VISIBLE) visible = CASE_MGMT_CASE_MAX_VISIBLE;
                SDL_Rect menu = case_case_dropdown_rect(fields[CASE_MGMT_FIELD_CASE_NUMBER], visible > 0 ? visible : 1);
                if (case_point_in_rect(mx, my, menu)) {
                    Global_Case_Case_Scroll -= event->wheel.y;
                    case_clamp_case_scroll();
                    return 1;
                }
            }
            if (Global_Case_Country_Dropdown_Open) {
                int matches[512];
                int count = case_build_country_matches(matches, (int)(sizeof(matches) / sizeof(matches[0])));
                int visible = count - Global_Case_Country_Scroll;
                if (visible > CASE_MGMT_COUNTRY_MAX_VISIBLE) visible = CASE_MGMT_COUNTRY_MAX_VISIBLE;
                SDL_Rect menu = case_country_dropdown_rect(fields[CASE_MGMT_FIELD_COUNTRY], visible > 0 ? visible : 1);
                if (case_point_in_rect(mx, my, menu)) {
                    Global_Case_Country_Scroll -= event->wheel.y;
                    case_clamp_country_scroll();
                    return 1;
                }
            }
        }
        if (case_point_in_rect(mx, my, canvas) && event->wheel.y != 0) {
            double before_x = Global_Case_View_X + ((double)mx - (double)canvas.x) / Global_Case_Zoom;
            double before_y = Global_Case_View_Y + ((double)my - (double)canvas.y) / Global_Case_Zoom;
            double factor = event->wheel.y > 0 ? 1.10 : (1.0 / 1.10);

            Global_Case_Zoom = case_limit_double(Global_Case_Zoom * factor,
                                                 CASE_MGMT_MIN_ZOOM,
                                                 CASE_MGMT_MAX_ZOOM);
            Global_Case_View_X = before_x - ((double)mx - (double)canvas.x) / Global_Case_Zoom;
            Global_Case_View_Y = before_y - ((double)my - (double)canvas.y) / Global_Case_Zoom;
            case_set_status("Case graph zoom adjusted", Case_Text);
            return 1;
        }
    }

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        SDL_Keymod mod = SDL_GetModState();

        if ((mod & KMOD_CTRL) && key == SDLK_s) {
            case_save();
            return 1;
        }
        if ((mod & KMOD_CTRL) && key == SDLK_o) {
            case_load();
            return 1;
        }

        if (Global_Case_Active_Field != CASE_MGMT_FIELD_NONE) {
            char *text = case_selected_field_text(Global_Case_Active_Field);
            if ((mod & KMOD_CTRL) && key == SDLK_v && Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION) {
                case_paste_description_from_clipboard();
                return 1;
            }
            if (key == SDLK_BACKSPACE) {
                if (text &&
                    Global_Case_Active_Field != CASE_MGMT_FIELD_STATUS &&
                    Global_Case_Active_Field != CASE_MGMT_FIELD_SOURCE_FILE) {
                    case_backspace(text, &Global_Case_Field_Cursor[Global_Case_Active_Field]);
                    if (Global_Case_Active_Field == CASE_MGMT_FIELD_CASE_NUMBER) { Global_Case_Case_Dropdown_Open = 1; Global_Case_Case_Scroll = 0; case_clamp_case_scroll(); }
                    if (Global_Case_Active_Field == CASE_MGMT_FIELD_COUNTRY) { Global_Case_Country_Dropdown_Open = 1; Global_Case_Country_Scroll = 0; case_clamp_country_scroll(); }
                }
                return 1;
            }
            if (key == SDLK_DELETE) {
                if (text &&
                    Global_Case_Active_Field != CASE_MGMT_FIELD_STATUS &&
                    Global_Case_Active_Field != CASE_MGMT_FIELD_SOURCE_FILE) {
                    case_delete_at_cursor(text, &Global_Case_Field_Cursor[Global_Case_Active_Field]);
                    if (Global_Case_Active_Field == CASE_MGMT_FIELD_CASE_NUMBER) { Global_Case_Case_Dropdown_Open = 1; Global_Case_Case_Scroll = 0; case_clamp_case_scroll(); }
                    if (Global_Case_Active_Field == CASE_MGMT_FIELD_COUNTRY) { Global_Case_Country_Dropdown_Open = 1; Global_Case_Country_Scroll = 0; case_clamp_country_scroll(); }
                }
                return 1;
            }
            if (key == SDLK_LEFT) {
                if (Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION) {
                    case_description_move_horizontal(-1);
                }
                else if (Global_Case_Field_Cursor[Global_Case_Active_Field] > 0) {
                    Global_Case_Field_Cursor[Global_Case_Active_Field]--;
                }
                return 1;
            }
            if (key == SDLK_RIGHT) {
                if (Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION) {
                    case_description_move_horizontal(1);
                }
                else if (text && Global_Case_Field_Cursor[Global_Case_Active_Field] < (int)strlen(text)) {
                    Global_Case_Field_Cursor[Global_Case_Active_Field]++;
                }
                return 1;
            }
            if (key == SDLK_UP && Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION) {
                case_description_move_vertical(-1);
                return 1;
            }
            if (key == SDLK_DOWN && Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION) {
                case_description_move_vertical(1);
                return 1;
            }
            if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
                Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION) {
                if (text) case_insert_text(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION], "\n", CASE_MGMT_FIELD_DESCRIPTION);
                return 1;
            }
            if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
                Global_Case_Status_Dropdown_Open = 0;
                Global_Case_Case_Dropdown_Open = 0;
                Global_Case_Country_Dropdown_Open = 0;
                Global_Case_Calendar_Open = 0;
                Global_Case_Source_Popup_Open = 0;
                return 1;
            }
            return 1;
        }

        if (key == SDLK_n) {
            case_add_block(canvas);
            return 1;
        }
        if (key == SDLK_l) {
            Global_Case_Link_Mode = !Global_Case_Link_Mode;
            Global_Case_Link_Source = -1;
            case_set_status(Global_Case_Link_Mode ? "Link mode: click source, then destination" : "Link mode disabled", Case_Text);
            return 1;
        }
        if (key == SDLK_d) {
            case_duplicate_selected_block(canvas);
            return 1;
        }
        if (key == SDLK_s && !(mod & KMOD_CTRL)) {
            case_cycle_status();
            return 1;
        }
        if (key == SDLK_DELETE || key == SDLK_BACKSPACE) {
            case_delete_selected();
            return 1;
        }
        if (key == SDLK_ESCAPE) {
            Global_Case_Link_Mode = 0;
            Global_Case_Link_Source = -1;
            Global_Case_Dragging = 0;
            Global_Case_Panning = 0;
            Global_Case_Link_Dragging = 0;
            Global_Case_Box_Selecting = 0;
            Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
            Global_Case_Status_Dropdown_Open = 0;
            Global_Case_Calendar_Open = 0;
            return 1;
        }
    }

    if (event->type == SDL_MOUSEMOTION) {
        if (Global_Case_Description_Selecting &&
            !Global_Case_Description_Popup_Open &&
            Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION &&
            Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count) {
            int mx = event->motion.x;
            int my = event->motion.y;
            case_set_description_cursor_from_mouse(fields[CASE_MGMT_FIELD_DESCRIPTION], mx, my);
            case_description_update_selection_to_cursor();
            return 1;
        }

        if (Global_Case_Status_Dropdown_Open) {
            SDL_Rect menu = case_status_dropdown_rect(fields[CASE_MGMT_FIELD_STATUS]);
            int mmx = event->motion.x;
            int mmy = event->motion.y;
            Global_Case_Status_Dropdown_Hover = case_point_in_rect(mmx, mmy, menu) ?
                (mmy - menu.y) / CASE_MGMT_STATUS_OPTION_H : -1;
        }

        if (Global_Case_Link_Dragging) {
            int motion_x = event->motion.x;
            int motion_y = event->motion.y;
            int side = -1;
            Global_Case_Link_Drag_Mouse_X = motion_x;
            Global_Case_Link_Drag_Mouse_Y = motion_y;
            Global_Case_Link_Drag_Target_Index = case_nearest_endpoint(motion_x,
                                                                       motion_y,
                                                                       canvas,
                                                                       Global_Case_Link_Drag_Start_Index,
                                                                       &side);
            Global_Case_Link_Drag_Target_Side = side;
            return 1;
        }

        if (Global_Case_Box_Selecting) {
            Global_Case_Box_End_X = event->motion.x;
            Global_Case_Box_End_Y = event->motion.y;
            return 1;
        }

        if (Global_Case_Panning) {
            int motion_x = event->motion.x;
            int motion_y = event->motion.y;
            int dx = motion_x - Global_Case_Pan_Last_X;
            int dy = motion_y - Global_Case_Pan_Last_Y;
            Global_Case_View_X -= (double)dx / Global_Case_Zoom;
            Global_Case_View_Y -= (double)dy / Global_Case_Zoom;
            Global_Case_Pan_Last_X = motion_x;
            Global_Case_Pan_Last_Y = motion_y;
            return 1;
        }

        if (Global_Case_Dragging) {
            int motion_x = event->motion.x;
            int motion_y = event->motion.y;
            int world_x = case_screen_to_world_x(canvas, motion_x);
            int world_y = case_screen_to_world_y(canvas, motion_y);
            int dx = world_x - Global_Case_Drag_Last_World_X;
            int dy = world_y - Global_Case_Drag_Last_World_Y;

            if (case_selected_block_count() == 0 &&
                Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count) {
                Global_Case_Selected_Blocks[Global_Case_Selected] = 1;
            }

            if (dx != 0 || dy != 0) {
                for (int i = 0; i < Global_Case_Block_Count; i++) {
                    if (Global_Case_Selected_Blocks[i]) {
                        Global_Case_Blocks[i].x += dx;
                        Global_Case_Blocks[i].y += dy;
                    }
                }
                Global_Case_Drag_Last_World_X = world_x;
                Global_Case_Drag_Last_World_Y = world_y;
            }
            return 1;
        }
    }

    if (event->type == SDL_MOUSEBUTTONDOWN &&
        (event->button.button == SDL_BUTTON_RIGHT || event->button.button == SDL_BUTTON_MIDDLE)) {
        int mx = event->button.x;
        int my = event->button.y;
        if (case_point_in_rect(mx, my, canvas)) {
            Global_Case_Panning = 1;
            Global_Case_Pan_Last_X = mx;
            Global_Case_Pan_Last_Y = my;
            return 1;
        }
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int mx = event->button.x;
        int my = event->button.y;

        if (Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count) {
            if (Global_Case_Case_Dropdown_Open) {
                if (case_handle_case_dropdown_click(mx, my, fields[CASE_MGMT_FIELD_CASE_NUMBER])) return 1;
                if (!case_point_in_rect(mx, my, fields[CASE_MGMT_FIELD_CASE_NUMBER])) Global_Case_Case_Dropdown_Open = 0;
            }

            if (Global_Case_Country_Dropdown_Open) {
                if (case_handle_country_dropdown_click(mx, my, fields[CASE_MGMT_FIELD_COUNTRY])) return 1;
                if (!case_point_in_rect(mx, my, fields[CASE_MGMT_FIELD_COUNTRY])) Global_Case_Country_Dropdown_Open = 0;
            }

            if (Global_Case_Status_Dropdown_Open) {
                if (case_handle_status_dropdown_click(mx, my, fields[CASE_MGMT_FIELD_STATUS])) return 1;
                if (!case_point_in_rect(mx, my, fields[CASE_MGMT_FIELD_STATUS])) Global_Case_Status_Dropdown_Open = 0;
            }

            if (Global_Case_Calendar_Open) {
                SDL_Rect active_date_field = fields[Global_Case_Calendar_Field];
                if (case_handle_calendar_click(mx, my, active_date_field)) return 1;
                if (!case_point_in_rect(mx, my, active_date_field)) Global_Case_Calendar_Open = 0;
            }
        }

        if (case_point_in_rect(mx, my, new_btn)) {
            case_add_block(canvas);
            return 1;
        }
        if (case_point_in_rect(mx, my, link_btn)) {
            Global_Case_Link_Mode = !Global_Case_Link_Mode;
            Global_Case_Link_Source = -1;
            case_set_status(Global_Case_Link_Mode ? "Link mode: click source, then destination" : "Link mode disabled", Case_Text);
            return 1;
        }
        if (case_point_in_rect(mx, my, save_btn)) {
            case_save();
            return 1;
        }
        if (case_point_in_rect(mx, my, load_btn)) {
            case_load();
            return 1;
        }

        if (Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count) {
            if (case_point_in_rect(mx, my, duplicate_btn)) {
                case_duplicate_selected_block(canvas);
                return 1;
            }
            if (case_point_in_rect(mx, my, delete_btn)) {
                case_delete_selected();
                return 1;
            }
            SDL_Rect desc_open_btn = case_description_open_button_rect(fields[CASE_MGMT_FIELD_DESCRIPTION]);
            if (case_point_in_rect(mx, my, desc_open_btn)) {
                Global_Case_Active_Field = CASE_MGMT_FIELD_DESCRIPTION;
                Global_Case_Description_Popup_Open = 1;
                Global_Case_Description_Popup_Scroll = 0;
                Global_Case_Status_Dropdown_Open = 0;
                Global_Case_Case_Dropdown_Open = 0;
                Global_Case_Country_Dropdown_Open = 0;
                Global_Case_Calendar_Open = 0;
                Global_Case_Source_Popup_Open = 0;
                return 1;
            }

            for (int i = 0; i < CASE_MGMT_FIELD_COUNT; i++) {
                if (case_point_in_rect(mx, my, case_field_hit_rect(fields[i]))) {
                    if (i == CASE_MGMT_FIELD_STATUS) {
                        Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
                        Global_Case_Status_Dropdown_Open = !Global_Case_Status_Dropdown_Open;
                        Global_Case_Case_Dropdown_Open = 0;
                        Global_Case_Country_Dropdown_Open = 0;
                        Global_Case_Calendar_Open = 0;
                        Global_Case_Source_Popup_Open = 0;
                        return 1;
                    }

                    if (i == CASE_MGMT_FIELD_SOURCE_FILE) {
                        Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
                        Global_Case_Status_Dropdown_Open = 0;
                        Global_Case_Case_Dropdown_Open = 0;
                        Global_Case_Country_Dropdown_Open = 0;
                        Global_Case_Calendar_Open = 0;
                        case_scan_source_files();
                        Global_Case_Source_Popup_Open = 1;
                        return 1;
                    }

                    if (i == CASE_MGMT_FIELD_CASE_NUMBER) {
                        case_scan_case_files();
                        Global_Case_Active_Field = i;
                        Global_Case_Field_Cursor[i] = case_selected_field_text(i) ? (int)strlen(case_selected_field_text(i)) : 0;
                        Global_Case_Status_Dropdown_Open = 0;
                        Global_Case_Case_Dropdown_Open = 1;
                        Global_Case_Case_Scroll = 0;
                        Global_Case_Country_Dropdown_Open = 0;
                        Global_Case_Calendar_Open = 0;
                        Global_Case_Source_Popup_Open = 0;
                        return 1;
                    }

                    if (i == CASE_MGMT_FIELD_COUNTRY) {
                        Global_Case_Active_Field = i;
                        Global_Case_Field_Cursor[i] = case_selected_field_text(i) ? (int)strlen(case_selected_field_text(i)) : 0;
                        Global_Case_Status_Dropdown_Open = 0;
                        Global_Case_Case_Dropdown_Open = 0;
                        Global_Case_Country_Dropdown_Open = 1;
                        Global_Case_Country_Scroll = 0;
                        Global_Case_Calendar_Open = 0;
                        Global_Case_Source_Popup_Open = 0;
                        return 1;
                    }

                    Global_Case_Active_Field = i;
                    Global_Case_Status_Dropdown_Open = 0;
                    Global_Case_Case_Dropdown_Open = 0;
                    Global_Case_Country_Dropdown_Open = 0;
                    Global_Case_Source_Popup_Open = 0;
                    char *text = case_selected_field_text(i);
                    Global_Case_Field_Cursor[i] = text ? (int)strlen(text) : 0;

                    if (i == CASE_MGMT_FIELD_DESCRIPTION) {
                        case_set_description_cursor_from_mouse(fields[i], mx, my);
                        case_description_start_selection_at_cursor();
                    }
                    else {
                        case_description_clear_selection();
                    }

                    if (i == CASE_MGMT_FIELD_START_DATE || i == CASE_MGMT_FIELD_END_DATE) {
                        case_open_calendar_for_field(i);
                    }
                    else {
                        Global_Case_Calendar_Open = 0;
                    }
                    return 1;
                }
            }
        }

        if (case_point_in_rect(mx, my, canvas)) {
            int endpoint_side = -1;
            int endpoint_index = case_endpoint_at(mx, my, canvas, &endpoint_side);
            int block_index;

            if (endpoint_index >= 0) {
                case_select_only_block(endpoint_index);
                Global_Case_Selected_Link = -1;
                Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
                Global_Case_Status_Dropdown_Open = 0;
                Global_Case_Case_Dropdown_Open = 0;
                Global_Case_Country_Dropdown_Open = 0;
                Global_Case_Calendar_Open = 0;
                Global_Case_Source_Popup_Open = 0;
                Global_Case_Description_Popup_Open = 0;
                Global_Case_Link_Dragging = 1;
                Global_Case_Link_Drag_Start_Index = endpoint_index;
                Global_Case_Link_Drag_Start_Side = endpoint_side;
                Global_Case_Link_Drag_Mouse_X = mx;
                Global_Case_Link_Drag_Mouse_Y = my;
                Global_Case_Link_Drag_Target_Index = -1;
                Global_Case_Link_Drag_Target_Side = -1;
                case_set_status("Drag to another block endpoint to link", Case_Text);
                return 1;
            }

            int link_index = case_link_at(mx, my, canvas);
            if (link_index >= 0) {
                Global_Case_Selected_Link = link_index;
                case_clear_block_selection();
                Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
                Global_Case_Status_Dropdown_Open = 0;
                Global_Case_Case_Dropdown_Open = 0;
                Global_Case_Country_Dropdown_Open = 0;
                Global_Case_Calendar_Open = 0;
                Global_Case_Source_Popup_Open = 0;
                Global_Case_Description_Popup_Open = 0;
                case_set_status("Selected link; press Delete to remove it", Case_Warn);
                return 1;
            }

            block_index = case_block_at(mx, my, canvas);
            if (block_index >= 0) {
                SDL_Keymod click_mod = SDL_GetModState();
                if ((click_mod & (KMOD_SHIFT | KMOD_CTRL)) != 0) {
                    case_toggle_block_selection(block_index);
                    Global_Case_Selected_Link = -1;
                    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
                    Global_Case_Status_Dropdown_Open = 0;
                    Global_Case_Calendar_Open = 0;
                    return 1;
                }
                if (!case_is_block_selected(block_index)) {
                    case_select_only_block(block_index);
                }
                else {
                    Global_Case_Selected = block_index;
                }
                Global_Case_Selected_Link = -1;
                Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
                Global_Case_Status_Dropdown_Open = 0;
                Global_Case_Calendar_Open = 0;

                if (Global_Case_Link_Mode) {
                    if (Global_Case_Link_Source < 0) {
                        Global_Case_Link_Source = block_index;
                        case_set_status("Source selected; click a destination block", Case_Text);
                    }
                    else {
                        case_add_link(Global_Case_Link_Source, block_index, CASE_MGMT_SIDE_RIGHT, CASE_MGMT_SIDE_LEFT);
                        Global_Case_Link_Source = -1;
                    }
                    return 1;
                }

                int world_x = case_screen_to_world_x(canvas, mx);
                int world_y = case_screen_to_world_y(canvas, my);
                Global_Case_Dragging = 1;
                Global_Case_Drag_Last_World_X = world_x;
                Global_Case_Drag_Last_World_Y = world_y;
                return 1;
            }

            Global_Case_Selected_Link = -1;
            Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
            Global_Case_Status_Dropdown_Open = 0;
            Global_Case_Calendar_Open = 0;
            Global_Case_Source_Popup_Open = 0;
            Global_Case_Description_Popup_Open = 0;
            Global_Case_Box_Selecting = 1;
            Global_Case_Box_Start_X = mx;
            Global_Case_Box_Start_Y = my;
            Global_Case_Box_End_X = mx;
            Global_Case_Box_End_Y = my;
            return 1;
        }
    }

    if (event->type == SDL_MOUSEBUTTONUP) {
        int mx = event->button.x;
        int my = event->button.y;

        if (event->button.button != SDL_BUTTON_LEFT) {
            Global_Case_Panning = 0;
            return 1;
        }

        if (Global_Case_Link_Dragging) {
            int side = -1;
            int target = case_nearest_endpoint(mx,
                                               my,
                                               canvas,
                                               Global_Case_Link_Drag_Start_Index,
                                               &side);

            if (target >= 0 && target != Global_Case_Link_Drag_Start_Index) {
                if (Global_Case_Link_Drag_Start_Side == 0) {
                    case_add_link(target, Global_Case_Link_Drag_Start_Index, side, Global_Case_Link_Drag_Start_Side);
                }
                else {
                    case_add_link(Global_Case_Link_Drag_Start_Index, target, Global_Case_Link_Drag_Start_Side, side);
                }
            }
            else {
                case_set_status("Link drag cancelled", Case_Muted);
            }

            Global_Case_Link_Dragging = 0;
            Global_Case_Link_Drag_Start_Index = -1;
            Global_Case_Link_Drag_Start_Side = -1;
            Global_Case_Link_Drag_Target_Index = -1;
            Global_Case_Link_Drag_Target_Side = -1;
        }

        if (Global_Case_Box_Selecting) {
            SDL_Rect selection = case_make_normalized_rect(Global_Case_Box_Start_X,
                                                           Global_Case_Box_Start_Y,
                                                           Global_Case_Box_End_X,
                                                           Global_Case_Box_End_Y);
            SDL_Keymod up_mod = SDL_GetModState();
            if (selection.w >= 4 || selection.h >= 4) {
                case_select_blocks_in_rect(canvas, selection, (up_mod & (KMOD_SHIFT | KMOD_CTRL)) != 0);
            }
            else if ((up_mod & (KMOD_SHIFT | KMOD_CTRL)) == 0) {
                case_clear_block_selection();
            }
            Global_Case_Box_Selecting = 0;
        }

        Global_Case_Dragging = 0;
        Global_Case_Panning = 0;
        Global_Case_Description_Selecting = 0;
        return 1;
    }

    return 0;
}

static void case_draw_grid(SDL_Renderer *renderer, SDL_Rect rect){
    int step = (int)(42.0 * Global_Case_Zoom);
    if (step < 18) step = 18;
    if (step > 80) step = 80;

    SDL_SetRenderDrawColor(renderer, 0, 50, 20, 105);
    for (int x = rect.x; x < rect.x + rect.w; x += step) {
        SDL_RenderDrawLine(renderer, x, rect.y, x, rect.y + rect.h);
    }
    for (int y = rect.y; y < rect.y + rect.h; y += step) {
        SDL_RenderDrawLine(renderer, rect.x, y, rect.x + rect.w, y);
    }
}


static void case_draw_selection_box(SDL_Renderer *renderer){
    if (!Global_Case_Box_Selecting) return;
    SDL_Rect r = case_make_normalized_rect(Global_Case_Box_Start_X,
                                           Global_Case_Box_Start_Y,
                                           Global_Case_Box_End_X,
                                           Global_Case_Box_End_Y);
    if (r.w < 4 && r.h < 4) return;

    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    /* 85% transparent fill: 15% opacity over the workspace. */
    draw_filled_rect(renderer, r, (SDL_Color){0, 90, 35, 38});

    SDL_SetRenderDrawBlendMode(renderer, old_blend);

    draw_outline_rect(renderer, r, Case_Border_Hi);
    SDL_Rect inner = {r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    if (inner.w > 4 && inner.h > 4) draw_outline_rect(renderer, inner, Case_Muted);
}


static void case_side_vector(int side, int *dx, int *dy){
    int vx = 0;
    int vy = 0;
    if (side == CASE_MGMT_SIDE_LEFT) vx = -1;
    else if (side == CASE_MGMT_SIDE_RIGHT) vx = 1;
    else if (side == CASE_MGMT_SIDE_TOP) vy = -1;
    else if (side == CASE_MGMT_SIDE_BOTTOM) vy = 1;
    if (dx) *dx = vx;
    if (dy) *dy = vy;
}

static int case_guess_end_side_for_preview(int from_side, int x1, int y1, int x2, int y2){
    int dx = x2 - x1;
    int dy = y2 - y1;
    (void)from_side;
    if (abs(dx) >= abs(dy)) return dx >= 0 ? CASE_MGMT_SIDE_LEFT : CASE_MGMT_SIDE_RIGHT;
    return dy >= 0 ? CASE_MGMT_SIDE_TOP : CASE_MGMT_SIDE_BOTTOM;
}

static int case_route_points(int x1,
                             int y1,
                             int from_side,
                             int x2,
                             int y2,
                             int to_side,
                             int pts_x[6],
                             int pts_y[6]){
    int dx1 = 0;
    int dy1 = 0;
    int dx2 = 0;
    int dy2 = 0;
    int stem = (int)(34.0 * Global_Case_Zoom);
    int sx;
    int sy;
    int ex;
    int ey;
    int mid;

    if (stem < 20) stem = 20;
    if (stem > 54) stem = 54;

    case_side_vector(from_side, &dx1, &dy1);
    case_side_vector(to_side, &dx2, &dy2);

    sx = x1 + dx1 * stem;
    sy = y1 + dy1 * stem;
    ex = x2 + dx2 * stem;
    ey = y2 + dy2 * stem;

    pts_x[0] = x1;
    pts_y[0] = y1;
    pts_x[1] = sx;
    pts_y[1] = sy;

    if (from_side == CASE_MGMT_SIDE_TOP || from_side == CASE_MGMT_SIDE_BOTTOM ||
        to_side == CASE_MGMT_SIDE_TOP || to_side == CASE_MGMT_SIDE_BOTTOM) {
        mid = (sy + ey) / 2;
        pts_x[2] = sx;
        pts_y[2] = mid;
        pts_x[3] = ex;
        pts_y[3] = mid;
    }
    else {
        mid = (sx + ex) / 2;
        pts_x[2] = mid;
        pts_y[2] = sy;
        pts_x[3] = mid;
        pts_y[3] = ey;
    }

    pts_x[4] = ex;
    pts_y[4] = ey;
    pts_x[5] = x2;
    pts_y[5] = y2;
    return 6;
}

static int case_link_points(SDL_Rect canvas,
                            const Type_Case_Link *link,
                            int pts_x[6],
                            int pts_y[6]){
    int from_index = link ? case_find_block_index_by_id(link->from_id) : -1;
    int to_index = link ? case_find_block_index_by_id(link->to_id) : -1;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    if (from_index < 0 || to_index < 0) {
        for (int i = 0; i < 6; i++) {
            pts_x[i] = 0;
            pts_y[i] = 0;
        }
        return 0;
    }

    case_endpoint_center(from_index, canvas, link->from_side, &x1, &y1);
    case_endpoint_center(to_index, canvas, link->to_side, &x2, &y2);
    return case_route_points(x1, y1, link->from_side, x2, y2, link->to_side, pts_x, pts_y);
}

static int case_segment_distance2(int px, int py, int x1, int y1, int x2, int y2){
    double vx = (double)(x2 - x1);
    double vy = (double)(y2 - y1);
    double wx = (double)(px - x1);
    double wy = (double)(py - y1);
    double c1 = vx * wx + vy * wy;
    double c2 = vx * vx + vy * vy;
    double t = c2 > 0.0 ? c1 / c2 : 0.0;
    double dx;
    double dy;

    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    dx = (double)px - ((double)x1 + t * vx);
    dy = (double)py - ((double)y1 + t * vy);
    return (int)(dx * dx + dy * dy);
}

static int case_link_at(int x, int y, SDL_Rect canvas){
    for (int i = Global_Case_Link_Count - 1; i >= 0; i--) {
        int px[6];
        int py[6];
        int point_count = case_link_points(canvas, &Global_Case_Links[i], px, py);
        for (int s = 0; s + 1 < point_count; s++) {
            if (case_segment_distance2(x, y, px[s], py[s], px[s + 1], py[s + 1]) <= 9 * 9) {
                return i;
            }
        }
    }
    return -1;
}

static void case_draw_link(SDL_Renderer *renderer, SDL_Rect canvas, int link_index, int related_to_selected_block){
    if (link_index < 0 || link_index >= Global_Case_Link_Count) return;

    Type_Case_Link *link = &Global_Case_Links[link_index];
    int from_index = case_find_block_index_by_id(link->from_id);
    int to_index = case_find_block_index_by_id(link->to_id);
    int connector = case_connector_px();
    int px[6];
    int py[6];
    int selected = link_index == Global_Case_Selected_Link;
    SDL_Color color = selected ? (SDL_Color){255, 150, 45, 255} :
                      (related_to_selected_block ? Case_Border_Hi : Case_Border);

    if (from_index < 0 || to_index < 0) return;

    int point_count = case_link_points(canvas, link, px, py);
    if (point_count < 2) return;

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i + 1 < point_count; i++) {
        SDL_RenderDrawLine(renderer, px[i], py[i], px[i + 1], py[i + 1]);
        if (selected) {
            SDL_RenderDrawLine(renderer, px[i], py[i] + 1, px[i + 1], py[i + 1] + 1);
            SDL_RenderDrawLine(renderer, px[i] + 1, py[i], px[i + 1] + 1, py[i + 1]);
        }
    }

    SDL_Rect out = {px[0] - connector / 2, py[0] - connector / 2, connector, connector};
    SDL_Rect in  = {px[point_count - 1] - connector / 2, py[point_count - 1] - connector / 2, connector, connector};
    draw_filled_rect(renderer, out, color);
    draw_filled_rect(renderer, in, color);
}

static void case_draw_arrow_head(SDL_Renderer *renderer, int x, int y, int direction, SDL_Color color){
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    if (direction >= 0) {
        SDL_RenderDrawLine(renderer, x, y, x - 8, y - 5);
        SDL_RenderDrawLine(renderer, x, y, x - 8, y + 5);
        SDL_RenderDrawLine(renderer, x - 1, y, x - 8, y - 4);
        SDL_RenderDrawLine(renderer, x - 1, y, x - 8, y + 4);
    }
    else {
        SDL_RenderDrawLine(renderer, x, y, x + 8, y - 5);
        SDL_RenderDrawLine(renderer, x, y, x + 8, y + 5);
        SDL_RenderDrawLine(renderer, x + 1, y, x + 8, y - 4);
        SDL_RenderDrawLine(renderer, x + 1, y, x + 8, y + 4);
    }
}

static void case_draw_link_preview(SDL_Renderer *renderer, SDL_Rect canvas){
    if (!Global_Case_Link_Dragging ||
        Global_Case_Link_Drag_Start_Index < 0 ||
        Global_Case_Link_Drag_Start_Index >= Global_Case_Block_Count) return;

    int x1 = 0;
    int y1 = 0;
    int x2 = Global_Case_Link_Drag_Mouse_X;
    int y2 = Global_Case_Link_Drag_Mouse_Y;
    int target = Global_Case_Link_Drag_Target_Index;
    int target_side = Global_Case_Link_Drag_Target_Side;
    int end_side;
    int px[6];
    int py[6];
    int point_count;
    SDL_Color color = target >= 0 ? Case_Border_Hi : Case_Warn;

    case_endpoint_center(Global_Case_Link_Drag_Start_Index,
                         canvas,
                         Global_Case_Link_Drag_Start_Side,
                         &x1,
                         &y1);

    if (target >= 0 && target < Global_Case_Block_Count && target_side >= 0) {
        case_endpoint_center(target, canvas, target_side, &x2, &y2);
        end_side = target_side;
    }
    else {
        end_side = case_guess_end_side_for_preview(Global_Case_Link_Drag_Start_Side, x1, y1, x2, y2);
    }

    point_count = case_route_points(x1,
                                    y1,
                                    Global_Case_Link_Drag_Start_Side,
                                    x2,
                                    y2,
                                    end_side,
                                    px,
                                    py);

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i + 1 < point_count; i++) {
        SDL_RenderDrawLine(renderer, px[i], py[i], px[i + 1], py[i + 1]);
        SDL_RenderDrawLine(renderer, px[i], py[i] + 1, px[i + 1], py[i + 1] + 1);
    }
    case_draw_arrow_head(renderer, x2, y2, x2 >= px[point_count - 2] ? 1 : -1, color);

    if (target >= 0 && target < Global_Case_Block_Count) {
        SDL_Rect t = case_block_endpoint_rect(target, canvas, target_side >= 0 ? target_side : 0, 1);
        draw_outline_rect(renderer, t, Case_Border_Hi);
        SDL_Rect halo = {t.x - 4, t.y - 4, t.w + 8, t.h + 8};
        draw_outline_rect(renderer, halo, Case_Warn);
    }
}

static void case_draw_block(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect canvas, int index){
    SDL_Rect r = case_block_screen_rect(index, canvas);
    Type_Case_Block *b = &Global_Case_Blocks[index];
    int selected = case_is_block_selected(index) || index == Global_Case_Selected;
    SDL_Color border = selected ? Case_Border_Hi : Case_Border;
    SDL_Color status_color = case_status_color(b->status);
    char task[96];
    char user[96];
    char time_text[96];
    char time_short[96];
    char status[96];
    char id_label[64];
    int compact = Global_Case_Zoom < 0.68 || r.w < 190 || r.h < 88;
    int connector = (int)((double)CASE_MGMT_CONNECTOR_SIZE * Global_Case_Zoom);
    if (connector < 6) connector = 6;
    if (connector > 14) connector = 14;

    case_make_timeline_text(b, time_text, sizeof(time_text));
    case_shorten(b->task, task, sizeof(task), compact ? 18 : 30);
    case_shorten(b->assigned_to, user, sizeof(user), compact ? 14 : 24);
    case_shorten(time_text, time_short, sizeof(time_short), compact ? 14 : 24);
    case_shorten(b->status, status, sizeof(status), compact ? 12 : 18);
    snprintf(id_label, sizeof(id_label), "BLOCK %03d", b->id);

    draw_filled_rect(renderer, r, selected ? Case_Panel_2 : Case_Panel);
    draw_outline_rect(renderer, r, border);
    if (selected) {
        SDL_Rect inner = {r.x + 3, r.y + 3, r.w - 6, r.h - 6};
        draw_outline_rect(renderer, inner, border);
    }

    int header_h = compact ? 24 : 28;
    SDL_Rect header = {r.x, r.y, r.w, header_h};
    draw_filled_rect(renderer, header, (SDL_Color){0, 35, 14, 255});
    draw_outline_rect(renderer, header, border);
    draw_text(renderer, font, id_label, r.x + 10, r.y + 7, Case_Text);

    SDL_Rect status_badge = {r.x + r.w - (compact ? 82 : 92), r.y + 5, compact ? 72 : 82, 18};
    if (status_badge.x > r.x + 92) {
        draw_outline_rect(renderer, status_badge, status_color);
        case_draw_text_centered(renderer, font, status, status_badge, status_color);
    }

    if (compact) {
        draw_text(renderer, font, task, r.x + 10, r.y + 34, Case_Text);
        if (r.h > 62) draw_text(renderer, font, status, r.x + 10, r.y + 56, status_color);
    }
    else {
        draw_text(renderer, font, "TASK", r.x + 10, r.y + 38, Case_Muted);
        draw_text(renderer, font, task, r.x + 74, r.y + 38, Case_Text);

        draw_text(renderer, font, "USER", r.x + 10, r.y + 62, Case_Muted);
        draw_text(renderer, font, user, r.x + 74, r.y + 62, Case_Text);

        draw_text(renderer, font, "TIME", r.x + 10, r.y + 86, Case_Muted);
        draw_text(renderer, font, time_short, r.x + 74, r.y + 86, Case_Text);
    }

    SDL_Color conn_color = selected ? Case_Border_Hi : Case_Border;
    for (int side = 0; side < 4; side++) {
        int cx = 0;
        int cy = 0;
        case_endpoint_center(index, canvas, side, &cx, &cy);
        SDL_Rect port = {cx - connector / 2, cy - connector / 2, connector, connector};
        draw_filled_rect(renderer, port, conn_color);
        draw_outline_rect(renderer, port, Case_BG);
    }

    if (Global_Case_Link_Mode && Global_Case_Link_Source == index) {
        SDL_Rect halo = {r.x - 6, r.y - 6, r.w + 12, r.h + 12};
        draw_outline_rect(renderer, halo, Case_Warn);
    }
}

static void case_draw_input(SDL_Renderer *renderer,
                            TTF_Font *font,
                            SDL_Rect rect,
                            const char *label,
                            const char *text,
                            int active,
                            int cursor,
                            int dropdown){
    draw_text(renderer, font, label, rect.x, rect.y - 20, Case_Muted);
    draw_filled_rect(renderer, rect, active ? Case_Panel_2 : (SDL_Color){0, 7, 3, 255});
    draw_outline_rect(renderer, rect, active ? Case_Border_Hi : Case_Border);

    char short_text[CASE_MGMT_TEXT_MAX];
    case_shorten(text, short_text, sizeof(short_text), 32);
    draw_text(renderer, font, short_text, rect.x + 10, rect.y + 9, active ? Case_Text : Case_Muted);

    if (dropdown) {
        SDL_SetRenderDrawColor(renderer, Case_Border_Hi.r, Case_Border_Hi.g, Case_Border_Hi.b, Case_Border_Hi.a);
        int cx = rect.x + rect.w - 18;
        int cy = rect.y + rect.h / 2 - 2;
        SDL_RenderDrawLine(renderer, cx - 5, cy, cx, cy + 5);
        SDL_RenderDrawLine(renderer, cx, cy + 5, cx + 5, cy);
    }

    if (active && !dropdown && ((SDL_GetTicks64() / 450) % 2) == 0) {
        char prefix[CASE_MGMT_TEXT_MAX];
        int tw = 0;
        int th = 0;
        int len = text ? (int)strlen(text) : 0;
        if (cursor < 0) cursor = 0;
        if (cursor > len) cursor = len;
        snprintf(prefix, sizeof(prefix), "%.*s", cursor, text ? text : "");
        if (font) TTF_SizeText(font, prefix, &tw, &th);
        SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
        SDL_RenderDrawLine(renderer, rect.x + 10 + tw, rect.y + 7, rect.x + 10 + tw, rect.y + rect.h - 7);
    }
}

static void case_draw_status_dropdown(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect status_field){
    if (!Global_Case_Status_Dropdown_Open) return;

    SDL_Rect menu = case_status_dropdown_rect(status_field);
    draw_filled_rect(renderer, menu, (SDL_Color){0, 7, 3, 255});
    draw_outline_rect(renderer, menu, Case_Border_Hi);

    for (int i = 0; i < CASE_MGMT_STATUS_COUNT; i++) {
        SDL_Rect opt = {menu.x, menu.y + i * CASE_MGMT_STATUS_OPTION_H, menu.w, CASE_MGMT_STATUS_OPTION_H};
        SDL_Color color = case_status_color(CASE_MGMT_STATUS_OPTIONS[i]);
        if (i == Global_Case_Status_Dropdown_Hover) {
            draw_filled_rect(renderer, opt, Case_Panel_2);
            SDL_Rect inner = {opt.x + 3, opt.y + 3, opt.w - 6, opt.h - 6};
            draw_outline_rect(renderer, inner, color);
        }
        draw_outline_rect(renderer, opt, color);
        draw_text(renderer, font, CASE_MGMT_STATUS_OPTIONS[i], opt.x + 10, opt.y + 7, color);
    }
}

static void case_draw_calendar(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect date_field){
    if (!Global_Case_Calendar_Open) return;

    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const char *days[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

    SDL_Rect cal = case_calendar_rect(date_field);
    SDL_Rect prev = {cal.x + 10, cal.y + 8, 28, 24};
    SDL_Rect next = {cal.x + cal.w - 38, cal.y + 8, 28, 24};
    char title[64];
    int grid_x = cal.x + 8;
    int grid_y = cal.y + 58;
    int cell_w = (cal.w - 16) / 7;
    int cell_h = 26;
    int first = case_first_weekday(Global_Case_Calendar_Month, Global_Case_Calendar_Year);
    int total_days = case_days_in_month(Global_Case_Calendar_Month, Global_Case_Calendar_Year);
    int mx = 0;
    int my = 0;
    case_get_adjusted_mouse_state(&mx, &my);

    draw_filled_rect(renderer, cal, (SDL_Color){0, 7, 3, 255});
    draw_outline_rect(renderer, cal, Case_Border_Hi);

    case_draw_button(renderer, font, prev, "<", 0, case_point_in_rect(mx, my, prev), 0);
    case_draw_button(renderer, font, next, ">", 0, case_point_in_rect(mx, my, next), 0);

    snprintf(title,
             sizeof(title),
             "%s %d",
             months[Global_Case_Calendar_Month - 1],
             Global_Case_Calendar_Year);
    SDL_Rect title_rect = {cal.x + 46, cal.y + 8, cal.w - 92, 24};
    case_draw_text_centered(renderer, font, title, title_rect, Case_Text);

    for (int i = 0; i < 7; i++) {
        SDL_Rect drect = {grid_x + i * cell_w, cal.y + 36, cell_w, 18};
        case_draw_text_centered(renderer, font, days[i], drect, Case_Muted);
    }

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            int day_num = row * 7 + col - first + 1;
            SDL_Rect cell = {grid_x + col * cell_w, grid_y + row * cell_h, cell_w, cell_h};
            int day_hovered = (day_num >= 1 && day_num <= total_days && case_point_in_rect(mx, my, cell));
            if (day_hovered) {
                SDL_Rect halo = {cell.x - 2, cell.y - 2, cell.w + 4, cell.h + 4};
                draw_filled_rect(renderer, cell, Case_Panel_2);
                draw_outline_rect(renderer, halo, Case_Border_Hi);
                draw_outline_rect(renderer, cell, Case_Border_Hi);
            }
            else {
                draw_outline_rect(renderer, cell, (SDL_Color){0, 75, 30, 255});
            }
            if (day_num >= 1 && day_num <= total_days) {
                char day_text[16];
                snprintf(day_text, sizeof(day_text), "%d", day_num);
                case_draw_text_centered(renderer, font, day_text, cell, day_hovered ? Case_Text : Case_Muted);
            }
        }
    }

    draw_text(renderer, font, "Type MM/DD/YYYY or pick a day", cal.x + 10, cal.y + cal.h - 24, Case_Muted);
}


static int case_text_width(TTF_Font *font, const char *text, int fallback_chars){
    int w = 0;
    int h = 0;
    if (text && font && TTF_SizeText(font, text, &w, &h) == 0) return w;
    if (fallback_chars < 0) fallback_chars = 0;
    return fallback_chars * 8;
}

static void case_draw_description_selection(SDL_Renderer *renderer,
                                            TTF_Font *font,
                                            SDL_Rect rect,
                                            const char *src,
                                            const int starts[128],
                                            const int ends[128],
                                            int line_count,
                                            int first_line,
                                            int max_lines){
    int sel_a = 0;
    int sel_b = 0;
    int line_h = 19;

    if (!case_description_selection_range(&sel_a, &sel_b)) return;
    if (!src) src = "";

    for (int line = first_line;
         line < line_count && line < first_line + max_lines;
         line++) {
        int a = sel_a > starts[line] ? sel_a : starts[line];
        int b = sel_b < ends[line] ? sel_b : ends[line];
        if (sel_b > ends[line] && sel_b > starts[line] && line + 1 < line_count) b = ends[line];
        if (a > b) continue;
        if (a == b && !(sel_b > ends[line] && line + 1 < line_count)) continue;

        char before[CASE_MGMT_DESCRIPTION_MAX + 8];
        char selected[CASE_MGMT_DESCRIPTION_MAX + 8];
        int before_len = a - starts[line];
        int selected_len = b - a;
        int x0;
        int w;
        SDL_Rect hl;

        if (before_len < 0) before_len = 0;
        if (selected_len < 0) selected_len = 0;
        if (before_len >= (int)sizeof(before)) before_len = (int)sizeof(before) - 1;
        if (selected_len >= (int)sizeof(selected)) selected_len = (int)sizeof(selected) - 1;

        memcpy(before, src + starts[line], (size_t)before_len);
        before[before_len] = '\0';
        memcpy(selected, src + a, (size_t)selected_len);
        selected[selected_len] = '\0';

        x0 = rect.x + 9 + case_text_width(font, before, before_len);
        w = case_text_width(font, selected, selected_len);
        if (w < 6) w = 6;
        if (x0 < rect.x + 9) x0 = rect.x + 9;
        if (x0 + w > rect.x + rect.w - 9) w = rect.x + rect.w - 9 - x0;
        if (w <= 0) continue;

        hl = (SDL_Rect){x0,
                        rect.y + 6 + ((line - first_line) * line_h),
                        w,
                        line_h};
        draw_filled_rect(renderer, hl, (SDL_Color){0, 78, 120, 255});
        draw_outline_rect(renderer, hl, Case_Blue);
    }
}

static void case_draw_description_box(SDL_Renderer *renderer,
                                      TTF_Font *font,
                                      SDL_Rect rect,
                                      const char *label,
                                      const char *text,
                                      int active){
    const char *src = text ? text : "";
    char local[CASE_MGMT_DESCRIPTION_MAX + 8];
    const char *line_starts[128];
    int line_count = 0;
    int line_h = 19;
    int max_lines = (rect.h - 12) / line_h;
    int first_line = 0;
    int y;

    draw_text(renderer, font, label, rect.x, rect.y - 20, Case_Muted);
    draw_filled_rect(renderer, rect, active ? Case_Panel_2 : (SDL_Color){0, 7, 3, 255});
    draw_outline_rect(renderer, rect, active ? Case_Border_Hi : Case_Border);

    Global_Case_Description_Font = font;
    Global_Case_Description_Wrap_Px = rect.w - 18;

    if (max_lines < 1) max_lines = 1;

    if (src[0]) {
        snprintf(local, sizeof(local), "%.*s", CASE_MGMT_DESCRIPTION_MAX - 1, src);
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

    if (line_count > max_lines) first_line = line_count - max_lines;

    {
        int starts[128];
        int ends[128];
        int raw_line_count = case_description_build_lines(src, starts, ends);
        case_draw_description_selection(renderer, font, rect, src, starts, ends, raw_line_count, first_line, max_lines);
    }

    y = rect.y + 7;
    for (int i = first_line; i < line_count; i++) {
        char short_line[CASE_MGMT_DESCRIPTION_MAX + 16];
        snprintf(short_line, sizeof(short_line), "%s", line_starts[i]);
        draw_text(renderer,
                  font,
                  short_line,
                  rect.x + 9,
                  y,
                  src[0] || active ? Case_Text : Case_Muted);
        y += line_h;
        if (y + line_h > rect.y + rect.h) break;
    }

    if (active && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {
        int starts[128];
        int ends[128];
        int raw_line_count = case_description_build_lines(src, starts, ends);
        int cursor = Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
        int src_len = (int)strlen(src);
        int cursor_line = 0;

        if (cursor < 0) cursor = 0;
        if (cursor > src_len) cursor = src_len;

        for (int i = 0; i < raw_line_count; i++) {
            if (cursor >= starts[i] && cursor <= ends[i]) {
                cursor_line = i;
                break;
            }
            if (i == raw_line_count - 1 && cursor > ends[i]) cursor_line = i;
        }

        int raw_first_line = 0;
        if (raw_line_count > max_lines) raw_first_line = raw_line_count - max_lines;

        if (cursor_line >= raw_first_line && cursor_line < raw_first_line + max_lines) {
            int line_start = starts[cursor_line];
            int line_end = ends[cursor_line];
            int text_w = 0;
            int text_h = 0;
            if (cursor < line_start) cursor = line_start;
            if (cursor > line_end) cursor = line_end;

            if (cursor > line_start) {
                char before[CASE_MGMT_DESCRIPTION_MAX + 8];
                int before_len = cursor - line_start;
                if (before_len >= (int)sizeof(before)) before_len = (int)sizeof(before) - 1;
                memcpy(before, src + line_start, (size_t)before_len);
                before[before_len] = '\0';
                if (TTF_SizeText(font, before, &text_w, &text_h) != 0) text_w = before_len * 8;
            }

            int cx = rect.x + 9 + text_w;
            int cy0 = rect.y + 7 + ((cursor_line - raw_first_line) * line_h);
            int cy1 = cy0 + line_h - 2;
            if (cx < rect.x + 9) cx = rect.x + 9;
            if (cx > rect.x + rect.w - 9) cx = rect.x + rect.w - 9;

            SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
            SDL_RenderDrawLine(renderer, cx, cy0, cx, cy1);
            SDL_RenderDrawLine(renderer, cx + 1, cy0, cx + 1, cy1);
        }
    }
}

static void case_draw_source_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h){
    if (!Global_Case_Source_Popup_Open) return;

    SDL_Rect popup = case_source_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = case_source_search_rect(popup);
    SDL_Rect current_rect = {popup.x + 18, popup.y + 62, popup.w - 36, 42};
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};
    int filtered_count = case_source_filtered_count();
    int mx = 0;
    int my = 0;
    case_get_adjusted_mouse_state(&mx, &my);
    case_clamp_source_scroll();

    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, Case_Border_Hi);
    SDL_Rect inner = {popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8};
    draw_outline_rect(renderer, inner, Case_Border);

    draw_text(renderer, font, "SOURCE FILE", popup.x + 18, popup.y + 20, Case_Text);
    case_draw_button(renderer,
                     font,
                     close_btn,
                     "Close",
                     0,
                     case_point_in_rect(mx, my, close_btn),
                     0);

    draw_filled_rect(renderer, search, Global_Case_Source_Search_Active ? Case_Panel_2 : (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, search, Global_Case_Source_Search_Active ? Case_Border_Hi : Case_Border);
    if (Global_Case_Source_Search[0]) {
        draw_text(renderer, font, Global_Case_Source_Search, search.x + 10, search.y + 8, Case_Text);
    }
    else {
        draw_text(renderer, font, "Search file", search.x + 10, search.y + 8, Case_Muted);
    }
    if (Global_Case_Source_Search_Active && ((SDL_GetTicks64() / 450ULL) % 2ULL) == 0ULL) {
        int tw = 0;
        int th = 0;
        char prefix[sizeof(Global_Case_Source_Search)];
        int cursor = Global_Case_Source_Search_Cursor;
        int len = (int)strlen(Global_Case_Source_Search);
        if (cursor < 0) cursor = 0;
        if (cursor > len) cursor = len;
        snprintf(prefix, sizeof(prefix), "%.*s", cursor, Global_Case_Source_Search);
        if (font && TTF_SizeText(font, prefix, &tw, &th) != 0) tw = cursor * 8;
        SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
        SDL_RenderDrawLine(renderer, search.x + 10 + tw, search.y + 6, search.x + 10 + tw, search.y + search.h - 6);
    }

    draw_text(renderer, font, "Currently selected", current_rect.x, current_rect.y - 18, Case_Muted);
    draw_filled_rect(renderer, current_rect, Case_Panel_2);
    draw_outline_rect(renderer, current_rect, Case_Border_Hi);
    {
        char *current_source = case_selected_field_text(CASE_MGMT_FIELD_SOURCE_FILE);
        char current_text[CASE_MGMT_SOURCE_FILE_MAX + 32];
        if (current_source && current_source[0]) snprintf(current_text, sizeof(current_text), "%s", current_source);
        else snprintf(current_text, sizeof(current_text), "(none selected)");
        draw_text(renderer, font, current_text, current_rect.x + 10, current_rect.y + 12, current_source && current_source[0] ? Case_Text : Case_Muted);
    }

    draw_filled_rect(renderer, list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, list, Case_Border);

    if (Global_Case_Source_File_Count <= 0) {
        char empty_msg[640];
        snprintf(empty_msg, sizeof(empty_msg), "No .complex16 files found in %s/", Global_Case_Record_Dir);
        draw_text(renderer, font, empty_msg, list.x + 12, list.y + 14, Case_Warn);
        return;
    }

    if (filtered_count <= 0) {
        draw_text(renderer, font, "No files match the search.", list.x + 12, list.y + 14, Case_Warn);
        return;
    }

    int visible = list.h / CASE_MGMT_SOURCE_ROW_H;
    if (visible > CASE_MGMT_SOURCE_VISIBLE) visible = CASE_MGMT_SOURCE_VISIBLE;
    if (visible < 1) visible = 1;

    Global_Case_Source_Hover = -1;
    if (case_point_in_rect(mx, my, list)) {
        int row = (my - list.y) / CASE_MGMT_SOURCE_ROW_H;
        int filtered_index = Global_Case_Source_Scroll + row;
        int source_index = case_source_filtered_index_at(filtered_index);
        if (row >= 0 && row < visible && source_index >= 0 && source_index < Global_Case_Source_File_Count) {
            Global_Case_Source_Hover = source_index;
        }
    }

    for (int row = 0; row < visible; row++) {
        int filtered_index = Global_Case_Source_Scroll + row;
        int source_index = case_source_filtered_index_at(filtered_index);
        SDL_Rect item = {list.x + 4, list.y + 4 + row * CASE_MGMT_SOURCE_ROW_H, list.w - 8, CASE_MGMT_SOURCE_ROW_H - 3};
        if (source_index < 0 || source_index >= Global_Case_Source_File_Count) break;

        if (source_index == Global_Case_Source_Hover) {
            draw_filled_rect(renderer, item, Case_Panel_2);
            SDL_Rect halo = {item.x - 2, item.y - 2, item.w + 4, item.h + 4};
            draw_outline_rect(renderer, halo, Case_Border_Hi);
        }

        draw_outline_rect(renderer, item, source_index == Global_Case_Source_Hover ? Case_Border_Hi : Case_Border);
        draw_text(renderer, font, Global_Case_Source_Files[source_index], item.x + 10, item.y + 7, Case_Text);
    }

    char count_label[128];
    if (Global_Case_Source_Search[0]) {
        snprintf(count_label, sizeof(count_label), "%d of %d source files", filtered_count, Global_Case_Source_File_Count);
    }
    else {
        snprintf(count_label, sizeof(count_label), "%d source files", Global_Case_Source_File_Count);
    }
    draw_text(renderer, font, count_label, popup.x + 18, popup.y + popup.h - 24, Case_Muted);
}

static void case_draw_description_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h){
    if (!Global_Case_Description_Popup_Open) return;

    SDL_Rect popup = case_description_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect text_rect = {popup.x + 18, popup.y + 58, popup.w - 36, popup.h - 88};
    char *src = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int starts[128];
    int ends[128];
    int line_h = 19;
    int max_lines = (text_rect.h - 12) / line_h;
    int line_count = case_description_build_lines(src, starts, ends);
    int mx = 0;
    int my = 0;
    case_get_adjusted_mouse_state(&mx, &my);

    Global_Case_Description_Font = font;
    Global_Case_Description_Wrap_Px = text_rect.w - 18;

    if (max_lines < 1) max_lines = 1;
    case_clamp_description_scroll(text_rect);

    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, Case_Border_Hi);
    SDL_Rect inner = {popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8};
    draw_outline_rect(renderer, inner, Case_Border);

    draw_text(renderer, font, "DESCRIPTION", popup.x + 18, popup.y + 20, Case_Text);
    case_draw_button(renderer,
                     font,
                     close_btn,
                     "Close",
                     0,
                     case_point_in_rect(mx, my, close_btn),
                     0);

    draw_filled_rect(renderer, text_rect, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer,
                      text_rect,
                      Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION ? Case_Border_Hi : Case_Border);

    case_draw_description_selection(renderer,
                                    font,
                                    text_rect,
                                    src ? src : "",
                                    starts,
                                    ends,
                                    line_count,
                                    Global_Case_Description_Popup_Scroll,
                                    max_lines);

    int y = text_rect.y + 7;
    for (int line = Global_Case_Description_Popup_Scroll;
         line < line_count && line < Global_Case_Description_Popup_Scroll + max_lines;
         line++) {
        char buf[CASE_MGMT_DESCRIPTION_MAX + 8];
        int n = ends[line] - starts[line];
        if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
        if (n < 0) n = 0;
        memcpy(buf, (src ? src : "") + starts[line], (size_t)n);
        buf[n] = '\0';
        if (buf[0] == '\0' && line_count == 1) {
            draw_text(renderer, font, "Click here to type the block description.", text_rect.x + 9, y, Case_Muted);
        }
        else {
            draw_text(renderer, font, buf, text_rect.x + 9, y, Case_Text);
        }
        y += line_h;
    }

    if (Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION &&
        ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {
        int cursor = Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
        int src_len = src ? (int)strlen(src) : 0;
        int cursor_line = 0;
        if (cursor < 0) cursor = 0;
        if (cursor > src_len) cursor = src_len;

        for (int i = 0; i < line_count; i++) {
            if (cursor >= starts[i] && cursor <= ends[i]) {
                cursor_line = i;
                break;
            }
            if (i == line_count - 1 && cursor > ends[i]) cursor_line = i;
        }

        if (cursor_line >= Global_Case_Description_Popup_Scroll &&
            cursor_line < Global_Case_Description_Popup_Scroll + max_lines) {
            int line_start = starts[cursor_line];
            int line_end = ends[cursor_line];
            int text_w = 0;
            int text_h = 0;
            if (cursor < line_start) cursor = line_start;
            if (cursor > line_end) cursor = line_end;
            if (cursor > line_start) {
                char before[CASE_MGMT_DESCRIPTION_MAX + 8];
                int before_len = cursor - line_start;
                if (before_len >= (int)sizeof(before)) before_len = (int)sizeof(before) - 1;
                memcpy(before, (src ? src : "") + line_start, (size_t)before_len);
                before[before_len] = '\0';
                if (TTF_SizeText(font, before, &text_w, &text_h) != 0) text_w = before_len * 8;
            }
            int cx = text_rect.x + 9 + text_w;
            int cy0 = text_rect.y + 7 + ((cursor_line - Global_Case_Description_Popup_Scroll) * line_h);
            int cy1 = cy0 + line_h - 2;
            if (cx < text_rect.x + 9) cx = text_rect.x + 9;
            if (cx > text_rect.x + text_rect.w - 9) cx = text_rect.x + text_rect.w - 9;
            SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
            SDL_RenderDrawLine(renderer, cx, cy0, cx, cy1);
            SDL_RenderDrawLine(renderer, cx + 1, cy0, cx + 1, cy1);
        }
    }

    if (line_count > max_lines) {
        char scroll_label[96];
        snprintf(scroll_label,
                 sizeof(scroll_label),
                 "Lines %d-%d of %d | drag selects | Ctrl+V pastes",
                 Global_Case_Description_Popup_Scroll + 1,
                 Global_Case_Description_Popup_Scroll + max_lines < line_count ?
                    Global_Case_Description_Popup_Scroll + max_lines : line_count,
                 line_count);
        draw_text(renderer, font, scroll_label, popup.x + 18, popup.y + popup.h - 24, Case_Muted);
    }
}



static void case_draw_case_dropdown(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect field){
    if (!Global_Case_Case_Dropdown_Open) return;

    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count = case_build_case_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
    if (count <= 0) return;

    case_clamp_case_scroll();
    int visible = count - Global_Case_Case_Scroll;
    if (visible > CASE_MGMT_CASE_MAX_VISIBLE) visible = CASE_MGMT_CASE_MAX_VISIBLE;
    if (visible <= 0) return;

    int mx = 0;
    int my = 0;
    case_get_adjusted_mouse_state(&mx, &my);

    SDL_Rect menu = case_case_dropdown_rect(field, visible);
    draw_filled_rect(renderer, menu, (SDL_Color){0, 0, 0, 245});
    draw_outline_rect(renderer, menu, Case_Border_Hi);

    Global_Case_Case_Hover = -1;
    for (int i = 0; i < visible; i++) {
        int pos = Global_Case_Case_Scroll + i;
        int option_index = matches[pos];
        SDL_Rect row = {menu.x, menu.y + i * CASE_MGMT_CASE_OPTION_H, menu.w, CASE_MGMT_CASE_OPTION_H};
        int hovered = case_point_in_rect(mx, my, row);
        if (hovered) Global_Case_Case_Hover = option_index;
        draw_filled_rect(renderer, row, hovered ? (SDL_Color){0, 70, 30, 250} : (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer, row, hovered ? Case_Border_Hi : Case_Border);
        char short_text[128];
        case_shorten(Global_Case_Case_Options[option_index], short_text, sizeof(short_text), 34);
        draw_text(renderer, font, short_text, row.x + 9, row.y + 6, hovered ? Case_Text : Case_Muted);
    }
}


static void case_draw_country_dropdown(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect field){
    if (!Global_Case_Country_Dropdown_Open) return;

    int matches[512];
    int count = case_build_country_matches(matches, (int)(sizeof(matches) / sizeof(matches[0])));
    if (count <= 0) return;

    case_clamp_country_scroll();
    int visible = count - Global_Case_Country_Scroll;
    if (visible > CASE_MGMT_COUNTRY_MAX_VISIBLE) visible = CASE_MGMT_COUNTRY_MAX_VISIBLE;
    if (visible <= 0) return;

    int mx = 0;
    int my = 0;
    case_get_adjusted_mouse_state(&mx, &my);

    SDL_Rect menu = case_country_dropdown_rect(field, visible);
    draw_filled_rect(renderer, menu, (SDL_Color){0, 0, 0, 245});
    draw_outline_rect(renderer, menu, Case_Border_Hi);

    Global_Case_Country_Hover = -1;
    for (int i = 0; i < visible; i++) {
        int pos = Global_Case_Country_Scroll + i;
        int option_index = matches[pos];
        SDL_Rect row = {menu.x, menu.y + i * CASE_MGMT_COUNTRY_OPTION_H, menu.w, CASE_MGMT_COUNTRY_OPTION_H};
        int hovered = case_point_in_rect(mx, my, row);
        if (hovered) Global_Case_Country_Hover = option_index;
        draw_filled_rect(renderer, row, hovered ? (SDL_Color){0, 70, 30, 250} : (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer, row, hovered ? Case_Border_Hi : Case_Border);
        char short_text[128];
        case_shorten(CASE_MGMT_COUNTRIES[option_index].name, short_text, sizeof(short_text), 34);
        draw_text(renderer, font, short_text, row.x + 9, row.y + 6, hovered ? Case_Text : Case_Muted);
    }
}


static void case_draw_editor(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect editor){
    SDL_Rect fields[CASE_MGMT_FIELD_COUNT];
    SDL_Rect duplicate_btn;
    SDL_Rect delete_btn;
    const char *labels[CASE_MGMT_FIELD_COUNT] = {
        "Case #",
        "Country",
        "Task Assigned",
        "Assigned User",
        "Start Date (MM/DD/YYYY)",
        "End Date (MM/DD/YYYY)",
        "Status",
        "Priority (1 highest)",
        "Source File",
        "Description"
    };
    int mx = 0;
    int my = 0;
    case_get_adjusted_mouse_state(&mx, &my);

    draw_filled_rect(renderer, editor, Case_Panel);
    draw_outline_rect(renderer, editor, Case_Border);
    draw_text(renderer, font, "BLOCK EDITOR", editor.x + 16, editor.y + 16, Case_Text);

    if (Global_Case_Selected < 0 || Global_Case_Selected >= Global_Case_Block_Count) {
        draw_text(renderer, font, "Select a block to edit its task, user, timeline, source, description, and status.",
                  editor.x + 16, editor.y + 54, Case_Muted);
        return;
    }

    char selected_label[64];
    snprintf(selected_label,
             sizeof(selected_label),
             "Selected Block: %03d",
             Global_Case_Blocks[Global_Case_Selected].id);
    draw_text(renderer, font, selected_label, editor.x + 16, editor.y + 56, Case_Muted);
    int selected_count = case_selected_block_count();
    if (selected_count > 1) {
        char group_label[64];
        snprintf(group_label, sizeof(group_label), "Group Selected: %d blocks", selected_count);
        draw_text(renderer, font, group_label, editor.x + 16, editor.y + 76, Case_Warn);
    }

    case_editor_field_rects(editor, fields, &duplicate_btn, &delete_btn);

    for (int i = 0; i < CASE_MGMT_FIELD_COUNT; i++) {
        char *text = case_selected_field_text(i);
        int active = Global_Case_Active_Field == i ||
                     (i == CASE_MGMT_FIELD_CASE_NUMBER && Global_Case_Case_Dropdown_Open) ||
                     (i == CASE_MGMT_FIELD_COUNTRY && Global_Case_Country_Dropdown_Open) ||
                     (i == CASE_MGMT_FIELD_STATUS && Global_Case_Status_Dropdown_Open) ||
                     (i == CASE_MGMT_FIELD_SOURCE_FILE && Global_Case_Source_Popup_Open) ||
                     ((i == CASE_MGMT_FIELD_START_DATE || i == CASE_MGMT_FIELD_END_DATE) &&
                      Global_Case_Calendar_Open && Global_Case_Calendar_Field == i);

        if (i == CASE_MGMT_FIELD_DESCRIPTION) {
            SDL_Rect open_btn = case_description_open_button_rect(fields[i]);
            case_draw_description_box(renderer,
                                      font,
                                      fields[i],
                                      labels[i],
                                      text ? text : "",
                                      active);
            case_draw_button(renderer,
                             font,
                             open_btn,
                             "Expand",
                             Global_Case_Description_Popup_Open,
                             case_point_in_rect(mx, my, open_btn),
                             0);
            continue;
        }

        case_draw_input(renderer,
                        font,
                        fields[i],
                        labels[i],
                        text ? text : "",
                        active,
                        Global_Case_Field_Cursor[i],
                        i == CASE_MGMT_FIELD_STATUS || i == CASE_MGMT_FIELD_SOURCE_FILE);

        if (i == CASE_MGMT_FIELD_STATUS) {
            SDL_Rect swatch = {fields[i].x + fields[i].w - 48, fields[i].y + 8, 18, 14};
            draw_filled_rect(renderer, swatch, case_status_color(text));
            draw_outline_rect(renderer, swatch, case_status_color(text));
        }
    }

    case_draw_button(renderer,
                     font,
                     duplicate_btn,
                     "Duplicate",
                     0,
                     case_point_in_rect(mx, my, duplicate_btn),
                     0);

    case_draw_button(renderer,
                     font,
                     delete_btn,
                     "Delete",
                     0,
                     case_point_in_rect(mx, my, delete_btn),
                     1);

    if (Global_Case_Calendar_Open &&
        (Global_Case_Calendar_Field == CASE_MGMT_FIELD_START_DATE ||
         Global_Case_Calendar_Field == CASE_MGMT_FIELD_END_DATE)) {
        case_draw_calendar(renderer, font, fields[Global_Case_Calendar_Field]);
    }

    case_draw_case_dropdown(renderer, font, fields[CASE_MGMT_FIELD_CASE_NUMBER]);
    case_draw_country_dropdown(renderer, font, fields[CASE_MGMT_FIELD_COUNTRY]);
    case_draw_status_dropdown(renderer, font, fields[CASE_MGMT_FIELD_STATUS]);
}

void CASE_MANAGEMENT_draw_workstation(SDL_Renderer *renderer,
                                      TTF_Font *font,
                                      int win_w,
                                      int win_h){
    SDL_Rect canvas;
    SDL_Rect editor;
    SDL_Rect new_btn;
    SDL_Rect link_btn;
    SDL_Rect save_btn;
    SDL_Rect load_btn;
    Uint64 now = SDL_GetTicks64();
    int mx = 0;
    int my = 0;

    if (!renderer || !Global_CaseManagement_Mode) return;

    canvas = case_canvas_rect(win_w, win_h);
    editor = case_editor_rect(win_w, win_h);
    case_ensure_view(canvas);
    case_toolbar_rects(win_w, &new_btn, &link_btn, &save_btn, &load_btn);
    case_get_adjusted_mouse_state(&mx, &my);

    SDL_SetRenderDrawColor(renderer, Case_BG.r, Case_BG.g, Case_BG.b, Case_BG.a);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0, 50, 20, 120);
    for (int x = 0; x < win_w; x += 48) SDL_RenderDrawLine(renderer, x, 0, x, win_h);
    for (int y = 0; y < win_h; y += 48) SDL_RenderDrawLine(renderer, 0, y, win_w, y);

    case_draw_button(renderer,
                     font,
                     new_btn,
                     "+ Block",
                     0,
                     case_point_in_rect(mx, my, new_btn),
                     0);
    case_draw_button(renderer,
                     font,
                     link_btn,
                     Global_Case_Link_Mode ? "Link: ON" : "Link: OFF",
                     Global_Case_Link_Mode,
                     case_point_in_rect(mx, my, link_btn),
                     0);
    case_draw_button(renderer,
                     font,
                     save_btn,
                     "Save",
                     0,
                     case_point_in_rect(mx, my, save_btn),
                     0);
    case_draw_button(renderer,
                     font,
                     load_btn,
                     "Load",
                     0,
                     case_point_in_rect(mx, my, load_btn),
                     0);

    draw_filled_rect(renderer, canvas, (SDL_Color){0, 5, 2, 235});
    draw_outline_rect(renderer, canvas, Case_Border);
    case_draw_grid(renderer, canvas);

    char zoom_label[64];
    snprintf(zoom_label, sizeof(zoom_label), "Zoom: %.0f%%", Global_Case_Zoom * 100.0);
    draw_text(renderer, font, zoom_label, canvas.x + canvas.w - 104, canvas.y + 12, Case_Muted);

    SDL_RenderSetClipRect(renderer, &canvas);

    for (int i = 0; i < Global_Case_Link_Count; i++) {
        int from_index = case_find_block_index_by_id(Global_Case_Links[i].from_id);
        int to_index = case_find_block_index_by_id(Global_Case_Links[i].to_id);
        if (from_index >= 0 && to_index >= 0) {
            int related = case_is_block_selected(from_index) || case_is_block_selected(to_index) ||
                          from_index == Global_Case_Selected || to_index == Global_Case_Selected;
            case_draw_link(renderer, canvas, i, related);
        }
    }

    for (int i = 0; i < Global_Case_Block_Count; i++) {
        case_draw_block(renderer, font, canvas, i);
    }

    case_draw_link_preview(renderer, canvas);
    case_draw_selection_box(renderer);

    SDL_RenderSetClipRect(renderer, NULL);

    if (editor.x < win_w) {
        case_draw_editor(renderer, font, editor);
    }
    else {
        draw_text(renderer,
                  font,
                  "Widen the window to show the block editor.",
                  canvas.x + 16,
                  canvas.y + canvas.h - 28,
                  Case_Warn);
    }

    case_draw_source_popup(renderer, font, win_w, win_h);
    case_draw_description_popup(renderer, font, win_w, win_h);

    if (Global_Case_Link_Mode) {
        const char *hint = Global_Case_Link_Source >= 0 ?
            "LINK MODE: source selected, click destination block" :
            "LINK MODE: click source block";
        draw_text(renderer, font, hint, canvas.x + 14, canvas.y + 12, Case_Warn);
    }

    if (Global_Case_Status[0] != '\0') {
        SDL_Color status_color = (now - Global_Case_Status_Time < 2500) ? Case_Text : Case_Muted;
        draw_text(renderer, font, Global_Case_Status, CASE_MGMT_MARGIN + 8, win_h - 32, status_color);
    }
}
