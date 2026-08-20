#ifndef SECURE_FUNCTIONS_H
#define SECURE_FUNCTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool sec_sprintf(char *dst, size_t dst_size, const char *fmt, ...);

bool sec_strcpy(char *dst, size_t dst_size, const char *src);

bool sec_strcat(char *dst, size_t dst_size, const char *suffix);

bool sec_memcpy(void *dst, size_t dst_size, const void *src, size_t copy_size);

bool sec_memmove(void *dst, size_t dst_size, const void *src, size_t move_size);

bool sec_str_memcpy(char *dst, size_t dst_size, const char *src, size_t src_len);

bool sec_memzero(void *dst, size_t dst_size);

bool sec_mul_bound(size_t a, size_t b);

void *sec_calloc_array(size_t count, size_t elem_size, size_t max_count);

bool sec_str_to_int(const char *s, int *out);

bool sec_str_to_double(const char *s, double *out);

bool sec_popen_read(const char *exe_path, char *const argv[], char *out, size_t out_size);

bool sec_fopen_complex16(const char *path, FILE **out_fp, size_t *out_iq_count);

bool sec_str_to_int_bound(const char *s, int min_value, int max_value, int *out);

bool sec_str_to_double_bound(const char *s, double min_value, double max_value, double *out);

bool sec_ensure_private_directory(const char *path, mode_t create_mode);

bool sec_fopen_exclusive_in_directory(const char *directory, const char *leaf_name, FILE **out_fp);

#ifdef __cplusplus
}
#endif

#endif
