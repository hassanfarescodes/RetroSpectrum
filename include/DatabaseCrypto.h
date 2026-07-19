#ifndef RETROSPECTRUM_DATABASE_CRYPTO_H
#define RETROSPECTRUM_DATABASE_CRYPTO_H

#include <sqlite3.h>
#include <stddef.h>

int DATABASE_CRYPTO_set_key_path(const char *path, char *error, size_t error_size);
int DATABASE_CRYPTO_is_unlocked(void);

int DATABASE_CRYPTO_open_auth(sqlite3 **database, char *path, size_t path_size, char *error, size_t error_size);
int DATABASE_CRYPTO_open_data(sqlite3 **database, char *path, size_t path_size, char *error, size_t error_size);
const char *DATABASE_CRYPTO_key_path(void);

#endif
