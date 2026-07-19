#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================================
 * File:            ServerIdentity.c
 * Author:          Hassan Fares
 *
 * Description:     Persistent post-quantum ML-DSA-87 server identity, public
 *                  identity-file export/import, and signed LAN announcements.
 *                  SHA-512 fingerprints bind trusted public-key files to the
 *                  complete ML-DSA-87 public key.
 *
 * Language:        C
 * Standard:        C11
 * Target:          Linux x86-64
 * ============================================================================
 */

#include "ServerIdentity.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if OPENSSL_VERSION_NUMBER < 0x30500000L
#error "ServerIdentity.c requires OpenSSL 3.5.0 or newer for ML-DSA-87 support."
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SERVER_IDENTITY_PORT 47741
#define SERVER_IDENTITY_MAGIC "RSIDPQ02"
#define SERVER_IDENTITY_MAGIC_BYTES 8
#define SERVER_IDENTITY_PACKET_VERSION 2U
#define SERVER_IDENTITY_PACKET_TYPE_ANNOUNCE 1U
#define SERVER_IDENTITY_PACKET_TYPE_QUERY 2U
#define SERVER_IDENTITY_ALGORITHM "ML-DSA-87"
#define SERVER_IDENTITY_PRIVATE_KEY_BYTES 4896
#define SERVER_IDENTITY_SIGNATURE_BYTES 4627
#define SERVER_IDENTITY_NONCE_BYTES 32
#define SERVER_IDENTITY_SHA512_BYTES SHA512_DIGEST_LENGTH
#define SERVER_IDENTITY_PUBLIC_OFFSET 22
#define SERVER_IDENTITY_NONCE_OFFSET (SERVER_IDENTITY_PUBLIC_OFFSET + SERVER_IDENTITY_PUBLIC_KEY_BYTES)
#define SERVER_IDENTITY_TIMESTAMP_OFFSET (SERVER_IDENTITY_NONCE_OFFSET + SERVER_IDENTITY_NONCE_BYTES)
#define SERVER_IDENTITY_SIGNED_BYTES (SERVER_IDENTITY_TIMESTAMP_OFFSET + 8)
#define SERVER_IDENTITY_SIGNATURE_OFFSET SERVER_IDENTITY_SIGNED_BYTES
#define SERVER_IDENTITY_PACKET_BYTES (SERVER_IDENTITY_SIGNED_BYTES + SERVER_IDENTITY_SIGNATURE_BYTES)
#define SERVER_IDENTITY_KEY_MAGIC "RSPQK002"
#define SERVER_IDENTITY_KEY_HEADER_BYTES 16
#define SERVER_IDENTITY_KEY_PRIVATE_OFFSET SERVER_IDENTITY_KEY_HEADER_BYTES
#define SERVER_IDENTITY_KEY_PUBLIC_OFFSET (SERVER_IDENTITY_KEY_PRIVATE_OFFSET + SERVER_IDENTITY_PRIVATE_KEY_BYTES)
#define SERVER_IDENTITY_KEY_DIGEST_OFFSET (SERVER_IDENTITY_KEY_PUBLIC_OFFSET + SERVER_IDENTITY_PUBLIC_KEY_BYTES)
#define SERVER_IDENTITY_KEY_FILE_BYTES (SERVER_IDENTITY_KEY_DIGEST_OFFSET + SERVER_IDENTITY_SHA512_BYTES)
#define SERVER_IDENTITY_ANNOUNCE_INTERVAL 3
#define SERVER_IDENTITY_MAX_CLOCK_SKEW 45
#define SERVER_IDENTITY_PUBLIC_FILE_MAGIC "RSPUB001"
#define SERVER_IDENTITY_PUBLIC_FILE_VERSION 1U
#define SERVER_IDENTITY_PUBLIC_FILE_HEADER_BYTES 16
#define SERVER_IDENTITY_PUBLIC_FILE_NAME_OFFSET SERVER_IDENTITY_PUBLIC_FILE_HEADER_BYTES
#define SERVER_IDENTITY_PUBLIC_FILE_NAME_BYTES SERVER_IDENTITY_SERVER_NAME_BUFFER
#define SERVER_IDENTITY_PUBLIC_FILE_KEY_OFFSET                                                                         \
    (SERVER_IDENTITY_PUBLIC_FILE_NAME_OFFSET + SERVER_IDENTITY_PUBLIC_FILE_NAME_BYTES)
#define SERVER_IDENTITY_PUBLIC_FILE_DIGEST_OFFSET                                                                      \
    (SERVER_IDENTITY_PUBLIC_FILE_KEY_OFFSET + SERVER_IDENTITY_PUBLIC_KEY_BYTES)
#define SERVER_IDENTITY_PUBLIC_FILE_BYTES (SERVER_IDENTITY_PUBLIC_FILE_DIGEST_OFFSET + SERVER_IDENTITY_SHA512_BYTES)

static pthread_t Global_Server_Identity_Thread;
static pthread_mutex_t Global_Server_Identity_Lock = PTHREAD_MUTEX_INITIALIZER;
static int Global_Server_Identity_Thread_Started = 0;
static int Global_Server_Identity_Running = 0;
static int Global_Server_Identity_Socket = -1;
static int Global_Server_Identity_Conflict = 0;
static char Global_Server_Identity_Id[SERVER_IDENTITY_ID_BUFFER] = "";
static char Global_Server_Identity_Fingerprint[SERVER_IDENTITY_FINGERPRINT_BUFFER] = "";
static char Global_Server_Identity_Status[256] = "Server identity has not been initialized.";
static unsigned char Global_Server_Identity_Public[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
static unsigned char Global_Server_Identity_Private[SERVER_IDENTITY_PRIVATE_KEY_BYTES];
static char Global_Server_Identity_Local_Name[SERVER_IDENTITY_SERVER_NAME_BUFFER] = "RetroSpectrum Server";
static char Global_Server_Identity_Trusted_Name[SERVER_IDENTITY_SERVER_NAME_BUFFER] = "RetroSpectrum Server";
static char Global_Server_Identity_Trusted_Fingerprint[SERVER_IDENTITY_FINGERPRINT_BUFFER] = "";
static unsigned char Global_Server_Identity_Trusted_Public[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
static int Global_Server_Identity_Trusted_Is_Local = 1;
static char Global_Server_Identity_Trusted_Host[INET_ADDRSTRLEN] = "127.0.0.1";
static int64_t Global_Server_Identity_Last_Verified_At = 0;
static char Global_Server_Identity_Public_File_Path[PATH_MAX] = "";

static int server_identity_write_all(int descriptor, const unsigned char *data, size_t size);

static void server_identity_secure_zero(void *memory, size_t size) {
    /*
        Purpose: Zeros the secure
        Returns: No value
    */

    if (memory && size > 0) {

        OPENSSL_cleanse(memory, size);

    }
}

static void server_identity_set_status(const char *status) {
    /*
        Purpose: Sets the status
        Returns: No value
    */

    pthread_mutex_lock(&Global_Server_Identity_Lock);
    snprintf(Global_Server_Identity_Status, sizeof(Global_Server_Identity_Status), "%s", status ? status : "");
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
}

static int server_identity_ensure_directory(const char *path) {
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

static int server_identity_key_path(char *path, size_t path_size) {
    /*
        Purpose: Builds the key path
        Returns: Success status
    */

    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char root[PATH_MAX];
    char directory[PATH_MAX];

    if (!path || path_size == 0) {

        return 0;

    }

    if (xdg_config && xdg_config[0] != '\0') {

        if (snprintf(root, sizeof(root), "%s", xdg_config) >= (int)sizeof(root)) {

            return 0;

        }

    }

    else if (home && home[0] != '\0') {

        if (snprintf(root, sizeof(root), "%s/.config", home) >= (int)sizeof(root)) {

            return 0;

        }

    }

    else if (snprintf(root, sizeof(root), ".") >= (int)sizeof(root)) {

        return 0;

    }

    if (!server_identity_ensure_directory(root)) {

        return 0;

    }

    if (snprintf(directory, sizeof(directory), "%s/retrospectrum", root) >= (int)sizeof(directory)) {

        return 0;

    }

    if (!server_identity_ensure_directory(directory)) {

        return 0;

    }

    if (chmod(directory, 0700) != 0 && errno != EPERM) {

        return 0;

    }
    return snprintf(path, path_size, "%s/server_identity_mldsa87.key", directory) < (int)path_size;
}

static int server_identity_named_path(char *path, size_t path_size, const char *filename) {
    /*
        Purpose: Builds the named path
        Returns: Success status
    */

    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char root[PATH_MAX];
    char directory[PATH_MAX];

    if (!path || path_size == 0 || !filename || filename[0] == '\0') {

        return 0;

    }

    if (xdg_config && xdg_config[0] != '\0') {

        if (snprintf(root, sizeof(root), "%s", xdg_config) >= (int)sizeof(root)) {

            return 0;

        }

    }

    else if (home && home[0] != '\0') {

        if (snprintf(root, sizeof(root), "%s/.config", home) >= (int)sizeof(root)) {

            return 0;

        }

    }

    else if (snprintf(root, sizeof(root), ".") >= (int)sizeof(root)) {

        return 0;

    }

    if (!server_identity_ensure_directory(root)) {

        return 0;

    }

    if (snprintf(directory, sizeof(directory), "%s/retrospectrum", root) >= (int)sizeof(directory) ||
        !server_identity_ensure_directory(directory)) {

        return 0;

    }

    if (chmod(directory, 0700) != 0 && errno != EPERM) {

        return 0;

    }
    return snprintf(path, path_size, "%s/%s", directory, filename) < (int)path_size;
}

static int server_identity_name_valid(const char *name) {
    /*
        Purpose: Checks whether the name is valid
        Returns: Boolean status
    */

    size_t length;

    if (!name) {

        return 0;

    }
    length = strlen(name);

    if (length == 0 || length > SERVER_IDENTITY_SERVER_NAME_MAX) {

        return 0;

    }
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)name[i];

        if (character < 0x20U || character > 0x7eU) {

            return 0;

        }
    }
    return 1;
}

static void server_identity_default_name(char name[SERVER_IDENTITY_SERVER_NAME_BUFFER]) {
    /*
        Purpose: Gets the default name
        Returns: No value
    */

    char hostname[64];

    memset(hostname, 0, sizeof(hostname));

    if (gethostname(hostname, sizeof(hostname) - 1) == 0 && hostname[0] != '\0') {

        for (size_t i = 0; hostname[i] != '\0'; i++) {
            unsigned char character = (unsigned char)hostname[i];

            if (character < 0x20U || character > 0x7eU) {

                hostname[i] = '-';

            }
        }
        snprintf(name, SERVER_IDENTITY_SERVER_NAME_BUFFER, "RetroSpectrum - %.72s", hostname);

    }

    else {

        snprintf(name, SERVER_IDENTITY_SERVER_NAME_BUFFER, "RetroSpectrum Server");

    }
}

static int server_identity_write_name_file(const char *path, const char *name) {
    /*
        Purpose: Writes the name file
        Returns: Success status
    */

    char line[SERVER_IDENTITY_SERVER_NAME_BUFFER + 2];
    int descriptor;
    int length;
    mode_t previous_mask;

    if (!path || !server_identity_name_valid(name)) {

        return 0;

    }
    length = snprintf(line, sizeof(line), "%s\n", name);

    if (length <= 0 || length >= (int)sizeof(line)) {

        return 0;

    }
    previous_mask = umask(0077);
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    umask(previous_mask);

    if (descriptor < 0) {

        return 0;

    }

    if (!server_identity_write_all(descriptor, (const unsigned char *)line, (size_t)length) || fsync(descriptor) != 0 ||
        close(descriptor) != 0) {

        close(descriptor);
        return 0;

    }
    chmod(path, 0600);
    return 1;
}

static int server_identity_load_or_create_name(char name[SERVER_IDENTITY_SERVER_NAME_BUFFER]) {
    /*
        Purpose: Loads or creates the name
        Returns: Success status
    */

    char path[PATH_MAX];
    char buffer[SERVER_IDENTITY_SERVER_NAME_BUFFER + 2];
    int descriptor;
    ssize_t received;

    if (!name || !server_identity_named_path(path, sizeof(path), "server_name.txt")) {

        return 0;

    }
    descriptor = open(path, O_RDONLY | O_NOFOLLOW);

    if (descriptor >= 0) {

        memset(buffer, 0, sizeof(buffer));
        received = read(descriptor, buffer, sizeof(buffer) - 1);
        close(descriptor);

        if (received > 0) {

            while (received > 0 && (buffer[received - 1] == '\n' || buffer[received - 1] == '\r' ||
                                    buffer[received - 1] == ' ' || buffer[received - 1] == '\t')) {
                buffer[--received] = '\0';
            }

            if (server_identity_name_valid(buffer)) {

                snprintf(name, SERVER_IDENTITY_SERVER_NAME_BUFFER, "%.96s", buffer);
                chmod(path, 0600);
                return 1;

            }

        }
        return 0;

    }

    if (errno != ENOENT) {

        return 0;

    }
    server_identity_default_name(name);
    return server_identity_write_name_file(path, name);
}

static int server_identity_generate_keypair(unsigned char private_key[SERVER_IDENTITY_PRIVATE_KEY_BYTES],
                                            unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Generates an ML-DSA server identity key pair
        Returns: Success status
    */

    EVP_PKEY *key = NULL;
    size_t private_size = SERVER_IDENTITY_PRIVATE_KEY_BYTES;
    size_t public_size = SERVER_IDENTITY_PUBLIC_KEY_BYTES;
    int success = 0;

    key = EVP_PKEY_Q_keygen(NULL, NULL, SERVER_IDENTITY_ALGORITHM);

    if (!key || EVP_PKEY_get_raw_private_key(key, private_key, &private_size) != 1 ||
        EVP_PKEY_get_raw_public_key(key, public_key, &public_size) != 1 ||
        private_size != SERVER_IDENTITY_PRIVATE_KEY_BYTES || public_size != SERVER_IDENTITY_PUBLIC_KEY_BYTES) {

        goto cleanup;

    }
    success = 1;

cleanup:
    EVP_PKEY_free(key);
    return success;
}

static int server_identity_sha512_parts(const unsigned char *part1, size_t part1_size, const unsigned char *part2,
                                        size_t part2_size, const unsigned char *part3, size_t part3_size,
                                        unsigned char digest[SERVER_IDENTITY_SHA512_BYTES]) {
    /*
        Purpose: Calculates a SHA-512 digest over multiple parts
        Returns: Success status
    */

    EVP_MD_CTX *context = NULL;
    unsigned int digest_size = 0;
    int success = 0;

    context = EVP_MD_CTX_new();

    if (!context || EVP_DigestInit_ex(context, EVP_sha512(), NULL) != 1) {

        goto cleanup;

    }

    if (part1_size > 0 && (!part1 || EVP_DigestUpdate(context, part1, part1_size) != 1)) {

        goto cleanup;

    }

    if (part2_size > 0 && (!part2 || EVP_DigestUpdate(context, part2, part2_size) != 1)) {

        goto cleanup;

    }

    if (part3_size > 0 && (!part3 || EVP_DigestUpdate(context, part3, part3_size) != 1)) {

        goto cleanup;

    }

    if (EVP_DigestFinal_ex(context, digest, &digest_size) != 1 || digest_size != SERVER_IDENTITY_SHA512_BYTES) {

        goto cleanup;

    }
    success = 1;

cleanup:
    EVP_MD_CTX_free(context);
    return success;
}

static int server_identity_key_file_digest(const unsigned char *file_data,
                                           unsigned char digest[SERVER_IDENTITY_SHA512_BYTES]) {
    /*
        Purpose: Calculates the key file digest
        Returns: Success status
    */

    static const unsigned char domain[] = "RetroSpectrum ML-DSA-87 identity key file v2";
    return file_data && server_identity_sha512_parts(domain, sizeof(domain) - 1, file_data,
                                                     SERVER_IDENTITY_KEY_DIGEST_OFFSET, NULL, 0, digest);
}

static int server_identity_write_all(int descriptor, const unsigned char *data, size_t size) {
    /*
        Purpose: Writes all requested data data
        Returns: Success status
    */

    size_t offset = 0;

    while (offset < size) {
        ssize_t written = write(descriptor, data + offset, size - offset);

        if (written < 0 && errno == EINTR) {

            continue;

        }

        if (written <= 0) {

            return 0;

        }
        offset += (size_t)written;
    }
    return 1;
}

static int server_identity_read_all(int descriptor, unsigned char *data, size_t size) {
    /*
        Purpose: Reads all requested bytes
        Returns: Success status
    */

    size_t offset = 0;

    while (offset < size) {
        ssize_t received = read(descriptor, data + offset, size - offset);

        if (received < 0 && errno == EINTR) {

            continue;

        }

        if (received <= 0) {

            return 0;

        }
        offset += (size_t)received;
    }
    return 1;
}

static int server_identity_write_key_file(const char *path,
                                          const unsigned char private_key[SERVER_IDENTITY_PRIVATE_KEY_BYTES],
                                          const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Writes the key file
        Returns: Success status
    */

    unsigned char *file_data = NULL;
    unsigned char digest[SERVER_IDENTITY_SHA512_BYTES];
    int descriptor = -1;
    int success = 0;
    mode_t previous_mask;

    if (!path || !private_key || !public_key) {

        return 0;

    }

    file_data = OPENSSL_zalloc(SERVER_IDENTITY_KEY_FILE_BYTES);

    if (!file_data) {

        return 0;

    }
    memcpy(file_data, SERVER_IDENTITY_KEY_MAGIC, 8);
    file_data[8] = SERVER_IDENTITY_PACKET_VERSION;
    file_data[9] = 87U;
    memcpy(file_data + SERVER_IDENTITY_KEY_PRIVATE_OFFSET, private_key, SERVER_IDENTITY_PRIVATE_KEY_BYTES);
    memcpy(file_data + SERVER_IDENTITY_KEY_PUBLIC_OFFSET, public_key, SERVER_IDENTITY_PUBLIC_KEY_BYTES);

    if (!server_identity_key_file_digest(file_data, digest)) {

        goto cleanup;

    }
    memcpy(file_data + SERVER_IDENTITY_KEY_DIGEST_OFFSET, digest, sizeof(digest));

    previous_mask = umask(0077);
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    umask(previous_mask);

    if (descriptor < 0) {

        goto cleanup;

    }

    if (!server_identity_write_all(descriptor, file_data, SERVER_IDENTITY_KEY_FILE_BYTES) || fsync(descriptor) != 0 ||
        close(descriptor) != 0) {

        descriptor = -1;
        unlink(path);
        goto cleanup;

    }
    descriptor = -1;
    chmod(path, 0600);
    success = 1;

cleanup:

    if (descriptor >= 0) {

        close(descriptor);

    }
    server_identity_secure_zero(digest, sizeof(digest));

    if (file_data) {

        server_identity_secure_zero(file_data, SERVER_IDENTITY_KEY_FILE_BYTES);
        OPENSSL_free(file_data);

    }
    return success;
}

static int server_identity_read_key_file(const char *path, unsigned char private_key[SERVER_IDENTITY_PRIVATE_KEY_BYTES],
                                         unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Reads the key file
        Returns: Success status
    */

    unsigned char *file_data = NULL;
    unsigned char expected_digest[SERVER_IDENTITY_SHA512_BYTES];
    int descriptor = -1;
    int success = 0;
    struct stat st;

    if (!path || !private_key || !public_key) {

        return 0;

    }

    descriptor = open(path, O_RDONLY | O_NOFOLLOW);

    if (descriptor < 0 || fstat(descriptor, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size != SERVER_IDENTITY_KEY_FILE_BYTES) {

        goto cleanup;

    }
    file_data = OPENSSL_malloc(SERVER_IDENTITY_KEY_FILE_BYTES);

    if (!file_data || !server_identity_read_all(descriptor, file_data, SERVER_IDENTITY_KEY_FILE_BYTES)) {

        goto cleanup;

    }

    if (memcmp(file_data, SERVER_IDENTITY_KEY_MAGIC, 8) != 0 || file_data[8] != SERVER_IDENTITY_PACKET_VERSION ||
        file_data[9] != 87U || !server_identity_key_file_digest(file_data, expected_digest) ||
        CRYPTO_memcmp(expected_digest, file_data + SERVER_IDENTITY_KEY_DIGEST_OFFSET, SERVER_IDENTITY_SHA512_BYTES) !=
            0) {

        goto cleanup;

    }

    memcpy(private_key, file_data + SERVER_IDENTITY_KEY_PRIVATE_OFFSET, SERVER_IDENTITY_PRIVATE_KEY_BYTES);
    memcpy(public_key, file_data + SERVER_IDENTITY_KEY_PUBLIC_OFFSET, SERVER_IDENTITY_PUBLIC_KEY_BYTES);
    success = 1;

cleanup:

    if (descriptor >= 0) {

        close(descriptor);

    }
    server_identity_secure_zero(expected_digest, sizeof(expected_digest));

    if (file_data) {

        server_identity_secure_zero(file_data, SERVER_IDENTITY_KEY_FILE_BYTES);
        OPENSSL_free(file_data);

    }
    return success;
}

static EVP_PKEY *server_identity_private_pkey(const unsigned char private_key[SERVER_IDENTITY_PRIVATE_KEY_BYTES]) {
    /*
        Purpose: Loads the private server identity key
        Returns: Result pointer
    */

    return EVP_PKEY_new_raw_private_key_ex(NULL, SERVER_IDENTITY_ALGORITHM, NULL, private_key,
                                           SERVER_IDENTITY_PRIVATE_KEY_BYTES);
}

static EVP_PKEY *server_identity_public_pkey(const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Loads the public server identity key
        Returns: Result pointer
    */

    return EVP_PKEY_new_raw_public_key_ex(NULL, SERVER_IDENTITY_ALGORITHM, NULL, public_key,
                                          SERVER_IDENTITY_PUBLIC_KEY_BYTES);
}

static int server_identity_sign_with_private(const unsigned char private_key[SERVER_IDENTITY_PRIVATE_KEY_BYTES],
                                             const unsigned char *message, size_t message_size,
                                             unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES]) {
    /*
        Purpose: Signs data with a private identity key
        Returns: Success status
    */

    EVP_PKEY *key = NULL;
    EVP_MD_CTX *context = NULL;
    size_t signature_size = SERVER_IDENTITY_SIGNATURE_BYTES;
    int success = 0;

    key = server_identity_private_pkey(private_key);
    context = EVP_MD_CTX_new();

    if (!key || !context || EVP_DigestSignInit_ex(context, NULL, NULL, NULL, NULL, key, NULL) != 1 ||
        EVP_DigestSign(context, signature, &signature_size, message, message_size) != 1 ||
        signature_size != SERVER_IDENTITY_SIGNATURE_BYTES) {

        goto cleanup;

    }
    success = 1;

cleanup:
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return success;
}

static int server_identity_verify_signature(const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES],
                                            const unsigned char *message, size_t message_size,
                                            const unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES]) {
    /*
        Purpose: Verifies the signature
        Returns: Success status
    */

    EVP_PKEY *key = NULL;
    EVP_MD_CTX *context = NULL;
    int valid = 0;

    key = server_identity_public_pkey(public_key);
    context = EVP_MD_CTX_new();

    if (key && context && EVP_DigestVerifyInit_ex(context, NULL, NULL, NULL, NULL, key, NULL) == 1 &&
        EVP_DigestVerify(context, signature, SERVER_IDENTITY_SIGNATURE_BYTES, message, message_size) == 1) {

        valid = 1;

    }
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return valid;
}

static int server_identity_verify_keypair(const unsigned char private_key[SERVER_IDENTITY_PRIVATE_KEY_BYTES],
                                          const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Verifies the key pair
        Returns: Success status
    */

    static const unsigned char test_message[] = "RetroSpectrum ML-DSA-87 keypair verification v2";
    unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES];
    int valid;

    memset(signature, 0, sizeof(signature));
    valid = server_identity_sign_with_private(private_key, test_message, sizeof(test_message) - 1, signature) &&
            server_identity_verify_signature(public_key, test_message, sizeof(test_message) - 1, signature);
    server_identity_secure_zero(signature, sizeof(signature));
    return valid;
}

static int server_identity_load_or_create_key(void) {
    /*
        Purpose: Loads or creates the key
        Returns: Success status
    */

    char path[PATH_MAX];

    if (!server_identity_key_path(path, sizeof(path))) {

        return 0;

    }

    if (server_identity_read_key_file(path, Global_Server_Identity_Private, Global_Server_Identity_Public) &&
        server_identity_verify_keypair(Global_Server_Identity_Private, Global_Server_Identity_Public)) {

        chmod(path, 0600);
        return 1;

    }

    unlink(path);

    if (!server_identity_generate_keypair(Global_Server_Identity_Private, Global_Server_Identity_Public)) {

        goto failure;

    }

    if (server_identity_write_key_file(path, Global_Server_Identity_Private, Global_Server_Identity_Public)) {

        return 1;

    }

    server_identity_secure_zero(Global_Server_Identity_Private, sizeof(Global_Server_Identity_Private));
    server_identity_secure_zero(Global_Server_Identity_Public, sizeof(Global_Server_Identity_Public));

    if (server_identity_read_key_file(path, Global_Server_Identity_Private, Global_Server_Identity_Public) &&
        server_identity_verify_keypair(Global_Server_Identity_Private, Global_Server_Identity_Public)) {

        return 1;

    }

failure:
    server_identity_secure_zero(Global_Server_Identity_Private, sizeof(Global_Server_Identity_Private));
    server_identity_secure_zero(Global_Server_Identity_Public, sizeof(Global_Server_Identity_Public));
    return 0;
}

static int server_identity_hash_public_key(const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES],
                                           unsigned char digest[SERVER_IDENTITY_SHA512_BYTES]) {
    /*
        Purpose: Hashes the public key
        Returns: Success status
    */

    static const unsigned char domain[] = "RetroSpectrum ML-DSA-87 public identity v2";
    return public_key && server_identity_sha512_parts(domain, sizeof(domain) - 1, public_key,
                                                      SERVER_IDENTITY_PUBLIC_KEY_BYTES, NULL, 0, digest);
}

static int server_identity_id_from_public_key(const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES],
                                              char server_id[SERVER_IDENTITY_ID_BUFFER]) {
    /*
        Purpose: Gets the ID from the public key
        Returns: Success status
    */

    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static const unsigned char domain[] = "RetroSpectrum 12-character server label v2";
    unsigned char seed[SERVER_IDENTITY_SHA512_BYTES];
    unsigned char digest[SERVER_IDENTITY_SHA512_BYTES];
    unsigned char counter_bytes[4];
    uint32_t counter = 0;
    int output_index = 0;

    if (!public_key || !server_id || !server_identity_hash_public_key(public_key, seed)) {

        return 0;

    }

    while (output_index < SERVER_IDENTITY_ID_LENGTH) {
        counter_bytes[0] = (unsigned char)(counter >> 24);
        counter_bytes[1] = (unsigned char)(counter >> 16);
        counter_bytes[2] = (unsigned char)(counter >> 8);
        counter_bytes[3] = (unsigned char)counter;

        if (!server_identity_sha512_parts(domain, sizeof(domain) - 1, seed, sizeof(seed), counter_bytes,
                                          sizeof(counter_bytes), digest)) {

            server_identity_secure_zero(seed, sizeof(seed));
            return 0;

        }
        counter++;

        for (size_t i = 0; i < sizeof(digest) && output_index < SERVER_IDENTITY_ID_LENGTH; i++) {

            if (digest[i] >= 252U) {

                continue;

            }
            server_id[output_index++] = alphabet[digest[i] % 36U];
        }
    }
    server_id[SERVER_IDENTITY_ID_LENGTH] = '\0';
    server_identity_secure_zero(seed, sizeof(seed));
    server_identity_secure_zero(digest, sizeof(digest));
    return 1;
}

static void
server_identity_fingerprint_from_public_key(const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES],
                                            char fingerprint[SERVER_IDENTITY_FINGERPRINT_BUFFER]) {
    /*
        Purpose: Calculates a public key fingerprint
        Returns: No value
    */

    static const char hex[] = "0123456789ABCDEF";
    unsigned char digest[SERVER_IDENTITY_SHA512_BYTES];

    if (!server_identity_hash_public_key(public_key, digest)) {

        fingerprint[0] = '\0';
        return;

    }
    for (size_t i = 0; i < sizeof(digest); i++) {
        fingerprint[i * 2] = hex[digest[i] >> 4];
        fingerprint[i * 2 + 1] = hex[digest[i] & 0x0fU];
    }
    fingerprint[SERVER_IDENTITY_FINGERPRINT_BUFFER - 1] = '\0';
    server_identity_secure_zero(digest, sizeof(digest));
}

static int server_identity_public_file_digest(const unsigned char *file_data,
                                              unsigned char digest[SERVER_IDENTITY_SHA512_BYTES]) {
    /*
        Purpose: Calculates the public file digest
        Returns: Success status
    */

    static const unsigned char domain[] = "RetroSpectrum ML-DSA-87 public identity file v1";
    return file_data && server_identity_sha512_parts(domain, sizeof(domain) - 1, file_data,
                                                     SERVER_IDENTITY_PUBLIC_FILE_DIGEST_OFFSET, NULL, 0, digest);
}

static void server_identity_write_u16(unsigned char *output, uint16_t value) {
    /*
        Purpose: Writes the 16-bit unsigned
        Returns: No value
    */

    output[0] = (unsigned char)(value >> 8);
    output[1] = (unsigned char)value;
}

static uint16_t server_identity_read_u16(const unsigned char *input) {
    /*
        Purpose: Reads the 16-bit unsigned
        Returns: Success status
    */

    return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static int server_identity_encode_public_file(const char *server_name,
                                              const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES],
                                              unsigned char file_data[SERVER_IDENTITY_PUBLIC_FILE_BYTES]) {
    /*
        Purpose: Encodes the public file
        Returns: Success status
    */

    unsigned char digest[SERVER_IDENTITY_SHA512_BYTES];
    size_t name_length;

    if (!server_identity_name_valid(server_name) || !public_key || !file_data) {

        return 0;

    }
    name_length = strlen(server_name);
    memset(file_data, 0, SERVER_IDENTITY_PUBLIC_FILE_BYTES);
    memcpy(file_data, SERVER_IDENTITY_PUBLIC_FILE_MAGIC, 8);
    file_data[8] = SERVER_IDENTITY_PUBLIC_FILE_VERSION;
    file_data[9] = 87U;
    server_identity_write_u16(file_data + 10, (uint16_t)name_length);
    memcpy(file_data + SERVER_IDENTITY_PUBLIC_FILE_NAME_OFFSET, server_name, name_length);
    memcpy(file_data + SERVER_IDENTITY_PUBLIC_FILE_KEY_OFFSET, public_key, SERVER_IDENTITY_PUBLIC_KEY_BYTES);

    if (!server_identity_public_file_digest(file_data, digest)) {

        return 0;

    }
    memcpy(file_data + SERVER_IDENTITY_PUBLIC_FILE_DIGEST_OFFSET, digest, sizeof(digest));
    server_identity_secure_zero(digest, sizeof(digest));
    return 1;
}

static int server_identity_decode_public_file(const unsigned char file_data[SERVER_IDENTITY_PUBLIC_FILE_BYTES],
                                              Type_Server_Public_Identity *identity) {
    /*
        Purpose: Decodes the public file
        Returns: Success status
    */

    unsigned char expected_digest[SERVER_IDENTITY_SHA512_BYTES];
    uint16_t name_length;

    if (!file_data || !identity || memcmp(file_data, SERVER_IDENTITY_PUBLIC_FILE_MAGIC, 8) != 0 ||
        file_data[8] != SERVER_IDENTITY_PUBLIC_FILE_VERSION || file_data[9] != 87U) {

        return 0;

    }
    name_length = server_identity_read_u16(file_data + 10);

    if (name_length == 0 || name_length > SERVER_IDENTITY_SERVER_NAME_MAX ||
        file_data[SERVER_IDENTITY_PUBLIC_FILE_NAME_OFFSET + name_length] != 0 ||
        !server_identity_public_file_digest(file_data, expected_digest) ||
        CRYPTO_memcmp(expected_digest, file_data + SERVER_IDENTITY_PUBLIC_FILE_DIGEST_OFFSET,
                      SERVER_IDENTITY_SHA512_BYTES) != 0) {

        server_identity_secure_zero(expected_digest, sizeof(expected_digest));
        return 0;

    }

    memset(identity, 0, sizeof(*identity));
    memcpy(identity->server_name, file_data + SERVER_IDENTITY_PUBLIC_FILE_NAME_OFFSET, name_length);
    identity->server_name[name_length] = '\0';

    if (!server_identity_name_valid(identity->server_name)) {

        server_identity_secure_zero(expected_digest, sizeof(expected_digest));
        return 0;

    }
    memcpy(identity->public_key, file_data + SERVER_IDENTITY_PUBLIC_FILE_KEY_OFFSET, SERVER_IDENTITY_PUBLIC_KEY_BYTES);
    server_identity_fingerprint_from_public_key(identity->public_key, identity->fingerprint_sha512);
    server_identity_secure_zero(expected_digest, sizeof(expected_digest));
    return identity->fingerprint_sha512[0] != '\0';
}

static int server_identity_read_public_file(const char *path, Type_Server_Public_Identity *identity) {
    /*
        Purpose: Reads the public file
        Returns: Success status
    */

    unsigned char *file_data = NULL;
    int descriptor = -1;
    int success = 0;
    struct stat st;

    if (!path || !identity) {

        return 0;

    }
    descriptor = open(path, O_RDONLY | O_NOFOLLOW);

    if (descriptor < 0 || fstat(descriptor, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size != SERVER_IDENTITY_PUBLIC_FILE_BYTES) {

        goto cleanup;

    }
    file_data = OPENSSL_malloc(SERVER_IDENTITY_PUBLIC_FILE_BYTES);

    if (!file_data || !server_identity_read_all(descriptor, file_data, SERVER_IDENTITY_PUBLIC_FILE_BYTES) ||
        !server_identity_decode_public_file(file_data, identity)) {

        goto cleanup;

    }
    success = 1;

cleanup:

    if (descriptor >= 0) {

        close(descriptor);

    }

    if (file_data) {

        server_identity_secure_zero(file_data, SERVER_IDENTITY_PUBLIC_FILE_BYTES);
        OPENSSL_free(file_data);

    }
    return success;
}

static int server_identity_write_public_file(const char *path, const char *server_name,
                                             const unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Writes the public file
        Returns: Success status
    */

    unsigned char *file_data = NULL;
    char temporary_path[PATH_MAX];
    int descriptor = -1;
    int success = 0;
    mode_t previous_mask;

    if (!path || !server_name || !public_key ||
        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp.%ld", path, (long)getpid()) >=
            (int)sizeof(temporary_path)) {

        return 0;

    }
    file_data = OPENSSL_malloc(SERVER_IDENTITY_PUBLIC_FILE_BYTES);

    if (!file_data || !server_identity_encode_public_file(server_name, public_key, file_data)) {

        goto cleanup;

    }
    previous_mask = umask(0077);
    descriptor = open(temporary_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    umask(previous_mask);

    if (descriptor < 0 || !server_identity_write_all(descriptor, file_data, SERVER_IDENTITY_PUBLIC_FILE_BYTES) ||
        fsync(descriptor) != 0 || close(descriptor) != 0) {

        descriptor = -1;
        unlink(temporary_path);
        goto cleanup;

    }
    descriptor = -1;

    if (rename(temporary_path, path) != 0) {

        unlink(temporary_path);
        goto cleanup;

    }
    chmod(path, 0600);
    success = 1;

cleanup:

    if (descriptor >= 0) {

        close(descriptor);
        unlink(temporary_path);

    }

    if (file_data) {

        server_identity_secure_zero(file_data, SERVER_IDENTITY_PUBLIC_FILE_BYTES);
        OPENSSL_free(file_data);

    }
    return success;
}

static void server_identity_set_trusted(const Type_Server_Public_Identity *identity) {
    /*
        Purpose: Sets the trusted
        Returns: No value
    */

    if (!identity) {

        return;

    }
    pthread_mutex_lock(&Global_Server_Identity_Lock);
    snprintf(Global_Server_Identity_Trusted_Name, sizeof(Global_Server_Identity_Trusted_Name), "%s",
             identity->server_name);
    snprintf(Global_Server_Identity_Trusted_Fingerprint, sizeof(Global_Server_Identity_Trusted_Fingerprint), "%s",
             identity->fingerprint_sha512);
    memcpy(Global_Server_Identity_Trusted_Public, identity->public_key, SERVER_IDENTITY_PUBLIC_KEY_BYTES);
    Global_Server_Identity_Trusted_Is_Local =
        CRYPTO_memcmp(Global_Server_Identity_Trusted_Public, Global_Server_Identity_Public,
                      SERVER_IDENTITY_PUBLIC_KEY_BYTES) == 0;
    snprintf(Global_Server_Identity_Trusted_Host, sizeof(Global_Server_Identity_Trusted_Host), "%s",
             Global_Server_Identity_Trusted_Is_Local ? "127.0.0.1" : "");
    Global_Server_Identity_Last_Verified_At = Global_Server_Identity_Trusted_Is_Local ? (int64_t)time(NULL) : 0;
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
}

static int server_identity_initialize_public_files(void) {
    /*
        Purpose: Initializes the public files
        Returns: Success status
    */

    char trusted_path[PATH_MAX];
    Type_Server_Public_Identity identity;
    struct stat st;

    if (!server_identity_load_or_create_name(Global_Server_Identity_Local_Name)) {

        return 0;

    }

    if (!server_identity_named_path(Global_Server_Identity_Public_File_Path,
                                    sizeof(Global_Server_Identity_Public_File_Path), "server_identity_mldsa87.rspub") ||
        !server_identity_write_public_file(Global_Server_Identity_Public_File_Path, Global_Server_Identity_Local_Name,
                                           Global_Server_Identity_Public) ||
        !server_identity_named_path(trusted_path, sizeof(trusted_path), "trusted_server.rspub")) {

        return 0;

    }

    if (stat(trusted_path, &st) == 0) {

        if (!server_identity_read_public_file(trusted_path, &identity)) {

            server_identity_set_status(
                "The trusted server public-key file is invalid; refusing to replace it automatically.");
            return 0;

        }

        if (CRYPTO_memcmp(identity.public_key, Global_Server_Identity_Public, SERVER_IDENTITY_PUBLIC_KEY_BYTES) == 0) {

            snprintf(identity.server_name, sizeof(identity.server_name), "%s", Global_Server_Identity_Local_Name);
            server_identity_fingerprint_from_public_key(identity.public_key, identity.fingerprint_sha512);
            (void)server_identity_write_public_file(trusted_path, identity.server_name, identity.public_key);

        }
        server_identity_set_trusted(&identity);
        server_identity_secure_zero(&identity, sizeof(identity));
        return 1;

    }

    if (errno != ENOENT) {

        return 0;

    }

    memset(&identity, 0, sizeof(identity));
    snprintf(identity.server_name, sizeof(identity.server_name), "%s", Global_Server_Identity_Local_Name);
    memcpy(identity.public_key, Global_Server_Identity_Public, SERVER_IDENTITY_PUBLIC_KEY_BYTES);
    server_identity_fingerprint_from_public_key(identity.public_key, identity.fingerprint_sha512);

    if (!server_identity_write_public_file(trusted_path, identity.server_name, identity.public_key)) {

        server_identity_secure_zero(&identity, sizeof(identity));
        return 0;

    }
    server_identity_set_trusted(&identity);
    server_identity_secure_zero(&identity, sizeof(identity));
    return 1;
}

static void server_identity_write_u64(unsigned char *output, uint64_t value) {
    /*
        Purpose: Writes the 64-bit unsigned
        Returns: No value
    */

    for (int i = 7; i >= 0; i--) {
        output[i] = (unsigned char)(value & 0xffU);
        value >>= 8;
    }
}

static uint64_t server_identity_read_u64(const unsigned char *input) {
    /*
        Purpose: Reads the 64-bit unsigned
        Returns: Success status
    */

    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value = (value << 8) | input[i];
    }
    return value;
}

static int server_identity_build_packet(unsigned char packet[SERVER_IDENTITY_PACKET_BYTES], unsigned char type) {
    /*
        Purpose: Builds the packet
        Returns: Success status
    */

    memset(packet, 0, SERVER_IDENTITY_PACKET_BYTES);
    memcpy(packet, SERVER_IDENTITY_MAGIC, SERVER_IDENTITY_MAGIC_BYTES);
    packet[8] = SERVER_IDENTITY_PACKET_VERSION;
    packet[9] = type;
    memcpy(packet + 10, Global_Server_Identity_Id, SERVER_IDENTITY_ID_LENGTH);
    memcpy(packet + SERVER_IDENTITY_PUBLIC_OFFSET, Global_Server_Identity_Public, SERVER_IDENTITY_PUBLIC_KEY_BYTES);

    if (RAND_bytes(packet + SERVER_IDENTITY_NONCE_OFFSET, SERVER_IDENTITY_NONCE_BYTES) != 1) {

        return 0;

    }
    server_identity_write_u64(packet + SERVER_IDENTITY_TIMESTAMP_OFFSET, (uint64_t)time(NULL));
    return server_identity_sign_with_private(Global_Server_Identity_Private, packet, SERVER_IDENTITY_SIGNED_BYTES,
                                             packet + SERVER_IDENTITY_SIGNATURE_OFFSET);
}

static int server_identity_packet_valid(const unsigned char packet[SERVER_IDENTITY_PACKET_BYTES],
                                        char packet_id[SERVER_IDENTITY_ID_BUFFER],
                                        unsigned char packet_public[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Checks whether the packet is valid
        Returns: Boolean status
    */

    char derived_id[SERVER_IDENTITY_ID_BUFFER];
    uint64_t timestamp;
    time_t now = time(NULL);

    if (memcmp(packet, SERVER_IDENTITY_MAGIC, SERVER_IDENTITY_MAGIC_BYTES) != 0 ||
        packet[8] != SERVER_IDENTITY_PACKET_VERSION ||
        (packet[9] != SERVER_IDENTITY_PACKET_TYPE_ANNOUNCE && packet[9] != SERVER_IDENTITY_PACKET_TYPE_QUERY)) {

        return 0;

    }

    memcpy(packet_id, packet + 10, SERVER_IDENTITY_ID_LENGTH);
    packet_id[SERVER_IDENTITY_ID_LENGTH] = '\0';
    memcpy(packet_public, packet + SERVER_IDENTITY_PUBLIC_OFFSET, SERVER_IDENTITY_PUBLIC_KEY_BYTES);

    if (!server_identity_id_from_public_key(packet_public, derived_id) || strcmp(packet_id, derived_id) != 0) {

        return 0;

    }

    timestamp = server_identity_read_u64(packet + SERVER_IDENTITY_TIMESTAMP_OFFSET);

    if (timestamp > (uint64_t)now + SERVER_IDENTITY_MAX_CLOCK_SKEW ||
        timestamp + SERVER_IDENTITY_MAX_CLOCK_SKEW < (uint64_t)now) {

        return 0;

    }

    return server_identity_verify_signature(packet_public, packet, SERVER_IDENTITY_SIGNED_BYTES,
                                            packet + SERVER_IDENTITY_SIGNATURE_OFFSET);
}

static void server_identity_mark_conflict(const struct sockaddr_in *source,
                                          const unsigned char remote_public[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Marks a server identity conflict
        Returns: No value
    */

    char address[INET_ADDRSTRLEN] = "unknown";
    char remote_fingerprint[SERVER_IDENTITY_FINGERPRINT_BUFFER];
    char message[256];

    if (source) {

        inet_ntop(AF_INET, &source->sin_addr, address, sizeof(address));

    }
    server_identity_fingerprint_from_public_key(remote_public, remote_fingerprint);
    snprintf(message, sizeof(message),
             "Short-label collision observed for %s from %s (SHA-512 %.16s...). Trusted public-key validation is "
             "unaffected.",
             Global_Server_Identity_Id, address, remote_fingerprint);

    pthread_mutex_lock(&Global_Server_Identity_Lock);
    Global_Server_Identity_Conflict = 1;
    snprintf(Global_Server_Identity_Status, sizeof(Global_Server_Identity_Status), "%s", message);
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
}

static void server_identity_refresh_local_validation(const unsigned char *packet, size_t packet_size) {
    /*
        Purpose: Refreshes local identity validation
        Returns: No value
    */

    char packet_id[SERVER_IDENTITY_ID_BUFFER];
    unsigned char packet_public[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
    int valid;

    if (!packet || packet_size != SERVER_IDENTITY_PACKET_BYTES) {

        return;

    }

    valid = server_identity_packet_valid(packet, packet_id, packet_public);

    if (valid) {

        pthread_mutex_lock(&Global_Server_Identity_Lock);

        if (Global_Server_Identity_Trusted_Is_Local &&
            CRYPTO_memcmp(packet_public, Global_Server_Identity_Public, SERVER_IDENTITY_PUBLIC_KEY_BYTES) == 0 &&
            CRYPTO_memcmp(packet_public, Global_Server_Identity_Trusted_Public, SERVER_IDENTITY_PUBLIC_KEY_BYTES) ==
                0) {

            Global_Server_Identity_Last_Verified_At = (int64_t)time(NULL);

            if (!Global_Server_Identity_Conflict) {

                snprintf(Global_Server_Identity_Status, sizeof(Global_Server_Identity_Status), "%s",
                         "ML-DSA-87 local identity revalidated; signed LAN announcement sent.");

            }

        }
        pthread_mutex_unlock(&Global_Server_Identity_Lock);

    }

    server_identity_secure_zero(packet_public, sizeof(packet_public));
    server_identity_secure_zero(packet_id, sizeof(packet_id));
}

static int server_identity_send_broadcast(unsigned char type) {
    /*
        Purpose: Sends the broadcast
        Returns: Success status
    */

    struct sockaddr_in destination;
    unsigned char *packet = NULL;
    ssize_t sent;
    int success = 0;

    if (Global_Server_Identity_Socket < 0) {

        return 0;

    }
    packet = OPENSSL_malloc(SERVER_IDENTITY_PACKET_BYTES);

    if (!packet || !server_identity_build_packet(packet, type)) {

        goto cleanup;

    }

    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(SERVER_IDENTITY_PORT);
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sent = sendto(Global_Server_Identity_Socket, packet, SERVER_IDENTITY_PACKET_BYTES, 0,
                  (const struct sockaddr *)&destination, sizeof(destination));
    success = sent == SERVER_IDENTITY_PACKET_BYTES;

    if (success && type == SERVER_IDENTITY_PACKET_TYPE_ANNOUNCE) {

        /*
         * A host is not guaranteed to receive its own UDP broadcast back on
         * the same socket. Revalidate the exact signed announcement locally
         * so a server trusting its own key refreshes every three seconds.
         * Remote hosts still refresh only after receiving and verifying it.
         */
        server_identity_refresh_local_validation(packet, SERVER_IDENTITY_PACKET_BYTES);

    }

cleanup:

    if (packet) {

        server_identity_secure_zero(packet, SERVER_IDENTITY_PACKET_BYTES);
        OPENSSL_free(packet);

    }
    return success;
}

static void server_identity_handle_packet(const unsigned char *packet, size_t packet_size,
                                          const struct sockaddr_in *source) {
    /*
        Purpose: Handles the packet
        Returns: No value
    */

    char packet_id[SERVER_IDENTITY_ID_BUFFER];
    unsigned char packet_public[SERVER_IDENTITY_PUBLIC_KEY_BYTES];

    if (!packet || packet_size != SERVER_IDENTITY_PACKET_BYTES ||
        !server_identity_packet_valid(packet, packet_id, packet_public)) {

        return;

    }

    if (strcmp(packet_id, Global_Server_Identity_Id) == 0 &&
        CRYPTO_memcmp(packet_public, Global_Server_Identity_Public, sizeof(packet_public)) != 0) {

        server_identity_mark_conflict(source, packet_public);
        server_identity_secure_zero(packet_public, sizeof(packet_public));
        return;

    }

    if (source &&
        CRYPTO_memcmp(packet_public, Global_Server_Identity_Trusted_Public, SERVER_IDENTITY_PUBLIC_KEY_BYTES) == 0) {

        char address[INET_ADDRSTRLEN] = "";

        if (inet_ntop(AF_INET, &source->sin_addr, address, sizeof(address))) {

            pthread_mutex_lock(&Global_Server_Identity_Lock);
            snprintf(Global_Server_Identity_Trusted_Host, sizeof(Global_Server_Identity_Trusted_Host), "%s", address);
            Global_Server_Identity_Last_Verified_At = (int64_t)time(NULL);

            if (!Global_Server_Identity_Conflict) {

                snprintf(Global_Server_Identity_Status, sizeof(Global_Server_Identity_Status), "%s",
                         "ML-DSA-87 identity verified, trusted public key loaded.");

            }
            pthread_mutex_unlock(&Global_Server_Identity_Lock);

        }

    }

    if (packet[9] == SERVER_IDENTITY_PACKET_TYPE_QUERY) {

        server_identity_send_broadcast(SERVER_IDENTITY_PACKET_TYPE_ANNOUNCE);

    }
    server_identity_secure_zero(packet_public, sizeof(packet_public));
}

static void *server_identity_thread_main(void *unused) {
    /*
        Purpose: Runs the server identity worker thread
        Returns: Thread result
    */

    time_t last_announce = 0;
    unsigned char *packet = OPENSSL_malloc(SERVER_IDENTITY_PACKET_BYTES);
    (void)unused;

    if (!packet) {

        server_identity_set_status("ML-DSA-87 identity monitor could not allocate its receive buffer.");
        return NULL;

    }

    while (Global_Server_Identity_Running) {
        struct sockaddr_in source;
        socklen_t source_size = sizeof(source);
        ssize_t received;
        time_t now = time(NULL);

        if (now - last_announce >= SERVER_IDENTITY_ANNOUNCE_INTERVAL) {

            server_identity_send_broadcast(SERVER_IDENTITY_PACKET_TYPE_ANNOUNCE);
            last_announce = now;

        }

        memset(&source, 0, sizeof(source));
        received = recvfrom(Global_Server_Identity_Socket, packet, SERVER_IDENTITY_PACKET_BYTES, 0,
                            (struct sockaddr *)&source, &source_size);

        if (received > 0) {

            server_identity_handle_packet(packet, (size_t)received, &source);

        }

        else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {

            pthread_mutex_lock(&Global_Server_Identity_Lock);
            Global_Server_Identity_Last_Verified_At = 0;
            pthread_mutex_unlock(&Global_Server_Identity_Lock);
            server_identity_set_status("LAN identity socket error; duplicate-ID monitoring is unavailable.");

        }
        {
            struct timespec delay = {0, 50000000L};
            nanosleep(&delay, NULL);
        }
    }

    server_identity_secure_zero(packet, SERVER_IDENTITY_PACKET_BYTES);
    OPENSSL_free(packet);
    return NULL;
}

static int server_identity_open_socket(void) {
    /*
        Purpose: Opens the socket
        Returns: Success status
    */

    struct sockaddr_in address;
    int enabled = 1;
    int flags;

    Global_Server_Identity_Socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (Global_Server_Identity_Socket < 0) {

        return 0;

    }

    setsockopt(Global_Server_Identity_Socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#ifdef SO_REUSEPORT
    setsockopt(Global_Server_Identity_Socket, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled));
#endif
    setsockopt(Global_Server_Identity_Socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_IDENTITY_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(Global_Server_Identity_Socket, (const struct sockaddr *)&address, sizeof(address)) != 0) {

        close(Global_Server_Identity_Socket);
        Global_Server_Identity_Socket = -1;
        return 0;

    }

    flags = fcntl(Global_Server_Identity_Socket, F_GETFL, 0);

    if (flags >= 0) {

        fcntl(Global_Server_Identity_Socket, F_SETFL, flags | O_NONBLOCK);

    }
    return 1;
}

int SERVER_IDENTITY_start(void) {
    /*
        Purpose: Starts the requested operation
        Returns: Success status
    */

    if (Global_Server_Identity_Thread_Started) {

        return 1;

    }

    if (!server_identity_load_or_create_key() ||
        !server_identity_id_from_public_key(Global_Server_Identity_Public, Global_Server_Identity_Id)) {

        server_identity_set_status("Unable to initialize the persistent ML-DSA-87 server identity.");
        return 0;

    }
    server_identity_fingerprint_from_public_key(Global_Server_Identity_Public, Global_Server_Identity_Fingerprint);

    if (!server_identity_initialize_public_files()) {

        server_identity_set_status("Unable to initialize the server public-key export or trusted-server file.");
        return 0;

    }

    if (!server_identity_open_socket()) {

        pthread_mutex_lock(&Global_Server_Identity_Lock);
        Global_Server_Identity_Last_Verified_At = 0;
        pthread_mutex_unlock(&Global_Server_Identity_Lock);
        server_identity_set_status("ML-DSA-87 identity is valid, but UDP port 47741 could not be bound.");
        return 1;

    }

    Global_Server_Identity_Running = 1;

    if (pthread_create(&Global_Server_Identity_Thread, NULL, server_identity_thread_main, NULL) != 0) {

        Global_Server_Identity_Running = 0;
        close(Global_Server_Identity_Socket);
        Global_Server_Identity_Socket = -1;
        pthread_mutex_lock(&Global_Server_Identity_Lock);
        Global_Server_Identity_Last_Verified_At = 0;
        pthread_mutex_unlock(&Global_Server_Identity_Lock);
        server_identity_set_status("ML-DSA-87 identity is valid, but the LAN identity monitor could not start.");
        return 1;

    }

    Global_Server_Identity_Thread_Started = 1;
    server_identity_set_status("ML-DSA-87 identity verified, trusted public key loaded.");
    server_identity_send_broadcast(SERVER_IDENTITY_PACKET_TYPE_QUERY);
    return 1;
}

void SERVER_IDENTITY_stop(void) {
    /*
        Purpose: Stops the requested operation
        Returns: No value
    */

    if (Global_Server_Identity_Thread_Started) {

        Global_Server_Identity_Running = 0;
        pthread_join(Global_Server_Identity_Thread, NULL);
        Global_Server_Identity_Thread_Started = 0;

    }

    if (Global_Server_Identity_Socket >= 0) {

        close(Global_Server_Identity_Socket);
        Global_Server_Identity_Socket = -1;

    }
    server_identity_secure_zero(Global_Server_Identity_Private, sizeof(Global_Server_Identity_Private));
}

const char *SERVER_IDENTITY_get_id(void) {
    /*
        Purpose: Gets the ID
        Returns: Text pointer
    */

    return Global_Server_Identity_Id;
}

const char *SERVER_IDENTITY_get_fingerprint(void) {
    /*
        Purpose: Gets the fingerprint
        Returns: Text pointer
    */

    return Global_Server_Identity_Fingerprint;
}

const char *SERVER_IDENTITY_get_algorithm(void) {
    /*
        Purpose: Gets the algorithm
        Returns: Text pointer
    */

    return SERVER_IDENTITY_ALGORITHM;
}

int SERVER_IDENTITY_has_conflict(void) {
    /*
        Purpose: Checks whether the conflict is present
        Returns: Boolean status
    */

    int conflict;
    pthread_mutex_lock(&Global_Server_Identity_Lock);
    conflict = Global_Server_Identity_Conflict;
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
    return conflict;
}

const char *SERVER_IDENTITY_get_local_name(void) {
    /*
        Purpose: Gets the local name
        Returns: Text pointer
    */

    return Global_Server_Identity_Local_Name;
}

const char *SERVER_IDENTITY_get_trusted_name(void) {
    /*
        Purpose: Gets the trusted name
        Returns: Text pointer
    */

    return Global_Server_Identity_Trusted_Name;
}

const char *SERVER_IDENTITY_get_trusted_fingerprint(void) {
    /*
        Purpose: Gets the trusted fingerprint
        Returns: Text pointer
    */

    return Global_Server_Identity_Trusted_Fingerprint;
}

const char *SERVER_IDENTITY_get_public_file_path(void) {
    /*
        Purpose: Gets the public file path
        Returns: Text pointer
    */

    return Global_Server_Identity_Public_File_Path;
}

int SERVER_IDENTITY_trusted_is_local(void) {
    /*
        Purpose: Checks whether the trusted identity is local
        Returns: Success status
    */

    int is_local;
    pthread_mutex_lock(&Global_Server_Identity_Lock);
    is_local = Global_Server_Identity_Trusted_Is_Local;
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
    return is_local;
}

static int server_identity_resolve_import_path(const char *path, char *resolved, size_t resolved_size) {
    /*
        Purpose: Resolves the import path
        Returns: Success status
    */

    const char *home;

    if (!path || !resolved || resolved_size == 0) {

        return 0;

    }

    if (path[0] == '~' && path[1] == '/') {

        home = getenv("HOME");

        if (!home || home[0] == '\0') {

            return 0;

        }
        return snprintf(resolved, resolved_size, "%s/%s", home, path + 2) < (int)resolved_size;

    }
    return snprintf(resolved, resolved_size, "%s", path) < (int)resolved_size;
}

int SERVER_IDENTITY_preview_public_file(const char *path, Type_Server_Public_Identity *identity, char *message,
                                        size_t message_size) {
    /*
        Purpose: Previews a public identity file
        Returns: Success status
    */

    char resolved_path[PATH_MAX];

    if (!path || path[0] == '\0' || !identity ||
        !server_identity_resolve_import_path(path, resolved_path, sizeof(resolved_path))) {

        if (message && message_size > 0) {

            snprintf(message, message_size, "Select a RetroSpectrum .rspub public-key file.");

        }
        return 0;

    }

    if (!server_identity_read_public_file(resolved_path, identity)) {

        if (message && message_size > 0) {

            snprintf(message, message_size,
                     "The selected file is not a valid ML-DSA-87 RetroSpectrum public-key file.");

        }
        return 0;

    }

    if (message && message_size > 0) {

        snprintf(message, message_size, "Public key loaded for %s.", identity->server_name);

    }
    return 1;
}

int SERVER_IDENTITY_import_public_file(const char *path, char *message, size_t message_size) {
    /*
        Purpose: Imports the public file
        Returns: Success status
    */

    Type_Server_Public_Identity identity;
    char trusted_path[PATH_MAX];
    int success = 0;

    memset(&identity, 0, sizeof(identity));

    if (!SERVER_IDENTITY_preview_public_file(path, &identity, message, message_size) ||
        !server_identity_named_path(trusted_path, sizeof(trusted_path), "trusted_server.rspub") ||
        !server_identity_write_public_file(trusted_path, identity.server_name, identity.public_key)) {

        goto cleanup;

    }
    server_identity_set_trusted(&identity);
    server_identity_set_status(SERVER_IDENTITY_trusted_is_local()
                                   ? "Local server public key selected and verified."
                                   : "Trusted remote server public key imported; encrypted TLS login is enabled.");

    if (message && message_size > 0) {

        snprintf(message, message_size, "Trusted server changed to %s.", identity.server_name);

    }
    success = 1;

cleanup:
    server_identity_secure_zero(&identity, sizeof(identity));
    return success;
}

int SERVER_IDENTITY_validate_target(const char *server_id, char *message, size_t message_size) {
    /*
        Purpose: Validates the target
        Returns: Boolean status
    */

    if (!server_id || strlen(server_id) != SERVER_IDENTITY_ID_LENGTH) {

        if (message && message_size > 0) {

            snprintf(message, message_size, "Server ID must contain exactly 12 uppercase letters or numbers.");

        }
        return 0;

    }
    for (int i = 0; i < SERVER_IDENTITY_ID_LENGTH; i++) {
        unsigned char character = (unsigned char)server_id[i];

        if (!((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9'))) {

            if (message && message_size > 0) {

                snprintf(message, message_size, "Server ID must contain exactly 12 uppercase letters or numbers.");

            }
            return 0;

        }
    }

    if (message && message_size > 0) {

        if (strcmp(server_id, Global_Server_Identity_Id) == 0) {

            snprintf(message, message_size, "Local ML-DSA-87 server identity verified.");

        }

        else {

            snprintf(message, message_size,
                     "Remote server selected; ML-DSA-87 authenticated hybrid post-quantum TLS is required.");

        }

    }
    return 1;
}

int SERVER_IDENTITY_get_trusted_host(char *host, size_t host_size) {
    /*
        Purpose: Gets the trusted host
        Returns: Success status
    */

    int available;

    if (!host || host_size == 0) {

        return 0;

    }
    pthread_mutex_lock(&Global_Server_Identity_Lock);
    available = Global_Server_Identity_Trusted_Host[0] != '\0';
    snprintf(host, host_size, "%s", Global_Server_Identity_Trusted_Host);
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
    return available;
}

int SERVER_IDENTITY_get_trusted_public(unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Gets the trusted public
        Returns: Success status
    */

    if (!public_key) {

        return 0;

    }
    pthread_mutex_lock(&Global_Server_Identity_Lock);
    memcpy(public_key, Global_Server_Identity_Trusted_Public, SERVER_IDENTITY_PUBLIC_KEY_BYTES);
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
    return 1;
}

int SERVER_IDENTITY_get_local_public(unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES]) {
    /*
        Purpose: Gets the local public identity information
        Returns: Success status
    */

    if (!public_key) {

        return 0;

    }
    pthread_mutex_lock(&Global_Server_Identity_Lock);
    memcpy(public_key, Global_Server_Identity_Public, SERVER_IDENTITY_PUBLIC_KEY_BYTES);
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
    return 1;
}

int SERVER_IDENTITY_sign_local(const unsigned char *message, size_t message_size,
                               unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES]) {
    /*
        Purpose: Signs data with the local server identity key
        Returns: Success status
    */

    int result;

    if (!message || !signature) {

        return 0;

    }
    pthread_mutex_lock(&Global_Server_Identity_Lock);
    result = server_identity_sign_with_private(Global_Server_Identity_Private, message, message_size, signature);
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
    return result;
}

int SERVER_IDENTITY_verify_trusted(const unsigned char *message, size_t message_size,
                                   const unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES]) {
    /*
        Purpose: Verifies the trusted
        Returns: Success status
    */

    unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
    int result;

    if (!message || !signature || !SERVER_IDENTITY_get_trusted_public(public_key)) {

        return 0;

    }
    result = server_identity_verify_signature(public_key, message, message_size, signature);
    server_identity_secure_zero(public_key, sizeof(public_key));
    return result;
}

int64_t SERVER_IDENTITY_last_verified_at(void) {
    /*
        Purpose: Gets the last verified at a position
        Returns: Success status
    */

    int64_t value;
    pthread_mutex_lock(&Global_Server_Identity_Lock);
    value = Global_Server_Identity_Last_Verified_At;
    pthread_mutex_unlock(&Global_Server_Identity_Lock);
    return value;
}

const char *SERVER_IDENTITY_status(void) {
    /*
        Purpose: Gets the requested item status
        Returns: Text pointer
    */

    return Global_Server_Identity_Status;
}
