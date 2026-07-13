#define _POSIX_C_SOURCE 200809L

#include "DataStore.h"
#include "DatabaseCrypto.h"
#include "SecureNetwork.h"

#include <errno.h>
#include <limits.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DATASTORE_MAX_DOCUMENT_BYTES (64u * 1024u * 1024u)
#define DATASTORE_SHA512_BYTES 64

static void datastore_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }
    snprintf(error, error_size, "%s", message ? message : "Unknown data-store error");
}

static int datastore_copy_text(char *dst, size_t dst_size, const char *src) {
    int written;

    if (!dst || dst_size == 0) {
        return 0;
    }

    written = snprintf(dst, dst_size, "%s", src ? src : "");
    return written >= 0 && (size_t)written < dst_size;
}

static int datastore_is_valid_kind(const char *kind) {
    return kind &&
           (strcmp(kind, DATASTORE_KIND_CASE_MANAGEMENT) == 0 ||
            strcmp(kind, DATASTORE_KIND_CLASSIFICATION) == 0);
}

static int datastore_make_directory(const char *path, mode_t mode) {
    struct stat st;

    if (!path || !path[0]) {
        return 0;
    }

    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            return 0;
        }
        chmod(path, mode);
        return 1;
    }

    if (mkdir(path, mode) == 0) {
        return 1;
    }

    return errno == EEXIST;
}

static int datastore_config_directory(char *path, size_t path_size) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char base[PATH_MAX];

    if (!path || path_size == 0) {
        return 0;
    }

    if (xdg && xdg[0]) {
        if (!datastore_copy_text(base, sizeof(base), xdg) || !datastore_make_directory(base, 0700)) {
            return 0;
        }
    } else {
        int written;

        if (!home || !home[0]) {
            return 0;
        }
        written = snprintf(base, sizeof(base), "%s/.config", home);
        if (written < 0 || (size_t)written >= sizeof(base) || !datastore_make_directory(base, 0700)) {
            return 0;
        }
    }

    {
        int written = snprintf(path, path_size, "%s/retrospectrum", base);
        if (written < 0 || (size_t)written >= path_size) {
            return 0;
        }
    }

    return datastore_make_directory(path, 0700);
}

int DATASTORE_get_path(char *path, size_t path_size) {
    char directory[PATH_MAX];
    int written;

    if (!path || path_size == 0 || !datastore_config_directory(directory, sizeof(directory))) {
        return 0;
    }

    written = snprintf(path, path_size, "%s/retrospectrum_data.db", directory);
    return written >= 0 && (size_t)written < path_size;
}

static int datastore_execute(sqlite3 *database, const char *sql, char *error, size_t error_size) {
    char *sqlite_error = NULL;
    int result = sqlite3_exec(database, sql, NULL, NULL, &sqlite_error);

    if (result != SQLITE_OK) {
        datastore_set_error(error, error_size, sqlite_error ? sqlite_error : sqlite3_errmsg(database));
        sqlite3_free(sqlite_error);
        return 0;
    }

    return 1;
}

static int datastore_open(sqlite3 **database, char *error, size_t error_size) {
    char path[PATH_MAX];

    if (!database || !DATABASE_CRYPTO_open_data(database, path, sizeof(path), error, error_size)) {
        if (error && error_size > 0 && error[0] == '\0') {
            datastore_set_error(error, error_size, "Unable to open encrypted data database.");
        }
        return 0;
    }

    if (!datastore_execute(*database, "PRAGMA foreign_keys=ON;", error, error_size) ||
        !datastore_execute(*database, "PRAGMA journal_mode=DELETE;", error, error_size) ||
        !datastore_execute(*database, "PRAGMA synchronous=FULL;", error, error_size) ||
        !datastore_execute(*database, "PRAGMA secure_delete=ON;", error, error_size) ||
        !datastore_execute(*database, "PRAGMA temp_store=MEMORY;", error, error_size) ||
        !datastore_execute(*database,
                           "CREATE TABLE IF NOT EXISTS stored_documents ("
                           "document_kind TEXT NOT NULL,"
                           "document_name TEXT NOT NULL,"
                           "case_number TEXT NOT NULL DEFAULT '',"
                           "content BLOB NOT NULL,"
                           "content_sha512 BLOB NOT NULL CHECK(length(content_sha512)=64),"
                           "created_at INTEGER NOT NULL,"
                           "updated_at INTEGER NOT NULL,"
                           "PRIMARY KEY(document_kind, document_name)"
                           ");",
                           error, error_size) ||
        !datastore_execute(*database,
                           "CREATE INDEX IF NOT EXISTS idx_stored_documents_case "
                           "ON stored_documents(document_kind, case_number);",
                           error, error_size)) {
        sqlite3_close(*database);
        *database = NULL;
        return 0;
    }

    return 1;
}

static int datastore_sha512(const unsigned char *content,
                            size_t content_size,
                            unsigned char digest[DATASTORE_SHA512_BYTES]) {
    unsigned int digest_size = 0;
    static const unsigned char empty = 0;
    const unsigned char *input = content_size > 0 ? content : &empty;

    return EVP_Digest(input, content_size, digest, &digest_size, EVP_sha512(), NULL) == 1 &&
           digest_size == DATASTORE_SHA512_BYTES;
}

int DATASTORE_server_save_content(const char *document_kind,
                           const char *document_name,
                           const char *case_number,
                           const void *content,
                           size_t content_size,
                           char *error,
                           size_t error_size) {
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    unsigned char digest[DATASTORE_SHA512_BYTES];
    static const unsigned char empty = 0;
    const unsigned char *bytes = content_size > 0 ? (const unsigned char *)content : &empty;
    int success = 0;

    memset(digest, 0, sizeof(digest));

    if (!datastore_is_valid_kind(document_kind) || !document_name || !document_name[0] ||
        (content_size > 0 && !content)) {
        datastore_set_error(error, error_size, "Invalid document save request.");
        return 0;
    }
    if (content_size > DATASTORE_MAX_DOCUMENT_BYTES) {
        datastore_set_error(error, error_size, "Document exceeds the 64 MiB database limit.");
        return 0;
    }
    if (!datastore_sha512(bytes, content_size, digest)) {
        datastore_set_error(error, error_size, "Unable to calculate the document SHA-512 digest.");
        return 0;
    }
    if (!datastore_open(&database, error, error_size) ||
        !datastore_execute(database, "BEGIN IMMEDIATE;", error, error_size)) {
        goto cleanup;
    }

    if (sqlite3_prepare_v2(
            database,
            "INSERT INTO stored_documents("
            "document_kind,document_name,case_number,content,content_sha512,created_at,updated_at"
            ") VALUES(?1,?2,?3,?4,?5,unixepoch(),unixepoch()) "
            "ON CONFLICT(document_kind,document_name) DO UPDATE SET "
            "case_number=excluded.case_number,"
            "content=excluded.content,"
            "content_sha512=excluded.content_sha512,"
            "updated_at=unixepoch();",
            -1, &statement, NULL) != SQLITE_OK) {
        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        datastore_execute(database, "ROLLBACK;", NULL, 0);
        goto cleanup;
    }

    sqlite3_bind_text(statement, 1, document_kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, document_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, case_number ? case_number : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob64(statement, 4, bytes, (sqlite3_uint64)content_size, SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 5, digest, sizeof(digest), SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE) {
        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        sqlite3_finalize(statement);
        statement = NULL;
        datastore_execute(database, "ROLLBACK;", NULL, 0);
        goto cleanup;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    if (!datastore_execute(database, "COMMIT;", error, error_size)) {
        datastore_execute(database, "ROLLBACK;", NULL, 0);
        goto cleanup;
    }

    success = 1;

cleanup:
    if (statement) {
        sqlite3_finalize(statement);
    }
    if (database) {
        sqlite3_close(database);
    }
    OPENSSL_cleanse(digest, sizeof(digest));
    return success;
}

int DATASTORE_server_load_content(const char *document_kind,
                           const char *document_name,
                           unsigned char **content,
                           size_t *content_size,
                           int *found,
                           char *error,
                           size_t error_size) {
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    const unsigned char *stored_content;
    const unsigned char *stored_digest;
    unsigned char calculated_digest[DATASTORE_SHA512_BYTES];
    unsigned char *copy = NULL;
    int stored_content_size;
    int stored_digest_size;
    int success = 0;

    memset(calculated_digest, 0, sizeof(calculated_digest));

    if (content) {
        *content = NULL;
    }
    if (content_size) {
        *content_size = 0;
    }
    if (found) {
        *found = 0;
    }

    if (!datastore_is_valid_kind(document_kind) || !document_name || !document_name[0] ||
        !content || !content_size) {
        datastore_set_error(error, error_size, "Invalid document load request.");
        return 0;
    }
    if (!datastore_open(&database, error, error_size)) {
        return 0;
    }

    if (sqlite3_prepare_v2(database,
                           "SELECT content,content_sha512 FROM stored_documents "
                           "WHERE document_kind=?1 AND document_name=?2;",
                           -1, &statement, NULL) != SQLITE_OK) {
        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;
    }

    sqlite3_bind_text(statement, 1, document_kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, document_name, -1, SQLITE_TRANSIENT);

    {
        int step_result = sqlite3_step(statement);
        if (step_result == SQLITE_DONE) {
            success = 1;
            goto cleanup;
        }
        if (step_result != SQLITE_ROW) {
            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            goto cleanup;
        }
    }

    stored_content = sqlite3_column_blob(statement, 0);
    stored_content_size = sqlite3_column_bytes(statement, 0);
    stored_digest = sqlite3_column_blob(statement, 1);
    stored_digest_size = sqlite3_column_bytes(statement, 1);

    if (stored_content_size < 0 || (unsigned int)stored_content_size > DATASTORE_MAX_DOCUMENT_BYTES ||
        stored_digest_size != DATASTORE_SHA512_BYTES ||
        (!stored_content && stored_content_size > 0) || !stored_digest) {
        datastore_set_error(error, error_size, "Stored document is malformed.");
        goto cleanup;
    }

    copy = malloc((size_t)stored_content_size + 1u);
    if (!copy) {
        datastore_set_error(error, error_size, "Out of memory while loading document.");
        goto cleanup;
    }
    if (stored_content_size > 0) {
        memcpy(copy, stored_content, (size_t)stored_content_size);
    }
    copy[stored_content_size] = '\0';

    if (!datastore_sha512(copy, (size_t)stored_content_size, calculated_digest) ||
        CRYPTO_memcmp(calculated_digest, stored_digest, DATASTORE_SHA512_BYTES) != 0) {
        datastore_set_error(error, error_size, "Stored document failed SHA-512 integrity verification.");
        goto cleanup;
    }

    *content = copy;
    *content_size = (size_t)stored_content_size;
    copy = NULL;
    if (found) {
        *found = 1;
    }
    success = 1;

cleanup:
    if (statement) {
        sqlite3_finalize(statement);
    }
    if (database) {
        sqlite3_close(database);
    }
    if (copy) {
        DATASTORE_free_content(copy, (size_t)(stored_content_size > 0 ? stored_content_size : 0));
    }
    OPENSSL_cleanse(calculated_digest, sizeof(calculated_digest));
    return success;
}

void DATASTORE_free_content(unsigned char *content, size_t content_size) {
    if (!content) {
        return;
    }
    OPENSSL_cleanse(content, content_size + 1u);
    free(content);
}

int DATASTORE_server_list_documents(const char *document_kind,
                             Type_DataStore_Document_Summary *documents,
                             size_t capacity,
                             size_t *count,
                             char *error,
                             size_t error_size) {
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    size_t used = 0;

    if (count) {
        *count = 0;
    }

    if (!datastore_is_valid_kind(document_kind) || !documents || capacity == 0 || !count) {
        datastore_set_error(error, error_size, "Invalid document-list request.");
        return 0;
    }
    if (!datastore_open(&database, error, error_size)) {
        return 0;
    }

    if (sqlite3_prepare_v2(database,
                           "SELECT document_name,case_number,updated_at FROM stored_documents "
                           "WHERE document_kind=?1 ORDER BY document_name COLLATE NOCASE;",
                           -1, &statement, NULL) != SQLITE_OK) {
        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        sqlite3_close(database);
        return 0;
    }

    sqlite3_bind_text(statement, 1, document_kind, -1, SQLITE_TRANSIENT);

    while (used < capacity) {
        int step_result = sqlite3_step(statement);

        if (step_result == SQLITE_DONE) {
            break;
        }
        if (step_result != SQLITE_ROW) {
            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            sqlite3_finalize(statement);
            sqlite3_close(database);
            return 0;
        }

        {
            const unsigned char *name = sqlite3_column_text(statement, 0);
            const unsigned char *case_number = sqlite3_column_text(statement, 1);

            datastore_copy_text(documents[used].document_name, sizeof(documents[used].document_name),
                                name ? (const char *)name : "");
            datastore_copy_text(documents[used].case_number, sizeof(documents[used].case_number),
                                case_number ? (const char *)case_number : "");
            documents[used].updated_at = (long long)sqlite3_column_int64(statement, 2);
            used++;
        }
    }

    sqlite3_finalize(statement);
    sqlite3_close(database);
    *count = used;
    return 1;
}


int DATASTORE_save_content(const char *document_kind,
                           const char *document_name,
                           const char *case_number,
                           const void *content,
                           size_t content_size,
                           char *error,
                           size_t error_size) {
    if (SECURE_NETWORK_is_authenticated_remote()) {
        return SECURE_NETWORK_save_document(document_kind, document_name, case_number,
                                            content, content_size, error, error_size);
    }
    return DATASTORE_server_save_content(document_kind, document_name, case_number,
                                         content, content_size, error, error_size);
}

int DATASTORE_load_content(const char *document_kind,
                           const char *document_name,
                           unsigned char **content,
                           size_t *content_size,
                           int *found,
                           char *error,
                           size_t error_size) {
    if (SECURE_NETWORK_is_authenticated_remote()) {
        return SECURE_NETWORK_load_document(document_kind, document_name, content,
                                            content_size, found, error, error_size);
    }
    return DATASTORE_server_load_content(document_kind, document_name, content,
                                         content_size, found, error, error_size);
}

int DATASTORE_list_documents(const char *document_kind,
                             Type_DataStore_Document_Summary *documents,
                             size_t capacity,
                             size_t *count,
                             char *error,
                             size_t error_size) {
    if (SECURE_NETWORK_is_authenticated_remote()) {
        return SECURE_NETWORK_list_documents(document_kind, documents, capacity,
                                             count, error, error_size);
    }
    return DATASTORE_server_list_documents(document_kind, documents, capacity,
                                           count, error, error_size);
}
