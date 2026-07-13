#ifndef RETROSPECTRUM_AUTH_SERVICE_H
#define RETROSPECTRUM_AUTH_SERVICE_H

#include <stddef.h>
#include "AuthTypes.h"

#define AUTH_SERVER_RESULT_ERROR 0
#define AUTH_SERVER_RESULT_SUCCESS 1
#define AUTH_SERVER_RESULT_TOTP_REQUIRED 2

int AUTH_SERVER_authenticate(const char *username, const char *password, const char *totp,
                             const char *remote_ip, int *is_admin,
                             char *error, size_t error_size);
int AUTH_DB_server_list_users(Type_Auth_User_Summary *users, size_t capacity, size_t *count,
                              char *error, size_t error_size);

#endif
