#define _POSIX_C_SOURCE 200809L

/*
 * ============================================================================
 * File:            SecureFunctions.c
 * Author:          Hassan Fares
 *
 * Description:     Security-hardened utility functions for RetroSpectrum
 *
 * Language:        C
 * Compiler:        GCC
 * Standard:        C11
 * Target:          Linux
 *
 *                                                               05/04/2026
 * ============================================================================
*/

#include "SecureFunctions.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SEC_COMPLEX16_BYTES_PER_IQ 4u
#define SEC_MAX_COMPLEX16_BYTES ((uintmax_t)2u * 1024u * 1024u * 1024u)

bool sec_sprintf(char *dst, size_t dst_size, const char *fmt, ...) {
    /*
        Purpose: Formats text into a bounded buffer
        Returns: Success status
    */

    if (dst == NULL || dst_size == 0 || fmt == NULL) {

        return false;

    }

    va_list args;
    va_start(args, fmt);

    int written = vsnprintf(dst, dst_size, fmt, args);

    va_end(args);

    if (written < 0 || (size_t)written >= dst_size) {

        dst[dst_size - 1] = '\0';
        return false;

    }

    return true;
}

bool sec_strcpy(char *dst, size_t dst_size, const char *src) {
    /*
        Purpose: Copies text into a bounded buffer
        Returns: Success status
    */

    if (dst == NULL || dst_size == 0 || src == NULL) {

        return false;

    }

    int written = snprintf(dst, dst_size, "%s", src);

    if (written < 0 || (size_t)written >= dst_size) {

        dst[dst_size - 1] = '\0';
        return false;

    }

    return true;
}

bool sec_strcat(char *dst, size_t dst_size, const char *suffix) {
    /*
        Purpose: Appends text to a bounded buffer
        Returns: Success status
    */

    if (dst == NULL || dst_size == 0 || suffix == NULL) {

        return false;

    }

    size_t len = strnlen(dst, dst_size);

    if (len >= dst_size) {

        dst[dst_size - 1] = '\0';
        return false;

    }

    return sec_sprintf(dst + len, dst_size - len, "%s", suffix);
}

bool sec_memcpy(void *dst, size_t dst_size, const void *src, size_t copy_size) {
    /*
        Purpose: Copies memory with bounds validation
        Returns: Success status
    */

    if (dst == NULL || src == NULL) {

        return false;

    }

    if (copy_size > dst_size) {

        return false;

    }

    memcpy(dst, src, copy_size);
    return true;
}

bool sec_memmove(void *dst, size_t dst_size, const void *src, size_t move_size) {
    /*
        Purpose: Moves memory with bounds validation
        Returns: Success status
    */

    if (dst == NULL || src == NULL) {

        return false;

    }

    if (move_size > dst_size) {

        return false;

    }

    memmove(dst, src, move_size);

    return true;
}

bool sec_str_memcpy(char *dst, size_t dst_size, const char *src, size_t src_len) {
    /*
        Purpose: Copies a bounded string from memory
        Returns: Success status
    */

    if (dst == NULL || dst_size == 0 || src == NULL) {

        return false;

    }

    if (src_len >= dst_size) {

        memcpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
        return false;

    }

    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
    return true;
}

bool sec_memzero(void *dst, size_t dst_size) {
    /*
        Purpose: Clears a memory region
        Returns: Success status
    */

    if (dst == NULL) {

        return false;

    }

    memset(dst, 0, dst_size);
    return true;
}

bool sec_mul_bound(size_t a, size_t b) {
    /*
        Purpose: Checks whether a size multiplication fits
        Returns: Success status
    */

    return !(a != 0 && b > SIZE_MAX / a);
}

void *sec_calloc_array(size_t count, size_t elem_size, size_t max_count) {
    /*
        Purpose: Allocates a checked zeroed array
        Returns: Result pointer
    */

    if (elem_size == 0 || count == 0 || count > max_count) {

        return NULL;

    }

    if (!sec_mul_bound(count, elem_size)) {

        return NULL;

    }

    return calloc(count, elem_size);
}

bool sec_str_to_int(const char *s, int *out) {
    /*
        Purpose: Converts the string to the int
        Returns: Success status
    */

    if (s == NULL || out == NULL) {

        return false;

    }

    char *end = NULL;
    errno = 0;

    long value = strtol(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0') {

        return false;

    }

    if (value < INT_MIN || value > INT_MAX) {

        return false;

    }

    *out = (int)value;

    return true;
}

bool sec_str_to_double(const char *s, double *out) {
    /*
        Purpose: Converts the string to the double
        Returns: Success status
    */

    if (s == NULL || out == NULL) {

        return false;

    }

    char *end = NULL;
    errno = 0;

    double value = strtod(s, &end);

    if (errno != 0 || end == s || *end != '\0') {

        return false;

    }

    if (!isfinite(value)) {

        return false;

    }

    *out = value;

    return true;
}

bool sec_popen_read(const char *exe_path, char *const argv[], char *out, size_t out_size) {
    /*
        Purpose: Executes a program and captures its output
        Returns: Success status
    */

    if (exe_path == NULL || argv == NULL || argv[0] == NULL || out == NULL || out_size == 0) {

        return false;

    }

    out[0] = '\0';

    int pipefd[2];

    if (pipe(pipefd) != 0) {

        return false;

    }

    pid_t pid = fork();

    if (pid < 0) {

        close(pipefd[0]);
        close(pipefd[1]);
        return false;

    }

    if (pid == 0) {

        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {

            _exit(127);

        }

        close(pipefd[1]);

        execv(exe_path, argv);

        _exit(127);

    }

    close(pipefd[1]);

    size_t used = 0;
    bool truncated = false;

    while (true) {
        char buf[512];

        ssize_t n = read(pipefd[0], buf, sizeof(buf));

        if (n < 0) {

            if (errno == EINTR) {

                continue;

            }

            close(pipefd[0]);
            waitpid(pid, NULL, 0);
            return false;

        }

        if (n == 0) {

            break;

        }

        size_t remaining = out_size - 1 - used;

        if ((size_t)n <= remaining) {

            memcpy(out + used, buf, (size_t)n);
            used += (size_t)n;
            out[used] = '\0';

        }

        else {

            if (remaining > 0) {

                memcpy(out + used, buf, remaining);
                used += remaining;
                out[used] = '\0';

            }

            truncated = true;

        }
    }

    close(pipefd[0]);

    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {

        return false;

    }

    if (truncated) {

        return false;

    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {

        return false;

    }

    return true;
}

static bool sec_has_extension(const char *s, const char *ext) {
    /*
        Purpose: Checks whether the extension is present
        Returns: Success status
    */

    if (s == NULL || ext == NULL) {

        return false;

    }

    size_t s_len = strlen(s);
    size_t ext_len = strlen(ext);

    if (s_len < ext_len) {

        return false;

    }

    return strcmp(s + s_len - ext_len, ext) == 0;
}

bool sec_fopen_complex16(const char *path, FILE **out_fp, size_t *out_iq_count) {
    /*
        Purpose: Opens and validates a complex16 IQ file
        Returns: Success status
    */

    if (path == NULL || out_fp == NULL || out_iq_count == NULL) {

        return false;

    }

    *out_fp = NULL;
    *out_iq_count = 0;

    if (!sec_has_extension(path, ".complex16")) {

        return false;

    }

    FILE *fp = fopen(path, "rb");

    if (fp == NULL) {

        return false;

    }

    struct stat st;

    if (fstat(fileno(fp), &st) != 0) {

        fclose(fp);
        return false;

    }

    if (!S_ISREG(st.st_mode)) {

        fclose(fp);
        return false;

    }

    if (st.st_size <= 0) {

        fclose(fp);
        return false;

    }

    if ((uintmax_t)st.st_size > SEC_MAX_COMPLEX16_BYTES) {

        fclose(fp);
        return false;

    }

    if ((uintmax_t)st.st_size > (uintmax_t)SIZE_MAX) {

        fclose(fp);
        return false;

    }

    size_t file_size = (size_t)st.st_size;

    if (file_size % SEC_COMPLEX16_BYTES_PER_IQ != 0) {

        fclose(fp);
        return false;

    }

    size_t iq_count = file_size / SEC_COMPLEX16_BYTES_PER_IQ;

    if (iq_count == 0) {

        fclose(fp);
        return false;

    }

    *out_fp = fp;
    *out_iq_count = iq_count;

    return true;
}

bool sec_str_to_int_bound(const char *s, int min_value, int max_value, int *out) {
    /*
        Purpose: Converts the string to the int bound
        Returns: Success status
    */

    if (s == NULL || out == NULL || min_value > max_value) {

        return false;

    }

    int value = 0;

    if (!sec_str_to_int(s, &value)) {

        return false;

    }

    if (value < min_value || value > max_value) {

        return false;

    }

    *out = value;
    return true;
}

bool sec_str_to_double_bound(const char *s, double min_value, double max_value, double *out) {
    /*
        Purpose: Converts the string to the double bound
        Returns: Success status
    */

    if (s == NULL || out == NULL) {

        return false;

    }

    if (!isfinite(min_value) || !isfinite(max_value) || min_value > max_value) {

        return false;

    }

    double value = 0.0;

    if (!sec_str_to_double(s, &value)) {

        return false;

    }

    if (value < min_value || value > max_value) {

        return false;

    }

    *out = value;
    return true;
}

static bool sec_normalize_directory_path(const char *path, char *normalized, size_t normalized_size) {
    /*
        Purpose: Copies a directory path and removes trailing seperators
        Returns: Success status
    */

    size_t length;

    if (path == NULL || normalized == NULL || normalized_size == 0) {

        return false;

    }

    if (!sec_strcpy(normalized, normalized_size, path)) {

        return false;

    }

    length = strnlen(normalized, normalized_size);

    if (length == 0 || length >= normalized_size) {

        return false;

    }

    while (length > 1 && normalized[length - 1] == '/') {
        normalized[--length] = '\0';
    }

    if (strcmp(normalized, ".") == 0 || strcmp(normalized, "..") == 0 || strcmp(normalized, "/") == 0) {

        return false;

    }

    return true;
}

static bool sec_directory_fd_is_secure(int directory_fd) {
    /*
        Purpose: Verifies that an opened directory is owned by this process
                 user and is not writable by group or other users
        Returns: Validation status
    */

    struct stat st;

    if (directory_fd < 0 || fstat(directory_fd, &st) != 0) {

        return false;

    }

    if (!S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {

        return false;

    }

    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {

        return false;

    }

    return true;
}

bool sec_ensure_private_directory(const char *path, mode_t create_mode) {
    /*
        Purpose: Ensures that an owned, non-writable-by-others directory exists
        Returns: Success status
    */

    char normalized[PATH_MAX];
    int directory_fd = -1;
    bool created = false;
    bool success = false;

    if ((create_mode & ~((mode_t)0777)) != 0 || (create_mode & 0777) == 0 ||
        !sec_normalize_directory_path(path, normalized, sizeof(normalized))) {

        return false;

    }

    directory_fd = open(normalized, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

    if (directory_fd < 0 && errno == ENOENT) {

        if (mkdir(normalized, create_mode) != 0) {

            if (errno != EEXIST) {

                return false;

            }

        }

        else {

            created = true;

        }

        directory_fd = open(normalized, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

    }

    if (directory_fd < 0 || !sec_directory_fd_is_secure(directory_fd)) {

        goto cleanup;

    }

    if (created && fchmod(directory_fd, create_mode) != 0) {

        goto cleanup;

    }

    success = true;

cleanup:

    if (directory_fd >= 0) {

        close(directory_fd);

    }

    return success;
}

bool sec_fopen_exclusive_in_directory(const char *directory, const char *leaf_name, FILE **out_fp) {
    /*
        Purpose: Exclusively creates a regular file inside a verified directory
        Returns: Success status
    */

    char normalized[PATH_MAX];
    size_t leaf_length;
    int directory_fd = -1;
    int file_fd = -1;
    FILE *fp = NULL;
    bool success = false;

    if (out_fp == NULL) {

        return false;

    }

    *out_fp = NULL;

    if (directory == NULL || leaf_name == NULL ||
        !sec_normalize_directory_path(directory, normalized, sizeof(normalized))) {

        return false;

    }

    leaf_length = strnlen(leaf_name, PATH_MAX);

    if (leaf_length == 0 || leaf_length >= PATH_MAX || strcmp(leaf_name, ".") == 0 || strcmp(leaf_name, "..") == 0 ||
        strchr(leaf_name, '/') != NULL) {

        return false;

    }

    directory_fd = open(normalized, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

    if (directory_fd < 0 || !sec_directory_fd_is_secure(directory_fd)) {

        goto cleanup;

    }

    file_fd = openat(directory_fd, leaf_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);

    if (file_fd < 0) {

        goto cleanup;

    }

    fp = fdopen(file_fd, "wb");

    if (fp == NULL) {

        unlinkat(directory_fd, leaf_name, 0);
        goto cleanup;

    }

    file_fd = -1;
    *out_fp = fp;
    fp = NULL;
    success = true;

cleanup:

    if (fp != NULL) {

        fclose(fp);

    }

    if (file_fd >= 0) {

        close(file_fd);

    }

    if (directory_fd >= 0) {

        close(directory_fd);

    }

    return success;
}
