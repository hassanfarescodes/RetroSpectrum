#define _GNU_SOURCE

/*
 * ============================================================================
 * File:            CaseManagementWorkstation.c
 * Author:          Hassan Fares
 *
 * Description:     Case management graph workstation logic for RetroSpectrum
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux
 *
 *                                                               05/04/2026
 * ============================================================================
 */

#include "CaseManagementWorkstation.h"
#include "AuthScreen.h"
#include "DataStore.h"
#include "GUIs.h"

#include <SDL2/SDL_image.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef SDL_MAJOR_VERSION
extern char *SDL_GetClipboardText(void);
extern void SDL_free(void *mem);
#endif

#define CASE_MGMT_MAX_BLOCKS 128
#define CASE_MGMT_MAX_LINKS 256
#define CASE_MGMT_TEXT_MAX 192
#define CASE_MGMT_DESCRIPTION_MAX 1024
#define CASE_MGMT_SOURCE_FILE_MAX 512
#define CASE_MGMT_DATE_MAX 16
#define CASE_MGMT_BLOCK_W 270
#define CASE_MGMT_BLOCK_H 126
#define CASE_MGMT_CONNECTOR_SIZE 10
#define CASE_MGMT_FIELD_COUNT 10
#define CASE_MGMT_FIELD_NONE -1
#define CASE_MGMT_MARGIN 20
#define CASE_MGMT_TOOLBAR_H 46
#define CASE_MGMT_EDITOR_W 340
#define CASE_MGMT_STATUS_COUNT 5
#define CASE_MGMT_STATUS_OPTION_H 28
#define CASE_MGMT_CALENDAR_H 250
#define CASE_MGMT_SOURCE_MAX_FILES 512
#define CASE_MGMT_SOURCE_VISIBLE 14
#define CASE_MGMT_CASE_MAX_VISIBLE 6
#define CASE_MGMT_CASE_OPTION_H 30
#define CASE_MGMT_COUNTRY_MAX_VISIBLE 6
#define CASE_MGMT_COUNTRY_OPTION_H 30
#define CASE_MGMT_USER_MAX_VISIBLE 7
#define CASE_MGMT_USER_OPTION_H 32
#define CASE_MGMT_CLASSIFICATION_DIR "Classification"
#define CASE_MGMT_SOURCE_ROW_H 30
#define CASE_MGMT_FILE_SEARCH_TEXT_MAX 256
#define CASE_MGMT_FILE_SEARCH_ROW_H 34
#define CASE_MGMT_FLAG_CACHE_MAX 512
#define CASE_MGMT_UNDO_DEPTH 32
#define CASE_MGMT_MIN_ZOOM 0.30
#define CASE_MGMT_MAX_ZOOM 1.60
#define CASE_MGMT_METADATA_MAX_CASES 512
#define CASE_MGMT_METADATA_DESCRIPTION_MAX 512
#define CASE_MGMT_METADATA_PREFIX "__case_metadata_"
#define CASE_MGMT_IMAGE_KIND "case_image"
#define CASE_MGMT_IMAGE_PREFIX "__case_image_"
#define CASE_MGMT_COLOR_KIND "case_color"
#define CASE_MGMT_COLOR_PREFIX "__case_color_"
#define CASE_MGMT_METADATA_VISIBLE_ROWS 4

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
    int type;
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

enum { CASE_MGMT_BLOCK_TASK = 0, CASE_MGMT_BLOCK_CASE = 1 };

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

enum { CASE_MGMT_SIDE_LEFT = 0, CASE_MGMT_SIDE_RIGHT = 1, CASE_MGMT_SIDE_TOP = 2, CASE_MGMT_SIDE_BOTTOM = 3 };

static Type_Case_Block Global_Case_Blocks[CASE_MGMT_MAX_BLOCKS];
static Type_Case_Link Global_Case_Links[CASE_MGMT_MAX_LINKS];
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
static Type_Auth_User_Summary Global_Case_User_Options[CASE_MGMT_SOURCE_MAX_FILES];
static int Global_Case_User_Count = 0;
static int Global_Case_User_Scroll = 0;
static int Global_Case_User_Hover = -1;
static int Global_Case_User_Dropdown_Open = 0;
static int Global_Case_User_Keyboard_Pos = -1;
static char Global_Case_Record_Dir[512] = "Recordings";
static char Global_Case_File_Name[CASE_MGMT_FILE_SEARCH_TEXT_MAX] = "case_graph.case.csv";
static char Global_Case_Loaded_Database_Record[CASE_MGMT_FILE_SEARCH_TEXT_MAX] = "";
static int Global_Case_File_Name_Cursor = 19;
static int Global_Case_File_Name_Active = 0;
static int Global_Case_File_Search_Open = 0;
static int Global_Case_File_Search_Active = 0;
static int Global_Case_File_Search_Cursor = 0;
static int Global_Case_File_Search_Scroll = 0;
static int Global_Case_File_Search_Hover = -1;
static int Global_Case_File_Search_Count = 0;
static char Global_Case_File_Search_Text[CASE_MGMT_FILE_SEARCH_TEXT_MAX] = "";
static char Global_Case_File_Search_Files[CASE_MGMT_SOURCE_MAX_FILES][CASE_MGMT_SOURCE_FILE_MAX];
static SDL_Texture *Global_Case_Country_Flag_Textures[CASE_MGMT_FLAG_CACHE_MAX];
static int Global_Case_Country_Flag_Attempted[CASE_MGMT_FLAG_CACHE_MAX];
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
static char Global_Case_Status[256] = "N: task | C: case | Drag connector: link | CTRL+Z: undo | CTRL+S: save | "
                                      "CTRL+O: load";
static Uint64 Global_Case_Status_Time = 0;

typedef struct Type_Case_Metadata_Info {
    char case_number[128];
    char description[CASE_MGMT_METADATA_DESCRIPTION_MAX];
    char document_name[256];
    int classification_count;
    int graph_count;
} Type_Case_Metadata_Info;

static Type_Case_Metadata_Info Global_Case_Metadata[CASE_MGMT_METADATA_MAX_CASES];
static int Global_Case_Metadata_Count = 0;
static int Global_Case_Metadata_Selected = -1;
static int Global_Case_Metadata_Scroll = 0;
static char Global_Case_Metadata_Search[128] = "";
static int Global_Case_Metadata_Search_Cursor = 0;
static int Global_Case_Metadata_Search_Active = 0;
static char Global_Case_Metadata_Edit_Name[128] = "";
static int Global_Case_Metadata_Name_Cursor = 0;
static int Global_Case_Metadata_Name_Active = 0;
static char Global_Case_Metadata_Edit_Description[CASE_MGMT_METADATA_DESCRIPTION_MAX] = "";
static int Global_Case_Metadata_Description_Cursor = 0;
static int Global_Case_Metadata_Description_Active = 0;
static int Global_Case_Metadata_Creating = 0;
static int Global_Case_Metadata_Renaming = 0;
static int Global_Case_Metadata_Delete_Confirm_Open = 0;
static char Global_Case_Metadata_Delete_Case[128] = "";
static int Global_Case_Database_Delete_Confirm_Open = 0;
static SDL_Rect Global_Case_Database_Confirm_Delete_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Database_Confirm_Cancel_Rect = {0, 0, 0, 0};
static Uint64 Global_Case_Metadata_Last_Refresh = 0;
static SDL_Rect Global_Case_Metadata_Search_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_List_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Create_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Name_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Description_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Save_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Rename_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Delete_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Cancel_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Confirm_Delete_Rect = {0, 0, 0, 0};
static SDL_Rect Global_Case_Metadata_Confirm_Cancel_Rect = {0, 0, 0, 0};

typedef struct Type_Case_Undo_State {
    Type_Case_Block blocks[CASE_MGMT_MAX_BLOCKS];
    Type_Case_Link links[CASE_MGMT_MAX_LINKS];
    int selected_blocks[CASE_MGMT_MAX_BLOCKS];
    int block_count;
    int link_count;
    int selected;
    int selected_link;
    int next_id;
    double zoom;
    double view_x;
    double view_y;
} Type_Case_Undo_State;

static Type_Case_Undo_State Global_Case_Undo_Stack[CASE_MGMT_UNDO_DEPTH];
static int Global_Case_Undo_Count = 0;
static int Global_Case_Drag_Undo_Pushed = 0;

static SDL_Color Case_BG = {0, 0, 0, 255};
static SDL_Color Case_Panel = {0, 10, 4, 245};
static SDL_Color Case_Panel_2 = {0, 18, 8, 255};
static SDL_Color Case_Border = {0, 150, 60, 255};
static SDL_Color Case_Border_Hi = {0, 255, 90, 255};
static SDL_Color Case_Text = {0, 255, 90, 255};
static SDL_Color Case_Muted = {0, 155, 65, 255};
static SDL_Color Case_Warn = {255, 180, 40, 255};
static SDL_Color Case_Red = {255, 75, 55, 255};
static SDL_Color Case_Blue = {70, 190, 255, 255};

static void case_copy_text(char *dst, size_t dst_size, const char *src);
static int case_description_delete_selection(void);
static void case_add_block_typed(SDL_Rect canvas, int block_type);
static void case_open_file_search_menu(void);
static int case_handle_file_search_event(const SDL_Event *event, int win_w, int win_h);
static void case_draw_file_search_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static void case_scan_case_graph_files(void);
static int case_selected_block_is_case(void);
static int case_text_equals_ci(const char *a, const char *b);
static void case_trim_text(char *text);
static int case_should_show_field(int field);
static int case_rect_is_valid(SDL_Rect r);
static int case_load_current_file(void);
static void case_delete_loaded_database_record(void);
static int case_handle_database_delete_confirmation(const SDL_Event *event);
static void case_draw_database_delete_confirmation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);
static void case_description_clear_selection(void);
static void case_clear_block_selection(void);
static void case_select_only_block(int index);
static void case_sync_primary_selection(void);
static int case_selected_block_count(void);
static int case_is_block_selected(int index);
static void case_push_undo_state(void);
static int case_undo_last_change(void);
static char *case_selected_field_text(int field);
static void case_metadata_refresh(void);
static void case_draw_case_browser(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect editor);
static int case_metadata_handle_event(const SDL_Event *event, SDL_Rect editor, int win_w, int win_h);
static void case_metadata_draw_delete_confirmation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h);

static const char *CASE_MGMT_STATUS_OPTIONS[CASE_MGMT_STATUS_COUNT] = {"Todo", "In Progress", "Review", "Done",
                                                                       "Blocked"};

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

static int case_country_count(void) {
    /*
        Purpose: Counts the country
        Returns: Item count
    */

    return (int)(sizeof(CASE_MGMT_COUNTRIES) / sizeof(CASE_MGMT_COUNTRIES[0]));
}

static int case_country_index_by_name(const char *name) {
    /*
        Purpose: Gets the country index by name
        Returns: Item index
    */

    if (!name || !name[0]) {

        return -1;

    }
    for (int i = 0; i < case_country_count(); i++) {

        if (case_text_equals_ci(CASE_MGMT_COUNTRIES[i].name, name)) {

            return i;

        }
    }
    return -1;
}

static SDL_Texture *case_country_flag_texture(SDL_Renderer *renderer, int country_index) {
    /*
        Purpose: Gets the country flag texture
        Returns: Texture pointer
    */

    char path[256];

    if (!renderer) {

        return NULL;

    }

    if (country_index < 0 || country_index >= case_country_count()) {

        return NULL;

    }

    if (country_index >= CASE_MGMT_FLAG_CACHE_MAX) {

        return NULL;

    }

    if (!Global_Case_Country_Flag_Attempted[country_index]) {

        Global_Case_Country_Flag_Attempted[country_index] = 1;
        snprintf(path, sizeof(path), "assets/flags/%s.png", CASE_MGMT_COUNTRIES[country_index].alpha2);
        Global_Case_Country_Flag_Textures[country_index] = IMG_LoadTexture(renderer, path);

    }

    return Global_Case_Country_Flag_Textures[country_index];
}

static void case_get_adjusted_mouse_state(int *x, int *y) {
    /*
        Purpose: Gets the adjusted mouse state
        Returns: No value
    */

    SDL_GetMouseState(x, y);

    if (y) {

        *y -= RETROSPECTRUM_DASHBOARD_TAB_BAR_H;

    }
}

static int case_is_complex16_file(const char *name) {
    /*
        Purpose: Checks whether the complex16 is file
        Returns: Boolean status
    */

    size_t len;
    const char *suffix = ".complex16";
    size_t suffix_len = strlen(suffix);

    if (!name) {

        return 0;

    }
    len = strlen(name);

    if (len < suffix_len) {

        return 0;

    }
    return strcmp(name + len - suffix_len, suffix) == 0;
}

static int case_name_compare(const void *a, const void *b) {
    /*
        Purpose: Compares the name
        Returns: Sort order
    */

    const char *sa = (const char *)a;
    const char *sb = (const char *)b;
    return strcmp(sa, sb);
}

static void case_scan_source_files(void) {
    /*
        Purpose: Scans the source files
        Returns: No value
    */

    DIR *dir = opendir(Global_Case_Record_Dir);
    struct dirent *entry;

    Global_Case_Source_File_Count = 0;
    Global_Case_Source_Scroll = 0;
    Global_Case_Source_Hover = -1;

    if (!dir) {

        return;

    }

    while ((entry = readdir(dir)) != NULL && Global_Case_Source_File_Count < CASE_MGMT_SOURCE_MAX_FILES) {

        if (entry->d_name[0] == '.') {

            continue;

        }

        if (!case_is_complex16_file(entry->d_name)) {

            continue;

        }

        case_copy_text(Global_Case_Source_Files[Global_Case_Source_File_Count],
                       sizeof(Global_Case_Source_Files[Global_Case_Source_File_Count]), entry->d_name);
        Global_Case_Source_File_Count++;
    }

    closedir(dir);

    if (Global_Case_Source_File_Count > 1) {

        qsort(Global_Case_Source_Files, (size_t)Global_Case_Source_File_Count, sizeof(Global_Case_Source_Files[0]),
              case_name_compare);

    }
}

static int case_point_in_rect(int x, int y, SDL_Rect r) {
    /*
        Purpose: Checks whether a point lies inside a rectangle
        Returns: Boolean status
    */

    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void case_copy_text(char *dst, size_t dst_size, const char *src) {
    /*
        Purpose: Copies the text
        Returns: No value
    */

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src) {

        src = "";

    }
    snprintf(dst, dst_size, "%s", src);
}

static double case_limit_double(double value, double low, double high) {
    /*
        Purpose: Limits the double
        Returns: Computed value
    */

    if (value < low) {

        return low;

    }

    if (value > high) {

        return high;

    }
    return value;
}

static void case_set_status(const char *status, SDL_Color color) {
    /*
        Purpose: Sets the status
        Returns: No value
    */

    (void)color;
    case_copy_text(Global_Case_Status, sizeof(Global_Case_Status), status);
    Global_Case_Status_Time = SDL_GetTicks64();
}

static void case_draw_text_centered(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect,
                                    SDL_Color color) {
    /*
        Purpose: Draws the text centered
        Returns: No value
    */

    int tw = 0;
    int th = 0;

    if (!font || !text) {

        return;

    }

    if (TTF_SizeText(font, text, &tw, &th) != 0) {

        tw = 0;
        th = 0;

    }

    draw_text(renderer, font, text, rect.x + (rect.w - tw) / 2, rect.y + (rect.h - th) / 2, color);
}

static void case_draw_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, int active,
                             int hovered, int danger) {
    /*
        Purpose: Draws the button
        Returns: No value
    */

    int hot = active || hovered;
    SDL_Color fill = hot ? (SDL_Color){0, 32, 13, 255} : Case_Panel;
    SDL_Color border = danger ? Case_Red : (hot ? Case_Border_Hi : Case_Border);
    SDL_Color text = danger ? Case_Red : (hot ? Case_Text : Case_Muted);

    if (hot) {

        SDL_Rect halo_outer = {rect.x - 7, rect.y - 7, rect.w + 14, rect.h + 14};
        SDL_Rect halo_mid = {rect.x - 5, rect.y - 5, rect.w + 10, rect.h + 10};
        SDL_Rect halo_inner = {rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6};
        draw_outline_rect(renderer, halo_outer, (SDL_Color){0, 60, 24, 255});
        draw_outline_rect(renderer, halo_mid, (SDL_Color){0, 120, 48, 255});
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

static void case_shorten(const char *src, char *dst, size_t dst_size, size_t max_chars) {
    /*
        Purpose: Shortens the requested operation
        Returns: No value
    */

    size_t len = 0;

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src) {

        src = "";

    }

    while (len < max_chars && len + 1 < dst_size && src[len]) {
        dst[len] = src[len];
        len++;
    }

    if (len + 4 < dst_size && max_chars > 3 && src[len]) {

        dst[len++] = '.';
        dst[len++] = '.';
        dst[len++] = '.';

    }

    dst[len] = '\0';
}

static SDL_Rect case_block_screen_rect(int index, SDL_Rect canvas);
static int case_find_block_index_by_id(int id) {
    /*
        Purpose: Finds the block index by ID
        Returns: Item index
    */

    for (int i = 0; i < Global_Case_Block_Count; i++) {

        if (Global_Case_Blocks[i].id == id) {

            return i;

        }
    }
    return -1;
}

static void case_clear_block_selection(void) {
    /*
        Purpose: Clears the block selection
        Returns: No value
    */

    memset(Global_Case_Selected_Blocks, 0, sizeof(Global_Case_Selected_Blocks));
    Global_Case_Selected = -1;
}

static int case_is_block_selected(int index) {
    /*
        Purpose: Checks whether the block is selected
        Returns: Boolean status
    */

    if (index < 0 || index >= Global_Case_Block_Count) {

        return 0;

    }
    return Global_Case_Selected_Blocks[index] != 0;
}

static int case_selected_block_count(void) {
    /*
        Purpose: Counts the selected block
        Returns: Item count
    */

    int count = 0;
    for (int i = 0; i < Global_Case_Block_Count; i++) {

        if (Global_Case_Selected_Blocks[i]) {

            count++;

        }
    }
    return count;
}

static void case_sync_primary_selection(void) {
    /*
        Purpose: Synchronizes the primary selection
        Returns: No value
    */

    if (Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count &&
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

static void case_select_only_block(int index) {
    /*
        Purpose: Selects the only block
        Returns: No value
    */

    case_clear_block_selection();

    if (index >= 0 && index < Global_Case_Block_Count) {

        Global_Case_Selected_Blocks[index] = 1;
        Global_Case_Selected = index;

    }
}

static void case_toggle_block_selection(int index) {
    /*
        Purpose: Toggles the block selection
        Returns: No value
    */

    if (index < 0 || index >= Global_Case_Block_Count) {

        return;

    }
    Global_Case_Selected_Blocks[index] = !Global_Case_Selected_Blocks[index];
    case_sync_primary_selection();
}

static int case_selected_block_is_case(void) {
    /*
        Purpose: Checks whether the selected block is case
        Returns: Success status
    */

    if (Global_Case_Selected < 0 || Global_Case_Selected >= Global_Case_Block_Count) {

        return 0;

    }
    return Global_Case_Blocks[Global_Case_Selected].type == CASE_MGMT_BLOCK_CASE;
}

static void case_push_undo_state(void) {
    /*
        Purpose: Pushes the undo state
        Returns: No value
    */

    Type_Case_Undo_State *state;

    if (Global_Case_Undo_Count >= CASE_MGMT_UNDO_DEPTH) {

        memmove(&Global_Case_Undo_Stack[0], &Global_Case_Undo_Stack[1],
                sizeof(Global_Case_Undo_Stack[0]) * (CASE_MGMT_UNDO_DEPTH - 1));
        Global_Case_Undo_Count = CASE_MGMT_UNDO_DEPTH - 1;

    }

    state = &Global_Case_Undo_Stack[Global_Case_Undo_Count++];
    memset(state, 0, sizeof(*state));
    memcpy(state->blocks, Global_Case_Blocks, sizeof(Global_Case_Blocks));
    memcpy(state->links, Global_Case_Links, sizeof(Global_Case_Links));
    memcpy(state->selected_blocks, Global_Case_Selected_Blocks, sizeof(Global_Case_Selected_Blocks));
    state->block_count = Global_Case_Block_Count;
    state->link_count = Global_Case_Link_Count;
    state->selected = Global_Case_Selected;
    state->selected_link = Global_Case_Selected_Link;
    state->next_id = Global_Case_Next_Id;
    state->zoom = Global_Case_Zoom;
    state->view_x = Global_Case_View_X;
    state->view_y = Global_Case_View_Y;
}

static int case_undo_last_change(void) {
    /*
        Purpose: Undoes the last change
        Returns: Success status
    */

    Type_Case_Undo_State state;

    if (Global_Case_Undo_Count <= 0) {

        case_set_status("Nothing to undo", Case_Warn);
        return 0;

    }

    state = Global_Case_Undo_Stack[--Global_Case_Undo_Count];
    memcpy(Global_Case_Blocks, state.blocks, sizeof(Global_Case_Blocks));
    memcpy(Global_Case_Links, state.links, sizeof(Global_Case_Links));
    memcpy(Global_Case_Selected_Blocks, state.selected_blocks, sizeof(Global_Case_Selected_Blocks));
    Global_Case_Block_Count = state.block_count;
    Global_Case_Link_Count = state.link_count;
    Global_Case_Selected = state.selected;
    Global_Case_Selected_Link = state.selected_link;
    Global_Case_Next_Id = state.next_id;
    Global_Case_Zoom = state.zoom;
    Global_Case_View_X = state.view_x;
    Global_Case_View_Y = state.view_y;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
    Global_Case_Link_Mode = 0;
    Global_Case_Link_Source = -1;
    Global_Case_Link_Dragging = 0;
    Global_Case_Dragging = 0;
    Global_Case_Box_Selecting = 0;
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Case_Dropdown_Open = 0;
    Global_Case_Country_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;
    Global_Case_Source_Popup_Open = 0;
    Global_Case_Description_Popup_Open = 0;
    case_description_clear_selection();
    case_set_status("Undo restored previous case graph state", Case_Text);
    return 1;
}

static int case_should_show_field(int field) {
    /*
        Purpose: Determines whether the show should show field
        Returns: Boolean status
    */

    if (Global_Case_Selected < 0 || Global_Case_Selected >= Global_Case_Block_Count) {

        return 1;

    }

    if (case_selected_block_is_case()) {

        return field == CASE_MGMT_FIELD_CASE_NUMBER || field == CASE_MGMT_FIELD_COUNTRY;

    }

    return field != CASE_MGMT_FIELD_CASE_NUMBER && field != CASE_MGMT_FIELD_COUNTRY;
}

static int case_rect_is_valid(SDL_Rect r) {
    /*
        Purpose: Checks whether the rectangle is valid
        Returns: Boolean status
    */

    return r.w > 0 && r.h > 0;
}

static int case_rects_intersect(SDL_Rect a, SDL_Rect b) {
    /*
        Purpose: Checks whether the rects intersects the target
        Returns: Success status
    */

    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

static SDL_Rect case_make_normalized_rect(int x0, int y0, int x1, int y1) {
    /*
        Purpose: Builds the normalized rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r;
    r.x = x0 < x1 ? x0 : x1;
    r.y = y0 < y1 ? y0 : y1;
    r.w = abs(x1 - x0);
    r.h = abs(y1 - y0);
    return r;
}

static void case_select_blocks_in_rect(SDL_Rect canvas, SDL_Rect selection, int additive) {
    /*
        Purpose: Selects the blocks in rectangle
        Returns: No value
    */

    int matched = 0;

    if (!additive) {

        case_clear_block_selection();

    }

    for (int i = 0; i < Global_Case_Block_Count; i++) {
        SDL_Rect br = case_block_screen_rect(i, canvas);

        if (case_rects_intersect(selection, br)) {

            Global_Case_Selected_Blocks[i] = 1;
            matched++;

        }
    }

    case_sync_primary_selection();
    Global_Case_Selected_Link = -1;

    if (matched > 1) {

        case_set_status("Selected multiple blocks", Case_Text);

    }

    else if (matched == 1) {

        case_set_status("Selected block", Case_Text);

    }

    else if (!additive) {

        case_set_status("Cleared block selection", Case_Muted);

    }
}

static SDL_Rect case_canvas_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the canvas rectangle
        Returns: Computed rectangle
    */

    SDL_Rect rect = {CASE_MGMT_MARGIN, CASE_MGMT_MARGIN + CASE_MGMT_TOOLBAR_H + 10,
                     win_w - CASE_MGMT_EDITOR_W - (CASE_MGMT_MARGIN * 3),
                     win_h - (CASE_MGMT_MARGIN * 3) - CASE_MGMT_TOOLBAR_H - 18};

    if (rect.w < 480) {

        rect.w = win_w - (CASE_MGMT_MARGIN * 2);

    }

    if (rect.h < 260) {

        rect.h = 260;

    }
    return rect;
}

static SDL_Rect case_editor_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the editor rectangle
        Returns: Computed rectangle
    */

    SDL_Rect rect = {win_w - CASE_MGMT_EDITOR_W - CASE_MGMT_MARGIN, CASE_MGMT_MARGIN + CASE_MGMT_TOOLBAR_H + 10,
                     CASE_MGMT_EDITOR_W, win_h - (CASE_MGMT_MARGIN * 3) - CASE_MGMT_TOOLBAR_H - 18};

    if (rect.x < CASE_MGMT_MARGIN + 480) {

        rect.x = win_w + 1000;

    }

    if (rect.h < 260) {

        rect.h = 260;

    }
    return rect;
}

static void case_ensure_view(SDL_Rect canvas) {
    /*
        Purpose: Ensures the view
        Returns: No value
    */

    if (Global_Case_View_Initialized) {

        return;

    }
    Global_Case_View_X = (double)canvas.x;
    Global_Case_View_Y = (double)canvas.y;
    Global_Case_View_Initialized = 1;
}

static int case_world_to_screen_x(SDL_Rect canvas, int world_x) {
    /*
        Purpose: Converts the world to the screen x
        Returns: Computed value
    */

    return canvas.x + (int)(((double)world_x - Global_Case_View_X) * Global_Case_Zoom);
}

static int case_world_to_screen_y(SDL_Rect canvas, int world_y) {
    /*
        Purpose: Converts the world to the screen y
        Returns: Computed value
    */

    return canvas.y + (int)(((double)world_y - Global_Case_View_Y) * Global_Case_Zoom);
}

static int case_screen_to_world_x(SDL_Rect canvas, int screen_x) {
    /*
        Purpose: Converts the screen to the world x
        Returns: Computed value
    */

    return (int)(Global_Case_View_X + ((double)screen_x - (double)canvas.x) / Global_Case_Zoom);
}

static int case_screen_to_world_y(SDL_Rect canvas, int screen_y) {
    /*
        Purpose: Converts the screen to the world y
        Returns: Computed value
    */

    return (int)(Global_Case_View_Y + ((double)screen_y - (double)canvas.y) / Global_Case_Zoom);
}

static SDL_Rect case_block_world_rect(int index) {
    /*
        Purpose: Computes the block world rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {Global_Case_Blocks[index].x, Global_Case_Blocks[index].y, CASE_MGMT_BLOCK_W, CASE_MGMT_BLOCK_H};
    return r;
}

static int case_block_at(int x, int y, SDL_Rect canvas);
static int case_link_at(int x, int y, SDL_Rect canvas);

static SDL_Rect case_block_screen_rect(int index, SDL_Rect canvas) {
    /*
        Purpose: Computes the block screen rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {case_world_to_screen_x(canvas, Global_Case_Blocks[index].x),
                  case_world_to_screen_y(canvas, Global_Case_Blocks[index].y),
                  (int)((double)CASE_MGMT_BLOCK_W * Global_Case_Zoom),
                  (int)((double)CASE_MGMT_BLOCK_H * Global_Case_Zoom)};

    if (r.w < 70) {

        r.w = 70;

    }

    if (r.h < 42) {

        r.h = 42;

    }
    return r;
}

static int case_connector_px(void) {
    /*
        Purpose: Gets the connector size in pixels
        Returns: Computed value
    */

    int connector = (int)((double)CASE_MGMT_CONNECTOR_SIZE * Global_Case_Zoom);

    if (connector < 6) {

        connector = 6;

    }

    if (connector > 14) {

        connector = 14;

    }
    return connector;
}

static SDL_Rect case_block_endpoint_rect(int index, SDL_Rect canvas, int side, int generous) {
    /*
        Purpose: Computes the block endpoint rectangle
        Returns: Computed rectangle
    */

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

static void case_endpoint_center(int index, SDL_Rect canvas, int side, int *x, int *y) {
    /*
        Purpose: Calculates the endpoint center
        Returns: No value
    */

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

    if (x) {

        *x = cx;

    }

    if (y) {

        *y = cy;

    }
}

static int case_endpoint_at(int x, int y, SDL_Rect canvas, int *side_out) {
    /*
        Purpose: Gets the endpoint at a position
        Returns: Success status
    */

    for (int i = Global_Case_Block_Count - 1; i >= 0; i--) {
        for (int side = 0; side < 4; side++) {
            SDL_Rect r = case_block_endpoint_rect(i, canvas, side, 1);

            if (case_point_in_rect(x, y, r)) {

                if (side_out) {

                    *side_out = side;

                }
                return i;

            }
        }
    }
    return -1;
}

static int case_nearest_endpoint(int x, int y, SDL_Rect canvas, int exclude_index, int *side_out) {
    /*
        Purpose: Finds the nearest block endpoint
        Returns: Success status
    */

    int best_index = -1;
    int best_side = -1;
    int best_dist2 = 42 * 42;

    for (int i = 0; i < Global_Case_Block_Count; i++) {

        if (i == exclude_index) {

            continue;

        }
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

    if (side_out) {

        *side_out = best_side;

    }
    return best_index;
}

static int case_block_at(int x, int y, SDL_Rect canvas) {
    /*
        Purpose: Gets the block at a position
        Returns: Success status
    */

    for (int i = Global_Case_Block_Count - 1; i >= 0; i--) {

        if (case_point_in_rect(x, y, case_block_screen_rect(i, canvas))) {

            return i;

        }
    }
    return -1;
}

static SDL_Color case_status_color(const char *status) {
    /*
        Purpose: Gets the status color
        Returns: Computed color
    */

    if (!status) {

        status = "";

    }

    if (strcmp(status, "Done") == 0) {

        return (SDL_Color){0, 255, 90, 255};

    }

    if (strcmp(status, "In Progress") == 0) {

        return Case_Blue;

    }

    if (strcmp(status, "Blocked") == 0) {

        return Case_Red;

    }

    if (strcmp(status, "Review") == 0) {

        return Case_Warn;

    }
    return Case_Muted;
}

static SDL_Color case_priority_color(const char *priority) {
    /*
        Purpose: Gets the priority color
        Returns: Computed color
    */

    int p = 0;

    if (priority && priority[0] >= '1' && priority[0] <= '5') {

        p = priority[0] - '0';

    }

    switch (p) {
    case 1:
        return Case_Red;
    case 2:
        return Case_Warn;
    case 3:
        return (SDL_Color){0, 255, 90, 255};
    case 4:
        return Case_Blue;
    case 5:
        return (SDL_Color){245, 245, 245, 255};
    default:
        return Case_Muted;
    }
}

static void case_make_timeline_text(const Type_Case_Block *b, char *out, size_t out_size) {
    /*
        Purpose: Builds the timeline text
        Returns: No value
    */

    if (!out || out_size == 0) {

        return;

    }

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

static void case_seed_default_blocks(void) {
    /*
        Purpose: Seeds the default blocks
        Returns: No value
    */

    if (Global_Case_Block_Count > 0) {

        return;

    }

    const int case_x = 344;
    const int case_y = 170;
    const int gap = 50;
    const int left_x = case_x - CASE_MGMT_BLOCK_W - gap;
    const int right_x = case_x + CASE_MGMT_BLOCK_W + gap;
    const int task_y = case_y;
    const int down_y = case_y + CASE_MGMT_BLOCK_H + gap;

    Type_Case_Block case_block = {0};
    case_block.id = Global_Case_Next_Id++;
    case_block.type = CASE_MGMT_BLOCK_CASE;
    case_block.x = case_x;
    case_block.y = case_y;
    Global_Case_Blocks[Global_Case_Block_Count++] = case_block;

    Type_Case_Block left_task = {0};
    left_task.id = Global_Case_Next_Id++;
    left_task.type = CASE_MGMT_BLOCK_TASK;
    left_task.x = left_x;
    left_task.y = task_y;
    Global_Case_Blocks[Global_Case_Block_Count++] = left_task;

    Type_Case_Block down_task = {0};
    down_task.id = Global_Case_Next_Id++;
    down_task.type = CASE_MGMT_BLOCK_TASK;
    down_task.x = case_x;
    down_task.y = down_y;
    Global_Case_Blocks[Global_Case_Block_Count++] = down_task;

    Type_Case_Block right_task = {0};
    right_task.id = Global_Case_Next_Id++;
    right_task.type = CASE_MGMT_BLOCK_TASK;
    right_task.x = right_x;
    right_task.y = task_y;
    Global_Case_Blocks[Global_Case_Block_Count++] = right_task;

    Global_Case_Links[Global_Case_Link_Count++] =
        (Type_Case_Link){case_block.id, left_task.id, CASE_MGMT_SIDE_LEFT, CASE_MGMT_SIDE_RIGHT};
    Global_Case_Links[Global_Case_Link_Count++] =
        (Type_Case_Link){case_block.id, down_task.id, CASE_MGMT_SIDE_BOTTOM, CASE_MGMT_SIDE_TOP};
    Global_Case_Links[Global_Case_Link_Count++] =
        (Type_Case_Link){case_block.id, right_task.id, CASE_MGMT_SIDE_RIGHT, CASE_MGMT_SIDE_LEFT};

    case_select_only_block(0);
}

static void case_csv_write_field(FILE *fp, const char *text) {
    /*
        Purpose: Writes the CSV field
        Returns: No value
    */

    fputc('"', fp);

    if (!text) {

        text = "";

    }
    for (const char *p = text; *p; p++) {

        if (*p == '"') {

            fputc('"', fp);

        }
        fputc(*p, fp);
    }
    fputc('"', fp);
}

static void case_csv_write_multiline_field(FILE *fp, const char *text) {
    /*
        Purpose: Writes the CSV multiline field
        Returns: No value
    */

    fputc('"', fp);

    if (!text) {

        text = "";

    }
    for (const char *p = text; *p; p++) {

        if (*p == '"') {

            fputc('"', fp);

        }

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

static void case_unescape_multiline(char *text) {
    /*
        Purpose: Unescapes the multiline
        Returns: No value
    */

    char *src;
    char *dst;

    if (!text) {

        return;

    }

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

static int case_has_extension(const char *name) {
    /*
        Purpose: Checks whether the extension is present
        Returns: Boolean status
    */

    const char *slash;
    const char *dot;

    if (!name) {

        return 0;

    }
    slash = strrchr(name, '/');

    if (!slash) {

        slash = strrchr(name, '\\');

    }
    dot = strrchr(name, '.');
    return dot && (!slash || dot > slash);
}

static void case_normalize_file_name(const char *src, char *dst, size_t dst_size) {
    /*
        Purpose: Normalizes the file name
        Returns: No value
    */

    char tmp[CASE_MGMT_FILE_SEARCH_TEXT_MAX];
    const char *base;
    size_t len;

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src || !src[0]) {

        src = "case_graph.case.csv";

    }

    base = strrchr(src, '/');

    if (!base) {

        base = strrchr(src, '\\');

    }
    base = base ? base + 1 : src;

    snprintf(tmp, sizeof(tmp), "%s", base);
    case_trim_text(tmp);

    if (tmp[0] == '\0') {

        snprintf(tmp, sizeof(tmp), "case_graph.case.csv");

    }

    for (size_t i = 0; tmp[i]; i++) {
        unsigned char c = (unsigned char)tmp[i];

        if (c < 32 || c == '/' || c == '\\' || c == ':' || c == '*') {

            tmp[i] = '_';

        }
    }

    if (!case_has_extension(tmp)) {

        len = strlen(tmp);

        if (len + strlen(".case.csv") < sizeof(tmp)) {

            memcpy(tmp + len, ".case.csv", sizeof(".case.csv"));

        }

    }

    snprintf(dst, dst_size, "%s", tmp);
}

static int case_is_case_graph_file(const char *name) {
    /*
        Purpose: Checks whether the case graph is file
        Returns: Boolean status
    */

    size_t len;

    if (!name || name[0] == '.') {

        return 0;

    }
    len = strlen(name);

    if (len >= 9 && strcmp(name + len - 9, ".case.csv") == 0) {

        return 1;

    }

    if (len >= 5 && strcmp(name + len - 5, ".case") == 0) {

        return 1;

    }

    if (len >= 4 && strcmp(name + len - 4, ".csv") == 0 && strncmp(name, "CASE_", 5) != 0) {

        return 1;

    }
    return 0;
}

static void case_scan_case_graph_files(void) {
    /*
        Purpose: Scans the case graph files
        Returns: No value
    */

    static Type_DataStore_Document_Summary stored[CASE_MGMT_SOURCE_MAX_FILES];
    size_t stored_count = 0;
    char database_error[256] = "";

    Global_Case_File_Search_Count = 0;
    Global_Case_File_Search_Scroll = 0;
    Global_Case_File_Search_Hover = -1;

    if (!DATASTORE_list_documents(DATASTORE_KIND_CASE_MANAGEMENT, stored, sizeof(stored) / sizeof(stored[0]),
                                  &stored_count, database_error, sizeof(database_error))) {

        char message[384];
        snprintf(message, sizeof(message), "Unable to list database cases: %.280s", database_error);
        case_set_status(message, Case_Red);
        return;

    }

    for (size_t i = 0; i < stored_count && Global_Case_File_Search_Count < CASE_MGMT_SOURCE_MAX_FILES; i++) {

        if (!case_is_case_graph_file(stored[i].document_name)) {

            continue;

        }
        case_copy_text(Global_Case_File_Search_Files[Global_Case_File_Search_Count],
                       sizeof(Global_Case_File_Search_Files[Global_Case_File_Search_Count]), stored[i].document_name);
        Global_Case_File_Search_Count++;
    }

    if (Global_Case_File_Search_Count > 1) {

        qsort(Global_Case_File_Search_Files, (size_t)Global_Case_File_Search_Count,
              sizeof(Global_Case_File_Search_Files[0]), case_name_compare);

    }
}

static void case_write_blocks_csv(FILE *fp) {
    /*
        Purpose: Writes the blocks CSV
        Returns: No value
    */

    fprintf(fp, "id,type,x,y,case_number,country,task,assigned_to,start_date,end_"
                "date,status,priority,source_file,description\n");
    for (int i = 0; i < Global_Case_Block_Count; i++) {
        Type_Case_Block *b = &Global_Case_Blocks[i];
        fprintf(fp, "%d,%d,%d,%d,", b->id, b->type, b->x, b->y);
        case_csv_write_field(fp, b->case_number);
        fputc(',', fp);
        case_csv_write_field(fp, b->country);
        fputc(',', fp);
        case_csv_write_field(fp, b->task);
        fputc(',', fp);
        case_csv_write_field(fp, b->assigned_to);
        fputc(',', fp);
        case_csv_write_field(fp, b->start_date);
        fputc(',', fp);
        case_csv_write_field(fp, b->end_date);
        fputc(',', fp);
        case_csv_write_field(fp, b->status);
        fputc(',', fp);
        case_csv_write_field(fp, b->priority);
        fputc(',', fp);
        case_csv_write_field(fp, b->source_file);
        fputc(',', fp);
        case_csv_write_multiline_field(fp, b->description);
        fputc('\n', fp);
    }
}

static void case_write_links_csv(FILE *fp) {
    /*
        Purpose: Writes the links CSV
        Returns: No value
    */

    fprintf(fp, "from_id,to_id,from_side,to_side\n");
    for (int i = 0; i < Global_Case_Link_Count; i++) {
        fprintf(fp, "%d,%d,%d,%d\n", Global_Case_Links[i].from_id, Global_Case_Links[i].to_id,
                Global_Case_Links[i].from_side, Global_Case_Links[i].to_side);
    }
}

static const char *case_database_case_number(void) {
    /*
        Purpose: Gets the database case number
        Returns: Text pointer
    */

    for (int i = 0; i < Global_Case_Block_Count; i++) {

        if (Global_Case_Blocks[i].case_number[0]) {

            return Global_Case_Blocks[i].case_number;

        }
    }
    return "";
}

static int case_serialize(unsigned char **content, size_t *content_size) {
    /*
        Purpose: Serializes the requested operation
        Returns: Success status
    */

    char *buffer = NULL;
    size_t size = 0;
    FILE *fp;

    if (!content || !content_size) {

        return 0;

    }

    *content = NULL;
    *content_size = 0;

    fp = open_memstream(&buffer, &size);

    if (!fp) {

        return 0;

    }

    fprintf(fp, "# RetroSpectrum CaseManagement v2\n");
    fprintf(fp, "[BLOCKS]\n");
    case_write_blocks_csv(fp);
    fprintf(fp, "[LINKS]\n");
    case_write_links_csv(fp);

    if (fclose(fp) != 0) {

        free(buffer);
        return 0;

    }

    *content = (unsigned char *)buffer;
    *content_size = size;
    return 1;
}

static void case_save(void) {
    /*
        Purpose: Saves the requested operation
        Returns: No value
    */

    char normalized[CASE_MGMT_FILE_SEARCH_TEXT_MAX];
    char database_error[256] = "";
    unsigned char *content = NULL;
    size_t content_size = 0;

    case_normalize_file_name(Global_Case_File_Name, normalized, sizeof(normalized));
    case_copy_text(Global_Case_File_Name, sizeof(Global_Case_File_Name), normalized);
    Global_Case_File_Name_Cursor = (int)strlen(Global_Case_File_Name);

    if (!case_serialize(&content, &content_size)) {

        case_set_status("Could not serialize case graph", Case_Red);
        return;

    }

    if (!DATASTORE_save_content(DATASTORE_KIND_CASE_MANAGEMENT, Global_Case_File_Name, case_database_case_number(),
                                content, content_size, database_error, sizeof(database_error))) {

        char message[384];
        snprintf(message, sizeof(message), "Database save failed: %.300s", database_error);
        DATASTORE_free_content(content, content_size);
        case_set_status(message, Case_Red);
        return;

    }

    DATASTORE_free_content(content, content_size);
    case_copy_text(Global_Case_Loaded_Database_Record, sizeof(Global_Case_Loaded_Database_Record),
                   Global_Case_File_Name);
    case_scan_case_graph_files();
    case_set_status("Case saved to database", Case_Text);
}

static void case_reset_graph_for_load(void) {
    /*
        Purpose: Resets the graph for load
        Returns: No value
    */

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
    Global_Case_Source_Popup_Open = 0;
    Global_Case_Description_Popup_Open = 0;
    Global_Case_File_Name_Active = 0;
}

static char *case_read_csv_field(char **cursor, char *out, size_t out_size) {
    /*
        Purpose: Reads the CSV field
        Returns: Text pointer
    */

    size_t n = 0;
    char *p;

    if (!cursor || !*cursor || !out || out_size == 0) {

        return NULL;

    }
    p = *cursor;

    if (*p == '"') {

        p++;
        while (*p) {

            if (*p == '"' && p[1] == '"') {

                if (n + 1 < out_size) {

                    out[n++] = '"';

                }
                p += 2;
                continue;

            }

            if (*p == '"') {

                p++;
                break;

            }

            if (n + 1 < out_size) {

                out[n++] = *p;

            }
            p++;
        }
        while (*p && *p != ',') {
            p++;
        }

    }

    else {

        while (*p && *p != ',' && *p != '\n' && *p != '\r') {

            if (n + 1 < out_size) {

                out[n++] = *p;

            }
            p++;
        }

    }

    out[n] = '\0';

    if (*p == ',') {

        p++;

    }
    *cursor = p;
    return out;
}

static int case_text_equals_ci(const char *a, const char *b) {
    /*
        Purpose: Checks whether the text case-insensitively values are equal
        Returns: Boolean status
    */

    if (!a) {

        a = "";

    }

    if (!b) {

        b = "";

    }
    while (*a && *b) {

        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {

            return 0;

        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int case_text_contains_ci(const char *haystack, const char *needle) {
    /*
        Purpose: Checks whether the text case-insensitively contains a value
        Returns: Boolean status
    */

    char hay[512];
    char ndl[256];
    size_t i;

    if (!haystack) {

        haystack = "";

    }

    if (!needle || !needle[0]) {

        return 1;

    }

    for (i = 0; i + 1 < sizeof(hay) && haystack[i]; i++) {
        hay[i] = (char)tolower((unsigned char)haystack[i]);
    }
    hay[i] = '\0';
    for (i = 0; i + 1 < sizeof(ndl) && needle[i]; i++) {
        ndl[i] = (char)tolower((unsigned char)needle[i]);
    }
    ndl[i] = '\0';
    return strstr(hay, ndl) != NULL;
}

static void case_trim_text(char *text) {
    /*
        Purpose: Trims the text
        Returns: No value
    */

    size_t len;
    char *start;

    if (!text) {

        return;

    }
    start = text;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {

        memmove(text, start, strlen(start) + 1);

    }
    len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
}

static int case_case_option_exists(const char *case_number) {
    /*
        Purpose: Checks whether the case option exists
        Returns: Boolean status
    */

    if (!case_number || !case_number[0]) {

        return 1;

    }
    for (int i = 0; i < Global_Case_Case_Count; i++) {

        if (case_text_equals_ci(Global_Case_Case_Options[i], case_number)) {

            return 1;

        }
    }
    return 0;
}

static void case_add_case_option(const char *case_number) {
    /*
        Purpose: Adds the case option
        Returns: No value
    */

    if (!case_number || !case_number[0]) {

        return;

    }

    if (Global_Case_Case_Count >= CASE_MGMT_SOURCE_MAX_FILES) {

        return;

    }

    if (case_case_option_exists(case_number)) {

        return;

    }
    snprintf(Global_Case_Case_Options[Global_Case_Case_Count], sizeof(Global_Case_Case_Options[Global_Case_Case_Count]),
             "%s", case_number);
    Global_Case_Case_Count++;
}

static void case_scan_case_files(void) {
    /*
        Purpose: Scans the case files
        Returns: No value
    */

    static Type_DataStore_Document_Summary stored[CASE_MGMT_SOURCE_MAX_FILES];
    size_t stored_count = 0;
    char database_error[256] = "";

    Global_Case_Case_Count = 0;
    Global_Case_Case_Scroll = 0;
    Global_Case_Case_Hover = -1;

    if (!DATASTORE_list_documents(DATASTORE_KIND_CLASSIFICATION, stored, sizeof(stored) / sizeof(stored[0]),
                                  &stored_count, database_error, sizeof(database_error))) {

        char message[384];
        snprintf(message, sizeof(message), "Unable to list database classifications: %.250s", database_error);
        case_set_status(message, Case_Red);
        return;

    }

    for (size_t i = 0; i < stored_count && Global_Case_Case_Count < CASE_MGMT_SOURCE_MAX_FILES; i++) {

        if (stored[i].case_number[0]) {

            case_add_case_option(stored[i].case_number);

        }
    }

    stored_count = 0;
    database_error[0] = '\0';

    if (DATASTORE_list_documents(DATASTORE_KIND_CASE_MANAGEMENT, stored, sizeof(stored) / sizeof(stored[0]),
                                 &stored_count, database_error, sizeof(database_error))) {

        for (size_t i = 0; i < stored_count && Global_Case_Case_Count < CASE_MGMT_SOURCE_MAX_FILES; i++) {

            if (stored[i].case_number[0]) {

                case_add_case_option(stored[i].case_number);

            }
        }

    }

    qsort(Global_Case_Case_Options, (size_t)Global_Case_Case_Count, sizeof(Global_Case_Case_Options[0]),
          case_name_compare);
}

static int case_build_case_matches(int *matches, int max_matches) {
    /*
        Purpose: Checks whether the build case matches the requested data
        Returns: Boolean status
    */

    char *query = case_selected_field_text(CASE_MGMT_FIELD_CASE_NUMBER);
    int out = 0;

    if (!matches || max_matches <= 0) {

        return 0;

    }
    for (int i = 0; i < Global_Case_Case_Count && out < max_matches; i++) {

        if (!query || !query[0] || case_text_contains_ci(Global_Case_Case_Options[i], query)) {

            matches[out++] = i;

        }
    }
    return out;
}

static int case_build_country_matches(int *matches, int max_matches) {
    /*
        Purpose: Checks whether the build country matches the requested data
        Returns: Boolean status
    */

    char *query = case_selected_field_text(CASE_MGMT_FIELD_COUNTRY);
    int out = 0;
    int count = case_country_count();

    if (!matches || max_matches <= 0) {

        return 0;

    }
    for (int i = 0; i < count && out < max_matches; i++) {

        if (!query || !query[0] || case_text_contains_ci(CASE_MGMT_COUNTRIES[i].name, query)) {

            matches[out++] = i;

        }
    }
    return out;
}

static void case_scan_users(void) {
    /*
        Purpose: Scans the users
        Returns: No value
    */

    size_t count = 0;
    char error[256] = "";

    Global_Case_User_Count = 0;
    Global_Case_User_Scroll = 0;
    Global_Case_User_Hover = -1;
    Global_Case_User_Keyboard_Pos = -1;

    if (!AUTH_DB_list_users(Global_Case_User_Options, CASE_MGMT_SOURCE_MAX_FILES, &count, error, sizeof(error))) {

        case_set_status(error[0] ? error : "Unable to load users", Case_Red);
        return;

    }

    if (count > CASE_MGMT_SOURCE_MAX_FILES) {

        count = CASE_MGMT_SOURCE_MAX_FILES;

    }
    Global_Case_User_Count = (int)count;
}

static int case_build_user_matches(int *matches, int max_matches) {
    /*
        Purpose: Checks whether the build user matches the requested data
        Returns: Boolean status
    */

    char *query = case_selected_field_text(CASE_MGMT_FIELD_USER);
    int out = 0;

    if (!matches || max_matches <= 0) {

        return 0;

    }

    for (int i = 0; i < Global_Case_User_Count && out < max_matches; i++) {

        if (!query || !query[0] || case_text_contains_ci(Global_Case_User_Options[i].username, query)) {

            matches[out++] = i;

        }
    }
    return out;
}

static void case_clamp_user_scroll(void) {
    /*
        Purpose: Clamps the user scroll
        Returns: No value
    */

    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count = case_build_user_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
    int max_scroll = count - CASE_MGMT_USER_MAX_VISIBLE;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Case_User_Scroll < 0) {

        Global_Case_User_Scroll = 0;

    }

    if (Global_Case_User_Scroll > max_scroll) {

        Global_Case_User_Scroll = max_scroll;

    }
}

static void case_select_user_option(int index) {
    /*
        Purpose: Selects the user option
        Returns: No value
    */

    char *dst;

    if (index < 0 || index >= Global_Case_User_Count) {

        return;

    }

    dst = case_selected_field_text(CASE_MGMT_FIELD_USER);

    if (dst) {

        case_copy_text(dst, CASE_MGMT_TEXT_MAX, Global_Case_User_Options[index].username);
        Global_Case_Field_Cursor[CASE_MGMT_FIELD_USER] = (int)strlen(dst);

    }

    Global_Case_User_Dropdown_Open = 0;
    Global_Case_User_Hover = -1;
    Global_Case_User_Keyboard_Pos = -1;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
}

static void case_select_case_option(int index) {
    /*
        Purpose: Selects the case option
        Returns: No value
    */

    char *dst;

    if (index < 0 || index >= Global_Case_Case_Count) {

        return;

    }
    dst = case_selected_field_text(CASE_MGMT_FIELD_CASE_NUMBER);

    if (dst) {

        case_copy_text(dst, 128, Global_Case_Case_Options[index]);
        Global_Case_Field_Cursor[CASE_MGMT_FIELD_CASE_NUMBER] = (int)strlen(dst);

    }
    Global_Case_Case_Dropdown_Open = 0;
    Global_Case_Case_Hover = -1;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
}

static void case_select_country_option(int index) {
    /*
        Purpose: Selects the country option
        Returns: No value
    */

    char *dst;

    if (index < 0 || index >= case_country_count()) {

        return;

    }
    dst = case_selected_field_text(CASE_MGMT_FIELD_COUNTRY);

    if (dst) {

        case_copy_text(dst, 128, CASE_MGMT_COUNTRIES[index].name);
        Global_Case_Field_Cursor[CASE_MGMT_FIELD_COUNTRY] = (int)strlen(dst);

    }
    Global_Case_Country_Dropdown_Open = 0;
    Global_Case_Country_Hover = -1;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
}

static int case_parse_block_line(char *line, int has_type, int has_case_number, int has_country,
                                 int old_timeline_format, int has_priority, int has_source_file, int has_description) {
    /*
        Purpose: Parses the block line
        Returns: Success status
    */

    Type_Case_Block b;
    char id_text[64];
    char type_text[64];
    char x_text[64];
    char y_text[64];
    char timeline_text[CASE_MGMT_TEXT_MAX];
    char *p = line;

    if (!line || Global_Case_Block_Count >= CASE_MGMT_MAX_BLOCKS) {

        return 0;

    }

    memset(&b, 0, sizeof(b));
    memset(timeline_text, 0, sizeof(timeline_text));
    b.type = CASE_MGMT_BLOCK_TASK;

    case_read_csv_field(&p, id_text, sizeof(id_text));

    if (has_type) {

        case_read_csv_field(&p, type_text, sizeof(type_text));
        b.type = atoi(type_text);

        if (b.type != CASE_MGMT_BLOCK_CASE) {

            b.type = CASE_MGMT_BLOCK_TASK;

        }

    }
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

    if (b.id <= 0) {

        b.id = Global_Case_Next_Id;

    }

    if (b.id >= Global_Case_Next_Id) {

        Global_Case_Next_Id = b.id + 1;

    }

    if (b.status[0] == '\0') {

        case_copy_text(b.status, sizeof(b.status), "Todo");

    }

    if (b.priority[0] == '\0') {

        case_copy_text(b.priority, sizeof(b.priority), "3");

    }

    if (b.type == CASE_MGMT_BLOCK_CASE && b.task[0] == '\0') {

        case_copy_text(b.task, sizeof(b.task), "Case metadata");

    }
    Global_Case_Blocks[Global_Case_Block_Count++] = b;
    return 1;
}

static void case_parse_link_line(char *line) {
    /*
        Purpose: Parses the link line
        Returns: No value
    */

    int from_id = 0;
    int to_id = 0;
    int from_side = CASE_MGMT_SIDE_RIGHT;
    int to_side = CASE_MGMT_SIDE_LEFT;
    int parsed;

    if (!line || Global_Case_Link_Count >= CASE_MGMT_MAX_LINKS) {

        return;

    }

    parsed = sscanf(line, "%d,%d,%d,%d", &from_id, &to_id, &from_side, &to_side);

    if (parsed >= 2 && case_find_block_index_by_id(from_id) >= 0 && case_find_block_index_by_id(to_id) >= 0) {

        if (from_side < 0 || from_side > 3) {

            from_side = CASE_MGMT_SIDE_RIGHT;

        }

        if (to_side < 0 || to_side > 3) {

            to_side = CASE_MGMT_SIDE_LEFT;

        }
        Global_Case_Links[Global_Case_Link_Count++] = (Type_Case_Link){from_id, to_id, from_side, to_side};

    }
}

static int case_load_stream(FILE *fp) {
    /*
        Purpose: Loads the stream
        Returns: Success status
    */

    char line[4096];
    char header[2048] = "";
    int in_blocks = 0;
    int in_links = 0;
    int block_header_seen = 0;
    int old_timeline_format = 0;
    int has_source_file = 1;
    int has_description = 1;
    int has_priority = 1;
    int has_case_number = 1;
    int has_country = 1;
    int has_type = 1;

    if (!fp) {

        return 0;

    }

    case_reset_graph_for_load();

    while (fgets(line, sizeof(line), fp)) {

        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') {

            continue;

        }

        if (strncmp(line, "[BLOCKS]", 8) == 0) {

            in_blocks = 1;
            in_links = 0;
            block_header_seen = 0;
            continue;

        }

        if (strncmp(line, "[LINKS]", 7) == 0) {

            in_blocks = 0;
            in_links = 1;
            fgets(line, sizeof(line), fp);
            continue;

        }

        if (in_blocks) {

            if (!block_header_seen) {

                snprintf(header, sizeof(header), "%s", line);
                old_timeline_format = strstr(header, "timeline") != NULL && strstr(header, "start_date") == NULL;
                has_source_file = strstr(header, "source_file") != NULL;
                has_description = strstr(header, "description") != NULL;
                has_priority = strstr(header, "priority") != NULL;
                has_case_number = strstr(header, "case_number") != NULL;
                has_country = strstr(header, "country") != NULL;
                has_type = strstr(header, "type") != NULL;
                block_header_seen = 1;
                continue;

            }
            case_parse_block_line(line, has_type, has_case_number, has_country, old_timeline_format, has_priority,
                                  has_source_file, has_description);

        }

        else if (in_links) {

            case_parse_link_line(line);

        }
    }

    if (Global_Case_Block_Count > 0) {

        case_select_only_block(0);

    }
    return 1;
}

static int case_load_current_file(void) {
    /*
        Purpose: Loads the current file
        Returns: Success status
    */

    char normalized[CASE_MGMT_FILE_SEARCH_TEXT_MAX];
    char database_error[256] = "";
    unsigned char *content = NULL;
    size_t content_size = 0;
    int found = 0;
    FILE *fp = NULL;
    int loaded = 0;

    case_normalize_file_name(Global_Case_File_Name, normalized, sizeof(normalized));
    case_copy_text(Global_Case_File_Name, sizeof(Global_Case_File_Name), normalized);
    Global_Case_File_Name_Cursor = (int)strlen(Global_Case_File_Name);

    if (!DATASTORE_load_content(DATASTORE_KIND_CASE_MANAGEMENT, Global_Case_File_Name, &content, &content_size, &found,
                                database_error, sizeof(database_error))) {

        char message[384];
        snprintf(message, sizeof(message), "Database load failed: %.300s", database_error);
        case_set_status(message, Case_Red);
        return 0;

    }

    if (!found) {

        case_set_status("No saved case found in database", Case_Warn);
        return 0;

    }

    fp = fmemopen(content, content_size, "r");

    if (!fp) {

        DATASTORE_free_content(content, content_size);
        case_set_status("Unable to open database case content", Case_Red);
        return 0;

    }

    loaded = case_load_stream(fp);
    fclose(fp);
    DATASTORE_free_content(content, content_size);

    if (!loaded) {

        case_set_status("Database case content is invalid", Case_Red);
        return 0;

    }

    case_copy_text(Global_Case_Loaded_Database_Record, sizeof(Global_Case_Loaded_Database_Record),
                   Global_Case_File_Name);
    case_set_status("Case loaded from database", Case_Text);
    return 1;
}

static void case_delete_loaded_database_record(void) {
    /*
        Purpose: Deletes the currently loaded database record
        Returns: No value
    */

    char database_error[256] = "";
    int deleted = 0;

    if (Global_Case_Loaded_Database_Record[0] == '\0') {

        case_set_status("No database record is currently loaded", Case_Warn);
        return;

    }

    if (!DATASTORE_delete_content(DATASTORE_KIND_CASE_MANAGEMENT, Global_Case_Loaded_Database_Record, &deleted,
                                  database_error, sizeof(database_error))) {

        char message[384];
        snprintf(message, sizeof(message), "Database record deletion failed: %.280s", database_error);
        case_set_status(message, Case_Red);
        return;

    }

    if (!deleted) {

        Global_Case_Loaded_Database_Record[0] = '\0';
        case_scan_case_graph_files();
        case_set_status("Loaded database record no longer exists", Case_Warn);
        return;

    }

    Global_Case_Loaded_Database_Record[0] = '\0';
    case_scan_case_graph_files();
    case_set_status("Database record deleted", Case_Text);
}

static int case_handle_database_delete_confirmation(const SDL_Event *event) {
    /*
        Purpose: Handles the database record deletion confirmation
        Returns: Handling status
    */

    if (!Global_Case_Database_Delete_Confirm_Open || !event) {

        return 0;

    }

    if (event->type == SDL_KEYDOWN) {

        if (event->key.keysym.sym == SDLK_ESCAPE) {

            Global_Case_Database_Delete_Confirm_Open = 0;
            return 1;

        }

        if (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER) {

            Global_Case_Database_Delete_Confirm_Open = 0;
            case_delete_loaded_database_record();
            return 1;

        }
        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        if (case_point_in_rect(event->button.x, event->button.y, Global_Case_Database_Confirm_Cancel_Rect)) {

            Global_Case_Database_Delete_Confirm_Open = 0;
            return 1;

        }

        if (case_point_in_rect(event->button.x, event->button.y, Global_Case_Database_Confirm_Delete_Rect)) {

            Global_Case_Database_Delete_Confirm_Open = 0;
            case_delete_loaded_database_record();
            return 1;

        }
        return 1;

    }

    return 1;
}

static void case_draw_database_delete_confirmation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the database record deletion confirmation dialog
        Returns: No value
    */

    SDL_Rect panel;
    SDL_Rect message;
    int mx = 0;
    int my = 0;
    int delete_hover;
    int cancel_hover;
    char title[384];

    if (!Global_Case_Database_Delete_Confirm_Open || !renderer || !font) {

        return;

    }

    case_get_adjusted_mouse_state(&mx, &my);
    panel = (SDL_Rect){(win_w - 540) / 2, (win_h - 238) / 2, 540, 238};

    if (panel.x < 12) {

        panel.x = 12;
        panel.w = win_w - 24;

    }

    if (panel.y < 12) {

        panel.y = 12;

    }

    Global_Case_Database_Confirm_Cancel_Rect = (SDL_Rect){panel.x + panel.w - 242, panel.y + panel.h - 58, 106, 36};
    Global_Case_Database_Confirm_Delete_Rect = (SDL_Rect){panel.x + panel.w - 124, panel.y + panel.h - 58, 106, 36};
    delete_hover = case_point_in_rect(mx, my, Global_Case_Database_Confirm_Delete_Rect);
    cancel_hover = case_point_in_rect(mx, my, Global_Case_Database_Confirm_Cancel_Rect);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, (SDL_Rect){0, 0, win_w, win_h}, (SDL_Color){0, 0, 0, 205});
    draw_filled_rect(renderer, panel, (SDL_Color){20, 0, 0, 252});
    draw_outline_rect(renderer, panel, (SDL_Color){255, 35, 35, 255});

    snprintf(title, sizeof(title), "Delete database record %s?", Global_Case_Loaded_Database_Record);
    draw_text(renderer, font, title, panel.x + 22, panel.y + 22, (SDL_Color){255, 90, 90, 255});

    message = (SDL_Rect){panel.x + 22, panel.y + 62, panel.w - 44, 92};
    draw_text(renderer, font, "This permanently deletes the currently loaded Case Management graph", message.x,
              message.y, (SDL_Color){235, 205, 205, 255});
    draw_text(renderer, font, "record from the database.", message.x, message.y + 24, (SDL_Color){235, 205, 205, 255});

    case_draw_button(renderer, font, Global_Case_Database_Confirm_Cancel_Rect, "Cancel", 0, cancel_hover, 0);
    case_draw_button(renderer, font, Global_Case_Database_Confirm_Delete_Rect, "Delete", 0, delete_hover, 1);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void case_load(void) {
    /*
        Purpose: Loads a case management graph
        Returns: No value
    */

    case_push_undo_state();

    if (!case_load_current_file()) {

        if (Global_Case_Undo_Count > 0) {

            Global_Case_Undo_Count--;

        }

    }
}

static void case_add_block_typed(SDL_Rect canvas, int block_type) {
    /*
        Purpose: Adds the block typed
        Returns: No value
    */

    if (Global_Case_Block_Count >= CASE_MGMT_MAX_BLOCKS) {

        case_set_status("Maximum block count reached", Case_Warn);
        return;

    }

    case_ensure_view(canvas);
    case_push_undo_state();

    int n = Global_Case_Block_Count;
    Type_Case_Block b;
    memset(&b, 0, sizeof(b));
    b.id = Global_Case_Next_Id++;
    b.type = (block_type == CASE_MGMT_BLOCK_CASE) ? CASE_MGMT_BLOCK_CASE : CASE_MGMT_BLOCK_TASK;

    int screen_x = canvas.x + 46 + ((n * 34) % (canvas.w > 360 ? canvas.w - 330 : 120));
    int screen_y = canvas.y + 44 + ((n * 52) % (canvas.h > 190 ? canvas.h - 170 : 120));
    b.x = case_screen_to_world_x(canvas, screen_x);
    b.y = case_screen_to_world_y(canvas, screen_y);

    b.case_number[0] = '\0';
    b.country[0] = '\0';

    if (b.type == CASE_MGMT_BLOCK_CASE) {

        case_copy_text(b.task, sizeof(b.task), "Case metadata");
        case_copy_text(b.assigned_to, sizeof(b.assigned_to), "Case owner");

    }

    else {

        case_copy_text(b.task, sizeof(b.task), "New task");
        case_copy_text(b.assigned_to, sizeof(b.assigned_to), "Unassigned");

    }
    b.start_date[0] = '\0';
    b.end_date[0] = '\0';
    case_copy_text(b.status, sizeof(b.status), "Todo");
    case_copy_text(b.priority, sizeof(b.priority), "3");
    b.source_file[0] = '\0';
    b.description[0] = '\0';

    Global_Case_Blocks[Global_Case_Block_Count++] = b;
    case_select_only_block(Global_Case_Block_Count - 1);
    Global_Case_Selected_Link = -1;
    Global_Case_Active_Field = (b.type == CASE_MGMT_BLOCK_CASE) ? CASE_MGMT_FIELD_CASE_NUMBER : CASE_MGMT_FIELD_TASK;
    Global_Case_Field_Cursor[Global_Case_Active_Field] = (int)strlen(
        case_selected_field_text(Global_Case_Active_Field) ? case_selected_field_text(Global_Case_Active_Field) : "");
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;
    case_set_status(b.type == CASE_MGMT_BLOCK_CASE ? "Created case metadata block" : "Created task block", Case_Text);
}

static void case_add_block(SDL_Rect canvas) {
    /*
        Purpose: Adds the block
        Returns: No value
    */

    case_add_block_typed(canvas, CASE_MGMT_BLOCK_TASK);
}

static void case_duplicate_selected_block(SDL_Rect canvas) {
    /*
        Purpose: Duplicates the selected block
        Returns: No value
    */

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
    case_push_undo_state();

    for (int i = 0; i < Global_Case_Block_Count; i++) {

        if (!Global_Case_Selected_Blocks[i]) {

            continue;

        }

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

            if (old_ids[m] == link.from_id) {

                new_from = new_ids[m];

            }

            if (old_ids[m] == link.to_id) {

                new_to = new_ids[m];

            }
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

    if (map_count > 1) {

        case_set_status("Duplicated selected block group", Case_Text);

    }

    else {

        case_set_status("Duplicated selected block", Case_Text);

    }
}

static int case_link_exists(int from_id, int to_id) {
    /*
        Purpose: Checks whether the link exists
        Returns: Boolean status
    */

    for (int i = 0; i < Global_Case_Link_Count; i++) {

        if (Global_Case_Links[i].from_id == from_id && Global_Case_Links[i].to_id == to_id) {

            return 1;

        }
    }
    return 0;
}

static void case_add_link(int from_index, int to_index, int from_side, int to_side) {
    /*
        Purpose: Adds the link
        Returns: No value
    */

    if (from_index < 0 || to_index < 0 || from_index == to_index) {

        return;

    }

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

    if (from_side < 0 || from_side > 3) {

        from_side = CASE_MGMT_SIDE_RIGHT;

    }

    if (to_side < 0 || to_side > 3) {

        to_side = CASE_MGMT_SIDE_LEFT;

    }
    case_push_undo_state();
    Global_Case_Links[Global_Case_Link_Count++] = (Type_Case_Link){from_id, to_id, from_side, to_side};
    Global_Case_Selected_Link = Global_Case_Link_Count - 1;
    Global_Case_Selected = -1;
    case_set_status("Linked blocks", Case_Text);
}

static int case_id_is_marked_for_removal(int id, const int *remove_ids, int remove_count) {
    /*
        Purpose: Checks whether the ID is marked for removal
        Returns: Success status
    */

    for (int i = 0; i < remove_count; i++) {

        if (remove_ids[i] == id) {

            return 1;

        }
    }
    return 0;
}

static void case_delete_selected(void) {
    /*
        Purpose: Deletes the selected
        Returns: No value
    */

    int remove_ids[CASE_MGMT_MAX_BLOCKS];
    int remove_count = 0;

    if (Global_Case_Selected_Link >= 0 && Global_Case_Selected_Link < Global_Case_Link_Count) {

        case_push_undo_state();
        for (int i = Global_Case_Selected_Link; i + 1 < Global_Case_Link_Count; i++) {
            Global_Case_Links[i] = Global_Case_Links[i + 1];
        }
        Global_Case_Link_Count--;
        Global_Case_Selected_Link = -1;
        case_set_status("Deleted selected link", Case_Text);
        return;

    }

    for (int i = 0; i < Global_Case_Block_Count; i++) {

        if (Global_Case_Selected_Blocks[i]) {

            remove_ids[remove_count++] = Global_Case_Blocks[i].id;

        }
    }

    if (remove_count == 0 && Global_Case_Selected >= 0 && Global_Case_Selected < Global_Case_Block_Count) {

        remove_ids[remove_count++] = Global_Case_Blocks[Global_Case_Selected].id;

    }

    if (remove_count == 0) {

        return;

    }

    case_push_undo_state();

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

    if (Global_Case_Block_Count > 0) {

        case_select_only_block(Global_Case_Block_Count - 1);

    }
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
    Global_Case_Link_Source = -1;
    Global_Case_Selected_Link = -1;
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;

    if (remove_count > 1) {

        case_set_status("Deleted selected block group", Case_Text);

    }

    else {

        case_set_status("Deleted selected block", Case_Text);

    }
}

static char *case_selected_field_text(int field) {
    /*
        Purpose: Gets the selected field text
        Returns: Text pointer
    */

    if (Global_Case_Selected < 0 || Global_Case_Selected >= Global_Case_Block_Count) {

        return NULL;

    }
    Type_Case_Block *b = &Global_Case_Blocks[Global_Case_Selected];

    if (field == CASE_MGMT_FIELD_CASE_NUMBER) {

        return b->case_number;

    }

    if (field == CASE_MGMT_FIELD_COUNTRY) {

        return b->country;

    }

    if (field == CASE_MGMT_FIELD_TASK) {

        return b->task;

    }

    if (field == CASE_MGMT_FIELD_USER) {

        return b->assigned_to;

    }

    if (field == CASE_MGMT_FIELD_START_DATE) {

        return b->start_date;

    }

    if (field == CASE_MGMT_FIELD_END_DATE) {

        return b->end_date;

    }

    if (field == CASE_MGMT_FIELD_STATUS) {

        return b->status;

    }

    if (field == CASE_MGMT_FIELD_PRIORITY) {

        return b->priority;

    }

    if (field == CASE_MGMT_FIELD_SOURCE_FILE) {

        return b->source_file;

    }

    if (field == CASE_MGMT_FIELD_DESCRIPTION) {

        return b->description;

    }
    return NULL;
}

static int case_field_max_len(int field) {
    /*
        Purpose: Gets the maximum field length
        Returns: Maximum length
    */

    if (field == CASE_MGMT_FIELD_START_DATE || field == CASE_MGMT_FIELD_END_DATE) {

        return 10;

    }

    if (field == CASE_MGMT_FIELD_PRIORITY) {

        return 1;

    }

    if (field == CASE_MGMT_FIELD_DESCRIPTION) {

        return CASE_MGMT_DESCRIPTION_MAX - 1;

    }

    if (field == CASE_MGMT_FIELD_SOURCE_FILE || field == CASE_MGMT_FIELD_STATUS) {

        return 0;

    }

    if (field == CASE_MGMT_FIELD_CASE_NUMBER || field == CASE_MGMT_FIELD_COUNTRY) {

        return 127;

    }
    return CASE_MGMT_TEXT_MAX - 1;
}

static size_t case_field_storage_size(int field) {
    /*
        Purpose: Gets the field storage size
        Returns: Computed size
    */

    if (field == CASE_MGMT_FIELD_DESCRIPTION) {

        return CASE_MGMT_DESCRIPTION_MAX;

    }

    if (field == CASE_MGMT_FIELD_SOURCE_FILE) {

        return CASE_MGMT_SOURCE_FILE_MAX;

    }

    if (field == CASE_MGMT_FIELD_CASE_NUMBER || field == CASE_MGMT_FIELD_COUNTRY) {

        return 128;

    }

    if (field == CASE_MGMT_FIELD_PRIORITY) {

        return 16;

    }
    return CASE_MGMT_TEXT_MAX;
}

static int case_text_allowed_for_field(int field, char c) {
    /*
        Purpose: Gets the text allowed for field
        Returns: Field index
    */

    if (field == CASE_MGMT_FIELD_START_DATE || field == CASE_MGMT_FIELD_END_DATE) {

        return isdigit((unsigned char)c) || c == '/';

    }

    if (field == CASE_MGMT_FIELD_STATUS || field == CASE_MGMT_FIELD_SOURCE_FILE) {

        return 0;

    }

    if (field == CASE_MGMT_FIELD_PRIORITY) {

        return c >= '1' && c <= '5';

    }

    if (field == CASE_MGMT_FIELD_DESCRIPTION) {

        return (c >= 32 && c <= 126) || c == '\n';

    }
    return c >= 32 && c <= 126;
}

static int case_description_range_width(TTF_Font *font, const char *text, size_t start, size_t end) {
    /*
        Purpose: Calculates the description range width
        Returns: Text width
    */

    char buf[CASE_MGMT_DESCRIPTION_MAX + 8];
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

static void case_auto_wrap_description_text(char *text, int *cursor) {
    /*
        Purpose: Wraps a case description automatically
        Returns: No value
    */

    size_t len;
    size_t line_start;
    int max_px = Global_Case_Description_Wrap_Px;

    if (!text) {

        return;

    }

    if (max_px < 16) {

        max_px = 520;

    }

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

                if (len + 1 >= CASE_MGMT_DESCRIPTION_MAX) {

                    break;

                }
                memmove(text + break_pos + 1, text + break_pos, len - break_pos + 1);
                text[break_pos] = '\n';
                len++;
                line_end++;

                if (cursor && *cursor >= (int)break_pos) {

                    (*cursor)++;

                }
                segment_start = break_pos + 1;

            }
        }

        if (line_end >= len) {

            break;

        }
        line_start = line_end + 1;
    }

    if (cursor) {

        int text_len = (int)strlen(text);

        if (*cursor < 0) {

            *cursor = 0;

        }

        if (*cursor > text_len) {

            *cursor = text_len;

        }

    }
}

static void case_insert_text(char *dst, int *cursor, const char *src, int field) {
    /*
        Purpose: Inserts the text
        Returns: No value
    */

    size_t len;
    char filtered[2048];
    size_t add = 0;
    int max_len;

    if (!dst || !cursor || !src) {

        return;

    }
    max_len = case_field_max_len(field);

    if (field == CASE_MGMT_FIELD_DESCRIPTION) {

        case_description_delete_selection();

    }

    for (const char *p = src; *p && add + 1 < sizeof(filtered); p++) {

        if (case_text_allowed_for_field(field, *p)) {

            filtered[add++] = *p;

        }
    }
    filtered[add] = '\0';

    if (add == 0) {

        return;

    }

    len = strlen(dst);

    if ((int)(len + add) > max_len) {

        add = (size_t)(max_len - (int)len);

    }

    if (add == 0 || len + add >= case_field_storage_size(field)) {

        return;

    }

    if (*cursor < 0) {

        *cursor = 0;

    }

    if ((size_t)*cursor > len) {

        *cursor = (int)len;

    }

    memmove(dst + *cursor + add, dst + *cursor, len - (size_t)*cursor + 1);
    memcpy(dst + *cursor, filtered, add);
    *cursor += (int)add;

    if (field == CASE_MGMT_FIELD_DESCRIPTION) {

        case_auto_wrap_description_text(dst, cursor);

    }
}

static void case_backspace(char *dst, int *cursor) {
    /*
        Purpose: Removes the previous character from the requested data
        Returns: No value
    */

    size_t len;

    if (dst == case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION) && case_description_delete_selection()) {

        return;

    }

    if (!dst || !cursor) {

        return;

    }
    len = strlen(dst);

    if (*cursor <= 0 || len == 0) {

        return;

    }

    if ((size_t)*cursor > len) {

        *cursor = (int)len;

    }
    memmove(dst + *cursor - 1, dst + *cursor, len - (size_t)*cursor + 1);
    (*cursor)--;
}

static void case_delete_at_cursor(char *dst, int *cursor) {
    /*
        Purpose: Deletes the at cursor
        Returns: No value
    */

    size_t len;

    if (dst == case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION) && case_description_delete_selection()) {

        return;

    }

    if (!dst || !cursor) {

        return;

    }
    len = strlen(dst);

    if (*cursor < 0) {

        *cursor = 0;

    }

    if ((size_t)*cursor >= len) {

        return;

    }
    memmove(dst + *cursor, dst + *cursor + 1, len - (size_t)*cursor);
}

static void case_description_clear_selection(void) {
    /*
        Purpose: Clears the description selection
        Returns: No value
    */

    Global_Case_Description_Selection_Start = -1;
    Global_Case_Description_Selection_End = -1;
    Global_Case_Description_Selecting = 0;
}

static int case_description_selection_range(int *a, int *b) {
    /*
        Purpose: Gets the description selection range
        Returns: Success status
    */

    int s = Global_Case_Description_Selection_Start;
    int e = Global_Case_Description_Selection_End;
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int len = text ? (int)strlen(text) : 0;

    if (s < 0 || e < 0 || s == e) {

        return 0;

    }

    if (s > e) {

        int t = s;
        s = e;
        e = t;

    }

    if (s < 0) {

        s = 0;

    }

    if (e < 0) {

        e = 0;

    }

    if (s > len) {

        s = len;

    }

    if (e > len) {

        e = len;

    }

    if (s == e) {

        return 0;

    }

    if (a) {

        *a = s;

    }

    if (b) {

        *b = e;

    }
    return 1;
}

static int case_description_delete_selection(void) {
    /*
        Purpose: Deletes the description selection
        Returns: Success status
    */

    int a = 0;
    int b = 0;
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    size_t len;

    if (!text || !case_description_selection_range(&a, &b)) {

        return 0;

    }
    len = strlen(text);

    if (a < 0) {

        a = 0;

    }

    if (b < a) {

        b = a;

    }

    if ((size_t)b > len) {

        b = (int)len;

    }
    memmove(text + a, text + b, len - (size_t)b + 1);
    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = a;
    case_description_clear_selection();
    return 1;
}

static void case_description_start_selection_at_cursor(void) {
    /*
        Purpose: Starts the description selection at cursor
        Returns: No value
    */

    int cursor = Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int len = text ? (int)strlen(text) : 0;

    if (cursor < 0) {

        cursor = 0;

    }

    if (cursor > len) {

        cursor = len;

    }
    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = cursor;
    Global_Case_Description_Selecting = 1;
    Global_Case_Description_Selection_Start = cursor;
    Global_Case_Description_Selection_End = cursor;
}

static void case_description_update_selection_to_cursor(void) {
    /*
        Purpose: Updates the description selection to cursor
        Returns: No value
    */

    int cursor = Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int len = text ? (int)strlen(text) : 0;

    if (cursor < 0) {

        cursor = 0;

    }

    if (cursor > len) {

        cursor = len;

    }
    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = cursor;

    if (Global_Case_Description_Selection_Start < 0) {

        Global_Case_Description_Selection_Start = cursor;

    }
    Global_Case_Description_Selection_End = cursor;
}

static int case_description_build_lines(const char *text, int starts[128], int ends[128]) {
    /*
        Purpose: Builds the description lines
        Returns: Success status
    */

    int len = text ? (int)strlen(text) : 0;
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

static void case_description_move_horizontal(int direction) {
    /*
        Purpose: Moves the description horizontal
        Returns: No value
    */

    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int *cursor = &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
    int len = text ? (int)strlen(text) : 0;

    if (*cursor < 0) {

        *cursor = 0;

    }

    if (*cursor > len) {

        *cursor = len;

    }

    if (direction < 0 && *cursor > 0) {

        (*cursor)--;

    }

    if (direction > 0 && *cursor < len) {

        (*cursor)++;

    }
}

static void case_description_move_vertical(int direction) {
    /*
        Purpose: Moves the description vertical
        Returns: No value
    */

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

    if (target < 0 || target >= line_count) {

        return;

    }
    *cursor = ends[target];
}

static void case_set_description_cursor_from_mouse(SDL_Rect rect, int mouse_x, int mouse_y) {
    /*
        Purpose: Sets the description cursor from mouse
        Returns: No value
    */

    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int starts[128];
    int ends[128];
    int line_h = 19;
    int max_lines = (rect.h - 12) / line_h;
    int line_count = case_description_build_lines(text, starts, ends);
    int first_line = 0;

    if (max_lines < 1) {

        max_lines = 1;

    }

    if (line_count > max_lines) {

        first_line = line_count - max_lines;

    }

    int visible_line = (mouse_y - (rect.y + 7)) / line_h;

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
    int rel_x = mouse_x - (rect.x + 9);
    int column = 0;

    for (int i = 0; i <= line_len; i++) {
        int w0 = case_description_range_width(Global_Case_Description_Font, text, (size_t)starts[line],
                                              (size_t)(starts[line] + i));
        int w1 = w0;

        if (i < line_len) {

            w1 = case_description_range_width(Global_Case_Description_Font, text, (size_t)starts[line],
                                              (size_t)(starts[line] + i + 1));

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

    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = starts[line] + column;
}

static void case_cycle_status(void) {
    /*
        Purpose: Cycles the status
        Returns: No value
    */

    char *status = case_selected_field_text(CASE_MGMT_FIELD_STATUS);

    if (!status) {

        return;

    }

    for (int i = 0; i < CASE_MGMT_STATUS_COUNT; i++) {

        if (strcmp(status, CASE_MGMT_STATUS_OPTIONS[i]) == 0) {

            case_copy_text(status, CASE_MGMT_TEXT_MAX, CASE_MGMT_STATUS_OPTIONS[(i + 1) % CASE_MGMT_STATUS_COUNT]);
            return;

        }
    }

    case_copy_text(status, CASE_MGMT_TEXT_MAX, CASE_MGMT_STATUS_OPTIONS[0]);
}

static int case_days_in_month(int month, int year) {
    /*
        Purpose: Calculates the number of days in a month
        Returns: Computed value
    */

    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month == 2) {

        int leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;

    }

    if (month < 1 || month > 12) {

        return 30;

    }
    return days[month - 1];
}

static int case_first_weekday(int month, int year) {
    /*
        Purpose: Calculates the first weekday of a month
        Returns: Computed value
    */

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_mday = 1;
    t.tm_mon = month - 1;
    t.tm_year = year - 1900;
    t.tm_isdst = -1;

    if (mktime(&t) == (time_t)-1) {

        return 0;

    }
    return t.tm_wday;
}

static void case_today_month_year(int *month, int *year) {
    /*
        Purpose: Gets the current month and year
        Returns: No value
    */

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);

    if (lt) {

        if (month) {

            *month = lt->tm_mon + 1;

        }

        if (year) {

            *year = lt->tm_year + 1900;

        }

    }

    else {

        if (month) {

            *month = 1;

        }

        if (year) {

            *year = 2026;

        }

    }
}

static int case_parse_mmddyyyy(const char *text, int *month, int *day, int *year) {
    /*
        Purpose: Parses the mmddyyyy
        Returns: Success status
    */

    const char *cursor;
    char *end = NULL;
    long parsed_month;
    long parsed_day;
    long parsed_year;
    int m;
    int d;
    int y;

    if (!text || text[0] == '\0') {

        return 0;

    }

    cursor = text;

    errno = 0;
    parsed_month = strtol(cursor, &end, 10);

    if (errno == ERANGE || end == cursor || *end != '/' || parsed_month < 1 || parsed_month > 12) {

        return 0;

    }

    cursor = end + 1;

    errno = 0;
    parsed_day = strtol(cursor, &end, 10);

    if (errno == ERANGE || end == cursor || *end != '/' || parsed_day < 1 || parsed_day > 31) {

        return 0;

    }

    cursor = end + 1;

    errno = 0;
    parsed_year = strtol(cursor, &end, 10);

    if (errno == ERANGE || end == cursor || *end != '\0' || parsed_year < 0 || parsed_year > 9999) {

        return 0;

    }

    m = (int)parsed_month;
    d = (int)parsed_day;
    y = (int)parsed_year;

    if (y < 100) {

        y += (y >= 70) ? 1900 : 2000;

    }

    if (d > case_days_in_month(m, y)) {

        return 0;

    }

    if (month) {

        *month = m;

    }

    if (day) {

        *day = d;

    }

    if (year) {

        *year = y;

    }

    return 1;
}

static void case_open_calendar_for_field(int field) {
    /*
        Purpose: Opens the calendar for field
        Returns: No value
    */

    char *text;
    int month = 0;
    int day = 0;
    int year = 0;

    if (field != CASE_MGMT_FIELD_START_DATE && field != CASE_MGMT_FIELD_END_DATE) {

        return;

    }

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

static void case_shift_calendar_month(int delta) {
    /*
        Purpose: Shifts the calendar month
        Returns: No value
    */

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

static void case_set_calendar_day(int day) {
    /*
        Purpose: Sets the calendar day
        Returns: No value
    */

    char *text = case_selected_field_text(Global_Case_Calendar_Field);

    if (!text || day < 1) {

        return;

    }
    int month = Global_Case_Calendar_Month;
    int year = Global_Case_Calendar_Year;

    if (month < 1) {

        month = 1;

    }

    if (month > 12) {

        month = 12;

    }

    if (day > 31) {

        day = 31;

    }

    if (year < 0) {

        year = 0;

    }

    if (year > 9999) {

        year = 9999;

    }
    snprintf(text, CASE_MGMT_DATE_MAX, "%02d/%02d/%04d", month, day, year);
    Global_Case_Field_Cursor[Global_Case_Calendar_Field] = (int)strlen(text);
    Global_Case_Calendar_Open = 0;
}

static void case_paste_description_from_clipboard(void) {
    /*
        Purpose: Pastes text into the description from clipboard
        Returns: No value
    */

    char *clip = SDL_GetClipboardText();
    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);

    if (clip && text) {

        case_insert_text(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION], clip,
                         CASE_MGMT_FIELD_DESCRIPTION);

    }

    if (clip) {

        SDL_free(clip);

    }
}

void CASE_MANAGEMENT_enter_mode(const char *record_dir) {
    /*
        Purpose: Enters the case mode
        Returns: No value
    */

    if (record_dir && record_dir[0] != '\0') {

        case_copy_text(Global_Case_Record_Dir, sizeof(Global_Case_Record_Dir), record_dir);

    }
    Global_CaseManagement_Mode = 1;
    SDL_StartTextInput();
    case_seed_default_blocks();
    case_metadata_refresh();
    case_set_status("Case Management Workstation", Case_Text);
}

void CASE_MANAGEMENT_exit_mode(void) {
    /*
        Purpose: Exits the case mode
        Returns: No value
    */

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
    Global_Case_File_Name_Active = 0;
    Global_Case_File_Search_Open = 0;
    Global_Case_File_Search_Active = 0;
    Global_Case_Metadata_Search_Active = 0;
    Global_Case_Metadata_Name_Active = 0;
    Global_Case_Metadata_Description_Active = 0;
    Global_Case_Metadata_Delete_Confirm_Open = 0;
    Global_Case_Database_Delete_Confirm_Open = 0;
}

int CASE_MANAGEMENT_is_text_entry_active(void) {
    /*
        Purpose: Checks whether the text entry is active
        Returns: Boolean status
    */

    return Global_CaseManagement_Mode &&
           (Global_Case_Active_Field != CASE_MGMT_FIELD_NONE || Global_Case_File_Name_Active ||
            (Global_Case_Source_Popup_Open && Global_Case_Source_Search_Active) ||
            (Global_Case_File_Search_Open && Global_Case_File_Search_Active) || Global_Case_Metadata_Search_Active ||
            Global_Case_Metadata_Name_Active || Global_Case_Metadata_Description_Active);
}

static void case_toolbar_rects(int win_w, SDL_Rect *task_btn, SDL_Rect *case_btn, SDL_Rect *link_btn,
                               SDL_Rect *save_btn, SDL_Rect *load_btn, SDL_Rect *undo_btn, SDL_Rect *file_rect) {
    /*
        Purpose: Computes the case toolbar rectangles
        Returns: No value
    */

    int x = CASE_MGMT_MARGIN;
    int y = CASE_MGMT_MARGIN;
    int h = 34;
    int remaining;

    if (task_btn) {

        *task_btn = (SDL_Rect){x, y, 104, h};

    }
    x += 114;

    if (case_btn) {

        *case_btn = (SDL_Rect){x, y, 104, h};

    }
    x += 114;

    if (link_btn) {

        *link_btn = (SDL_Rect){x, y, 120, h};

    }
    x += 130;

    if (save_btn) {

        *save_btn = (SDL_Rect){x, y, 90, h};

    }
    x += 100;

    if (load_btn) {

        *load_btn = (SDL_Rect){x, y, 90, h};

    }
    x += 100;

    if (undo_btn) {

        *undo_btn = (SDL_Rect){x, y, 90, h};

    }
    x += 108;

    remaining = win_w - x - CASE_MGMT_MARGIN;

    if (remaining > 360) {

        remaining = 360;

    }

    if (remaining < 210) {

        remaining = 210;

    }

    if (file_rect) {

        *file_rect = (SDL_Rect){x, y, remaining, h};

    }
}

static SDL_Rect case_delete_database_record_rect(int win_w) {
    /*
        Purpose: Computes the delete database record button rectangle
        Returns: Button rectangle
    */

    return (SDL_Rect){win_w - CASE_MGMT_MARGIN - 200, CASE_MGMT_MARGIN, 200, 34};
}

static void case_editor_field_rects(SDL_Rect editor, SDL_Rect fields[CASE_MGMT_FIELD_COUNT], SDL_Rect *duplicate_btn,
                                    SDL_Rect *delete_btn) {
    /*
        Purpose: Computes the case editor field rectangles
        Returns: No value
    */

    int x = editor.x + 16;
    int y = editor.y + 104;
    int w = editor.w - 32;
    int normal_h = 31;
    int normal_gap = 58;
    int extra_description_gap = 24;
    int action_y = editor.y + editor.h - 42;
    int desc_h;

    for (int i = 0; i < CASE_MGMT_FIELD_COUNT; i++) {
        fields[i] = (SDL_Rect){0, 0, 0, 0};
    }

    for (int i = 0; i < CASE_MGMT_FIELD_COUNT; i++) {

        if (!case_should_show_field(i)) {

            continue;

        }

        if (i == CASE_MGMT_FIELD_DESCRIPTION) {

            y += extra_description_gap;
            desc_h = action_y - y - 18;

            if (desc_h < 104) {

                desc_h = 104;

            }

            if (desc_h > 180) {

                desc_h = 180;

            }
            fields[i] = (SDL_Rect){x, y, w, desc_h};
            y += desc_h + 12;

        }

        else {

            fields[i] = (SDL_Rect){x, y, w, normal_h};
            y += normal_gap;

        }
    }

    if (duplicate_btn) {

        *duplicate_btn = (SDL_Rect){editor.x + 16, action_y, 118, 34};

    }

    if (delete_btn) {

        *delete_btn = (SDL_Rect){editor.x + editor.w - 126, action_y, 110, 34};

    }
}

static SDL_Rect case_field_hit_rect(SDL_Rect field) {
    /*
        Purpose: Computes the field hit rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {field.x, field.y - 24, field.w, field.h + 24};
    return r;
}

static SDL_Rect case_description_open_button_rect(SDL_Rect field) {
    /*
        Purpose: Opens the description button rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {field.x + field.w - 70, field.y - 24, 70, 20};
    return r;
}

static SDL_Rect case_status_dropdown_rect(SDL_Rect status_field) {
    /*
        Purpose: Computes the status dropdown rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {status_field.x, status_field.y + status_field.h + 4, status_field.w,
                  CASE_MGMT_STATUS_COUNT * CASE_MGMT_STATUS_OPTION_H};
    return r;
}

static SDL_Rect case_calendar_rect(SDL_Rect field) {
    /*
        Purpose: Computes the calendar rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {field.x, field.y + field.h + 4, field.w, CASE_MGMT_CALENDAR_H};
    return r;
}

static SDL_Rect case_source_popup_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the source popup rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {(win_w - 1050) / 2, (win_h - 740) / 2, 1050, 740};

    if (r.x < CASE_MGMT_MARGIN) {

        r.x = CASE_MGMT_MARGIN;

    }

    if (r.y < CASE_MGMT_MARGIN) {

        r.y = CASE_MGMT_MARGIN;

    }

    if (r.w > win_w - 2 * CASE_MGMT_MARGIN) {

        r.w = win_w - 2 * CASE_MGMT_MARGIN;

    }

    if (r.h > win_h - 2 * CASE_MGMT_MARGIN) {

        r.h = win_h - 2 * CASE_MGMT_MARGIN;

    }

    if (r.w < 320) {

        r.w = 320;

    }

    if (r.h < 260) {

        r.h = 260;

    }
    return r;
}

static SDL_Rect case_file_search_popup_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the file search popup rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {(win_w - 1050) / 2, (win_h - 740) / 2, 1050, 740};

    if (r.x < CASE_MGMT_MARGIN) {

        r.x = CASE_MGMT_MARGIN;

    }

    if (r.y < CASE_MGMT_MARGIN) {

        r.y = CASE_MGMT_MARGIN;

    }

    if (r.w > win_w - 2 * CASE_MGMT_MARGIN) {

        r.w = win_w - 2 * CASE_MGMT_MARGIN;

    }

    if (r.h > win_h - 2 * CASE_MGMT_MARGIN) {

        r.h = win_h - 2 * CASE_MGMT_MARGIN;

    }

    if (r.w < 320) {

        r.w = 320;

    }

    if (r.h < 260) {

        r.h = 260;

    }
    return r;
}

static SDL_Rect case_file_search_input_rect(SDL_Rect popup) {
    /*
        Purpose: Computes the file search input rectangle
        Returns: Computed rectangle
    */

    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = {close_btn.x - 292, popup.y + 14, 276, 30};

    if (search.x < popup.x + 210) {

        search.x = popup.x + 210;
        search.w = close_btn.x - search.x - 16;

    }

    if (search.w < 120) {

        search.w = 120;

    }
    return search;
}

static SDL_Rect case_description_popup_rect(int win_w, int win_h) {
    /*
        Purpose: Computes the description popup rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {(win_w - 760) / 2, (win_h - 520) / 2, 760, 520};

    if (r.x < CASE_MGMT_MARGIN) {

        r.x = CASE_MGMT_MARGIN;

    }

    if (r.y < CASE_MGMT_MARGIN) {

        r.y = CASE_MGMT_MARGIN;

    }

    if (r.w > win_w - 2 * CASE_MGMT_MARGIN) {

        r.w = win_w - 2 * CASE_MGMT_MARGIN;

    }

    if (r.h > win_h - 2 * CASE_MGMT_MARGIN) {

        r.h = win_h - 2 * CASE_MGMT_MARGIN;

    }

    if (r.w < 360) {

        r.w = 360;

    }

    if (r.h < 300) {

        r.h = 300;

    }
    return r;
}

static int case_file_search_matches(const char *name) {
    /*
        Purpose: Checks whether the file search matches the requested data
        Returns: Boolean status
    */

    char hay[512];
    char needle[CASE_MGMT_FILE_SEARCH_TEXT_MAX];
    size_t i;

    if (!name) {

        name = "";

    }

    if (Global_Case_File_Search_Text[0] == '\0') {

        return 1;

    }

    for (i = 0; i + 1 < sizeof(hay) && name[i]; i++) {
        hay[i] = (char)tolower((unsigned char)name[i]);
    }
    hay[i] = '\0';
    for (i = 0; i + 1 < sizeof(needle) && Global_Case_File_Search_Text[i]; i++) {
        needle[i] = (char)tolower((unsigned char)Global_Case_File_Search_Text[i]);
    }
    needle[i] = '\0';

    return strstr(hay, needle) != NULL;
}

static int case_file_search_filtered_count(void) {
    /*
        Purpose: Counts filtered file search results
        Returns: Item count
    */

    int count = 0;
    for (int i = 0; i < Global_Case_File_Search_Count; i++) {

        if (case_file_search_matches(Global_Case_File_Search_Files[i])) {

            count++;

        }
    }
    return count;
}

static int case_file_search_filtered_index_at(int filtered_index) {
    /*
        Purpose: Gets the file search filtered index at a position
        Returns: Item index
    */

    int seen = 0;

    if (filtered_index < 0) {

        return -1;

    }
    for (int i = 0; i < Global_Case_File_Search_Count; i++) {

        if (!case_file_search_matches(Global_Case_File_Search_Files[i])) {

            continue;

        }

        if (seen == filtered_index) {

            return i;

        }
        seen++;
    }
    return -1;
}

static void case_file_search_clamp_scroll(void) {
    /*
        Purpose: Clamps the file search scroll
        Returns: No value
    */

    int filtered_count = case_file_search_filtered_count();
    int visible = 14;
    int max_scroll = filtered_count - visible;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Case_File_Search_Scroll < 0) {

        Global_Case_File_Search_Scroll = 0;

    }

    if (Global_Case_File_Search_Scroll > max_scroll) {

        Global_Case_File_Search_Scroll = max_scroll;

    }
}

static void case_close_file_search_menu(void) {
    /*
        Purpose: Closes the file search menu
        Returns: No value
    */

    Global_Case_File_Search_Open = 0;
    Global_Case_File_Search_Active = 0;
    Global_Case_File_Search_Hover = -1;
}

static void case_open_file_search_menu(void) {
    /*
        Purpose: Opens the file search menu
        Returns: No value
    */

    case_scan_case_graph_files();
    Global_Case_File_Search_Open = 1;
    Global_Case_File_Search_Active = 1;
    Global_Case_File_Search_Hover = -1;
    Global_Case_File_Search_Text[0] = '\0';
    Global_Case_File_Search_Cursor = 0;
    Global_Case_File_Search_Scroll = 0;
    Global_Case_File_Name_Active = 0;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
    Global_Case_Source_Popup_Open = 0;
    Global_Case_Description_Popup_Open = 0;

    if (Global_Case_File_Search_Count > 0) {

        case_set_status("Click a database case to load it", Case_Text);

    }

    else {

        case_set_status("No case graphs found in database", Case_Warn);

    }
}

static void case_file_search_select_index(int index, int open_after_select) {
    /*
        Purpose: Selects the file search index
        Returns: No value
    */

    if (index < 0 || index >= Global_Case_File_Search_Count) {

        return;

    }

    case_copy_text(Global_Case_File_Name, sizeof(Global_Case_File_Name), Global_Case_File_Search_Files[index]);
    Global_Case_File_Name_Cursor = (int)strlen(Global_Case_File_Name);
    case_close_file_search_menu();

    if (open_after_select) {

        case_push_undo_state();

        if (!case_load_current_file() && Global_Case_Undo_Count > 0) {

            Global_Case_Undo_Count--;

        }

    }

    else {

        case_set_status("Selected database case", Case_Text);

    }
}

static void case_file_search_insert_text(const char *text) {
    /*
        Purpose: Inserts the file search text
        Returns: No value
    */

    int len;
    int add;

    if (!text || text[0] == '\0') {

        return;

    }

    len = (int)strlen(Global_Case_File_Search_Text);
    add = (int)strlen(text);

    if (Global_Case_File_Search_Cursor < 0) {

        Global_Case_File_Search_Cursor = 0;

    }

    if (Global_Case_File_Search_Cursor > len) {

        Global_Case_File_Search_Cursor = len;

    }

    if (len + add >= CASE_MGMT_FILE_SEARCH_TEXT_MAX) {

        add = CASE_MGMT_FILE_SEARCH_TEXT_MAX - len - 1;

    }

    if (add <= 0) {

        return;

    }

    memmove(Global_Case_File_Search_Text + Global_Case_File_Search_Cursor + add,
            Global_Case_File_Search_Text + Global_Case_File_Search_Cursor,
            (size_t)(len - Global_Case_File_Search_Cursor + 1));
    memcpy(Global_Case_File_Search_Text + Global_Case_File_Search_Cursor, text, (size_t)add);
    Global_Case_File_Search_Cursor += add;
    Global_Case_File_Search_Scroll = 0;
}

static void case_file_search_backspace(void) {
    /*
        Purpose: Removes the previous character from the file search
        Returns: No value
    */

    int len = (int)strlen(Global_Case_File_Search_Text);

    if (Global_Case_File_Search_Cursor <= 0 || len <= 0) {

        return;

    }

    if (Global_Case_File_Search_Cursor > len) {

        Global_Case_File_Search_Cursor = len;

    }
    memmove(Global_Case_File_Search_Text + Global_Case_File_Search_Cursor - 1,
            Global_Case_File_Search_Text + Global_Case_File_Search_Cursor,
            (size_t)(len - Global_Case_File_Search_Cursor + 1));
    Global_Case_File_Search_Cursor--;
    Global_Case_File_Search_Scroll = 0;
}

static void case_file_search_delete(void) {
    /*
        Purpose: Deletes the file search
        Returns: No value
    */

    int len = (int)strlen(Global_Case_File_Search_Text);

    if (Global_Case_File_Search_Cursor < 0) {

        Global_Case_File_Search_Cursor = 0;

    }

    if (Global_Case_File_Search_Cursor >= len) {

        return;

    }
    memmove(Global_Case_File_Search_Text + Global_Case_File_Search_Cursor,
            Global_Case_File_Search_Text + Global_Case_File_Search_Cursor + 1,
            (size_t)(len - Global_Case_File_Search_Cursor));
    Global_Case_File_Search_Scroll = 0;
}

static void case_file_name_insert_text(const char *text) {
    /*
        Purpose: Inserts the file name text
        Returns: No value
    */

    int len;
    int add;
    char clean[CASE_MGMT_FILE_SEARCH_TEXT_MAX];
    int out = 0;

    if (!text || text[0] == '\0') {

        return;

    }
    for (int i = 0; text[i] && out + 1 < (int)sizeof(clean); i++) {
        unsigned char c = (unsigned char)text[i];

        if (c >= 32 && c <= 126 && c != '/' && c != '\\') {

            clean[out++] = (char)c;

        }
    }
    clean[out] = '\0';

    if (out <= 0) {

        return;

    }

    len = (int)strlen(Global_Case_File_Name);
    add = out;

    if (Global_Case_File_Name_Cursor < 0) {

        Global_Case_File_Name_Cursor = 0;

    }

    if (Global_Case_File_Name_Cursor > len) {

        Global_Case_File_Name_Cursor = len;

    }

    if (len + add >= CASE_MGMT_FILE_SEARCH_TEXT_MAX) {

        add = CASE_MGMT_FILE_SEARCH_TEXT_MAX - len - 1;

    }

    if (add <= 0) {

        return;

    }

    memmove(Global_Case_File_Name + Global_Case_File_Name_Cursor + add,
            Global_Case_File_Name + Global_Case_File_Name_Cursor, (size_t)(len - Global_Case_File_Name_Cursor + 1));
    memcpy(Global_Case_File_Name + Global_Case_File_Name_Cursor, clean, (size_t)add);
    Global_Case_File_Name_Cursor += add;
}

static void case_file_name_backspace(void) {
    /*
        Purpose: Removes the previous character from the file name
        Returns: No value
    */

    int len = (int)strlen(Global_Case_File_Name);

    if (Global_Case_File_Name_Cursor <= 0 || len <= 0) {

        return;

    }

    if (Global_Case_File_Name_Cursor > len) {

        Global_Case_File_Name_Cursor = len;

    }
    memmove(Global_Case_File_Name + Global_Case_File_Name_Cursor - 1,
            Global_Case_File_Name + Global_Case_File_Name_Cursor, (size_t)(len - Global_Case_File_Name_Cursor + 1));
    Global_Case_File_Name_Cursor--;
}

static void case_file_name_delete(void) {
    /*
        Purpose: Deletes the file name
        Returns: No value
    */

    int len = (int)strlen(Global_Case_File_Name);

    if (Global_Case_File_Name_Cursor < 0) {

        Global_Case_File_Name_Cursor = 0;

    }

    if (Global_Case_File_Name_Cursor >= len) {

        return;

    }
    memmove(Global_Case_File_Name + Global_Case_File_Name_Cursor,
            Global_Case_File_Name + Global_Case_File_Name_Cursor + 1, (size_t)(len - Global_Case_File_Name_Cursor));
}

static int case_handle_file_search_event(const SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the file search event
        Returns: Handling status
    */

    SDL_Rect popup;
    SDL_Rect close_btn;
    SDL_Rect search;
    SDL_Rect list;

    if (!event || !Global_Case_File_Search_Open) {

        return 0;

    }

    popup = case_file_search_popup_rect(win_w, win_h);
    close_btn = (SDL_Rect){popup.x + popup.w - 86, popup.y + 14, 68, 30};
    search = case_file_search_input_rect(popup);
    list = (SDL_Rect){popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};

    if (event->type == SDL_TEXTINPUT) {

        if (Global_Case_File_Search_Active) {

            case_file_search_insert_text(event->text.text);

        }
        return 1;

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;
        int len = (int)strlen(Global_Case_File_Search_Text);

        if (key == SDLK_ESCAPE) {

            case_close_file_search_menu();
            return 1;

        }

        if (key == SDLK_BACKSPACE) {

            case_file_search_backspace();
            return 1;

        }

        if (key == SDLK_DELETE) {

            case_file_search_delete();
            return 1;

        }

        if (key == SDLK_LEFT) {

            if (Global_Case_File_Search_Cursor > 0) {

                Global_Case_File_Search_Cursor--;

            }
            return 1;

        }

        if (key == SDLK_RIGHT) {

            if (Global_Case_File_Search_Cursor < len) {

                Global_Case_File_Search_Cursor++;

            }
            return 1;

        }

        if (key == SDLK_HOME) {

            Global_Case_File_Search_Cursor = 0;
            return 1;

        }

        if (key == SDLK_END) {

            Global_Case_File_Search_Cursor = len;
            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            int index = case_file_search_filtered_index_at(Global_Case_File_Search_Scroll);

            if (index >= 0) {

                case_file_search_select_index(index, 1);

            }
            return 1;

        }

        if (key == SDLK_DOWN) {

            Global_Case_File_Search_Scroll++;
            case_file_search_clamp_scroll();
            return 1;

        }

        if (key == SDLK_UP) {

            Global_Case_File_Search_Scroll--;
            case_file_search_clamp_scroll();
            return 1;

        }

        if (key == SDLK_r) {

            case_scan_case_graph_files();
            return 1;

        }
        return 1;

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mx = 0;
        int my = 0;
        case_get_adjusted_mouse_state(&mx, &my);

        if (case_point_in_rect(mx, my, list)) {

            Global_Case_File_Search_Scroll -= event->wheel.y * 3;
            case_file_search_clamp_scroll();

        }
        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        int mx = 0;
        int my = 0;

        case_get_adjusted_mouse_state(&mx, &my);

        if (!case_point_in_rect(mx, my, popup) || case_point_in_rect(mx, my, close_btn)) {

            case_close_file_search_menu();
            return 1;

        }

        if (case_point_in_rect(mx, my, search)) {

            Global_Case_File_Search_Active = 1;
            return 1;

        }

        Global_Case_File_Search_Active = 0;

        if (case_point_in_rect(mx, my, list)) {

            int row = (my - list.y - 4) / CASE_MGMT_FILE_SEARCH_ROW_H;
            int visible = list.h / CASE_MGMT_FILE_SEARCH_ROW_H;

            if (visible < 1) {

                visible = 1;

            }

            if (visible > 14) {

                visible = 14;

            }

            if (row >= 0 && row < visible) {

                int filtered_index = Global_Case_File_Search_Scroll + row;
                int index = case_file_search_filtered_index_at(filtered_index);

                if (index >= 0) {

                    case_file_search_select_index(index, 1);

                }

            }
            return 1;

        }

        return 1;

    }

    if (event->type == SDL_MOUSEMOTION) {

        return 1;

    }
    return 1;
}

static void case_clamp_description_scroll(SDL_Rect text_rect) {
    /*
        Purpose: Clamps the description scroll
        Returns: No value
    */

    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int starts[128];
    int ends[128];
    int line_count = case_description_build_lines(text, starts, ends);
    int max_lines = (text_rect.h - 12) / 19;
    int max_scroll;

    if (max_lines < 1) {

        max_lines = 1;

    }
    max_scroll = line_count - max_lines;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Case_Description_Popup_Scroll < 0) {

        Global_Case_Description_Popup_Scroll = 0;

    }

    if (Global_Case_Description_Popup_Scroll > max_scroll) {

        Global_Case_Description_Popup_Scroll = max_scroll;

    }
}

static void case_set_description_cursor_from_mouse_scrolled(SDL_Rect rect, int mouse_x, int mouse_y, int first_line) {
    /*
        Purpose: Sets the description cursor from mouse scrolled
        Returns: No value
    */

    char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);
    int starts[128];
    int ends[128];
    int line_h = 19;
    int max_lines = (rect.h - 12) / line_h;
    int line_count = case_description_build_lines(text, starts, ends);

    if (max_lines < 1) {

        max_lines = 1;

    }

    if (first_line < 0) {

        first_line = 0;

    }

    if (first_line > line_count - 1) {

        first_line = line_count - 1;

    }

    int visible_line = (mouse_y - (rect.y + 7)) / line_h;

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
    int rel_x = mouse_x - (rect.x + 9);
    int column = 0;

    for (int i = 0; i <= line_len; i++) {
        int w0 = case_description_range_width(Global_Case_Description_Font, text, (size_t)starts[line],
                                              (size_t)(starts[line] + i));
        int w1 = w0;

        if (i < line_len) {

            w1 = case_description_range_width(Global_Case_Description_Font, text, (size_t)starts[line],
                                              (size_t)(starts[line] + i + 1));

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

    Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION] = starts[line] + column;
}

static int case_source_name_matches_search(const char *name) {
    /*
        Purpose: Checks whether the source name matches the search
        Returns: Boolean status
    */

    char hay[CASE_MGMT_SOURCE_FILE_MAX];
    char needle[sizeof(Global_Case_Source_Search)];
    size_t i;

    if (!name) {

        name = "";

    }

    if (Global_Case_Source_Search[0] == '\0') {

        return 1;

    }

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

static int case_source_filtered_count(void) {
    /*
        Purpose: Counts filtered source results
        Returns: Item count
    */

    int count = 0;
    for (int i = 0; i < Global_Case_Source_File_Count; i++) {

        if (case_source_name_matches_search(Global_Case_Source_Files[i])) {

            count++;

        }
    }
    return count;
}

static int case_source_filtered_index_at(int filtered_index) {
    /*
        Purpose: Gets the source filtered index at a position
        Returns: Item index
    */

    int seen = 0;

    if (filtered_index < 0) {

        return -1;

    }
    for (int i = 0; i < Global_Case_Source_File_Count; i++) {

        if (!case_source_name_matches_search(Global_Case_Source_Files[i])) {

            continue;

        }

        if (seen == filtered_index) {

            return i;

        }
        seen++;
    }
    return -1;
}

static SDL_Rect case_source_search_rect(SDL_Rect popup) {
    /*
        Purpose: Computes the source search rectangle
        Returns: Computed rectangle
    */

    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = {close_btn.x - 292, popup.y + 14, 276, 30};

    if (search.x < popup.x + 140) {

        search.x = popup.x + 140;
        search.w = close_btn.x - search.x - 16;

    }

    if (search.w < 120) {

        search.w = 120;

    }
    return search;
}

static void case_clamp_source_scroll(void) {
    /*
        Purpose: Clamps the source scroll
        Returns: No value
    */

    int visible = CASE_MGMT_SOURCE_VISIBLE;
    int filtered_count = case_source_filtered_count();
    int max_scroll = filtered_count - visible;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Case_Source_Scroll < 0) {

        Global_Case_Source_Scroll = 0;

    }

    if (Global_Case_Source_Scroll > max_scroll) {

        Global_Case_Source_Scroll = max_scroll;

    }
}

static void case_source_short_text(TTF_Font *font, const char *src, char *dst, size_t dst_size, int max_px) {
    /*
        Purpose: Shortens source filenames by rendered width
        Returns: No value
    */

    if (!dst || dst_size == 0) {

        return;

    }

    if (!src) {

        src = "";

    }

    size_t copy_len = strlen(src);

    if (copy_len >= dst_size) {

        copy_len = dst_size - 1;

    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';

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

        if (len >= 3) {

            dst[len - 3] = '.';
            dst[len - 2] = '.';
            dst[len - 1] = '.';
            dst[len] = '\0';

        }

        if (TTF_SizeText(font, dst, &text_w, &text_h) != 0 || text_w <= max_px) {

            return;

        }

        if (len >= 3) {

            dst[len - 3] = '\0';

        }
    }

    snprintf(dst, dst_size, "...");
}

static void case_wrap_block_text_two_lines(TTF_Font *font, const char *src, char *line_one, size_t line_one_size,
                                           char *line_two, size_t line_two_size, int max_px) {
    /*
        Purpose: Wraps block text across at most two rendered-width-limited lines
        Returns: No value
    */

    char probe[CASE_MGMT_TEXT_MAX];
    size_t src_len;
    size_t fit = 0;
    size_t last_space = 0;
    size_t split;
    size_t second_start;
    int text_w = 0;
    int text_h = 0;

    if (!line_one || line_one_size == 0 || !line_two || line_two_size == 0) {

        return;

    }

    line_one[0] = '\0';
    line_two[0] = '\0';

    if (!src || !src[0]) {

        return;

    }

    if (!font || max_px <= 0) {

        case_shorten(src, line_one, line_one_size, 30);
        return;

    }

    src_len = strlen(src);

    if (src_len >= sizeof(probe)) {

        src_len = sizeof(probe) - 1;

    }

    memcpy(probe, src, src_len);
    probe[src_len] = '\0';

    if (TTF_SizeText(font, probe, &text_w, &text_h) == 0 && text_w <= max_px) {

        case_copy_text(line_one, line_one_size, probe);
        return;

    }

    for (size_t i = 1; i <= src_len; i++) {
        char saved = probe[i];
        probe[i] = '\0';

        if (TTF_SizeText(font, probe, &text_w, &text_h) != 0 || text_w > max_px) {

            probe[i] = saved;
            break;

        }

        fit = i;

        if (probe[i - 1] == ' ' || probe[i - 1] == '\t') {

            last_space = i - 1;

        }

        probe[i] = saved;
    }

    if (fit == 0) {

        fit = 1;

    }

    split = last_space > 0 ? last_space : fit;

    while (split > 0 && (probe[split - 1] == ' ' || probe[split - 1] == '\t')) {
        split--;
    }

    if (split >= line_one_size) {

        split = line_one_size - 1;

    }

    memcpy(line_one, probe, split);
    line_one[split] = '\0';

    second_start = last_space > 0 ? last_space + 1 : fit;

    while (second_start < src_len && (probe[second_start] == ' ' || probe[second_start] == '\t')) {
        second_start++;
    }

    if (second_start < src_len) {

        case_source_short_text(font, probe + second_start, line_two, line_two_size, max_px);

    }
}

static void case_draw_source_modal_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label,
                                          int hovered) {
    /*
        Purpose: Draws an Analysis-style source-search modal button
        Returns: No value
    */

    SDL_Color fill = hovered ? (SDL_Color){0, 44, 16, 255} : (SDL_Color){0, 8, 3, 255};
    SDL_Color border = hovered ? Case_Border_Hi : Case_Border;
    SDL_Color text = hovered ? (SDL_Color){235, 255, 240, 255} : Case_Text;

    if (hovered) {

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_Rect glow = {rect.x - 4, rect.y - 4, rect.w + 8, rect.h + 8};
        draw_filled_rect(renderer, glow, (SDL_Color){0, 255, 90, 38});
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    }

    draw_filled_rect(renderer, rect, fill);
    draw_outline_rect(renderer, rect, border);
    case_draw_text_centered(renderer, font, label, rect, text);
}

static void case_close_source_file_search_menu(void) {
    /*
        Purpose: Closes the source-file search menu
        Returns: No value
    */

    Global_Case_Source_Popup_Open = 0;
    Global_Case_Source_Search_Active = 0;
    Global_Case_Source_Hover = -1;
}

static void case_open_source_file_search_menu(void) {
    /*
        Purpose: Opens the source-file search menu using Analysis-style behavior
        Returns: No value
    */

    case_scan_source_files();
    Global_Case_Source_Popup_Open = 1;
    Global_Case_Source_Search_Active = 1;
    Global_Case_Source_Hover = -1;
    Global_Case_Source_Search[0] = '\0';
    Global_Case_Source_Search_Cursor = 0;
    Global_Case_Source_Scroll = 0;
    Global_Case_Status_Dropdown_Open = 0;
    Global_Case_Case_Dropdown_Open = 0;
    Global_Case_Country_Dropdown_Open = 0;
    Global_Case_User_Dropdown_Open = 0;
    Global_Case_Calendar_Open = 0;
    Global_Case_Description_Popup_Open = 0;
    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;

    case_set_status("Source filename search menu opened", Case_Text);
}

static void case_source_file_search_select_index(int source_index) {
    /*
        Purpose: Selects one source filename from the filtered search results
        Returns: No value
    */

    if (source_index < 0 || source_index >= Global_Case_Source_File_Count) {

        return;

    }

    char *source = case_selected_field_text(CASE_MGMT_FIELD_SOURCE_FILE);

    if (source) {

        case_push_undo_state();
        case_copy_text(source, CASE_MGMT_SOURCE_FILE_MAX, Global_Case_Source_Files[source_index]);
        Global_Case_Field_Cursor[CASE_MGMT_FIELD_SOURCE_FILE] = (int)strlen(source);

    }

    case_close_source_file_search_menu();
    case_set_status("Source file selected", Case_Text);
}

static int case_handle_source_popup_click(int mx, int my, int win_w, int win_h) {
    /*
        Purpose: Handles the Analysis-style source filename search click
        Returns: Handling status
    */

    SDL_Rect popup = case_source_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = case_source_search_rect(popup);
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};

    if (!Global_Case_Source_Popup_Open) {

        return 0;

    }

    if (!case_point_in_rect(mx, my, popup) || case_point_in_rect(mx, my, close_btn)) {

        case_close_source_file_search_menu();
        return 1;

    }

    if (case_point_in_rect(mx, my, search)) {

        Global_Case_Source_Search_Active = 1;
        return 1;

    }

    Global_Case_Source_Search_Active = 0;

    if (case_point_in_rect(mx, my, list) && Global_Case_Source_File_Count > 0) {

        int row = (my - list.y - 4) / CASE_MGMT_FILE_SEARCH_ROW_H;
        int visible = list.h / CASE_MGMT_FILE_SEARCH_ROW_H;

        if (visible < 1) {

            visible = 1;

        }

        if (visible > 14) {

            visible = 14;

        }

        if (row >= 0 && row < visible) {

            int filtered_index = Global_Case_Source_Scroll + row;
            int source_index = case_source_filtered_index_at(filtered_index);

            if (source_index >= 0) {

                case_source_file_search_select_index(source_index);

            }

        }

        return 1;

    }

    return 1;
}

static int case_handle_status_dropdown_click(int mx, int my, SDL_Rect status_field) {
    /*
        Purpose: Handles the status dropdown click
        Returns: Handling status
    */

    SDL_Rect menu = case_status_dropdown_rect(status_field);

    if (!Global_Case_Status_Dropdown_Open) {

        return 0;

    }

    if (!case_point_in_rect(mx, my, menu)) {

        Global_Case_Status_Dropdown_Open = 0;
        return 0;

    }

    int index = (my - menu.y) / CASE_MGMT_STATUS_OPTION_H;

    if (index >= 0 && index < CASE_MGMT_STATUS_COUNT) {

        char *status = case_selected_field_text(CASE_MGMT_FIELD_STATUS);

        if (status) {

            case_copy_text(status, CASE_MGMT_TEXT_MAX, CASE_MGMT_STATUS_OPTIONS[index]);

        }
        Global_Case_Status_Dropdown_Open = 0;
        Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
        return 1;

    }

    return 0;
}

static int case_handle_calendar_click(int mx, int my, SDL_Rect date_field) {
    /*
        Purpose: Handles the calendar click
        Returns: Handling status
    */

    SDL_Rect cal = case_calendar_rect(date_field);

    if (!Global_Case_Calendar_Open) {

        return 0;

    }

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

static SDL_Rect case_case_dropdown_rect(SDL_Rect field, int visible) {
    /*
        Purpose: Computes the case dropdown rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {field.x, field.y + field.h + 4, field.w, visible * CASE_MGMT_CASE_OPTION_H};
    return r;
}

static SDL_Rect case_country_dropdown_rect(SDL_Rect field, int visible) {
    /*
        Purpose: Computes the country dropdown rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {field.x, field.y + field.h + 4, field.w, visible * CASE_MGMT_COUNTRY_OPTION_H};
    return r;
}

static SDL_Rect case_user_dropdown_rect(SDL_Rect field, int visible) {
    /*
        Purpose: Computes the user dropdown rectangle
        Returns: Computed rectangle
    */

    SDL_Rect r = {field.x, field.y + field.h + 4, field.w, visible * CASE_MGMT_USER_OPTION_H};
    return r;
}

static int case_handle_case_dropdown_click(int mx, int my, SDL_Rect field) {
    /*
        Purpose: Handles the case dropdown click
        Returns: Handling status
    */

    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count;
    int visible;
    SDL_Rect menu;

    if (!Global_Case_Case_Dropdown_Open) {

        return 0;

    }
    count = case_build_case_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);

    if (count <= 0) {

        Global_Case_Case_Dropdown_Open = 0;
        return 0;

    }

    visible = count - Global_Case_Case_Scroll;

    if (visible > CASE_MGMT_CASE_MAX_VISIBLE) {

        visible = CASE_MGMT_CASE_MAX_VISIBLE;

    }

    if (visible < 0) {

        visible = 0;

    }
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

static int case_handle_country_dropdown_click(int mx, int my, SDL_Rect field) {
    /*
        Purpose: Handles the country dropdown click
        Returns: Handling status
    */

    int matches[512];
    int count;
    int visible;
    SDL_Rect menu;

    if (!Global_Case_Country_Dropdown_Open) {

        return 0;

    }
    count = case_build_country_matches(matches, (int)(sizeof(matches) / sizeof(matches[0])));

    if (count <= 0) {

        Global_Case_Country_Dropdown_Open = 0;
        return 0;

    }

    visible = count - Global_Case_Country_Scroll;

    if (visible > CASE_MGMT_COUNTRY_MAX_VISIBLE) {

        visible = CASE_MGMT_COUNTRY_MAX_VISIBLE;

    }

    if (visible < 0) {

        visible = 0;

    }
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

static int case_handle_user_dropdown_click(int mx, int my, SDL_Rect field) {
    /*
        Purpose: Handles the user dropdown click
        Returns: Handling status
    */

    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count;
    int visible;
    SDL_Rect menu;

    if (!Global_Case_User_Dropdown_Open || Global_Case_Active_Field != CASE_MGMT_FIELD_USER) {

        return 0;

    }

    count = case_build_user_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);

    if (count <= 0) {

        return 0;

    }

    visible = count - Global_Case_User_Scroll;

    if (visible > CASE_MGMT_USER_MAX_VISIBLE) {

        visible = CASE_MGMT_USER_MAX_VISIBLE;

    }

    if (visible < 0) {

        visible = 0;

    }
    menu = case_user_dropdown_rect(field, visible);

    if (!case_point_in_rect(mx, my, menu)) {

        Global_Case_User_Dropdown_Open = 0;
        Global_Case_User_Keyboard_Pos = -1;
        return 0;

    }

    int row = (my - menu.y) / CASE_MGMT_USER_OPTION_H;
    int pos = Global_Case_User_Scroll + row;

    if (row >= 0 && row < visible && pos >= 0 && pos < count) {

        case_select_user_option(matches[pos]);
        case_set_status("Assigned user selected", Case_Text);
        return 1;

    }
    return 1;
}

static void case_clamp_case_scroll(void) {
    /*
        Purpose: Clamps the case scroll
        Returns: No value
    */

    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count = case_build_case_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
    int max_scroll = count - CASE_MGMT_CASE_MAX_VISIBLE;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Case_Case_Scroll < 0) {

        Global_Case_Case_Scroll = 0;

    }

    if (Global_Case_Case_Scroll > max_scroll) {

        Global_Case_Case_Scroll = max_scroll;

    }
}

static void case_clamp_country_scroll(void) {
    /*
        Purpose: Clamps the country scroll
        Returns: No value
    */

    int matches[512];
    int count = case_build_country_matches(matches, (int)(sizeof(matches) / sizeof(matches[0])));
    int max_scroll = count - CASE_MGMT_COUNTRY_MAX_VISIBLE;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Case_Country_Scroll < 0) {

        Global_Case_Country_Scroll = 0;

    }

    if (Global_Case_Country_Scroll > max_scroll) {

        Global_Case_Country_Scroll = max_scroll;

    }
}

static int case_metadata_is_document_name(const char *name) {
    /*
        Purpose: Checks whether a document stores case metadata
        Returns: Boolean status
    */

    return name && strncmp(name, CASE_MGMT_METADATA_PREFIX, strlen(CASE_MGMT_METADATA_PREFIX)) == 0;
}

static uint64_t case_metadata_hash(const char *text) {
    /*
        Purpose: Calculates a stable case metadata hash
        Returns: Hash value
    */

    uint64_t hash = UINT64_C(1469598103934665603);
    const unsigned char *p = (const unsigned char *)(text ? text : "");

    while (*p) {
        hash ^= (uint64_t)*p++;
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

static void case_metadata_document_name(const char *case_number, char *out, size_t out_size) {
    /*
        Purpose: Builds the case metadata document name
        Returns: No value
    */

    if (!out || out_size == 0) {

        return;

    }

    snprintf(out, out_size, "%s%016llx.meta", CASE_MGMT_METADATA_PREFIX,
             (unsigned long long)case_metadata_hash(case_number));
}

static int case_metadata_compare(const void *a, const void *b) {
    /*
        Purpose: Compares case metadata records for sorting
        Returns: Sort order
    */

    const Type_Case_Metadata_Info *left = (const Type_Case_Metadata_Info *)a;
    const Type_Case_Metadata_Info *right = (const Type_Case_Metadata_Info *)b;
    return case_name_compare(left->case_number, right->case_number);
}

static int case_metadata_find(const char *case_number) {
    /*
        Purpose: Finds a case metadata record
        Returns: Item index
    */

    if (!case_number || !case_number[0]) {

        return -1;

    }

    for (int i = 0; i < Global_Case_Metadata_Count; i++) {

        if (case_text_equals_ci(Global_Case_Metadata[i].case_number, case_number)) {

            return i;

        }
    }

    return -1;
}

static int case_metadata_add(const char *case_number) {
    /*
        Purpose: Adds a case metadata record when needed
        Returns: Item index
    */

    int existing;

    if (!case_number || !case_number[0]) {

        return -1;

    }

    existing = case_metadata_find(case_number);

    if (existing >= 0) {

        return existing;

    }

    if (Global_Case_Metadata_Count >= CASE_MGMT_METADATA_MAX_CASES) {

        return -1;

    }

    existing = Global_Case_Metadata_Count++;
    memset(&Global_Case_Metadata[existing], 0, sizeof(Global_Case_Metadata[existing]));
    case_copy_text(Global_Case_Metadata[existing].case_number, sizeof(Global_Case_Metadata[existing].case_number),
                   case_number);
    return existing;
}

static int case_metadata_parse_content(const unsigned char *content, size_t content_size, char *case_number,
                                       size_t case_number_size, char *description, size_t description_size) {
    /*
        Purpose: Parses a stored case metadata document
        Returns: Success status
    */

    const unsigned char *newline;
    size_t name_size;
    size_t desc_size;

    if (!content || !case_number || case_number_size == 0 || !description || description_size == 0) {

        return 0;

    }

    case_number[0] = '\0';
    description[0] = '\0';
    newline = memchr(content, '\n', content_size);

    if (!newline) {

        return 0;

    }

    name_size = (size_t)(newline - content);

    if (name_size >= case_number_size) {

        name_size = case_number_size - 1;

    }
    memcpy(case_number, content, name_size);
    case_number[name_size] = '\0';
    case_trim_text(case_number);

    desc_size = content_size - (size_t)(newline + 1 - content);

    if (desc_size >= description_size) {

        desc_size = description_size - 1;

    }
    memcpy(description, newline + 1, desc_size);
    description[desc_size] = '\0';

    return case_number[0] != '\0';
}

static void case_metadata_sync_editor(void) {
    /*
        Purpose: Synchronizes the case metadata editor with the selected case
        Returns: No value
    */

    if (Global_Case_Metadata_Selected < 0 || Global_Case_Metadata_Selected >= Global_Case_Metadata_Count) {

        Global_Case_Metadata_Edit_Name[0] = '\0';
        Global_Case_Metadata_Edit_Description[0] = '\0';
        Global_Case_Metadata_Name_Cursor = 0;
        Global_Case_Metadata_Description_Cursor = 0;
        return;

    }

    case_copy_text(Global_Case_Metadata_Edit_Name, sizeof(Global_Case_Metadata_Edit_Name),
                   Global_Case_Metadata[Global_Case_Metadata_Selected].case_number);
    case_copy_text(Global_Case_Metadata_Edit_Description, sizeof(Global_Case_Metadata_Edit_Description),
                   Global_Case_Metadata[Global_Case_Metadata_Selected].description);
    Global_Case_Metadata_Name_Cursor = (int)strlen(Global_Case_Metadata_Edit_Name);
    Global_Case_Metadata_Description_Cursor = (int)strlen(Global_Case_Metadata_Edit_Description);
}

static void case_metadata_refresh(void) {
    /*
        Purpose: Refreshes the searchable case metadata list
        Returns: No value
    */

    static Type_DataStore_Document_Summary documents[CASE_MGMT_SOURCE_MAX_FILES];
    char selected_case[128] = "";
    char database_error[256] = "";
    size_t document_count = 0;

    if (Global_Case_Metadata_Selected >= 0 && Global_Case_Metadata_Selected < Global_Case_Metadata_Count) {

        case_copy_text(selected_case, sizeof(selected_case),
                       Global_Case_Metadata[Global_Case_Metadata_Selected].case_number);

    }

    memset(Global_Case_Metadata, 0, sizeof(Global_Case_Metadata));
    Global_Case_Metadata_Count = 0;

    if (DATASTORE_list_documents(DATASTORE_KIND_CLASSIFICATION, documents, sizeof(documents) / sizeof(documents[0]),
                                 &document_count, database_error, sizeof(database_error))) {

        for (size_t i = 0; i < document_count; i++) {
            int index = case_metadata_add(documents[i].case_number);

            if (index >= 0) {

                Global_Case_Metadata[index].classification_count++;

            }
        }

    }

    document_count = 0;
    database_error[0] = '\0';

    if (DATASTORE_list_documents(DATASTORE_KIND_CASE_MANAGEMENT, documents, sizeof(documents) / sizeof(documents[0]),
                                 &document_count, database_error, sizeof(database_error))) {

        for (size_t i = 0; i < document_count; i++) {

            if (case_metadata_is_document_name(documents[i].document_name)) {

                unsigned char *content = NULL;
                size_t content_size = 0;
                int found = 0;
                char case_number[128] = "";
                char description[CASE_MGMT_METADATA_DESCRIPTION_MAX] = "";
                int index;

                if (!DATASTORE_load_content(DATASTORE_KIND_CASE_MANAGEMENT, documents[i].document_name, &content,
                                            &content_size, &found, database_error, sizeof(database_error)) ||
                    !found ||
                    !case_metadata_parse_content(content, content_size, case_number, sizeof(case_number), description,
                                                 sizeof(description))) {

                    DATASTORE_free_content(content, content_size);
                    continue;

                }

                index = case_metadata_add(case_number);

                if (index >= 0) {

                    case_copy_text(Global_Case_Metadata[index].description,
                                   sizeof(Global_Case_Metadata[index].description), description);
                    case_copy_text(Global_Case_Metadata[index].document_name,
                                   sizeof(Global_Case_Metadata[index].document_name), documents[i].document_name);

                }
                DATASTORE_free_content(content, content_size);

            }

            else if (documents[i].case_number[0]) {

                int index = case_metadata_add(documents[i].case_number);

                if (index >= 0) {

                    Global_Case_Metadata[index].graph_count++;

                }

            }
        }

    }

    if (Global_Case_Metadata_Count > 1) {

        qsort(Global_Case_Metadata, (size_t)Global_Case_Metadata_Count, sizeof(Global_Case_Metadata[0]),
              case_metadata_compare);

    }

    Global_Case_Metadata_Selected = selected_case[0] ? case_metadata_find(selected_case) : -1;

    if (Global_Case_Metadata_Selected < 0 && Global_Case_Metadata_Count > 0 && !Global_Case_Metadata_Creating) {

        Global_Case_Metadata_Selected = 0;

    }

    if (!Global_Case_Metadata_Creating && !Global_Case_Metadata_Renaming && !Global_Case_Metadata_Name_Active &&
        !Global_Case_Metadata_Description_Active) {

        case_metadata_sync_editor();

    }

    if (Global_Case_Metadata_Scroll < 0) {

        Global_Case_Metadata_Scroll = 0;

    }

    if (Global_Case_Metadata_Scroll >= Global_Case_Metadata_Count) {

        Global_Case_Metadata_Scroll = Global_Case_Metadata_Count > 0 ? Global_Case_Metadata_Count - 1 : 0;

    }

    Global_Case_Metadata_Last_Refresh = SDL_GetTicks64();
}

static int case_metadata_filtered_index_at(int filtered_position) {
    /*
        Purpose: Gets a filtered case metadata index
        Returns: Item index
    */

    int position = 0;

    for (int i = 0; i < Global_Case_Metadata_Count; i++) {

        if (Global_Case_Metadata_Search[0] &&
            !case_text_contains_ci(Global_Case_Metadata[i].case_number, Global_Case_Metadata_Search)) {

            continue;

        }

        if (position == filtered_position) {

            return i;

        }
        position++;
    }

    return -1;
}

static int case_metadata_filtered_count(void) {
    /*
        Purpose: Counts filtered case metadata records
        Returns: Item count
    */

    int count = 0;
    while (case_metadata_filtered_index_at(count) >= 0) {
        count++;
    }
    return count;
}

static void case_metadata_clamp_scroll(void) {
    /*
        Purpose: Clamps the case metadata list scroll position
        Returns: No value
    */

    int count = case_metadata_filtered_count();
    int max_scroll = count - CASE_MGMT_METADATA_VISIBLE_ROWS;

    if (max_scroll < 0) {

        max_scroll = 0;

    }

    if (Global_Case_Metadata_Scroll < 0) {

        Global_Case_Metadata_Scroll = 0;

    }

    if (Global_Case_Metadata_Scroll > max_scroll) {

        Global_Case_Metadata_Scroll = max_scroll;

    }
}

static void case_metadata_select(int index) {
    /*
        Purpose: Selects a case metadata record
        Returns: No value
    */

    if (index < 0 || index >= Global_Case_Metadata_Count) {

        return;

    }

    Global_Case_Metadata_Selected = index;
    Global_Case_Metadata_Creating = 0;
    Global_Case_Metadata_Renaming = 0;
    Global_Case_Metadata_Name_Active = 0;
    Global_Case_Metadata_Description_Active = 0;
    case_metadata_sync_editor();
}

static void case_metadata_begin_create(void) {
    /*
        Purpose: Starts creating a new case record
        Returns: No value
    */

    Global_Case_Metadata_Selected = -1;
    Global_Case_Metadata_Creating = 1;
    Global_Case_Metadata_Renaming = 0;
    Global_Case_Metadata_Edit_Name[0] = '\0';
    Global_Case_Metadata_Edit_Description[0] = '\0';
    Global_Case_Metadata_Name_Cursor = 0;
    Global_Case_Metadata_Description_Cursor = 0;
    Global_Case_Metadata_Name_Active = 1;
    Global_Case_Metadata_Description_Active = 0;
}

static void case_metadata_begin_rename(void) {
    /*
        Purpose: Starts renaming the selected case record
        Returns: No value
    */

    if (Global_Case_Metadata_Selected < 0 || Global_Case_Metadata_Selected >= Global_Case_Metadata_Count) {

        return;

    }

    Global_Case_Metadata_Renaming = 1;
    Global_Case_Metadata_Creating = 0;
    Global_Case_Metadata_Name_Active = 1;
    Global_Case_Metadata_Name_Cursor = (int)strlen(Global_Case_Metadata_Edit_Name);
}

static int case_metadata_save_document(const char *case_number, const char *description, char *document_name,
                                       size_t document_name_size) {
    /*
        Purpose: Saves a case metadata document to the database
        Returns: Success status
    */

    char normalized[128];
    char database_error[256] = "";
    char generated_name[256];
    char *content = NULL;
    size_t content_size = 0;
    FILE *fp;

    case_copy_text(normalized, sizeof(normalized), case_number);
    case_trim_text(normalized);

    if (!normalized[0] || strchr(normalized, '\n') || strchr(normalized, '\r')) {

        case_set_status("Enter a valid case number", Case_Red);
        return 0;

    }

    case_metadata_document_name(normalized, generated_name, sizeof(generated_name));
    fp = open_memstream(&content, &content_size);

    if (!fp) {

        case_set_status("Unable to create case metadata", Case_Red);
        return 0;

    }

    fprintf(fp, "%s\n", normalized);

    if (description && description[0]) {

        fwrite(description, 1, strlen(description), fp);

    }

    if (fclose(fp) != 0) {

        free(content);
        case_set_status("Unable to finalize case metadata", Case_Red);
        return 0;

    }

    if (!DATASTORE_save_content(DATASTORE_KIND_CASE_MANAGEMENT, generated_name, normalized, content, content_size,
                                database_error, sizeof(database_error))) {

        free(content);
        {
            char message[384];
            snprintf(message, sizeof(message), "Unable to save case metadata: %.280s", database_error);
            case_set_status(message, Case_Red);
        }
        return 0;

    }

    free(content);

    if (document_name && document_name_size > 0) {

        case_copy_text(document_name, document_name_size, generated_name);

    }
    return 1;
}

static size_t case_metadata_first_csv_field_end(const char *line, size_t line_size) {
    /*
        Purpose: Finds the end of the first CSV field
        Returns: Byte offset
    */

    int quoted = 0;

    for (size_t i = 0; i < line_size; i++) {

        if (line[i] == '"') {

            if (quoted && i + 1 < line_size && line[i + 1] == '"') {

                i++;
                continue;

            }
            quoted = !quoted;

        }

        else if (line[i] == ',' && !quoted) {

            return i;

        }
    }

    return line_size;
}

static int case_metadata_rewrite_classification(const unsigned char *content, size_t content_size, const char *new_case,
                                                unsigned char **updated, size_t *updated_size) {
    /*
        Purpose: Rewrites classification rows with a new case number
        Returns: Success status
    */

    char *buffer = NULL;
    size_t buffer_size = 0;
    FILE *fp;
    size_t offset = 0;
    int first = 1;

    if (!content || !new_case || !updated || !updated_size) {

        return 0;

    }

    *updated = NULL;
    *updated_size = 0;
    fp = open_memstream(&buffer, &buffer_size);

    if (!fp) {

        return 0;

    }

    while (offset < content_size) {
        const char *line = (const char *)content + offset;
        const char *newline = memchr(line, '\n', content_size - offset);
        size_t line_size = newline ? (size_t)(newline - line) : content_size - offset;

        if (first || (line_size >= 12 && strncmp(line, "case_number", 11) == 0)) {

            fwrite(line, 1, line_size, fp);

        }

        else {

            size_t field_end = case_metadata_first_csv_field_end(line, line_size);

            if (field_end < line_size) {

                case_csv_write_field(fp, new_case);
                fwrite(line + field_end, 1, line_size - field_end, fp);

            }

            else {

                fwrite(line, 1, line_size, fp);

            }

        }

        if (newline) {

            fputc('\n', fp);
            offset += line_size + 1;

        }

        else {

            offset += line_size;

        }
        first = 0;
    }

    if (fclose(fp) != 0) {

        free(buffer);
        return 0;

    }

    *updated = (unsigned char *)buffer;
    *updated_size = buffer_size;
    return 1;
}

static void case_metadata_classification_name(const char *case_number, char *out, size_t out_size) {
    /*
        Purpose: Builds a classification document name for a case
        Returns: No value
    */

    char safe[128];
    size_t write_index = 0;

    if (!out || out_size == 0) {

        return;

    }

    for (const unsigned char *p = (const unsigned char *)(case_number ? case_number : "");
         *p && write_index + 1 < sizeof(safe); p++) {
        unsigned char c = *p;
        safe[write_index++] = (char)((isalnum(c) || c == '-' || c == '_') ? c : '_');
    }
    safe[write_index] = '\0';

    if (!safe[0]) {

        case_copy_text(safe, sizeof(safe), "UNNAMED");

    }

    snprintf(out, out_size, "CASE_%s.csv", safe);
}

static void case_metadata_asset_document_name(const char *prefix, const char *case_number, char *document_name,
                                              size_t document_name_size) {
    /*
        Purpose: Builds a deterministic case asset document name
        Returns: No value
    */

    if (!document_name || document_name_size == 0) {

        return;

    }

    snprintf(document_name, document_name_size, "%s%s", prefix ? prefix : "",
             case_number && case_number[0] ? case_number : "UNCASED");
}

static int case_metadata_rename_asset(const char *document_kind, const char *document_prefix, const char *old_case,
                                      const char *new_case) {
    /*
        Purpose: Moves a name-keyed encrypted case asset during a case rename
        Returns: Success status
    */

    unsigned char *content = NULL;
    size_t content_size = 0;
    int found = 0;
    int deleted = 0;
    char old_document[256];
    char new_document[256];
    char database_error[256] = "";

    case_metadata_asset_document_name(document_prefix, old_case, old_document, sizeof(old_document));
    case_metadata_asset_document_name(document_prefix, new_case, new_document, sizeof(new_document));

    if (!DATASTORE_load_content(document_kind, old_document, &content, &content_size, &found, database_error,
                                sizeof(database_error))) {

        DATASTORE_free_content(content, content_size);
        return 0;

    }

    if (!found) {

        DATASTORE_free_content(content, content_size);
        return 1;

    }

    if (!DATASTORE_save_content(document_kind, new_document, new_case, content, content_size, database_error,
                                sizeof(database_error))) {

        DATASTORE_free_content(content, content_size);
        return 0;

    }

    DATASTORE_free_content(content, content_size);

    if (strcmp(old_document, new_document) != 0 &&
        !DATASTORE_delete_content(document_kind, old_document, &deleted, database_error, sizeof(database_error))) {

        return 0;

    }

    return 1;
}

static int case_metadata_rename_classifications(const char *old_case, const char *new_case) {
    /*
        Purpose: Renames classification database records for a case
        Returns: Success status
    */

    static Type_DataStore_Document_Summary documents[CASE_MGMT_SOURCE_MAX_FILES];
    char database_error[256] = "";
    size_t document_count = 0;

    if (!DATASTORE_list_documents(DATASTORE_KIND_CLASSIFICATION, documents, sizeof(documents) / sizeof(documents[0]),
                                  &document_count, database_error, sizeof(database_error))) {

        return 0;

    }

    for (size_t i = 0; i < document_count; i++) {
        unsigned char *content = NULL;
        unsigned char *updated = NULL;
        size_t content_size = 0;
        size_t updated_size = 0;
        int found = 0;
        int deleted = 0;
        char new_document[256];

        if (!case_text_equals_ci(documents[i].case_number, old_case)) {

            continue;

        }

        if (!DATASTORE_load_content(DATASTORE_KIND_CLASSIFICATION, documents[i].document_name, &content, &content_size,
                                    &found, database_error, sizeof(database_error)) ||
            !found || !case_metadata_rewrite_classification(content, content_size, new_case, &updated, &updated_size)) {

            DATASTORE_free_content(content, content_size);
            free(updated);
            return 0;

        }

        case_metadata_classification_name(new_case, new_document, sizeof(new_document));

        if (!DATASTORE_save_content(DATASTORE_KIND_CLASSIFICATION, new_document, new_case, updated, updated_size,
                                    database_error, sizeof(database_error))) {

            DATASTORE_free_content(content, content_size);
            free(updated);
            return 0;

        }

        if (strcmp(new_document, documents[i].document_name) != 0 &&
            !DATASTORE_delete_content(DATASTORE_KIND_CLASSIFICATION, documents[i].document_name, &deleted,
                                      database_error, sizeof(database_error))) {

            DATASTORE_free_content(content, content_size);
            free(updated);
            return 0;

        }

        DATASTORE_free_content(content, content_size);
        free(updated);
    }

    return 1;
}

static int case_metadata_rewrite_graph(const unsigned char *content, size_t content_size, const char *old_case,
                                       const char *new_case, unsigned char **updated, size_t *updated_size) {
    /*
        Purpose: Rewrites case graph blocks with a new case number
        Returns: Success status
    */

    char *buffer = NULL;
    size_t buffer_size = 0;
    FILE *fp;
    size_t offset = 0;
    int in_blocks = 0;

    if (!content || !old_case || !new_case || !updated || !updated_size) {

        return 0;

    }

    *updated = NULL;
    *updated_size = 0;
    fp = open_memstream(&buffer, &buffer_size);

    if (!fp) {

        return 0;

    }

    while (offset < content_size) {
        const char *line = (const char *)content + offset;
        const char *newline = memchr(line, '\n', content_size - offset);
        size_t line_size = newline ? (size_t)(newline - line) : content_size - offset;
        int rewritten = 0;

        if (line_size == 8 && strncmp(line, "[BLOCKS]", 8) == 0) {

            in_blocks = 1;

        }

        else if (line_size > 0 && line[0] == '[' && !(line_size == 8 && strncmp(line, "[BLOCKS]", 8) == 0)) {

            in_blocks = 0;

        }

        else if (in_blocks && line_size > 0 && strncmp(line, "id,type,x,y,", 12) != 0) {

            size_t comma = 0;
            int comma_count = 0;

            while (comma < line_size && comma_count < 4) {

                if (line[comma] == ',') {

                    comma_count++;

                }
                comma++;
            }

            if (comma_count == 4 && comma < line_size) {

                size_t field_end = case_metadata_first_csv_field_end(line + comma, line_size - comma);
                char *copy = malloc(line_size - comma + 1);

                if (copy) {

                    char field[128] = "";
                    char *cursor;
                    memcpy(copy, line + comma, line_size - comma);
                    copy[line_size - comma] = '\0';
                    cursor = copy;
                    case_read_csv_field(&cursor, field, sizeof(field));

                    if (case_text_equals_ci(field, old_case) && field_end < line_size - comma) {

                        fwrite(line, 1, comma, fp);
                        case_csv_write_field(fp, new_case);
                        fwrite(line + comma + field_end, 1, line_size - comma - field_end, fp);
                        rewritten = 1;

                    }
                    free(copy);

                }

            }

        }

        if (!rewritten) {

            fwrite(line, 1, line_size, fp);

        }

        if (newline) {

            fputc('\n', fp);
            offset += line_size + 1;

        }

        else {

            offset += line_size;

        }
    }

    if (fclose(fp) != 0) {

        free(buffer);
        return 0;

    }

    *updated = (unsigned char *)buffer;
    *updated_size = buffer_size;
    return 1;
}

static int case_metadata_rename_graphs(const char *old_case, const char *new_case) {
    /*
        Purpose: Renames case numbers inside stored case graphs
        Returns: Success status
    */

    static Type_DataStore_Document_Summary documents[CASE_MGMT_SOURCE_MAX_FILES];
    char database_error[256] = "";
    size_t document_count = 0;

    if (!DATASTORE_list_documents(DATASTORE_KIND_CASE_MANAGEMENT, documents, sizeof(documents) / sizeof(documents[0]),
                                  &document_count, database_error, sizeof(database_error))) {

        return 0;

    }

    for (size_t i = 0; i < document_count; i++) {
        unsigned char *content = NULL;
        unsigned char *updated = NULL;
        size_t content_size = 0;
        size_t updated_size = 0;
        int found = 0;

        if (case_metadata_is_document_name(documents[i].document_name) ||
            !case_text_equals_ci(documents[i].case_number, old_case)) {

            continue;

        }

        if (!DATASTORE_load_content(DATASTORE_KIND_CASE_MANAGEMENT, documents[i].document_name, &content, &content_size,
                                    &found, database_error, sizeof(database_error)) ||
            !found ||
            !case_metadata_rewrite_graph(content, content_size, old_case, new_case, &updated, &updated_size) ||
            !DATASTORE_save_content(DATASTORE_KIND_CASE_MANAGEMENT, documents[i].document_name, new_case, updated,
                                    updated_size, database_error, sizeof(database_error))) {

            DATASTORE_free_content(content, content_size);
            free(updated);
            return 0;

        }

        DATASTORE_free_content(content, content_size);
        free(updated);
    }

    for (int i = 0; i < Global_Case_Block_Count; i++) {

        if (case_text_equals_ci(Global_Case_Blocks[i].case_number, old_case)) {

            case_copy_text(Global_Case_Blocks[i].case_number, sizeof(Global_Case_Blocks[i].case_number), new_case);

        }
    }

    return 1;
}

static int case_metadata_rename_selected(void) {
    /*
        Purpose: Renames the selected case and its stored records
        Returns: Success status
    */

    char old_case[128];
    char new_case[128];
    char old_document[256] = "";
    char new_document[256] = "";
    char database_error[256] = "";
    int deleted = 0;
    int duplicate;

    if (Global_Case_Metadata_Selected < 0 || Global_Case_Metadata_Selected >= Global_Case_Metadata_Count) {

        return 0;

    }

    case_copy_text(old_case, sizeof(old_case), Global_Case_Metadata[Global_Case_Metadata_Selected].case_number);
    case_copy_text(old_document, sizeof(old_document),
                   Global_Case_Metadata[Global_Case_Metadata_Selected].document_name);
    case_copy_text(new_case, sizeof(new_case), Global_Case_Metadata_Edit_Name);
    case_trim_text(new_case);

    if (!new_case[0]) {

        case_set_status("Enter a case number before renaming", Case_Red);
        return 0;

    }

    duplicate = case_metadata_find(new_case);

    if (duplicate >= 0 && duplicate != Global_Case_Metadata_Selected) {

        case_set_status("A case with that number already exists", Case_Red);
        return 0;

    }

    if (!case_metadata_save_document(new_case, Global_Case_Metadata_Edit_Description, new_document,
                                     sizeof(new_document)) ||
        !case_metadata_rename_classifications(old_case, new_case) || !case_metadata_rename_graphs(old_case, new_case) ||
        !case_metadata_rename_asset(CASE_MGMT_IMAGE_KIND, CASE_MGMT_IMAGE_PREFIX, old_case, new_case) ||
        !case_metadata_rename_asset(CASE_MGMT_COLOR_KIND, CASE_MGMT_COLOR_PREFIX, old_case, new_case)) {

        case_set_status("Case rename stopped because a database update failed", Case_Red);
        return 0;

    }

    if (old_document[0] && strcmp(old_document, new_document) != 0) {

        DATASTORE_delete_content(DATASTORE_KIND_CASE_MANAGEMENT, old_document, &deleted, database_error,
                                 sizeof(database_error));

    }

    Global_Case_Metadata_Renaming = 0;
    Global_Case_Metadata_Name_Active = 0;
    Global_Case_Metadata_Description_Active = 0;
    case_metadata_refresh();
    Global_Case_Metadata_Selected = case_metadata_find(new_case);
    case_metadata_sync_editor();
    case_scan_case_files();
    case_scan_case_graph_files();
    case_set_status("Case renamed across database records", Case_Text);
    return 1;
}

static int case_metadata_save_current(void) {
    /*
        Purpose: Saves the current case metadata editor values
        Returns: Success status
    */

    char case_number[128];
    char document_name[256] = "";
    int duplicate;

    case_copy_text(case_number, sizeof(case_number), Global_Case_Metadata_Edit_Name);
    case_trim_text(case_number);

    if (Global_Case_Metadata_Renaming) {

        return case_metadata_rename_selected();

    }

    if (Global_Case_Metadata_Creating) {

        duplicate = case_metadata_find(case_number);

        if (duplicate >= 0) {

            case_set_status("A case with that number already exists", Case_Red);
            return 0;

        }

    }

    else if (Global_Case_Metadata_Selected >= 0 && Global_Case_Metadata_Selected < Global_Case_Metadata_Count) {

        case_copy_text(case_number, sizeof(case_number),
                       Global_Case_Metadata[Global_Case_Metadata_Selected].case_number);

    }

    if (!case_metadata_save_document(case_number, Global_Case_Metadata_Edit_Description, document_name,
                                     sizeof(document_name))) {

        return 0;

    }

    Global_Case_Metadata_Creating = 0;
    Global_Case_Metadata_Renaming = 0;
    Global_Case_Metadata_Name_Active = 0;
    Global_Case_Metadata_Description_Active = 0;
    case_metadata_refresh();
    Global_Case_Metadata_Selected = case_metadata_find(case_number);
    case_metadata_sync_editor();
    case_scan_case_files();
    case_set_status("Case description saved to database", Case_Text);
    return 1;
}

static int case_metadata_delete_case(const char *case_number) {
    /*
        Purpose: Deletes a case and its associated database records
        Returns: Success status
    */

    static Type_DataStore_Document_Summary documents[CASE_MGMT_SOURCE_MAX_FILES];
    char database_error[256] = "";
    size_t document_count = 0;
    int deleted_total = 0;

    if (!case_number || !case_number[0]) {

        return 0;

    }

    if (!DATASTORE_list_documents(DATASTORE_KIND_CLASSIFICATION, documents, sizeof(documents) / sizeof(documents[0]),
                                  &document_count, database_error, sizeof(database_error))) {

        case_set_status("Unable to list classification records for deletion", Case_Red);
        return 0;

    }

    for (size_t i = 0; i < document_count; i++) {
        int deleted = 0;

        if (case_text_equals_ci(documents[i].case_number, case_number) &&
            DATASTORE_delete_content(DATASTORE_KIND_CLASSIFICATION, documents[i].document_name, &deleted,
                                     database_error, sizeof(database_error))) {

            deleted_total += deleted;

        }
    }

    document_count = 0;

    if (!DATASTORE_list_documents(DATASTORE_KIND_CASE_MANAGEMENT, documents, sizeof(documents) / sizeof(documents[0]),
                                  &document_count, database_error, sizeof(database_error))) {

        case_set_status("Unable to list case management records for deletion", Case_Red);
        return 0;

    }

    for (size_t i = 0; i < document_count; i++) {
        int deleted = 0;

        if (case_text_equals_ci(documents[i].case_number, case_number) &&
            DATASTORE_delete_content(DATASTORE_KIND_CASE_MANAGEMENT, documents[i].document_name, &deleted,
                                     database_error, sizeof(database_error))) {

            deleted_total += deleted;

        }
    }

    {
        char image_document[256];
        int deleted = 0;

        case_metadata_asset_document_name(CASE_MGMT_IMAGE_PREFIX, case_number, image_document, sizeof(image_document));

        if (!DATASTORE_delete_content(CASE_MGMT_IMAGE_KIND, image_document, &deleted, database_error,
                                      sizeof(database_error))) {

            char message[384];
            snprintf(message, sizeof(message), "Unable to delete the case image: %.280s", database_error);
            case_set_status(message, Case_Red);
            return 0;

        }

        deleted_total += deleted;
    }

    for (int i = 0; i < Global_Case_Block_Count; i++) {

        if (case_text_equals_ci(Global_Case_Blocks[i].case_number, case_number)) {

            Global_Case_Blocks[i].case_number[0] = '\0';

        }
    }

    Global_Case_Metadata_Delete_Confirm_Open = 0;
    Global_Case_Metadata_Delete_Case[0] = '\0';
    Global_Case_Metadata_Selected = -1;
    Global_Case_Metadata_Creating = 0;
    Global_Case_Metadata_Renaming = 0;
    Global_Case_Metadata_Name_Active = 0;
    Global_Case_Metadata_Description_Active = 0;
    case_metadata_refresh();
    case_scan_case_files();
    case_scan_case_graph_files();

    {
        char message[256];
        snprintf(message, sizeof(message), "Deleted case %.100s and %d database record(s)", case_number, deleted_total);
        case_set_status(message, Case_Text);
    }
    return 1;
}

static void case_metadata_insert_text(char *text, size_t text_size, int *cursor, const char *input,
                                      int allow_newlines) {
    /*
        Purpose: Inserts text into a case metadata field
        Returns: No value
    */

    size_t length;
    int position;

    if (!text || text_size == 0 || !cursor || !input) {

        return;

    }

    length = strlen(text);
    position = *cursor;

    if (position < 0) {

        position = 0;

    }

    if ((size_t)position > length) {

        position = (int)length;

    }

    for (const unsigned char *p = (const unsigned char *)input; *p && length + 1 < text_size; p++) {
        unsigned char c = *p;

        if (c == '\r') {

            continue;

        }

        if (c == '\n' && !allow_newlines) {

            continue;

        }

        if (c < 32 && c != '\n' && c != '\t') {

            continue;

        }
        memmove(text + position + 1, text + position, length - (size_t)position + 1);
        text[position++] = (char)c;
        length++;
    }

    *cursor = position;
}

static void case_metadata_backspace(char *text, int *cursor) {
    /*
        Purpose: Removes the previous character from a case metadata field
        Returns: No value
    */

    int length;

    if (!text || !cursor) {

        return;

    }
    length = (int)strlen(text);

    if (*cursor > length) {

        *cursor = length;

    }

    if (*cursor <= 0) {

        return;

    }
    memmove(text + *cursor - 1, text + *cursor, (size_t)(length - *cursor) + 1);
    (*cursor)--;
}

static void case_metadata_delete_at_cursor(char *text, int *cursor) {
    /*
        Purpose: Deletes a character from a case metadata field
        Returns: No value
    */

    int length;

    if (!text || !cursor) {

        return;

    }
    length = (int)strlen(text);

    if (*cursor < 0) {

        *cursor = 0;

    }

    if (*cursor >= length) {

        return;

    }
    memmove(text + *cursor, text + *cursor + 1, (size_t)(length - *cursor));
}

static void case_metadata_paste(char *text, size_t text_size, int *cursor, int allow_newlines) {
    /*
        Purpose: Pastes clipboard text into a case metadata field
        Returns: No value
    */

    char *clipboard = SDL_GetClipboardText();

    if (clipboard) {

        case_metadata_insert_text(text, text_size, cursor, clipboard, allow_newlines);
        SDL_free(clipboard);

    }
}

static void case_metadata_draw_input(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label,
                                     const char *text, int active, int cursor, int read_only) {
    /*
        Purpose: Draws a case metadata text input
        Returns: No value
    */

    char shown[128];
    int text_w = 0;
    int text_h = 0;

    draw_text(renderer, font, label, rect.x, rect.y - 19, Case_Muted);
    draw_filled_rect(renderer, rect, active ? Case_Panel_2 : (SDL_Color){0, 7, 3, 255});
    draw_outline_rect(renderer, rect, active ? Case_Border_Hi : Case_Border);
    case_shorten(text && text[0] ? text : (read_only ? "No case selected" : "Click to type"), shown, sizeof(shown), 34);
    draw_text(renderer, font, shown, rect.x + 9, rect.y + 8, text && text[0] ? Case_Text : Case_Muted);

    if (active && !read_only && ((SDL_GetTicks64() / 500ULL) % 2ULL) == 0ULL) {

        char prefix[128];
        int length = text ? (int)strlen(text) : 0;

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > length) {

            cursor = length;

        }
        snprintf(prefix, sizeof(prefix), "%.*s", cursor, text ? text : "");

        if (font && TTF_SizeText(font, prefix, &text_w, &text_h) != 0) {

            text_w = cursor * 8;

        }
        SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
        SDL_RenderDrawLine(renderer, rect.x + 9 + text_w, rect.y + 6, rect.x + 9 + text_w, rect.y + rect.h - 6);

    }
}

static void case_metadata_draw_description(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *text,
                                           int active) {
    /*
        Purpose: Draws the editable case metadata description
        Returns: No value
    */

    int starts[128];
    int ends[128];
    int line_count = 0;
    int max_width = rect.w - 18;
    int max_lines = (rect.h - 16) / 20;
    int length = text ? (int)strlen(text) : 0;
    int position = 0;
    int cursor = Global_Case_Metadata_Description_Cursor;
    int cursor_line = 0;
    int first_line = 0;

    draw_filled_rect(renderer, rect, active ? Case_Panel_2 : (SDL_Color){0, 7, 3, 255});
    draw_outline_rect(renderer, rect, active ? Case_Border_Hi : Case_Border);

    if (max_width < 8) {

        max_width = 8;

    }

    if (max_lines < 1) {

        max_lines = 1;

    }

    if (!text || !text[0]) {

        if (!active) {

            draw_text(renderer, font, "Click to edit the case description", rect.x + 9, rect.y + 8, Case_Muted);
            return;

        }

        starts[0] = 0;
        ends[0] = 0;
        line_count = 1;

    }

    while (position < length && line_count < 128) {
        int paragraph_end = position;

        while (paragraph_end < length && text[paragraph_end] != '\n') {
            paragraph_end++;
        }

        if (position == paragraph_end) {

            starts[line_count] = position;
            ends[line_count] = position;
            line_count++;

        }

        while (position < paragraph_end && line_count < 128) {
            int fit = position;
            int next_position;

            for (int i = position + 1; i <= paragraph_end; i++) {

                if (case_description_range_width(font, text, (size_t)position, (size_t)i) <= max_width) {

                    fit = i;

                }

                else {

                    break;

                }
            }

            if (fit <= position) {

                fit = position + 1;

            }

            if (fit < paragraph_end) {

                int word_break = -1;

                for (int i = fit; i > position; i--) {

                    if (text[i - 1] == ' ' || text[i - 1] == '\t') {

                        word_break = i - 1;
                        break;

                    }
                }

                if (word_break > position) {

                    fit = word_break;

                }

            }

            starts[line_count] = position;
            ends[line_count] = fit;
            line_count++;
            next_position = fit;

            while (next_position < paragraph_end && (text[next_position] == ' ' || text[next_position] == '\t')) {
                next_position++;
            }

            if (next_position <= position) {

                next_position = position + 1;

            }
            position = next_position;
        }

        if (paragraph_end < length && text[paragraph_end] == '\n') {

            position = paragraph_end + 1;

            if (position == length && line_count < 128) {

                starts[line_count] = position;
                ends[line_count] = position;
                line_count++;

            }

        }
    }

    if (line_count == 0) {

        starts[0] = 0;
        ends[0] = 0;
        line_count = 1;

    }

    if (cursor < 0) {

        cursor = 0;

    }

    if (cursor > length) {

        cursor = length;

    }

    cursor_line = line_count - 1;

    for (int i = 0; i < line_count; i++) {

        if (cursor >= starts[i] && cursor < ends[i]) {

            cursor_line = i;
            break;

        }

        if (cursor == ends[i]) {

            if (i + 1 < line_count && starts[i + 1] == cursor) {

                cursor_line = i + 1;

            }

            else {

                cursor_line = i;

            }
            break;

        }

        if (i + 1 < line_count && cursor > ends[i] && cursor < starts[i + 1]) {

            cursor_line = i + 1;
            break;

        }
    }

    if (active && cursor_line >= max_lines) {

        first_line = cursor_line - max_lines + 1;

    }

    for (int i = first_line; i < line_count && i < first_line + max_lines; i++) {
        char line[CASE_MGMT_METADATA_DESCRIPTION_MAX];
        int count = ends[i] - starts[i];
        int y = rect.y + 8 + ((i - first_line) * 20);

        if (count < 0) {

            count = 0;

        }

        if (count >= (int)sizeof(line)) {

            count = (int)sizeof(line) - 1;

        }

        if (count > 0) {

            memcpy(line, text + starts[i], (size_t)count);

        }
        line[count] = '\0';

        if (line[0]) {

            draw_text(renderer, font, line, rect.x + 9, y, Case_Text);

        }
    }

    if (active && cursor_line >= first_line && cursor_line < first_line + max_lines &&
        ((SDL_GetTicks64() / 500ULL) % 2ULL) == 0ULL) {

        int line_start = starts[cursor_line];
        int line_end = ends[cursor_line];
        int cursor_on_line = cursor;
        int cursor_x;
        int cursor_y;

        if (cursor_on_line < line_start) {

            cursor_on_line = line_start;

        }

        if (cursor_on_line > line_end) {

            cursor_on_line = line_end;

        }

        cursor_x = rect.x + 9 + case_description_range_width(font, text, (size_t)line_start, (size_t)cursor_on_line);
        cursor_y = rect.y + 7 + ((cursor_line - first_line) * 20);
        SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
        SDL_RenderDrawLine(renderer, cursor_x, cursor_y, cursor_x, cursor_y + 17);

    }
}

static void case_draw_case_browser(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect editor) {
    /*
        Purpose: Draws the searchable case record manager
        Returns: No value
    */

    int mx = 0;
    int my = 0;
    int list_y;
    int detail_y;
    int filtered_count;

    case_get_adjusted_mouse_state(&mx, &my);
    draw_filled_rect(renderer, editor, Case_Panel);
    draw_outline_rect(renderer, editor, Case_Border);
    draw_text(renderer, font, "CASE RECORDS", editor.x + 14, editor.y + 16, Case_Text);

    Global_Case_Metadata_Create_Rect = (SDL_Rect){editor.x + editor.w - 118, editor.y + 10, 104, 32};
    case_draw_button(renderer, font, Global_Case_Metadata_Create_Rect, "Create New", Global_Case_Metadata_Creating,
                     case_point_in_rect(mx, my, Global_Case_Metadata_Create_Rect), 0);

    Global_Case_Metadata_Search_Rect = (SDL_Rect){editor.x + 14, editor.y + 68, editor.w - 28, 34};
    case_metadata_draw_input(renderer, font, Global_Case_Metadata_Search_Rect, "Search current cases",
                             Global_Case_Metadata_Search, Global_Case_Metadata_Search_Active,
                             Global_Case_Metadata_Search_Cursor, 0);

    list_y = Global_Case_Metadata_Search_Rect.y + Global_Case_Metadata_Search_Rect.h + 12;
    Global_Case_Metadata_List_Rect =
        (SDL_Rect){editor.x + 14, list_y, editor.w - 28, CASE_MGMT_METADATA_VISIBLE_ROWS * 32};
    draw_filled_rect(renderer, Global_Case_Metadata_List_Rect, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, Global_Case_Metadata_List_Rect, Case_Border);

    case_metadata_clamp_scroll();
    filtered_count = case_metadata_filtered_count();
    for (int row = 0; row < CASE_MGMT_METADATA_VISIBLE_ROWS; row++) {
        int filtered_position = Global_Case_Metadata_Scroll + row;
        int index = case_metadata_filtered_index_at(filtered_position);
        SDL_Rect row_rect = {Global_Case_Metadata_List_Rect.x, Global_Case_Metadata_List_Rect.y + row * 32,
                             Global_Case_Metadata_List_Rect.w, 32};
        int hovered = case_point_in_rect(mx, my, row_rect);
        int selected = index >= 0 && index == Global_Case_Metadata_Selected;

        if (index < 0) {

            continue;

        }

        draw_filled_rect(renderer, row_rect,
                         selected ? (SDL_Color){0, 55, 24, 255}
                                  : (hovered ? (SDL_Color){0, 35, 15, 255} : (SDL_Color){0, 8, 3, 255}));
        draw_outline_rect(renderer, row_rect, selected || hovered ? Case_Border_Hi : Case_Border);
        draw_text(renderer, font, Global_Case_Metadata[index].case_number, row_rect.x + 9, row_rect.y + 7,
                  selected ? Case_Text : Case_Muted);
    }

    if (filtered_count == 0) {

        draw_text(renderer, font, "No matching cases", Global_Case_Metadata_List_Rect.x + 9,
                  Global_Case_Metadata_List_Rect.y + 9, Case_Muted);

    }

    detail_y = Global_Case_Metadata_List_Rect.y + Global_Case_Metadata_List_Rect.h + 28;

    if (!Global_Case_Metadata_Creating &&
        (Global_Case_Metadata_Selected < 0 || Global_Case_Metadata_Selected >= Global_Case_Metadata_Count)) {

        draw_text(renderer, font, "Select a case or create a new one", editor.x + 14, detail_y, Case_Muted);
        return;

    }

    Global_Case_Metadata_Name_Rect = (SDL_Rect){editor.x + 14, detail_y + 22, editor.w - 28, 34};
    case_metadata_draw_input(renderer, font, Global_Case_Metadata_Name_Rect,
                             Global_Case_Metadata_Creating ? "New Case #" : "Case #", Global_Case_Metadata_Edit_Name,
                             Global_Case_Metadata_Name_Active, Global_Case_Metadata_Name_Cursor,
                             !Global_Case_Metadata_Creating && !Global_Case_Metadata_Renaming);

    {
        int button_y = Global_Case_Metadata_Name_Rect.y + Global_Case_Metadata_Name_Rect.h + 12;
        int gap = 8;
        int width = (editor.w - 28 - gap * 2) / 3;
        Global_Case_Metadata_Save_Rect = (SDL_Rect){editor.x + 14, button_y, width, 32};
        Global_Case_Metadata_Rename_Rect =
            (SDL_Rect){Global_Case_Metadata_Save_Rect.x + width + gap, button_y, width, 32};
        Global_Case_Metadata_Delete_Rect =
            (SDL_Rect){Global_Case_Metadata_Rename_Rect.x + width + gap, button_y, width, 32};
        Global_Case_Metadata_Cancel_Rect = Global_Case_Metadata_Rename_Rect;

        case_draw_button(renderer, font, Global_Case_Metadata_Save_Rect, "Save", 0,
                         case_point_in_rect(mx, my, Global_Case_Metadata_Save_Rect), 0);

        if (Global_Case_Metadata_Creating || Global_Case_Metadata_Renaming) {

            case_draw_button(renderer, font, Global_Case_Metadata_Cancel_Rect, "Cancel", 0,
                             case_point_in_rect(mx, my, Global_Case_Metadata_Cancel_Rect), 0);

        }

        else {

            case_draw_button(renderer, font, Global_Case_Metadata_Rename_Rect, "Rename", 0,
                             case_point_in_rect(mx, my, Global_Case_Metadata_Rename_Rect), 0);
            case_draw_button(renderer, font, Global_Case_Metadata_Delete_Rect, "Delete", 0,
                             case_point_in_rect(mx, my, Global_Case_Metadata_Delete_Rect), 1);

        }
    }

    if (!Global_Case_Metadata_Creating && Global_Case_Metadata_Selected >= 0 &&
        Global_Case_Metadata_Selected < Global_Case_Metadata_Count) {

        char counts[128];
        snprintf(counts, sizeof(counts), "%d classification record(s) | %d case graph(s)",
                 Global_Case_Metadata[Global_Case_Metadata_Selected].classification_count,
                 Global_Case_Metadata[Global_Case_Metadata_Selected].graph_count);
        draw_text(renderer, font, counts, editor.x + 14,
                  Global_Case_Metadata_Save_Rect.y + Global_Case_Metadata_Save_Rect.h + 8, Case_Muted);

    }

    Global_Case_Metadata_Description_Rect =
        (SDL_Rect){editor.x + 14, Global_Case_Metadata_Save_Rect.y + 72, editor.w - 28,
                   editor.y + editor.h - (Global_Case_Metadata_Save_Rect.y + 72) - 14};

    if (Global_Case_Metadata_Description_Rect.h < 80) {

        Global_Case_Metadata_Description_Rect.h = 80;

    }
    case_metadata_draw_description(renderer, font, Global_Case_Metadata_Description_Rect,
                                   Global_Case_Metadata_Edit_Description, Global_Case_Metadata_Description_Active);
}

static void case_metadata_cancel_edit(void) {
    /*
        Purpose: Cancels case metadata creation or renaming
        Returns: No value
    */

    Global_Case_Metadata_Creating = 0;
    Global_Case_Metadata_Renaming = 0;
    Global_Case_Metadata_Name_Active = 0;
    Global_Case_Metadata_Description_Active = 0;

    if (Global_Case_Metadata_Selected < 0 && Global_Case_Metadata_Count > 0) {

        Global_Case_Metadata_Selected = 0;

    }
    case_metadata_sync_editor();
}

static int case_metadata_handle_delete_confirmation(const SDL_Event *event) {
    /*
        Purpose: Handles the case deletion confirmation
        Returns: Handling status
    */

    if (!Global_Case_Metadata_Delete_Confirm_Open || !event) {

        return 0;

    }

    if (event->type == SDL_KEYDOWN) {

        if (event->key.keysym.sym == SDLK_ESCAPE) {

            Global_Case_Metadata_Delete_Confirm_Open = 0;
            return 1;

        }

        if (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER) {

            char case_number[128];
            case_copy_text(case_number, sizeof(case_number), Global_Case_Metadata_Delete_Case);
            case_metadata_delete_case(case_number);
            return 1;

        }
        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        if (case_point_in_rect(event->button.x, event->button.y, Global_Case_Metadata_Confirm_Cancel_Rect)) {

            Global_Case_Metadata_Delete_Confirm_Open = 0;
            return 1;

        }

        if (case_point_in_rect(event->button.x, event->button.y, Global_Case_Metadata_Confirm_Delete_Rect)) {

            char case_number[128];
            case_copy_text(case_number, sizeof(case_number), Global_Case_Metadata_Delete_Case);
            case_metadata_delete_case(case_number);
            return 1;

        }
        return 1;

    }

    return 1;
}

static int case_metadata_handle_event(const SDL_Event *event, SDL_Rect editor, int win_w, int win_h) {
    /*
        Purpose: Handles searchable case record management events
        Returns: Handling status
    */

    (void)editor;
    (void)win_w;
    (void)win_h;

    if (!event) {

        return 0;

    }

    if (case_metadata_handle_delete_confirmation(event)) {

        return 1;

    }

    if (event->type == SDL_TEXTINPUT) {

        if (Global_Case_Metadata_Search_Active) {

            case_metadata_insert_text(Global_Case_Metadata_Search, sizeof(Global_Case_Metadata_Search),
                                      &Global_Case_Metadata_Search_Cursor, event->text.text, 0);
            Global_Case_Metadata_Scroll = 0;
            return 1;

        }

        if (Global_Case_Metadata_Name_Active) {

            case_metadata_insert_text(Global_Case_Metadata_Edit_Name, sizeof(Global_Case_Metadata_Edit_Name),
                                      &Global_Case_Metadata_Name_Cursor, event->text.text, 0);
            return 1;

        }

        if (Global_Case_Metadata_Description_Active) {

            case_metadata_insert_text(Global_Case_Metadata_Edit_Description,
                                      sizeof(Global_Case_Metadata_Edit_Description),
                                      &Global_Case_Metadata_Description_Cursor, event->text.text, 1);
            return 1;

        }

    }

    if (event->type == SDL_MOUSEWHEEL) {

        int mx = 0;
        int my = 0;
        case_get_adjusted_mouse_state(&mx, &my);

        if (case_point_in_rect(mx, my, Global_Case_Metadata_List_Rect)) {

            Global_Case_Metadata_Scroll -= event->wheel.y;
            case_metadata_clamp_scroll();
            return 1;

        }

    }

    if (event->type == SDL_KEYDOWN && (Global_Case_Metadata_Search_Active || Global_Case_Metadata_Name_Active ||
                                       Global_Case_Metadata_Description_Active)) {

        SDL_Keycode key = event->key.keysym.sym;
        SDL_Keymod mod = SDL_GetModState();
        char *text = NULL;
        size_t text_size = 0;
        int *cursor = NULL;
        int allow_newlines = 0;

        if (Global_Case_Metadata_Search_Active) {

            text = Global_Case_Metadata_Search;
            text_size = sizeof(Global_Case_Metadata_Search);
            cursor = &Global_Case_Metadata_Search_Cursor;

        }

        else if (Global_Case_Metadata_Name_Active) {

            text = Global_Case_Metadata_Edit_Name;
            text_size = sizeof(Global_Case_Metadata_Edit_Name);
            cursor = &Global_Case_Metadata_Name_Cursor;

        }

        else {

            text = Global_Case_Metadata_Edit_Description;
            text_size = sizeof(Global_Case_Metadata_Edit_Description);
            cursor = &Global_Case_Metadata_Description_Cursor;
            allow_newlines = 1;

        }

        if ((mod & KMOD_CTRL) && key == SDLK_v) {

            case_metadata_paste(text, text_size, cursor, allow_newlines);
            return 1;

        }

        if (key == SDLK_BACKSPACE) {

            case_metadata_backspace(text, cursor);

            if (Global_Case_Metadata_Search_Active) {

                Global_Case_Metadata_Scroll = 0;

            }
            return 1;

        }

        if (key == SDLK_DELETE) {

            case_metadata_delete_at_cursor(text, cursor);
            return 1;

        }

        if (key == SDLK_LEFT) {

            if (*cursor > 0) {

                (*cursor)--;

            }
            return 1;

        }

        if (key == SDLK_RIGHT) {

            if (*cursor < (int)strlen(text)) {

                (*cursor)++;

            }
            return 1;

        }

        if (key == SDLK_HOME) {

            *cursor = 0;
            return 1;

        }

        if (key == SDLK_END) {

            *cursor = (int)strlen(text);
            return 1;

        }

        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && Global_Case_Metadata_Description_Active) {

            case_metadata_insert_text(text, text_size, cursor, "\n", 1);
            return 1;

        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

            Global_Case_Metadata_Search_Active = 0;
            Global_Case_Metadata_Name_Active = 0;
            return 1;

        }

        if (key == SDLK_ESCAPE) {

            Global_Case_Metadata_Search_Active = 0;
            Global_Case_Metadata_Name_Active = 0;
            Global_Case_Metadata_Description_Active = 0;

            if (Global_Case_Metadata_Creating || Global_Case_Metadata_Renaming) {

                case_metadata_cancel_edit();

            }
            return 1;

        }
        return 1;

    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

        int mx = event->button.x;
        int my = event->button.y;

        if (case_point_in_rect(mx, my, Global_Case_Metadata_Search_Rect)) {

            Global_Case_Metadata_Search_Active = 1;
            Global_Case_Metadata_Search_Cursor = (int)strlen(Global_Case_Metadata_Search);
            Global_Case_Metadata_Name_Active = 0;
            Global_Case_Metadata_Description_Active = 0;
            return 1;

        }

        if (case_point_in_rect(mx, my, Global_Case_Metadata_Create_Rect)) {

            case_metadata_begin_create();
            return 1;

        }

        if (case_point_in_rect(mx, my, Global_Case_Metadata_List_Rect)) {

            int row = (my - Global_Case_Metadata_List_Rect.y) / 32;
            int index = case_metadata_filtered_index_at(Global_Case_Metadata_Scroll + row);

            if (index >= 0) {

                case_metadata_select(index);

            }
            return 1;

        }

        if ((Global_Case_Metadata_Creating || Global_Case_Metadata_Renaming) &&
            case_point_in_rect(mx, my, Global_Case_Metadata_Name_Rect)) {

            Global_Case_Metadata_Name_Active = 1;
            Global_Case_Metadata_Name_Cursor = (int)strlen(Global_Case_Metadata_Edit_Name);
            Global_Case_Metadata_Search_Active = 0;
            Global_Case_Metadata_Description_Active = 0;
            return 1;

        }

        if (case_point_in_rect(mx, my, Global_Case_Metadata_Description_Rect)) {

            Global_Case_Metadata_Description_Active = 1;
            Global_Case_Metadata_Description_Cursor = (int)strlen(Global_Case_Metadata_Edit_Description);
            Global_Case_Metadata_Search_Active = 0;
            Global_Case_Metadata_Name_Active = 0;
            return 1;

        }

        if (case_point_in_rect(mx, my, Global_Case_Metadata_Save_Rect)) {

            case_metadata_save_current();
            return 1;

        }

        if ((Global_Case_Metadata_Creating || Global_Case_Metadata_Renaming) &&
            case_point_in_rect(mx, my, Global_Case_Metadata_Cancel_Rect)) {

            case_metadata_cancel_edit();
            return 1;

        }

        if (!Global_Case_Metadata_Creating && !Global_Case_Metadata_Renaming &&
            case_point_in_rect(mx, my, Global_Case_Metadata_Rename_Rect)) {

            case_metadata_begin_rename();
            return 1;

        }

        if (!Global_Case_Metadata_Creating && !Global_Case_Metadata_Renaming && Global_Case_Metadata_Selected >= 0 &&
            case_point_in_rect(mx, my, Global_Case_Metadata_Delete_Rect)) {

            case_copy_text(Global_Case_Metadata_Delete_Case, sizeof(Global_Case_Metadata_Delete_Case),
                           Global_Case_Metadata[Global_Case_Metadata_Selected].case_number);
            Global_Case_Metadata_Delete_Confirm_Open = 1;
            Global_Case_Metadata_Search_Active = 0;
            Global_Case_Metadata_Name_Active = 0;
            Global_Case_Metadata_Description_Active = 0;
            return 1;

        }

        Global_Case_Metadata_Search_Active = 0;
        Global_Case_Metadata_Name_Active = 0;
        Global_Case_Metadata_Description_Active = 0;

    }

    return 0;
}

static void case_metadata_draw_delete_confirmation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the case deletion confirmation dialog
        Returns: No value
    */

    SDL_Rect panel;
    SDL_Rect message;
    int mx = 0;
    int my = 0;
    int delete_hover;
    int cancel_hover;
    char title[256];

    if (!Global_Case_Metadata_Delete_Confirm_Open || !renderer || !font) {

        return;

    }

    case_get_adjusted_mouse_state(&mx, &my);
    panel = (SDL_Rect){(win_w - 540) / 2, (win_h - 238) / 2, 540, 238};

    if (panel.x < 12) {

        panel.x = 12;
        panel.w = win_w - 24;

    }

    if (panel.y < 12) {

        panel.y = 12;

    }

    Global_Case_Metadata_Confirm_Cancel_Rect = (SDL_Rect){panel.x + panel.w - 242, panel.y + panel.h - 58, 106, 36};
    Global_Case_Metadata_Confirm_Delete_Rect = (SDL_Rect){panel.x + panel.w - 124, panel.y + panel.h - 58, 106, 36};
    delete_hover = case_point_in_rect(mx, my, Global_Case_Metadata_Confirm_Delete_Rect);
    cancel_hover = case_point_in_rect(mx, my, Global_Case_Metadata_Confirm_Cancel_Rect);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_filled_rect(renderer, (SDL_Rect){0, 0, win_w, win_h}, (SDL_Color){0, 0, 0, 205});
    draw_filled_rect(renderer, panel, (SDL_Color){20, 0, 0, 252});
    draw_outline_rect(renderer, panel, (SDL_Color){255, 35, 35, 255});

    snprintf(title, sizeof(title), "Delete case %s?", Global_Case_Metadata_Delete_Case);
    draw_text(renderer, font, title, panel.x + 22, panel.y + 22, (SDL_Color){255, 90, 90, 255});

    message = (SDL_Rect){panel.x + 22, panel.y + 62, panel.w - 44, 92};
    draw_text(renderer, font, "This permanently deletes the case description, classification records,", message.x,
              message.y, (SDL_Color){235, 205, 205, 255});
    draw_text(renderer, font, "map points, and stored Case Management graph files for this case.", message.x,
              message.y + 24, (SDL_Color){235, 205, 205, 255});

    case_draw_button(renderer, font, Global_Case_Metadata_Confirm_Cancel_Rect, "Cancel", 0, cancel_hover, 0);
    case_draw_button(renderer, font, Global_Case_Metadata_Confirm_Delete_Rect, "Delete", 0, delete_hover, 1);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

int CASE_MANAGEMENT_handle_event(const SDL_Event *event, int win_w, int win_h) {
    /*
        Purpose: Handles the event
        Returns: Handling status
    */

    SDL_Rect canvas;
    SDL_Rect editor;
    SDL_Rect new_btn;
    SDL_Rect case_btn;
    SDL_Rect link_btn;
    SDL_Rect save_btn;
    SDL_Rect load_btn;
    SDL_Rect undo_btn;
    SDL_Rect file_rect;
    SDL_Rect delete_database_btn;
    SDL_Rect fields[CASE_MGMT_FIELD_COUNT];
    SDL_Rect duplicate_btn;
    SDL_Rect delete_btn;

    if (!event || !Global_CaseManagement_Mode) {

        return 0;

    }

    canvas = case_canvas_rect(win_w, win_h);
    editor = case_editor_rect(win_w, win_h);
    case_ensure_view(canvas);
    case_toolbar_rects(win_w, &new_btn, &case_btn, &link_btn, &save_btn, &load_btn, &undo_btn, &file_rect);
    delete_database_btn = case_delete_database_record_rect(win_w);

    if (file_rect.x + file_rect.w > delete_database_btn.x - 10) {

        file_rect.w = delete_database_btn.x - 10 - file_rect.x;

        if (file_rect.w < 80) {

            file_rect.w = 80;

        }

    }
    case_editor_field_rects(editor, fields, &duplicate_btn, &delete_btn);

    if (case_handle_database_delete_confirmation(event)) {

        return 1;

    }

    if (Global_Case_File_Search_Open) {

        return case_handle_file_search_event(event, win_w, win_h);

    }

    if (Global_Case_Description_Popup_Open) {

        SDL_Rect popup = case_description_popup_rect(win_w, win_h);
        SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
        SDL_Rect text_rect = {popup.x + 18, popup.y + 58, popup.w - 36, popup.h - 88};
        char *text = case_selected_field_text(CASE_MGMT_FIELD_DESCRIPTION);

        if (event->type == SDL_TEXTINPUT) {

            if (text) {

                case_insert_text(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION], event->text.text,
                                 CASE_MGMT_FIELD_DESCRIPTION);

            }
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

                if (text) {

                    case_backspace(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION]);

                }
                return 1;

            }

            if (key == SDLK_DELETE) {

                if (text) {

                    case_delete_at_cursor(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION]);

                }
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

                if (text) {

                    case_insert_text(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION], "\n",
                                     CASE_MGMT_FIELD_DESCRIPTION);

                }
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
                case_set_description_cursor_from_mouse_scrolled(text_rect, mx, my,
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

                case_set_description_cursor_from_mouse_scrolled(text_rect, mx, my,
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

    if ((Global_Case_Selected < 0 || Global_Case_Selected >= Global_Case_Block_Count ||
         Global_Case_Metadata_Search_Active || Global_Case_Metadata_Name_Active ||
         Global_Case_Metadata_Description_Active || Global_Case_Metadata_Delete_Confirm_Open) &&
        case_metadata_handle_event(event, editor, win_w, win_h)) {

        return 1;

    }

    if (Global_Case_Source_Popup_Open) {

        if (event->type == SDL_TEXTINPUT) {

            if (Global_Case_Source_Search_Active) {

                size_t len = strlen(Global_Case_Source_Search);
                const char *src = event->text.text;

                while (*src && len + 1 < sizeof(Global_Case_Source_Search)) {
                    char c = *src++;

                    if (c >= 32 && c <= 126) {

                        int cursor = Global_Case_Source_Search_Cursor;

                        if (cursor < 0) {

                            cursor = 0;

                        }

                        if (cursor > (int)len) {

                            cursor = (int)len;

                        }

                        memmove(Global_Case_Source_Search + cursor + 1, Global_Case_Source_Search + cursor,
                                len - (size_t)cursor + 1);
                        Global_Case_Source_Search[cursor] = c;
                        Global_Case_Source_Search_Cursor = cursor + 1;
                        len++;

                    }
                }

                Global_Case_Source_Scroll = 0;
                case_clamp_source_scroll();

            }

            return 1;

        }

        if (event->type == SDL_KEYDOWN) {

            SDL_Keycode key = event->key.keysym.sym;
            int len = (int)strlen(Global_Case_Source_Search);

            if (key == SDLK_ESCAPE) {

                case_close_source_file_search_menu();
                return 1;

            }

            if (key == SDLK_BACKSPACE) {

                int cursor = Global_Case_Source_Search_Cursor;

                if (cursor > 0 && len > 0) {

                    if (cursor > len) {

                        cursor = len;

                    }

                    memmove(Global_Case_Source_Search + cursor - 1, Global_Case_Source_Search + cursor,
                            (size_t)(len - cursor + 1));
                    Global_Case_Source_Search_Cursor = cursor - 1;
                    Global_Case_Source_Scroll = 0;
                    case_clamp_source_scroll();

                }

                return 1;

            }

            if (key == SDLK_DELETE) {

                int cursor = Global_Case_Source_Search_Cursor;

                if (cursor < 0) {

                    cursor = 0;

                }

                if (cursor < len) {

                    memmove(Global_Case_Source_Search + cursor, Global_Case_Source_Search + cursor + 1,
                            (size_t)(len - cursor));
                    Global_Case_Source_Scroll = 0;
                    case_clamp_source_scroll();

                }

                return 1;

            }

            if (key == SDLK_LEFT) {

                if (Global_Case_Source_Search_Cursor > 0) {

                    Global_Case_Source_Search_Cursor--;

                }

                return 1;

            }

            if (key == SDLK_RIGHT) {

                if (Global_Case_Source_Search_Cursor < len) {

                    Global_Case_Source_Search_Cursor++;

                }

                return 1;

            }

            if (key == SDLK_HOME) {

                Global_Case_Source_Search_Cursor = 0;
                return 1;

            }

            if (key == SDLK_END) {

                Global_Case_Source_Search_Cursor = len;
                return 1;

            }

            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

                int source_index = case_source_filtered_index_at(Global_Case_Source_Scroll);

                if (source_index >= 0) {

                    case_source_file_search_select_index(source_index);

                }

                return 1;

            }

            if (key == SDLK_DOWN) {

                Global_Case_Source_Scroll++;
                case_clamp_source_scroll();
                return 1;

            }

            if (key == SDLK_UP) {

                Global_Case_Source_Scroll--;
                case_clamp_source_scroll();
                return 1;

            }

            if (key == SDLK_r) {

                case_scan_source_files();
                case_clamp_source_scroll();
                return 1;

            }

            return 1;

        }

        if (event->type == SDL_MOUSEWHEEL) {

            int mx = 0;
            int my = 0;
            SDL_Rect popup = case_source_popup_rect(win_w, win_h);
            SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};

            case_get_adjusted_mouse_state(&mx, &my);

            if (case_point_in_rect(mx, my, list)) {

                Global_Case_Source_Scroll -= event->wheel.y * 3;
                case_clamp_source_scroll();

            }

            return 1;

        }

        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {

            return case_handle_source_popup_click(event->button.x, event->button.y, win_w, win_h);

        }

        return 1;

    }

    if (event->type == SDL_TEXTINPUT && Global_Case_File_Name_Active) {

        case_file_name_insert_text(event->text.text);
        return 1;

    }

    if (event->type == SDL_TEXTINPUT && Global_Case_Active_Field != CASE_MGMT_FIELD_NONE) {

        if (Global_Case_Active_Field == CASE_MGMT_FIELD_STATUS ||
            Global_Case_Active_Field == CASE_MGMT_FIELD_SOURCE_FILE) {

            return 1;

        }
        char *text = case_selected_field_text(Global_Case_Active_Field);

        if (text) {

            case_insert_text(text, &Global_Case_Field_Cursor[Global_Case_Active_Field], event->text.text,
                             Global_Case_Active_Field);

        }

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

        if (Global_Case_Active_Field == CASE_MGMT_FIELD_USER) {

            Global_Case_User_Dropdown_Open = 1;
            Global_Case_User_Scroll = 0;
            Global_Case_User_Keyboard_Pos = -1;
            case_clamp_user_scroll();

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

                if (visible > CASE_MGMT_CASE_MAX_VISIBLE) {

                    visible = CASE_MGMT_CASE_MAX_VISIBLE;

                }
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

                if (visible > CASE_MGMT_COUNTRY_MAX_VISIBLE) {

                    visible = CASE_MGMT_COUNTRY_MAX_VISIBLE;

                }
                SDL_Rect menu = case_country_dropdown_rect(fields[CASE_MGMT_FIELD_COUNTRY], visible > 0 ? visible : 1);

                if (case_point_in_rect(mx, my, menu)) {

                    Global_Case_Country_Scroll -= event->wheel.y;
                    case_clamp_country_scroll();
                    return 1;

                }

            }

            if (Global_Case_User_Dropdown_Open && Global_Case_Active_Field == CASE_MGMT_FIELD_USER) {

                int matches[CASE_MGMT_SOURCE_MAX_FILES];
                int count = case_build_user_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
                int visible = count - Global_Case_User_Scroll;

                if (visible > CASE_MGMT_USER_MAX_VISIBLE) {

                    visible = CASE_MGMT_USER_MAX_VISIBLE;

                }
                SDL_Rect menu = case_user_dropdown_rect(fields[CASE_MGMT_FIELD_USER], visible > 0 ? visible : 1);

                if (case_point_in_rect(mx, my, menu)) {

                    Global_Case_User_Scroll -= event->wheel.y;
                    Global_Case_User_Keyboard_Pos = -1;
                    case_clamp_user_scroll();
                    return 1;

                }

            }

        }

        if (case_point_in_rect(mx, my, canvas) && event->wheel.y != 0) {

            double before_x = Global_Case_View_X + ((double)mx - (double)canvas.x) / Global_Case_Zoom;
            double before_y = Global_Case_View_Y + ((double)my - (double)canvas.y) / Global_Case_Zoom;
            double factor = event->wheel.y > 0 ? 1.10 : (1.0 / 1.10);

            Global_Case_Zoom = case_limit_double(Global_Case_Zoom * factor, CASE_MGMT_MIN_ZOOM, CASE_MGMT_MAX_ZOOM);
            Global_Case_View_X = before_x - ((double)mx - (double)canvas.x) / Global_Case_Zoom;
            Global_Case_View_Y = before_y - ((double)my - (double)canvas.y) / Global_Case_Zoom;
            case_set_status("Case graph zoom adjusted", Case_Text);
            return 1;

        }

    }

    if (event->type == SDL_KEYDOWN) {

        SDL_Keycode key = event->key.keysym.sym;
        SDL_Keymod mod = SDL_GetModState();

        if ((mod & KMOD_CTRL) && key == SDLK_z) {

            case_undo_last_change();
            return 1;

        }

        if (Global_Case_File_Name_Active) {

            int len = (int)strlen(Global_Case_File_Name);

            if ((mod & KMOD_CTRL) && key == SDLK_s) {

                case_save();
                return 1;

            }

            if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {

                Global_Case_File_Name_Active = 0;
                return 1;

            }

            if (key == SDLK_BACKSPACE) {

                case_file_name_backspace();
                return 1;

            }

            if (key == SDLK_DELETE) {

                case_file_name_delete();
                return 1;

            }

            if (key == SDLK_LEFT) {

                if (Global_Case_File_Name_Cursor > 0) {

                    Global_Case_File_Name_Cursor--;

                }
                return 1;

            }

            if (key == SDLK_RIGHT) {

                if (Global_Case_File_Name_Cursor < len) {

                    Global_Case_File_Name_Cursor++;

                }
                return 1;

            }

            if (key == SDLK_HOME) {

                Global_Case_File_Name_Cursor = 0;
                return 1;

            }

            if (key == SDLK_END) {

                Global_Case_File_Name_Cursor = len;
                return 1;

            }
            return 1;

        }

        if ((mod & KMOD_CTRL) && key == SDLK_s) {

            case_save();
            return 1;

        }

        if ((mod & KMOD_CTRL) && key == SDLK_o) {

            case_open_file_search_menu();
            return 1;

        }

        if (Global_Case_Active_Field != CASE_MGMT_FIELD_NONE) {

            char *text = case_selected_field_text(Global_Case_Active_Field);

            if ((mod & KMOD_CTRL) && key == SDLK_v && Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION) {

                case_paste_description_from_clipboard();
                return 1;

            }

            if (key == SDLK_BACKSPACE) {

                if (text && Global_Case_Active_Field != CASE_MGMT_FIELD_STATUS &&
                    Global_Case_Active_Field != CASE_MGMT_FIELD_SOURCE_FILE) {

                    case_backspace(text, &Global_Case_Field_Cursor[Global_Case_Active_Field]);

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

                    if (Global_Case_Active_Field == CASE_MGMT_FIELD_USER) {

                        Global_Case_User_Dropdown_Open = 1;
                        Global_Case_User_Scroll = 0;
                        Global_Case_User_Keyboard_Pos = -1;
                        case_clamp_user_scroll();

                    }

                }
                return 1;

            }

            if (key == SDLK_DELETE) {

                if (text && Global_Case_Active_Field != CASE_MGMT_FIELD_STATUS &&
                    Global_Case_Active_Field != CASE_MGMT_FIELD_SOURCE_FILE) {

                    case_delete_at_cursor(text, &Global_Case_Field_Cursor[Global_Case_Active_Field]);

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

                    if (Global_Case_Active_Field == CASE_MGMT_FIELD_USER) {

                        Global_Case_User_Dropdown_Open = 1;
                        Global_Case_User_Scroll = 0;
                        Global_Case_User_Keyboard_Pos = -1;
                        case_clamp_user_scroll();

                    }

                }
                return 1;

            }

            if (Global_Case_Active_Field == CASE_MGMT_FIELD_USER && Global_Case_User_Dropdown_Open &&
                (key == SDLK_UP || key == SDLK_DOWN)) {

                int matches[CASE_MGMT_SOURCE_MAX_FILES];
                int count = case_build_user_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);

                if (count > 0) {

                    if (Global_Case_User_Keyboard_Pos < 0) {

                        Global_Case_User_Keyboard_Pos = key == SDLK_DOWN ? 0 : count - 1;

                    }

                    else {

                        Global_Case_User_Keyboard_Pos += key == SDLK_DOWN ? 1 : -1;

                        if (Global_Case_User_Keyboard_Pos < 0) {

                            Global_Case_User_Keyboard_Pos = count - 1;

                        }

                        if (Global_Case_User_Keyboard_Pos >= count) {

                            Global_Case_User_Keyboard_Pos = 0;

                        }

                    }

                    if (Global_Case_User_Keyboard_Pos < Global_Case_User_Scroll) {

                        Global_Case_User_Scroll = Global_Case_User_Keyboard_Pos;

                    }

                    if (Global_Case_User_Keyboard_Pos >= Global_Case_User_Scroll + CASE_MGMT_USER_MAX_VISIBLE) {

                        Global_Case_User_Scroll = Global_Case_User_Keyboard_Pos - CASE_MGMT_USER_MAX_VISIBLE + 1;

                    }
                    case_clamp_user_scroll();

                }
                return 1;

            }

            if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && Global_Case_Active_Field == CASE_MGMT_FIELD_USER &&
                Global_Case_User_Dropdown_Open) {

                int matches[CASE_MGMT_SOURCE_MAX_FILES];
                int count = case_build_user_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
                int pos = Global_Case_User_Keyboard_Pos;

                if (count > 0) {

                    if (pos < 0 || pos >= count) {

                        pos = 0;

                    }
                    case_select_user_option(matches[pos]);
                    case_set_status("Assigned user selected", Case_Text);

                }

                else {

                    Global_Case_User_Dropdown_Open = 0;
                    Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;

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

                if (text) {

                    case_insert_text(text, &Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION], "\n",
                                     CASE_MGMT_FIELD_DESCRIPTION);

                }
                return 1;

            }

            if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER) {

                Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
                Global_Case_Status_Dropdown_Open = 0;
                Global_Case_Case_Dropdown_Open = 0;
                Global_Case_Country_Dropdown_Open = 0;
                Global_Case_User_Dropdown_Open = 0;
                Global_Case_User_Keyboard_Pos = -1;
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

        if (key == SDLK_c) {

            case_add_block_typed(canvas, CASE_MGMT_BLOCK_CASE);
            return 1;

        }

        if (key == SDLK_l) {

            Global_Case_Link_Mode = !Global_Case_Link_Mode;
            Global_Case_Link_Source = -1;
            case_set_status(Global_Case_Link_Mode ? "Link mode: click source, then destination" : "Link mode disabled",
                            Case_Text);
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
            Global_Case_Drag_Undo_Pushed = 0;
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

        if (Global_Case_Description_Selecting && !Global_Case_Description_Popup_Open &&
            Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION && Global_Case_Selected >= 0 &&
            Global_Case_Selected < Global_Case_Block_Count) {

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
            Global_Case_Status_Dropdown_Hover =
                case_point_in_rect(mmx, mmy, menu) ? (mmy - menu.y) / CASE_MGMT_STATUS_OPTION_H : -1;

        }

        if (Global_Case_Link_Dragging) {

            int motion_x = event->motion.x;
            int motion_y = event->motion.y;
            int side = -1;
            Global_Case_Link_Drag_Mouse_X = motion_x;
            Global_Case_Link_Drag_Mouse_Y = motion_y;
            Global_Case_Link_Drag_Target_Index =
                case_nearest_endpoint(motion_x, motion_y, canvas, Global_Case_Link_Drag_Start_Index, &side);
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

            if (case_selected_block_count() == 0 && Global_Case_Selected >= 0 &&
                Global_Case_Selected < Global_Case_Block_Count) {

                Global_Case_Selected_Blocks[Global_Case_Selected] = 1;

            }

            if (dx != 0 || dy != 0) {

                if (!Global_Case_Drag_Undo_Pushed) {

                    case_push_undo_state();
                    Global_Case_Drag_Undo_Pushed = 1;

                }
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

                if (case_handle_case_dropdown_click(mx, my, fields[CASE_MGMT_FIELD_CASE_NUMBER])) {

                    return 1;

                }

                if (!case_point_in_rect(mx, my, fields[CASE_MGMT_FIELD_CASE_NUMBER])) {

                    Global_Case_Case_Dropdown_Open = 0;

                }

            }

            if (Global_Case_Country_Dropdown_Open) {

                if (case_handle_country_dropdown_click(mx, my, fields[CASE_MGMT_FIELD_COUNTRY])) {

                    return 1;

                }

                if (!case_point_in_rect(mx, my, fields[CASE_MGMT_FIELD_COUNTRY])) {

                    Global_Case_Country_Dropdown_Open = 0;

                }

            }

            if (Global_Case_User_Dropdown_Open && Global_Case_Active_Field == CASE_MGMT_FIELD_USER) {

                if (case_handle_user_dropdown_click(mx, my, fields[CASE_MGMT_FIELD_USER])) {

                    return 1;

                }

                if (!case_point_in_rect(mx, my, fields[CASE_MGMT_FIELD_USER])) {

                    Global_Case_User_Dropdown_Open = 0;
                    Global_Case_User_Keyboard_Pos = -1;

                }

            }

            if (Global_Case_Status_Dropdown_Open) {

                if (case_handle_status_dropdown_click(mx, my, fields[CASE_MGMT_FIELD_STATUS])) {

                    return 1;

                }

                if (!case_point_in_rect(mx, my, fields[CASE_MGMT_FIELD_STATUS])) {

                    Global_Case_Status_Dropdown_Open = 0;

                }

            }

            if (Global_Case_Calendar_Open) {

                SDL_Rect active_date_field = fields[Global_Case_Calendar_Field];

                if (case_handle_calendar_click(mx, my, active_date_field)) {

                    return 1;

                }

                if (!case_point_in_rect(mx, my, active_date_field)) {

                    Global_Case_Calendar_Open = 0;

                }

            }

        }

        if (case_point_in_rect(mx, my, new_btn)) {

            case_add_block(canvas);
            return 1;

        }

        if (case_point_in_rect(mx, my, case_btn)) {

            case_add_block_typed(canvas, CASE_MGMT_BLOCK_CASE);
            return 1;

        }

        if (case_point_in_rect(mx, my, file_rect)) {

            Global_Case_File_Name_Active = 1;
            Global_Case_File_Name_Cursor = (int)strlen(Global_Case_File_Name);
            Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;
            Global_Case_Status_Dropdown_Open = 0;
            Global_Case_Case_Dropdown_Open = 0;
            Global_Case_Country_Dropdown_Open = 0;
            Global_Case_Calendar_Open = 0;
            Global_Case_Source_Popup_Open = 0;
            return 1;

        }

        if (case_point_in_rect(mx, my, link_btn)) {

            Global_Case_Link_Mode = !Global_Case_Link_Mode;
            Global_Case_Link_Source = -1;
            case_set_status(Global_Case_Link_Mode ? "Link mode: click source, then destination" : "Link mode disabled",
                            Case_Text);
            return 1;

        }

        if (case_point_in_rect(mx, my, save_btn)) {

            case_save();
            return 1;

        }

        if (case_point_in_rect(mx, my, load_btn)) {

            case_open_file_search_menu();
            return 1;

        }

        if (case_point_in_rect(mx, my, undo_btn)) {

            case_undo_last_change();
            return 1;

        }

        if (case_point_in_rect(mx, my, delete_database_btn)) {

            if (Global_Case_Loaded_Database_Record[0] == '\0') {

                case_set_status("No database record is currently loaded", Case_Warn);

            }

            else {

                Global_Case_Database_Delete_Confirm_Open = 1;
                Global_Case_File_Name_Active = 0;
                Global_Case_Active_Field = CASE_MGMT_FIELD_NONE;

            }
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

            if (case_rect_is_valid(fields[CASE_MGMT_FIELD_DESCRIPTION]) && case_point_in_rect(mx, my, desc_open_btn)) {

                Global_Case_Active_Field = CASE_MGMT_FIELD_DESCRIPTION;
                Global_Case_Description_Popup_Open = 1;
                Global_Case_Description_Popup_Scroll = 0;
                Global_Case_Status_Dropdown_Open = 0;
                Global_Case_Case_Dropdown_Open = 0;
                Global_Case_Country_Dropdown_Open = 0;
                Global_Case_User_Dropdown_Open = 0;
                Global_Case_User_Keyboard_Pos = -1;
                Global_Case_Calendar_Open = 0;
                Global_Case_Source_Popup_Open = 0;
                return 1;

            }

            for (int i = 0; i < CASE_MGMT_FIELD_COUNT; i++) {

                if (!case_rect_is_valid(fields[i])) {

                    continue;

                }

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
                        case_open_source_file_search_menu();
                        return 1;

                    }

                    if (i == CASE_MGMT_FIELD_CASE_NUMBER) {

                        case_scan_case_files();
                        Global_Case_Active_Field = i;
                        Global_Case_Field_Cursor[i] =
                            case_selected_field_text(i) ? (int)strlen(case_selected_field_text(i)) : 0;
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
                        Global_Case_Field_Cursor[i] =
                            case_selected_field_text(i) ? (int)strlen(case_selected_field_text(i)) : 0;
                        Global_Case_Status_Dropdown_Open = 0;
                        Global_Case_Case_Dropdown_Open = 0;
                        Global_Case_Country_Dropdown_Open = 1;
                        Global_Case_Country_Scroll = 0;
                        Global_Case_Calendar_Open = 0;
                        Global_Case_Source_Popup_Open = 0;
                        return 1;

                    }

                    if (i == CASE_MGMT_FIELD_USER) {

                        case_scan_users();
                        Global_Case_Active_Field = i;
                        Global_Case_Field_Cursor[i] =
                            case_selected_field_text(i) ? (int)strlen(case_selected_field_text(i)) : 0;
                        Global_Case_Status_Dropdown_Open = 0;
                        Global_Case_Case_Dropdown_Open = 0;
                        Global_Case_Country_Dropdown_Open = 0;
                        Global_Case_User_Dropdown_Open = 1;
                        Global_Case_User_Scroll = 0;
                        Global_Case_User_Hover = -1;
                        Global_Case_User_Keyboard_Pos = -1;
                        Global_Case_Calendar_Open = 0;
                        Global_Case_Source_Popup_Open = 0;
                        case_clamp_user_scroll();
                        return 1;

                    }

                    Global_Case_Active_Field = i;
                    Global_Case_Status_Dropdown_Open = 0;
                    Global_Case_Case_Dropdown_Open = 0;
                    Global_Case_Country_Dropdown_Open = 0;
                    Global_Case_User_Dropdown_Open = 0;
                    Global_Case_User_Keyboard_Pos = -1;
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
                Global_Case_User_Dropdown_Open = 0;
                Global_Case_User_Keyboard_Pos = -1;
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
                Global_Case_User_Dropdown_Open = 0;
                Global_Case_User_Keyboard_Pos = -1;
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
                Global_Case_Drag_Undo_Pushed = 0;
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
            int target = case_nearest_endpoint(mx, my, canvas, Global_Case_Link_Drag_Start_Index, &side);

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

            SDL_Rect selection = case_make_normalized_rect(Global_Case_Box_Start_X, Global_Case_Box_Start_Y,
                                                           Global_Case_Box_End_X, Global_Case_Box_End_Y);
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
        Global_Case_Drag_Undo_Pushed = 0;
        Global_Case_Panning = 0;
        Global_Case_Description_Selecting = 0;
        return 1;

    }

    return 0;
}

static void case_draw_grid(SDL_Renderer *renderer, SDL_Rect rect) {
    /*
        Purpose: Draws the grid
        Returns: No value
    */

    int step = (int)(42.0 * Global_Case_Zoom);

    if (step < 18) {

        step = 18;

    }

    if (step > 80) {

        step = 80;

    }

    SDL_SetRenderDrawColor(renderer, 0, 50, 20, 105);
    for (int x = rect.x; x < rect.x + rect.w; x += step) {
        SDL_RenderDrawLine(renderer, x, rect.y, x, rect.y + rect.h);
    }
    for (int y = rect.y; y < rect.y + rect.h; y += step) {
        SDL_RenderDrawLine(renderer, rect.x, y, rect.x + rect.w, y);
    }
}

static void case_draw_selection_box(SDL_Renderer *renderer) {
    /*
        Purpose: Draws the selection box
        Returns: No value
    */

    if (!Global_Case_Box_Selecting) {

        return;

    }
    SDL_Rect r = case_make_normalized_rect(Global_Case_Box_Start_X, Global_Case_Box_Start_Y, Global_Case_Box_End_X,
                                           Global_Case_Box_End_Y);

    if (r.w < 4 && r.h < 4) {

        return;

    }

    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    /* 85% transparent fill: 15% opacity over the workspace. */
    draw_filled_rect(renderer, r, (SDL_Color){0, 90, 35, 38});

    SDL_SetRenderDrawBlendMode(renderer, old_blend);

    draw_outline_rect(renderer, r, Case_Border_Hi);
    SDL_Rect inner = {r.x + 2, r.y + 2, r.w - 4, r.h - 4};

    if (inner.w > 4 && inner.h > 4) {

        draw_outline_rect(renderer, inner, Case_Muted);

    }
}

static void case_side_vector(int side, int *dx, int *dy) {
    /*
        Purpose: Calculates a connector side direction vector
        Returns: No value
    */

    int vx = 0;
    int vy = 0;

    if (side == CASE_MGMT_SIDE_LEFT) {

        vx = -1;

    }

    else if (side == CASE_MGMT_SIDE_RIGHT) {

        vx = 1;

    }

    else if (side == CASE_MGMT_SIDE_TOP) {

        vy = -1;

    }

    else if (side == CASE_MGMT_SIDE_BOTTOM) {

        vy = 1;

    }

    if (dx) {

        *dx = vx;

    }

    if (dy) {

        *dy = vy;

    }
}

static int case_guess_end_side_for_preview(int from_side, int x1, int y1, int x2, int y2) {
    /*
        Purpose: Determines the preview link end side
        Returns: Success status
    */

    int dx = x2 - x1;
    int dy = y2 - y1;
    (void)from_side;

    if (abs(dx) >= abs(dy)) {

        return dx >= 0 ? CASE_MGMT_SIDE_LEFT : CASE_MGMT_SIDE_RIGHT;

    }
    return dy >= 0 ? CASE_MGMT_SIDE_TOP : CASE_MGMT_SIDE_BOTTOM;
}

static int case_route_points(int x1, int y1, int from_side, int x2, int y2, int to_side, int pts_x[6], int pts_y[6]) {
    /*
        Purpose: Routes the points
        Returns: Success status
    */

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

    if (stem < 20) {

        stem = 20;

    }

    if (stem > 54) {

        stem = 54;

    }

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

    if (from_side == CASE_MGMT_SIDE_TOP || from_side == CASE_MGMT_SIDE_BOTTOM || to_side == CASE_MGMT_SIDE_TOP ||
        to_side == CASE_MGMT_SIDE_BOTTOM) {

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

static int case_link_points(SDL_Rect canvas, const Type_Case_Link *link, int pts_x[6], int pts_y[6]) {
    /*
        Purpose: Calculates link endpoint points
        Returns: Success status
    */

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

static int case_segment_distance2(int px, int py, int x1, int y1, int x2, int y2) {
    /*
        Purpose: Calculates the squared distance to a line segment
        Returns: Computed value
    */

    double vx = (double)(x2 - x1);
    double vy = (double)(y2 - y1);
    double wx = (double)(px - x1);
    double wy = (double)(py - y1);
    double c1 = vx * wx + vy * wy;
    double c2 = vx * vx + vy * vy;
    double t = c2 > 0.0 ? c1 / c2 : 0.0;
    double dx;
    double dy;

    if (t < 0.0) {

        t = 0.0;

    }

    if (t > 1.0) {

        t = 1.0;

    }
    dx = (double)px - ((double)x1 + t * vx);
    dy = (double)py - ((double)y1 + t * vy);
    return (int)(dx * dx + dy * dy);
}

static int case_link_at(int x, int y, SDL_Rect canvas) {
    /*
        Purpose: Gets the link at a position
        Returns: Success status
    */

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

static void case_draw_link(SDL_Renderer *renderer, SDL_Rect canvas, int link_index, int related_to_selected_block) {
    /*
        Purpose: Draws the link
        Returns: No value
    */

    if (link_index < 0 || link_index >= Global_Case_Link_Count) {

        return;

    }

    Type_Case_Link *link = &Global_Case_Links[link_index];
    int from_index = case_find_block_index_by_id(link->from_id);
    int to_index = case_find_block_index_by_id(link->to_id);
    int connector = case_connector_px();
    int px[6];
    int py[6];
    int selected = link_index == Global_Case_Selected_Link;
    SDL_Color color =
        selected ? (SDL_Color){255, 150, 45, 255} : (related_to_selected_block ? Case_Border_Hi : Case_Border);

    if (from_index < 0 || to_index < 0) {

        return;

    }

    int point_count = case_link_points(canvas, link, px, py);

    if (point_count < 2) {

        return;

    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int i = 0; i + 1 < point_count; i++) {
        SDL_RenderDrawLine(renderer, px[i], py[i], px[i + 1], py[i + 1]);

        if (selected) {

            SDL_RenderDrawLine(renderer, px[i], py[i] + 1, px[i + 1], py[i + 1] + 1);
            SDL_RenderDrawLine(renderer, px[i] + 1, py[i], px[i + 1] + 1, py[i + 1]);

        }
    }

    SDL_Rect out = {px[0] - connector / 2, py[0] - connector / 2, connector, connector};
    SDL_Rect in = {px[point_count - 1] - connector / 2, py[point_count - 1] - connector / 2, connector, connector};
    draw_filled_rect(renderer, out, color);
    draw_filled_rect(renderer, in, color);
}

static void case_draw_arrow_head(SDL_Renderer *renderer, int x, int y, int direction, SDL_Color color) {
    /*
        Purpose: Draws the arrow head
        Returns: No value
    */

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

static void case_draw_link_preview(SDL_Renderer *renderer, SDL_Rect canvas) {
    /*
        Purpose: Draws the link preview
        Returns: No value
    */

    if (!Global_Case_Link_Dragging || Global_Case_Link_Drag_Start_Index < 0 ||
        Global_Case_Link_Drag_Start_Index >= Global_Case_Block_Count) {

        return;

    }

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

    case_endpoint_center(Global_Case_Link_Drag_Start_Index, canvas, Global_Case_Link_Drag_Start_Side, &x1, &y1);

    if (target >= 0 && target < Global_Case_Block_Count && target_side >= 0) {

        case_endpoint_center(target, canvas, target_side, &x2, &y2);
        end_side = target_side;

    }

    else {

        end_side = case_guess_end_side_for_preview(Global_Case_Link_Drag_Start_Side, x1, y1, x2, y2);

    }

    point_count = case_route_points(x1, y1, Global_Case_Link_Drag_Start_Side, x2, y2, end_side, px, py);

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

static void case_draw_block(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect canvas, int index) {
    /*
        Purpose: Draws the block
        Returns: No value
    */

    SDL_Rect r = case_block_screen_rect(index, canvas);
    Type_Case_Block *b = &Global_Case_Blocks[index];
    int selected = case_is_block_selected(index) || index == Global_Case_Selected;
    SDL_Color border = selected ? Case_Border_Hi : Case_Border;
    int compact = Global_Case_Zoom < 0.68 || r.w < 190 || r.h < 88;
    int connector = (int)((double)CASE_MGMT_CONNECTOR_SIZE * Global_Case_Zoom);

    if (connector < 6) {

        connector = 6;

    }

    if (connector > 14) {

        connector = 14;

    }

    draw_filled_rect(renderer, r, selected ? Case_Panel_2 : Case_Panel);
    draw_outline_rect(renderer, r, border);

    if (selected) {

        SDL_Rect inner = {r.x + 3, r.y + 3, r.w - 6, r.h - 6};
        draw_outline_rect(renderer, inner, border);

    }

    int header_h = compact ? 24 : 28;
    SDL_Rect header = {r.x, r.y, r.w, header_h};
    draw_filled_rect(renderer, header,
                     b->type == CASE_MGMT_BLOCK_CASE ? (SDL_Color){0, 28, 42, 255} : (SDL_Color){0, 35, 14, 255});
    draw_outline_rect(renderer, header, border);

    if (b->type == CASE_MGMT_BLOCK_CASE) {

        char id_label[64];
        char case_line_one[128];
        char case_line_two[128];
        char country_text[128];
        int country_index = case_country_index_by_name(b->country);
        SDL_Texture *flag = case_country_flag_texture(renderer, country_index);

        snprintf(id_label, sizeof(id_label), "CASE %03d", b->id);
        draw_text(renderer, font, id_label, r.x + 10, r.y + 7, Case_Text);

        if (compact) {

            int text_max_px = r.w - 20;

            case_source_short_text(font, b->case_number[0] ? b->case_number : "Case #", case_line_one,
                                   sizeof(case_line_one), text_max_px);
            case_source_short_text(font, b->country[0] ? b->country : "Country", country_text, sizeof(country_text),
                                   text_max_px);
            draw_text(renderer, font, case_line_one, r.x + 10, r.y + 34, Case_Text);

            if (r.h > 62) {

                draw_text(renderer, font, country_text, r.x + 10, r.y + 56, Case_Blue);

            }

        }

        else {

            const int case_value_x = r.x + 146;
            const int country_value_x = r.x + 158;
            const int case_value_max_px = r.x + r.w - 10 - case_value_x;
            const int country_value_max_px = r.x + r.w - 10 - country_value_x;
            SDL_Rect flag_rect = {r.x + 12, r.y + 42, 54, 36};

            case_wrap_block_text_two_lines(font, b->case_number[0] ? b->case_number : "Case #", case_line_one,
                                           sizeof(case_line_one), case_line_two, sizeof(case_line_two),
                                           case_value_max_px);
            case_source_short_text(font, b->country[0] ? b->country : "Country", country_text, sizeof(country_text),
                                   country_value_max_px);

            draw_filled_rect(renderer, flag_rect, (SDL_Color){0, 4, 8, 255});
            draw_outline_rect(renderer, flag_rect, country_index >= 0 ? Case_Blue : Case_Border);

            if (flag) {

                SDL_RenderCopy(renderer, flag, NULL, &flag_rect);

            }

            else if (country_index >= 0) {

                draw_text(renderer, font, CASE_MGMT_COUNTRIES[country_index].alpha2, flag_rect.x + 14, flag_rect.y + 10,
                          Case_Blue);

            }

            draw_text(renderer, font, "CASE #", r.x + 78, r.y + 42, Case_Muted);
            draw_text(renderer, font, case_line_one, case_value_x, r.y + 42, Case_Text);

            if (case_line_two[0]) {

                draw_text(renderer, font, case_line_two, case_value_x, r.y + 62, Case_Text);

            }

            draw_text(renderer, font, "COUNTRY", r.x + 78, r.y + 92, Case_Muted);
            draw_text(renderer, font, country_text, country_value_x, r.y + 92, Case_Blue);

        }

    }

    else {

        SDL_Color status_color = case_status_color(b->status);
        char task[96];
        char user[96];
        char time_text[96];
        char time_short[96];
        char status[96];
        char id_label[64];

        case_make_timeline_text(b, time_text, sizeof(time_text));
        case_shorten(b->task, task, sizeof(task), compact ? 18 : 30);
        case_shorten(b->assigned_to, user, sizeof(user), compact ? 14 : 24);
        case_shorten(time_text, time_short, sizeof(time_short), compact ? 14 : 24);
        case_shorten(b->status, status, sizeof(status), compact ? 12 : 18);
        snprintf(id_label, sizeof(id_label), "TASK %03d", b->id);

        draw_text(renderer, font, id_label, r.x + 10, r.y + 7, Case_Text);

        SDL_Rect status_badge = {r.x + r.w - (compact ? 82 : 92), r.y + 5, compact ? 72 : 82, 18};
        SDL_Rect priority_badge = {status_badge.x - (compact ? 24 : 28), status_badge.y, compact ? 18 : 22,
                                   status_badge.h};
        SDL_Color priority_color = case_priority_color(b->priority);
        char priority_text[2] = {'\0', '\0'};

        if (b->priority[0] >= '1' && b->priority[0] <= '5') {

            priority_text[0] = b->priority[0];

        }

        if (status_badge.x > r.x + 146 && priority_badge.x > r.x + 118) {

            draw_outline_rect(renderer, priority_badge, priority_color);
            case_draw_text_centered(renderer, font, priority_text, priority_badge, priority_color);
            draw_outline_rect(renderer, status_badge, status_color);
            case_draw_text_centered(renderer, font, status, status_badge, status_color);

        }

        if (compact) {

            draw_text(renderer, font, task, r.x + 10, r.y + 34, Case_Text);

            if (r.h > 62) {

                draw_text(renderer, font, status, r.x + 10, r.y + 56, status_color);

            }

        }

        else {

            draw_text(renderer, font, "TASK", r.x + 10, r.y + 38, Case_Muted);
            draw_text(renderer, font, task, r.x + 74, r.y + 38, Case_Text);
            draw_text(renderer, font, "USER", r.x + 10, r.y + 62, Case_Muted);
            draw_text(renderer, font, user, r.x + 74, r.y + 62, Case_Text);
            draw_text(renderer, font, "TIME", r.x + 10, r.y + 86, Case_Muted);
            draw_text(renderer, font, time_short, r.x + 74, r.y + 86, Case_Text);

        }

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

static void case_draw_input(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, const char *text,
                            int active, int cursor, int dropdown) {
    /*
        Purpose: Draws the input
        Returns: No value
    */

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

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > len) {

            cursor = len;

        }
        snprintf(prefix, sizeof(prefix), "%.*s", cursor, text ? text : "");

        if (font) {

            TTF_SizeText(font, prefix, &tw, &th);

        }
        SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
        SDL_RenderDrawLine(renderer, rect.x + 10 + tw, rect.y + 7, rect.x + 10 + tw, rect.y + rect.h - 7);

    }
}

static void case_draw_status_dropdown(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect status_field) {
    /*
        Purpose: Draws the status dropdown
        Returns: No value
    */

    if (!Global_Case_Status_Dropdown_Open) {

        return;

    }

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

static void case_draw_calendar(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect date_field) {
    /*
        Purpose: Draws the calendar
        Returns: No value
    */

    if (!Global_Case_Calendar_Open) {

        return;

    }

    static const char *months[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
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

    snprintf(title, sizeof(title), "%s %d", months[Global_Case_Calendar_Month - 1], Global_Case_Calendar_Year);
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

static int case_text_width(TTF_Font *font, const char *text, int fallback_chars) {
    /*
        Purpose: Calculates rendered text width
        Returns: Text width
    */

    int w = 0;
    int h = 0;

    if (text && font && TTF_SizeText(font, text, &w, &h) == 0) {

        return w;

    }

    if (fallback_chars < 0) {

        fallback_chars = 0;

    }
    return fallback_chars * 8;
}

static void case_draw_description_selection(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *src,
                                            const int starts[128], const int ends[128], int line_count, int first_line,
                                            int max_lines) {
    /*
        Purpose: Draws the description selection
        Returns: No value
    */

    int sel_a = 0;
    int sel_b = 0;
    int line_h = 19;

    if (!case_description_selection_range(&sel_a, &sel_b)) {

        return;

    }

    if (!src) {

        src = "";

    }

    for (int line = first_line; line < line_count && line < first_line + max_lines; line++) {
        int a = sel_a > starts[line] ? sel_a : starts[line];
        int b = sel_b < ends[line] ? sel_b : ends[line];

        if (sel_b > ends[line] && sel_b > starts[line] && line + 1 < line_count) {

            b = ends[line];

        }

        if (a > b) {

            continue;

        }

        if (a == b && !(sel_b > ends[line] && line + 1 < line_count)) {

            continue;

        }

        char before[CASE_MGMT_DESCRIPTION_MAX + 8];
        char selected[CASE_MGMT_DESCRIPTION_MAX + 8];
        int before_len = a - starts[line];
        int selected_len = b - a;
        int x0;
        int w;
        SDL_Rect hl;

        if (before_len < 0) {

            before_len = 0;

        }

        if (selected_len < 0) {

            selected_len = 0;

        }

        if (before_len >= (int)sizeof(before)) {

            before_len = (int)sizeof(before) - 1;

        }

        if (selected_len >= (int)sizeof(selected)) {

            selected_len = (int)sizeof(selected) - 1;

        }

        memcpy(before, src + starts[line], (size_t)before_len);
        before[before_len] = '\0';
        memcpy(selected, src + a, (size_t)selected_len);
        selected[selected_len] = '\0';

        x0 = rect.x + 9 + case_text_width(font, before, before_len);
        w = case_text_width(font, selected, selected_len);

        if (w < 6) {

            w = 6;

        }

        if (x0 < rect.x + 9) {

            x0 = rect.x + 9;

        }

        if (x0 + w > rect.x + rect.w - 9) {

            w = rect.x + rect.w - 9 - x0;

        }

        if (w <= 0) {

            continue;

        }

        hl = (SDL_Rect){x0, rect.y + 6 + ((line - first_line) * line_h), w, line_h};
        draw_filled_rect(renderer, hl, (SDL_Color){0, 78, 120, 255});
        draw_outline_rect(renderer, hl, Case_Blue);
    }
}

static void case_draw_description_box(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label,
                                      const char *text, int active) {
    /*
        Purpose: Draws the description box
        Returns: No value
    */

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

    if (max_lines < 1) {

        max_lines = 1;

    }

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

    if (line_count > max_lines) {

        first_line = line_count - max_lines;

    }

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
        draw_text(renderer, font, short_line, rect.x + 9, y, src[0] || active ? Case_Text : Case_Muted);
        y += line_h;

        if (y + line_h > rect.y + rect.h) {

            break;

        }
    }

    if (active && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {

        int starts[128];
        int ends[128];
        int raw_line_count = case_description_build_lines(src, starts, ends);
        int cursor = Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
        int src_len = (int)strlen(src);
        int cursor_line = 0;

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > src_len) {

            cursor = src_len;

        }

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
            int text_w = 0;
            int text_h = 0;

            if (cursor < line_start) {

                cursor = line_start;

            }

            if (cursor > line_end) {

                cursor = line_end;

            }

            if (cursor > line_start) {

                char before[CASE_MGMT_DESCRIPTION_MAX + 8];
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

            SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
            SDL_RenderDrawLine(renderer, cx, cy0, cx, cy1);
            SDL_RenderDrawLine(renderer, cx + 1, cy0, cx + 1, cy1);

        }

    }
}

static void case_draw_source_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the source-file selector using the Analysis filename-search interface
        Returns: No value
    */

    if (!renderer || !font || !Global_Case_Source_Popup_Open) {

        return;

    }

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

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 155});

    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, Case_Border_Hi);
    SDL_Rect inner = {popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8};
    draw_outline_rect(renderer, inner, Case_Border);

    draw_text(renderer, font, "SOURCE FILE SEARCH", popup.x + 18, popup.y + 20, Case_Text);
    case_draw_source_modal_button(renderer, font, close_btn, "Close", case_point_in_rect(mx, my, close_btn));

    draw_filled_rect(renderer, search,
                     Global_Case_Source_Search_Active ? (SDL_Color){0, 20, 8, 255} : (SDL_Color){0, 5, 2, 255});
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

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > len) {

            cursor = len;

        }

        snprintf(prefix, sizeof(prefix), "%.*s", cursor, Global_Case_Source_Search);

        if (TTF_SizeText(font, prefix, &tw, &th) != 0) {

            tw = cursor * 8;

        }

        SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
        SDL_RenderDrawLine(renderer, search.x + 10 + tw, search.y + 6, search.x + 10 + tw, search.y + search.h - 6);
        SDL_RenderDrawLine(renderer, search.x + 11 + tw, search.y + 6, search.x + 11 + tw, search.y + search.h - 6);

    }

    draw_text(renderer, font, "Currently selected", current_rect.x, current_rect.y - 18, Case_Muted);
    draw_filled_rect(renderer, current_rect, (SDL_Color){0, 20, 8, 255});
    draw_outline_rect(renderer, current_rect, Case_Border_Hi);

    {
        char short_name[CASE_MGMT_SOURCE_FILE_MAX];
        char *current_source = case_selected_field_text(CASE_MGMT_FIELD_SOURCE_FILE);
        const char *current = current_source && current_source[0] ? current_source : "(none selected)";

        case_source_short_text(font, current, short_name, sizeof(short_name), current_rect.w - 20);
        draw_text(renderer, font, short_name, current_rect.x + 10, current_rect.y + 12,
                  current[0] == '(' ? Case_Muted : Case_Text);
    }

    draw_filled_rect(renderer, list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, list, Case_Border);

    if (Global_Case_Source_File_Count <= 0) {

        char empty_msg[640];
        snprintf(empty_msg, sizeof(empty_msg), "No .complex16 files found in %s/", Global_Case_Record_Dir);
        draw_text(renderer, font, empty_msg, list.x + 12, list.y + 14, Case_Warn);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    if (filtered_count <= 0) {

        draw_text(renderer, font, "No files match the search.", list.x + 12, list.y + 14, Case_Warn);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    int visible = list.h / CASE_MGMT_FILE_SEARCH_ROW_H;

    if (visible > 14) {

        visible = 14;

    }

    if (visible < 1) {

        visible = 1;

    }

    Global_Case_Source_Hover = -1;

    if (case_point_in_rect(mx, my, list)) {

        int row = (my - list.y - 4) / CASE_MGMT_FILE_SEARCH_ROW_H;
        int filtered_index = Global_Case_Source_Scroll + row;
        int source_index = case_source_filtered_index_at(filtered_index);

        if (row >= 0 && row < visible && source_index >= 0 && source_index < Global_Case_Source_File_Count) {

            Global_Case_Source_Hover = source_index;

        }

    }

    char *selected_source = case_selected_field_text(CASE_MGMT_FIELD_SOURCE_FILE);

    for (int row = 0; row < visible; row++) {
        int filtered_index = Global_Case_Source_Scroll + row;
        int source_index = case_source_filtered_index_at(filtered_index);
        SDL_Rect item = {list.x + 4, list.y + 4 + row * CASE_MGMT_FILE_SEARCH_ROW_H, list.w - 8,
                         CASE_MGMT_FILE_SEARCH_ROW_H - 3};

        if (source_index < 0 || source_index >= Global_Case_Source_File_Count) {

            break;

        }

        int hovered = source_index == Global_Case_Source_Hover;
        int selected = selected_source && selected_source[0] &&
                       strcmp(selected_source, Global_Case_Source_Files[source_index]) == 0;
        char short_name[CASE_MGMT_SOURCE_FILE_MAX];

        if (hovered) {

            draw_filled_rect(renderer, item, (SDL_Color){0, 44, 16, 255});
            SDL_Rect halo = {item.x - 2, item.y - 2, item.w + 4, item.h + 4};
            draw_outline_rect(renderer, halo, Case_Border_Hi);

        }

        else if (selected) {

            draw_filled_rect(renderer, item, (SDL_Color){15, 85, 45, 245});

        }

        draw_outline_rect(renderer, item,
                          hovered    ? Case_Border_Hi
                          : selected ? (SDL_Color){0, 220, 80, 255}
                                     : (SDL_Color){0, 130, 55, 255});

        case_source_short_text(font, Global_Case_Source_Files[source_index], short_name, sizeof(short_name),
                               item.w - 20);
        draw_text(renderer, font, short_name, item.x + 10, item.y + 8,
                  hovered    ? (SDL_Color){235, 255, 240, 255}
                  : selected ? (SDL_Color){255, 255, 255, 255}
                             : Case_Text);
    }

    char count_label[128];

    if (Global_Case_Source_Search[0]) {

        snprintf(count_label, sizeof(count_label), "%d of %d files", filtered_count, Global_Case_Source_File_Count);

    }

    else {

        snprintf(count_label, sizeof(count_label), "%d files", Global_Case_Source_File_Count);

    }

    draw_text(renderer, font, count_label, popup.x + 18, popup.y + popup.h - 24, Case_Muted);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void case_draw_description_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the description popup
        Returns: No value
    */

    if (!Global_Case_Description_Popup_Open) {

        return;

    }

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

    if (max_lines < 1) {

        max_lines = 1;

    }
    case_clamp_description_scroll(text_rect);

    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, Case_Border_Hi);
    SDL_Rect inner = {popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8};
    draw_outline_rect(renderer, inner, Case_Border);

    draw_text(renderer, font, "DESCRIPTION", popup.x + 18, popup.y + 20, Case_Text);
    case_draw_button(renderer, font, close_btn, "Close", 0, case_point_in_rect(mx, my, close_btn), 0);

    draw_filled_rect(renderer, text_rect, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, text_rect,
                      Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION ? Case_Border_Hi : Case_Border);

    case_draw_description_selection(renderer, font, text_rect, src ? src : "", starts, ends, line_count,
                                    Global_Case_Description_Popup_Scroll, max_lines);

    int y = text_rect.y + 7;
    for (int line = Global_Case_Description_Popup_Scroll;
         line < line_count && line < Global_Case_Description_Popup_Scroll + max_lines; line++) {
        char buf[CASE_MGMT_DESCRIPTION_MAX + 8];
        int n = ends[line] - starts[line];

        if (n >= (int)sizeof(buf)) {

            n = (int)sizeof(buf) - 1;

        }

        if (n < 0) {

            n = 0;

        }
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

    if (Global_Case_Active_Field == CASE_MGMT_FIELD_DESCRIPTION && ((SDL_GetTicks64() / 520ULL) % 2ULL) == 0ULL) {

        int cursor = Global_Case_Field_Cursor[CASE_MGMT_FIELD_DESCRIPTION];
        int src_len = src ? (int)strlen(src) : 0;
        int cursor_line = 0;

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > src_len) {

            cursor = src_len;

        }

        for (int i = 0; i < line_count; i++) {

            if (cursor >= starts[i] && cursor <= ends[i]) {

                cursor_line = i;
                break;

            }

            if (i == line_count - 1 && cursor > ends[i]) {

                cursor_line = i;

            }
        }

        if (cursor_line >= Global_Case_Description_Popup_Scroll &&
            cursor_line < Global_Case_Description_Popup_Scroll + max_lines) {

            int line_start = starts[cursor_line];
            int line_end = ends[cursor_line];
            int text_w = 0;
            int text_h = 0;

            if (cursor < line_start) {

                cursor = line_start;

            }

            if (cursor > line_end) {

                cursor = line_end;

            }

            if (cursor > line_start) {

                char before[CASE_MGMT_DESCRIPTION_MAX + 8];
                int before_len = cursor - line_start;

                if (before_len >= (int)sizeof(before)) {

                    before_len = (int)sizeof(before) - 1;

                }
                memcpy(before, (src ? src : "") + line_start, (size_t)before_len);
                before[before_len] = '\0';

                if (TTF_SizeText(font, before, &text_w, &text_h) != 0) {

                    text_w = before_len * 8;

                }

            }
            int cx = text_rect.x + 9 + text_w;
            int cy0 = text_rect.y + 7 + ((cursor_line - Global_Case_Description_Popup_Scroll) * line_h);
            int cy1 = cy0 + line_h - 2;

            if (cx < text_rect.x + 9) {

                cx = text_rect.x + 9;

            }

            if (cx > text_rect.x + text_rect.w - 9) {

                cx = text_rect.x + text_rect.w - 9;

            }
            SDL_SetRenderDrawColor(renderer, Case_Blue.r, Case_Blue.g, Case_Blue.b, Case_Blue.a);
            SDL_RenderDrawLine(renderer, cx, cy0, cx, cy1);
            SDL_RenderDrawLine(renderer, cx + 1, cy0, cx + 1, cy1);

        }

    }

    if (line_count > max_lines) {

        char scroll_label[96];
        snprintf(scroll_label, sizeof(scroll_label), "Lines %d-%d of %d | drag selects | Ctrl+V pastes",
                 Global_Case_Description_Popup_Scroll + 1,
                 Global_Case_Description_Popup_Scroll + max_lines < line_count
                     ? Global_Case_Description_Popup_Scroll + max_lines
                     : line_count,
                 line_count);
        draw_text(renderer, font, scroll_label, popup.x + 18, popup.y + popup.h - 24, Case_Muted);

    }
}

static void case_draw_file_search_popup(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the file search popup
        Returns: No value
    */

    if (!renderer || !font || !Global_Case_File_Search_Open) {

        return;

    }

    SDL_Rect popup = case_file_search_popup_rect(win_w, win_h);
    SDL_Rect close_btn = {popup.x + popup.w - 86, popup.y + 14, 68, 30};
    SDL_Rect search = case_file_search_input_rect(popup);
    SDL_Rect current_rect = {popup.x + 18, popup.y + 62, popup.w - 36, 42};
    SDL_Rect list = {popup.x + 18, popup.y + 124, popup.w - 36, popup.h - 164};
    int mx = 0;
    int my = 0;
    int filtered_count;

    case_get_adjusted_mouse_state(&mx, &my);
    case_file_search_clamp_scroll();
    filtered_count = case_file_search_filtered_count();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect dim = {0, 0, win_w, win_h};
    draw_filled_rect(renderer, dim, (SDL_Color){0, 0, 0, 155});

    draw_filled_rect(renderer, popup, (SDL_Color){0, 8, 3, 252});
    draw_outline_rect(renderer, popup, Case_Border_Hi);
    SDL_Rect inner = {popup.x + 4, popup.y + 4, popup.w - 8, popup.h - 8};
    draw_outline_rect(renderer, inner, Case_Border);

    draw_text(renderer, font, "DATABASE CASE SEARCH", popup.x + 18, popup.y + 20, Case_Text);
    case_draw_button(renderer, font, close_btn, "Close", 0, case_point_in_rect(mx, my, close_btn), 0);

    draw_filled_rect(renderer, search, Global_Case_File_Search_Active ? Case_Panel_2 : (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, search, Global_Case_File_Search_Active ? Case_Border_Hi : Case_Border);

    if (Global_Case_File_Search_Text[0]) {

        draw_text(renderer, font, Global_Case_File_Search_Text, search.x + 10, search.y + 8, Case_Text);

    }

    else {

        draw_text(renderer, font, "Search database cases", search.x + 10, search.y + 8, Case_Muted);

    }

    if (Global_Case_File_Search_Active && ((SDL_GetTicks64() / 450ULL) % 2ULL) == 0ULL) {

        int tw = 0;
        int th = 0;
        char prefix[CASE_MGMT_FILE_SEARCH_TEXT_MAX];
        int cursor = Global_Case_File_Search_Cursor;
        int len = (int)strlen(Global_Case_File_Search_Text);

        if (cursor < 0) {

            cursor = 0;

        }

        if (cursor > len) {

            cursor = len;

        }
        snprintf(prefix, sizeof(prefix), "%.*s", cursor, Global_Case_File_Search_Text);

        if (font && TTF_SizeText(font, prefix, &tw, &th) != 0) {

            tw = cursor * 8;

        }
        SDL_SetRenderDrawColor(renderer, 0, 170, 255, 255);
        SDL_RenderDrawLine(renderer, search.x + 10 + tw, search.y + 6, search.x + 10 + tw, search.y + search.h - 6);
        SDL_RenderDrawLine(renderer, search.x + 11 + tw, search.y + 6, search.x + 11 + tw, search.y + search.h - 6);

    }

    draw_text(renderer, font, "Click a database case to load it", current_rect.x, current_rect.y - 18, Case_Muted);
    draw_filled_rect(renderer, current_rect, Case_Panel_2);
    draw_outline_rect(renderer, current_rect, Case_Border_Hi);
    {
        char short_name[CASE_MGMT_SOURCE_FILE_MAX];
        case_shorten(Global_Case_File_Name[0] ? Global_Case_File_Name : "(none selected)", short_name,
                     sizeof(short_name), 70);
        draw_text(renderer, font, short_name, current_rect.x + 10, current_rect.y + 12, Case_Text);
    }

    draw_filled_rect(renderer, list, (SDL_Color){0, 5, 2, 255});
    draw_outline_rect(renderer, list, Case_Border);

    if (Global_Case_File_Search_Count <= 0) {

        char empty_msg[640];
        snprintf(empty_msg, sizeof(empty_msg), "No case graphs found in database.");
        draw_text(renderer, font, empty_msg, list.x + 12, list.y + 14, Case_Warn);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    if (filtered_count <= 0) {

        draw_text(renderer, font, "No database cases match the search.", list.x + 12, list.y + 14, Case_Warn);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;

    }

    int visible = list.h / CASE_MGMT_FILE_SEARCH_ROW_H;

    if (visible > 14) {

        visible = 14;

    }

    if (visible < 1) {

        visible = 1;

    }

    Global_Case_File_Search_Hover = -1;

    if (case_point_in_rect(mx, my, list)) {

        int row = (my - list.y - 4) / CASE_MGMT_FILE_SEARCH_ROW_H;
        int filtered_index = Global_Case_File_Search_Scroll + row;
        int index = case_file_search_filtered_index_at(filtered_index);

        if (row >= 0 && row < visible && index >= 0 && index < Global_Case_File_Search_Count) {

            Global_Case_File_Search_Hover = index;

        }

    }

    for (int row = 0; row < visible; row++) {
        int filtered_index = Global_Case_File_Search_Scroll + row;
        int index = case_file_search_filtered_index_at(filtered_index);
        SDL_Rect item = {list.x + 4, list.y + 4 + row * CASE_MGMT_FILE_SEARCH_ROW_H, list.w - 8,
                         CASE_MGMT_FILE_SEARCH_ROW_H - 3};
        int hovered;
        int selected;
        char short_name[CASE_MGMT_SOURCE_FILE_MAX];

        if (index < 0 || index >= Global_Case_File_Search_Count) {

            break;

        }
        hovered = index == Global_Case_File_Search_Hover;
        selected = case_text_equals_ci(Global_Case_File_Search_Files[index], Global_Case_File_Name);

        if (hovered) {

            draw_filled_rect(renderer, item, (SDL_Color){0, 44, 16, 255});
            SDL_Rect halo = {item.x - 2, item.y - 2, item.w + 4, item.h + 4};
            draw_outline_rect(renderer, halo, Case_Border_Hi);

        }

        else if (selected) {

            draw_filled_rect(renderer, item, (SDL_Color){15, 85, 45, 245});

        }

        draw_outline_rect(renderer, item, hovered ? Case_Border_Hi : selected ? Case_Text : Case_Border);
        case_shorten(Global_Case_File_Search_Files[index], short_name, sizeof(short_name), 70);
        draw_text(renderer, font, short_name, item.x + 10, item.y + 8,
                  hovered    ? (SDL_Color){235, 255, 240, 255}
                  : selected ? (SDL_Color){255, 255, 255, 255}
                             : Case_Text);
    }

    char count_label[128];

    if (Global_Case_File_Search_Text[0]) {

        snprintf(count_label, sizeof(count_label), "%d of %d records", filtered_count, Global_Case_File_Search_Count);

    }

    else {

        snprintf(count_label, sizeof(count_label), "%d records", Global_Case_File_Search_Count);

    }
    draw_text(renderer, font, count_label, popup.x + 18, popup.y + popup.h - 24, Case_Muted);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void case_draw_case_dropdown(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect field) {
    /*
        Purpose: Draws the case dropdown
        Returns: No value
    */

    if (!Global_Case_Case_Dropdown_Open) {

        return;

    }

    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count = case_build_case_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);

    if (count <= 0) {

        return;

    }

    case_clamp_case_scroll();
    int visible = count - Global_Case_Case_Scroll;

    if (visible > CASE_MGMT_CASE_MAX_VISIBLE) {

        visible = CASE_MGMT_CASE_MAX_VISIBLE;

    }

    if (visible <= 0) {

        return;

    }

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

        if (hovered) {

            Global_Case_Case_Hover = option_index;

        }
        draw_filled_rect(renderer, row, hovered ? (SDL_Color){0, 70, 30, 250} : (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer, row, hovered ? Case_Border_Hi : Case_Border);
        char short_text[128];
        case_shorten(Global_Case_Case_Options[option_index], short_text, sizeof(short_text), 34);
        draw_text(renderer, font, short_text, row.x + 9, row.y + 6, hovered ? Case_Text : Case_Muted);
    }
}

static void case_draw_country_dropdown(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect field) {
    /*
        Purpose: Draws the country dropdown
        Returns: No value
    */

    if (!Global_Case_Country_Dropdown_Open) {

        return;

    }

    int matches[512];
    int count = case_build_country_matches(matches, (int)(sizeof(matches) / sizeof(matches[0])));

    if (count <= 0) {

        return;

    }

    case_clamp_country_scroll();
    int visible = count - Global_Case_Country_Scroll;

    if (visible > CASE_MGMT_COUNTRY_MAX_VISIBLE) {

        visible = CASE_MGMT_COUNTRY_MAX_VISIBLE;

    }

    if (visible <= 0) {

        return;

    }

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

        if (hovered) {

            Global_Case_Country_Hover = option_index;

        }
        draw_filled_rect(renderer, row, hovered ? (SDL_Color){0, 70, 30, 250} : (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer, row, hovered ? Case_Border_Hi : Case_Border);
        char short_text[128];
        case_shorten(CASE_MGMT_COUNTRIES[option_index].name, short_text, sizeof(short_text), 34);
        draw_text(renderer, font, short_text, row.x + 9, row.y + 6, hovered ? Case_Text : Case_Muted);
    }
}

static void case_draw_user_dropdown(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect field) {
    /*
        Purpose: Draws the user dropdown
        Returns: No value
    */

    int matches[CASE_MGMT_SOURCE_MAX_FILES];
    int count;
    int visible;
    int mx = 0;
    int my = 0;
    SDL_Rect menu;

    if (!Global_Case_User_Dropdown_Open || Global_Case_Active_Field != CASE_MGMT_FIELD_USER) {

        return;

    }

    count = case_build_user_matches(matches, CASE_MGMT_SOURCE_MAX_FILES);
    case_clamp_user_scroll();
    visible = count - Global_Case_User_Scroll;

    if (visible > CASE_MGMT_USER_MAX_VISIBLE) {

        visible = CASE_MGMT_USER_MAX_VISIBLE;

    }

    if (visible <= 0) {

        menu = case_user_dropdown_rect(field, 1);
        draw_filled_rect(renderer, menu, (SDL_Color){0, 0, 0, 245});
        draw_outline_rect(renderer, menu, Case_Border_Hi);
        draw_text(renderer, font, Global_Case_User_Count > 0 ? "No users match" : "No users found", menu.x + 10,
                  menu.y + 7, Case_Warn);
        return;

    }

    case_get_adjusted_mouse_state(&mx, &my);
    menu = case_user_dropdown_rect(field, visible);
    draw_filled_rect(renderer, menu, (SDL_Color){0, 0, 0, 245});
    draw_outline_rect(renderer, menu, Case_Border_Hi);

    Global_Case_User_Hover = -1;
    for (int i = 0; i < visible; i++) {
        int pos = Global_Case_User_Scroll + i;
        int option_index = matches[pos];
        SDL_Rect row = {menu.x, menu.y + i * CASE_MGMT_USER_OPTION_H, menu.w, CASE_MGMT_USER_OPTION_H};
        int hovered = case_point_in_rect(mx, my, row);
        int keyboard_selected = pos == Global_Case_User_Keyboard_Pos;
        char label[AUTH_PUBLIC_USERNAME_MAX + 32];

        if (hovered) {

            Global_Case_User_Hover = option_index;

        }

        draw_filled_rect(renderer, row,
                         hovered || keyboard_selected ? (SDL_Color){0, 70, 30, 250} : (SDL_Color){0, 12, 4, 245});
        draw_outline_rect(renderer, row, hovered || keyboard_selected ? Case_Border_Hi : Case_Border);

        snprintf(label, sizeof(label), "%s%s", Global_Case_User_Options[option_index].username,
                 Global_Case_User_Options[option_index].is_admin ? "  [ADMIN]" : "");
        draw_text(renderer, font, label, row.x + 9, row.y + 7, hovered || keyboard_selected ? Case_Text : Case_Muted);
    }
}

static void case_draw_editor(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect editor) {
    /*
        Purpose: Draws the editor
        Returns: No value
    */

    SDL_Rect fields[CASE_MGMT_FIELD_COUNT];
    SDL_Rect duplicate_btn;
    SDL_Rect delete_btn;
    const char *labels[CASE_MGMT_FIELD_COUNT] = {"Case #",
                                                 "Country",
                                                 "Task Assigned",
                                                 "Assigned User",
                                                 "Start Date (MM/DD/YYYY)",
                                                 "End Date (MM/DD/YYYY)",
                                                 "Status",
                                                 "Priority (1 highest)",
                                                 "Source File",
                                                 "Description"};
    int mx = 0;
    int my = 0;
    case_get_adjusted_mouse_state(&mx, &my);

    draw_filled_rect(renderer, editor, Case_Panel);
    draw_outline_rect(renderer, editor, Case_Border);
    draw_text(renderer, font, case_selected_block_is_case() ? "CASE BLOCK EDITOR" : "TASK BLOCK EDITOR", editor.x + 16,
              editor.y + 16, Case_Text);

    if (Global_Case_Selected < 0 || Global_Case_Selected >= Global_Case_Block_Count) {

        case_draw_case_browser(renderer, font, editor);
        return;

    }

    char selected_label[64];
    snprintf(selected_label, sizeof(selected_label), "Selected Block: %03d",
             Global_Case_Blocks[Global_Case_Selected].id);
    draw_text(renderer, font, selected_label, editor.x + 16, editor.y + 56, Case_Muted);
    int selected_count = case_selected_block_count();

    if (selected_count > 1) {

        char group_label[64];
        snprintf(group_label, sizeof(group_label), "Group Selected: %d blocks", selected_count);
        draw_text(renderer, font, group_label, editor.x + 16, editor.y + 94, Case_Warn);

    }

    case_editor_field_rects(editor, fields, &duplicate_btn, &delete_btn);

    for (int i = 0; i < CASE_MGMT_FIELD_COUNT; i++) {

        if (!case_rect_is_valid(fields[i])) {

            continue;

        }
        char *text = case_selected_field_text(i);
        int active = Global_Case_Active_Field == i ||
                     (i == CASE_MGMT_FIELD_CASE_NUMBER && Global_Case_Case_Dropdown_Open) ||
                     (i == CASE_MGMT_FIELD_COUNTRY && Global_Case_Country_Dropdown_Open) ||
                     (i == CASE_MGMT_FIELD_USER && Global_Case_User_Dropdown_Open) ||
                     (i == CASE_MGMT_FIELD_STATUS && Global_Case_Status_Dropdown_Open) ||
                     (i == CASE_MGMT_FIELD_SOURCE_FILE && Global_Case_Source_Popup_Open) ||
                     ((i == CASE_MGMT_FIELD_START_DATE || i == CASE_MGMT_FIELD_END_DATE) && Global_Case_Calendar_Open &&
                      Global_Case_Calendar_Field == i);

        if (i == CASE_MGMT_FIELD_DESCRIPTION) {

            SDL_Rect open_btn = case_description_open_button_rect(fields[i]);
            case_draw_description_box(renderer, font, fields[i], labels[i], text ? text : "", active);
            case_draw_button(renderer, font, open_btn, "Expand", Global_Case_Description_Popup_Open,
                             case_point_in_rect(mx, my, open_btn), 0);
            continue;

        }

        case_draw_input(renderer, font, fields[i], labels[i], text ? text : "", active, Global_Case_Field_Cursor[i],
                        i == CASE_MGMT_FIELD_STATUS || i == CASE_MGMT_FIELD_SOURCE_FILE);

        if (i == CASE_MGMT_FIELD_STATUS) {

            SDL_Rect swatch = {fields[i].x + fields[i].w - 48, fields[i].y + 8, 18, 14};
            draw_filled_rect(renderer, swatch, case_status_color(text));
            draw_outline_rect(renderer, swatch, case_status_color(text));

        }
    }

    case_draw_button(renderer, font, duplicate_btn, "Duplicate", 0, case_point_in_rect(mx, my, duplicate_btn), 0);

    case_draw_button(renderer, font, delete_btn, "Delete", 0, case_point_in_rect(mx, my, delete_btn), 1);

    if (Global_Case_Calendar_Open && (Global_Case_Calendar_Field == CASE_MGMT_FIELD_START_DATE ||
                                      Global_Case_Calendar_Field == CASE_MGMT_FIELD_END_DATE)) {

        case_draw_calendar(renderer, font, fields[Global_Case_Calendar_Field]);

    }

    if (case_rect_is_valid(fields[CASE_MGMT_FIELD_CASE_NUMBER])) {

        case_draw_case_dropdown(renderer, font, fields[CASE_MGMT_FIELD_CASE_NUMBER]);

    }

    if (case_rect_is_valid(fields[CASE_MGMT_FIELD_COUNTRY])) {

        case_draw_country_dropdown(renderer, font, fields[CASE_MGMT_FIELD_COUNTRY]);

    }

    if (case_rect_is_valid(fields[CASE_MGMT_FIELD_USER])) {

        case_draw_user_dropdown(renderer, font, fields[CASE_MGMT_FIELD_USER]);

    }

    if (case_rect_is_valid(fields[CASE_MGMT_FIELD_STATUS])) {

        case_draw_status_dropdown(renderer, font, fields[CASE_MGMT_FIELD_STATUS]);

    }
}

void CASE_MANAGEMENT_draw_workstation(SDL_Renderer *renderer, TTF_Font *font, int win_w, int win_h) {
    /*
        Purpose: Draws the workstation
        Returns: No value
    */

    SDL_Rect canvas;
    SDL_Rect editor;
    SDL_Rect new_btn;
    SDL_Rect case_btn;
    SDL_Rect link_btn;
    SDL_Rect save_btn;
    SDL_Rect load_btn;
    SDL_Rect undo_btn;
    SDL_Rect file_rect;
    SDL_Rect delete_database_btn;
    Uint64 now = SDL_GetTicks64();
    int mx = 0;
    int my = 0;

    if (!renderer || !Global_CaseManagement_Mode) {

        return;

    }

    canvas = case_canvas_rect(win_w, win_h);
    editor = case_editor_rect(win_w, win_h);
    case_ensure_view(canvas);
    case_toolbar_rects(win_w, &new_btn, &case_btn, &link_btn, &save_btn, &load_btn, &undo_btn, &file_rect);
    delete_database_btn = case_delete_database_record_rect(win_w);

    if (file_rect.x + file_rect.w > delete_database_btn.x - 10) {

        file_rect.w = delete_database_btn.x - 10 - file_rect.x;

        if (file_rect.w < 80) {

            file_rect.w = 80;

        }

    }
    case_get_adjusted_mouse_state(&mx, &my);

    if ((Global_Case_Selected < 0 || Global_Case_Selected >= Global_Case_Block_Count) &&
        !Global_Case_Metadata_Search_Active && !Global_Case_Metadata_Name_Active &&
        !Global_Case_Metadata_Description_Active && !Global_Case_Metadata_Delete_Confirm_Open &&
        now - Global_Case_Metadata_Last_Refresh > 1200) {

        case_metadata_refresh();

    }

    SDL_SetRenderDrawColor(renderer, Case_BG.r, Case_BG.g, Case_BG.b, Case_BG.a);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0, 50, 20, 120);
    for (int x = 0; x < win_w; x += 48) {
        SDL_RenderDrawLine(renderer, x, 0, x, win_h);
    }
    for (int y = 0; y < win_h; y += 48) {
        SDL_RenderDrawLine(renderer, 0, y, win_w, y);
    }

    case_draw_button(renderer, font, new_btn, "+ Task", 0, case_point_in_rect(mx, my, new_btn), 0);
    case_draw_button(renderer, font, case_btn, "+ Case", 0, case_point_in_rect(mx, my, case_btn), 0);
    case_draw_button(renderer, font, link_btn, Global_Case_Link_Mode ? "Link: ON" : "Link: OFF", Global_Case_Link_Mode,
                     case_point_in_rect(mx, my, link_btn), 0);
    case_draw_button(renderer, font, save_btn, "Save", 0, case_point_in_rect(mx, my, save_btn), 0);
    case_draw_button(renderer, font, load_btn, "Load", 0, case_point_in_rect(mx, my, load_btn), 0);
    case_draw_button(renderer, font, undo_btn, "Undo", Global_Case_Undo_Count > 0, case_point_in_rect(mx, my, undo_btn),
                     0);
    case_draw_button(renderer, font, delete_database_btn, "Delete Database Record", 0,
                     case_point_in_rect(mx, my, delete_database_btn), 1);

    case_draw_input(renderer, font, file_rect, "Database Record", Global_Case_File_Name, Global_Case_File_Name_Active,
                    Global_Case_File_Name_Cursor, 0);

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

        draw_text(renderer, font, "Widen the window to show the block editor.", canvas.x + 16, canvas.y + canvas.h - 28,
                  Case_Warn);

    }

    case_draw_source_popup(renderer, font, win_w, win_h);
    case_draw_description_popup(renderer, font, win_w, win_h);
    case_draw_file_search_popup(renderer, font, win_w, win_h);
    case_metadata_draw_delete_confirmation(renderer, font, win_w, win_h);
    case_draw_database_delete_confirmation(renderer, font, win_w, win_h);

    if (Global_Case_Link_Mode) {

        const char *hint = Global_Case_Link_Source >= 0 ? "LINK MODE: source selected, click destination block"
                                                        : "LINK MODE: click source block";
        draw_text(renderer, font, hint, canvas.x + 14, canvas.y + 12, Case_Warn);

    }

    if (Global_Case_Status[0] != '\0') {

        SDL_Color status_color = (now - Global_Case_Status_Time < 2500) ? Case_Text : Case_Muted;
        draw_text(renderer, font, Global_Case_Status, CASE_MGMT_MARGIN + 8, win_h - 32, status_color);

    }
}
