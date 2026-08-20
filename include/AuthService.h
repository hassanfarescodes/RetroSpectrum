#ifndef RETROSPECTRUM_AUTH_SERVICE_H
#define RETROSPECTRUM_AUTH_SERVICE_H

#include "AuthTypes.h"
#include <stddef.h>

#define AUTH_SERVER_RESULT_ERROR 0
#define AUTH_SERVER_RESULT_SUCCESS 1
#define AUTH_SERVER_RESULT_TOTP_REQUIRED 2

int AUTH_SERVER_authenticate(const char *username, const char *password, const char *totp, const char *remote_ip,
                             int *is_admin, char *error, size_t error_size);
int AUTH_SERVER_verify_password(const char *username, const char *password, const char *remote_ip, char *error,
                                size_t error_size);
int AUTH_DB_server_list_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count, char *error,
                              size_t error_size);
int AUTH_DB_create_user(const char *username, const char *password, int enable_totp, int is_admin,
                        const unsigned char *totp_secret, const char *acting_admin, char *error,
                        size_t error_size);
int AUTH_DB_reset_password(const char *username, const char *new_password, const char *acting_admin, char *error,
                           size_t error_size);
int AUTH_DB_set_totp(const char *username, const unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES],
                     const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_remove_totp(const char *username, const char *acting_admin, char *error, size_t error_size);
int AUTH_DB_set_role(const char *username, int role, const char *acting_admin, char *error,
                     size_t error_size);
int AUTH_DB_delete_user(const char *username, const char *acting_admin, char *error,
                        size_t error_size);
#endif
