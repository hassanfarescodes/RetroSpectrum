#ifndef RETROSPECTRUM_SERVER_IDENTITY_H
#define RETROSPECTRUM_SERVER_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#define SERVER_IDENTITY_ID_LENGTH 12
#define SERVER_IDENTITY_ID_BUFFER (SERVER_IDENTITY_ID_LENGTH + 1)
#define SERVER_IDENTITY_FINGERPRINT_BUFFER 129
#define SERVER_IDENTITY_PUBLIC_KEY_BYTES 2592
#define SERVER_IDENTITY_PRIVATE_KEY_BYTES 4896
#define SERVER_IDENTITY_SIGNATURE_BYTES 4627
#define SERVER_IDENTITY_SERVER_NAME_MAX 96
#define SERVER_IDENTITY_SERVER_NAME_BUFFER (SERVER_IDENTITY_SERVER_NAME_MAX + 1)

typedef struct Type_Server_Public_Identity {
    char server_name[SERVER_IDENTITY_SERVER_NAME_BUFFER];
    char fingerprint_sha512[SERVER_IDENTITY_FINGERPRINT_BUFFER];
    unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
} Type_Server_Public_Identity;

int SERVER_IDENTITY_start(void);
void SERVER_IDENTITY_stop(void);

const char *SERVER_IDENTITY_get_id(void);
const char *SERVER_IDENTITY_get_fingerprint(void);
const char *SERVER_IDENTITY_get_algorithm(void);
const char *SERVER_IDENTITY_get_local_name(void);
const char *SERVER_IDENTITY_get_trusted_name(void);
const char *SERVER_IDENTITY_get_trusted_fingerprint(void);
const char *SERVER_IDENTITY_get_public_file_path(void);
int SERVER_IDENTITY_trusted_is_local(void);
int SERVER_IDENTITY_get_trusted_host(char *host, size_t host_size);
int SERVER_IDENTITY_get_trusted_public(unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]);
int SERVER_IDENTITY_get_local_public(unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]);
int SERVER_IDENTITY_sign_local(const unsigned char *message, size_t message_size,
                               unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES]);
int SERVER_IDENTITY_verify_trusted(const unsigned char *message, size_t message_size,
                                   const unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES]);

int SERVER_IDENTITY_preview_public_file(const char *path, Type_Server_Public_Identity *identity,
                                        char *message, size_t message_size);
int SERVER_IDENTITY_import_public_file(const char *path, char *message, size_t message_size);

int SERVER_IDENTITY_has_conflict(void);
int SERVER_IDENTITY_validate_target(const char *server_id, char *message, size_t message_size);
int64_t SERVER_IDENTITY_last_verified_at(void);
const char *SERVER_IDENTITY_status(void);

#endif
