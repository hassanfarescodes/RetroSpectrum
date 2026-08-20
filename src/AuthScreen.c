#define _POSIX_C_SOURCE 200809L

/*
 * ============================================================================
 * File:            AuthScreen.c
 * Author:          Hassan Fares
 *
 * Description:     Authentication and login interface for RetroSpectrum
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux
 *
 *                                                               05/04/2026
 * ============================================================================
 */

#include "AuthScreen.h"
#include "AuthAdmin.h"
#include "DatabaseCrypto.h"
#include "SecureFunctions.h"
#include "SecureNetwork.h"
#include "ServerIdentity.h"

#include <stddef.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <argon2.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <sqlite3.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define AUTH_USERNAME_MAX 63
#define AUTH_PASSWORD_MAX 127
#define AUTH_CODE_MAX 6
#define AUTH_STATUS_MAX 384

#define AUTH_ARGON2_SALT_BYTES 16
#define AUTH_ARGON2_HASH_BYTES 32
#define AUTH_ARGON2_ENCODED_MAX 256
#define AUTH_ARGON2_TIME_COST 3U
#define AUTH_ARGON2_MEMORY_KIB 65536U
#define AUTH_ARGON2_PARALLELISM 4U

#define AUTH_TOTP_SECRET_BYTES 32
#define AUTH_TOTP_SALT_BYTES 16
#define AUTH_TOTP_NONCE_BYTES 12
#define AUTH_TOTP_TAG_BYTES 16
#define AUTH_TOTP_CIPHER_BYTES AUTH_TOTP_SECRET_BYTES
#define AUTH_TOTP_BASE32_MAX 128
#define AUTH_TOTP_ALGORITHM_MAX 16
#define AUTH_TOTP_ALGORITHM_DEFAULT "sha512"
#define AUTH_TOTP_KDF_DEFAULT "server-sha512"
#define AUTH_TOTP_MASTER_KEY_BYTES 32
#define AUTH_TOTP_MASTER_KEY_FILENAME "totp_master.key"
#define AUTH_TOTP_PERIOD_SECONDS 30
#define AUTH_TOTP_DIGITS 6

#define AUTH_RATE_LIMIT_WINDOW_SECONDS 900
#define AUTH_RATE_LIMIT_THRESHOLD 5
#define AUTH_RATE_LIMIT_BASE_LOCK_SECONDS 30
#define AUTH_RATE_LIMIT_MAX_LOCK_SECONDS 900

#define AUTH_FIELD_NONE -1
#define AUTH_FIELD_USERNAME 0
#define AUTH_FIELD_PASSWORD 1
#define AUTH_FIELD_CONFIRM_PASSWORD 2
#define AUTH_FIELD_CODE 3
#define AUTH_FIELD_IMPORT_PATH 4

typedef enum Type_Auth_Stage {
    AUTH_STAGE_LOGIN = 0,
    AUTH_STAGE_LOGIN_TWO_FACTOR,
    AUTH_STAGE_AUTHORIZE_CREATE,
    AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR,
    AUTH_STAGE_CREATE_USER,
    AUTH_STAGE_CREATE_TWO_FACTOR,
    AUTH_STAGE_DATABASE_KEY_PATH,
    AUTH_STAGE_CHANGE_SERVER_PATH,
    AUTH_STAGE_CHANGE_SERVER_CONFIRM
} Type_Auth_Stage;

typedef struct Type_Auth_User_Record {
    char password_encoded[AUTH_ARGON2_ENCODED_MAX];
    int totp_enabled;
    char totp_algorithm[AUTH_TOTP_ALGORITHM_MAX];
    int totp_secret_bytes;
    char totp_kdf_algorithm[AUTH_TOTP_ALGORITHM_MAX];
    int role;
    int64_t last_totp_counter;
    unsigned char totp_salt[AUTH_TOTP_SALT_BYTES];
    unsigned char totp_nonce[AUTH_TOTP_NONCE_BYTES];
    unsigned char totp_tag[AUTH_TOTP_TAG_BYTES];
    unsigned char totp_ciphertext[AUTH_TOTP_CIPHER_BYTES];
} Type_Auth_User_Record;

typedef struct Type_Auth_State {
    Type_Auth_Stage stage;
    int active_field;
    int enable_two_factor;
    int user_count;
    char username[AUTH_USERNAME_MAX + 1];
    char password[AUTH_PASSWORD_MAX + 1];
    char confirm_password[AUTH_PASSWORD_MAX + 1];
    char code[AUTH_CODE_MAX + 1];
    char status[AUTH_STATUS_MAX];
    SDL_Color status_color;
    unsigned char active_totp_secret[AUTH_TOTP_SECRET_BYTES];
    int active_totp_secret_valid;
    int active_totp_secret_bytes;
    char active_totp_algorithm[AUTH_TOTP_ALGORITHM_MAX];
    int admin_console_ready;
    int database_ready;
    char import_path[PATH_MAX];
    size_t import_cursor;
    int import_select_all;
    Type_Server_Public_Identity pending_server;
    int pending_server_valid;
} Type_Auth_State;

static const SDL_Color AUTH_BG = {0, 0, 0, 255};
static const SDL_Color AUTH_PANEL = {0, 12, 5, 250};
static const SDL_Color AUTH_FIELD_BG = {0, 20, 8, 255};
static const SDL_Color AUTH_GRID = {0, 45, 18, 120};
static const SDL_Color AUTH_BORDER = {0, 150, 60, 255};
static const SDL_Color AUTH_BORDER_ACTIVE = {0, 255, 90, 255};
static const SDL_Color AUTH_TEXT = {0, 255, 90, 255};
static const SDL_Color AUTH_MUTED = {0, 155, 65, 255};
static const SDL_Color AUTH_ERROR = {255, 75, 55, 255};
static const SDL_Color AUTH_WARN = {255, 180, 40, 255};

static char Global_Auth_Server_Id[SERVER_IDENTITY_ID_BUFFER] = "";
static char Global_Auth_Server_Name[SERVER_IDENTITY_SERVER_NAME_BUFFER] = "RetroSpectrum Server";
static char Global_Auth_Current_Username[AUTH_USERNAME_MAX + 1] = "";
static int Global_Auth_Client_Only_Mode = 0;

static int auth_point_in_rect(int x, int y, SDL_Rect rect) {
    /*
        Purpose: Checks whether a point lies inside a rectangle
        Returns: Boolean status
    */

    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

static void auth_secure_zero(void *memory, size_t size) {
    /*
        Purpose: Clears sensitive memory
        Returns: No value
    */

    if (memory && size > 0) {

        OPENSSL_cleanse(memory, size);

    }
}

static void auth_copy_text(char *destination, size_t destination_size, const char *source) {
    /*
        Purpose: Copies the text
        Returns: No value
    */

    if (!destination || destination_size == 0) {

        return;

    }

    if (!source) {

        source = "";

    }
    (void)sec_strcpy(destination, destination_size, source);
}

static void auth_set_status(Type_Auth_State *state, SDL_Color color, const char *message) {
    /*
        Purpose: Sets the status
        Returns: No value
    */

    if (!state) {

        return;

    }
    auth_copy_text(state->status, sizeof(state->status), message);
    state->status_color = color;
}

static size_t auth_utf8_previous_index(const char *text, size_t index) {
    /*
        Purpose: Gets the UTF-8 previous index
        Returns: Item index
    */

    if (!text || index == 0) {

        return 0;

    }

    index--;

    while (index > 0 && (((unsigned char)text[index] & 0xC0U) == 0x80U)) {
        index--;
    }

    return index;
}

static size_t auth_utf8_next_index(const char *text, size_t length, size_t index) {
    /*
        Purpose: Gets the UTF-8 next index
        Returns: Item index
    */

    if (!text || index >= length) {

        return length;

    }
    index++;
    while (index < length && (((unsigned char)text[index] & 0xC0U) == 0x80U)) {
        index++;
    }
    return index;
}

static void auth_import_path_clamp_cursor(Type_Auth_State *state) {
    /*
        Purpose: Imports the path clamp cursor
        Returns: No value
    */

    size_t length;

    if (!state) {

        return;

    }
    length = strlen(state->import_path);

    if (state->import_cursor > length) {

        state->import_cursor = length;

    }
}

static void auth_import_path_replace_selection(Type_Auth_State *state) {
    /*
        Purpose: Imports the path replace selection
        Returns: No value
    */

    if (!state) {

        return;

    }

    if (!state->import_select_all) {

        return;

    }

    state->import_path[0] = '\0';
    state->import_cursor = 0;
    state->import_select_all = 0;
}

static void auth_import_path_insert(Type_Auth_State *state, const char *text) {
    /*
        Purpose: Imports the path insert
        Returns: No value
    */

    char filtered[PATH_MAX];
    size_t filtered_length = 0;
    size_t current_length;
    size_t available;

    if (!state || !text) {

        return;

    }
    auth_import_path_replace_selection(state);
    auth_import_path_clamp_cursor(state);

    for (size_t index = 0; text[index] != '\0' && filtered_length + 1 < sizeof(filtered); index++) {
        unsigned char character = (unsigned char)text[index];

        if (character == '\r' || character == '\n' || character == '\t' || character == '\0') {

            continue;

        }

        if (character < 0x20U) {

            continue;

        }
        filtered[filtered_length++] = (char)character;
    }
    filtered[filtered_length] = '\0';

    current_length = strlen(state->import_path);
    available = sizeof(state->import_path) - current_length - 1;

    if (filtered_length > available) {

        filtered_length = available;

    }

    if (filtered_length == 0) {

        return;

    }

    if (!sec_memmove(state->import_path + state->import_cursor + filtered_length,
                     sizeof(state->import_path) - state->import_cursor - filtered_length,
                     state->import_path + state->import_cursor, current_length - state->import_cursor + 1) ||
        !sec_memcpy(state->import_path + state->import_cursor, sizeof(state->import_path) - state->import_cursor,
                    filtered, filtered_length)) {

        return;

    }

    state->import_cursor += filtered_length;
}

static void auth_import_path_backspace(Type_Auth_State *state) {
    /*
        Purpose: Imports the path backspace
        Returns: No value
    */

    size_t length;
    size_t previous;

    if (!state) {

        return;

    }

    if (state->import_select_all) {

        auth_import_path_replace_selection(state);
        return;

    }
    auth_import_path_clamp_cursor(state);

    if (state->import_cursor == 0) {

        return;

    }
    length = strlen(state->import_path);
    previous = auth_utf8_previous_index(state->import_path, state->import_cursor);

    if (!sec_memmove(state->import_path + previous, sizeof(state->import_path) - previous,
                     state->import_path + state->import_cursor, length - state->import_cursor + 1)) {

        return;

    }

    state->import_cursor = previous;
}

static void auth_import_path_delete(Type_Auth_State *state) {
    /*
        Purpose: Imports the path delete
        Returns: No value
    */

    size_t length;
    size_t next;

    if (!state) {

        return;

    }

    if (state->import_select_all) {

        auth_import_path_replace_selection(state);
        return;

    }
    auth_import_path_clamp_cursor(state);
    length = strlen(state->import_path);

    if (state->import_cursor >= length) {

        return;

    }
    next = auth_utf8_next_index(state->import_path, length, state->import_cursor);

    if (!sec_memmove(state->import_path + state->import_cursor, sizeof(state->import_path) - state->import_cursor,
                     state->import_path + next, length - next + 1)) {

        return;

    }
}

static void auth_import_path_copy(Type_Auth_State *state) {
    /*
        Purpose: Imports the path copy
        Returns: No value
    */

    if (!state) {

        return;

    }

    if (SDL_SetClipboardText(state->import_path) == 0) {

        auth_set_status(state, AUTH_MUTED, "File path copied to the clipboard.");

    }

    else {

        auth_set_status(state, AUTH_ERROR, "Unable to copy the public-key path.");

    }
}

static void auth_import_path_paste(Type_Auth_State *state) {
    /*
        Purpose: Imports the path paste
        Returns: No value
    */

    char *clipboard;

    if (!state) {

        return;

    }
    clipboard = SDL_GetClipboardText();

    if (!clipboard) {

        auth_set_status(state, AUTH_ERROR, "Unable to read the clipboard.");
        return;

    }
    auth_import_path_insert(state, clipboard);
    SDL_free(clipboard);
    auth_set_status(state, AUTH_MUTED, "Path pasted from the clipboard.");
}

static void auth_clear_sensitive(Type_Auth_State *state) {
    /*
        Purpose: Clears sensitive authentication data
        Returns: No value
    */

    if (!state) {

        return;

    }
    auth_secure_zero(state->password, sizeof(state->password));
    auth_secure_zero(state->confirm_password, sizeof(state->confirm_password));
    auth_secure_zero(state->code, sizeof(state->code));
    auth_secure_zero(state->active_totp_secret, sizeof(state->active_totp_secret));
    state->active_totp_secret_valid = 0;
    state->active_totp_secret_bytes = 0;
    auth_secure_zero(state->active_totp_algorithm, sizeof(state->active_totp_algorithm));
}

static void auth_draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    /*
        Purpose: Draws the text
        Returns: No value
    */

    SDL_Surface *surface;
    SDL_Texture *texture;
    SDL_Rect destination;

    if (!renderer || !font || !text || text[0] == '\0') {

        return;

    }

    surface = TTF_RenderUTF8_Blended(font, text, color);

    if (!surface) {

        return;

    }

    texture = SDL_CreateTextureFromSurface(renderer, surface);

    if (!texture) {

        SDL_FreeSurface(surface);
        return;

    }

    destination = (SDL_Rect){x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &destination);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

static void auth_draw_centered_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect,
                                    SDL_Color color) {
    /*
        Purpose: Draws the centered text
        Returns: No value
    */

    int width = 0;
    int height = 0;

    if (!font || !text || TTF_SizeUTF8(font, text, &width, &height) != 0) {

        return;

    }

    auth_draw_text(renderer, font, text, rect.x + (rect.w - width) / 2, rect.y + (rect.h - height) / 2, color);
}

static void auth_fill_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Fills the rectangle
        Returns: No value
    */

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

static void auth_outline_rect(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Outlines the rectangle
        Returns: No value
    */

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

static void auth_draw_grid(SDL_Renderer *renderer, int width, int height, int offset) {
    /*
        Purpose: Draws the grid
        Returns: No value
    */

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, AUTH_GRID.r, AUTH_GRID.g, AUTH_GRID.b, AUTH_GRID.a);

    for (int x = -48 + offset; x < width + 48; x += 48) {
        SDL_RenderDrawLine(renderer, x, 0, x, height);
    }
    for (int y = -48 + offset; y < height + 48; y += 48) {
        SDL_RenderDrawLine(renderer, 0, y, width, y);
    }
}

static SDL_Rect auth_top_right_button_rect(int width) {
    /*
        Purpose: Computes the top right button rectangle
        Returns: Computed rectangle
    */

    SDL_Rect rect = {width - 252, 18, 232, 40};

    if (rect.x < 20) {

        rect.x = 20;
        rect.w = width - 40;

    }
    return rect;
}

static SDL_Rect auth_top_left_button_rect(int width) {
    /*
        Purpose: Computes the top left button rectangle
        Returns: Computed rectangle
    */

    SDL_Rect rect = {20, 18, 190, 40};

    if (width < 460) {

        rect.w = (width - 60) / 2;

    }
    return rect;
}

static SDL_Rect auth_database_key_button_rect(int width) {
    /*
        Purpose: Computes the database key button rectangle
        Returns: Computed rectangle
    */

    SDL_Rect rect = {20, 66, 190, 40};

    if (width < 460) {

        rect.w = (width - 60) / 2;

    }
    return rect;
}

static void auth_make_panel(int width, int height, Type_Auth_Stage stage, SDL_Rect *panel) {
    /*
        Purpose: Builds the panel
        Returns: No value
    */

    int panel_width = 620;
    int panel_height = 440;
    int reserved_top = 160;
    int available_height = height - reserved_top - 18;

    if (stage == AUTH_STAGE_CREATE_USER) {

        panel_height = 570;

    }

    else if (stage == AUTH_STAGE_CREATE_TWO_FACTOR) {

        panel_height = 600;

    }

    else if (stage == AUTH_STAGE_DATABASE_KEY_PATH || stage == AUTH_STAGE_CHANGE_SERVER_PATH) {

        panel_height = 470;

    }

    else if (stage == AUTH_STAGE_CHANGE_SERVER_CONFIRM) {

        panel_height = 530;

    }

    if (panel_width > width - 40) {

        panel_width = width - 40;

    }

    if (panel_height > available_height) {

        panel_height = available_height;

    }

    if (panel_height < 400) {

        panel_height = height - 40;
        reserved_top = 20;

    }

    *panel = (SDL_Rect){(width - panel_width) / 2, reserved_top + (height - reserved_top - panel_height) / 2,
                        panel_width, panel_height};
}

static SDL_Rect auth_field_rect(SDL_Rect panel, int row) {
    /*
        Purpose: Computes the field rectangle
        Returns: Computed rectangle
    */

    return (SDL_Rect){panel.x + 70, panel.y + 145 + row * 82, panel.w - 140, 46};
}

static SDL_Rect auth_primary_button_rect(SDL_Rect panel) {
    /*
        Purpose: Computes the primary button rectangle
        Returns: Computed rectangle
    */

    return (SDL_Rect){panel.x + panel.w - 220, panel.y + panel.h - 72, 150, 42};
}

static SDL_Rect auth_checkbox_rect(SDL_Rect panel) {
    /*
        Purpose: Computes the checkbox rectangle
        Returns: Computed rectangle
    */

    return (SDL_Rect){panel.x + 70, panel.y + 395, 22, 22};
}

static SDL_Rect auth_copy_secret_button_rect(SDL_Rect panel) {
    /*
        Purpose: Computes the copy-secret button rectangle
        Returns: Computed rectangle
    */

    return (SDL_Rect){panel.x + panel.w - 198, panel.y + 250, 128, 36};
}

static SDL_Rect auth_create_two_factor_code_rect(SDL_Rect panel) {
    /*
        Purpose: Creates the two factor code rectangle
        Returns: Computed rectangle
    */

    return (SDL_Rect){panel.x + 70, panel.y + 360, panel.w - 140, 46};
}

static void auth_mask_text(const char *input, char *output, size_t output_size) {
    /*
        Purpose: Masks the text
        Returns: No value
    */

    size_t length;

    if (!output || output_size == 0) {

        return;

    }

    if (!input) {

        input = "";

    }

    length = strlen(input);

    if (length >= output_size) {

        length = output_size - 1;

    }
    memset(output, '*', length);
    output[length] = '\0';
}

static void auth_draw_field(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, const char *value,
                            int active, int masked) {
    /*
        Purpose: Draws the field
        Returns: No value
    */

    char display[AUTH_PASSWORD_MAX + 2];
    SDL_Color border = active ? AUTH_BORDER_ACTIVE : AUTH_BORDER;

    if (masked) {

        auth_mask_text(value, display, sizeof(display));
        value = display;

    }

    auth_draw_text(renderer, font, label, rect.x, rect.y - 24, AUTH_MUTED);
    auth_fill_rect(renderer, rect, AUTH_FIELD_BG);
    auth_outline_rect(renderer, rect, border);
    auth_draw_text(renderer, font, value && value[0] ? value : " ", rect.x + 12, rect.y + 12, AUTH_TEXT);

    if (active && ((SDL_GetTicks64() / 500U) % 2U) == 0U) {

        int text_width = 0;
        int text_height = 0;

        if (value && TTF_SizeUTF8(font, value, &text_width, &text_height) == 0) {

            int cursor_x = rect.x + 12 + text_width + 2;
            SDL_SetRenderDrawColor(renderer, AUTH_BORDER_ACTIVE.r, AUTH_BORDER_ACTIVE.g, AUTH_BORDER_ACTIVE.b,
                                   AUTH_BORDER_ACTIVE.a);
            SDL_RenderDrawLine(renderer, cursor_x, rect.y + 9, cursor_x, rect.y + rect.h - 9);

        }

    }
}

static size_t auth_path_line_end(TTF_Font *font, const char *text, size_t start, size_t length, int maximum_width) {
    /*
        Purpose: Finds the end of the current path line
        Returns: Computed size
    */

    size_t index = start;
    size_t best = start;
    char segment[PATH_MAX];
    int width = 0;
    int height = 0;

    if (!font || !text || start >= length || maximum_width <= 0) {

        return start;

    }

    while (index < length) {
        size_t next = auth_utf8_next_index(text, length, index);
        size_t bytes = next - start;

        if (bytes >= sizeof(segment)) {

            break;

        }

        if (!sec_str_memcpy(segment, sizeof(segment), text + start, bytes)) {

            return best;

        }

        if (TTF_SizeUTF8(font, segment, &width, &height) != 0 || width > maximum_width) {

            break;

        }
        best = next;
        index = next;
    }

    if (best == start) {

        best = auth_utf8_next_index(text, length, start);

    }
    return best;
}

static void auth_draw_import_path_field(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect,
                                        const Type_Auth_State *state, const char *label, const char *placeholder) {
    /*
        Purpose: Draws the import path field
        Returns: No value
    */

    size_t starts[256];
    size_t ends[256];
    size_t length;
    size_t cursor;
    int line_count = 0;
    int cursor_line = 0;
    int line_height = 18;
    int maximum_lines;
    int first_line = 0;
    SDL_Rect clip;
    SDL_Color border;

    if (!renderer || !font || !state) {

        return;

    }

    auth_draw_text(renderer, font, label ? label : "File path", rect.x, rect.y - 24, AUTH_MUTED);
    auth_fill_rect(renderer, rect, AUTH_FIELD_BG);
    border = state->active_field == AUTH_FIELD_IMPORT_PATH ? AUTH_BORDER_ACTIVE : AUTH_BORDER;
    auth_outline_rect(renderer, rect, border);

    clip = (SDL_Rect){rect.x + 9, rect.y + 7, rect.w - 18, rect.h - 14};

    if (clip.w <= 0 || clip.h <= 0) {

        return;

    }
    SDL_RenderSetClipRect(renderer, &clip);

    {
        int measured_width = 0;
        int measured_height = 0;

        if (TTF_SizeUTF8(font, "Ag", &measured_width, &measured_height) == 0 && measured_height > 0) {

            line_height = measured_height + 3;

        }
    }

    maximum_lines = clip.h / line_height;

    if (maximum_lines < 1) {

        maximum_lines = 1;

    }

    length = strlen(state->import_path);
    cursor = state->import_cursor <= length ? state->import_cursor : length;

    if (length == 0) {

        auth_draw_text(renderer, font, placeholder ? placeholder : "Paste or type the file path here", clip.x, clip.y,
                       AUTH_MUTED);
        line_count = 1;
        starts[0] = 0;
        ends[0] = 0;
        cursor_line = 0;

    }

    else {

        size_t start = 0;
        while (start < length && line_count < (int)(sizeof(starts) / sizeof(starts[0]))) {
            size_t end = auth_path_line_end(font, state->import_path, start, length, clip.w);
            starts[line_count] = start;
            ends[line_count] = end;

            if (cursor >= start && cursor <= end) {

                cursor_line = line_count;

            }
            line_count++;
            start = end;
        }

        if (cursor == length && line_count > 0) {

            cursor_line = line_count - 1;

        }

    }

    if (cursor_line >= maximum_lines) {

        first_line = cursor_line - maximum_lines + 1;

    }

    if (first_line + maximum_lines > line_count) {

        first_line = line_count - maximum_lines;

        if (first_line < 0) {

            first_line = 0;

        }

    }

    for (int visible = 0; visible < maximum_lines; visible++) {
        int line = first_line + visible;
        char segment[PATH_MAX];
        size_t bytes;
        int y;

        if (line >= line_count) {

            break;

        }
        bytes = ends[line] - starts[line];

        if (bytes >= sizeof(segment)) {

            bytes = sizeof(segment) - 1;

        }

        if (!sec_str_memcpy(segment, sizeof(segment), state->import_path + starts[line], bytes)) {

            break;

        }

        y = clip.y + visible * line_height;

        if (state->import_select_all && length > 0) {

            SDL_Rect highlight = {clip.x, y, clip.w, line_height};
            auth_fill_rect(renderer, highlight, (SDL_Color){0, 72, 30, 210});

        }
        auth_draw_text(renderer, font, segment, clip.x, y,
                       state->import_select_all ? (SDL_Color){210, 255, 225, 255} : AUTH_TEXT);

        if (state->active_field == AUTH_FIELD_IMPORT_PATH && !state->import_select_all && line == cursor_line &&
            ((SDL_GetTicks64() / 500U) % 2U) == 0U) {

            char prefix[PATH_MAX];
            size_t prefix_bytes = cursor > starts[line] ? cursor - starts[line] : 0;
            int prefix_width = 0;
            int prefix_height = 0;
            int cursor_x;

            if (prefix_bytes >= sizeof(prefix)) {

                prefix_bytes = sizeof(prefix) - 1;

            }

            if (!sec_str_memcpy(prefix, sizeof(prefix), state->import_path + starts[line], prefix_bytes)) {

                prefix[0] = '\0';

            }

            (void)TTF_SizeUTF8(font, prefix, &prefix_width, &prefix_height);
            cursor_x = clip.x + prefix_width + 1;
            SDL_SetRenderDrawColor(renderer, AUTH_BORDER_ACTIVE.r, AUTH_BORDER_ACTIVE.g, AUTH_BORDER_ACTIVE.b,
                                   AUTH_BORDER_ACTIVE.a);
            SDL_RenderDrawLine(renderer, cursor_x, y, cursor_x, y + line_height - 3);

        }
    }

    SDL_RenderSetClipRect(renderer, NULL);
}

static void auth_draw_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, int hovered) {
    /*
        Purpose: Draws the button
        Returns: No value
    */

    auth_fill_rect(renderer, rect, AUTH_FIELD_BG);
    auth_outline_rect(renderer, rect, hovered ? AUTH_BORDER_ACTIVE : AUTH_BORDER);
    auth_draw_centered_text(renderer, font, label, rect, AUTH_TEXT);
}

static void auth_draw_danger_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label,
                                    int hovered) {
    /*
        Purpose: Draws the danger button
        Returns: No value
    */

    SDL_Color background = hovered ? (SDL_Color){100, 15, 12, 255} : (SDL_Color){55, 8, 7, 255};
    SDL_Color border = hovered ? (SDL_Color){255, 95, 75, 255} : AUTH_ERROR;

    auth_fill_rect(renderer, rect, background);
    auth_outline_rect(renderer, rect, border);
    auth_draw_centered_text(renderer, font, label, rect, (SDL_Color){255, 210, 205, 255});
}

static void auth_draw_trusted_server_header(SDL_Renderer *renderer, TTF_Font *font, int width) {
    /*
        Purpose: Draws the trusted server header
        Returns: No value
    */

    const char *name = SERVER_IDENTITY_get_trusted_name();
    const char *fingerprint = SERVER_IDENTITY_get_trusted_fingerprint();
    char first_half[65];
    char second_half[65];
    SDL_Rect center = {220, 0, width - 440, 96};

    if (center.w < 240) {

        center.x = 20;
        center.w = width - 40;

    }
    memset(first_half, 0, sizeof(first_half));
    memset(second_half, 0, sizeof(second_half));

    if (fingerprint) {

        snprintf(first_half, sizeof(first_half), "%.64s", fingerprint);

        if (strlen(fingerprint) > 64) {

            snprintf(second_half, sizeof(second_half), "%.64s", fingerprint + 64);

        }

    }

    auth_draw_centered_text(renderer, font, name && name[0] ? name : "UNNAMED RETROSPECTRUM SERVER",
                            (SDL_Rect){center.x, 8, center.w, 24}, AUTH_TEXT);
    auth_draw_centered_text(renderer, font, "ML-DSA-87 PUBLIC KEY / SHA-512 FINGERPRINT",
                            (SDL_Rect){center.x, 34, center.w, 18}, AUTH_MUTED);
    auth_draw_centered_text(renderer, font, first_half, (SDL_Rect){center.x, 56, center.w, 18}, AUTH_TEXT);
    auth_draw_centered_text(renderer, font, second_half, (SDL_Rect){center.x, 76, center.w, 18}, AUTH_TEXT);
}

static void auth_format_12_hour_time(int64_t timestamp, char *output, size_t output_size) {
    /*
        Purpose: Formats the 12 hour time
        Returns: No value
    */

    time_t value = (time_t)timestamp;
    struct tm local_time;
    char formatted[64];

    if (!output || output_size == 0) {

        return;

    }
    snprintf(output, output_size, "%s", "waiting for signed announcement");

    if (timestamp <= 0 || !localtime_r(&value, &local_time) ||
        strftime(formatted, sizeof(formatted), "%I:%M:%S %p", &local_time) == 0) {

        return;

    }

    if (formatted[0] == '0') {

        memmove(formatted, formatted + 1, strlen(formatted));

    }
    snprintf(output, output_size, "%s", formatted);
}

static void auth_draw_lock_icon(SDL_Renderer *renderer, int x, int y, SDL_Color color) {
    /*
        Purpose: Draws the lock icon
        Returns: No value
    */

    SDL_Rect body = {x, y + 8, 14, 11};

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &body);
    SDL_RenderDrawLine(renderer, x + 3, y + 8, x + 3, y + 4);
    SDL_RenderDrawLine(renderer, x + 10, y + 8, x + 10, y + 4);
    SDL_RenderDrawLine(renderer, x + 3, y + 4, x + 5, y + 1);
    SDL_RenderDrawLine(renderer, x + 5, y + 1, x + 8, y + 1);
    SDL_RenderDrawLine(renderer, x + 8, y + 1, x + 10, y + 4);
    SDL_RenderDrawPoint(renderer, x + 7, y + 13);
    SDL_RenderDrawLine(renderer, x + 7, y + 14, x + 7, y + 16);
}

static void auth_draw_identity_status(SDL_Renderer *renderer, TTF_Font *font, int width) {
    /*
        Purpose: Draws the identity status
        Returns: No value
    */

    int64_t last_verified = SERVER_IDENTITY_last_verified_at();
    int conflict = SERVER_IDENTITY_has_conflict();
    char checked[64];
    char line[384];
    SDL_Color color = conflict ? AUTH_WARN : AUTH_MUTED;
    int text_width = 0;
    int text_height = 0;
    SDL_Rect rect = {20, 108, width - 40, 24};

    if (!conflict && last_verified > 0) {

        int64_t age = (int64_t)time(NULL) - last_verified;
        int recent = age >= 0 && age <= 10;
        auth_format_12_hour_time(last_verified, checked, sizeof(checked));
        snprintf(line, sizeof(line),
                 recent ? "ML-DSA-87 identity [last checked %s] verified, trusted public key loaded"
                        : "ML-DSA-87 identity [last checked %s] validation stale, trusted public key loaded",
                 checked);
        color = recent ? AUTH_TEXT : AUTH_WARN;

    }

    else {

        snprintf(line, sizeof(line), "%s", SERVER_IDENTITY_status());

    }

    auth_draw_centered_text(renderer, font, line, rect, color);

    if (!conflict && last_verified > 0 && ((int64_t)time(NULL) - last_verified) >= 0 &&
        ((int64_t)time(NULL) - last_verified) <= 10 && TTF_SizeUTF8(font, line, &text_width, &text_height) == 0) {

        int icon_x = rect.x + (rect.w - text_width) / 2 - 24;
        int icon_y = rect.y + (rect.h - 20) / 2;
        auth_draw_lock_icon(renderer, icon_x, icon_y, color);

    }
}

static int auth_base32_encode(const unsigned char *input, size_t input_size, char *output, size_t output_size) {
    /*
        Purpose: Encodes the Base32
        Returns: Success status
    */

    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    uint32_t buffer = 0;
    int bits_left = 0;
    size_t output_length = 0;

    if (!input || !output || output_size == 0) {

        return 0;

    }

    for (size_t i = 0; i < input_size; i++) {
        buffer = (buffer << 8) | input[i];
        bits_left += 8;

        while (bits_left >= 5) {

            if (output_length + 1 >= output_size) {

                return 0;

            }
            output[output_length++] = alphabet[(buffer >> (bits_left - 5)) & 31U];
            bits_left -= 5;
        }
    }

    if (bits_left > 0) {

        if (output_length + 1 >= output_size) {

            return 0;

        }
        output[output_length++] = alphabet[(buffer << (5 - bits_left)) & 31U];

    }

    output[output_length] = '\0';
    return 1;
}

static void auth_group_secret(const char *secret, char *grouped, size_t grouped_size) {
    /*
        Purpose: Groups a secret for readable display
        Returns: No value
    */

    size_t output_index = 0;

    if (!secret || !grouped || grouped_size == 0) {

        return;

    }

    for (size_t i = 0; secret[i] != '\0' && output_index + 1 < grouped_size; i++) {

        if (i > 0 && (i % 4) == 0 && output_index + 2 < grouped_size) {

            grouped[output_index++] = ' ';

        }
        grouped[output_index++] = secret[i];
    }
    grouped[output_index] = '\0';
}

static void auth_render(SDL_Window *window, SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                        const Type_Auth_State *state) {
    /*
        Purpose: Renders the requested operation
        Returns: No value
    */

    int width = 0;
    int height = 0;
    int mouse_x = 0;
    int mouse_y = 0;
    SDL_Rect panel;
    SDL_Rect top_left;
    SDL_Rect database_key_button;
    SDL_Rect top_right;
    SDL_Rect primary_button;
    const char *top_right_label;
    int show_change_server;

    SDL_GetWindowSize(window, &width, &height);
    SDL_GetMouseState(&mouse_x, &mouse_y);
    auth_make_panel(width, height, state->stage, &panel);
    top_left = auth_top_left_button_rect(width);
    database_key_button = auth_database_key_button_rect(width);
    top_right = auth_top_right_button_rect(width);
    primary_button = auth_primary_button_rect(panel);
    show_change_server = state->stage == AUTH_STAGE_LOGIN || state->stage == AUTH_STAGE_LOGIN_TWO_FACTOR;

    if (show_change_server) {

        top_right_label =
            (Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local())
                ? "ADMIN CONSOLE"
                : (!state->database_ready ? "DATABASE LOCKED"
                                          : (state->user_count == 0 ? "INITIALIZE ADMIN" : "ADMIN CONSOLE"));

    }

    else {

        top_right_label = "BACK TO LOGIN";

    }

    SDL_SetRenderDrawColor(renderer, AUTH_BG.r, AUTH_BG.g, AUTH_BG.b, AUTH_BG.a);
    SDL_RenderClear(renderer);
    auth_draw_grid(renderer, width, height, 0);
    auth_draw_trusted_server_header(renderer, font_small, width);

    if (show_change_server) {

        auth_draw_button(renderer, font_small, top_left, "CHANGE SERVER",
                         auth_point_in_rect(mouse_x, mouse_y, top_left));

        if (!Global_Auth_Client_Only_Mode) {

            auth_draw_button(renderer, font_small, database_key_button, "KEY FILE PATH",
                             auth_point_in_rect(mouse_x, mouse_y, database_key_button));

        }

    }
    auth_draw_button(renderer, font_small, top_right, top_right_label, auth_point_in_rect(mouse_x, mouse_y, top_right));
    auth_draw_identity_status(renderer, font_small, width);

    auth_fill_rect(renderer, panel, AUTH_PANEL);
    auth_outline_rect(renderer, panel, AUTH_BORDER_ACTIVE);
    auth_draw_centered_text(renderer, font_medium, "RETROSPECTRUM ACCESS CONTROL",
                            (SDL_Rect){panel.x, panel.y + 24, panel.w, 30}, AUTH_TEXT);

    if (state->stage == AUTH_STAGE_LOGIN || state->stage == AUTH_STAGE_AUTHORIZE_CREATE) {

        SDL_Rect username = auth_field_rect(panel, 0);
        SDL_Rect password = auth_field_rect(panel, 1);

        auth_draw_centered_text(
            renderer, font_small,
            state->stage == AUTH_STAGE_LOGIN
                ? ((Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) ? "REMOTE SERVER LOGIN"
                                                                                         : "LOCAL ACCOUNT LOGIN")
                : (Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local() ? "REMOTE CO-ADMINISTRATOR LOGIN"
                                                                                       : "ADMINISTRATOR LOGIN"),
            (SDL_Rect){panel.x, panel.y + 70, panel.w, 24}, AUTH_MUTED);
        auth_draw_field(renderer, font_small, username, "Username", state->username,
                        state->active_field == AUTH_FIELD_USERNAME, 0);
        auth_draw_field(renderer, font_small, password, "Password", state->password,
                        state->active_field == AUTH_FIELD_PASSWORD, 1);
        auth_draw_button(renderer, font_small, primary_button, state->stage == AUTH_STAGE_LOGIN ? "LOGIN" : "AUTHORIZE",
                         auth_point_in_rect(mouse_x, mouse_y, primary_button));

        if (!Global_Auth_Client_Only_Mode && state->stage == AUTH_STAGE_LOGIN && state->user_count == 0 &&
            state->status[0] == '\0') {

            auth_draw_centered_text(renderer, font_small, "No administrator exists. Use Initialize Admin.",
                                    (SDL_Rect){panel.x + 25, panel.y + panel.h - 120, panel.w - 50, 24}, AUTH_WARN);

        }

    }

    else if (state->stage == AUTH_STAGE_LOGIN_TWO_FACTOR || state->stage == AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR) {

        SDL_Rect code = auth_field_rect(panel, 1);
        char identity[128];

        if (!sec_sprintf(identity, sizeof(identity), "Authenticating as %s", state->username)) {

            identity[0] = '\0';

        }

        auth_draw_centered_text(renderer, font_small,
                                state->stage == AUTH_STAGE_LOGIN_TWO_FACTOR
                                    ? "TWO-FACTOR AUTHENTICATION"
                                    : (Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()
                                           ? "CO-ADMINISTRATOR TWO-FACTOR AUTHENTICATION"
                                           : "ADMINISTRATOR TWO-FACTOR AUTHENTICATION"),
                                (SDL_Rect){panel.x, panel.y + 70, panel.w, 24}, AUTH_MUTED);
        auth_draw_centered_text(renderer, font_small, identity,
                                (SDL_Rect){panel.x + 40, panel.y + 111, panel.w - 80, 24}, AUTH_TEXT);
        auth_draw_field(renderer, font_small, code, "Six-digit authenticator code", state->code,
                        state->active_field == AUTH_FIELD_CODE, 0);
        auth_draw_button(renderer, font_small, primary_button, "VERIFY",
                         auth_point_in_rect(mouse_x, mouse_y, primary_button));

    }

    else if (state->stage == AUTH_STAGE_CREATE_USER) {

        SDL_Rect username = auth_field_rect(panel, 0);
        SDL_Rect password = auth_field_rect(panel, 1);
        SDL_Rect confirm = auth_field_rect(panel, 2);
        SDL_Rect checkbox = auth_checkbox_rect(panel);

        auth_draw_centered_text(renderer, font_small, "CREATE A LOCAL USER",
                                (SDL_Rect){panel.x, panel.y + 70, panel.w, 24}, AUTH_MUTED);
        auth_draw_field(renderer, font_small, username, "Username", state->username,
                        state->active_field == AUTH_FIELD_USERNAME, 0);
        auth_draw_field(renderer, font_small, password, "Password", state->password,
                        state->active_field == AUTH_FIELD_PASSWORD, 1);
        auth_draw_field(renderer, font_small, confirm, "Confirm password", state->confirm_password,
                        state->active_field == AUTH_FIELD_CONFIRM_PASSWORD, 1);

        auth_fill_rect(renderer, checkbox, AUTH_FIELD_BG);
        auth_outline_rect(renderer, checkbox, state->enable_two_factor ? AUTH_BORDER_ACTIVE : AUTH_BORDER);

        if (state->enable_two_factor) {

            SDL_SetRenderDrawColor(renderer, AUTH_BORDER_ACTIVE.r, AUTH_BORDER_ACTIVE.g, AUTH_BORDER_ACTIVE.b,
                                   AUTH_BORDER_ACTIVE.a);
            SDL_RenderDrawLine(renderer, checkbox.x + 4, checkbox.y + 11, checkbox.x + 9, checkbox.y + 17);
            SDL_RenderDrawLine(renderer, checkbox.x + 9, checkbox.y + 17, checkbox.x + 19, checkbox.y + 4);

        }
        auth_draw_text(renderer, font_small, "Enable authenticator-app 2FA", checkbox.x + 34, checkbox.y + 2,
                       AUTH_TEXT);
        auth_draw_text(renderer, font_small,
                       state->user_count == 0
                           ? "Password: 10+ characters. The first account becomes the local administrator."
                           : "Passwords must contain at least 10 characters.",
                       checkbox.x, checkbox.y + 34, state->user_count == 0 ? AUTH_WARN : AUTH_MUTED);
        auth_draw_button(renderer, font_small, primary_button, state->enable_two_factor ? "CONTINUE" : "CREATE",
                         auth_point_in_rect(mouse_x, mouse_y, primary_button));

    }

    else if (state->stage == AUTH_STAGE_CREATE_TWO_FACTOR) {

        SDL_Rect code = auth_create_two_factor_code_rect(panel);
        SDL_Rect copy_button = auth_copy_secret_button_rect(panel);
        char secret[AUTH_TOTP_BASE32_MAX];
        char grouped_secret[AUTH_TOTP_BASE32_MAX + 16];

        secret[0] = '\0';
        grouped_secret[0] = '\0';

        if (state->active_totp_secret_valid &&
            auth_base32_encode(state->active_totp_secret, sizeof(state->active_totp_secret), secret, sizeof(secret))) {

            auth_group_secret(secret, grouped_secret, sizeof(grouped_secret));

        }

        auth_draw_centered_text(renderer, font_small, "SET UP TWO-FACTOR AUTHENTICATION",
                                (SDL_Rect){panel.x, panel.y + 70, panel.w, 24}, AUTH_MUTED);
        auth_draw_text(renderer, font_small, "1. Add a new time-based account in your authenticator app.", panel.x + 70,
                       panel.y + 118, AUTH_TEXT);
        auth_draw_text(renderer, font_small, "2. Enter this secret manually:", panel.x + 70, panel.y + 154, AUTH_TEXT);
        auth_fill_rect(renderer, (SDL_Rect){panel.x + 70, panel.y + 190, panel.w - 140, 48}, AUTH_FIELD_BG);
        auth_outline_rect(renderer, (SDL_Rect){panel.x + 70, panel.y + 190, panel.w - 140, 48}, AUTH_BORDER_ACTIVE);
        auth_draw_centered_text(renderer, font_small, grouped_secret,
                                (SDL_Rect){panel.x + 76, panel.y + 190, panel.w - 152, 48}, AUTH_TEXT);
        auth_draw_button(renderer, font_small, copy_button, "COPY SECRET",
                         auth_point_in_rect(mouse_x, mouse_y, copy_button));
        auth_draw_text(renderer, font_small,
                       "Issuer: RetroSpectrum   Algorithm: SHA-512   Digits: 6   Period: 30 seconds", panel.x + 70,
                       panel.y + 302, AUTH_MUTED);
        auth_draw_field(renderer, font_small, code, "3. Enter the current code to confirm setup", state->code,
                        state->active_field == AUTH_FIELD_CODE, 0);
        auth_draw_button(renderer, font_small, primary_button, "CREATE USER",
                         auth_point_in_rect(mouse_x, mouse_y, primary_button));

    }

    else if (state->stage == AUTH_STAGE_DATABASE_KEY_PATH) {

        SDL_Rect path_field = (SDL_Rect){panel.x + 50, panel.y + 170, panel.w - 100, 104};

        auth_draw_centered_text(renderer, font_small, "SELECT DATABASE MASTER KEY FILE",
                                (SDL_Rect){panel.x, panel.y + 72, panel.w, 24}, AUTH_MUTED);
        auth_draw_centered_text(renderer, font_small,
                                "Paste an absolute path, type it directly, or drag the 32-byte key file here.",
                                (SDL_Rect){panel.x + 25, panel.y + 112, panel.w - 50, 24}, AUTH_TEXT);
        auth_draw_import_path_field(renderer, font_small, path_field, state, "Database master-key file",
                                    "Paste or type the database key-file path here");
        auth_draw_button(renderer, font_small, primary_button, "USE KEY FILE",
                         auth_point_in_rect(mouse_x, mouse_y, primary_button));

    }

    else if (state->stage == AUTH_STAGE_CHANGE_SERVER_PATH) {

        SDL_Rect path_field = (SDL_Rect){panel.x + 50, panel.y + 170, panel.w - 100, 104};

        auth_draw_centered_text(renderer, font_small, "IMPORT A TRUSTED SERVER PUBLIC KEY",
                                (SDL_Rect){panel.x, panel.y + 72, panel.w, 24}, AUTH_MUTED);
        auth_draw_centered_text(renderer, font_small,
                                "Paste a .rspub path, type it directly, or drag the file into this window.",
                                (SDL_Rect){panel.x + 25, panel.y + 112, panel.w - 50, 24}, AUTH_TEXT);
        auth_draw_import_path_field(renderer, font_small, path_field, state, "Public-key file",
                                    "Paste or type the .rspub file path here");
        auth_draw_button(renderer, font_small, primary_button, "REVIEW KEY",
                         auth_point_in_rect(mouse_x, mouse_y, primary_button));

    }

    else if (state->stage == AUTH_STAGE_CHANGE_SERVER_CONFIRM) {

        char first_half[65] = "";
        char second_half[65] = "";

        if (state->pending_server_valid) {

            snprintf(first_half, sizeof(first_half), "%.64s", state->pending_server.fingerprint_sha512);
            snprintf(second_half, sizeof(second_half), "%.64s", state->pending_server.fingerprint_sha512 + 64);

        }

        auth_draw_centered_text(renderer, font_small, "CONFIRM TRUSTED SERVER CHANGE",
                                (SDL_Rect){panel.x, panel.y + 70, panel.w, 24}, AUTH_MUTED);
        auth_draw_centered_text(renderer, font_medium,
                                state->pending_server_valid ? state->pending_server.server_name : "INVALID SERVER",
                                (SDL_Rect){panel.x + 35, panel.y + 112, panel.w - 70, 32}, AUTH_TEXT);
        auth_draw_centered_text(renderer, font_small, "ML-DSA-87 PUBLIC KEY / SHA-512",
                                (SDL_Rect){panel.x + 35, panel.y + 160, panel.w - 70, 20}, AUTH_MUTED);
        auth_draw_centered_text(renderer, font_small, first_half,
                                (SDL_Rect){panel.x + 20, panel.y + 190, panel.w - 40, 20}, AUTH_TEXT);
        auth_draw_centered_text(renderer, font_small, second_half,
                                (SDL_Rect){panel.x + 20, panel.y + 214, panel.w - 40, 20}, AUTH_TEXT);
        auth_draw_centered_text(renderer, font_small,
                                "Only continue if this public key came from a trusted organizational source.",
                                (SDL_Rect){panel.x + 25, panel.y + 276, panel.w - 50, 24}, AUTH_ERROR);
        auth_draw_centered_text(renderer, font_small,
                                "Approving an untrusted key can redirect your login to an impersonating server.",
                                (SDL_Rect){panel.x + 25, panel.y + 306, panel.w - 50, 24}, AUTH_ERROR);
        auth_draw_danger_button(renderer, font_small, primary_button, "YES, TRUST KEY",
                                auth_point_in_rect(mouse_x, mouse_y, primary_button));

    }

    if (state->status[0] != '\0') {

        auth_draw_centered_text(renderer, font_small, state->status,
                                (SDL_Rect){panel.x + 25, panel.y + panel.h - 122, panel.w - 50, 32},
                                state->status_color);

    }

    SDL_RenderPresent(renderer);
}

static int auth_append_text(char *destination, size_t destination_size, const char *text, int digits_only) {
    /*
        Purpose: Appends the text
        Returns: Success status
    */

    size_t length;

    if (!destination || destination_size == 0 || !text) {

        return 0;

    }

    length = strlen(destination);
    for (size_t i = 0; text[i] != '\0'; i++) {
        unsigned char character = (unsigned char)text[i];

        if (digits_only && !isdigit(character)) {

            continue;

        }

        if (length + 1 >= destination_size) {

            break;

        }
        destination[length++] = (char)character;
    }
    destination[length] = '\0';
    return 1;
}

static void auth_backspace(char *text) {
    /*
        Purpose: Removes the previous character from the requested data
        Returns: No value
    */

    size_t length;

    if (!text) {

        return;

    }
    length = strlen(text);

    if (length > 0) {

        text[length - 1] = '\0';

    }
}

static int auth_ensure_directory(const char *path) {
    /*
        Purpose: Ensures the directory
        Returns: Success status
    */

    struct stat st;

    if (!path || path[0] == '\0') {

        return 0;

    }

    if (stat(path, &st) == 0) {

        return S_ISDIR(st.st_mode);

    }

    if (mkdir(path, 0700) == 0) {

        return 1;

    }
    return errno == EEXIST;
}

static int auth_database_path(char *path, size_t path_size) {
    /*
        Purpose: Builds the database path
        Returns: Success status
    */

    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char config_root[PATH_MAX];
    char app_directory[PATH_MAX];

    if (!path || path_size == 0) {

        return 0;

    }

    if (xdg_config && xdg_config[0] != '\0') {

        if (!sec_strcpy(config_root, sizeof(config_root), xdg_config)) {

            return 0;

        }

    }

    else if (home && home[0] != '\0') {

        if (!sec_sprintf(config_root, sizeof(config_root), "%s/.config", home)) {

            return 0;

        }

    }

    else {

        if (!sec_strcpy(config_root, sizeof(config_root), ".")) {

            return 0;

        }

    }

    if (!auth_ensure_directory(config_root)) {

        return 0;

    }

    if (!sec_sprintf(app_directory, sizeof(app_directory), "%s/retrospectrum", config_root)) {

        return 0;

    }

    if (!sec_ensure_private_directory(app_directory, 0700)) {

        return 0;

    }

    if (!sec_sprintf(path, path_size, "%s/auth.db", app_directory)) {

        return 0;

    }
    return 1;
}

static int auth_totp_master_key_path(char *path, size_t path_size) {
    /*
        Purpose: Builds the master key path
        Returns: Success status
    */

    char database_path[PATH_MAX];
    char *slash;

    if (!path || path_size == 0 || !auth_database_path(database_path, sizeof(database_path))) {

        return 0;

    }

    slash = strrchr(database_path, '/');

    if (!slash) {

        return 0;

    }
    *slash = '\0';

    return sec_sprintf(path, path_size, "%s/%s", database_path, AUTH_TOTP_MASTER_KEY_FILENAME);
}

static int auth_read_exact_fd(int fd, unsigned char *buffer, size_t size) {
    /*
        Purpose: Reads the exact file descriptor
        Returns: Success status
    */

    size_t offset = 0;

    while (offset < size) {
        ssize_t amount = read(fd, buffer + offset, size - offset);

        if (amount < 0) {

            if (errno == EINTR) {

                continue;

            }
            return 0;

        }

        if (amount == 0) {

            return 0;

        }
        offset += (size_t)amount;
    }
    return 1;
}

static int auth_write_exact_fd(int fd, const unsigned char *buffer, size_t size) {
    /*
        Purpose: Writes an exact number of bytes to a file descriptor
        Returns: Success status
    */

    size_t offset = 0;

    while (offset < size) {
        ssize_t amount = write(fd, buffer + offset, size - offset);

        if (amount < 0) {

            if (errno == EINTR) {

                continue;

            }
            return 0;

        }
        offset += (size_t)amount;
    }
    return 1;
}

static int auth_load_existing_totp_master_key(const char *path, unsigned char key[AUTH_TOTP_MASTER_KEY_BYTES]) {
    /*
        Purpose: Loads the existing TOTP master key
        Returns: Success status
    */

    struct stat st;
    int fd;
    int success = 0;

    if (!path || !key) {

        return 0;

    }

    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if (fd < 0) {

        return 0;

    }

    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
        st.st_size == AUTH_TOTP_MASTER_KEY_BYTES && auth_read_exact_fd(fd, key, AUTH_TOTP_MASTER_KEY_BYTES)) {

        success = 1;

    }

    close(fd);

    if (success) {

        (void)chmod(path, 0600);

    }

    else {

        auth_secure_zero(key, AUTH_TOTP_MASTER_KEY_BYTES);

    }
    return success;
}

static int auth_load_or_create_totp_master_key(unsigned char key[AUTH_TOTP_MASTER_KEY_BYTES]) {
    /*
        Purpose: Loads or creates the TOTP master key
        Returns: Success status
    */

    char path[PATH_MAX];
    mode_t previous_mask;
    int fd;

    if (!key || !auth_totp_master_key_path(path, sizeof(path))) {

        return 0;

    }

    if (auth_load_existing_totp_master_key(path, key)) {

        return 1;

    }

    if (access(path, F_OK) == 0) {

        return 0;

    }

    if (RAND_bytes(key, AUTH_TOTP_MASTER_KEY_BYTES) != 1) {

        auth_secure_zero(key, AUTH_TOTP_MASTER_KEY_BYTES);
        return 0;

    }

    previous_mask = umask(0077);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    umask(previous_mask);

    if (fd < 0) {

        if (errno == EEXIST) {

            auth_secure_zero(key, AUTH_TOTP_MASTER_KEY_BYTES);
            return auth_load_existing_totp_master_key(path, key);

        }
        auth_secure_zero(key, AUTH_TOTP_MASTER_KEY_BYTES);
        return 0;

    }

    if (!auth_write_exact_fd(fd, key, AUTH_TOTP_MASTER_KEY_BYTES) || fsync(fd) != 0) {

        close(fd);
        unlink(path);
        auth_secure_zero(key, AUTH_TOTP_MASTER_KEY_BYTES);
        return 0;

    }

    close(fd);
    (void)chmod(path, 0600);
    return 1;
}

static int auth_derive_server_totp_key(const char *username, const unsigned char salt[AUTH_TOTP_SALT_BYTES],
                                       unsigned char key[32]) {
    /*
        Purpose: Derives the server TOTP key
        Returns: Success status
    */

    static const char domain[] = "RetroSpectrum TOTP wrapping key v1|";
    unsigned char master_key[AUTH_TOTP_MASTER_KEY_BYTES];
    unsigned char pseudorandom_key[EVP_MAX_MD_SIZE];
    unsigned char expanded_key[EVP_MAX_MD_SIZE];
    unsigned char info[sizeof(domain) + AUTH_USERNAME_MAX + 2];
    unsigned int pseudorandom_key_length = 0;
    unsigned int output_length = 0;
    size_t username_length;
    size_t info_length;
    int success = 0;

    memset(master_key, 0, sizeof(master_key));
    memset(pseudorandom_key, 0, sizeof(pseudorandom_key));
    memset(expanded_key, 0, sizeof(expanded_key));
    memset(info, 0, sizeof(info));

    if (!username || !salt || !key || !auth_load_or_create_totp_master_key(master_key)) {

        goto cleanup;

    }

    username_length = strlen(username);

    if (username_length == 0 || username_length > AUTH_USERNAME_MAX) {

        goto cleanup;

    }

    if (!HMAC(EVP_sha512(), salt, AUTH_TOTP_SALT_BYTES, master_key, sizeof(master_key), pseudorandom_key,
              &pseudorandom_key_length) ||
        pseudorandom_key_length != 64U) {

        goto cleanup;

    }

    memcpy(info, domain, sizeof(domain) - 1);
    memcpy(info + sizeof(domain) - 1, username, username_length);
    info_length = sizeof(domain) - 1 + username_length;
    info[info_length++] = 0x01;

    if (!HMAC(EVP_sha512(), pseudorandom_key, (int)pseudorandom_key_length, info, info_length, expanded_key,
              &output_length) ||
        output_length < 32U) {

        goto cleanup;

    }

    memcpy(key, expanded_key, 32);
    success = 1;

cleanup:
    auth_secure_zero(master_key, sizeof(master_key));
    auth_secure_zero(pseudorandom_key, sizeof(pseudorandom_key));
    auth_secure_zero(expanded_key, sizeof(expanded_key));
    auth_secure_zero(info, sizeof(info));

    if (!success) {

        auth_secure_zero(key, 32);

    }
    return success;
}

static int auth_execute_sql(sqlite3 *database, const char *sql) {
    /*
        Purpose: Executes the SQL
        Returns: Success status
    */

    char *error_message = NULL;
    int result;

    if (!database || !sql) {

        return 0;

    }

    result = sqlite3_exec(database, sql, NULL, NULL, &error_message);

    if (result != SQLITE_OK) {

        fprintf(stderr, "Authentication database error: %s\n", error_message ? error_message : "unknown error");
        sqlite3_free(error_message);
        return 0;

    }
    return 1;
}

static void auth_rate_limit_scope(char *output, size_t output_size, const char *category, const char *username) {
    /*
        Purpose: Builds a rate-limit scope
        Returns: No value
    */

    if (!output || output_size == 0) {

        return;

    }

    if (!category) {

        category = "LOGIN";

    }

    if (!username) {

        username = "";

    }

    if (!sec_sprintf(output, output_size, "%s:%s", category, username)) {

        output[0] = '\0';

    }
}

static int auth_rate_limit_remaining(sqlite3 *database, const char *scope) {
    /*
        Purpose: Gets the remaining rate-limit delay
        Returns: Success status
    */

    sqlite3_stmt *statement = NULL;
    sqlite3_int64 locked_until = 0;
    sqlite3_int64 now = (sqlite3_int64)time(NULL);
    int remaining = 0;

    if (!database || !scope ||
        sqlite3_prepare_v2(database, "SELECT locked_until FROM auth_rate_limits WHERE scope = ?1;", -1, &statement,
                           NULL) != SQLITE_OK) {

        return 0;

    }

    sqlite3_bind_text(statement, 1, scope, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) == SQLITE_ROW) {

        locked_until = sqlite3_column_int64(statement, 0);

    }
    sqlite3_finalize(statement);

    if (locked_until > now) {

        sqlite3_int64 difference = locked_until - now;
        remaining = difference > INT_MAX ? INT_MAX : (int)difference;

    }

    return remaining;
}

static int auth_rate_limit_failure(sqlite3 *database, const char *scope) {
    /*
        Purpose: Records a rate-limit failure
        Returns: Success status
    */

    static const char select_sql[] = "SELECT failure_count, window_started_at FROM auth_rate_limits WHERE scope = ?1;";
    static const char upsert_sql[] =
        "INSERT INTO auth_rate_limits "
        "(scope, failure_count, window_started_at, locked_until, last_failure_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(scope) DO UPDATE SET failure_count = excluded.failure_count, "
        "window_started_at = excluded.window_started_at, locked_until = excluded.locked_until, "
        "last_failure_at = excluded.last_failure_at;";
    sqlite3_stmt *statement = NULL;
    sqlite3_int64 now = (sqlite3_int64)time(NULL);
    sqlite3_int64 window_started_at = now;
    sqlite3_int64 locked_until = 0;
    int failure_count = 0;
    int lock_seconds = 0;
    int success = 0;

    if (!database || !scope) {

        return 0;

    }

    if (sqlite3_prepare_v2(database, select_sql, -1, &statement, NULL) == SQLITE_OK) {

        sqlite3_bind_text(statement, 1, scope, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(statement) == SQLITE_ROW) {

            failure_count = sqlite3_column_int(statement, 0);
            window_started_at = sqlite3_column_int64(statement, 1);

        }

    }
    sqlite3_finalize(statement);
    statement = NULL;

    if (now - window_started_at >= AUTH_RATE_LIMIT_WINDOW_SECONDS || now < window_started_at) {

        failure_count = 0;
        window_started_at = now;

    }

    failure_count++;

    if (failure_count >= AUTH_RATE_LIMIT_THRESHOLD) {

        int exponent = failure_count - AUTH_RATE_LIMIT_THRESHOLD;

        if (exponent > 5) {

            exponent = 5;

        }
        lock_seconds = AUTH_RATE_LIMIT_BASE_LOCK_SECONDS << exponent;

        if (lock_seconds > AUTH_RATE_LIMIT_MAX_LOCK_SECONDS) {

            lock_seconds = AUTH_RATE_LIMIT_MAX_LOCK_SECONDS;

        }
        locked_until = now + lock_seconds;

    }

    if (sqlite3_prepare_v2(database, upsert_sql, -1, &statement, NULL) != SQLITE_OK) {

        return 0;

    }

    sqlite3_bind_text(statement, 1, scope, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, failure_count);
    sqlite3_bind_int64(statement, 3, window_started_at);
    sqlite3_bind_int64(statement, 4, locked_until);
    sqlite3_bind_int64(statement, 5, now);
    success = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);

    return success ? lock_seconds : 0;
}

static void auth_rate_limit_success(sqlite3 *database, const char *scope) {
    /*
        Purpose: Clears rate-limit failures
        Returns: No value
    */

    sqlite3_stmt *statement = NULL;

    if (!database || !scope ||
        sqlite3_prepare_v2(database, "DELETE FROM auth_rate_limits WHERE scope = ?1;", -1, &statement, NULL) !=
            SQLITE_OK) {

        return;

    }

    sqlite3_bind_text(statement, 1, scope, -1, SQLITE_TRANSIENT);
    sqlite3_step(statement);
    sqlite3_finalize(statement);
}

static int auth_rate_limit_guard(sqlite3 *database, Type_Auth_State *state, const char *category) {
    /*
        Purpose: Checks the authentication rate limit
        Returns: Success status
    */

    char scope[AUTH_USERNAME_MAX + 32];
    char message[AUTH_STATUS_MAX];
    int remaining;

    if (!database || !state) {

        return 0;

    }

    auth_rate_limit_scope(scope, sizeof(scope), category, state->username);
    remaining = auth_rate_limit_remaining(database, scope);

    if (remaining <= 0) {

        return 1;

    }

    snprintf(message, sizeof(message), "Too many failed attempts. Try again in %d second%s.", remaining,
             remaining == 1 ? "" : "s");
    auth_set_status(state, AUTH_ERROR, message);
    return 0;
}

static void auth_rate_limit_note_failure(sqlite3 *database, Type_Auth_State *state, const char *category,
                                         const char *default_message) {
    /*
        Purpose: Records a rate-limit failure
        Returns: No value
    */

    char scope[AUTH_USERNAME_MAX + 32];
    char message[AUTH_STATUS_MAX];
    int lock_seconds;

    if (!database || !state) {

        return;

    }

    auth_rate_limit_scope(scope, sizeof(scope), category, state->username);
    lock_seconds = auth_rate_limit_failure(database, scope);

    if (lock_seconds > 0) {

        snprintf(message, sizeof(message), "Too many failed attempts. Locked for %d second%s.", lock_seconds,
                 lock_seconds == 1 ? "" : "s");
        auth_set_status(state, AUTH_ERROR, message);

    }

    else {

        auth_set_status(state, AUTH_ERROR, default_message);

    }
}

static int auth_open_database(sqlite3 **database, char *path, size_t path_size) {
    /*
        Purpose: Opens the database
        Returns: Success status
    */

    mode_t previous_mask;
    int result;

    if (!database || !path || !auth_database_path(path, path_size)) {

        return 0;

    }

    (void)previous_mask;
    (void)result;
    {
        char database_error[256] = "";

        if (!DATABASE_CRYPTO_open_auth(database, path, path_size, database_error, sizeof(database_error))) {

            fprintf(stderr, "Unable to open encrypted authentication database: %s\n", database_error);
            return 0;

        }
    }

    if (!auth_execute_sql(*database, "PRAGMA foreign_keys = ON;") ||
        !auth_execute_sql(*database, "PRAGMA secure_delete = ON;") ||
        !auth_execute_sql(*database,
                          "CREATE TABLE IF NOT EXISTS users ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                          "username TEXT NOT NULL UNIQUE COLLATE BINARY,"
                          "password_encoded TEXT NOT NULL,"
                          "password_algorithm TEXT NOT NULL DEFAULT 'argon2id' CHECK(password_algorithm = 'argon2id'),"
                          "totp_enabled INTEGER NOT NULL DEFAULT 0,"
                          "totp_algorithm TEXT NOT NULL DEFAULT 'sha512' CHECK(totp_algorithm = 'sha512'),"
                          "totp_secret_bytes INTEGER NOT NULL DEFAULT 32 CHECK(totp_secret_bytes = 32),"
                          "totp_kdf_algorithm TEXT NOT NULL DEFAULT 'server-sha512' "
                          "CHECK(totp_kdf_algorithm = 'server-sha512'),"
                          "role INTEGER NOT NULL DEFAULT 0 CHECK(role IN (0, 1, 2)),"
                          "totp_salt BLOB,"
                          "totp_nonce BLOB,"
                          "totp_tag BLOB,"
                          "totp_ciphertext BLOB,"
                          "last_totp_counter INTEGER NOT NULL DEFAULT -1,"
                          "created_at INTEGER NOT NULL"
                          ");") ||
        !auth_execute_sql(*database, "CREATE TABLE IF NOT EXISTS server_config ("
                                     "singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
                                     "server_id TEXT NOT NULL UNIQUE CHECK(length(server_id) = 12),"
                                     "updated_at INTEGER NOT NULL"
                                     ");") ||
        !auth_execute_sql(*database, "CREATE TABLE IF NOT EXISTS auth_rate_limits ("
                                     "scope TEXT PRIMARY KEY,"
                                     "failure_count INTEGER NOT NULL,"
                                     "window_started_at INTEGER NOT NULL,"
                                     "locked_until INTEGER NOT NULL,"
                                     "last_failure_at INTEGER NOT NULL"
                                     ");") ||
        !auth_execute_sql(*database, "CREATE UNIQUE INDEX IF NOT EXISTS users_single_primary_admin "
                                     "ON users(role) WHERE role = 2;") ||
        !auth_execute_sql(*database, "CREATE TRIGGER IF NOT EXISTS users_protect_primary_delete "
                                     "BEFORE DELETE ON users WHEN OLD.role = 2 BEGIN "
                                     "SELECT RAISE(ABORT, 'primary administrator cannot be deleted'); END;") ||
        !auth_execute_sql(*database, "CREATE TRIGGER IF NOT EXISTS users_protect_primary_role "
                                     "BEFORE UPDATE OF role ON users "
                                     "WHEN OLD.role = 2 AND NEW.role <> 2 BEGIN "
                                     "SELECT RAISE(ABORT, 'primary administrator role cannot be changed'); END;")) {

        sqlite3_close(*database);
        *database = NULL;
        return 0;

    }

    return 1;
}

static int auth_count_users(sqlite3 *database) {
    /*
        Purpose: Counts the users
        Returns: Item count
    */

    sqlite3_stmt *statement = NULL;
    int count = 0;

    if (!database || sqlite3_prepare_v2(database, "SELECT COUNT(*) FROM users;", -1, &statement, NULL) != SQLITE_OK) {

        return 0;

    }

    if (sqlite3_step(statement) == SQLITE_ROW) {

        count = sqlite3_column_int(statement, 0);

    }
    sqlite3_finalize(statement);
    return count;
}

static int auth_username_valid(const char *username) {
    /*
        Purpose: Checks whether the username is valid
        Returns: Boolean status
    */

    size_t length;

    if (!username) {

        return 0;

    }
    length = strlen(username);

    if (length < 3 || length > AUTH_USERNAME_MAX) {

        return 0;

    }
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)username[i];

        if (!isalnum(character) && character != '_' && character != '-' && character != '.') {

            return 0;

        }
    }
    return 1;
}

static int auth_user_exists(sqlite3 *database, const char *username) {
    /*
        Purpose: Checks whether the user exists
        Returns: Boolean status
    */

    sqlite3_stmt *statement = NULL;
    int exists = 0;

    if (!database || !username ||
        sqlite3_prepare_v2(database, "SELECT 1 FROM users WHERE username = ?1 LIMIT 1;", -1, &statement, NULL) !=
            SQLITE_OK) {

        return 0;

    }

    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    exists = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return exists;
}

static const EVP_MD *auth_digest_from_name(const char *name) {
    /*
        Purpose: Gets the digest from the name
        Returns: Result pointer
    */

    if (name && strcmp(name, AUTH_TOTP_ALGORITHM_DEFAULT) == 0) {

        return EVP_sha512();

    }

    return NULL;
}

static int auth_hash_password_argon2id(const char *password, char encoded[AUTH_ARGON2_ENCODED_MAX]) {
    /*
        Purpose: Hashes the password Argon2id
        Returns: Success status
    */

    unsigned char salt[AUTH_ARGON2_SALT_BYTES];
    size_t required_length;
    int result;

    if (!password || !encoded || RAND_bytes(salt, sizeof(salt)) != 1) {

        return 0;

    }

    required_length = argon2_encodedlen(AUTH_ARGON2_TIME_COST, AUTH_ARGON2_MEMORY_KIB, AUTH_ARGON2_PARALLELISM,
                                        AUTH_ARGON2_SALT_BYTES, AUTH_ARGON2_HASH_BYTES, Argon2_id);

    if (required_length == 0 || required_length > AUTH_ARGON2_ENCODED_MAX) {

        auth_secure_zero(salt, sizeof(salt));
        return 0;

    }

    result = argon2id_hash_encoded(AUTH_ARGON2_TIME_COST, AUTH_ARGON2_MEMORY_KIB, AUTH_ARGON2_PARALLELISM, password,
                                   strlen(password), salt, sizeof(salt), AUTH_ARGON2_HASH_BYTES, encoded,
                                   AUTH_ARGON2_ENCODED_MAX);
    auth_secure_zero(salt, sizeof(salt));
    return result == ARGON2_OK;
}

static int auth_verify_argon2id(const char *password, const char *encoded) {
    /*
        Purpose: Verifies the Argon2id
        Returns: Success status
    */

    if (!password || !encoded || strncmp(encoded, "$argon2id$", 10) != 0) {

        return 0;

    }

    return argon2id_verify(encoded, password, strlen(password)) == ARGON2_OK;
}

static void auth_dummy_password_work(const char *password) {
    /*
        Purpose: Performs dummy password hashing work
        Returns: No value
    */

    static const unsigned char salt[AUTH_ARGON2_SALT_BYTES] = {0x52, 0x65, 0x74, 0x72, 0x6f, 0x53, 0x70, 0x65,
                                                               0x63, 0x74, 0x72, 0x75, 0x6d, 0x41, 0x75, 0x74};
    unsigned char output[AUTH_ARGON2_HASH_BYTES];

    if (!password) {

        password = "";

    }

    (void)argon2id_hash_raw(AUTH_ARGON2_TIME_COST, AUTH_ARGON2_MEMORY_KIB, AUTH_ARGON2_PARALLELISM, password,
                            strlen(password), salt, sizeof(salt), output, sizeof(output));
    auth_secure_zero(output, sizeof(output));
}

static int auth_derive_totp_encryption_key(const char *username, const unsigned char salt[AUTH_TOTP_SALT_BYTES],
                                           unsigned char key[32]) {
    /*
        Purpose: Derives the TOTP encryption key
        Returns: Success status
    */

    if (!username || !salt || !key) {

        return 0;

    }

    return auth_derive_server_totp_key(username, salt, key);
}

static int auth_encrypt_totp_secret(const char *username, const unsigned char *secret, int secret_size,
                                    unsigned char *salt, unsigned char *nonce, unsigned char *tag,
                                    unsigned char *ciphertext) {
    /*
        Purpose: Encrypts the TOTP secret
        Returns: Success status
    */

    EVP_CIPHER_CTX *context = NULL;
    unsigned char key[32];
    int output_length = 0;
    int final_length = 0;
    int success = 0;

    memset(key, 0, sizeof(key));

    if (!username || !secret || secret_size != AUTH_TOTP_SECRET_BYTES || !salt || !nonce || !tag || !ciphertext ||
        RAND_bytes(salt, AUTH_TOTP_SALT_BYTES) != 1 || RAND_bytes(nonce, AUTH_TOTP_NONCE_BYTES) != 1 ||
        !auth_derive_totp_encryption_key(username, salt, key)) {

        goto cleanup;

    }

    context = EVP_CIPHER_CTX_new();

    if (!context || EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, AUTH_TOTP_NONCE_BYTES, NULL) != 1 ||
        EVP_EncryptInit_ex(context, NULL, NULL, key, nonce) != 1 ||
        EVP_EncryptUpdate(context, ciphertext, &output_length, secret, secret_size) != 1 ||
        EVP_EncryptFinal_ex(context, ciphertext + output_length, &final_length) != 1 ||
        output_length + final_length != secret_size ||
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, AUTH_TOTP_TAG_BYTES, tag) != 1) {

        goto cleanup;

    }

    success = 1;

cleanup:
    EVP_CIPHER_CTX_free(context);
    auth_secure_zero(key, sizeof(key));
    return success;
}

static int auth_decrypt_totp_secret(const char *username, const Type_Auth_User_Record *record, unsigned char *secret) {
    /*
        Purpose: Decrypts the TOTP secret
        Returns: Success status
    */

    EVP_CIPHER_CTX *context = NULL;
    unsigned char key[32];
    int output_length = 0;
    int final_length = 0;
    int success = 0;

    memset(key, 0, sizeof(key));

    if (!username || !record || !secret || record->totp_secret_bytes != AUTH_TOTP_SECRET_BYTES ||
        strcmp(record->totp_algorithm, AUTH_TOTP_ALGORITHM_DEFAULT) != 0 ||
        strcmp(record->totp_kdf_algorithm, AUTH_TOTP_KDF_DEFAULT) != 0 ||
        !auth_derive_totp_encryption_key(username, record->totp_salt, key)) {

        goto cleanup;

    }

    context = EVP_CIPHER_CTX_new();

    if (!context || EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, AUTH_TOTP_NONCE_BYTES, NULL) != 1 ||
        EVP_DecryptInit_ex(context, NULL, NULL, key, record->totp_nonce) != 1 ||
        EVP_DecryptUpdate(context, secret, &output_length, record->totp_ciphertext, record->totp_secret_bytes) != 1 ||
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, AUTH_TOTP_TAG_BYTES, (void *)record->totp_tag) != 1 ||
        EVP_DecryptFinal_ex(context, secret + output_length, &final_length) != 1 ||
        output_length + final_length != record->totp_secret_bytes) {

        goto cleanup;

    }

    success = 1;

cleanup:
    EVP_CIPHER_CTX_free(context);
    auth_secure_zero(key, sizeof(key));

    if (!success) {

        auth_secure_zero(secret, AUTH_TOTP_SECRET_BYTES);

    }
    return success;
}

static int auth_store_server_wrapped_totp(sqlite3 *database, const char *username, const unsigned char *secret,
                                          int secret_size, const char *totp_algorithm) {
    /*
        Purpose: Stores the server wrapped TOTP
        Returns: Success status
    */

    static const char sql[] = "UPDATE users SET totp_enabled = 1, totp_algorithm = ?1, "
                              "totp_secret_bytes = ?2, totp_kdf_algorithm = ?3, totp_salt = ?4, "
                              "totp_nonce = ?5, totp_tag = ?6, totp_ciphertext = ?7 WHERE username = ?8;";
    sqlite3_stmt *statement = NULL;
    unsigned char salt[AUTH_TOTP_SALT_BYTES];
    unsigned char nonce[AUTH_TOTP_NONCE_BYTES];
    unsigned char tag[AUTH_TOTP_TAG_BYTES];
    unsigned char ciphertext[AUTH_TOTP_CIPHER_BYTES];
    int success = 0;

    memset(salt, 0, sizeof(salt));
    memset(nonce, 0, sizeof(nonce));
    memset(tag, 0, sizeof(tag));
    memset(ciphertext, 0, sizeof(ciphertext));

    if (!database || !username || !secret || secret_size != AUTH_TOTP_SECRET_BYTES || !totp_algorithm ||
        strcmp(totp_algorithm, AUTH_TOTP_ALGORITHM_DEFAULT) != 0 ||
        !auth_encrypt_totp_secret(username, secret, secret_size, salt, nonce, tag, ciphertext) ||
        sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) {

        goto cleanup;

    }

    sqlite3_bind_text(statement, 1, totp_algorithm, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, secret_size);
    sqlite3_bind_text(statement, 3, AUTH_TOTP_KDF_DEFAULT, -1, SQLITE_STATIC);
    sqlite3_bind_blob(statement, 4, salt, sizeof(salt), SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 5, nonce, sizeof(nonce), SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 6, tag, sizeof(tag), SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 7, ciphertext, secret_size, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 8, username, -1, SQLITE_TRANSIENT);
    success = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(database) == 1;

cleanup:
    sqlite3_finalize(statement);
    auth_secure_zero(salt, sizeof(salt));
    auth_secure_zero(nonce, sizeof(nonce));
    auth_secure_zero(tag, sizeof(tag));
    auth_secure_zero(ciphertext, sizeof(ciphertext));
    return success;
}

static int auth_load_user(sqlite3 *database, const char *username, Type_Auth_User_Record *record) {
    /*
        Purpose: Loads the user
        Returns: Success status
    */

    static const char sql[] =
        "SELECT password_encoded, password_algorithm, totp_enabled, totp_algorithm, totp_secret_bytes, "
        "totp_kdf_algorithm, role, totp_salt, totp_nonce, totp_tag, totp_ciphertext, last_totp_counter "
        "FROM users WHERE username = ?1 LIMIT 1;";
    sqlite3_stmt *statement = NULL;
    int result = 0;

    if (!database || !username || !record || sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) {

        return -1;

    }

    memset(record, 0, sizeof(*record));
    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) == SQLITE_ROW) {

        const unsigned char *encoded = sqlite3_column_text(statement, 0);
        const unsigned char *algorithm = sqlite3_column_text(statement, 1);
        const unsigned char *totp_algorithm = sqlite3_column_text(statement, 3);
        const unsigned char *totp_kdf_algorithm = sqlite3_column_text(statement, 5);

        if (!encoded || !algorithm || strcmp((const char *)algorithm, "argon2id") != 0 ||
            strncmp((const char *)encoded, "$argon2id$", 10) != 0 || !totp_algorithm ||
            strcmp((const char *)totp_algorithm, AUTH_TOTP_ALGORITHM_DEFAULT) != 0 ||
            sqlite3_column_int(statement, 4) != AUTH_TOTP_SECRET_BYTES || !totp_kdf_algorithm ||
            strcmp((const char *)totp_kdf_algorithm, AUTH_TOTP_KDF_DEFAULT) != 0) {

            result = -1;
            goto cleanup;

        }

        auth_copy_text(record->password_encoded, sizeof(record->password_encoded), (const char *)encoded);
        record->totp_enabled = sqlite3_column_int(statement, 2) != 0;
        auth_copy_text(record->totp_algorithm, sizeof(record->totp_algorithm), (const char *)totp_algorithm);
        record->totp_secret_bytes = sqlite3_column_int(statement, 4);
        auth_copy_text(record->totp_kdf_algorithm, sizeof(record->totp_kdf_algorithm),
                       (const char *)totp_kdf_algorithm);
        record->role = sqlite3_column_int(statement, 6);

        if (record->role < AUTH_ROLE_USER || record->role > AUTH_ROLE_ADMIN) {

            result = -1;
            goto cleanup;

        }
        record->last_totp_counter = sqlite3_column_int64(statement, 11);

        if (record->totp_enabled) {

            const void *totp_salt = sqlite3_column_blob(statement, 7);
            const void *totp_nonce = sqlite3_column_blob(statement, 8);
            const void *totp_tag = sqlite3_column_blob(statement, 9);
            const void *totp_ciphertext = sqlite3_column_blob(statement, 10);

            if (!totp_salt || !totp_nonce || !totp_tag || !totp_ciphertext ||
                sqlite3_column_bytes(statement, 7) != AUTH_TOTP_SALT_BYTES ||
                sqlite3_column_bytes(statement, 8) != AUTH_TOTP_NONCE_BYTES ||
                sqlite3_column_bytes(statement, 9) != AUTH_TOTP_TAG_BYTES ||
                sqlite3_column_bytes(statement, 10) != AUTH_TOTP_SECRET_BYTES) {

                result = -1;
                goto cleanup;

            }

            memcpy(record->totp_salt, totp_salt, AUTH_TOTP_SALT_BYTES);
            memcpy(record->totp_nonce, totp_nonce, AUTH_TOTP_NONCE_BYTES);
            memcpy(record->totp_tag, totp_tag, AUTH_TOTP_TAG_BYTES);
            memcpy(record->totp_ciphertext, totp_ciphertext, AUTH_TOTP_SECRET_BYTES);

        }

        result = 1;

    }

cleanup:
    sqlite3_finalize(statement);
    return result;
}

static int auth_verify_password(const char *password, const Type_Auth_User_Record *record) {
    /*
        Purpose: Verifies the password
        Returns: Success status
    */

    if (!password || !record) {

        return 0;

    }

    return auth_verify_argon2id(password, record->password_encoded);
}

static int auth_insert_user(sqlite3 *database, const char *username, const char *password, int enable_totp,
                            int is_admin, const unsigned char *totp_secret) {
    /*
        Purpose: Inserts the user
        Returns: Success status
    */

    static const char sql[] =
        "INSERT INTO users "
        "(username, password_encoded, password_algorithm, totp_enabled, totp_algorithm, totp_secret_bytes, "
        "totp_kdf_algorithm, role, totp_salt, totp_nonce, totp_tag, totp_ciphertext, created_at) "
        "VALUES (?1, ?2, 'argon2id', ?3, 'sha512', 32, 'server-sha512', ?4, ?5, ?6, ?7, ?8, ?9);";
    sqlite3_stmt *statement = NULL;
    char password_encoded[AUTH_ARGON2_ENCODED_MAX];
    unsigned char totp_salt[AUTH_TOTP_SALT_BYTES];
    unsigned char totp_nonce[AUTH_TOTP_NONCE_BYTES];
    unsigned char totp_tag[AUTH_TOTP_TAG_BYTES];
    unsigned char totp_ciphertext[AUTH_TOTP_CIPHER_BYTES];
    int success = 0;

    memset(password_encoded, 0, sizeof(password_encoded));
    memset(totp_salt, 0, sizeof(totp_salt));
    memset(totp_nonce, 0, sizeof(totp_nonce));
    memset(totp_tag, 0, sizeof(totp_tag));
    memset(totp_ciphertext, 0, sizeof(totp_ciphertext));

    if (!database || !username || !password || !auth_hash_password_argon2id(password, password_encoded)) {

        goto cleanup;

    }

    if (enable_totp && (!totp_secret || !auth_encrypt_totp_secret(username, totp_secret, AUTH_TOTP_SECRET_BYTES,
                                                                  totp_salt, totp_nonce, totp_tag, totp_ciphertext))) {

        goto cleanup;

    }

    if (sqlite3_prepare_v2(database, sql, -1, &statement, NULL) != SQLITE_OK) {

        goto cleanup;

    }

    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, password_encoded, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 3, enable_totp ? 1 : 0);
    sqlite3_bind_int(statement, 4, is_admin ? AUTH_ROLE_ADMIN : AUTH_ROLE_USER);

    if (enable_totp) {

        sqlite3_bind_blob(statement, 5, totp_salt, sizeof(totp_salt), SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 6, totp_nonce, sizeof(totp_nonce), SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 7, totp_tag, sizeof(totp_tag), SQLITE_TRANSIENT);
        sqlite3_bind_blob(statement, 8, totp_ciphertext, AUTH_TOTP_SECRET_BYTES, SQLITE_TRANSIENT);

    }

    else {

        sqlite3_bind_null(statement, 5);
        sqlite3_bind_null(statement, 6);
        sqlite3_bind_null(statement, 7);
        sqlite3_bind_null(statement, 8);

    }

    sqlite3_bind_int64(statement, 9, (sqlite3_int64)time(NULL));
    success = sqlite3_step(statement) == SQLITE_DONE;

cleanup:
    sqlite3_finalize(statement);
    auth_secure_zero(password_encoded, sizeof(password_encoded));
    auth_secure_zero(totp_salt, sizeof(totp_salt));
    auth_secure_zero(totp_nonce, sizeof(totp_nonce));
    auth_secure_zero(totp_tag, sizeof(totp_tag));
    auth_secure_zero(totp_ciphertext, sizeof(totp_ciphertext));
    return success;
}

static uint32_t auth_totp_at_counter(const unsigned char *secret, int secret_size, uint64_t counter,
                                     const char *algorithm) {
    /*
        Purpose: Generates a TOTP code for a counter
        Returns: Success status
    */

    unsigned char counter_bytes[8];
    unsigned char digest[EVP_MAX_MD_SIZE];
    const EVP_MD *digest_algorithm = auth_digest_from_name(algorithm);
    unsigned int digest_length = 0;
    uint32_t binary_code;
    int offset;

    if (!secret || secret_size != AUTH_TOTP_SECRET_BYTES || !digest_algorithm) {

        return UINT32_MAX;

    }

    for (int i = 7; i >= 0; i--) {
        counter_bytes[i] = (unsigned char)(counter & 0xffU);
        counter >>= 8;
    }

    if (!HMAC(digest_algorithm, secret, secret_size, counter_bytes, sizeof(counter_bytes), digest, &digest_length) ||
        digest_length < 20) {

        auth_secure_zero(digest, sizeof(digest));
        return UINT32_MAX;

    }

    offset = digest[digest_length - 1] & 0x0f;

    if ((unsigned int)(offset + 3) >= digest_length) {

        auth_secure_zero(digest, sizeof(digest));
        return UINT32_MAX;

    }

    binary_code = ((uint32_t)(digest[offset] & 0x7f) << 24) | ((uint32_t)digest[offset + 1] << 16) |
                  ((uint32_t)digest[offset + 2] << 8) | (uint32_t)digest[offset + 3];
    auth_secure_zero(digest, sizeof(digest));
    return binary_code % 1000000U;
}

static int auth_verify_totp_counter_with_algorithm(const unsigned char *secret, int secret_size, const char *code,
                                                   const char *algorithm, uint64_t *matched_counter) {
    /*
        Purpose: Verifies the TOTP counter with algorithm
        Returns: Success status
    */

    uint64_t current_counter;
    unsigned long entered_code;
    char *end = NULL;

    if (!secret || secret_size != AUTH_TOTP_SECRET_BYTES || !code || strlen(code) != AUTH_TOTP_DIGITS ||
        !auth_digest_from_name(algorithm)) {

        return 0;

    }
    for (int i = 0; i < AUTH_TOTP_DIGITS; i++) {

        if (!isdigit((unsigned char)code[i])) {

            return 0;

        }
    }

    entered_code = strtoul(code, &end, 10);

    if (!end || *end != '\0' || entered_code > 999999UL) {

        return 0;

    }

    current_counter = (uint64_t)time(NULL) / AUTH_TOTP_PERIOD_SECONDS;
    for (int offset = -1; offset <= 1; offset++) {
        uint64_t candidate_counter = offset < 0 ? current_counter - 1U : current_counter + (uint64_t)offset;
        uint32_t candidate = auth_totp_at_counter(secret, secret_size, candidate_counter, algorithm);

        if (candidate != UINT32_MAX && candidate == (uint32_t)entered_code) {

            if (matched_counter) {

                *matched_counter = candidate_counter;

            }
            return 1;

        }
    }
    return 0;
}

static int auth_verify_totp_with_algorithm(const unsigned char *secret, int secret_size, const char *code,
                                           const char *algorithm) {
    /*
        Purpose: Verifies the TOTP with algorithm
        Returns: Success status
    */

    return auth_verify_totp_counter_with_algorithm(secret, secret_size, code, algorithm, NULL);
}

static int auth_verify_totp(const unsigned char *secret, const char *code) {
    /*
        Purpose: Verifies the TOTP
        Returns: Success status
    */

    return auth_verify_totp_with_algorithm(secret, AUTH_TOTP_SECRET_BYTES, code, AUTH_TOTP_ALGORITHM_DEFAULT);
}

static void auth_reset_for_login(Type_Auth_State *state, int preserve_status) {
    /*
        Purpose: Resets state for login
        Returns: No value
    */

    char status[AUTH_STATUS_MAX];
    SDL_Color status_color;

    if (!state) {

        return;

    }

    auth_copy_text(status, sizeof(status), state->status);
    status_color = state->status_color;
    auth_clear_sensitive(state);
    memset(state->username, 0, sizeof(state->username));
    state->stage = AUTH_STAGE_LOGIN;
    state->active_field = AUTH_FIELD_USERNAME;
    state->enable_two_factor = 0;
    memset(state->import_path, 0, sizeof(state->import_path));
    auth_secure_zero(&state->pending_server, sizeof(state->pending_server));
    state->pending_server_valid = 0;

    if (preserve_status) {

        auth_copy_text(state->status, sizeof(state->status), status);
        state->status_color = status_color;

    }

    else {

        state->status[0] = '\0';

    }
}

static void auth_reset_for_create(Type_Auth_State *state) {
    /*
        Purpose: Resets state for create
        Returns: No value
    */

    if (!state) {

        return;

    }
    auth_clear_sensitive(state);
    memset(state->username, 0, sizeof(state->username));
    state->stage = AUTH_STAGE_CREATE_USER;
    state->active_field = AUTH_FIELD_USERNAME;
    state->enable_two_factor = 0;
    state->status[0] = '\0';
}

static void auth_reset_for_authorization(Type_Auth_State *state) {
    /*
        Purpose: Resets state for authorization
        Returns: No value
    */

    if (!state) {

        return;

    }
    auth_clear_sensitive(state);
    memset(state->username, 0, sizeof(state->username));
    state->stage = AUTH_STAGE_AUTHORIZE_CREATE;
    state->active_field = AUTH_FIELD_USERNAME;
    state->enable_two_factor = 0;
    state->status[0] = '\0';
}

static int auth_submit_login(sqlite3 *database, Type_Auth_State *state) {
    /*
        Purpose: Submits the login
        Returns: Success status
    */

    Type_Auth_User_Record record;
    char scope[AUTH_USERNAME_MAX + 32];
    int load_result;

    if (!state || state->username[0] == '\0' || state->password[0] == '\0') {

        auth_set_status(state, AUTH_ERROR, "Enter both a username and password.");
        return 0;

    }

    if (!auth_rate_limit_guard(database, state, "LOGIN")) {

        return 0;

    }

    load_result = auth_load_user(database, state->username, &record);

    if (load_result <= 0) {

        auth_dummy_password_work(state->password);

    }

    if (load_result <= 0 || !auth_verify_password(state->password, &record)) {

        auth_rate_limit_note_failure(database, state, "LOGIN", "Invalid username or password.");
        auth_secure_zero(state->password, sizeof(state->password));
        state->active_field = AUTH_FIELD_PASSWORD;
        auth_secure_zero(&record, sizeof(record));
        return 0;

    }

    auth_rate_limit_scope(scope, sizeof(scope), "LOGIN", state->username);
    auth_rate_limit_success(database, scope);

    if (!record.totp_enabled) {

        auth_secure_zero(state->password, sizeof(state->password));
        auth_secure_zero(&record, sizeof(record));
        return 1;

    }

    if (!auth_decrypt_totp_secret(state->username, &record, state->active_totp_secret)) {

        auth_set_status(state, AUTH_ERROR, "Unable to unlock this account's 2FA secret.");
        auth_secure_zero(state->password, sizeof(state->password));
        auth_secure_zero(&record, sizeof(record));
        return 0;

    }

    state->active_totp_secret_valid = 1;
    state->active_totp_secret_bytes = record.totp_secret_bytes;
    auth_copy_text(state->active_totp_algorithm, sizeof(state->active_totp_algorithm), record.totp_algorithm);
    auth_secure_zero(state->password, sizeof(state->password));
    auth_secure_zero(state->code, sizeof(state->code));
    auth_secure_zero(&record, sizeof(record));
    state->stage = AUTH_STAGE_LOGIN_TWO_FACTOR;
    state->active_field = AUTH_FIELD_CODE;
    state->status[0] = '\0';
    return 0;
}

static int auth_submit_login_two_factor(sqlite3 *database, Type_Auth_State *state) {
    /*
        Purpose: Submits the login two factor
        Returns: Success status
    */

    char scope[AUTH_USERNAME_MAX + 32];

    if (!auth_rate_limit_guard(database, state, "LOGIN_TOTP")) {

        return 0;

    }

    if (!state) {

        return 0;

    }

    if (!state->active_totp_secret_valid ||
        !auth_verify_totp_with_algorithm(state->active_totp_secret, state->active_totp_secret_bytes, state->code,
                                         state->active_totp_algorithm)) {

        auth_rate_limit_note_failure(database, state, "LOGIN_TOTP", "Invalid or expired authentication code.");
        auth_secure_zero(state->code, sizeof(state->code));
        return 0;

    }

    auth_rate_limit_scope(scope, sizeof(scope), "LOGIN_TOTP", state->username);
    auth_rate_limit_success(database, scope);
    auth_secure_zero(state->code, sizeof(state->code));
    auth_secure_zero(state->active_totp_secret, sizeof(state->active_totp_secret));
    state->active_totp_secret_valid = 0;
    state->active_totp_secret_bytes = 0;
    auth_secure_zero(state->active_totp_algorithm, sizeof(state->active_totp_algorithm));
    return 1;
}

static void auth_submit_authorize_create(sqlite3 *database, Type_Auth_State *state) {
    /*
        Purpose: Submits the authorize create
        Returns: No value
    */

    Type_Auth_User_Record record;
    char scope[AUTH_USERNAME_MAX + 32];
    int load_result;

    if (!state || state->username[0] == '\0' || state->password[0] == '\0') {

        auth_set_status(state, AUTH_ERROR, "Enter an administrator username and password.");
        return;

    }

    if (!auth_rate_limit_guard(database, state, "ADMIN")) {

        return;

    }

    load_result = auth_load_user(database, state->username, &record);

    if (load_result <= 0) {

        auth_dummy_password_work(state->password);

    }

    if (load_result <= 0 || !auth_verify_password(state->password, &record) ||
        (record.role != AUTH_ROLE_CO_ADMIN && record.role != AUTH_ROLE_ADMIN)) {

        auth_rate_limit_note_failure(database, state, "ADMIN", "Administrator authorization failed.");
        auth_secure_zero(state->password, sizeof(state->password));
        state->active_field = AUTH_FIELD_PASSWORD;
        auth_secure_zero(&record, sizeof(record));
        return;

    }

    auth_rate_limit_scope(scope, sizeof(scope), "ADMIN", state->username);
    auth_rate_limit_success(database, scope);

    if (!record.totp_enabled) {

        auth_secure_zero(state->password, sizeof(state->password));
        auth_secure_zero(&record, sizeof(record));
        state->admin_console_ready = 1;
        return;

    }

    if (!auth_decrypt_totp_secret(state->username, &record, state->active_totp_secret)) {

        auth_set_status(state, AUTH_ERROR, "Unable to unlock the administrator 2FA secret.");
        auth_secure_zero(state->password, sizeof(state->password));
        auth_secure_zero(&record, sizeof(record));
        return;

    }

    state->active_totp_secret_valid = 1;
    state->active_totp_secret_bytes = record.totp_secret_bytes;
    auth_copy_text(state->active_totp_algorithm, sizeof(state->active_totp_algorithm), record.totp_algorithm);
    auth_secure_zero(state->password, sizeof(state->password));
    auth_secure_zero(state->code, sizeof(state->code));
    auth_secure_zero(&record, sizeof(record));
    state->stage = AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR;
    state->active_field = AUTH_FIELD_CODE;
    state->status[0] = '\0';
}

static void auth_submit_authorize_create_two_factor(sqlite3 *database, Type_Auth_State *state) {
    /*
        Purpose: Submits the authorize create two factor
        Returns: No value
    */

    char scope[AUTH_USERNAME_MAX + 32];

    if (!auth_rate_limit_guard(database, state, "ADMIN_TOTP")) {

        return;

    }

    if (!state) {

        return;

    }

    if (!state->active_totp_secret_valid ||
        !auth_verify_totp_with_algorithm(state->active_totp_secret, state->active_totp_secret_bytes, state->code,
                                         state->active_totp_algorithm)) {

        auth_rate_limit_note_failure(database, state, "ADMIN_TOTP",
                                     "Invalid or expired administrator authentication code.");
        auth_secure_zero(state->code, sizeof(state->code));
        return;

    }

    auth_rate_limit_scope(scope, sizeof(scope), "ADMIN_TOTP", state->username);
    auth_rate_limit_success(database, scope);
    auth_secure_zero(state->code, sizeof(state->code));
    auth_secure_zero(state->active_totp_secret, sizeof(state->active_totp_secret));
    state->active_totp_secret_valid = 0;
    state->active_totp_secret_bytes = 0;
    auth_secure_zero(state->active_totp_algorithm, sizeof(state->active_totp_algorithm));
    state->admin_console_ready = 1;
}

static int auth_submit_create(sqlite3 *database, Type_Auth_State *state) {
    /*
        Purpose: Submits the create
        Returns: Success status
    */

    size_t password_length;

    if (!state || !auth_username_valid(state->username)) {

        auth_set_status(state, AUTH_ERROR, "Username: 3-63 letters, numbers, periods, hyphens, or underscores.");
        return 0;

    }

    password_length = strlen(state->password);

    if (password_length < 10) {

        auth_set_status(state, AUTH_ERROR, "Password must contain at least 10 characters.");
        return 0;

    }

    if (strcmp(state->password, state->confirm_password) != 0) {

        auth_set_status(state, AUTH_ERROR, "The password confirmation does not match.");
        auth_secure_zero(state->confirm_password, sizeof(state->confirm_password));
        state->active_field = AUTH_FIELD_CONFIRM_PASSWORD;
        return 0;

    }

    if (auth_user_exists(database, state->username)) {

        auth_set_status(state, AUTH_ERROR, "That username already exists.");
        return 0;

    }

    if (state->enable_two_factor) {

        if (RAND_bytes(state->active_totp_secret, sizeof(state->active_totp_secret)) != 1) {

            auth_set_status(state, AUTH_ERROR, "Unable to generate a secure 2FA secret.");
            return 0;

        }
        state->active_totp_secret_valid = 1;
        state->active_totp_secret_bytes = AUTH_TOTP_SECRET_BYTES;
        auth_copy_text(state->active_totp_algorithm, sizeof(state->active_totp_algorithm), AUTH_TOTP_ALGORITHM_DEFAULT);
        auth_secure_zero(state->code, sizeof(state->code));
        state->stage = AUTH_STAGE_CREATE_TWO_FACTOR;
        state->active_field = AUTH_FIELD_CODE;
        state->status[0] = '\0';
        return 0;

    }

    if (!auth_insert_user(database, state->username, state->password, 0, auth_count_users(database) == 0, NULL)) {

        auth_set_status(state, AUTH_ERROR, "Unable to create the local user.");
        return 0;

    }

    auth_secure_zero(state->password, sizeof(state->password));
    auth_secure_zero(state->confirm_password, sizeof(state->confirm_password));
    return 1;
}

static int auth_submit_create_two_factor(sqlite3 *database, Type_Auth_State *state) {
    /*
        Purpose: Submits the create two factor
        Returns: Success status
    */

    if (!state) {

        return 0;

    }

    if (!state->active_totp_secret_valid ||
        !auth_verify_totp_with_algorithm(state->active_totp_secret, state->active_totp_secret_bytes, state->code,
                                         state->active_totp_algorithm)) {

        auth_set_status(state, AUTH_ERROR, "The authenticator code is invalid or expired.");
        auth_secure_zero(state->code, sizeof(state->code));
        return 0;

    }

    if (auth_user_exists(database, state->username)) {

        auth_set_status(state, AUTH_ERROR, "That username was created by another process. Choose another username.");
        return 0;

    }

    if (!auth_insert_user(database, state->username, state->password, 1, auth_count_users(database) == 0,
                          state->active_totp_secret)) {

        auth_set_status(state, AUTH_ERROR, "Unable to save the local user.");
        return 0;

    }

    auth_clear_sensitive(state);
    return 1;
}

void AUTH_set_client_only_mode(int client_only) {
    /*
        Purpose: Restricts authentication to the trusted remote server
        Returns: No value
    */

    Global_Auth_Client_Only_Mode = client_only != 0;
}

static int auth_submit_remote(Type_Auth_State *state) {
    /*
        Purpose: Submits remote application login or co-administrator-console authorization
        Returns: Success status
    */

    char message[AUTH_STATUS_MAX] = "";
    int is_admin = 0;
    int result;
    int console_authorization;

    if (!state || state->username[0] == '\0' || state->password[0] == '\0') {

        auth_set_status(state, AUTH_ERROR, "Enter both a username and password.");
        return 0;

    }

    console_authorization =
        state->stage == AUTH_STAGE_AUTHORIZE_CREATE || state->stage == AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR;

    if (state->stage != AUTH_STAGE_LOGIN && state->stage != AUTH_STAGE_LOGIN_TWO_FACTOR && !console_authorization) {

        auth_set_status(state, AUTH_ERROR, "Unsupported remote authentication operation.");
        return 0;

    }

    result = SECURE_NETWORK_authenticate(state->username, state->password,
                                         state->stage == AUTH_STAGE_LOGIN_TWO_FACTOR ||
                                                 state->stage == AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR
                                             ? state->code
                                             : NULL,
                                         &is_admin, message, sizeof(message));

    if (result == SECURE_NETWORK_AUTH_SUCCESS) {

        auth_secure_zero(state->password, sizeof(state->password));
        auth_secure_zero(state->code, sizeof(state->code));

        if (console_authorization) {

            if (!is_admin) {

                SECURE_NETWORK_disconnect();
                auth_set_status(state, AUTH_ERROR, "Remote admin-console access requires a co-administrator account.");
                state->stage = AUTH_STAGE_AUTHORIZE_CREATE;
                state->active_field = AUTH_FIELD_PASSWORD;
                return 0;

            }

            state->admin_console_ready = 1;
            return 0;

        }

        return 1;

    }

    if (result == SECURE_NETWORK_AUTH_TOTP_REQUIRED) {

        state->stage = console_authorization ? AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR : AUTH_STAGE_LOGIN_TWO_FACTOR;
        state->active_field = AUTH_FIELD_CODE;
        auth_secure_zero(state->code, sizeof(state->code));
        auth_set_status(state, AUTH_MUTED,
                        console_authorization ? "Enter the co-administrator authenticator code."
                                              : "Enter the authenticator code for the remote server.");
        return 0;

    }

    auth_secure_zero(state->code, sizeof(state->code));
    auth_set_status(state, AUTH_ERROR, message[0] ? message : "Remote authentication failed.");
    return 0;
}

static int auth_submit(sqlite3 *database, Type_Auth_State *state) {
    /*
        Purpose: Submits the requested operation
        Returns: Success status
    */

    if (!state) {

        return 0;

    }

    if (Global_Auth_Client_Only_Mode) {

        if (SERVER_IDENTITY_trusted_is_local()) {

            auth_set_status(state, AUTH_ERROR,
                            "Client mode requires a server-provided .rspub key. Use CHANGE SERVER first.");
            return 0;

        }
        return auth_submit_remote(state);

    }

    if (!SERVER_IDENTITY_trusted_is_local()) {

        return auth_submit_remote(state);

    }

    if (!database) {

        return 0;

    }

    if (state->stage == AUTH_STAGE_LOGIN) {

        return auth_submit_login(database, state);

    }

    if (state->stage == AUTH_STAGE_LOGIN_TWO_FACTOR) {

        return auth_submit_login_two_factor(database, state);

    }

    if (state->stage == AUTH_STAGE_AUTHORIZE_CREATE) {

        auth_submit_authorize_create(database, state);
        return 0;

    }

    if (state->stage == AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR) {

        auth_submit_authorize_create_two_factor(database, state);
        return 0;

    }

    if (state->stage == AUTH_STAGE_CREATE_USER || state->stage == AUTH_STAGE_CREATE_TWO_FACTOR) {

        auth_set_status(state, AUTH_ERROR,
                        "Account creation is restricted to the authenticated administrator console.");
        return 0;

    }
    return 0;
}

static int auth_run_transition(SDL_Window *window, SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                               const char *username) {
    /*
        Purpose: Runs the transition
        Returns: Success status
    */

    const Uint64 duration = 1050;
    Uint64 start = SDL_GetTicks64();
    int running = 1;

    while (running) {
        Uint64 now = SDL_GetTicks64();
        double progress = (double)(now - start) / (double)duration;
        int width = 0;
        int height = 0;
        SDL_Rect central_line;
        SDL_Rect message_rect;
        char session_text[128];

        if (progress >= 1.0) {

            break;

        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {

                running = 0;

            }
        }

        if (!running) {

            break;

        }

        SDL_GetWindowSize(window, &width, &height);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        auth_draw_grid(renderer, width, height, (int)(progress * 48.0));

        central_line.w = (int)((double)width * (progress < 0.45 ? progress / 0.45 : 1.0));

        if (central_line.w > width) {

            central_line.w = width;

        }
        central_line.h = progress < 0.45 ? 4 : (int)(4.0 + (progress - 0.45) * 210.0);

        if (central_line.h > 118) {

            central_line.h = 118;

        }
        central_line.x = (width - central_line.w) / 2;
        central_line.y = (height - central_line.h) / 2;

        auth_fill_rect(renderer, central_line, (SDL_Color){0, 38, 16, 245});
        auth_outline_rect(renderer, central_line, AUTH_BORDER_ACTIVE);

        if (progress > 0.38) {

            message_rect = (SDL_Rect){central_line.x, central_line.y + 16, central_line.w, 36};
            auth_draw_centered_text(renderer, font_medium, "ACCESS GRANTED", message_rect, AUTH_TEXT);

            if (!sec_sprintf(session_text, sizeof(session_text), "SESSION ESTABLISHED: %s @ %.72s",
                             username ? username : "USER", Global_Auth_Server_Name)) {

                session_text[0] = '\0';

            }

            auth_draw_centered_text(renderer, font_small, session_text,
                                    (SDL_Rect){central_line.x, central_line.y + 62, central_line.w, 26}, AUTH_MUTED);

        }

        if (progress > 0.72) {

            Uint8 alpha = (Uint8)(((progress - 0.72) / 0.28) * 255.0);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            auth_fill_rect(renderer, (SDL_Rect){0, 0, width, height}, (SDL_Color){0, 0, 0, alpha});

        }

        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    return running;
}

static void auth_cycle_field(Type_Auth_State *state) {
    /*
        Purpose: Cycles the field
        Returns: No value
    */

    if (!state) {

        return;

    }

    if (state->stage == AUTH_STAGE_LOGIN || state->stage == AUTH_STAGE_AUTHORIZE_CREATE) {

        state->active_field = state->active_field == AUTH_FIELD_USERNAME ? AUTH_FIELD_PASSWORD : AUTH_FIELD_USERNAME;

    }

    else if (state->stage == AUTH_STAGE_LOGIN_TWO_FACTOR || state->stage == AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR ||
             state->stage == AUTH_STAGE_CREATE_TWO_FACTOR) {

        state->active_field = AUTH_FIELD_CODE;

    }

    else if (state->stage == AUTH_STAGE_CREATE_USER) {

        if (state->active_field == AUTH_FIELD_USERNAME) {

            state->active_field = AUTH_FIELD_PASSWORD;

        }

        else if (state->active_field == AUTH_FIELD_PASSWORD) {

            state->active_field = AUTH_FIELD_CONFIRM_PASSWORD;

        }

        else {

            state->active_field = AUTH_FIELD_USERNAME;

        }

    }

    else if (state->stage == AUTH_STAGE_DATABASE_KEY_PATH || state->stage == AUTH_STAGE_CHANGE_SERVER_PATH) {

        state->active_field = AUTH_FIELD_IMPORT_PATH;

    }
}

static void auth_handle_text_input(Type_Auth_State *state, const char *text) {
    /*
        Purpose: Handles the text input
        Returns: No value
    */

    if (!state || !text) {

        return;

    }

    if (state->active_field == AUTH_FIELD_IMPORT_PATH) {

        auth_import_path_insert(state, text);

    }

    else if (state->active_field == AUTH_FIELD_USERNAME) {

        auth_append_text(state->username, sizeof(state->username), text, 0);

    }

    else if (state->active_field == AUTH_FIELD_PASSWORD) {

        auth_append_text(state->password, sizeof(state->password), text, 0);

    }

    else if (state->active_field == AUTH_FIELD_CONFIRM_PASSWORD) {

        auth_append_text(state->confirm_password, sizeof(state->confirm_password), text, 0);

    }

    else if (state->active_field == AUTH_FIELD_CODE) {

        auth_append_text(state->code, sizeof(state->code), text, 1);

    }
    state->status[0] = '\0';
}

static void auth_handle_backspace(Type_Auth_State *state) {
    /*
        Purpose: Handles the backspace
        Returns: No value
    */

    if (!state) {

        return;

    }

    if (state->active_field == AUTH_FIELD_IMPORT_PATH) {

        auth_import_path_backspace(state);

    }

    else if (state->active_field == AUTH_FIELD_USERNAME) {

        auth_backspace(state->username);

    }

    else if (state->active_field == AUTH_FIELD_PASSWORD) {

        auth_backspace(state->password);

    }

    else if (state->active_field == AUTH_FIELD_CONFIRM_PASSWORD) {

        auth_backspace(state->confirm_password);

    }

    else if (state->active_field == AUTH_FIELD_CODE) {

        auth_backspace(state->code);

    }
    state->status[0] = '\0';
}

static void auth_handle_mouse(Type_Auth_State *state, int mouse_x, int mouse_y, int width, int height,
                              sqlite3 **database, int *authenticated, int *admin_request) {
    /*
        Purpose: Handles the mouse
        Returns: No value
    */

    SDL_Rect panel;
    SDL_Rect top_left;
    SDL_Rect database_key_button;
    SDL_Rect top_right;
    SDL_Rect primary_button;
    int show_change_server;

    auth_make_panel(width, height, state->stage, &panel);
    top_left = auth_top_left_button_rect(width);
    database_key_button = auth_database_key_button_rect(width);
    top_right = auth_top_right_button_rect(width);
    primary_button = auth_primary_button_rect(panel);
    show_change_server = state->stage == AUTH_STAGE_LOGIN || state->stage == AUTH_STAGE_LOGIN_TWO_FACTOR;

    if (show_change_server && auth_point_in_rect(mouse_x, mouse_y, top_left)) {

        auth_clear_sensitive(state);
        memset(state->import_path, 0, sizeof(state->import_path));
        state->import_cursor = 0;
        state->import_select_all = 0;
        auth_secure_zero(&state->pending_server, sizeof(state->pending_server));
        state->pending_server_valid = 0;
        state->stage = AUTH_STAGE_CHANGE_SERVER_PATH;
        state->active_field = AUTH_FIELD_IMPORT_PATH;
        auth_set_status(state, AUTH_MUTED, "Select the public-key file distributed by your organization.");
        return;

    }

    if (show_change_server && auth_point_in_rect(mouse_x, mouse_y, database_key_button)) {

        const char *current_path;

        if (Global_Auth_Client_Only_Mode) {

            auth_set_status(state, AUTH_ERROR, "Client mode does not open a local authentication database.");
            return;

        }

        current_path = DATABASE_CRYPTO_key_path();
        auth_clear_sensitive(state);
        auth_copy_text(state->import_path, sizeof(state->import_path), current_path ? current_path : "");
        state->import_cursor = strlen(state->import_path);
        state->import_select_all = 0;
        state->stage = AUTH_STAGE_DATABASE_KEY_PATH;
        state->active_field = AUTH_FIELD_IMPORT_PATH;
        auth_set_status(state, AUTH_MUTED, "Select the existing 32-byte key file used to unlock the databases.");
        return;

    }

    if (auth_point_in_rect(mouse_x, mouse_y, top_right)) {

        if (show_change_server) {

            if (Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) {

                if (SERVER_IDENTITY_trusted_is_local()) {

                    auth_set_status(state, AUTH_ERROR,
                                    "Import the remote server's .rspub identity before opening its admin console.");
                    return;

                }

                auth_reset_for_authorization(state);
                auth_set_status(state, AUTH_MUTED, "Remote co-administrator credentials are required.");
                return;

            }

            if (!state->database_ready || !database || !*database) {

                auth_set_status(state, AUTH_ERROR, "Select a valid database key file before account administration.");
                return;

            }

            if (!SERVER_IDENTITY_trusted_is_local()) {

                auth_set_status(state, AUTH_ERROR,
                                "Remote account administration is restricted to the server console.");
                return;

            }

            if (state->user_count == 0) {

                if (admin_request) {

                    *admin_request = 2;

                }

            }

            else {

                auth_reset_for_authorization(state);
                auth_set_status(state, AUTH_MUTED, "Administrator credentials are required.");

            }

        }

        else {

            auth_reset_for_login(state, 0);

        }
        return;

    }

    if (state->stage == AUTH_STAGE_LOGIN || state->stage == AUTH_STAGE_AUTHORIZE_CREATE) {

        if (auth_point_in_rect(mouse_x, mouse_y, auth_field_rect(panel, 0))) {

            state->active_field = AUTH_FIELD_USERNAME;

        }

        else if (auth_point_in_rect(mouse_x, mouse_y, auth_field_rect(panel, 1))) {

            state->active_field = AUTH_FIELD_PASSWORD;

        }

    }

    else if (state->stage == AUTH_STAGE_LOGIN_TWO_FACTOR || state->stage == AUTH_STAGE_AUTHORIZE_CREATE_TWO_FACTOR) {

        if (auth_point_in_rect(mouse_x, mouse_y, auth_field_rect(panel, 1))) {

            state->active_field = AUTH_FIELD_CODE;

        }

    }

    else if (state->stage == AUTH_STAGE_CREATE_USER) {

        if (auth_point_in_rect(mouse_x, mouse_y, auth_field_rect(panel, 0))) {

            state->active_field = AUTH_FIELD_USERNAME;

        }

        else if (auth_point_in_rect(mouse_x, mouse_y, auth_field_rect(panel, 1))) {

            state->active_field = AUTH_FIELD_PASSWORD;

        }

        else if (auth_point_in_rect(mouse_x, mouse_y, auth_field_rect(panel, 2))) {

            state->active_field = AUTH_FIELD_CONFIRM_PASSWORD;

        }

        else if (auth_point_in_rect(mouse_x, mouse_y, auth_checkbox_rect(panel))) {

            state->enable_two_factor = !state->enable_two_factor;

        }

    }

    else if (state->stage == AUTH_STAGE_CREATE_TWO_FACTOR) {

        if (auth_point_in_rect(mouse_x, mouse_y, auth_create_two_factor_code_rect(panel))) {

            state->active_field = AUTH_FIELD_CODE;

        }

        else if (auth_point_in_rect(mouse_x, mouse_y, auth_copy_secret_button_rect(panel))) {

            char secret[AUTH_TOTP_BASE32_MAX];

            if (state->active_totp_secret_valid &&
                auth_base32_encode(state->active_totp_secret, sizeof(state->active_totp_secret), secret,
                                   sizeof(secret)) &&
                SDL_SetClipboardText(secret) == 0) {

                auth_set_status(state, AUTH_MUTED, "2FA secret copied to the clipboard.");

            }

            else {

                auth_set_status(state, AUTH_ERROR, "Unable to copy the 2FA secret.");

            }
            auth_secure_zero(secret, sizeof(secret));
            return;

        }

    }

    else if (state->stage == AUTH_STAGE_DATABASE_KEY_PATH || state->stage == AUTH_STAGE_CHANGE_SERVER_PATH) {

        SDL_Rect path_field = (SDL_Rect){panel.x + 50, panel.y + 170, panel.w - 100, 104};

        if (auth_point_in_rect(mouse_x, mouse_y, path_field)) {

            state->active_field = AUTH_FIELD_IMPORT_PATH;
            state->import_cursor = strlen(state->import_path);
            state->import_select_all = 0;

        }

    }

    if (!auth_point_in_rect(mouse_x, mouse_y, primary_button)) {

        return;

    }

    if (state->stage == AUTH_STAGE_DATABASE_KEY_PATH) {

        char message[AUTH_STATUS_MAX];
        char database_path[PATH_MAX];

        if (!DATABASE_CRYPTO_set_key_path(state->import_path, message, sizeof(message))) {

            auth_set_status(state, AUTH_ERROR, message);
            return;

        }

        if (database && *database) {

            sqlite3_close(*database);
            *database = NULL;

        }

        if (!database || !auth_open_database(database, database_path, sizeof(database_path))) {

            state->database_ready = 0;
            auth_set_status(state, AUTH_ERROR,
                            "The key file was loaded, but the authentication database could not be opened.");
            return;

        }
        state->database_ready = 1;
        state->user_count = auth_count_users(*database);
        auth_reset_for_login(state, 0);
        auth_set_status(state, AUTH_MUTED,
                        "Database key loaded. The key file may now be unmounted while RetroSpectrum remains running.");
        return;

    }

    if (state->stage == AUTH_STAGE_CHANGE_SERVER_PATH) {

        char message[AUTH_STATUS_MAX];
        auth_secure_zero(&state->pending_server, sizeof(state->pending_server));
        state->pending_server_valid =
            SERVER_IDENTITY_preview_public_file(state->import_path, &state->pending_server, message, sizeof(message));

        if (!state->pending_server_valid) {

            auth_set_status(state, AUTH_ERROR, message);
            return;

        }
        state->stage = AUTH_STAGE_CHANGE_SERVER_CONFIRM;
        state->active_field = AUTH_FIELD_NONE;
        auth_set_status(state, AUTH_WARN, "Verify the server name and complete SHA-512 fingerprint before approving.");
        return;

    }

    if (state->stage == AUTH_STAGE_CHANGE_SERVER_CONFIRM) {

        char message[AUTH_STATUS_MAX];
        SECURE_NETWORK_disconnect();

        if (!state->pending_server_valid ||
            !SERVER_IDENTITY_import_public_file(state->import_path, message, sizeof(message))) {

            auth_set_status(state, AUTH_ERROR,
                            state->pending_server_valid ? message : "No valid public key is pending approval.");
            return;

        }
        auth_copy_text(Global_Auth_Server_Name, sizeof(Global_Auth_Server_Name), SERVER_IDENTITY_get_trusted_name());
        auth_reset_for_login(state, 0);
        auth_set_status(state, AUTH_MUTED, message);
        return;

    }

    if (Global_Auth_Client_Only_Mode) {

        *authenticated = auth_submit(NULL, state);
        return;

    }

    if (!database || !*database || !state->database_ready) {

        auth_set_status(state, AUTH_ERROR, "Select a valid database key file before logging in.");
        return;

    }
    *authenticated = auth_submit(*database, state);
}

const char *AUTH_get_server_id(void) {
    /*
        Purpose: Gets the server ID
        Returns: Text pointer
    */

    return Global_Auth_Server_Id;
}

const char *AUTH_get_server_name(void) {
    /*
        Purpose: Gets the server name
        Returns: Text pointer
    */

    return Global_Auth_Server_Name;
}

const char *AUTH_get_current_username(void) {
    /*
        Purpose: Gets the username for the active authenticated session
        Returns: Text pointer
    */

    return Global_Auth_Current_Username;
}

int AUTH_run(SDL_Window *window, SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium) {
    /*
        Purpose: Runs the requested operation
        Returns: Success status
    */

    Type_Auth_State state;
    sqlite3 *database = NULL;
    char database_path[PATH_MAX];
    int authenticated = 0;
    int running = 1;
    int admin_request = 0;

    auth_secure_zero(Global_Auth_Current_Username, sizeof(Global_Auth_Current_Username));

    if (!window || !renderer || !font_small || !font_medium) {

        fprintf(stderr, "AUTH_run received an invalid SDL object or font.\n");
        return 0;

    }

    memset(&state, 0, sizeof(state));
    memset(database_path, 0, sizeof(database_path));
    state.stage = AUTH_STAGE_LOGIN;
    state.active_field = AUTH_FIELD_USERNAME;
    state.import_cursor = 0;
    state.import_select_all = 0;
    state.status_color = AUTH_MUTED;

    if (!SERVER_IDENTITY_start() || !SERVER_IDENTITY_get_id()[0] || !SERVER_IDENTITY_get_trusted_name()[0]) {

        fprintf(stderr, "Unable to initialize the RetroSpectrum cryptographic server identity.\n");
        return 0;

    }

    if (Global_Auth_Client_Only_Mode) {

        state.database_ready = 0;
        auth_set_status(&state, AUTH_MUTED,
                        SERVER_IDENTITY_trusted_is_local()
                            ? "Client mode: use CHANGE SERVER to import the server-provided .rspub key."
                            : "Client mode: sign in to the trusted remote server.");

    }

    else if (auth_open_database(&database, database_path, sizeof(database_path))) {

        state.database_ready = 1;

    }

    else {

        state.database_ready = 0;
        auth_set_status(&state, AUTH_ERROR, "Database locked. Use KEY FILE PATH to select the master key.");

    }
    auth_copy_text(Global_Auth_Server_Id, sizeof(Global_Auth_Server_Id), SERVER_IDENTITY_get_id());
    auth_copy_text(Global_Auth_Server_Name, sizeof(Global_Auth_Server_Name), SERVER_IDENTITY_get_trusted_name());
    state.user_count = database ? auth_count_users(database) : 0;

    SDL_StartTextInput();

    while (running && !authenticated) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_QUIT) {

                running = 0;
                break;

            }

            if (event.type == SDL_TEXTINPUT) {

                auth_handle_text_input(&state, event.text.text);
                continue;

            }

            if (event.type == SDL_DROPFILE) {

                if ((state.stage == AUTH_STAGE_DATABASE_KEY_PATH || state.stage == AUTH_STAGE_CHANGE_SERVER_PATH) &&
                    event.drop.file) {

                    auth_copy_text(state.import_path, sizeof(state.import_path), event.drop.file);
                    state.import_cursor = strlen(state.import_path);
                    state.import_select_all = 0;
                    state.active_field = AUTH_FIELD_IMPORT_PATH;
                    auth_set_status(&state, AUTH_MUTED,
                                    state.stage == AUTH_STAGE_DATABASE_KEY_PATH
                                        ? "Database key file selected. Use it to unlock the databases."
                                        : "Public-key file selected. Review it before trusting it.");

                }

                if (event.drop.file) {

                    SDL_free(event.drop.file);

                }
                continue;

            }

            if (event.type == SDL_KEYDOWN) {

                SDL_Keycode key = event.key.keysym.sym;
                SDL_Keymod modifiers = SDL_GetModState();

                if (state.active_field == AUTH_FIELD_IMPORT_PATH && (modifiers & KMOD_CTRL) != 0) {

                    if (key == SDLK_a) {

                        state.import_select_all = 1;
                        state.import_cursor = strlen(state.import_path);
                        continue;

                    }

                    if (key == SDLK_c) {

                        auth_import_path_copy(&state);
                        continue;

                    }

                    if (key == SDLK_v) {

                        auth_import_path_paste(&state);
                        continue;

                    }

                    if (key == SDLK_x) {

                        auth_import_path_copy(&state);
                        state.import_path[0] = '\0';
                        state.import_cursor = 0;
                        state.import_select_all = 0;
                        continue;

                    }

                }

                if (state.active_field == AUTH_FIELD_IMPORT_PATH) {

                    size_t path_length = strlen(state.import_path);
                    auth_import_path_clamp_cursor(&state);

                    if (key == SDLK_LEFT) {

                        state.import_select_all = 0;
                        state.import_cursor = auth_utf8_previous_index(state.import_path, state.import_cursor);
                        continue;

                    }

                    if (key == SDLK_RIGHT) {

                        state.import_select_all = 0;
                        state.import_cursor = auth_utf8_next_index(state.import_path, path_length, state.import_cursor);
                        continue;

                    }

                    if (key == SDLK_HOME) {

                        state.import_select_all = 0;
                        state.import_cursor = 0;
                        continue;

                    }

                    if (key == SDLK_END) {

                        state.import_select_all = 0;
                        state.import_cursor = path_length;
                        continue;

                    }

                    if (key == SDLK_DELETE) {

                        auth_import_path_delete(&state);
                        state.status[0] = '\0';
                        continue;

                    }

                }

                if (key == SDLK_ESCAPE) {

                    if (state.stage == AUTH_STAGE_LOGIN) {

                        running = 0;
                        break;

                    }
                    auth_reset_for_login(&state, 0);
                    continue;

                }

                if (key == SDLK_TAB) {

                    auth_cycle_field(&state);
                    continue;

                }

                if (key == SDLK_BACKSPACE) {

                    auth_handle_backspace(&state);
                    continue;

                }

                if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

                    if (state.stage == AUTH_STAGE_DATABASE_KEY_PATH || state.stage == AUTH_STAGE_CHANGE_SERVER_PATH ||
                        state.stage == AUTH_STAGE_CHANGE_SERVER_CONFIRM) {

                        int width = 0;
                        int height = 0;
                        SDL_Rect panel;
                        SDL_Rect button;
                        SDL_GetWindowSize(window, &width, &height);
                        auth_make_panel(width, height, state.stage, &panel);
                        button = auth_primary_button_rect(panel);
                        auth_handle_mouse(&state, button.x + button.w / 2, button.y + button.h / 2, width, height,
                                          &database, &authenticated, &admin_request);

                    }

                    else if (Global_Auth_Client_Only_Mode) {

                        authenticated = auth_submit(NULL, &state);

                    }

                    else if (database && state.database_ready) {

                        authenticated = auth_submit(database, &state);

                    }

                    else {

                        auth_set_status(&state, AUTH_ERROR, "Select a valid database key file before logging in.");

                    }

                    if (!authenticated && database) {

                        state.user_count = auth_count_users(database);

                    }
                    continue;

                }

            }

            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {

                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                auth_handle_mouse(&state, event.button.x, event.button.y, width, height, &database, &authenticated,
                                  &admin_request);

                if (!authenticated && database) {

                    state.user_count = auth_count_users(database);

                }

            }
        }

        if (running && !authenticated && admin_request == 2) {

            (void)AUTH_ADMIN_run(window, renderer, font_small, font_medium, NULL, 1);
            admin_request = 0;
            state.user_count = database ? auth_count_users(database) : 0;
            auth_reset_for_login(&state, 0);

            if (state.user_count > 0) {

                auth_set_status(&state, AUTH_MUTED, "Administrator initialized. Log in to continue.");

            }

        }

        if (running && !authenticated && state.admin_console_ready) {

            char administrator[AUTH_USERNAME_MAX + 1];
            int remote_console = SECURE_NETWORK_is_authenticated_remote();
            auth_copy_text(administrator, sizeof(administrator), state.username);
            state.admin_console_ready = 0;
            (void)AUTH_ADMIN_run(window, renderer, font_small, font_medium, administrator, 0);

            if (remote_console) {

                SECURE_NETWORK_disconnect();

            }

            auth_secure_zero(administrator, sizeof(administrator));
            state.user_count = database ? auth_count_users(database) : 0;
            auth_reset_for_login(&state, 0);
            auth_set_status(&state, AUTH_MUTED,
                            remote_console ? "Remote co-administrator console closed."
                                           : "Administrator console closed.");

        }

        if (running && !authenticated) {

            auth_render(window, renderer, font_small, font_medium, &state);
            SDL_Delay(8);

        }
    }

    if (authenticated) {

        auth_copy_text(Global_Auth_Current_Username, sizeof(Global_Auth_Current_Username), state.username);
        running = auth_run_transition(window, renderer, font_small, font_medium, state.username);

    }

    if (!authenticated || !running) {

        auth_secure_zero(Global_Auth_Current_Username, sizeof(Global_Auth_Current_Username));

    }

    auth_clear_sensitive(&state);
    sqlite3_close(database);
    return authenticated && running;
}

static void auth_public_error(char *error, size_t error_size, const char *message) {
    /*
        Purpose: Sets a public authentication error message
        Returns: No value
    */

    if (error && error_size > 0) {

        snprintf(error, error_size, "%s", message ? message : "Authentication operation failed.");

    }
}

static int auth_role_is_privileged(int role) {
    /*
        Purpose: Checks whether the role is privileged
        Returns: Success status
    */

    return role == AUTH_ROLE_CO_ADMIN || role == AUTH_ROLE_ADMIN;
}

static int auth_get_user_role(sqlite3 *database, const char *username, int *role) {
    /*
        Purpose: Gets the user role
        Returns: Success status
    */

    sqlite3_stmt *statement = NULL;
    int found = 0;

    if (role) {

        *role = AUTH_ROLE_USER;

    }

    if (!database || !username || username[0] == '\0' || !role ||
        sqlite3_prepare_v2(database, "SELECT role FROM users WHERE username = ?1 LIMIT 1;", -1, &statement, NULL) !=
            SQLITE_OK) {

        return 0;

    }
    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) == SQLITE_ROW) {

        int value = sqlite3_column_int(statement, 0);

        if (value >= AUTH_ROLE_USER && value <= AUTH_ROLE_ADMIN) {

            *role = value;
            found = 1;

        }

    }
    sqlite3_finalize(statement);
    return found;
}

static int auth_authorize_account_management(sqlite3 *database, const char *acting_admin, const char *target_username,
                                             int allow_primary_target, int *actor_role, int *target_role, char *error,
                                             size_t error_size) {
    /*
        Purpose: Authorizes an account management operation
        Returns: Success status
    */

    int local_actor_role = AUTH_ROLE_USER;
    int local_target_role = AUTH_ROLE_USER;

    if (!database || !acting_admin || acting_admin[0] == '\0' ||
        !auth_get_user_role(database, acting_admin, &local_actor_role) || !auth_role_is_privileged(local_actor_role)) {

        auth_public_error(error, error_size, "Administrator or co-administrator authorization is required.");
        return 0;

    }

    if (target_username) {

        if (!auth_get_user_role(database, target_username, &local_target_role)) {

            auth_public_error(error, error_size, "The selected user account no longer exists.");
            return 0;

        }

        if (!allow_primary_target && local_target_role == AUTH_ROLE_ADMIN) {

            auth_public_error(error, error_size, "The primary administrator account is protected.");
            return 0;

        }

        if (local_target_role == AUTH_ROLE_ADMIN && local_actor_role != AUTH_ROLE_ADMIN) {

            auth_public_error(error, error_size, "Co-administrators cannot modify the primary administrator account.");
            return 0;

        }

    }

    if (actor_role) {

        *actor_role = local_actor_role;

    }

    if (target_role) {

        *target_role = local_target_role;

    }
    return 1;
}

int AUTH_SERVER_authenticate(const char *username, const char *password, const char *totp, const char *remote_ip,
                             int *is_admin, char *error, size_t error_size) {
    /*
        Purpose: Authenticates the requested operation
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    Type_Auth_User_Record record;
    unsigned char secret[AUTH_TOTP_SECRET_BYTES];
    char path[PATH_MAX];
    char ip_scope[128];
    char account_scope[256];
    uint64_t matched_counter = 0;
    int load_result;
    int is_local_console;
    int result = AUTH_SERVER_RESULT_ERROR;

    memset(&record, 0, sizeof(record));
    memset(secret, 0, sizeof(secret));

    if (error && error_size > 0) {

        error[0] = '\0';

    }

    if (is_admin) {

        *is_admin = 0;

    }

    if (!username || !password || !remote_ip || !sec_sprintf(ip_scope, sizeof(ip_scope), "NET_IP:%s", remote_ip) ||
        !sec_sprintf(account_scope, sizeof(account_scope), "NET_LOGIN:%s:%s", username, remote_ip)) {

        auth_public_error(error, error_size, "Invalid encrypted authentication request.");
        return result;

    }

    /*
     * The local administration CLI deliberately identifies its trusted
     * server-console authentication path with the internal "local-cli"
     * origin. Network authentication supplies a numeric peer address from
     * inet_ntop(), so remote clients cannot select this path.
     */
    is_local_console = strcmp(remote_ip, "local-cli") == 0;

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Authentication service unavailable.");
        return result;

    }

    if (auth_rate_limit_remaining(database, ip_scope) > 0 || auth_rate_limit_remaining(database, account_scope) > 0) {

        auth_public_error(error, error_size, "Too many authentication attempts. Try again later.");
        goto cleanup;

    }

    load_result = auth_load_user(database, username, &record);

    if (load_result <= 0) {

        auth_dummy_password_work(password);

    }

    if (load_result <= 0 || !auth_verify_password(password, &record)) {

        (void)auth_rate_limit_failure(database, ip_scope);
        (void)auth_rate_limit_failure(database, account_scope);
        auth_public_error(error, error_size, "Invalid username, password, or authentication code.");
        goto cleanup;

    }

    if (record.totp_enabled) {

        if (!totp || totp[0] == '\0') {

            result = AUTH_SERVER_RESULT_TOTP_REQUIRED;
            goto cleanup;

        }

        if (!auth_decrypt_totp_secret(username, &record, secret) ||
            !auth_verify_totp_counter_with_algorithm(secret, record.totp_secret_bytes, totp, record.totp_algorithm,
                                                     &matched_counter)) {

            (void)auth_rate_limit_failure(database, ip_scope);
            (void)auth_rate_limit_failure(database, account_scope);
            auth_public_error(error, error_size, "Invalid username, password, or authentication code.");
            goto cleanup;

        }

        if (matched_counter > INT64_MAX || (int64_t)matched_counter <= record.last_totp_counter ||
            sqlite3_prepare_v2(database,
                               "UPDATE users SET last_totp_counter = ?1 "
                               "WHERE username = ?2 AND last_totp_counter < ?1;",
                               -1, &statement, NULL) != SQLITE_OK) {

            auth_public_error(error, error_size, "Authentication code was already used.");
            goto cleanup;

        }
        sqlite3_bind_int64(statement, 1, (sqlite3_int64)matched_counter);
        sqlite3_bind_text(statement, 2, username, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {

            auth_public_error(error, error_size, "Authentication code was already used.");
            goto cleanup;

        }

    }

    /*
     * Remote sessions may be opened by normal users and co-administrators.
     * The primary administrator is intentionally restricted to the local
     * server console so its highest-privilege credentials cannot be used
     * across the network.
     */

    if (record.role == AUTH_ROLE_ADMIN && !is_local_console) {

        auth_rate_limit_success(database, ip_scope);
        auth_rate_limit_success(database, account_scope);
        auth_public_error(error, error_size,
                          "The primary administrator may only sign in locally on the server console.");
        goto cleanup;

    }

    auth_rate_limit_success(database, ip_scope);
    auth_rate_limit_success(database, account_scope);

    if (is_admin) {

        /*
         * Remote privileged sessions are limited to co-administrators. The
         * primary administrator is privileged only on the local server CLI.
         */
        *is_admin = record.role == AUTH_ROLE_CO_ADMIN || (is_local_console && record.role == AUTH_ROLE_ADMIN);

    }
    result = AUTH_SERVER_RESULT_SUCCESS;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close(database);
    auth_secure_zero(secret, sizeof(secret));
    auth_secure_zero(&record, sizeof(record));
    return result;
}

int AUTH_SERVER_verify_password(const char *username, const char *password, const char *remote_ip, char *error,
                                size_t error_size) {
    /*
        Purpose: Re-verifies an authenticated account password without starting a new session
        Returns: Success status
    */

    sqlite3 *database = NULL;
    Type_Auth_User_Record record;
    char path[PATH_MAX];
    char ip_scope[128];
    char account_scope[256];
    int load_result;
    int success = 0;

    memset(&record, 0, sizeof(record));

    if (error && error_size > 0) {

        error[0] = '\0';

    }

    if (!username || username[0] == '\0' || !password || password[0] == '\0' || !remote_ip ||
        !sec_sprintf(ip_scope, sizeof(ip_scope), "NET_REAUTH_IP:%s", remote_ip) ||
        !sec_sprintf(account_scope, sizeof(account_scope), "NET_REAUTH:%s:%s", username, remote_ip)) {

        auth_public_error(error, error_size, "Invalid password verification request.");
        return 0;

    }

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Authentication service unavailable.");
        return 0;

    }

    if (auth_rate_limit_remaining(database, ip_scope) > 0 || auth_rate_limit_remaining(database, account_scope) > 0) {

        auth_public_error(error, error_size, "Too many password attempts. Try again later.");
        goto cleanup;

    }

    load_result = auth_load_user(database, username, &record);

    if (load_result <= 0) {

        auth_dummy_password_work(password);

    }

    if (load_result <= 0 || !auth_verify_password(password, &record)) {

        (void)auth_rate_limit_failure(database, ip_scope);
        (void)auth_rate_limit_failure(database, account_scope);
        auth_public_error(error, error_size, "Invalid password.");
        goto cleanup;

    }

    if (record.role == AUTH_ROLE_ADMIN) {

        auth_rate_limit_success(database, ip_scope);
        auth_rate_limit_success(database, account_scope);
        auth_public_error(error, error_size,
                          "The primary administrator may only authorize actions locally on the server console.");
        goto cleanup;

    }

    auth_rate_limit_success(database, ip_scope);
    auth_rate_limit_success(database, account_scope);
    success = 1;

cleanup:
    sqlite3_close(database);
    auth_secure_zero(&record, sizeof(record));
    return success;
}

int AUTH_verify_current_password(const char *password, char *error, size_t error_size) {
    /*
        Purpose: Verifies the password for the active authenticated session
        Returns: Success status
    */

    if (!password || password[0] == '\0') {

        auth_public_error(error, error_size, "Enter your password.");
        return 0;

    }

    if (Global_Auth_Current_Username[0] == '\0') {

        auth_public_error(error, error_size, "No authenticated user session is available.");
        return 0;

    }

    if (Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) {

        return SECURE_NETWORK_verify_password(Global_Auth_Current_Username, password, error, error_size);

    }

    return AUTH_SERVER_verify_password(Global_Auth_Current_Username, password, "local-gui", error, error_size);
}

int AUTH_DB_server_list_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count, char *error,
                              size_t error_size) {
    /*
        Purpose: Lists users from the server authentication database
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    char path[PATH_MAX];
    size_t used = 0;
    int success = 0;

    if (count) {

        *count = 0;

    }

    if (!users || capacity == 0 || !count) {

        auth_public_error(error, error_size, "Invalid user-list output buffer.");
        return 0;

    }

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Unable to open the authentication database.");
        return 0;

    }

    if (sqlite3_prepare_v2(database,
                           "SELECT username, role, totp_enabled, created_at FROM users "
                           "ORDER BY role DESC, username COLLATE BINARY ASC;",
                           -1, &statement, NULL) != SQLITE_OK) {

        auth_public_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    while (used < capacity && sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *username = sqlite3_column_text(statement, 0);

        if (!username) {

            continue;

        }
        memset(&users[used], 0, sizeof(users[used]));
        auth_copy_text(users[used].username, sizeof(users[used].username), (const char *)username);
        users[used].role = sqlite3_column_int(statement, 1);
        users[used].is_admin = users[used].role >= AUTH_ROLE_CO_ADMIN;
        users[used].totp_enabled = sqlite3_column_int(statement, 2) != 0;
        users[used].created_at = (int64_t)sqlite3_column_int64(statement, 3);
        used++;
    }
    *count = used;
    success = 1;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return success;
}

int AUTH_DB_list_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count, char *error, size_t error_size) {
    /*
        Purpose: Lists users from the authentication database
        Returns: Success status
    */

    if (SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_list_users(users, capacity, count, error, error_size);

    }
    return AUTH_DB_server_list_users(users, capacity, count, error, error_size);
}

int AUTH_DB_create_user(const char *username, const char *password, int enable_totp, int is_admin,
                        const unsigned char *totp_secret, const char *acting_admin, char *error, size_t error_size) {
    /*
        Purpose: Creates the user
        Returns: Success status
    */

    sqlite3 *database = NULL;
    char path[PATH_MAX];
    int user_count;
    int success = 0;

    if (!auth_username_valid(username)) {

        auth_public_error(error, error_size,
                          "Username must contain 3-63 letters, numbers, periods, hyphens, or underscores.");
        return 0;

    }

    if (!password || strlen(password) < 10 || strlen(password) > AUTH_PASSWORD_MAX) {

        auth_public_error(error, error_size, "Password must contain between 10 and 127 characters.");
        return 0;

    }

    if (enable_totp && !totp_secret) {

        auth_public_error(error, error_size, "A valid 2FA secret is required.");
        return 0;

    }

    if ((Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) &&
        SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_admin_create_user(username, password, enable_totp, totp_secret, error, error_size);

    }

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Unable to open the authentication database.");
        return 0;

    }

    user_count = auth_count_users(database);

    if (user_count == 0) {

        is_admin = 1;

    }

    else {

        if (!auth_authorize_account_management(database, acting_admin, NULL, 1, NULL, NULL, error, error_size)) {

            goto cleanup;

        }
        /* Primary-administrator assignment is bootstrap-only. New accounts are
         * created as users and may then be promoted to co-administrator. */
        is_admin = 0;

    }

    if (auth_user_exists(database, username)) {

        auth_public_error(error, error_size, "That username already exists.");
        goto cleanup;

    }
    success = auth_insert_user(database, username, password, enable_totp, is_admin, totp_secret);

    if (!success) {

        auth_public_error(error, error_size, sqlite3_errmsg(database));

    }

cleanup:
    sqlite3_close(database);
    return success;
}

int AUTH_DB_reset_password(const char *username, const char *new_password, const char *acting_admin, char *error,
                           size_t error_size) {
    /*
        Purpose: Resets the password
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    char path[PATH_MAX];
    char encoded[AUTH_ARGON2_ENCODED_MAX];
    int success = 0;

    memset(encoded, 0, sizeof(encoded));

    if (!username || !new_password || strlen(new_password) < 10 || strlen(new_password) > AUTH_PASSWORD_MAX) {

        auth_public_error(error, error_size, "Password must contain between 10 and 127 characters.");
        return 0;

    }

    if ((Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) &&
        SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_admin_reset_password(username, new_password, error, error_size);

    }

    if (!auth_hash_password_argon2id(new_password, encoded)) {

        auth_public_error(error, error_size, "Unable to hash the replacement password.");
        return 0;

    }

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Unable to open the authentication database.");
        goto cleanup;

    }

    if (!auth_authorize_account_management(database, acting_admin, username, 1, NULL, NULL, error, error_size)) {

        goto cleanup;

    }

    if (sqlite3_prepare_v2(database,
                           "UPDATE users SET password_encoded = ?1, password_algorithm = 'argon2id' "
                           "WHERE username = ?2;",
                           -1, &statement, NULL) != SQLITE_OK) {

        auth_public_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    sqlite3_bind_text(statement, 1, encoded, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, username, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {

        auth_public_error(error, error_size, "The selected user account no longer exists.");
        goto cleanup;

    }
    sqlite3_finalize(statement);
    statement = NULL;

    if (sqlite3_prepare_v2(database,
                           "DELETE FROM auth_rate_limits WHERE scope IN "
                           "('LOGIN:' || ?1, 'LOGIN_TOTP:' || ?1, 'ADMIN:' || ?1, 'ADMIN_TOTP:' || ?1);",
                           -1, &statement, NULL) == SQLITE_OK) {

        sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);

    }
    success = 1;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close(database);
    auth_secure_zero(encoded, sizeof(encoded));
    return success;
}

int AUTH_DB_set_totp(const char *username, const unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES],
                     const char *acting_admin, char *error, size_t error_size) {
    /*
        Purpose: Stores TOTP configuration in the authentication database
        Returns: Success status
    */

    sqlite3 *database = NULL;
    char path[PATH_MAX];
    int success = 0;

    if (!auth_username_valid(username) || !secret) {

        auth_public_error(error, error_size, "Invalid 2FA enrollment request.");
        return 0;

    }

    if ((Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) &&
        SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_admin_set_totp(username, secret, error, error_size);

    }

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Unable to open the authentication database.");
        return 0;

    }

    if (!auth_authorize_account_management(database, acting_admin, username, 1, NULL, NULL, error, error_size)) {

        goto cleanup;

    }

    success =
        auth_store_server_wrapped_totp(database, username, secret, AUTH_TOTP_SECRET_BYTES, AUTH_TOTP_ALGORITHM_DEFAULT);

    if (!success) {

        auth_public_error(error, error_size, sqlite3_errmsg(database));

    }

cleanup:
    sqlite3_close(database);
    return success;
}

int AUTH_DB_remove_totp(const char *username, const char *acting_admin, char *error, size_t error_size) {
    /*
        Purpose: Removes the TOTP
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    char path[PATH_MAX];
    int success = 0;

    if (!auth_username_valid(username)) {

        auth_public_error(error, error_size, "Invalid 2FA removal request.");
        return 0;

    }

    if ((Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) &&
        SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_admin_remove_totp(username, error, error_size);

    }

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Unable to open the authentication database.");
        return 0;

    }

    if (!auth_authorize_account_management(database, acting_admin, username, 1, NULL, NULL, error, error_size)) {

        goto cleanup;

    }

    if (sqlite3_prepare_v2(database,
                           "UPDATE users SET totp_enabled = 0, totp_algorithm = 'sha512', "
                           "totp_secret_bytes = 32, totp_kdf_algorithm = 'server-sha512', "
                           "totp_salt = NULL, totp_nonce = NULL, totp_tag = NULL, "
                           "totp_ciphertext = NULL WHERE username = ?1;",
                           -1, &statement, NULL) != SQLITE_OK) {

        auth_public_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }
    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {

        auth_public_error(error, error_size, "The selected user account no longer exists.");
        goto cleanup;

    }
    success = 1;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return success;
}

int AUTH_DB_set_role(const char *username, int role, const char *acting_admin, char *error, size_t error_size) {
    /*
        Purpose: Sets the role
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    char path[PATH_MAX];
    int target_role = AUTH_ROLE_USER;
    int success = 0;

    if (!auth_username_valid(username) || (role != AUTH_ROLE_USER && role != AUTH_ROLE_CO_ADMIN)) {

        auth_public_error(error, error_size, "Invalid account-role update request.");
        return 0;

    }

    if (!acting_admin || acting_admin[0] == '\0' || strcmp(username, acting_admin) == 0) {

        auth_public_error(error, error_size,
                          "The role of the account currently authorizing this console cannot be changed.");
        return 0;

    }

    if ((Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) &&
        SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_admin_set_role(username, role, error, error_size);

    }

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Unable to open the authentication database.");
        return 0;

    }

    if (!auth_authorize_account_management(database, acting_admin, username, 0, NULL, &target_role, error,
                                           error_size)) {

        goto cleanup;

    }

    if (target_role == role) {

        success = 1;
        goto cleanup;

    }

    if (sqlite3_prepare_v2(database, "UPDATE users SET role = ?1 WHERE username = ?2;", -1, &statement, NULL) !=
        SQLITE_OK) {

        auth_public_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }
    sqlite3_bind_int(statement, 1, role);
    sqlite3_bind_text(statement, 2, username, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {

        auth_public_error(error, error_size, "Unable to update the selected account role.");
        goto cleanup;

    }
    success = 1;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return success;
}

int AUTH_DB_delete_user(const char *username, const char *acting_admin, char *error, size_t error_size) {
    /*
        Purpose: Deletes the user
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    char path[PATH_MAX];
    int success = 0;

    if (!username || username[0] == '\0' || !acting_admin || acting_admin[0] == '\0') {

        auth_public_error(error, error_size, "Invalid account deletion request.");
        return 0;

    }

    if (strcmp(username, acting_admin) == 0) {

        auth_public_error(error, error_size,
                          "An administrator cannot delete the account currently authorizing this console.");
        return 0;

    }

    if ((Global_Auth_Client_Only_Mode || !SERVER_IDENTITY_trusted_is_local()) &&
        SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_admin_delete_user(username, error, error_size);

    }

    if (!auth_open_database(&database, path, sizeof(path))) {

        auth_public_error(error, error_size, "Unable to open the authentication database.");
        return 0;

    }

    if (!auth_authorize_account_management(database, acting_admin, username, 0, NULL, NULL, error, error_size)) {

        goto cleanup;

    }

    if (sqlite3_prepare_v2(database, "DELETE FROM users WHERE username = ?1;", -1, &statement, NULL) != SQLITE_OK) {

        auth_public_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }
    sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {

        auth_public_error(error, error_size, "Unable to delete the selected user account.");
        goto cleanup;

    }
    sqlite3_finalize(statement);
    statement = NULL;

    if (sqlite3_prepare_v2(database,
                           "DELETE FROM auth_rate_limits WHERE scope IN "
                           "('LOGIN:' || ?1, 'LOGIN_TOTP:' || ?1, 'ADMIN:' || ?1, 'ADMIN_TOTP:' || ?1);",
                           -1, &statement, NULL) == SQLITE_OK) {

        sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);

    }
    success = 1;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return success;
}

int AUTH_DB_user_count(void) {
    /*
        Purpose: Counts the user
        Returns: Item count
    */

    sqlite3 *database = NULL;
    char path[PATH_MAX];
    int count = 0;

    if (!auth_open_database(&database, path, sizeof(path))) {

        return 0;

    }
    count = auth_count_users(database);
    sqlite3_close(database);
    return count;
}

int AUTH_TOTP_generate_secret(unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES]) {
    /*
        Purpose: Generates a TOTP secret
        Returns: Success status
    */

    return secret && RAND_bytes(secret, AUTH_PUBLIC_TOTP_SECRET_BYTES) == 1;
}

int AUTH_TOTP_verify(const unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES], const char *code) {
    /*
        Purpose: Verifies the requested data
        Returns: Success status
    */

    return auth_verify_totp(secret, code);
}

int AUTH_TOTP_base32(const unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES], char *output, size_t output_size) {
    /*
        Purpose: Encodes a TOTP secret as Base32
        Returns: Success status
    */

    return auth_base32_encode(secret, AUTH_PUBLIC_TOTP_SECRET_BYTES, output, output_size);
}
