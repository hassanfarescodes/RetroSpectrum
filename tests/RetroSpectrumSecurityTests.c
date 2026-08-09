/*
 * RetroSpectrum adversarial security test harness.
 *
 * This file is compiled repeatedly by run_retrospectrum_security_tests.sh.
 * Exactly one production .c translation unit is included for each build, which
 * exposes static functions to target-specific runtime probes without modifying
 * production source.
 *
 * Security coverage is intentionally two-layered:
 *   1. Every discovered function body in the active production .c file is
 *      attacked by a set of source-level security checks. This guarantees
 *      per-function PASS/FAIL output even for GUI/render/thread functions that
 *      cannot safely be called with arbitrary invalid arguments.
 *   2. Functions with deterministic, safely testable interfaces receive
 *      additional runtime hostile-input probes (oversized input, injection,
 *      malformed encodings, traversal, truncation, integer boundaries, etc.).
 *
 * Run under ASan/UBSan/FORTIFY/stack-protector via the companion shell script.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef RS_SECURITY_TARGET
#error "Compile with -DRS_SECURITY_TARGET=<1..16>."
#endif

static int rs_sec_passed = 0;
static int rs_sec_failed = 0;
static int rs_sec_skipped = 0;
static int rs_sec_quiet = 0;
static char rs_sec_temp_dir[PATH_MAX];
static const char *rs_sec_project_root = ".";

#define RS_SEC_GREEN "\033[92m"
#define RS_SEC_RED "\033[91m"
#define RS_SEC_YELLOW "\033[93m"
#define RS_SEC_CYAN "\033[96m"
#define RS_SEC_RESET "\033[0m"

static void rs_sec_pass(const char *name) {
    rs_sec_passed++;
    if (!rs_sec_quiet) printf(RS_SEC_GREEN "PASSED" RS_SEC_RESET " %s\n", name);
}

static void rs_sec_fail(const char *name, const char *file, int line) {
    rs_sec_failed++;
    printf(RS_SEC_RED "FAILED" RS_SEC_RESET " %s (%s:%d)\n", name, file, line);
}

static void rs_sec_skip(const char *name, const char *reason) {
    rs_sec_skipped++;
    printf(RS_SEC_YELLOW "SKIPPED" RS_SEC_RESET " %s — %s\n", name, reason ? reason : "unavailable");
}

#define RS_ATTACK(name, expression) \
    do { \
        if ((expression)) rs_sec_pass((name)); \
        else rs_sec_fail((name), __FILE__, __LINE__); \
    } while (0)

#define RS_SKIP(name, reason) rs_sec_skip((name), (reason))

static int rs_sec_write_bytes(const char *path, const void *data, size_t size, mode_t mode) {
    const unsigned char *cursor = (const unsigned char *)data;
    size_t remaining = size;
    mode_t previous_mask;
    int fd;

    if (!path || (size > 0 && !data)) return 0;
    previous_mask = umask(0077);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
    umask(previous_mask);
    if (fd < 0) return 0;

    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return 0;
        }
        if (written == 0) {
            close(fd);
            return 0;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (fsync(fd) != 0) {
        close(fd);
        return 0;
    }
    return close(fd) == 0;
}

static char *rs_sec_read_file(const char *path, size_t *out_size) {
    FILE *fp;
    long length;
    char *buffer;

    if (out_size) *out_size = 0;
    if (!path) return NULL;
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    length = ftell(fp);
    if (length < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buffer = (char *)calloc((size_t)length + 1U, 1U);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    if ((size_t)length > 0 && fread(buffer, 1, (size_t)length, fp) != (size_t)length) {
        free(buffer);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    if (out_size) *out_size = (size_t)length;
    return buffer;
}

/* Replace comments, string literals, and character literals with spaces while
 * preserving newlines and punctuation needed to locate function bodies. */
static char *rs_sec_sanitize_c_source(const char *source, size_t size) {
    char *clean = (char *)malloc(size + 1U);
    size_t i = 0;
    int state = 0; /* 0 normal, 1 line comment, 2 block comment, 3 string, 4 char */

    if (!clean || !source) {
        free(clean);
        return NULL;
    }
    memcpy(clean, source, size);
    clean[size] = '\0';

    while (i < size) {
        char c = source[i];
        char n = i + 1U < size ? source[i + 1U] : '\0';

        if (state == 0) {
            if (c == '/' && n == '/') {
                clean[i++] = ' ';
                clean[i++] = ' ';
                state = 1;
                continue;
            }
            if (c == '/' && n == '*') {
                clean[i++] = ' ';
                clean[i++] = ' ';
                state = 2;
                continue;
            }
            if (c == '"') {
                clean[i] = ' ';
                state = 3;
            } else if (c == '\'') {
                clean[i] = ' ';
                state = 4;
            }
            i++;
            continue;
        }

        if (state == 1) {
            if (c == '\n') {
                state = 0;
            } else {
                clean[i] = ' ';
            }
            i++;
            continue;
        }

        if (state == 2) {
            if (c == '*' && n == '/') {
                clean[i++] = ' ';
                clean[i++] = ' ';
                state = 0;
            } else {
                if (c != '\n') clean[i] = ' ';
                i++;
            }
            continue;
        }

        if (state == 3 || state == 4) {
            char terminator = state == 3 ? '"' : '\'';
            if (c == '\\' && i + 1U < size) {
                clean[i++] = ' ';
                if (source[i] != '\n') clean[i] = ' ';
                i++;
                continue;
            }
            if (c == terminator) {
                clean[i] = ' ';
                state = 0;
            } else if (c != '\n') {
                clean[i] = ' ';
            }
            i++;
        }
    }
    return clean;
}

typedef struct Rs_Security_Function {
    char name[160];
    size_t header_start;
    size_t body_start;
    size_t body_end;
    int line;
} Rs_Security_Function;

static int rs_sec_keyword_name(const char *name) {
    static const char *keywords[] = {
        "if", "for", "while", "switch", "sizeof", "return", "_Static_assert", NULL
    };
    for (size_t i = 0; keywords[i]; i++) {
        if (strcmp(name, keywords[i]) == 0) return 1;
    }
    return 0;
}

static int rs_sec_extract_function_name(const char *clean, size_t open_paren, char *name, size_t name_size) {
    size_t end = open_paren;
    size_t start;
    size_t length;

    if (!clean || !name || name_size == 0 || open_paren == 0) return 0;
    while (end > 0 && isspace((unsigned char)clean[end - 1U])) end--;
    start = end;
    while (start > 0 && (isalnum((unsigned char)clean[start - 1U]) || clean[start - 1U] == '_')) start--;
    if (start == end) return 0;
    length = end - start;
    if (length >= name_size) return 0;
    memcpy(name, clean + start, length);
    name[length] = '\0';
    return !rs_sec_keyword_name(name);
}

static size_t rs_sec_matching_open_paren(const char *clean, size_t close_paren) {
    int depth = 1;
    size_t i = close_paren;
    while (i > 0) {
        i--;
        if (clean[i] == ')') depth++;
        else if (clean[i] == '(' && --depth == 0) return i;
    }
    return SIZE_MAX;
}

static size_t rs_sec_matching_close_brace(const char *clean, size_t size, size_t open_brace) {
    int depth = 1;
    for (size_t i = open_brace + 1U; i < size; i++) {
        if (clean[i] == '{') depth++;
        else if (clean[i] == '}' && --depth == 0) return i;
    }
    return SIZE_MAX;
}

static int rs_sec_line_number(const char *source, size_t offset) {
    int line = 1;
    for (size_t i = 0; i < offset; i++) if (source[i] == '\n') line++;
    return line;
}

static size_t rs_sec_header_start(const char *clean, size_t open_paren) {
    size_t i = open_paren;
    while (i > 0) {
        char c = clean[i - 1U];
        if (c == ';' || c == '}' || c == '{') break;
        i--;
    }
    while (clean[i] && isspace((unsigned char)clean[i])) i++;
    return i;
}

static int rs_sec_discover_functions(const char *source, const char *clean, size_t size,
                                     Rs_Security_Function **out_functions, size_t *out_count) {
    Rs_Security_Function *functions = NULL;
    size_t capacity = 0;
    size_t count = 0;
    int top_depth = 0;

    if (!source || !clean || !out_functions || !out_count) return 0;
    *out_functions = NULL;
    *out_count = 0;

    for (size_t i = 0; i < size; i++) {
        if (clean[i] == '{') {
            if (top_depth == 0) {
                size_t p = i;
                size_t close_paren;
                size_t open_paren;
                size_t close_brace;
                char name[160];

                while (p > 0 && isspace((unsigned char)clean[p - 1U])) p--;
                if (p == 0 || clean[p - 1U] != ')') {
                    top_depth++;
                    continue;
                }
                close_paren = p - 1U;
                open_paren = rs_sec_matching_open_paren(clean, close_paren);
                if (open_paren == SIZE_MAX ||
                    !rs_sec_extract_function_name(clean, open_paren, name, sizeof(name))) {
                    top_depth++;
                    continue;
                }
                close_brace = rs_sec_matching_close_brace(clean, size, i);
                if (close_brace == SIZE_MAX) return 0;

                if (count == capacity) {
                    size_t next = capacity ? capacity * 2U : 128U;
                    Rs_Security_Function *grown = (Rs_Security_Function *)realloc(functions, next * sizeof(*grown));
                    if (!grown) {
                        free(functions);
                        return 0;
                    }
                    functions = grown;
                    capacity = next;
                }
                memset(&functions[count], 0, sizeof(functions[count]));
                snprintf(functions[count].name, sizeof(functions[count].name), "%s", name);
                functions[count].header_start = rs_sec_header_start(clean, open_paren);
                functions[count].body_start = i;
                functions[count].body_end = close_brace;
                functions[count].line = rs_sec_line_number(source, functions[count].header_start);
                count++;
                i = close_brace;
                continue;
            }
            top_depth++;
        } else if (clean[i] == '}' && top_depth > 0) {
            top_depth--;
        }
    }

    *out_functions = functions;
    *out_count = count;
    return 1;
}

static int rs_sec_range_contains(const char *text, size_t start, size_t end, const char *needle) {
    size_t needle_len;
    if (!text || !needle || start > end) return 0;
    needle_len = strlen(needle);
    if (needle_len == 0 || end - start + 1U < needle_len) return 0;
    for (size_t i = start; i + needle_len <= end + 1U; i++) {
        if (memcmp(text + i, needle, needle_len) == 0) return 1;
    }
    return 0;
}


static int rs_sec_range_contains_call(const char *text, size_t start, size_t end, const char *needle) {
    size_t needle_len;
    if (!text || !needle || start > end) return 0;
    needle_len = strlen(needle);
    if (needle_len == 0 || end - start + 1U < needle_len) return 0;
    for (size_t i = start; i + needle_len <= end + 1U; i++) {
        if (memcmp(text + i, needle, needle_len) == 0) {
            if (i > start) {
                unsigned char previous = (unsigned char)text[i - 1U];
                if (isalnum(previous) || previous == '_') continue;
            }
            return 1;
        }
    }
    return 0;
}

static int rs_sec_range_contains_any_call(const char *text, size_t start, size_t end, const char *const needles[]) {
    for (size_t i = 0; needles[i]; i++) {
        if (rs_sec_range_contains_call(text, start, end, needles[i])) return 1;
    }
    return 0;
}

static int rs_sec_range_contains_any(const char *text, size_t start, size_t end, const char *const needles[]) {
    for (size_t i = 0; needles[i]; i++) {
        if (rs_sec_range_contains(text, start, end, needles[i])) return 1;
    }
    return 0;
}

static void rs_sec_function_result(const Rs_Security_Function *function, const char *attack, int passed) {
    char label[384];
    snprintf(label, sizeof(label), "%s:%d %s — %s", function->name, function->line, attack,
             passed ? "blocked" : "exposed");
    if (passed) rs_sec_pass(label);
    else rs_sec_fail(label, __FILE__, __LINE__);
}

static void rs_sec_scan_one_function(const char *clean, const Rs_Security_Function *function) {
    static const char *const unsafe_copy[] = {
        "strcpy(", "strcat(", "strncpy(", "strncat(", "gets(", NULL
    };
    static const char *const unbounded_format[] = {
        "sprintf(", "vsprintf(", NULL
    };
    static const char *const shell_exec[] = {
        "system(", "popen(", "execl(", "execlp(", "execle(", "execv(", "execvp(", "execve(", NULL
    };
    static const char *const unsafe_temp[] = {
        "tmpnam(", "tempnam(", "mktemp(", NULL
    };
    static const char *const weak_rng[] = {
        "rand(", "random(", "drand48(", NULL
    };
    size_t start = function->body_start;
    size_t end = function->body_end;
    int has_create = rs_sec_range_contains(clean, start, end, "O_CREAT");
    int has_nofollow = rs_sec_range_contains(clean, start, end, "O_NOFOLLOW");
    int approved_exec_wrapper = strcmp(function->name, "sec_popen_read") == 0;

    rs_sec_function_result(function, "unsafe string/input API attack",
                           !rs_sec_range_contains_any_call(clean, start, end, unsafe_copy));
    rs_sec_function_result(function, "unbounded format-write attack",
                           !rs_sec_range_contains_any_call(clean, start, end, unbounded_format));
    rs_sec_function_result(function, "shell-command execution sink attack",
                           approved_exec_wrapper || !rs_sec_range_contains_any_call(clean, start, end, shell_exec));
    rs_sec_function_result(function, "predictable temporary-file attack",
                           !rs_sec_range_contains_any_call(clean, start, end, unsafe_temp));
    rs_sec_function_result(function, "symlink file-creation attack", !has_create || has_nofollow);
    rs_sec_function_result(function, "weak PRNG attack",
                           !rs_sec_range_contains_any_call(clean, start, end, weak_rng));
}

static int rs_sec_scan_every_function(const char *target_name) {
    char path[PATH_MAX];
    char *source = NULL;
    char *clean = NULL;
    Rs_Security_Function *functions = NULL;
    size_t size = 0;
    size_t count = 0;
    int ok = 0;

    if (snprintf(path, sizeof(path), "%s/src/%s", rs_sec_project_root, target_name) >= (int)sizeof(path)) {
        rs_sec_fail("source scan path construction", __FILE__, __LINE__);
        return 0;
    }
    source = rs_sec_read_file(path, &size);
    if (!source) {
        rs_sec_fail("read active production source for per-function attacks", __FILE__, __LINE__);
        goto cleanup;
    }
    clean = rs_sec_sanitize_c_source(source, size);
    if (!clean || !rs_sec_discover_functions(source, clean, size, &functions, &count)) {
        rs_sec_fail("discover all production function bodies", __FILE__, __LINE__);
        goto cleanup;
    }

    printf("\n" RS_SEC_CYAN "=== Per-function source attacks: %s (%zu functions discovered) ===" RS_SEC_RESET "\n",
           target_name, count);
    RS_ATTACK("at least one production function discovered", count > 0);
    for (size_t i = 0; i < count; i++) rs_sec_scan_one_function(clean, &functions[i]);
    ok = 1;

cleanup:
    free(functions);
    free(clean);
    free(source);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Target 1: AnalysisWorkstation.c                                           */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 1
#include "../src/AnalysisWorkstation.c"
static const char *rs_sec_target_name = "AnalysisWorkstation.c";
static void rs_sec_runtime_attacks(void) {
    char output[64];
    char tiny[4] = {'X','X','X','X'};
    uint64_t uvalue = 0;
    int64_t svalue = 0;

    ANALYSIS_signal_sanitize_output_filename("../../../../etc/passwd", output, sizeof(output));
    RS_ATTACK("analysis traversal filename strips directory components",
              strchr(output, '/') == NULL && strchr(output, '\\') == NULL && output[0] != '\0');
    ANALYSIS_signal_sanitize_output_filename("AAAAAAAAAAAAAAAAAAAAAAAA", tiny, sizeof(tiny));
    RS_ATTACK("analysis tiny filename buffer remains terminated", tiny[sizeof(tiny)-1] == '\0');

    snprintf(Global_Analysis_Transmit_Field_Text[0], sizeof(Global_Analysis_Transmit_Field_Text[0]), "%s",
             "18446744073709551616");
    RS_ATTACK("analysis rejects unsigned integer overflow",
              !ANALYSIS_parse_transmit_integer(0, "Frequency", 1, UINT64_MAX, &uvalue));
    snprintf(Global_Analysis_Transmit_Field_Text[0], sizeof(Global_Analysis_Transmit_Field_Text[0]), "%s", "12;rm -rf /");
    RS_ATTACK("analysis rejects command-injection text in numeric field",
              !ANALYSIS_parse_transmit_integer(0, "Frequency", 1, UINT64_MAX, &uvalue));
    snprintf(Global_Analysis_Transmit_Field_Text[3], sizeof(Global_Analysis_Transmit_Field_Text[3]), "%s", "999999999999999999999");
    RS_ATTACK("analysis rejects signed integer overflow",
              !ANALYSIS_parse_transmit_signed_integer(3, "Gain", -50, 50, &svalue));
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 2: AuthAdmin.c                                                     */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 2
#include "../src/AuthAdmin.c"
static const char *rs_sec_target_name = "AuthAdmin.c";
static void rs_sec_runtime_attacks(void) {
    char small[8] = "";
    char append[8] = "12";
    char long_user[AUTH_PUBLIC_USERNAME_MAX + 16];

    memset(long_user, 'A', sizeof(long_user));
    long_user[sizeof(long_user)-1] = '\0';
    RS_ATTACK("admin rejects NULL username", !admin_username_valid(NULL));
    RS_ATTACK("admin rejects overlong username", !admin_username_valid(long_user));
    RS_ATTACK("admin rejects SQL/shell punctuation username", !admin_username_valid("admin';--"));
    RS_ATTACK("admin rejects path username", !admin_username_valid("../admin"));
    admin_copy(small, sizeof(small), "AAAAAAAAAAAAAAAAAAAAAAAA");
    RS_ATTACK("admin bounded copy preserves terminator on overflow", small[sizeof(small)-1] == '\0');
    admin_append(append, sizeof(append), "3x4;5", 1);
    RS_ATTACK("admin digit-only append filters hostile characters", strcmp(append, "12345") == 0);
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 3: AuthScreen.c                                                    */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 3
#include "../src/AuthScreen.c"
static const char *rs_sec_target_name = "AuthScreen.c";
static void rs_sec_runtime_attacks(void) {
    char copied[8] = "";
    char long_user[AUTH_PUBLIC_USERNAME_MAX + 32];
    memset(long_user, 'B', sizeof(long_user));
    long_user[sizeof(long_user)-1] = '\0';
    RS_ATTACK("auth rejects NULL username", !auth_username_valid(NULL));
    RS_ATTACK("auth rejects overlong username", !auth_username_valid(long_user));
    RS_ATTACK("auth rejects newline username", !auth_username_valid("user\nadmin"));
    RS_ATTACK("auth rejects SQL metacharacters username", !auth_username_valid("user'OR'1"));
    auth_copy_text(copied, sizeof(copied), "0123456789abcdef");
    RS_ATTACK("auth bounded copy remains terminated", copied[sizeof(copied)-1] == '\0');
    RS_ATTACK("auth rejects non-Argon2id password record", !auth_verify_argon2id("password", "legacy-pbkdf2-record"));
    RS_ATTACK("auth rejects NULL Argon2id record", !auth_verify_argon2id("password", NULL));
    RS_ATTACK("auth modern TOTP digest accepts SHA-512", auth_digest_from_name(AUTH_TOTP_ALGORITHM_DEFAULT) != NULL);
    RS_ATTACK("auth modern TOTP digest rejects SHA-1 legacy algorithm", auth_digest_from_name("SHA1") == NULL);
    RS_ATTACK("auth modern TOTP digest rejects SHA-256 legacy algorithm", auth_digest_from_name("SHA256") == NULL);


    {
        char encoded[AUTH_ARGON2_ENCODED_MAX] = "";
        int hashed = auth_hash_password_argon2id("Security-Test-Password-2026!", encoded);

        RS_ATTACK("auth Argon2id hashing produces a modern record",
                  hashed && strncmp(encoded, "$argon2id$", 10) == 0);
        RS_ATTACK("auth Argon2id verifies the correct password",
                  hashed && auth_verify_argon2id("Security-Test-Password-2026!", encoded));
        RS_ATTACK("auth Argon2id rejects a wrong password",
                  hashed && !auth_verify_argon2id("Security-Test-Password-2026?", encoded));
    }

    {
        unsigned char secret[AUTH_TOTP_SECRET_BYTES];
        unsigned char salt[AUTH_TOTP_SALT_BYTES];
        unsigned char nonce[AUTH_TOTP_NONCE_BYTES];
        unsigned char tag[AUTH_TOTP_TAG_BYTES];
        unsigned char ciphertext[AUTH_TOTP_CIPHER_BYTES];
        unsigned char recovered[AUTH_TOTP_SECRET_BYTES];
        Type_Auth_User_Record record;
        int encrypted;

        for (size_t i = 0; i < sizeof(secret); i++) secret[i] = (unsigned char)(0x31U + i);
        memset(salt, 0, sizeof(salt));
        memset(nonce, 0, sizeof(nonce));
        memset(tag, 0, sizeof(tag));
        memset(ciphertext, 0, sizeof(ciphertext));
        memset(recovered, 0, sizeof(recovered));
        memset(&record, 0, sizeof(record));

        encrypted = auth_encrypt_totp_secret("security_user", secret, AUTH_TOTP_SECRET_BYTES,
                                             salt, nonce, tag, ciphertext);
        RS_ATTACK("auth AES-256-GCM wraps TOTP secret", encrypted);

        if (encrypted) {
            record.totp_enabled = 1;
            record.totp_secret_bytes = AUTH_TOTP_SECRET_BYTES;
            auth_copy_text(record.totp_algorithm, sizeof(record.totp_algorithm), AUTH_TOTP_ALGORITHM_DEFAULT);
            auth_copy_text(record.totp_kdf_algorithm, sizeof(record.totp_kdf_algorithm), AUTH_TOTP_KDF_DEFAULT);
            memcpy(record.totp_salt, salt, sizeof(salt));
            memcpy(record.totp_nonce, nonce, sizeof(nonce));
            memcpy(record.totp_tag, tag, sizeof(tag));
            memcpy(record.totp_ciphertext, ciphertext, AUTH_TOTP_SECRET_BYTES);

            RS_ATTACK("auth AES-256-GCM unwrap round-trip preserves TOTP secret",
                      auth_decrypt_totp_secret("security_user", &record, recovered) &&
                      CRYPTO_memcmp(secret, recovered, sizeof(secret)) == 0);

            memset(recovered, 0xA5, sizeof(recovered));
            record.totp_tag[0] ^= 0x01U;
            RS_ATTACK("auth TOTP unwrap rejects tampered GCM tag",
                      !auth_decrypt_totp_secret("security_user", &record, recovered));
            RS_ATTACK("auth failed TOTP unwrap zeroizes destination after tag tamper",
                      ({ int zero = 1; for (size_t i = 0; i < sizeof(recovered); i++) if (recovered[i] != 0) zero = 0; zero; }));
            record.totp_tag[0] ^= 0x01U;

            memset(recovered, 0xA5, sizeof(recovered));
            record.totp_ciphertext[0] ^= 0x80U;
            RS_ATTACK("auth TOTP unwrap rejects tampered ciphertext",
                      !auth_decrypt_totp_secret("security_user", &record, recovered));
            record.totp_ciphertext[0] ^= 0x80U;

            memset(recovered, 0xA5, sizeof(recovered));
            RS_ATTACK("auth TOTP wrapping key is bound to username",
                      !auth_decrypt_totp_secret("different_user", &record, recovered));
        }

        OPENSSL_cleanse(secret, sizeof(secret));
        OPENSSL_cleanse(salt, sizeof(salt));
        OPENSSL_cleanse(nonce, sizeof(nonce));
        OPENSSL_cleanse(tag, sizeof(tag));
        OPENSSL_cleanse(ciphertext, sizeof(ciphertext));
        OPENSSL_cleanse(recovered, sizeof(recovered));
        OPENSSL_cleanse(&record, sizeof(record));
    }
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 4: CaseManagementWorkstation.c                                     */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 4
#include "../src/CaseManagementWorkstation.c"
static const char *rs_sec_target_name = "CaseManagementWorkstation.c";
static void rs_sec_runtime_attacks(void) {
    char normalized[128];
    char trim[64] = " \t\r\nCase Alpha\r\n ";
    int month = 0, day = 0, year = 0;

    case_normalize_file_name("../../../../tmp/evil.case.csv", normalized, sizeof(normalized));
    RS_ATTACK("case management strips path traversal from save name",
              strchr(normalized, '/') == NULL && strchr(normalized, '\\') == NULL);
    case_normalize_file_name("", normalized, sizeof(normalized));
    RS_ATTACK("case management supplies safe default filename", normalized[0] != '\0');
    case_trim_text(trim);
    RS_ATTACK("case management trim removes control-edge whitespace", strcmp(trim, "Case Alpha") == 0);
    RS_ATTACK("case management rejects impossible calendar date", !case_parse_mmddyyyy("02/31/2026", &month, &day, &year));
    RS_ATTACK("case management rejects injected date", !case_parse_mmddyyyy("01/01/2026;DROP", &month, &day, &year));
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 5: ClassificationWorkstation.c                                     */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 5
#include "../src/ClassificationWorkstation.c"
static const char *rs_sec_target_name = "ClassificationWorkstation.c";
static void rs_sec_runtime_attacks(void) {
    char output[128];
    char trim[64] = " \tSignal Name\r\n";
    double frequency = 0.0, bandwidth = 0.0, start_time = 0.0, end_time = 0.0;

    CLASSIFICATION_make_filename_safe("../../../../etc/shadow", output, sizeof(output));
    RS_ATTACK("classification sanitizes traversal filename",
              strchr(output, '/') == NULL && strchr(output, '\\') == NULL);
    CLASSIFICATION_make_filename_safe("bad:name*?<>|.complex16", output, sizeof(output));
    RS_ATTACK("classification removes filesystem metacharacters",
              strpbrk(output, ":*?<>|") == NULL);
    CLASSIFICATION_trim_text(trim);
    RS_ATTACK("classification trims hostile edge whitespace", strcmp(trim, "Signal Name") == 0);
    CLASSIFICATION_parse_file_metadata("../../bad.complex16", &frequency, &bandwidth, &start_time, &end_time);
    RS_ATTACK("classification malformed metadata does not create nonfinite values",
              isfinite(frequency) && isfinite(bandwidth) && isfinite(start_time) && isfinite(end_time));
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 6: CorrelationWorkstation.c                                        */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 6
#include "../src/CorrelationWorkstation.c"
static const char *rs_sec_target_name = "CorrelationWorkstation.c";
static void rs_sec_runtime_attacks(void) {
    double value = 0.0;
    int points = 0;

    RS_ATTACK("correlation rejects negative scalar", !correlation_parse_nonnegative_scalar("-0.1", &value));
    RS_ATTACK("correlation rejects NaN", !correlation_parse_nonnegative_scalar("nan", &value));
    RS_ATTACK("correlation rejects infinity", !correlation_parse_nonnegative_scalar("inf", &value));
    RS_ATTACK("correlation rejects numeric command injection", !correlation_parse_nonnegative_scalar("1.0;id", &value));
    RS_ATTACK("correlation rejects trend integer overflow",
              !correlation_parse_trend_points("999999999999999999999999", &points));
    RS_ATTACK("correlation rejects trend trailing payload", !correlation_parse_trend_points("64xyz", &points));
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 7: DataStore.c                                                     */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 7
#include "../src/DataStore.c"
static const char *rs_sec_target_name = "DataStore.c";
static void rs_sec_runtime_attacks(void) {
    char huge_name[DATASTORE_DOCUMENT_NAME_MAX + 32];
    char huge_case[DATASTORE_CASE_NUMBER_MAX + 32];
    char tiny[4] = "";
    unsigned char fake_image[32] = {0};

    memset(huge_name, 'N', sizeof(huge_name));
    huge_name[sizeof(huge_name)-1] = '\0';
    memset(huge_case, 'C', sizeof(huge_case));
    huge_case[sizeof(huge_case)-1] = '\0';

    RS_ATTACK("datastore rejects unknown document kind", !datastore_is_valid_kind("../../evil"));
    RS_ATTACK("datastore rejects empty document name", !datastore_validate_document_fields("", "case"));
    RS_ATTACK("datastore rejects oversized document name", !datastore_validate_document_fields(huge_name, "case"));
    RS_ATTACK("datastore rejects oversized case number", !datastore_validate_document_fields("doc", huge_case));
    RS_ATTACK("datastore rejects malformed case image", !datastore_is_supported_case_image(fake_image, sizeof(fake_image)));
    RS_ATTACK("datastore rejects NULL image with nonzero size", !datastore_is_supported_case_image(NULL, 1));
    RS_ATTACK("datastore bounded copy reports truncation", !datastore_copy_text(tiny, sizeof(tiny), "0123456789"));
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 8: DatabaseCrypto.c                                                */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 8
#include "../src/DatabaseCrypto.c"
static const char *rs_sec_target_name = "DatabaseCrypto.c";
static void rs_sec_runtime_attacks(void) {
    int major = 0, minor = 0, patch = 0;
    char path[PATH_MAX];
    static const unsigned char sqlite_header[] = "SQLite format 3\000";

    RS_ATTACK("database crypto rejects malformed SQLCipher version",
              !database_crypto_parse_version("4.x.1", &major, &minor, &patch));
    RS_ATTACK("database crypto rejects version trailing injection",
              !database_crypto_parse_version("4.6.1;DROP", &major, &minor, &patch));
    RS_ATTACK("database crypto rejects absurd version overflow",
              !database_crypto_parse_version("999999999999.6.1", &major, &minor, &patch));

    snprintf(path, sizeof(path), "%s/plaintext.sqlite", rs_sec_temp_dir);
    RS_ATTACK("database crypto create plaintext attack fixture",
              rs_sec_write_bytes(path, sqlite_header, sizeof(sqlite_header)-1U, 0600));
    RS_ATTACK("database crypto detects plaintext SQLite database", database_crypto_file_is_plaintext(path));


    {
        unsigned char master_a[DATABASE_CRYPTO_MASTER_BYTES];
        unsigned char master_b[DATABASE_CRYPTO_MASTER_BYTES];
        unsigned char key_a1[DATABASE_CRYPTO_DERIVED_BYTES];
        unsigned char key_a2[DATABASE_CRYPTO_DERIVED_BYTES];
        unsigned char key_domain[DATABASE_CRYPTO_DERIVED_BYTES];
        unsigned char key_master[DATABASE_CRYPTO_DERIVED_BYTES];
        char error[256] = "";

        for (size_t i = 0; i < sizeof(master_a); i++) master_a[i] = (unsigned char)(0x40U + i);
        memcpy(master_b, master_a, sizeof(master_b));
        master_b[0] ^= 0x01U;
        memset(key_a1, 0, sizeof(key_a1));
        memset(key_a2, 0, sizeof(key_a2));
        memset(key_domain, 0, sizeof(key_domain));
        memset(key_master, 0, sizeof(key_master));

        RS_ATTACK("database crypto HMAC-SHA512 derivation succeeds",
                  database_crypto_derive_from_master(master_a,
                      "RetroSpectrum SQLCipher auth database key v1", key_a1, error, sizeof(error)));
        RS_ATTACK("database crypto derivation is deterministic for same master/domain",
                  database_crypto_derive_from_master(master_a,
                      "RetroSpectrum SQLCipher auth database key v1", key_a2, error, sizeof(error)) &&
                  CRYPTO_memcmp(key_a1, key_a2, sizeof(key_a1)) == 0);
        RS_ATTACK("database crypto domain separation changes derived auth/data keys",
                  database_crypto_derive_from_master(master_a,
                      "RetroSpectrum SQLCipher operational database key v1", key_domain, error, sizeof(error)) &&
                  CRYPTO_memcmp(key_a1, key_domain, sizeof(key_a1)) != 0);
        RS_ATTACK("database crypto master-key bit change alters derived key",
                  database_crypto_derive_from_master(master_b,
                      "RetroSpectrum SQLCipher auth database key v1", key_master, error, sizeof(error)) &&
                  CRYPTO_memcmp(key_a1, key_master, sizeof(key_a1)) != 0);
        RS_ATTACK("database crypto derivation rejects NULL domain",
                  !database_crypto_derive_from_master(master_a, NULL, key_a2, error, sizeof(error)));

        OPENSSL_cleanse(master_a, sizeof(master_a));
        OPENSSL_cleanse(master_b, sizeof(master_b));
        OPENSSL_cleanse(key_a1, sizeof(key_a1));
        OPENSSL_cleanse(key_a2, sizeof(key_a2));
        OPENSSL_cleanse(key_domain, sizeof(key_domain));
        OPENSSL_cleanse(key_master, sizeof(key_master));
    }

    {
        unsigned char key_bytes[DATABASE_CRYPTO_MASTER_BYTES];
        unsigned char loaded[DATABASE_CRYPTO_MASTER_BYTES];
        char permissive_path[PATH_MAX];
        char short_path[PATH_MAX];
        char real_path[PATH_MAX];
        char link_path[PATH_MAX];
        char error[256] = "";
        struct stat st;
        int fd;

        memset(key_bytes, 0x5A, sizeof(key_bytes));
        memset(loaded, 0, sizeof(loaded));
        snprintf(permissive_path, sizeof(permissive_path), "%s/permissive-master.key", rs_sec_temp_dir);
        snprintf(short_path, sizeof(short_path), "%s/short-master.key", rs_sec_temp_dir);
        snprintf(real_path, sizeof(real_path), "%s/real-master.key", rs_sec_temp_dir);
        snprintf(link_path, sizeof(link_path), "%s/link-master.key", rs_sec_temp_dir);

        RS_ATTACK("database crypto creates key-permission fixture",
                  rs_sec_write_bytes(permissive_path, key_bytes, sizeof(key_bytes), 0600));
        chmod(permissive_path, 0644);
        fd = open(permissive_path, O_RDONLY | O_CLOEXEC);
        RS_ATTACK("database crypto accepts owner key file and restricts permissive mode",
                  fd >= 0 && database_crypto_validate_key_file(fd) &&
                  fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0600);
        if (fd >= 0) close(fd);

        RS_ATTACK("database crypto creates wrong-size key fixture",
                  rs_sec_write_bytes(short_path, key_bytes, sizeof(key_bytes) - 1U, 0600));
        fd = open(short_path, O_RDONLY | O_CLOEXEC);
        RS_ATTACK("database crypto rejects wrong-size master key",
                  fd >= 0 && !database_crypto_validate_key_file(fd));
        if (fd >= 0) close(fd);

        RS_ATTACK("database crypto creates symlink key fixture",
                  rs_sec_write_bytes(real_path, key_bytes, sizeof(key_bytes), 0600) &&
                  symlink(real_path, link_path) == 0);
        RS_ATTACK("database crypto key loader refuses symlink key path",
                  !database_crypto_load_key_file(link_path, loaded, error, sizeof(error)));
        RS_ATTACK("database crypto database-file validator refuses symlink path",
                  !database_crypto_validate_database_file(link_path, error, sizeof(error)));

        OPENSSL_cleanse(key_bytes, sizeof(key_bytes));
        OPENSSL_cleanse(loaded, sizeof(loaded));
    }
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 9: DecodeWorkstation.c                                             */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 9
#include "../src/DecodeWorkstation.c"
static const char *rs_sec_target_name = "DecodeWorkstation.c";
static void rs_sec_runtime_attacks(void) {
    int value = 0;
    char trimmed[32];

    RS_ATTACK("decode RGB rejects negative", !decode_classifier_parse_rgb_component("-1", &value));
    RS_ATTACK("decode RGB rejects >255", !decode_classifier_parse_rgb_component("256", &value));
    RS_ATTACK("decode RGB rejects trailing command payload", !decode_classifier_parse_rgb_component("10;id", &value));
    RS_ATTACK("decode RGB rejects integer overflow", !decode_classifier_parse_rgb_component("99999999999999999999", &value));
    decode_classifier_trim_text(" \t\r\nLabel Name\r\n ", trimmed, sizeof(trimmed));
    RS_ATTACK("decode classifier trim canonicalizes hostile whitespace", strcmp(trimmed, "Label Name") == 0);
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 10: GUIs.c                                                         */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 10
#include "../src/GUIs.c"
static const char *rs_sec_target_name = "GUIs.c";
static void rs_sec_runtime_attacks(void) {
    struct { char text[8]; unsigned char canary[8]; } guarded;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xA5, sizeof(guarded.canary));
    snprintf(guarded.text, sizeof(guarded.text), "%s", "1");
    append_text(guarded.text, sizeof(guarded.text), "2x3;4/5.6AAAA");
    RS_ATTACK("GUI numeric append filters hostile characters", strcmp(guarded.text, "12345.6") == 0);
    RS_ATTACK("GUI numeric append does not overwrite adjacent canary",
              guarded.canary[0] == 0xA5 && guarded.canary[7] == 0xA5);
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 11: MapDashboard.c                                                 */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 11
#include "../src/MapDashboard.c"
static const char *rs_sec_target_name = "MapDashboard.c";
static void rs_sec_runtime_attacks(void) {
    double value = 0.0;
    SDL_Color color;

    RS_ATTACK("dashboard coordinate rejects NaN", !dashboard_parse_coordinate("nan", -90.0, 90.0, &value));
    RS_ATTACK("dashboard coordinate rejects infinity", !dashboard_parse_coordinate("inf", -90.0, 90.0, &value));
    RS_ATTACK("dashboard coordinate rejects out-of-range latitude", !dashboard_parse_coordinate("91", -90.0, 90.0, &value));
    RS_ATTACK("dashboard coordinate rejects trailing injection", !dashboard_parse_coordinate("45;id", -90.0, 90.0, &value));
    RS_ATTACK("dashboard color rejects malformed text", !dashboard_case_color_parse("255,0,0;DROP", &color));
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 12: RetroSpectrum.c                                                */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 12
#define main retrospectrum_application_main
#include "../src/RetroSpectrum.c"
#undef main
static const char *rs_sec_target_name = "RetroSpectrum.c";
static void rs_sec_runtime_attacks(void) {
    double d = 0.0;
    int i = 0;
    char line[32] = "  value\r\n";

    RS_ATTACK("main numeric parser rejects NaN", !parse_positive_double("nan", &d));
    RS_ATTACK("main numeric parser rejects infinity", !parse_positive_double("inf", &d));
    RS_ATTACK("main numeric parser rejects command payload", !parse_positive_double("1.5;id", &d));
    RS_ATTACK("main integer parser rejects negative", !parse_nonnegative_int("-1", &i));
    RS_ATTACK("main integer parser rejects overflow", !parse_nonnegative_int("999999999999999999", &i));
    RS_ATTACK("CLI trim rejects embedded newline tail safely", cli_trim_line(line, sizeof(line)) && strcmp(line, "value") == 0);
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 13: SecureFunctions.c                                              */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 13
#include "../src/SecureFunctions.c"
static const char *rs_sec_target_name = "SecureFunctions.c";
static void rs_sec_runtime_attacks(void) {
    struct { char dst[8]; unsigned char canary[8]; } guarded;
    char cat[8] = "abc";
    char mem[8] = {0};
    int ivalue = 0;
    double dvalue = 0.0;
    size_t huge = SIZE_MAX;

    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0x5A, sizeof(guarded.canary));
    RS_ATTACK("secure strcpy rejects NULL destination", !sec_strcpy(NULL, 8, "x"));
    RS_ATTACK("secure strcpy rejects NULL source", !sec_strcpy(guarded.dst, sizeof(guarded.dst), NULL));
    RS_ATTACK("secure strcpy reports truncation", !sec_strcpy(guarded.dst, sizeof(guarded.dst), "0123456789ABCDEF"));
    RS_ATTACK("secure strcpy preserves terminator on truncation", guarded.dst[sizeof(guarded.dst)-1] == '\0');
    RS_ATTACK("secure strcpy preserves adjacent canary", guarded.canary[0] == 0x5A && guarded.canary[7] == 0x5A);

    RS_ATTACK("secure strcat rejects overflow", !sec_strcat(cat, sizeof(cat), "0123456789"));
    RS_ATTACK("secure strcat remains terminated", cat[sizeof(cat)-1] == '\0');
    RS_ATTACK("secure memcpy rejects oversized copy", !sec_memcpy(mem, sizeof(mem), "0123456789", 10));
    RS_ATTACK("secure memcpy rejects NULL source", !sec_memcpy(mem, sizeof(mem), NULL, 1));
    RS_ATTACK("secure memmove rejects oversized move", !sec_memmove(mem, sizeof(mem), "0123456789", 10));
    RS_ATTACK("secure str_memcpy rejects missing terminator capacity", !sec_str_memcpy(mem, 3, "abc", 3));
    RS_ATTACK("secure memzero rejects NULL nonzero", !sec_memzero(NULL, 1));
    RS_ATTACK("secure multiplication bound rejects overflow", !sec_mul_bound(huge, 2));

    RS_ATTACK("secure integer parser rejects overflow", !sec_str_to_int("99999999999999999999", &ivalue));
    RS_ATTACK("secure integer parser rejects command payload", !sec_str_to_int("12;id", &ivalue));
    RS_ATTACK("secure bounded integer parser rejects below minimum", !sec_str_to_int_bound("-1", 0, 100, &ivalue));
    RS_ATTACK("secure bounded integer parser rejects above maximum", !sec_str_to_int_bound("101", 0, 100, &ivalue));
    RS_ATTACK("secure double parser rejects NaN", !sec_str_to_double("nan", &dvalue));
    RS_ATTACK("secure double parser rejects infinity", !sec_str_to_double("inf", &dvalue));
    RS_ATTACK("secure double parser rejects trailing payload", !sec_str_to_double("1.0;id", &dvalue));
    RS_ATTACK("secure bounded double parser rejects range escape", !sec_str_to_double_bound("101", 0.0, 100.0, &dvalue));

    RS_ATTACK("secure private-directory rejects NULL", !sec_ensure_private_directory(NULL, 0700));
    RS_ATTACK("secure exclusive-open rejects traversal leaf",
              ({ FILE *fp = NULL; int result = !sec_fopen_exclusive_in_directory(rs_sec_temp_dir, "../escape", &fp); if (fp) fclose(fp); result; }));
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 14: SecureNetwork.c                                                */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 14
#include "../src/SecureNetwork.c"
static const char *rs_sec_target_name = "SecureNetwork.c";
static void rs_sec_runtime_attacks(void) {
    unsigned char buffer[16] = {0};
    unsigned char payload[32] = {0};
    char *username = NULL, *password = NULL, *totp = NULL;
    uint16_t mode = 0;

    secure_network_store_u16(buffer, 0xABCDU);
    secure_network_store_u32(buffer + 2, 0x89ABCDEFU);
    secure_network_store_u64(buffer + 6, UINT64_C(0x0123456789ABCDEF));
    RS_ATTACK("network u16 wire codec resists endian confusion", secure_network_load_u16(buffer) == 0xABCDU);
    RS_ATTACK("network u32 wire codec resists endian confusion", secure_network_load_u32(buffer + 2) == 0x89ABCDEFU);
    RS_ATTACK("network u64 wire codec resists endian confusion", secure_network_load_u64(buffer + 6) == UINT64_C(0x0123456789ABCDEF));

    RS_ATTACK("network auth parser rejects NULL payload",
              !secure_network_parse_auth(NULL, 0, &username, &password, &totp, &mode));
    RS_ATTACK("network auth parser rejects undersized frame",
              !secure_network_parse_auth(payload, 7, &username, &password, &totp, &mode));
    secure_network_store_u16(payload, 65535U);
    secure_network_store_u16(payload + 2, 1U);
    secure_network_store_u16(payload + 4, 0U);
    secure_network_store_u16(payload + 6, 0U);
    RS_ATTACK("network auth parser rejects length-overflow/oversized username",
              !secure_network_parse_auth(payload, sizeof(payload), &username, &password, &totp, &mode));
    if (username) OPENSSL_clear_free(username, strlen(username) + 1U);
    if (password) OPENSSL_clear_free(password, strlen(password) + 1U);
    if (totp) OPENSSL_clear_free(totp, strlen(totp) + 1U);


    {
        unsigned char client_nonce[SECURE_NETWORK_NONCE_BYTES];
        unsigned char server_nonce[SECURE_NETWORK_NONCE_BYTES];
        unsigned char binding[SECURE_NETWORK_EXPORTER_BYTES];
        unsigned char proof_a[256];
        unsigned char proof_b[256];
        size_t proof_size;

        memset(client_nonce, 0x11, sizeof(client_nonce));
        memset(server_nonce, 0x22, sizeof(server_nonce));
        memset(binding, 0x33, sizeof(binding));
        memset(proof_a, 0, sizeof(proof_a));
        memset(proof_b, 0, sizeof(proof_b));

        proof_size = secure_network_build_proof_message(client_nonce, server_nonce, binding,
                                                        proof_a, sizeof(proof_a));
        RS_ATTACK("network channel-proof transcript builds with domain/version/nonces/binding", proof_size > 0);
        RS_ATTACK("network channel-proof transcript rejects undersized destination",
                  proof_size > 0 && !secure_network_build_proof_message(client_nonce, server_nonce, binding,
                                                                         proof_b, proof_size - 1U));
        RS_ATTACK("network channel-proof transcript is deterministic for identical inputs",
                  proof_size > 0 &&
                  secure_network_build_proof_message(client_nonce, server_nonce, binding,
                                                     proof_b, sizeof(proof_b)) == proof_size &&
                  CRYPTO_memcmp(proof_a, proof_b, proof_size) == 0);

        client_nonce[0] ^= 0x01U;
        RS_ATTACK("network channel-proof transcript binds client nonce",
                  secure_network_build_proof_message(client_nonce, server_nonce, binding,
                                                     proof_b, sizeof(proof_b)) == proof_size &&
                  CRYPTO_memcmp(proof_a, proof_b, proof_size) != 0);
        client_nonce[0] ^= 0x01U;

        server_nonce[0] ^= 0x01U;
        RS_ATTACK("network channel-proof transcript binds server nonce",
                  secure_network_build_proof_message(client_nonce, server_nonce, binding,
                                                     proof_b, sizeof(proof_b)) == proof_size &&
                  CRYPTO_memcmp(proof_a, proof_b, proof_size) != 0);
        server_nonce[0] ^= 0x01U;

        binding[0] ^= 0x01U;
        RS_ATTACK("network channel-proof transcript binds TLS exporter material",
                  secure_network_build_proof_message(client_nonce, server_nonce, binding,
                                                     proof_b, sizeof(proof_b)) == proof_size &&
                  CRYPTO_memcmp(proof_a, proof_b, proof_size) != 0);
    }

    {
        SSL_CTX *context = SSL_CTX_new(TLS_method());
        int configured = context && secure_network_configure_context(context);
        unsigned long options = configured ? SSL_CTX_get_options(context) : 0;

        RS_ATTACK("network TLS context accepts required hardened policy", configured);
        RS_ATTACK("network TLS policy pins minimum to TLS 1.3",
                  configured && SSL_CTX_get_min_proto_version(context) == TLS1_3_VERSION);
        RS_ATTACK("network TLS policy pins maximum to TLS 1.3",
                  configured && SSL_CTX_get_max_proto_version(context) == TLS1_3_VERSION);
        RS_ATTACK("network TLS policy enforces OpenSSL security level 4",
                  configured && SSL_CTX_get_security_level(context) == 4);
        RS_ATTACK("network TLS policy disables compression",
                  configured && (options & SSL_OP_NO_COMPRESSION) != 0);
        RS_ATTACK("network TLS policy disables renegotiation",
                  configured && (options & SSL_OP_NO_RENEGOTIATION) != 0);
        RS_ATTACK("network TLS policy disables session tickets",
                  configured && (options & SSL_OP_NO_TICKET) != 0);
        RS_ATTACK("network TLS policy disables session cache",
                  configured && SSL_CTX_get_session_cache_mode(context) == SSL_SESS_CACHE_OFF);
        RS_ATTACK("network TLS policy disables early data",
                  configured && SSL_CTX_get_max_early_data(context) == 0);

        SSL_CTX_free(context);
    }

    {
        uint32_t attacker = htonl(0x0A000001U);
        uint32_t other = htonl(0x0A000002U);
        int initial_allowed = 1;

        pthread_mutex_lock(&Global_Secure_Rate_Lock);
        memset(Global_Secure_Rate_Entries, 0, sizeof(Global_Secure_Rate_Entries));
        pthread_mutex_unlock(&Global_Secure_Rate_Lock);

        for (unsigned int i = 0; i < SECURE_NETWORK_HANDSHAKES_PER_MINUTE; i++) {
            if (!secure_network_handshake_allowed(attacker)) initial_allowed = 0;
        }
        RS_ATTACK("network handshake limiter allows configured per-minute budget", initial_allowed);
        RS_ATTACK("network handshake limiter blocks address after budget exhaustion",
                  !secure_network_handshake_allowed(attacker));
        RS_ATTACK("network handshake limiter isolates independent source addresses",
                  secure_network_handshake_allowed(other));
    }

    {
        unsigned char valid[64] = {0};
        unsigned char trailing[65] = {0};
        unsigned char nul_user[64] = {0};
        char *u = NULL, *p = NULL, *t = NULL;
        uint16_t m = 0;
        size_t valid_size = 8U + 4U + 8U + 6U;
        int parsed;

        secure_network_store_u16(valid, 4U);
        secure_network_store_u16(valid + 2, 8U);
        secure_network_store_u16(valid + 4, 6U);
        secure_network_store_u16(valid + 6, SECURE_NETWORK_AUTH_MODE_LOGIN);
        memcpy(valid + 8, "user", 4);
        memcpy(valid + 12, "password", 8);
        memcpy(valid + 20, "123456", 6);
        parsed = secure_network_parse_auth(valid, valid_size, &u, &p, &t, &m);
        RS_ATTACK("network auth parser accepts exactly framed canonical credentials",
                  parsed && strcmp(u, "user") == 0 && strcmp(p, "password") == 0 &&
                  strcmp(t, "123456") == 0 && m == SECURE_NETWORK_AUTH_MODE_LOGIN);
        if (u) OPENSSL_clear_free(u, 5U); u = NULL;
        if (p) OPENSSL_clear_free(p, 9U); p = NULL;
        if (t) OPENSSL_clear_free(t, 7U); t = NULL;

        memcpy(trailing, valid, valid_size);
        trailing[valid_size] = 0xA5U;
        RS_ATTACK("network auth parser rejects trailing bytes after declared fields",
                  !secure_network_parse_auth(trailing, valid_size + 1U, &u, &p, &t, &m));
        if (u) OPENSSL_clear_free(u, 5U); u = NULL;
        if (p) OPENSSL_clear_free(p, 9U); p = NULL;
        if (t) OPENSSL_clear_free(t, 7U); t = NULL;

        secure_network_store_u16(nul_user, 5U);
        secure_network_store_u16(nul_user + 2, 8U);
        secure_network_store_u16(nul_user + 4, 0U);
        secure_network_store_u16(nul_user + 6, SECURE_NETWORK_AUTH_MODE_LOGIN);
        memcpy(nul_user + 8, "adm\0n", 5U);
        memcpy(nul_user + 13, "password", 8U);
        parsed = secure_network_parse_auth(nul_user, 21U, &u, &p, &t, &m);
        RS_ATTACK("network auth parser rejects embedded-NUL username encoding", !parsed);
        if (u) OPENSSL_clear_free(u, 6U); u = NULL;
        if (p) OPENSSL_clear_free(p, 9U); p = NULL;
        if (t) OPENSSL_clear_free(t, 1U); t = NULL;
    }
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 15: ServerIdentity.c                                               */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 15
#include "../src/ServerIdentity.c"
static const char *rs_sec_target_name = "ServerIdentity.c";
static void rs_sec_runtime_attacks(void) {
    char resolved[PATH_MAX];
    char message[256] = "";
    unsigned char fake_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
    unsigned char encoded[SERVER_IDENTITY_PUBLIC_FILE_BYTES];
    Type_Server_Public_Identity identity;

    memset(fake_key, 0x42, sizeof(fake_key));
    memset(encoded, 0, sizeof(encoded));
    memset(&identity, 0, sizeof(identity));

    RS_ATTACK("server identity rejects NULL name", !server_identity_name_valid(NULL));
    RS_ATTACK("server identity rejects newline/control name", !server_identity_name_valid("server\nspoof"));
    RS_ATTACK("server identity rejects overlong name",
              ({ char n[SERVER_IDENTITY_SERVER_NAME_BUFFER + 32]; memset(n, 'A', sizeof(n)); n[sizeof(n)-1] = '\0'; !server_identity_name_valid(n); }));
    RS_ATTACK("server identity rejects malformed target ID",
              !SERVER_IDENTITY_validate_target("../../ATTACK", message, sizeof(message)));
    RS_ATTACK("server identity rejects lowercase target ID",
              !SERVER_IDENTITY_validate_target("abcdefghijkl", message, sizeof(message)));
    RS_ATTACK("server identity import path rejects tiny destination",
              !server_identity_resolve_import_path("/tmp/very-long-file-name.rspub", resolved, 4));

    RS_ATTACK("server identity can encode bounded identity fixture",
              server_identity_encode_public_file("Security Test", fake_key, encoded));
    encoded[SERVER_IDENTITY_PUBLIC_FILE_KEY_OFFSET] ^= 0x01U;
    RS_ATTACK("server identity rejects tampered public identity file",
              !server_identity_decode_public_file(encoded, &identity));


    {
        unsigned char private_key[SERVER_IDENTITY_PRIVATE_KEY_BYTES];
        unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
        unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES];
        unsigned char tampered_signature[SERVER_IDENTITY_SIGNATURE_BYTES];
        static const unsigned char message[] = "RetroSpectrum security regression ML-DSA message";
        unsigned char tampered_message[sizeof(message)];
        int generated;

        memset(private_key, 0, sizeof(private_key));
        memset(public_key, 0, sizeof(public_key));
        memset(signature, 0, sizeof(signature));
        generated = server_identity_generate_keypair(private_key, public_key);

        RS_ATTACK("server identity generates ML-DSA-87 keypair", generated);
        RS_ATTACK("server identity verifies generated ML-DSA-87 keypair",
                  generated && server_identity_verify_keypair(private_key, public_key));
        RS_ATTACK("server identity signs security-test message",
                  generated && server_identity_sign_with_private(private_key, message, sizeof(message) - 1U,
                                                                 signature));
        RS_ATTACK("server identity verifies valid ML-DSA-87 signature",
                  generated && server_identity_verify_signature(public_key, message, sizeof(message) - 1U,
                                                                signature));

        memcpy(tampered_message, message, sizeof(message));
        tampered_message[0] ^= 0x01U;
        RS_ATTACK("server identity rejects signature after message tamper",
                  generated && !server_identity_verify_signature(public_key, tampered_message,
                                                                 sizeof(message) - 1U, signature));

        memcpy(tampered_signature, signature, sizeof(tampered_signature));
        tampered_signature[0] ^= 0x01U;
        RS_ATTACK("server identity rejects tampered ML-DSA-87 signature",
                  generated && !server_identity_verify_signature(public_key, message, sizeof(message) - 1U,
                                                                 tampered_signature));

        if (generated && server_identity_id_from_public_key(public_key, Global_Server_Identity_Id)) {
            unsigned char packet[SERVER_IDENTITY_PACKET_BYTES];
            unsigned char packet_public[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
            char packet_id[SERVER_IDENTITY_ID_BUFFER];

            memcpy(Global_Server_Identity_Private, private_key, sizeof(private_key));
            memcpy(Global_Server_Identity_Public, public_key, sizeof(public_key));
            memset(packet, 0, sizeof(packet));
            memset(packet_public, 0, sizeof(packet_public));
            memset(packet_id, 0, sizeof(packet_id));

            RS_ATTACK("server identity builds signed LAN identity packet",
                      server_identity_build_packet(packet, SERVER_IDENTITY_PACKET_TYPE_ANNOUNCE));
            RS_ATTACK("server identity validates fresh signed LAN identity packet",
                      server_identity_packet_valid(packet, packet_id, packet_public));

            packet[SERVER_IDENTITY_NONCE_OFFSET] ^= 0x01U;
            RS_ATTACK("server identity rejects LAN packet after signed nonce tamper",
                      !server_identity_packet_valid(packet, packet_id, packet_public));
            packet[SERVER_IDENTITY_NONCE_OFFSET] ^= 0x01U;

            RS_ATTACK("server identity rebuilds packet for stale-timestamp attack",
                      server_identity_build_packet(packet, SERVER_IDENTITY_PACKET_TYPE_ANNOUNCE));
            server_identity_write_u64(packet + SERVER_IDENTITY_TIMESTAMP_OFFSET,
                                      (uint64_t)time(NULL) - SERVER_IDENTITY_MAX_CLOCK_SKEW - 5U);
            RS_ATTACK("server identity resigns stale packet attack fixture",
                      server_identity_sign_with_private(private_key, packet, SERVER_IDENTITY_SIGNED_BYTES,
                                                        packet + SERVER_IDENTITY_SIGNATURE_OFFSET));
            RS_ATTACK("server identity rejects correctly signed stale replay packet",
                      !server_identity_packet_valid(packet, packet_id, packet_public));

            RS_ATTACK("server identity rebuilds packet for future-timestamp attack",
                      server_identity_build_packet(packet, SERVER_IDENTITY_PACKET_TYPE_ANNOUNCE));
            server_identity_write_u64(packet + SERVER_IDENTITY_TIMESTAMP_OFFSET,
                                      (uint64_t)time(NULL) + SERVER_IDENTITY_MAX_CLOCK_SKEW + 5U);
            RS_ATTACK("server identity resigns future packet attack fixture",
                      server_identity_sign_with_private(private_key, packet, SERVER_IDENTITY_SIGNED_BYTES,
                                                        packet + SERVER_IDENTITY_SIGNATURE_OFFSET));
            RS_ATTACK("server identity rejects correctly signed future-dated packet",
                      !server_identity_packet_valid(packet, packet_id, packet_public));

            OPENSSL_cleanse(packet, sizeof(packet));
            OPENSSL_cleanse(packet_public, sizeof(packet_public));
            OPENSSL_cleanse(packet_id, sizeof(packet_id));
        } else {
            RS_ATTACK("server identity derives public-key server ID for packet tests", 0);
        }

        OPENSSL_cleanse(private_key, sizeof(private_key));
        OPENSSL_cleanse(public_key, sizeof(public_key));
        OPENSSL_cleanse(signature, sizeof(signature));
        OPENSSL_cleanse(tampered_signature, sizeof(tampered_signature));
        OPENSSL_cleanse(tampered_message, sizeof(tampered_message));
    }
}
#endif

/* ------------------------------------------------------------------------- */
/* Target 16: optional world_map_bin_loader.c                                */
/* ------------------------------------------------------------------------- */
#if RS_SECURITY_TARGET == 16
#define WORLD_MAP_NO_DEMO
#include "../src/world_map_bin_loader.c"
static const char *rs_sec_target_name = "world_map_bin_loader.c";
static void rs_sec_runtime_attacks(void) {
    char copied[8] = "";
    int x = 0, y = 0;
    SDL_Rect map = {0, 0, 1000, 500};

    WM_copy_text(copied, sizeof(copied), "AAAAAAAAAAAAAAAA");
    RS_ATTACK("world-map bounded copy remains terminated", copied[sizeof(copied)-1] == '\0');
    WM_project_point(0, 0, map, &x, &y);
    RS_ATTACK("world-map projection remains inside integer viewport", x >= 0 && x <= map.w && y >= 0 && y <= map.h);
    RS_ATTACK("world-map loader rejects NULL path", !WORLD_MAP_load(NULL));
}
#endif

int main(void) {
    char template_path[] = "/tmp/retrospectrum-security-tests-XXXXXX";
    const char *quiet = getenv("RS_SECURITY_QUIET");
    const char *root = getenv("RS_SECURITY_PROJECT_ROOT");

    rs_sec_quiet = quiet && quiet[0] && strcmp(quiet, "0") != 0;
    if (root && root[0]) rs_sec_project_root = root;

    if (!mkdtemp(template_path)) {
        fprintf(stderr, RS_SEC_RED "FAILED" RS_SEC_RESET " create security test directory: %s\n", strerror(errno));
        return 1;
    }
    snprintf(rs_sec_temp_dir, sizeof(rs_sec_temp_dir), "%s", template_path);
    setenv("HOME", rs_sec_temp_dir, 1);
    setenv("XDG_CONFIG_HOME", rs_sec_temp_dir, 1);
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 0);

    printf("\n" RS_SEC_CYAN "RetroSpectrum security target: %s" RS_SEC_RESET "\n", rs_sec_target_name);
    printf("Attack classes: unsafe-copy, unbounded-format, command execution, temporary-file race, symlink creation, weak RNG + runtime hostile probes.\n\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) RS_SKIP("SDL initialization", SDL_GetError());
    else rs_sec_pass("SDL initialization");

    if (TTF_Init() != 0) RS_SKIP("SDL_ttf initialization", TTF_GetError());
    else rs_sec_pass("SDL_ttf initialization");

    (void)rs_sec_scan_every_function(rs_sec_target_name);

    printf("\n" RS_SEC_CYAN "=== Runtime hostile-input attacks: %s ===" RS_SEC_RESET "\n", rs_sec_target_name);
    rs_sec_runtime_attacks();

    TTF_Quit();
    SDL_Quit();

    printf("\n%s security summary: " RS_SEC_GREEN "%d passed" RS_SEC_RESET ", "
           RS_SEC_RED "%d failed" RS_SEC_RESET ", " RS_SEC_YELLOW "%d skipped" RS_SEC_RESET "\n",
           rs_sec_target_name, rs_sec_passed, rs_sec_failed, rs_sec_skipped);

    return rs_sec_failed == 0 ? 0 : 1;
}
