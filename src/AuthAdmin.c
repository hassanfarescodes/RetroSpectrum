#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * File:            AuthAdmin.c
 * Author:          Hassan Fares
 *
 * Description:     Administrator-only local account management interface for
 *                  RetroSpectrum. Administrators can create users, reset
 *                  passwords, manage 2FA, and delete accounts. Ordinary login users never
 *                  enter this interface.
 *
 * Language:        C
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include "AuthAdmin.h"
#include "AuthScreen.h"

#include <ctype.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUTH_ADMIN_MAX_USERS 256
#define AUTH_ADMIN_PASSWORD_MAX 127
#define AUTH_ADMIN_STATUS_MAX 384
#define AUTH_ADMIN_CODE_MAX 6
#define AUTH_ADMIN_FIELD_NONE -1
#define AUTH_ADMIN_FIELD_USERNAME 0
#define AUTH_ADMIN_FIELD_PASSWORD 1
#define AUTH_ADMIN_FIELD_CONFIRM 2
#define AUTH_ADMIN_FIELD_CODE 3

typedef enum Type_Auth_Admin_View {
    AUTH_ADMIN_VIEW_USERS = 0,
    AUTH_ADMIN_VIEW_CREATE,
    AUTH_ADMIN_VIEW_CREATE_TOTP,
    AUTH_ADMIN_VIEW_SET_TOTP,
    AUTH_ADMIN_VIEW_REMOVE_TOTP,
    AUTH_ADMIN_VIEW_RESET,
    AUTH_ADMIN_VIEW_DELETE
} Type_Auth_Admin_View;

typedef struct Type_Auth_Admin_State {
    Type_Auth_Admin_View view;
    Type_Auth_User_Summary users[AUTH_ADMIN_MAX_USERS];
    size_t user_count;
    int selected;
    int scroll;
    int active_field;
    int bootstrap_mode;
    int enable_two_factor;
    int acting_role;
    char acting_admin[AUTH_PUBLIC_USERNAME_MAX + 1];
    char username[AUTH_PUBLIC_USERNAME_MAX + 1];
    char password[AUTH_ADMIN_PASSWORD_MAX + 1];
    char confirm[AUTH_ADMIN_PASSWORD_MAX + 1];
    char code[AUTH_ADMIN_CODE_MAX + 1];
    unsigned char totp_secret[AUTH_PUBLIC_TOTP_SECRET_BYTES];
    int totp_secret_valid;
    char status[AUTH_ADMIN_STATUS_MAX];
    SDL_Color status_color;
} Type_Auth_Admin_State;

static const SDL_Color Admin_BG = {0, 0, 0, 255};
static const SDL_Color Admin_PANEL = {0, 12, 5, 250};
static const SDL_Color Admin_FIELD = {0, 20, 8, 255};
static const SDL_Color Admin_BORDER = {0, 150, 60, 255};
static const SDL_Color Admin_ACTIVE = {0, 255, 90, 255};
static const SDL_Color Admin_TEXT = {0, 255, 90, 255};
static const SDL_Color Admin_MUTED = {0, 155, 65, 255};
static const SDL_Color Admin_WARN = {255, 180, 40, 255};
static const SDL_Color Admin_COADMIN = {70, 150, 255, 255};
static const SDL_Color Admin_ERROR = {255, 75, 55, 255};

static const char *admin_role_name(int role) {
    /*
        Purpose: Gets the role name
        Returns: Text pointer
    */

    if (role == AUTH_ROLE_ADMIN) {

        return "Administrator";

    }

    if (role == AUTH_ROLE_CO_ADMIN) {

        return "Co-Administrator";

    }
    return "User";
}

static SDL_Color admin_role_color(int role) {
    /*
        Purpose: Gets the role color
        Returns: Computed color
    */

    if (role == AUTH_ROLE_ADMIN) {

        return Admin_WARN;

    }

    if (role == AUTH_ROLE_CO_ADMIN) {

        return Admin_COADMIN;

    }
    return Admin_TEXT;
}

static int admin_role_is_privileged(int role) {
    /*
        Purpose: Checks whether the role is privileged
        Returns: Success status
    */

    return role == AUTH_ROLE_ADMIN || role == AUTH_ROLE_CO_ADMIN;
}

static void admin_secure_zero(void *memory, size_t size) {
    /*
        Purpose: Clears sensitive administrative memory
        Returns: No value
    */

    if (memory && size > 0) {

        OPENSSL_cleanse(memory, size);

    }
}

static void admin_copy(char *destination, size_t destination_size, const char *source) {
    /*
        Purpose: Copies the requested operation
        Returns: No value
    */

    if (!destination || destination_size == 0) {

        return;

    }
    snprintf(destination, destination_size, "%s", source ? source : "");
}

static int admin_point_in_rect(int x, int y, SDL_Rect rect) {
    /*
        Purpose: Checks whether a point lies inside a rectangle
        Returns: Boolean status
    */

    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

static void admin_fill(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Fills the requested operation
        Returns: No value
    */

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

static void admin_outline(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Outlines the requested operation
        Returns: No value
    */

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

static void admin_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    /*
        Purpose: Draws administrative text
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

static void admin_centered(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Rect rect, SDL_Color color) {
    /*
        Purpose: Draws centered administrative text
        Returns: No value
    */

    int width = 0;
    int height = 0;

    if (!font || !text || TTF_SizeUTF8(font, text, &width, &height) != 0) {

        return;

    }
    admin_text(renderer, font, text, rect.x + (rect.w - width) / 2, rect.y + (rect.h - height) / 2, color);
}

static void admin_button(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, int hovered,
                         SDL_Color color) {
    /*
        Purpose: Draws an administrative button
        Returns: No value
    */

    admin_fill(renderer, rect, Admin_FIELD);
    admin_outline(renderer, rect, hovered ? color : Admin_BORDER);
    admin_centered(renderer, font, label, rect, color);
}

static void admin_mask(const char *input, char *output, size_t output_size) {
    /*
        Purpose: Masks the requested operation
        Returns: No value
    */

    size_t length = input ? strlen(input) : 0;

    if (!output || output_size == 0) {

        return;

    }

    if (length >= output_size) {

        length = output_size - 1;

    }
    memset(output, '*', length);
    output[length] = '\0';
}

static void admin_field(SDL_Renderer *renderer, TTF_Font *font, SDL_Rect rect, const char *label, const char *value,
                        int active, int masked) {
    /*
        Purpose: Gets the requested item field
        Returns: No value
    */

    char display[AUTH_ADMIN_PASSWORD_MAX + 2];
    const char *shown = value ? value : "";

    if (masked) {

        admin_mask(shown, display, sizeof(display));
        shown = display;

    }
    admin_text(renderer, font, label, rect.x, rect.y - 23, Admin_MUTED);
    admin_fill(renderer, rect, Admin_FIELD);
    admin_outline(renderer, rect, active ? Admin_ACTIVE : Admin_BORDER);
    admin_text(renderer, font, shown[0] ? shown : " ", rect.x + 11, rect.y + 11, Admin_TEXT);

    if (active && ((SDL_GetTicks64() / 500U) % 2U) == 0U) {

        int width = 0;
        int height = 0;
        TTF_SizeUTF8(font, shown, &width, &height);
        SDL_SetRenderDrawColor(renderer, Admin_ACTIVE.r, Admin_ACTIVE.g, Admin_ACTIVE.b, Admin_ACTIVE.a);
        SDL_RenderDrawLine(renderer, rect.x + 13 + width, rect.y + 8, rect.x + 13 + width, rect.y + rect.h - 8);

    }
}

static void admin_set_status(Type_Auth_Admin_State *state, SDL_Color color, const char *message) {
    /*
        Purpose: Sets the status
        Returns: No value
    */

    if (!state) {

        return;

    }
    admin_copy(state->status, sizeof(state->status), message);
    state->status_color = color;
}

static void admin_clear_form(Type_Auth_Admin_State *state) {
    /*
        Purpose: Clears the form
        Returns: No value
    */

    if (!state) {

        return;

    }
    memset(state->username, 0, sizeof(state->username));
    admin_secure_zero(state->password, sizeof(state->password));
    admin_secure_zero(state->confirm, sizeof(state->confirm));
    admin_secure_zero(state->code, sizeof(state->code));
    admin_secure_zero(state->totp_secret, sizeof(state->totp_secret));
    state->totp_secret_valid = 0;
    state->enable_two_factor = 0;
    state->active_field = AUTH_ADMIN_FIELD_NONE;
}

static int admin_refresh_users(Type_Auth_Admin_State *state) {
    /*
        Purpose: Refreshes the administrative user list
        Returns: Success status
    */

    char error[256];
    size_t count = 0;

    if (!state || !AUTH_DB_list_users(state->users, AUTH_ADMIN_MAX_USERS, &count, error, sizeof(error))) {

        admin_set_status(state, Admin_ERROR, error[0] ? error : "Unable to load user accounts.");
        return 0;

    }
    state->user_count = count;
    state->acting_role = AUTH_ROLE_USER;
    for (size_t index = 0; index < state->user_count; index++) {

        if (strcmp(state->users[index].username, state->acting_admin) == 0) {

            state->acting_role = state->users[index].role;
            break;

        }
    }

    if (!state->bootstrap_mode && !admin_role_is_privileged(state->acting_role)) {

        admin_set_status(state, Admin_ERROR, "This account no longer has administrator privileges.");
        return 0;

    }

    if (state->user_count == 0) {

        state->selected = -1;

    }

    else if (state->selected < 0 || (size_t)state->selected >= state->user_count) {

        state->selected = 0;

    }
    return 1;
}

static int admin_selected_valid(const Type_Auth_Admin_State *state) {
    /*
        Purpose: Checks whether the selected is valid
        Returns: Boolean status
    */

    return state && state->selected >= 0 && (size_t)state->selected < state->user_count;
}

static int admin_can_modify_selected(const Type_Auth_Admin_State *state) {
    /*
        Purpose: Checks whether the selected user can be modified
        Returns: Success status
    */

    const Type_Auth_User_Summary *target;

    if (!admin_selected_valid(state) || !admin_role_is_privileged(state->acting_role)) {

        return 0;

    }
    target = &state->users[state->selected];
    return target->role != AUTH_ROLE_ADMIN || state->acting_role == AUTH_ROLE_ADMIN;
}

static int admin_can_delete_selected(const Type_Auth_Admin_State *state) {
    /*
        Purpose: Checks whether the selected user can be deleted
        Returns: Success status
    */

    const Type_Auth_User_Summary *target;

    if (!admin_can_modify_selected(state)) {

        return 0;

    }
    target = &state->users[state->selected];
    return target->role != AUTH_ROLE_ADMIN && strcmp(target->username, state->acting_admin) != 0;
}

static int admin_can_change_selected_role(const Type_Auth_Admin_State *state) {
    /*
        Purpose: Checks whether the selected user role can be changed
        Returns: Success status
    */

    const Type_Auth_User_Summary *target;

    if (!admin_selected_valid(state) || !admin_role_is_privileged(state->acting_role)) {

        return 0;

    }
    target = &state->users[state->selected];
    return target->role != AUTH_ROLE_ADMIN && strcmp(target->username, state->acting_admin) != 0;
}

static int admin_username_valid(const char *username) {
    /*
        Purpose: Checks whether the username is valid
        Returns: Boolean status
    */

    size_t length;

    if (!username) {

        return 0;

    }
    length = strlen(username);

    if (length < 3 || length > AUTH_PUBLIC_USERNAME_MAX) {

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

static SDL_Rect admin_main_panel(int width, int height) {
    /*
        Purpose: Processes the main panel
        Returns: Computed rectangle
    */

    int panel_width = width - 80;
    int panel_height = height - 90;

    if (panel_width > 1120) {

        panel_width = 1120;

    }

    if (panel_height > 700) {

        panel_height = 700;

    }

    if (panel_width < 760) {

        panel_width = width - 30;

    }

    if (panel_height < 540) {

        panel_height = height - 30;

    }
    return (SDL_Rect){(width - panel_width) / 2, (height - panel_height) / 2, panel_width, panel_height};
}

static SDL_Rect admin_form_panel(int width, int height) {
    /*
        Purpose: Computes the administrative form panel rectangle
        Returns: Computed rectangle
    */

    int panel_width = 650;
    int panel_height = 590;

    if (panel_width > width - 40) {

        panel_width = width - 40;

    }

    if (panel_height > height - 40) {

        panel_height = height - 40;

    }
    return (SDL_Rect){(width - panel_width) / 2, (height - panel_height) / 2, panel_width, panel_height};
}

static SDL_Rect admin_form_field(SDL_Rect panel, int row) {
    /*
        Purpose: Gets the form field
        Returns: Computed rectangle
    */

    return (SDL_Rect){panel.x + 75, panel.y + 145 + row * 82, panel.w - 150, 46};
}

static void admin_render_users(SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                               Type_Auth_Admin_State *state, int width, int height, int mouse_x, int mouse_y) {
    /*
        Purpose: Renders the users
        Returns: No value
    */

    SDL_Rect panel = admin_main_panel(width, height);
    SDL_Rect list = {panel.x + 26, panel.y + 92, 390, panel.h - 150};
    SDL_Rect details = {list.x + list.w + 24, list.y, panel.x + panel.w - 26 - (list.x + list.w + 24), list.h};
    SDL_Rect create_button = {details.x + 28, details.y + details.h - 264, details.w - 56, 40};
    SDL_Rect role_button = {details.x + 28, details.y + details.h - 212, details.w - 56, 40};
    SDL_Rect reset_button = {details.x + 28, details.y + details.h - 160, (details.w - 68) / 2, 40};
    SDL_Rect delete_button = {reset_button.x + reset_button.w + 12, reset_button.y, reset_button.w, 40};
    SDL_Rect set_totp_button = {details.x + 28, details.y + details.h - 108, (details.w - 68) / 2, 40};
    SDL_Rect remove_totp_button = {set_totp_button.x + set_totp_button.w + 12, set_totp_button.y, set_totp_button.w,
                                   40};
    SDL_Rect back_button = {panel.x + panel.w - 156, panel.y + 22, 126, 38};
    int row_height = 44;
    int visible = list.h / row_height;
    int selected_valid = admin_selected_valid(state);
    int can_modify = admin_can_modify_selected(state);
    int can_delete = admin_can_delete_selected(state);
    int can_change_role = admin_can_change_selected_role(state);
    const char *role_button_label = "ROLE LOCKED";
    SDL_Color role_button_color = Admin_MUTED;

    if (selected_valid) {

        if (state->users[state->selected].role == AUTH_ROLE_USER) {

            role_button_label = "MAKE CO-ADMIN";
            role_button_color = can_change_role ? Admin_COADMIN : Admin_MUTED;

        }

        else if (state->users[state->selected].role == AUTH_ROLE_CO_ADMIN) {

            role_button_label = "SET ROLE TO USER";
            role_button_color = can_change_role ? Admin_COADMIN : Admin_MUTED;

        }

        else {

            role_button_label = "PRIMARY ADMIN LOCKED";

        }

    }

    admin_fill(renderer, panel, Admin_PANEL);
    admin_outline(renderer, panel, Admin_ACTIVE);
    admin_centered(renderer, font_medium, "AUTHENTICATION ADMINISTRATION",
                   (SDL_Rect){panel.x, panel.y + 24, panel.w, 28}, Admin_TEXT);
    admin_button(renderer, font_small, back_button, "BACK", admin_point_in_rect(mouse_x, mouse_y, back_button),
                 Admin_TEXT);

    admin_fill(renderer, list, Admin_FIELD);
    admin_outline(renderer, list, Admin_BORDER);
    admin_fill(renderer, details, Admin_FIELD);
    admin_outline(renderer, details, Admin_BORDER);
    admin_text(renderer, font_small, "USER ACCOUNTS", list.x + 14, list.y - 26, Admin_MUTED);
    admin_text(renderer, font_small, "ACCOUNT DETAILS", details.x + 14, details.y - 26, Admin_MUTED);

    if (state->scroll > (int)state->user_count - visible) {

        state->scroll = (int)state->user_count - visible;

    }

    if (state->scroll < 0) {

        state->scroll = 0;

    }

    for (int row = 0; row < visible; row++) {
        int index = state->scroll + row;
        SDL_Rect row_rect = {list.x + 8, list.y + 8 + row * row_height, list.w - 16, row_height - 4};

        if (index >= (int)state->user_count) {

            break;

        }

        if (index == state->selected) {

            admin_fill(renderer, row_rect, (SDL_Color){0, 52, 22, 255});

        }
        admin_outline(renderer, row_rect,
                      admin_point_in_rect(mouse_x, mouse_y, row_rect) || index == state->selected ? Admin_ACTIVE
                                                                                                  : Admin_BORDER);
        admin_text(renderer, font_small, state->users[index].username, row_rect.x + 12, row_rect.y + 11, Admin_TEXT);

        if (state->users[index].role == AUTH_ROLE_ADMIN) {

            admin_text(renderer, font_small, "ADMIN", row_rect.x + row_rect.w - 70, row_rect.y + 11, Admin_WARN);

        }

        else if (state->users[index].role == AUTH_ROLE_CO_ADMIN) {

            admin_text(renderer, font_small, "CO-ADMIN", row_rect.x + row_rect.w - 98, row_rect.y + 11, Admin_COADMIN);

        }
    }

    if (selected_valid) {

        Type_Auth_User_Summary *user = &state->users[state->selected];
        char created[64] = "Unknown";
        char line[256];
        time_t created_time = (time_t)user->created_at;
        struct tm local_time;

        if (localtime_r(&created_time, &local_time) &&
            strftime(created, sizeof(created), "%Y-%m-%d %I:%M:%S %p", &local_time) > 0) {

            char *time_part = strchr(created, ' ');

            if (time_part && time_part[1] == '0') {

                memmove(time_part + 1, time_part + 2, strlen(time_part + 2) + 1);

            }

        }
        snprintf(line, sizeof(line), "Username: %s", user->username);
        admin_text(renderer, font_small, line, details.x + 30, details.y + 26, Admin_TEXT);
        snprintf(line, sizeof(line), "Role: %s", admin_role_name(user->role));
        admin_text(renderer, font_small, line, details.x + 30, details.y + 56, admin_role_color(user->role));
        snprintf(line, sizeof(line), "Two-factor authentication: %s", user->totp_enabled ? "Enabled" : "Disabled");
        admin_text(renderer, font_small, line, details.x + 30, details.y + 86, Admin_TEXT);
        snprintf(line, sizeof(line), "Created: %s", created);
        admin_text(renderer, font_small, line, details.x + 30, details.y + 116, Admin_MUTED);

    }

    else {

        admin_centered(renderer, font_small, "No accounts exist.", details, Admin_WARN);

    }

    admin_button(renderer, font_small, create_button, "CREATE USER",
                 admin_point_in_rect(mouse_x, mouse_y, create_button), Admin_TEXT);
    admin_button(renderer, font_small, role_button, role_button_label,
                 can_change_role && admin_point_in_rect(mouse_x, mouse_y, role_button), role_button_color);
    admin_button(renderer, font_small, reset_button, "RESET PASSWORD",
                 can_modify && admin_point_in_rect(mouse_x, mouse_y, reset_button),
                 can_modify ? Admin_WARN : Admin_MUTED);
    admin_button(renderer, font_small, delete_button, "DELETE USER",
                 can_delete && admin_point_in_rect(mouse_x, mouse_y, delete_button),
                 can_delete ? Admin_ERROR : Admin_MUTED);
    admin_button(renderer, font_small, set_totp_button, "SET / REPLACE 2FA",
                 can_modify && admin_point_in_rect(mouse_x, mouse_y, set_totp_button),
                 can_modify ? Admin_TEXT : Admin_MUTED);
    admin_button(renderer, font_small, remove_totp_button, "REMOVE 2FA",
                 can_modify && selected_valid && state->users[state->selected].totp_enabled &&
                     admin_point_in_rect(mouse_x, mouse_y, remove_totp_button),
                 (can_modify && selected_valid && state->users[state->selected].totp_enabled) ? Admin_ERROR
                                                                                              : Admin_MUTED);

    if (state->status[0]) {

        admin_centered(renderer, font_small, state->status,
                       (SDL_Rect){panel.x + 24, panel.y + panel.h - 42, panel.w - 48, 24}, state->status_color);

    }
}

static void admin_render_create(SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                                Type_Auth_Admin_State *state, int width, int height, int mouse_x, int mouse_y) {
    /*
        Purpose: Renders the create
        Returns: No value
    */

    SDL_Rect panel = admin_form_panel(width, height);
    SDL_Rect username = admin_form_field(panel, 0);
    SDL_Rect password = admin_form_field(panel, 1);
    SDL_Rect confirm = admin_form_field(panel, 2);
    SDL_Rect checkbox = {panel.x + 75, panel.y + 405, 22, 22};
    SDL_Rect save = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
    SDL_Rect cancel = {panel.x + 75, save.y, 120, 40};

    admin_fill(renderer, panel, Admin_PANEL);
    admin_outline(renderer, panel, Admin_ACTIVE);
    admin_centered(renderer, font_medium, state->bootstrap_mode ? "INITIALIZE ADMINISTRATOR" : "CREATE USER ACCOUNT",
                   (SDL_Rect){panel.x, panel.y + 26, panel.w, 28}, Admin_TEXT);
    admin_centered(renderer, font_small,
                   state->bootstrap_mode ? "The first account is the local administrator."
                                         : "Only an authenticated administrator can create this account.",
                   (SDL_Rect){panel.x + 30, panel.y + 73, panel.w - 60, 24}, Admin_MUTED);

    admin_field(renderer, font_small, username, "Username", state->username,
                state->active_field == AUTH_ADMIN_FIELD_USERNAME, 0);
    admin_field(renderer, font_small, password, "Password", state->password,
                state->active_field == AUTH_ADMIN_FIELD_PASSWORD, 1);
    admin_field(renderer, font_small, confirm, "Confirm Password", state->confirm,
                state->active_field == AUTH_ADMIN_FIELD_CONFIRM, 1);

    admin_fill(renderer, checkbox, state->enable_two_factor ? Admin_ACTIVE : Admin_FIELD);
    admin_outline(renderer, checkbox, Admin_BORDER);
    admin_text(renderer, font_small, "Require authenticator-app 2FA", checkbox.x + 34, checkbox.y + 2, Admin_TEXT);

    admin_button(renderer, font_small, cancel, state->bootstrap_mode ? "EXIT" : "CANCEL",
                 admin_point_in_rect(mouse_x, mouse_y, cancel), Admin_MUTED);
    admin_button(renderer, font_small, save, state->enable_two_factor ? "CONTINUE" : "CREATE",
                 admin_point_in_rect(mouse_x, mouse_y, save), Admin_TEXT);

    if (state->status[0]) {

        admin_centered(renderer, font_small, state->status,
                       (SDL_Rect){panel.x + 30, panel.y + panel.h - 115, panel.w - 60, 24}, state->status_color);

    }
}

static void admin_group_secret(const char *secret, char *grouped, size_t grouped_size) {
    /*
        Purpose: Groups a secret for readable display
        Returns: No value
    */

    size_t output = 0;

    if (!secret || !grouped || grouped_size == 0) {

        return;

    }
    for (size_t i = 0; secret[i] && output + 1 < grouped_size; i++) {

        if (i > 0 && i % 4 == 0 && output + 2 < grouped_size) {

            grouped[output++] = ' ';

        }
        grouped[output++] = secret[i];
    }
    grouped[output] = '\0';
}

static void admin_render_totp(SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                              Type_Auth_Admin_State *state, int width, int height, int mouse_x, int mouse_y) {
    /*
        Purpose: Renders the TOTP
        Returns: No value
    */

    SDL_Rect panel = admin_form_panel(width, height);
    SDL_Rect code = admin_form_field(panel, 3);
    SDL_Rect copy = {panel.x + panel.w - 205, panel.y + 270, 130, 38};
    SDL_Rect create = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
    SDL_Rect cancel = {panel.x + 75, create.y, 120, 40};
    char secret[128];
    char grouped[160];
    char uri[384];

    memset(secret, 0, sizeof(secret));
    memset(grouped, 0, sizeof(grouped));

    if (state->totp_secret_valid) {

        AUTH_TOTP_base32(state->totp_secret, secret, sizeof(secret));
        admin_group_secret(secret, grouped, sizeof(grouped));

    }

    admin_fill(renderer, panel, Admin_PANEL);
    admin_outline(renderer, panel, Admin_ACTIVE);
    admin_centered(renderer, font_medium,
                   state->view == AUTH_ADMIN_VIEW_SET_TOTP ? "SET / REPLACE TWO-FACTOR AUTHENTICATION"
                                                           : "ENROLL TWO-FACTOR AUTHENTICATION",
                   (SDL_Rect){panel.x, panel.y + 26, panel.w, 28}, Admin_TEXT);
    admin_centered(renderer, font_small,
                   state->view == AUTH_ADMIN_VIEW_SET_TOTP
                       ? "Enroll a new SHA-512 authenticator secret for the selected account."
                       : "Add the SHA-512 secret to an authenticator app, then enter its current code.",
                   (SDL_Rect){panel.x + 30, panel.y + 73, panel.w - 60, 24}, Admin_MUTED);
    SDL_Rect secret_box = {panel.x + 40, panel.y + 178, panel.w - 80, 58};

    admin_text(renderer, font_small, "Manual secret", secret_box.x, panel.y + 150, Admin_MUTED);
    admin_fill(renderer, secret_box, Admin_FIELD);
    admin_outline(renderer, secret_box, Admin_BORDER);
    admin_centered(renderer, font_small, grouped,
                   (SDL_Rect){secret_box.x + 8, secret_box.y, secret_box.w - 16, secret_box.h}, Admin_TEXT);

    snprintf(uri, sizeof(uri),
             "otpauth://totp/RetroSpectrum:%s?secret=%s&issuer=RetroSpectrum&algorithm=SHA512&digits=6&period=30",
             state->username, secret);
    (void)uri;

    admin_button(renderer, font_small, copy, "COPY SECRET", admin_point_in_rect(mouse_x, mouse_y, copy), Admin_TEXT);
    admin_field(renderer, font_small, code, "Six-digit authenticator code", state->code,
                state->active_field == AUTH_ADMIN_FIELD_CODE, 0);
    admin_button(renderer, font_small, cancel, "CANCEL", admin_point_in_rect(mouse_x, mouse_y, cancel), Admin_MUTED);
    admin_button(renderer, font_small, create,
                 state->view == AUTH_ADMIN_VIEW_SET_TOTP ? "VERIFY & SAVE" : "VERIFY & CREATE",
                 admin_point_in_rect(mouse_x, mouse_y, create), Admin_TEXT);

    if (state->status[0]) {

        admin_centered(renderer, font_small, state->status,
                       (SDL_Rect){panel.x + 30, panel.y + panel.h - 115, panel.w - 60, 24}, state->status_color);

    }
    admin_secure_zero(secret, sizeof(secret));
    admin_secure_zero(grouped, sizeof(grouped));
}

static void admin_render_reset(SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                               Type_Auth_Admin_State *state, int width, int height, int mouse_x, int mouse_y) {
    /*
        Purpose: Renders the reset
        Returns: No value
    */

    SDL_Rect panel = admin_form_panel(width, height);
    SDL_Rect password = admin_form_field(panel, 0);
    SDL_Rect confirm = admin_form_field(panel, 1);
    SDL_Rect save = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
    SDL_Rect cancel = {panel.x + 75, save.y, 120, 40};
    char title[256];

    snprintf(title, sizeof(title), "RESET PASSWORD: %s", state->username);
    admin_fill(renderer, panel, Admin_PANEL);
    admin_outline(renderer, panel, Admin_ACTIVE);
    admin_centered(renderer, font_medium, title, (SDL_Rect){panel.x, panel.y + 26, panel.w, 28}, Admin_TEXT);
    admin_centered(renderer, font_small, "Resetting a password disables that account's existing 2FA enrollment.",
                   (SDL_Rect){panel.x + 30, panel.y + 78, panel.w - 60, 24}, Admin_WARN);
    admin_field(renderer, font_small, password, "New Password", state->password,
                state->active_field == AUTH_ADMIN_FIELD_PASSWORD, 1);
    admin_field(renderer, font_small, confirm, "Confirm New Password", state->confirm,
                state->active_field == AUTH_ADMIN_FIELD_CONFIRM, 1);
    admin_button(renderer, font_small, cancel, "CANCEL", admin_point_in_rect(mouse_x, mouse_y, cancel), Admin_MUTED);
    admin_button(renderer, font_small, save, "RESET PASSWORD", admin_point_in_rect(mouse_x, mouse_y, save), Admin_WARN);

    if (state->status[0]) {

        admin_centered(renderer, font_small, state->status,
                       (SDL_Rect){panel.x + 30, panel.y + panel.h - 115, panel.w - 60, 24}, state->status_color);

    }
}

static void admin_render_remove_totp(SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                                     Type_Auth_Admin_State *state, int width, int height, int mouse_x, int mouse_y) {
    /*
        Purpose: Renders the remove TOTP
        Returns: No value
    */

    SDL_Rect panel = admin_form_panel(width, height);
    SDL_Rect confirm = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
    SDL_Rect cancel = {panel.x + 75, confirm.y, 120, 40};
    char line[256];

    admin_fill(renderer, panel, Admin_PANEL);
    admin_outline(renderer, panel, Admin_ERROR);
    admin_centered(renderer, font_medium, "REMOVE TWO-FACTOR AUTHENTICATION",
                   (SDL_Rect){panel.x, panel.y + 40, panel.w, 28}, Admin_ERROR);
    snprintf(line, sizeof(line), "Disable 2FA for account: %s", state->username);
    admin_centered(renderer, font_small, line, (SDL_Rect){panel.x + 30, panel.y + 170, panel.w - 60, 28}, Admin_TEXT);
    admin_centered(renderer, font_small, "The account will require only its password until 2FA is enrolled again.",
                   (SDL_Rect){panel.x + 30, panel.y + 218, panel.w - 60, 28}, Admin_WARN);
    admin_button(renderer, font_small, cancel, "CANCEL", admin_point_in_rect(mouse_x, mouse_y, cancel), Admin_MUTED);
    admin_button(renderer, font_small, confirm, "REMOVE 2FA", admin_point_in_rect(mouse_x, mouse_y, confirm),
                 Admin_ERROR);

    if (state->status[0]) {

        admin_centered(renderer, font_small, state->status,
                       (SDL_Rect){panel.x + 30, panel.y + panel.h - 115, panel.w - 60, 24}, state->status_color);

    }
}

static void admin_render_delete(SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                                Type_Auth_Admin_State *state, int width, int height, int mouse_x, int mouse_y) {
    /*
        Purpose: Renders the delete
        Returns: No value
    */

    SDL_Rect panel = admin_form_panel(width, height);
    SDL_Rect confirm = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
    SDL_Rect cancel = {panel.x + 75, confirm.y, 120, 40};
    char line[256];

    admin_fill(renderer, panel, Admin_PANEL);
    admin_outline(renderer, panel, Admin_ERROR);
    admin_centered(renderer, font_medium, "DELETE USER ACCOUNT", (SDL_Rect){panel.x, panel.y + 40, panel.w, 28},
                   Admin_ERROR);
    snprintf(line, sizeof(line), "Permanently delete account: %s", state->username);
    admin_centered(renderer, font_small, line, (SDL_Rect){panel.x + 30, panel.y + 170, panel.w - 60, 28}, Admin_TEXT);
    admin_centered(renderer, font_small, "This action cannot be undone.",
                   (SDL_Rect){panel.x + 30, panel.y + 218, panel.w - 60, 28}, Admin_WARN);
    admin_button(renderer, font_small, cancel, "CANCEL", admin_point_in_rect(mouse_x, mouse_y, cancel), Admin_MUTED);
    admin_button(renderer, font_small, confirm, "DELETE", admin_point_in_rect(mouse_x, mouse_y, confirm), Admin_ERROR);

    if (state->status[0]) {

        admin_centered(renderer, font_small, state->status,
                       (SDL_Rect){panel.x + 30, panel.y + panel.h - 115, panel.w - 60, 24}, state->status_color);

    }
}

static void admin_render(SDL_Window *window, SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                         Type_Auth_Admin_State *state) {
    /*
        Purpose: Renders the requested operation
        Returns: No value
    */

    int width = 0;
    int height = 0;
    int mouse_x = 0;
    int mouse_y = 0;

    SDL_GetWindowSize(window, &width, &height);
    SDL_GetMouseState(&mouse_x, &mouse_y);
    SDL_SetRenderDrawColor(renderer, Admin_BG.r, Admin_BG.g, Admin_BG.b, Admin_BG.a);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0, 42, 16, 120);
    for (int x = 0; x < width; x += 48) {
        SDL_RenderDrawLine(renderer, x, 0, x, height);
    }
    for (int y = 0; y < height; y += 48) {
        SDL_RenderDrawLine(renderer, 0, y, width, y);
    }

    if (state->view == AUTH_ADMIN_VIEW_USERS) {

        admin_render_users(renderer, font_small, font_medium, state, width, height, mouse_x, mouse_y);

    }

    else if (state->view == AUTH_ADMIN_VIEW_CREATE) {

        admin_render_create(renderer, font_small, font_medium, state, width, height, mouse_x, mouse_y);

    }

    else if (state->view == AUTH_ADMIN_VIEW_CREATE_TOTP || state->view == AUTH_ADMIN_VIEW_SET_TOTP) {

        admin_render_totp(renderer, font_small, font_medium, state, width, height, mouse_x, mouse_y);

    }

    else if (state->view == AUTH_ADMIN_VIEW_REMOVE_TOTP) {

        admin_render_remove_totp(renderer, font_small, font_medium, state, width, height, mouse_x, mouse_y);

    }

    else if (state->view == AUTH_ADMIN_VIEW_RESET) {

        admin_render_reset(renderer, font_small, font_medium, state, width, height, mouse_x, mouse_y);

    }

    else {

        admin_render_delete(renderer, font_small, font_medium, state, width, height, mouse_x, mouse_y);

    }
    SDL_RenderPresent(renderer);
}

static void admin_append(char *destination, size_t destination_size, const char *text, int digits_only) {
    /*
        Purpose: Appends the requested operation
        Returns: No value
    */

    size_t length;

    if (!destination || destination_size == 0 || !text) {

        return;

    }
    length = strlen(destination);
    for (size_t i = 0; text[i] && length + 1 < destination_size; i++) {
        unsigned char character = (unsigned char)text[i];

        if (digits_only && !isdigit(character)) {

            continue;

        }

        if (character < 32 || character == 127) {

            continue;

        }
        destination[length++] = (char)character;
    }
    destination[length] = '\0';
}

static void admin_backspace(char *text) {
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

static void admin_handle_text(Type_Auth_Admin_State *state, const char *text) {
    /*
        Purpose: Handles the text
        Returns: No value
    */

    if (!state || !text) {

        return;

    }

    if (state->active_field == AUTH_ADMIN_FIELD_USERNAME) {

        admin_append(state->username, sizeof(state->username), text, 0);

    }

    else if (state->active_field == AUTH_ADMIN_FIELD_PASSWORD) {

        admin_append(state->password, sizeof(state->password), text, 0);

    }

    else if (state->active_field == AUTH_ADMIN_FIELD_CONFIRM) {

        admin_append(state->confirm, sizeof(state->confirm), text, 0);

    }

    else if (state->active_field == AUTH_ADMIN_FIELD_CODE) {

        admin_append(state->code, sizeof(state->code), text, 1);

    }
    state->status[0] = '\0';
}

static void admin_handle_backspace(Type_Auth_Admin_State *state) {
    /*
        Purpose: Handles the backspace
        Returns: No value
    */

    if (!state) {

        return;

    }

    if (state->active_field == AUTH_ADMIN_FIELD_USERNAME) {

        admin_backspace(state->username);

    }

    else if (state->active_field == AUTH_ADMIN_FIELD_PASSWORD) {

        admin_backspace(state->password);

    }

    else if (state->active_field == AUTH_ADMIN_FIELD_CONFIRM) {

        admin_backspace(state->confirm);

    }

    else if (state->active_field == AUTH_ADMIN_FIELD_CODE) {

        admin_backspace(state->code);

    }
    state->status[0] = '\0';
}

static void admin_cycle(Type_Auth_Admin_State *state) {
    /*
        Purpose: Cycles the requested operation
        Returns: No value
    */

    if (!state) {

        return;

    }

    if (state->view == AUTH_ADMIN_VIEW_CREATE) {

        if (state->active_field == AUTH_ADMIN_FIELD_USERNAME) {

            state->active_field = AUTH_ADMIN_FIELD_PASSWORD;

        }

        else if (state->active_field == AUTH_ADMIN_FIELD_PASSWORD) {

            state->active_field = AUTH_ADMIN_FIELD_CONFIRM;

        }

        else {

            state->active_field = AUTH_ADMIN_FIELD_USERNAME;

        }

    }

    else if (state->view == AUTH_ADMIN_VIEW_RESET) {

        state->active_field =
            state->active_field == AUTH_ADMIN_FIELD_PASSWORD ? AUTH_ADMIN_FIELD_CONFIRM : AUTH_ADMIN_FIELD_PASSWORD;

    }

    else if (state->view == AUTH_ADMIN_VIEW_CREATE_TOTP || state->view == AUTH_ADMIN_VIEW_SET_TOTP) {

        state->active_field = AUTH_ADMIN_FIELD_CODE;

    }
}

static int admin_submit_create(Type_Auth_Admin_State *state) {
    /*
        Purpose: Submits the create
        Returns: Success status
    */

    char error[256];
    int is_admin;

    if (!admin_username_valid(state->username)) {

        admin_set_status(state, Admin_ERROR, "Username: 3-63 letters, numbers, periods, hyphens, or underscores.");
        return 0;

    }

    if (strlen(state->password) < 10) {

        admin_set_status(state, Admin_ERROR, "Password must contain at least 10 characters.");
        return 0;

    }

    if (strcmp(state->password, state->confirm) != 0) {

        admin_set_status(state, Admin_ERROR, "Password confirmation does not match.");
        return 0;

    }

    if (state->enable_two_factor && !state->totp_secret_valid) {

        if (!AUTH_TOTP_generate_secret(state->totp_secret)) {

            admin_set_status(state, Admin_ERROR, "Unable to generate a secure 2FA secret.");
            return 0;

        }
        state->totp_secret_valid = 1;
        state->view = AUTH_ADMIN_VIEW_CREATE_TOTP;
        state->active_field = AUTH_ADMIN_FIELD_CODE;
        state->status[0] = '\0';
        return 0;

    }

    is_admin = state->bootstrap_mode ? 1 : 0;

    if (!AUTH_DB_create_user(state->username, state->password, state->enable_two_factor, is_admin,
                             state->enable_two_factor ? state->totp_secret : NULL,
                             state->bootstrap_mode ? NULL : state->acting_admin, error, sizeof(error))) {

        admin_set_status(state, Admin_ERROR, error);
        return 0;

    }

    admin_clear_form(state);

    if (state->bootstrap_mode) {

        return 1;

    }
    state->view = AUTH_ADMIN_VIEW_USERS;
    admin_refresh_users(state);
    admin_set_status(state, Admin_TEXT, "User account created.");
    return 0;
}

static int admin_submit_totp(Type_Auth_Admin_State *state) {
    /*
        Purpose: Submits the TOTP
        Returns: Success status
    */

    if (!state->totp_secret_valid || !AUTH_TOTP_verify(state->totp_secret, state->code)) {

        admin_secure_zero(state->code, sizeof(state->code));
        admin_set_status(state, Admin_ERROR, "Invalid or expired authenticator code.");
        return 0;

    }
    return admin_submit_create(state);
}

static void admin_submit_set_totp(Type_Auth_Admin_State *state) {
    /*
        Purpose: Submits the set TOTP
        Returns: No value
    */

    char error[256] = {0};

    if (!state || !state->totp_secret_valid || !AUTH_TOTP_verify(state->totp_secret, state->code)) {

        if (state) {

            admin_secure_zero(state->code, sizeof(state->code));
            admin_set_status(state, Admin_ERROR, "Invalid or expired authenticator code.");

        }
        return;

    }

    if (!AUTH_DB_set_totp(state->username, state->totp_secret, state->acting_admin, error, sizeof(error))) {

        admin_set_status(state, Admin_ERROR, error);
        return;

    }

    admin_clear_form(state);
    state->view = AUTH_ADMIN_VIEW_USERS;
    admin_refresh_users(state);
    admin_set_status(state, Admin_TEXT, "Two-factor authentication was enrolled for the selected account.");
}

static void admin_submit_remove_totp(Type_Auth_Admin_State *state) {
    /*
        Purpose: Submits the remove TOTP
        Returns: No value
    */

    char error[256] = {0};

    if (!state || !AUTH_DB_remove_totp(state->username, state->acting_admin, error, sizeof(error))) {

        if (state) {

            admin_set_status(state, Admin_ERROR, error[0] ? error : "Unable to remove 2FA.");

        }
        return;

    }

    admin_clear_form(state);
    state->view = AUTH_ADMIN_VIEW_USERS;
    admin_refresh_users(state);
    admin_set_status(state, Admin_WARN, "Two-factor authentication was removed from the selected account.");
}

static void admin_submit_reset(Type_Auth_Admin_State *state) {
    /*
        Purpose: Submits the reset
        Returns: No value
    */

    char error[256];

    if (strlen(state->password) < 10) {

        admin_set_status(state, Admin_ERROR, "Password must contain at least 10 characters.");
        return;

    }

    if (strcmp(state->password, state->confirm) != 0) {

        admin_set_status(state, Admin_ERROR, "Password confirmation does not match.");
        return;

    }

    if (!AUTH_DB_reset_password(state->username, state->password, state->acting_admin, error, sizeof(error))) {

        admin_set_status(state, Admin_ERROR, error);
        return;

    }
    admin_secure_zero(state->password, sizeof(state->password));
    admin_secure_zero(state->confirm, sizeof(state->confirm));
    state->view = AUTH_ADMIN_VIEW_USERS;
    admin_refresh_users(state);
    admin_set_status(state, Admin_WARN,
                     "Password reset. Server-wrapped 2FA was preserved; legacy password-wrapped 2FA may be disabled.");
}

static void admin_submit_role(Type_Auth_Admin_State *state) {
    /*
        Purpose: Submits the role
        Returns: No value
    */

    char error[256] = {0};
    Type_Auth_User_Summary *selected;
    int new_role;

    if (!admin_can_change_selected_role(state)) {

        admin_set_status(state, Admin_ERROR, "The selected account role cannot be changed.");
        return;

    }
    selected = &state->users[state->selected];
    new_role = selected->role == AUTH_ROLE_CO_ADMIN ? AUTH_ROLE_USER : AUTH_ROLE_CO_ADMIN;

    if (!AUTH_DB_set_role(selected->username, new_role, state->acting_admin, error, sizeof(error))) {

        admin_set_status(state, Admin_ERROR, error[0] ? error : "Unable to update the account role.");
        return;

    }

    admin_refresh_users(state);
    admin_set_status(state, new_role == AUTH_ROLE_CO_ADMIN ? Admin_COADMIN : Admin_TEXT,
                     new_role == AUTH_ROLE_CO_ADMIN ? "The selected account is now a co-administrator."
                                                    : "The selected account role was set back to user.");
}

static void admin_submit_delete(Type_Auth_Admin_State *state) {
    /*
        Purpose: Submits the delete
        Returns: No value
    */

    char error[256];

    if (!AUTH_DB_delete_user(state->username, state->acting_admin, error, sizeof(error))) {

        admin_set_status(state, Admin_ERROR, error);
        return;

    }
    state->view = AUTH_ADMIN_VIEW_USERS;
    state->selected = 0;
    admin_refresh_users(state);
    admin_set_status(state, Admin_TEXT, "User account deleted.");
}

static int admin_handle_mouse(Type_Auth_Admin_State *state, int mouse_x, int mouse_y, int width, int height,
                              int *running) {
    /*
        Purpose: Handles the mouse
        Returns: Handling status
    */

    if (state->view == AUTH_ADMIN_VIEW_USERS) {

        SDL_Rect panel = admin_main_panel(width, height);
        SDL_Rect list = {panel.x + 26, panel.y + 92, 390, panel.h - 150};
        SDL_Rect details = {list.x + list.w + 24, list.y, panel.x + panel.w - 26 - (list.x + list.w + 24), list.h};
        SDL_Rect create_button = {details.x + 28, details.y + details.h - 264, details.w - 56, 40};
        SDL_Rect role_button = {details.x + 28, details.y + details.h - 212, details.w - 56, 40};
        SDL_Rect reset_button = {details.x + 28, details.y + details.h - 160, (details.w - 68) / 2, 40};
        SDL_Rect delete_button = {reset_button.x + reset_button.w + 12, reset_button.y, reset_button.w, 40};
        SDL_Rect set_totp_button = {details.x + 28, details.y + details.h - 108, (details.w - 68) / 2, 40};
        SDL_Rect remove_totp_button = {set_totp_button.x + set_totp_button.w + 12, set_totp_button.y, set_totp_button.w,
                                       40};
        SDL_Rect back_button = {panel.x + panel.w - 156, panel.y + 22, 126, 38};
        int row_height = 44;

        if (admin_point_in_rect(mouse_x, mouse_y, back_button)) {

            *running = 0;
            return 0;

        }

        if (admin_point_in_rect(mouse_x, mouse_y, list)) {

            int row = (mouse_y - list.y - 8) / row_height;
            int index = state->scroll + row;

            if (index >= 0 && (size_t)index < state->user_count) {

                state->selected = index;
                state->status[0] = '\0';

            }
            return 0;

        }

        if (admin_point_in_rect(mouse_x, mouse_y, create_button)) {

            admin_clear_form(state);
            state->view = AUTH_ADMIN_VIEW_CREATE;
            state->active_field = AUTH_ADMIN_FIELD_USERNAME;
            return 0;

        }

        if (admin_point_in_rect(mouse_x, mouse_y, role_button)) {

            admin_submit_role(state);
            return 0;

        }

        if (admin_can_modify_selected(state) && admin_point_in_rect(mouse_x, mouse_y, reset_button)) {

            admin_clear_form(state);
            admin_copy(state->username, sizeof(state->username), state->users[state->selected].username);
            state->view = AUTH_ADMIN_VIEW_RESET;
            state->active_field = AUTH_ADMIN_FIELD_PASSWORD;
            return 0;

        }

        if (admin_can_delete_selected(state) && admin_point_in_rect(mouse_x, mouse_y, delete_button)) {

            admin_clear_form(state);
            admin_copy(state->username, sizeof(state->username), state->users[state->selected].username);
            state->view = AUTH_ADMIN_VIEW_DELETE;
            return 0;

        }

        if (admin_can_modify_selected(state) && admin_point_in_rect(mouse_x, mouse_y, set_totp_button)) {

            admin_clear_form(state);
            admin_copy(state->username, sizeof(state->username), state->users[state->selected].username);

            if (!AUTH_TOTP_generate_secret(state->totp_secret)) {

                admin_set_status(state, Admin_ERROR, "Unable to generate a secure 2FA secret.");
                return 0;

            }
            state->totp_secret_valid = 1;
            state->view = AUTH_ADMIN_VIEW_SET_TOTP;
            state->active_field = AUTH_ADMIN_FIELD_CODE;
            return 0;

        }

        if (admin_can_modify_selected(state) && admin_point_in_rect(mouse_x, mouse_y, remove_totp_button)) {

            if (!state->users[state->selected].totp_enabled) {

                admin_set_status(state, Admin_MUTED, "The selected account does not have 2FA enabled.");
                return 0;

            }
            admin_clear_form(state);
            admin_copy(state->username, sizeof(state->username), state->users[state->selected].username);
            state->view = AUTH_ADMIN_VIEW_REMOVE_TOTP;
            return 0;

        }

    }

    else if (state->view == AUTH_ADMIN_VIEW_CREATE) {

        SDL_Rect panel = admin_form_panel(width, height);
        SDL_Rect save = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
        SDL_Rect cancel = {panel.x + 75, save.y, 120, 40};
        SDL_Rect checkbox = {panel.x + 75, panel.y + 405, 22, 22};

        for (int row = 0; row < 3; row++) {

            if (admin_point_in_rect(mouse_x, mouse_y, admin_form_field(panel, row))) {

                state->active_field = row;
                return 0;

            }
        }

        if (admin_point_in_rect(mouse_x, mouse_y, checkbox)) {

            state->enable_two_factor = !state->enable_two_factor;
            return 0;

        }

        if (admin_point_in_rect(mouse_x, mouse_y, cancel)) {

            if (state->bootstrap_mode) {

                *running = 0;

            }

            else {

                admin_clear_form(state);
                state->view = AUTH_ADMIN_VIEW_USERS;

            }
            return 0;

        }

        if (admin_point_in_rect(mouse_x, mouse_y, save)) {

            return admin_submit_create(state);

        }

    }

    else if (state->view == AUTH_ADMIN_VIEW_CREATE_TOTP || state->view == AUTH_ADMIN_VIEW_SET_TOTP) {

        SDL_Rect panel = admin_form_panel(width, height);
        SDL_Rect code = admin_form_field(panel, 3);
        SDL_Rect copy = {panel.x + panel.w - 205, panel.y + 270, 130, 38};
        SDL_Rect create = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
        SDL_Rect cancel = {panel.x + 75, create.y, 120, 40};

        if (admin_point_in_rect(mouse_x, mouse_y, code)) {

            state->active_field = AUTH_ADMIN_FIELD_CODE;

        }

        else if (admin_point_in_rect(mouse_x, mouse_y, copy)) {

            char secret[96];

            if (AUTH_TOTP_base32(state->totp_secret, secret, sizeof(secret)) && SDL_SetClipboardText(secret) == 0) {

                admin_set_status(state, Admin_TEXT, "2FA secret copied to clipboard.");

            }

            else {

                admin_set_status(state, Admin_ERROR, "Unable to copy the 2FA secret.");

            }
            admin_secure_zero(secret, sizeof(secret));

        }

        else if (admin_point_in_rect(mouse_x, mouse_y, cancel)) {

            admin_secure_zero(state->totp_secret, sizeof(state->totp_secret));
            state->totp_secret_valid = 0;

            if (state->view == AUTH_ADMIN_VIEW_SET_TOTP) {

                admin_clear_form(state);
                state->view = AUTH_ADMIN_VIEW_USERS;

            }

            else {

                state->view = AUTH_ADMIN_VIEW_CREATE;
                state->active_field = AUTH_ADMIN_FIELD_USERNAME;

            }

        }

        else if (admin_point_in_rect(mouse_x, mouse_y, create)) {

            if (state->view == AUTH_ADMIN_VIEW_SET_TOTP) {

                admin_submit_set_totp(state);
                return 0;

            }
            return admin_submit_totp(state);

        }

    }

    else if (state->view == AUTH_ADMIN_VIEW_REMOVE_TOTP) {

        SDL_Rect panel = admin_form_panel(width, height);
        SDL_Rect confirm = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
        SDL_Rect cancel = {panel.x + 75, confirm.y, 120, 40};

        if (admin_point_in_rect(mouse_x, mouse_y, cancel)) {

            admin_clear_form(state);
            state->view = AUTH_ADMIN_VIEW_USERS;

        }

        else if (admin_point_in_rect(mouse_x, mouse_y, confirm)) {

            admin_submit_remove_totp(state);

        }

    }

    else if (state->view == AUTH_ADMIN_VIEW_RESET) {

        SDL_Rect panel = admin_form_panel(width, height);
        SDL_Rect save = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
        SDL_Rect cancel = {panel.x + 75, save.y, 120, 40};

        if (admin_point_in_rect(mouse_x, mouse_y, admin_form_field(panel, 0))) {

            state->active_field = AUTH_ADMIN_FIELD_PASSWORD;

        }

        else if (admin_point_in_rect(mouse_x, mouse_y, admin_form_field(panel, 1))) {

            state->active_field = AUTH_ADMIN_FIELD_CONFIRM;

        }

        else if (admin_point_in_rect(mouse_x, mouse_y, cancel)) {

            admin_clear_form(state);
            state->view = AUTH_ADMIN_VIEW_USERS;

        }

        else if (admin_point_in_rect(mouse_x, mouse_y, save)) {

            admin_submit_reset(state);

        }

    }

    else {

        SDL_Rect panel = admin_form_panel(width, height);
        SDL_Rect confirm = {panel.x + panel.w - 225, panel.y + panel.h - 70, 150, 40};
        SDL_Rect cancel = {panel.x + 75, confirm.y, 120, 40};

        if (admin_point_in_rect(mouse_x, mouse_y, cancel)) {

            state->view = AUTH_ADMIN_VIEW_USERS;

        }

        else if (admin_point_in_rect(mouse_x, mouse_y, confirm)) {

            admin_submit_delete(state);

        }

    }
    return 0;
}

int AUTH_ADMIN_run(SDL_Window *window, SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium,
                   const char *authenticated_admin, int bootstrap_mode) {
    /*
        Purpose: Runs the requested operation
        Returns: Success status
    */

    Type_Auth_Admin_State state;
    int running = 1;
    int bootstrap_created = 0;

    if (!window || !renderer || !font_small || !font_medium) {

        return 0;

    }

    memset(&state, 0, sizeof(state));
    state.bootstrap_mode = bootstrap_mode != 0;
    state.selected = 0;
    state.status_color = Admin_MUTED;
    admin_copy(state.acting_admin, sizeof(state.acting_admin), authenticated_admin);

    if (state.bootstrap_mode) {

        if (AUTH_DB_user_count() != 0) {

            return 0;

        }
        state.view = AUTH_ADMIN_VIEW_CREATE;
        state.active_field = AUTH_ADMIN_FIELD_USERNAME;

    }

    else {

        state.view = AUTH_ADMIN_VIEW_USERS;

        if (!authenticated_admin || authenticated_admin[0] == '\0' || !admin_refresh_users(&state)) {

            return 0;

        }

    }

    SDL_StartTextInput();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_QUIT) {

                running = 0;

            }

            else if (event.type == SDL_TEXTINPUT) {

                admin_handle_text(&state, event.text.text);

            }

            else if (event.type == SDL_MOUSEWHEEL && state.view == AUTH_ADMIN_VIEW_USERS) {

                state.scroll -= event.wheel.y;

            }

            else if (event.type == SDL_KEYDOWN) {

                SDL_Keycode key = event.key.keysym.sym;

                if (key == SDLK_ESCAPE) {

                    if (state.bootstrap_mode || state.view == AUTH_ADMIN_VIEW_USERS) {

                        running = 0;

                    }

                    else {

                        admin_clear_form(&state);
                        state.view = AUTH_ADMIN_VIEW_USERS;

                    }

                }

                else if (key == SDLK_TAB) {

                    admin_cycle(&state);

                }

                else if (key == SDLK_BACKSPACE) {

                    admin_handle_backspace(&state);

                }

                else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {

                    if (state.view == AUTH_ADMIN_VIEW_CREATE) {

                        bootstrap_created = admin_submit_create(&state);

                        if (bootstrap_created) {

                            running = 0;

                        }

                    }

                    else if (state.view == AUTH_ADMIN_VIEW_CREATE_TOTP) {

                        bootstrap_created = admin_submit_totp(&state);

                        if (bootstrap_created) {

                            running = 0;

                        }

                    }

                    else if (state.view == AUTH_ADMIN_VIEW_SET_TOTP) {

                        admin_submit_set_totp(&state);

                    }

                    else if (state.view == AUTH_ADMIN_VIEW_REMOVE_TOTP) {

                        admin_submit_remove_totp(&state);

                    }

                    else if (state.view == AUTH_ADMIN_VIEW_RESET) {

                        admin_submit_reset(&state);

                    }

                }

            }

            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {

                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                bootstrap_created = admin_handle_mouse(&state, event.button.x, event.button.y, width, height, &running);

                if (bootstrap_created) {

                    running = 0;

                }

            }
        }

        if (running) {

            admin_render(window, renderer, font_small, font_medium, &state);
            SDL_Delay(8);

        }
    }

    admin_clear_form(&state);

    if (state.bootstrap_mode) {

        return bootstrap_created && AUTH_DB_user_count() > 0;

    }
    return 1;
}
