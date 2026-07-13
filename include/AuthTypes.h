#ifndef RETROSPECTRUM_AUTH_TYPES_H
#define RETROSPECTRUM_AUTH_TYPES_H

#include <stdint.h>

#define AUTH_PUBLIC_USERNAME_MAX 63
#define AUTH_PUBLIC_TOTP_SECRET_BYTES 32

#define AUTH_ROLE_USER 0
#define AUTH_ROLE_CO_ADMIN 1
#define AUTH_ROLE_ADMIN 2

typedef struct Type_Auth_User_Summary {
    char username[AUTH_PUBLIC_USERNAME_MAX + 1];
    int role;
    int is_admin;
    int totp_enabled;
    int64_t created_at;
} Type_Auth_User_Summary;

#endif
