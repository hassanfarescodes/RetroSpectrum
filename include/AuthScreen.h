#ifndef RETROSPECTRUM_AUTH_SCREEN_H
#define RETROSPECTRUM_AUTH_SCREEN_H

#include "AuthService.h"
#include "AuthTypes.h"
#include <stddef.h>
#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

int AUTH_run(SDL_Window *window, SDL_Renderer *renderer, TTF_Font *font_small, TTF_Font *font_medium);
const char *AUTH_get_server_id(void);
const char *AUTH_get_server_name(void);
const char *AUTH_get_current_username(void);

int AUTH_verify_current_password(const char *password, char *error, size_t error_size);
void AUTH_set_client_only_mode(int client_only);
int AUTH_DB_list_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count, char *error, size_t error_size);
int AUTH_DB_server_list_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count, char *error,
                              size_t error_size);
int AUTH_DB_create_user(const char *username, const char *password, int enable_totp, int is_admin,
                        const unsigned char *totp_secret, const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_reset_password(const char *username, const char *new_password, const char *acting_admin, char *error,
                           size_t error_size);
int AUTH_DB_set_totp(const char *username, const unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES],
                     const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_remove_totp(const char *username, const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_set_role(const char *username, int role, const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_delete_user(const char *username, const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_user_count(void);
int AUTH_TOTP_generate_secret(unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES]);
int AUTH_TOTP_verify(const unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES], const char *code);
int AUTH_TOTP_base32(const unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES], char *output, size_t output_size);

#endif
