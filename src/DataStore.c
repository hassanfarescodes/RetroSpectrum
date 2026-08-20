#define _POSIX_C_SOURCE 200809L

/*
 * ============================================================================
 * File:            DataStore.c
 * Author:          Hassan Fares
 *
 * Description:     Persistent application data storage logic for RetroSpectrum
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux
 *
 *                                                               05/04/2026
 * ============================================================================
 */

#include "DataStore.h"
#include "DatabaseCrypto.h"
#include "SecureFunctions.h"
#include "SecureNetwork.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

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
#define DATASTORE_MAX_CASE_IMAGE_BYTES (16u * 1024u * 1024u)
#define DATASTORE_SHA512_BYTES 64
#define DATASTORE_CASE_IMAGE_KIND "case_image"
#define DATASTORE_CASE_COLOR_KIND "case_color"
#define DATASTORE_CLASSIFICATION_FIELD_COUNT 13
#define DATASTORE_CLASSIFICATION_FIELD_BYTES 4096

static const char DATASTORE_CLASSIFICATION_HEADER[] = "case_number,signal_name,frequency_mhz,bandwidth,start_time,"
                                                      "end_time,calculated_modulation,signal_class,country,latitude,"
                                                      "longitude,notes,file_name\n";

static void datastore_set_error(char *error, size_t error_size, const char *message) {
    /*
        Purpose: Sets the error
        Returns: No value
    */

    if (!error || error_size == 0) {

        return;

    }
    (void)sec_strcpy(error, error_size, message ? message : "Unknown data-store error");
}

static int datastore_copy_text(char *dst, size_t dst_size, const char *src) {
    /*
        Purpose: Copies the text
        Returns: Success status
    */

    return sec_strcpy(dst, dst_size, src ? src : "") ? 1 : 0;
}

static int datastore_is_valid_kind(const char *kind) {
    /*
        Purpose: Checks whether the valid is kind
        Returns: Boolean status
    */

    return kind &&
           (strcmp(kind, DATASTORE_KIND_CASE_MANAGEMENT) == 0 || strcmp(kind, DATASTORE_KIND_CLASSIFICATION) == 0 ||
            strcmp(kind, "correlation") == 0 || strcmp(kind, "bitstream_classifier") == 0 ||
            strcmp(kind, DATASTORE_CASE_IMAGE_KIND) == 0 || strcmp(kind, DATASTORE_CASE_COLOR_KIND) == 0);
}

static int datastore_validate_document_fields(const char *document_name, const char *case_number) {
    /*
        Purpose: Validates bounded user-controlled document identifiers
        Returns: Boolean status
    */

    char checked_name[DATASTORE_DOCUMENT_NAME_MAX];
    char checked_case[DATASTORE_CASE_NUMBER_MAX];

    if (!document_name || !document_name[0] || !sec_strcpy(checked_name, sizeof(checked_name), document_name)) {

        return 0;

    }

    if (case_number && !sec_strcpy(checked_case, sizeof(checked_case), case_number)) {

        return 0;

    }

    return 1;
}

static int datastore_is_supported_case_image(const unsigned char *content, size_t content_size) {
    /*
        Purpose: Validates that a case image can be safely decoded within supported bounds
        Returns: Boolean status
    */

    SDL_RWops *source;
    SDL_Surface *surface;
    int valid = 0;

    if (!content || content_size == 0 || content_size > DATASTORE_MAX_CASE_IMAGE_BYTES || content_size > INT_MAX) {

        return 0;

    }

    source = SDL_RWFromConstMem(content, (int)content_size);

    if (!source) {

        return 0;

    }

    surface = IMG_Load_RW(source, 1);

    if (!surface) {

        return 0;

    }

    if (surface->w <= 0 || surface->h <= 0 || surface->w > 16384 || surface->h > 16384) {

        SDL_FreeSurface(surface);
        return 0;

    }

    valid = 1;

    SDL_FreeSurface(surface);
    return valid;
}

static int datastore_make_directory(const char *path, mode_t mode) {
    /*
        Purpose: Builds the directory
        Returns: Success status
    */

    if (!path || !path[0]) {

        return 0;

    }

    return sec_ensure_private_directory(path, mode) ? 1 : 0;
}

static int datastore_config_directory(char *path, size_t path_size) {
    /*
        Purpose: Gets the data store configuration directory
        Returns: Success status
    */

    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char base[PATH_MAX];

    if (!path || path_size == 0) {

        return 0;

    }

    if (xdg && xdg[0]) {

        if (xdg[0] != '/' || !sec_strcpy(base, sizeof(base), xdg) || !datastore_make_directory(base, 0700)) {

            return 0;

        }

    }

    else {

        if (!home || !home[0] || home[0] != '/' || !sec_sprintf(base, sizeof(base), "%s/.config", home) ||
            !datastore_make_directory(base, 0700)) {

            return 0;

        }

    }

    if (!sec_sprintf(path, path_size, "%s/retrospectrum", base)) {

        return 0;

    }

    return datastore_make_directory(path, 0700);
}

int DATASTORE_get_path(char *path, size_t path_size) {
    /*
        Purpose: Gets the path
        Returns: Success status
    */

    char directory[PATH_MAX];

    if (!path || path_size == 0 || !datastore_config_directory(directory, sizeof(directory))) {

        return 0;

    }

    return sec_sprintf(path, path_size, "%s/retrospectrum_data.db", directory) ? 1 : 0;
}

static int datastore_sha512(const unsigned char *content, size_t content_size,
                            unsigned char digest[DATASTORE_SHA512_BYTES]);

static int datastore_execute(sqlite3 *database, const char *sql, char *error, size_t error_size) {
    /*
        Purpose: Executes the requested operation
        Returns: Success status
    */

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
    /*
        Purpose: Opens the requested operation
        Returns: Success status
    */

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
                           error, error_size) ||
        !datastore_execute(*database,
                           "CREATE TABLE IF NOT EXISTS classification_sets ("
                           "document_name TEXT PRIMARY KEY,"
                           "case_number TEXT NOT NULL DEFAULT '',"
                           "created_at INTEGER NOT NULL,"
                           "updated_at INTEGER NOT NULL"
                           ");",
                           error, error_size) ||
        !datastore_execute(*database,
                           "CREATE TABLE IF NOT EXISTS classification_records ("
                           "record_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "document_name TEXT NOT NULL,"
                           "row_order INTEGER NOT NULL,"
                           "case_number TEXT NOT NULL DEFAULT '',"
                           "signal_name TEXT NOT NULL DEFAULT '',"
                           "frequency_mhz TEXT NOT NULL DEFAULT '',"
                           "bandwidth TEXT NOT NULL DEFAULT '',"
                           "start_time TEXT NOT NULL DEFAULT '',"
                           "end_time TEXT NOT NULL DEFAULT '',"
                           "calculated_modulation TEXT NOT NULL DEFAULT '',"
                           "signal_class TEXT NOT NULL DEFAULT '',"
                           "country TEXT NOT NULL DEFAULT '',"
                           "latitude TEXT NOT NULL DEFAULT '',"
                           "longitude TEXT NOT NULL DEFAULT '',"
                           "notes TEXT NOT NULL DEFAULT '',"
                           "file_name TEXT NOT NULL DEFAULT '',"
                           "created_at INTEGER NOT NULL,"
                           "updated_at INTEGER NOT NULL,"
                           "UNIQUE(document_name, row_order),"
                           "FOREIGN KEY(document_name) REFERENCES classification_sets(document_name) "
                           "ON DELETE CASCADE"
                           ");",
                           error, error_size) ||
        !datastore_execute(*database,
                           "CREATE INDEX IF NOT EXISTS idx_classification_records_case "
                           "ON classification_records(case_number);",
                           error, error_size) ||
        !datastore_execute(*database,
                           "CREATE INDEX IF NOT EXISTS idx_classification_records_document "
                           "ON classification_records(document_name, row_order);",
                           error, error_size)) {

        sqlite3_close(*database);
        *database = NULL;
        return 0;

    }

    return 1;
}

static int datastore_sha512(const unsigned char *content, size_t content_size,
                            unsigned char digest[DATASTORE_SHA512_BYTES]) {
    /*
        Purpose: Calculates a SHA-512 content digest
        Returns: Success status
    */

    unsigned int digest_size = 0;
    static const unsigned char empty = 0;
    const unsigned char *input = content_size > 0 ? content : &empty;

    return EVP_Digest(input, content_size, digest, &digest_size, EVP_sha512(), NULL) == 1 &&
           digest_size == DATASTORE_SHA512_BYTES;
}

static int datastore_classification_parse_line(
    const unsigned char *line, size_t line_size,
    char fields[DATASTORE_CLASSIFICATION_FIELD_COUNT][DATASTORE_CLASSIFICATION_FIELD_BYTES], int *field_count) {
    /*
        Purpose: Parses one compatibility classification row
        Returns: Success status
    */

    size_t position = 0;
    int count = 0;

    if (!line || !fields || !field_count) {

        return 0;

    }

    memset(fields, 0, DATASTORE_CLASSIFICATION_FIELD_COUNT * DATASTORE_CLASSIFICATION_FIELD_BYTES);
    *field_count = 0;

    while (count < DATASTORE_CLASSIFICATION_FIELD_COUNT) {
        size_t written = 0;
        int had_separator = 0;

        if (position > line_size) {

            break;

        }

        if (position == line_size && count > 0 && line[position - 1] != ',') {

            break;

        }

        if (position < line_size && line[position] == '"') {

            position++;

            while (position < line_size) {

                if (line[position] == '"') {

                    if (position + 1 < line_size && line[position + 1] == '"') {

                        if (written + 1 >= DATASTORE_CLASSIFICATION_FIELD_BYTES) {

                            return 0;

                        }
                        fields[count][written++] = '"';
                        position += 2;
                        continue;

                    }

                    position++;
                    break;

                }

                if (written + 1 >= DATASTORE_CLASSIFICATION_FIELD_BYTES) {

                    return 0;

                }
                fields[count][written++] = (char)line[position++];
            }

            while (position < line_size && line[position] != ',') {

                if (line[position] != ' ' && line[position] != '\t' && line[position] != '\r') {

                    return 0;

                }
                position++;
            }

        }

        else {

            while (position < line_size && line[position] != ',') {

                if (line[position] != '\r' && line[position] != '\n') {

                    if (written + 1 >= DATASTORE_CLASSIFICATION_FIELD_BYTES) {

                        return 0;

                    }
                    fields[count][written++] = (char)line[position];

                }
                position++;
            }

        }

        fields[count][written] = '\0';
        count++;

        if (position < line_size && line[position] == ',') {

            position++;
            had_separator = 1;

        }

        if (!had_separator) {

            break;

        }
    }

    *field_count = count;
    return 1;
}

static void datastore_classification_write_field(FILE *stream, const char *text) {
    /*
        Purpose: Writes one compatibility field for existing workstation readers
        Returns: No value
    */

    const unsigned char *cursor = (const unsigned char *)(text ? text : "");

    fputc('"', stream);

    while (*cursor) {

        if (*cursor == '"') {

            fputc('"', stream);
            fputc('"', stream);

        }

        else if (*cursor == '\n') {

            fputc('\\', stream);
            fputc('n', stream);

        }

        else if (*cursor == '\r') {

            fputc('\\', stream);
            fputc('r', stream);

        }

        else {

            fputc(*cursor, stream);

        }
        cursor++;
    }

    fputc('"', stream);
}

static int datastore_classification_replace_in_transaction(sqlite3 *database, const char *document_name,
                                                           const char *case_number, const unsigned char *content,
                                                           size_t content_size, sqlite3_int64 created_at,
                                                           sqlite3_int64 updated_at, char *error, size_t error_size) {
    /*
        Purpose: Replaces one logical case classification with structured SQL rows
        Returns: Success status
    */

    sqlite3_stmt *set_statement = NULL;
    sqlite3_stmt *delete_statement = NULL;
    sqlite3_stmt *insert_statement = NULL;
    size_t offset = 0;
    int first_line = 1;
    int row_order = 0;
    int success = 0;
    char resolved_case[DATASTORE_CASE_NUMBER_MAX] = "";

    if (!database || !datastore_validate_document_fields(document_name, case_number) ||
        (content_size > 0 && !content)) {

        datastore_set_error(error, error_size, "Invalid structured classification save request.");
        return 0;

    }

    if (!datastore_copy_text(resolved_case, sizeof(resolved_case), case_number ? case_number : "")) {

        datastore_set_error(error, error_size, "Classification case number exceeds the supported size.");
        return 0;

    }

    if (sqlite3_prepare_v2(database,
                           "INSERT INTO classification_sets(document_name,case_number,created_at,updated_at) "
                           "VALUES(?1,?2,CASE WHEN ?3>0 THEN ?3 ELSE unixepoch() END,"
                           "CASE WHEN ?4>0 THEN ?4 ELSE unixepoch() END) "
                           "ON CONFLICT(document_name) DO UPDATE SET "
                           "case_number=excluded.case_number,updated_at=excluded.updated_at;",
                           -1, &set_statement, NULL) != SQLITE_OK) {

        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    sqlite3_bind_text(set_statement, 1, document_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(set_statement, 2, resolved_case, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(set_statement, 3, created_at);
    sqlite3_bind_int64(set_statement, 4, updated_at);

    if (sqlite3_step(set_statement) != SQLITE_DONE) {

        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    sqlite3_finalize(set_statement);
    set_statement = NULL;

    if (sqlite3_prepare_v2(database, "DELETE FROM classification_records WHERE document_name=?1;", -1,
                           &delete_statement, NULL) != SQLITE_OK) {

        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    sqlite3_bind_text(delete_statement, 1, document_name, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(delete_statement) != SQLITE_DONE) {

        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    sqlite3_finalize(delete_statement);
    delete_statement = NULL;

    if (sqlite3_prepare_v2(database,
                           "INSERT INTO classification_records("
                           "document_name,row_order,case_number,signal_name,frequency_mhz,bandwidth,start_time,"
                           "end_time,calculated_modulation,signal_class,country,latitude,longitude,notes,file_name,"
                           "created_at,updated_at) "
                           "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,"
                           "CASE WHEN ?16>0 THEN ?16 ELSE unixepoch() END,"
                           "CASE WHEN ?17>0 THEN ?17 ELSE unixepoch() END);",
                           -1, &insert_statement, NULL) != SQLITE_OK) {

        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    while (offset < content_size) {
        size_t line_start = offset;
        size_t line_size;
        char fields[DATASTORE_CLASSIFICATION_FIELD_COUNT][DATASTORE_CLASSIFICATION_FIELD_BYTES];
        int field_count = 0;
        int is_header = 0;

        while (offset < content_size && content[offset] != '\n') {
            offset++;
        }

        line_size = offset - line_start;

        if (line_size > 0 && content[line_start + line_size - 1] == '\r') {

            line_size--;

        }

        if (offset < content_size && content[offset] == '\n') {

            offset++;

        }

        if (line_size == 0) {

            first_line = 0;
            continue;

        }

        if (!datastore_classification_parse_line(content + line_start, line_size, fields, &field_count)) {

            datastore_set_error(error, error_size, "A classification field exceeds the supported database size.");
            goto cleanup;

        }

        if (first_line && field_count > 0 && strcmp(fields[0], "case_number") == 0) {

            is_header = 1;

        }
        first_line = 0;

        if (is_header) {

            continue;

        }

        if (field_count < 12) {

            datastore_set_error(error, error_size, "A classification record is malformed.");
            goto cleanup;

        }

        if (!resolved_case[0] && fields[0][0]) {

            if (!datastore_copy_text(resolved_case, sizeof(resolved_case), fields[0])) {

                datastore_set_error(error, error_size, "Classification case number exceeds the supported size.");

                goto cleanup;

            }

        }

        sqlite3_reset(insert_statement);
        sqlite3_clear_bindings(insert_statement);
        sqlite3_bind_text(insert_statement, 1, document_name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_statement, 2, row_order);

        for (int field = 0; field < DATASTORE_CLASSIFICATION_FIELD_COUNT; field++) {
            sqlite3_bind_text(insert_statement, field + 3, fields[field], -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_int64(insert_statement, 16, created_at);
        sqlite3_bind_int64(insert_statement, 17, updated_at);

        if (sqlite3_step(insert_statement) != SQLITE_DONE) {

            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            goto cleanup;

        }

        row_order++;
    }

    if (resolved_case[0] && (!case_number || !case_number[0])) {

        sqlite3_stmt *update_statement = NULL;

        if (sqlite3_prepare_v2(database, "UPDATE classification_sets SET case_number=?1 WHERE document_name=?2;", -1,
                               &update_statement, NULL) != SQLITE_OK) {

            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            goto cleanup;

        }

        sqlite3_bind_text(update_statement, 1, resolved_case, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(update_statement, 2, document_name, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(update_statement) != SQLITE_DONE) {

            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            sqlite3_finalize(update_statement);
            goto cleanup;

        }
        sqlite3_finalize(update_statement);

    }

    success = 1;

cleanup:

    if (set_statement) {

        sqlite3_finalize(set_statement);

    }

    if (delete_statement) {

        sqlite3_finalize(delete_statement);

    }

    if (insert_statement) {

        sqlite3_finalize(insert_statement);

    }

    return success;
}

static int datastore_classification_load(sqlite3 *database, const char *document_name, unsigned char **content,
                                         size_t *content_size, int *found, char *error, size_t error_size) {
    /*
        Purpose: Rebuilds the existing in-memory view from structured SQL rows
        Returns: Success status
    */

    sqlite3_stmt *statement = NULL;
    FILE *stream = NULL;
    char *buffer = NULL;
    size_t buffer_size = 0;
    int success = 0;

    if (found) {

        *found = 0;

    }

    if (sqlite3_prepare_v2(database, "SELECT 1 FROM classification_sets WHERE document_name=?1;", -1, &statement,
                           NULL) != SQLITE_OK) {

        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    sqlite3_bind_text(statement, 1, document_name, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_ROW) {

        sqlite3_finalize(statement);
        statement = NULL;
        success = 1;
        goto cleanup;

    }

    sqlite3_finalize(statement);
    statement = NULL;

    stream = open_memstream(&buffer, &buffer_size);

    if (!stream) {

        datastore_set_error(error, error_size, "Unable to build classification data in memory.");
        goto cleanup;

    }

    if (fwrite(DATASTORE_CLASSIFICATION_HEADER, 1, sizeof(DATASTORE_CLASSIFICATION_HEADER) - 1u, stream) !=
        sizeof(DATASTORE_CLASSIFICATION_HEADER) - 1u) {

        datastore_set_error(error, error_size, "Unable to build classification data in memory.");
        goto cleanup;

    }

    if (sqlite3_prepare_v2(database,
                           "SELECT case_number,signal_name,frequency_mhz,bandwidth,start_time,end_time,"
                           "calculated_modulation,signal_class,country,latitude,longitude,notes,file_name "
                           "FROM classification_records WHERE document_name=?1 ORDER BY row_order;",
                           -1, &statement, NULL) != SQLITE_OK) {

        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        goto cleanup;

    }

    sqlite3_bind_text(statement, 1, document_name, -1, SQLITE_TRANSIENT);

    for (;;) {
        int step_result = sqlite3_step(statement);

        if (step_result == SQLITE_DONE) {

            break;

        }

        if (step_result != SQLITE_ROW) {

            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            goto cleanup;

        }

        for (int field = 0; field < DATASTORE_CLASSIFICATION_FIELD_COUNT; field++) {
            const unsigned char *value = sqlite3_column_text(statement, field);

            if (field > 0) {

                fputc(',', stream);

            }
            datastore_classification_write_field(stream, value ? (const char *)value : "");
        }
        fputc('\n', stream);

        if (ferror(stream)) {

            datastore_set_error(error, error_size, "Unable to build classification data in memory.");
            goto cleanup;

        }
    }

    sqlite3_finalize(statement);
    statement = NULL;

    if (fclose(stream) != 0) {

        stream = NULL;
        datastore_set_error(error, error_size, "Unable to finalize classification data.");
        goto cleanup;

    }
    stream = NULL;

    if (buffer_size > DATASTORE_MAX_DOCUMENT_BYTES) {

        datastore_set_error(error, error_size, "Classification data exceeds the 64 MiB transfer limit.");
        goto cleanup;

    }

    *content = (unsigned char *)buffer;
    *content_size = buffer_size;
    buffer = NULL;

    if (found) {

        *found = 1;

    }
    success = 1;

cleanup:

    if (statement) {

        sqlite3_finalize(statement);

    }

    if (stream) {

        fclose(stream);

    }

    if (buffer) {

        OPENSSL_cleanse(buffer, buffer_size + 1u);
        free(buffer);

    }

    return success;
}

int DATASTORE_server_save_content(const char *document_kind, const char *document_name, const char *case_number,
                                  const void *content, size_t content_size, char *error, size_t error_size) {
    /*
        Purpose: Saves the server content
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    unsigned char digest[DATASTORE_SHA512_BYTES];
    static const unsigned char empty = 0;
    const unsigned char *bytes = content_size > 0 ? (const unsigned char *)content : &empty;
    int success = 0;

    memset(digest, 0, sizeof(digest));

    if (!datastore_is_valid_kind(document_kind) || !datastore_validate_document_fields(document_name, case_number) ||
        (content_size > 0 && !content)) {

        datastore_set_error(error, error_size, "Invalid document save request.");
        return 0;

    }

    if (content_size > DATASTORE_MAX_DOCUMENT_BYTES) {

        datastore_set_error(error, error_size, "Document exceeds the 64 MiB database limit.");
        return 0;

    }

    if (strcmp(document_kind, DATASTORE_CASE_IMAGE_KIND) == 0 &&
        !datastore_is_supported_case_image(bytes, content_size)) {

        datastore_set_error(error, error_size,
                            "Case image must be a supported image no larger than 16 MiB with valid dimensions.");
        return 0;

    }

    if (strcmp(document_kind, DATASTORE_KIND_CLASSIFICATION) == 0) {

        if (!datastore_open(&database, error, error_size) ||
            !datastore_execute(database, "BEGIN IMMEDIATE;", error, error_size)) {

            goto cleanup;

        }

        if (!datastore_classification_replace_in_transaction(database, document_name, case_number ? case_number : "",
                                                             bytes, content_size, 0, 0, error, error_size) ||
            !datastore_execute(database, "COMMIT;", error, error_size)) {

            datastore_execute(database, "ROLLBACK;", NULL, 0);
            goto cleanup;

        }

        success = 1;
        goto cleanup;

    }

    if (!datastore_sha512(bytes, content_size, digest)) {

        datastore_set_error(error, error_size, "Unable to calculate the document SHA-512 digest.");
        return 0;

    }

    if (!datastore_open(&database, error, error_size) ||
        !datastore_execute(database, "BEGIN IMMEDIATE;", error, error_size)) {

        goto cleanup;

    }

    if (sqlite3_prepare_v2(database,
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

int DATASTORE_server_load_content(const char *document_kind, const char *document_name, unsigned char **content,
                                  size_t *content_size, int *found, char *error, size_t error_size) {
    /*
        Purpose: Loads the server content
        Returns: Success status
    */

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

    if (!datastore_is_valid_kind(document_kind) || !datastore_validate_document_fields(document_name, NULL) ||
        !content || !content_size) {

        datastore_set_error(error, error_size, "Invalid document load request.");
        return 0;

    }

    if (!datastore_open(&database, error, error_size)) {

        return 0;

    }

    if (strcmp(document_kind, DATASTORE_KIND_CLASSIFICATION) == 0) {

        success =
            datastore_classification_load(database, document_name, content, content_size, found, error, error_size);
        goto cleanup;

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
        stored_digest_size != DATASTORE_SHA512_BYTES || (!stored_content && stored_content_size > 0) ||
        !stored_digest) {

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
    /*
        Purpose: Frees the content
        Returns: No value
    */

    if (!content) {

        return;

    }
    OPENSSL_cleanse(content, content_size + 1u);
    free(content);
}

int DATASTORE_server_list_documents(const char *document_kind, Type_DataStore_Document_Summary *documents,
                                    size_t capacity, size_t *count, char *error, size_t error_size) {
    /*
        Purpose: Lists the server documents
        Returns: Success status
    */

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

    if (strcmp(document_kind, DATASTORE_KIND_CLASSIFICATION) == 0) {

        if (sqlite3_prepare_v2(database,
                               "SELECT document_name,case_number,updated_at FROM classification_sets "
                               "ORDER BY document_name COLLATE NOCASE;",
                               -1, &statement, NULL) != SQLITE_OK) {

            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            sqlite3_close(database);
            return 0;

        }

    }

    else {

        if (sqlite3_prepare_v2(database,
                               "SELECT document_name,case_number,updated_at FROM stored_documents "
                               "WHERE document_kind=?1 ORDER BY document_name COLLATE NOCASE;",
                               -1, &statement, NULL) != SQLITE_OK) {

            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            sqlite3_close(database);
            return 0;

        }

        sqlite3_bind_text(statement, 1, document_kind, -1, SQLITE_TRANSIENT);

    }

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

            if (!datastore_copy_text(documents[used].document_name, sizeof(documents[used].document_name),
                                     name ? (const char *)name : "") ||
                !datastore_copy_text(documents[used].case_number, sizeof(documents[used].case_number),
                                     case_number ? (const char *)case_number : "")) {

                datastore_set_error(error, error_size, "Stored document identifiers exceed supported sizes.");

                sqlite3_finalize(statement);
                sqlite3_close(database);
                return 0;

            }

            documents[used].updated_at = (long long)sqlite3_column_int64(statement, 2);
            used++;
        }
    }

    sqlite3_finalize(statement);
    sqlite3_close(database);
    *count = used;
    return 1;
}

int DATASTORE_server_delete_content(const char *document_kind, const char *document_name, int *deleted, char *error,
                                    size_t error_size) {
    /*
        Purpose: Deletes the server content
        Returns: Success status
    */

    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int success = 0;

    if (deleted) {

        *deleted = 0;

    }

    if (!datastore_is_valid_kind(document_kind) || !datastore_validate_document_fields(document_name, NULL) ||
        !deleted) {

        datastore_set_error(error, error_size, "Invalid document delete request.");
        return 0;

    }

    if (!datastore_open(&database, error, error_size) ||
        !datastore_execute(database, "BEGIN IMMEDIATE;", error, error_size)) {

        goto cleanup;

    }

    if (strcmp(document_kind, DATASTORE_KIND_CLASSIFICATION) == 0) {

        if (sqlite3_prepare_v2(database, "DELETE FROM classification_sets WHERE document_name=?1;", -1, &statement,
                               NULL) != SQLITE_OK) {

            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            datastore_execute(database, "ROLLBACK;", NULL, 0);
            goto cleanup;

        }

        sqlite3_bind_text(statement, 1, document_name, -1, SQLITE_TRANSIENT);

    }

    else {

        if (sqlite3_prepare_v2(database, "DELETE FROM stored_documents WHERE document_kind=?1 AND document_name=?2;",
                               -1, &statement, NULL) != SQLITE_OK) {

            datastore_set_error(error, error_size, sqlite3_errmsg(database));
            datastore_execute(database, "ROLLBACK;", NULL, 0);
            goto cleanup;

        }

        sqlite3_bind_text(statement, 1, document_kind, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, document_name, -1, SQLITE_TRANSIENT);

    }

    if (sqlite3_step(statement) != SQLITE_DONE) {

        datastore_set_error(error, error_size, sqlite3_errmsg(database));
        sqlite3_finalize(statement);
        statement = NULL;
        datastore_execute(database, "ROLLBACK;", NULL, 0);
        goto cleanup;

    }

    *deleted = sqlite3_changes(database) > 0;
    sqlite3_finalize(statement);
    statement = NULL;

    if (!datastore_execute(database, "COMMIT;", error, error_size)) {

        *deleted = 0;
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
    return success;
}

int DATASTORE_save_content(const char *document_kind, const char *document_name, const char *case_number,
                           const void *content, size_t content_size, char *error, size_t error_size) {
    /*
        Purpose: Saves the content
        Returns: Success status
    */

    if (SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_save_document(document_kind, document_name, case_number, content, content_size, error,
                                            error_size);

    }
    return DATASTORE_server_save_content(document_kind, document_name, case_number, content, content_size, error,
                                         error_size);
}

int DATASTORE_load_content(const char *document_kind, const char *document_name, unsigned char **content,
                           size_t *content_size, int *found, char *error, size_t error_size) {
    /*
        Purpose: Loads the content
        Returns: Success status
    */

    if (SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_load_document(document_kind, document_name, content, content_size, found, error,
                                            error_size);

    }
    return DATASTORE_server_load_content(document_kind, document_name, content, content_size, found, error, error_size);
}

int DATASTORE_list_documents(const char *document_kind, Type_DataStore_Document_Summary *documents, size_t capacity,
                             size_t *count, char *error, size_t error_size) {
    /*
        Purpose: Lists the documents
        Returns: Success status
    */

    if (SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_list_documents(document_kind, documents, capacity, count, error, error_size);

    }
    return DATASTORE_server_list_documents(document_kind, documents, capacity, count, error, error_size);
}

int DATASTORE_delete_content(const char *document_kind, const char *document_name, int *deleted, char *error,
                             size_t error_size) {
    /*
        Purpose: Deletes the content
        Returns: Success status
    */

    if (SECURE_NETWORK_is_authenticated_remote()) {

        return SECURE_NETWORK_delete_document(document_kind, document_name, deleted, error, error_size);

    }
    return DATASTORE_server_delete_content(document_kind, document_name, deleted, error, error_size);
}
