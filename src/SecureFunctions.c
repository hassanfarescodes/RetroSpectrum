#define _POSIX_C_SOURCE 200809L

#include "SecureFunctions.h"

#include <errno.h>
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
    if (dst == NULL) {
        return false;
    }

    memset(dst, 0, dst_size);
    return true;
}

bool sec_mul_bound(size_t a, size_t b) {
    return !(a != 0 && b > SIZE_MAX / a);
}

void *sec_calloc_array(size_t count, size_t elem_size, size_t max_count) {
    if (elem_size == 0 || count == 0 || count > max_count) {
        return NULL;
    }

    if (!sec_mul_bound(count, elem_size)) {
        return NULL;
    }

    return calloc(count, elem_size);
}

bool sec_str_to_int(const char *s, int *out) {
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
