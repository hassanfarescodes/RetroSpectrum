#ifndef RETROSPECTRUM_H
#define RETROSPECTRUM_H

#include <stddef.h>
#include <stdint.h>

int RETROSPECTRUM_start_file_transmission(const char *path, uint64_t center_frequency_hz, uint32_t sample_rate_hz,
                                          uint32_t bandwidth_hz, int tx_gain_db, unsigned int repeat_count, char *error,
                                          size_t error_size);
void RETROSPECTRUM_cancel_file_transmission(void);
int RETROSPECTRUM_get_transmission_status(double *progress, int *active, int *result_ready, int *succeeded,
                                          char *message, size_t message_size);
void RETROSPECTRUM_acknowledge_transmission_result(void);

#endif
