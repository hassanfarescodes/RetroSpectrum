#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

/*
 * ============================================================================
 * File:            DatabaseCrypto.c
 * Author:          Hassan Fares
 *
 * Description:     Database encryption and key management for RetroSpectrum
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux
 *
 *                                                               05/04/2026
 * ============================================================================
 */

#include "DatabaseCrypto.h"
#include "SecureFunctions.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DATABASE_CRYPTO_MASTER_BYTES 32
#define DATABASE_CRYPTO_DERIVED_BYTES 32
#define DATABASE_CRYPTO_HEX_BYTES (DATABASE_CRYPTO_DERIVED_BYTES * 2)
#define DATABASE_CRYPTO_KEY_FILENAME "database_master.key"
#define DATABASE_CRYPTO_KEY_PATH_FILENAME "database_key_path.txt"
#define DATABASE_CRYPTO_AUTH_FILENAME "auth.db"
#define DATABASE_CRYPTO_DATA_FILENAME "retrospectrum_data.db"
#define DATABASE_CRYPTO_MIN_CIPHER_MAJOR 4
#define DATABASE_CRYPTO_MIN_CIPHER_MINOR 6
#define DATABASE_CRYPTO_MIN_CIPHER_PATCH 1

static pthread_mutex_t Global_Database_Crypto_Key_Lock = PTHREAD_MUTEX_INITIALIZER;
static char Global_Database_Crypto_Key_Path[PATH_MAX] = "";
static unsigned char Global_Database_Crypto_Master[DATABASE_CRYPTO_MASTER_BYTES];
static int Global_Database_Crypto_Master_Loaded = 0;
static int Global_Database_Crypto_Cleanup_Registered = 0;

static void database_crypto_error(char *error, size_t error_size, const char *message) {
    /*
        Purpose: Reports a database encryption error
        Returns: No value
    */

    if (error && error_size > 0) {

        (void)sec_strcpy(error, error_size, message ? message : "Database cryptography error.");

    }
}

static int database_crypto_write_all(int descriptor, const unsigned char *data, size_t size) {
    /*
        Purpose: Writes all requested bytes
        Returns: Success status
    */

    size_t offset = 0;

    while (offset < size) {
        ssize_t written = write(descriptor, data + offset, size - offset);

        if (written < 0) {

            if (errno == EINTR) {

                continue;

            }
            return 0;

        }

        if (written == 0) {

            return 0;

        }
        offset += (size_t)written;
    }
    return 1;
}

static int database_crypto_read_all(int descriptor, unsigned char *data, size_t size) {
    /*
        Purpose: Reads all requested bytes
        Returns: Success status
    */

    size_t offset = 0;

    while (offset < size) {
        ssize_t received = read(descriptor, data + offset, size - offset);

        if (received < 0) {

            if (errno == EINTR) {

                continue;

            }
            return 0;

        }

        if (received == 0) {

            return 0;

        }
        offset += (size_t)received;
    }
    return 1;
}

static int database_crypto_ensure_directory(const char *path) {
    /*
        Purpose: Ensures the directory
        Returns: Success status
    */

    struct stat st;

    if (!path || path[0] == '\0') {

        return 0;

    }

    if (lstat(path, &st) == 0) {

        if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode) || st.st_uid != geteuid()) {

            return 0;

        }

        if ((st.st_mode & 077) != 0 && chmod(path, 0700) != 0) {

            return 0;

        }
        return access(path, R_OK | W_OK | X_OK) == 0;

    }

    if (errno != ENOENT) {

        return 0;

    }

    if (mkdir(path, 0700) != 0) {

        return 0;

    }
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && st.st_uid == geteuid() &&
           access(path, R_OK | W_OK | X_OK) == 0;
}

static int database_crypto_directory(char *directory, size_t directory_size) {
    /*
        Purpose: Gets the database configuration directory
        Returns: Success status
    */

    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char root[PATH_MAX];

    if (!directory || directory_size == 0) {

        return 0;

    }

    if (xdg && xdg[0] != '\0') {

        if (xdg[0] != '/' || !sec_strcpy(root, sizeof(root), xdg)) {

            return 0;

        }

    }

    else if (home && home[0] != '\0') {

        if (home[0] != '/' || !sec_sprintf(root, sizeof(root), "%s/.config", home)) {

            return 0;

        }

    }

    else {

        return 0;

    }

    if (!database_crypto_ensure_directory(root) || !sec_sprintf(directory, directory_size, "%s/retrospectrum", root) ||
        !database_crypto_ensure_directory(directory)) {

        return 0;

    }

    if (chmod(directory, 0700) != 0 && errno != EPERM) {

        return 0;

    }
    return 1;
}

static int database_crypto_path(char *path, size_t path_size, const char *filename) {
    /*
        Purpose: Builds the requested item path
        Returns: Success status
    */

    char directory[PATH_MAX];

    if (!path || path_size == 0 || !filename || !database_crypto_directory(directory, sizeof(directory))) {

        return 0;

    }
    return sec_sprintf(path, path_size, "%s/%s", directory, filename);
}

static int database_crypto_validate_key_file(int descriptor) {
    /*
        Purpose: Validates the key file
        Returns: Boolean status
    */

    struct stat st;

    if (fstat(descriptor, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_size != DATABASE_CRYPTO_MASTER_BYTES) {

        return 0;

    }

    if ((st.st_mode & 077) != 0) {

        if (fchmod(descriptor, 0600) != 0) {

            return 0;

        }

    }
    return 1;
}

static void database_crypto_cleanup_cached_master(void) {
    /*
        Purpose: Clears the cached master
        Returns: No value
    */

    pthread_mutex_lock(&Global_Database_Crypto_Key_Lock);

    if (Global_Database_Crypto_Master_Loaded) {

        OPENSSL_cleanse(Global_Database_Crypto_Master, sizeof(Global_Database_Crypto_Master));
        (void)munlock(Global_Database_Crypto_Master, sizeof(Global_Database_Crypto_Master));
        Global_Database_Crypto_Master_Loaded = 0;

    }
    pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);
}

static void database_crypto_cache_master(const unsigned char master[DATABASE_CRYPTO_MASTER_BYTES]) {
    /*
        Purpose: Caches the master
        Returns: No value
    */

    pthread_mutex_lock(&Global_Database_Crypto_Key_Lock);

    if (Global_Database_Crypto_Master_Loaded) {

        OPENSSL_cleanse(Global_Database_Crypto_Master, sizeof(Global_Database_Crypto_Master));

    }
    memcpy(Global_Database_Crypto_Master, master, sizeof(Global_Database_Crypto_Master));
    Global_Database_Crypto_Master_Loaded = 1;
    (void)mlock(Global_Database_Crypto_Master, sizeof(Global_Database_Crypto_Master));
#ifdef MADV_DONTDUMP
    (void)madvise(Global_Database_Crypto_Master, sizeof(Global_Database_Crypto_Master), MADV_DONTDUMP);
#endif

    if (!Global_Database_Crypto_Cleanup_Registered) {

        if (atexit(database_crypto_cleanup_cached_master) == 0) {

            Global_Database_Crypto_Cleanup_Registered = 1;

        }

    }
    pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);
}

static int database_crypto_copy_cached_master(unsigned char master[DATABASE_CRYPTO_MASTER_BYTES]) {
    /*
        Purpose: Copies the cached master
        Returns: Success status
    */

    int available;

    pthread_mutex_lock(&Global_Database_Crypto_Key_Lock);
    available = Global_Database_Crypto_Master_Loaded;

    if (available) {

        memcpy(master, Global_Database_Crypto_Master, DATABASE_CRYPTO_MASTER_BYTES);

    }
    pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);
    return available;
}

static int database_crypto_default_key_path(char *path, size_t path_size) {
    /*
        Purpose: Builds the default key path
        Returns: Success status
    */

    return database_crypto_path(path, path_size, DATABASE_CRYPTO_KEY_FILENAME);
}

static int database_crypto_read_saved_key_path(char *path, size_t path_size) {
    /*
        Purpose: Reads the saved key path
        Returns: Success status
    */

    char config_path[PATH_MAX];
    char buffer[PATH_MAX];
    int descriptor;
    ssize_t received;
    struct stat st;
    size_t length;

    if (!path || path_size == 0 ||
        !database_crypto_path(config_path, sizeof(config_path), DATABASE_CRYPTO_KEY_PATH_FILENAME)) {

        return -1;

    }

    descriptor = open(config_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if (descriptor < 0) {

        return errno == ENOENT ? 0 : -1;

    }

    if (fstat(descriptor, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_size <= 0 ||
        st.st_size >= (off_t)sizeof(buffer)) {

        close(descriptor);
        return -1;

    }
    received = read(descriptor, buffer, sizeof(buffer) - 1);
    close(descriptor);

    if (received <= 0) {

        return -1;

    }

    buffer[received] = '\0';
    length = strcspn(buffer, "\r\n");
    buffer[length] = '\0';

    if (buffer[0] != '/' || !sec_strcpy(path, path_size, buffer)) {

        return -1;

    }

    return 1;
}

static int database_crypto_resolve_key_path(char *path, size_t path_size, char *error, size_t error_size) {
    /*
        Purpose: Resolves the key path
        Returns: Success status
    */

    char resolved[PATH_MAX] = "";

    if (!path || path_size == 0) {

        database_crypto_error(error, error_size, "Invalid database key-path destination.");
        return 0;

    }

    pthread_mutex_lock(&Global_Database_Crypto_Key_Lock);

    if (Global_Database_Crypto_Key_Path[0] != '\0' &&
        !sec_strcpy(resolved, sizeof(resolved), Global_Database_Crypto_Key_Path)) {

        pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);

        database_crypto_error(error, error_size, "The cached database master-key path is invalid.");

        return 0;

    }

    pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);

    if (resolved[0] == '\0') {

        int saved_result = database_crypto_read_saved_key_path(resolved, sizeof(resolved));

        if (saved_result < 0) {

            database_crypto_error(error, error_size, "The saved database key-file path is invalid or unsafe.");
            return 0;

        }

        if (saved_result == 0 && !database_crypto_default_key_path(resolved, sizeof(resolved))) {

            database_crypto_error(error, error_size, "Unable to resolve the database master-key path.");
            return 0;

        }

    }

    pthread_mutex_lock(&Global_Database_Crypto_Key_Lock);

    if (Global_Database_Crypto_Key_Path[0] == '\0' &&
        !sec_strcpy(Global_Database_Crypto_Key_Path, sizeof(Global_Database_Crypto_Key_Path), resolved)) {

        pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);

        database_crypto_error(error, error_size, "The database master-key path is too long.");

        return 0;

    }

    if (!sec_strcpy(path, path_size, Global_Database_Crypto_Key_Path)) {

        pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);

        database_crypto_error(error, error_size, "The database master-key path is too long.");

        return 0;

    }

    pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);
    return 1;
}

static int database_crypto_load_key_file(const char *path, unsigned char master[DATABASE_CRYPTO_MASTER_BYTES],
                                         char *error, size_t error_size) {
    /*
        Purpose: Loads the key file
        Returns: Success status
    */

    int descriptor;

    if (!path || path[0] == '\0' || !master) {

        database_crypto_error(error, error_size, "No database key file was selected.");
        return 0;

    }
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if (descriptor < 0) {

        char message[PATH_MAX + 96];

        if (!sec_sprintf(message, sizeof(message), "Unable to open database key file %s: %s", path, strerror(errno))) {

            database_crypto_error(error, error_size, "Unable to open database key file.");

        }

        else {

            database_crypto_error(error, error_size, message);

        }

        return 0;

    }

    if (!database_crypto_validate_key_file(descriptor) ||
        !database_crypto_read_all(descriptor, master, DATABASE_CRYPTO_MASTER_BYTES)) {

        close(descriptor);
        database_crypto_error(error, error_size, "The database key file must be an owner-only regular 32-byte file.");
        return 0;

    }
    close(descriptor);
    return 1;
}

static int database_crypto_any_database_exists(void) {
    /*
        Purpose: Checks whether the any database exists
        Returns: Boolean status
    */

    char auth_path[PATH_MAX];
    char data_path[PATH_MAX];
    struct stat st;

    if (database_crypto_path(auth_path, sizeof(auth_path), DATABASE_CRYPTO_AUTH_FILENAME) &&
        lstat(auth_path, &st) == 0) {

        return 1;

    }

    if (database_crypto_path(data_path, sizeof(data_path), DATABASE_CRYPTO_DATA_FILENAME) &&
        lstat(data_path, &st) == 0) {

        return 1;

    }
    return 0;
}

static int database_crypto_create_default_master(const char *path, unsigned char master[DATABASE_CRYPTO_MASTER_BYTES],
                                                 char *error, size_t error_size) {
    /*
        Purpose: Creates the default master
        Returns: Success status
    */

    int descriptor = -1;
    mode_t old_mask;
    int success = 0;

    if (RAND_priv_bytes(master, DATABASE_CRYPTO_MASTER_BYTES) != 1) {

        database_crypto_error(error, error_size, "Unable to generate a database master key.");
        return 0;

    }
    old_mask = umask(0077);
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    umask(old_mask);

    if (descriptor < 0) {

        database_crypto_error(error, error_size, "Unable to securely create the local database master-key file.");
        goto cleanup;

    }

    if (!database_crypto_write_all(descriptor, master, DATABASE_CRYPTO_MASTER_BYTES) || fsync(descriptor) != 0 ||
        fchmod(descriptor, 0600) != 0) {

        database_crypto_error(error, error_size, "Unable to persist the database master key.");
        close(descriptor);
        descriptor = -1;
        unlink(path);
        goto cleanup;

    }
    success = 1;

cleanup:

    if (descriptor >= 0) {

        close(descriptor);

    }

    if (!success) {

        OPENSSL_cleanse(master, DATABASE_CRYPTO_MASTER_BYTES);

    }
    return success;
}

static int database_crypto_load_or_create_master(unsigned char master[DATABASE_CRYPTO_MASTER_BYTES], char *error,
                                                 size_t error_size) {
    /*
        Purpose: Loads or creates the master
        Returns: Success status
    */

    char path[PATH_MAX];
    char default_path[PATH_MAX];

    if (!master) {

        database_crypto_error(error, error_size, "Invalid database master-key buffer.");
        return 0;

    }

    if (database_crypto_copy_cached_master(master)) {

        return 1;

    }

    if (!database_crypto_resolve_key_path(path, sizeof(path), error, error_size) ||
        !database_crypto_default_key_path(default_path, sizeof(default_path))) {

        return 0;

    }
    {
        struct stat key_st;
        int key_missing = lstat(path, &key_st) != 0 && errno == ENOENT;

        if (!database_crypto_load_key_file(path, master, error, error_size)) {

            if (!key_missing || strcmp(path, default_path) != 0 || database_crypto_any_database_exists()) {

                OPENSSL_cleanse(master, DATABASE_CRYPTO_MASTER_BYTES);
                return 0;

            }

            if (!database_crypto_create_default_master(path, master, error, error_size)) {

                return 0;

            }

        }
    }
    database_crypto_cache_master(master);
    return 1;
}

static int database_crypto_derive_from_master(const unsigned char master[DATABASE_CRYPTO_MASTER_BYTES],
                                              const char *domain, unsigned char key[DATABASE_CRYPTO_DERIVED_BYTES],
                                              char *error, size_t error_size) {
    /*
        Purpose: Derives the from master
        Returns: Success status
    */

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    int success = 0;

    memset(digest, 0, sizeof(digest));

    if (!master || !domain || !key ||
        !HMAC(EVP_sha512(), master, DATABASE_CRYPTO_MASTER_BYTES, (const unsigned char *)domain, strlen(domain), digest,
              &digest_size) ||
        digest_size < DATABASE_CRYPTO_DERIVED_BYTES) {

        database_crypto_error(error, error_size, "Unable to derive the database encryption key.");
        goto cleanup;

    }
    memcpy(key, digest, DATABASE_CRYPTO_DERIVED_BYTES);
    success = 1;

cleanup:
    OPENSSL_cleanse(digest, sizeof(digest));
    return success;
}

static int database_crypto_derive_key(const char *domain, unsigned char key[DATABASE_CRYPTO_DERIVED_BYTES], char *error,
                                      size_t error_size) {
    /*
        Purpose: Derives the key
        Returns: Success status
    */

    unsigned char master[DATABASE_CRYPTO_MASTER_BYTES];
    int success;

    memset(master, 0, sizeof(master));

    if (!domain || !key || !database_crypto_load_or_create_master(master, error, error_size)) {

        OPENSSL_cleanse(master, sizeof(master));
        return 0;

    }
    success = database_crypto_derive_from_master(master, domain, key, error, error_size);
    OPENSSL_cleanse(master, sizeof(master));
    return success;
}

static void database_crypto_hex(const unsigned char key[DATABASE_CRYPTO_DERIVED_BYTES],
                                char output[DATABASE_CRYPTO_HEX_BYTES + 1]) {
    /*
        Purpose: Encodes bytes as hexadecimal text
        Returns: No value
    */

    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < DATABASE_CRYPTO_DERIVED_BYTES; i++) {
        output[i * 2] = digits[key[i] >> 4];
        output[i * 2 + 1] = digits[key[i] & 0x0fU];
    }
    output[DATABASE_CRYPTO_HEX_BYTES] = '\0';
}

static int database_crypto_parse_version(const char *version, int *major, int *minor, int *patch) {
    /*
        Purpose: Parses the SQLCipher version
        Returns: Success status
    */

    const char *cursor;
    char *end = NULL;
    long value;

    if (!version || !major || !minor || !patch) {

        return 0;

    }

    cursor = version;

    /* Major */

    if (*cursor < '0' || *cursor > '9') {

        return 0;

    }

    errno = 0;
    value = strtol(cursor, &end, 10);

    if (errno == ERANGE || end == cursor || value < 0 || value > INT_MAX || *end != '.') {

        return 0;

    }

    *major = (int)value;
    cursor = end + 1;

    /* Minor */

    if (*cursor < '0' || *cursor > '9') {

        return 0;

    }

    errno = 0;
    value = strtol(cursor, &end, 10);

    if (errno == ERANGE || end == cursor || value < 0 || value > INT_MAX || *end != '.') {

        return 0;

    }

    *minor = (int)value;
    cursor = end + 1;

    /* Patch */

    if (*cursor < '0' || *cursor > '9') {

        return 0;

    }

    errno = 0;
    value = strtol(cursor, &end, 10);

    if (errno == ERANGE || end == cursor || value < 0 || value > INT_MAX) {

        return 0;

    }

    *patch = (int)value;

    /* Accept either a bare version or SQLCipher's legitimate community suffix. */

    if (*end == '\0') {

        return 1;

    }

    if (strcmp(end, " community") == 0) {

        return 1;

    }

    return 0;
}

static int database_crypto_version_supported(const char *version) {
    /*
        Purpose: Checks whether the SQLCipher version is supported
        Returns: Success status
    */

    int major = 0;
    int minor = 0;
    int patch = 0;

    if (!database_crypto_parse_version(version, &major, &minor, &patch)) {

        return 0;

    }

    if (major != DATABASE_CRYPTO_MIN_CIPHER_MAJOR) {

        return major > DATABASE_CRYPTO_MIN_CIPHER_MAJOR;

    }

    if (minor != DATABASE_CRYPTO_MIN_CIPHER_MINOR) {

        return minor > DATABASE_CRYPTO_MIN_CIPHER_MINOR;

    }
    return patch >= DATABASE_CRYPTO_MIN_CIPHER_PATCH;
}

static int database_crypto_has_sqlcipher(sqlite3 *database) {
    /*
        Purpose: Checks whether the sqlcipher is present
        Returns: Boolean status
    */

    sqlite3_stmt *statement = NULL;
    int valid = 0;

    if (!database || sqlite3_prepare_v2(database, "PRAGMA cipher_version;", -1, &statement, NULL) != SQLITE_OK) {

        return 0;

    }

    if (sqlite3_step(statement) == SQLITE_ROW) {

        const unsigned char *version = sqlite3_column_text(statement, 0);
        valid = version && database_crypto_version_supported((const char *)version);

    }
    sqlite3_finalize(statement);
    return valid;
}

static int database_crypto_exec(sqlite3 *database, const char *sql, char *error, size_t error_size) {
    /*
        Purpose: Executes the requested operation
        Returns: Success status
    */

    char *sqlite_error = NULL;
    int result = sqlite3_exec(database, sql, NULL, NULL, &sqlite_error);

    if (result != SQLITE_OK) {

        database_crypto_error(error, error_size, sqlite_error ? sqlite_error : sqlite3_errmsg(database));
        sqlite3_free(sqlite_error);
        return 0;

    }
    return 1;
}

static int database_crypto_validate_database_file(const char *path, char *error, size_t error_size) {
    /*
        Purpose: Validates the database file
        Returns: Boolean status
    */

    struct stat st;

    if (!path || path[0] == '\0') {

        database_crypto_error(error, error_size, "Invalid encrypted-database path.");
        return 0;

    }

    if (lstat(path, &st) != 0) {

        if (errno == ENOENT) {

            return 1;

        }
        database_crypto_error(error, error_size, "Unable to inspect the encrypted-database file.");
        return 0;

    }

    if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode) || st.st_uid != geteuid()) {

        database_crypto_error(error, error_size,
                              "The encrypted-database path is not a safe owner-controlled regular file.");
        return 0;

    }

    if ((st.st_mode & 077) != 0 && chmod(path, 0600) != 0) {

        database_crypto_error(error, error_size, "Unable to restrict encrypted-database permissions.");
        return 0;

    }
    return 1;
}

static int database_crypto_apply_key(sqlite3 *database, const unsigned char key[DATABASE_CRYPTO_DERIVED_BYTES],
                                     char *error, size_t error_size) {
    /*
        Purpose: Applies the key
        Returns: Success status
    */

    char key_hex[DATABASE_CRYPTO_HEX_BYTES + 1];
    char sql[128];
    sqlite3_stmt *statement = NULL;
    int success = 0;

    if (!database || !key || !database_crypto_has_sqlcipher(database)) {

        database_crypto_error(error, error_size,
                              "RetroSpectrum requires SQLCipher 4.6.1 or newer; plaintext SQLite and older SQLCipher "
                              "builds are refused.");
        return 0;

    }

    database_crypto_hex(key, key_hex);
    snprintf(sql, sizeof(sql), "PRAGMA key = \"x'%s'\";", key_hex);

    if (!database_crypto_exec(database, sql, error, error_size) ||
        !database_crypto_exec(database, "PRAGMA cipher_page_size = 4096;", error, error_size) ||
        !database_crypto_exec(database, "PRAGMA cipher_hmac_algorithm = HMAC_SHA512;", error, error_size) ||
        !database_crypto_exec(database, "PRAGMA cipher_kdf_algorithm = PBKDF2_HMAC_SHA512;", error, error_size) ||
        !database_crypto_exec(database, "PRAGMA cipher_memory_security = ON;", error, error_size) ||
        sqlite3_prepare_v2(database, "SELECT count(*) FROM sqlite_master;", -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) {

        if (error && error_size > 0 && error[0] == '\0') {

            database_crypto_error(error, error_size,
                                  "The encrypted database key is incorrect or the database is damaged.");

        }
        goto cleanup;

    }
    success = 1;

cleanup:
    sqlite3_finalize(statement);
    OPENSSL_cleanse(key_hex, sizeof(key_hex));
    OPENSSL_cleanse(sql, sizeof(sql));
    return success;
}

static int database_crypto_key_opens_database(const char *path, const char *domain,
                                              const unsigned char master[DATABASE_CRYPTO_MASTER_BYTES], char *error,
                                              size_t error_size) {
    /*
        Purpose: Checks whether a key opens the database
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    unsigned char key[DATABASE_CRYPTO_DERIVED_BYTES];
    unsigned char header[16];
    int descriptor = -1;
    int result;
    int success = 0;

    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if (descriptor < 0) {

        if (errno == ENOENT) {

            return 1;

        }
        database_crypto_error(error, error_size, "Unable to inspect an existing database file.");
        return 0;

    }
    result = (int)read(descriptor, header, sizeof(header));
    close(descriptor);
    descriptor = -1;

    if (result == (int)sizeof(header) && memcmp(header, "SQLite format 3\000", sizeof(header)) == 0) {

        database_crypto_error(
            error, error_size,
            "Plaintext SQLite databases are unsupported; only current SQLCipher databases are accepted.");
        return 0;

    }

    memset(key, 0, sizeof(key));

    if (!database_crypto_derive_from_master(master, domain, key, error, error_size)) {

        goto cleanup;

    }
    result = sqlite3_open_v2(path, &database, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL);

    if (result != SQLITE_OK || !database_crypto_apply_key(database, key, error, error_size) ||
        sqlite3_prepare_v2(database, "SELECT count(*) FROM sqlite_master;", -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) {

        database_crypto_error(error, error_size,
                              "The selected key file does not unlock the existing RetroSpectrum databases.");
        goto cleanup;

    }
    success = 1;

cleanup:
    sqlite3_finalize(statement);

    if (database) {

        sqlite3_close(database);

    }
    OPENSSL_cleanse(key, sizeof(key));
    return success;
}

static int database_crypto_write_saved_key_path(const char *path, char *error, size_t error_size) {
    /*
        Purpose: Writes the saved key path
        Returns: Success status
    */

    char config_path[PATH_MAX];
    char temporary[PATH_MAX];
    int descriptor = -1;
    mode_t old_mask;
    size_t length;
    int success = 0;

    if (!path || !database_crypto_path(config_path, sizeof(config_path), DATABASE_CRYPTO_KEY_PATH_FILENAME) ||
        !sec_sprintf(temporary, sizeof(temporary), "%s.tmp-%ld", config_path, (long)getpid())) {

        database_crypto_error(error, error_size, "Unable to save the database key-file path.");
        return 0;

    }

    length = strlen(path);
    old_mask = umask(0077);
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    umask(old_mask);

    if (descriptor < 0 || !database_crypto_write_all(descriptor, (const unsigned char *)path, length) ||
        !database_crypto_write_all(descriptor, (const unsigned char *)"\n", 1) || fsync(descriptor) != 0 ||
        close(descriptor) != 0) {

        descriptor = -1;
        unlink(temporary);
        database_crypto_error(error, error_size, "Unable to save the database key-file path.");
        return 0;

    }
    descriptor = -1;

    if (rename(temporary, config_path) != 0 || chmod(config_path, 0600) != 0) {

        unlink(temporary);
        database_crypto_error(error, error_size, "Unable to activate the database key-file path.");
        return 0;

    }
    success = 1;
    return success;
}

static int database_crypto_file_is_plaintext(const char *path) {
    /*
        Purpose: Checks whether the file is plaintext
        Returns: Success status
    */

    unsigned char header[16];
    int descriptor;
    ssize_t received;

    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if (descriptor < 0) {

        return 0;

    }
    received = read(descriptor, header, sizeof(header));
    close(descriptor);
    return received == (ssize_t)sizeof(header) && memcmp(header, "SQLite format 3\0", 16) == 0;
}

static int database_crypto_open(sqlite3 **database, char *path, size_t path_size, const char *filename,
                                const char *domain, char *error, size_t error_size) {
    /*
        Purpose: Opens the requested operation
        Returns: Success status
    */

    unsigned char key[DATABASE_CRYPTO_DERIVED_BYTES];
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    mode_t previous_mask;
    int result;
    int success = 0;

    if (error && error_size > 0) {

        error[0] = '\0';

    }

    if (!database || !path || !filename || !domain || !database_crypto_path(path, path_size, filename) ||
        !database_crypto_derive_key(domain, key, error, error_size)) {

        return 0;

    }

    if (!database_crypto_validate_database_file(path, error, error_size)) {

        goto cleanup;

    }

    if (database_crypto_file_is_plaintext(path)) {

        database_crypto_error(
            error, error_size,
            "Plaintext SQLite databases are unsupported; only current SQLCipher databases are accepted.");
        goto cleanup;

    }

    previous_mask = umask(0077);
    result = sqlite3_open_v2(path, database, flags, NULL);
    umask(previous_mask);

    if (result != SQLITE_OK) {

        database_crypto_error(error, error_size,
                              *database ? sqlite3_errmsg(*database) : "Unable to open encrypted database.");

        if (*database) {

            sqlite3_close(*database);
            *database = NULL;

        }
        goto cleanup;

    }

    if (!database_crypto_apply_key(*database, key, error, error_size)) {

        sqlite3_close(*database);
        *database = NULL;
        goto cleanup;

    }

    if (chmod(path, 0600) != 0 || !database_crypto_exec(*database, "PRAGMA foreign_keys = ON;", error, error_size) ||
        !database_crypto_exec(*database, "PRAGMA secure_delete = ON;", error, error_size)) {

        sqlite3_close(*database);
        *database = NULL;
        goto cleanup;

    }
    sqlite3_busy_timeout(*database, 5000);
    success = 1;

cleanup:
    OPENSSL_cleanse(key, sizeof(key));
    return success;
}

int DATABASE_CRYPTO_open_auth(sqlite3 **database, char *path, size_t path_size, char *error, size_t error_size) {
    /*
        Purpose: Opens the authentication
        Returns: Success status
    */

    return database_crypto_open(database, path, path_size, DATABASE_CRYPTO_AUTH_FILENAME,
                                "RetroSpectrum SQLCipher auth database key v1", error, error_size);
}

int DATABASE_CRYPTO_open_data(sqlite3 **database, char *path, size_t path_size, char *error, size_t error_size) {
    /*
        Purpose: Opens the data
        Returns: Success status
    */

    return database_crypto_open(database, path, path_size, DATABASE_CRYPTO_DATA_FILENAME,
                                "RetroSpectrum SQLCipher operational database key v1", error, error_size);
}

int DATABASE_CRYPTO_set_key_path(const char *path, char *error, size_t error_size) {
    /*
        Purpose: Sets the key path
        Returns: Success status
    */

    char canonical[PATH_MAX];
    char auth_path[PATH_MAX];
    char data_path[PATH_MAX];
    unsigned char master[DATABASE_CRYPTO_MASTER_BYTES];
    int success = 0;

    if (error && error_size > 0) {

        error[0] = '\0';

    }
    memset(master, 0, sizeof(master));

    if (!path || path[0] != '/' || !realpath(path, canonical)) {

        database_crypto_error(error, error_size, "Enter an absolute path to an existing 32-byte database key file.");
        goto cleanup;

    }

    if (!database_crypto_load_key_file(canonical, master, error, error_size) ||
        !database_crypto_path(auth_path, sizeof(auth_path), DATABASE_CRYPTO_AUTH_FILENAME) ||
        !database_crypto_path(data_path, sizeof(data_path), DATABASE_CRYPTO_DATA_FILENAME) ||
        !database_crypto_key_opens_database(auth_path, "RetroSpectrum SQLCipher auth database key v1", master, error,
                                            error_size) ||
        !database_crypto_key_opens_database(data_path, "RetroSpectrum SQLCipher operational database key v1", master,
                                            error, error_size) ||
        !database_crypto_write_saved_key_path(canonical, error, error_size)) {

        goto cleanup;

    }

    pthread_mutex_lock(&Global_Database_Crypto_Key_Lock);

    if (!sec_strcpy(Global_Database_Crypto_Key_Path, sizeof(Global_Database_Crypto_Key_Path), canonical)) {

        pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);

        database_crypto_error(error, error_size, "The selected database key-file path is too long.");

        goto cleanup;

    }

    pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);

    database_crypto_cache_master(master);
    success = 1;

cleanup:
    OPENSSL_cleanse(master, sizeof(master));
    return success;
}

int DATABASE_CRYPTO_is_unlocked(void) {
    /*
        Purpose: Checks whether database encryption is unlocked
        Returns: Boolean status
    */

    int unlocked;
    pthread_mutex_lock(&Global_Database_Crypto_Key_Lock);
    unlocked = Global_Database_Crypto_Master_Loaded;
    pthread_mutex_unlock(&Global_Database_Crypto_Key_Lock);
    return unlocked;
}

const char *DATABASE_CRYPTO_key_path(void) {
    /*
        Purpose: Builds the key path
        Returns: Text pointer
    */

    char path[PATH_MAX];
    (void)database_crypto_resolve_key_path(path, sizeof(path), NULL, 0);
    return Global_Database_Crypto_Key_Path;
}
