#ifndef IQS_H
#define IQS_H

#include <stddef.h>
#include <pthread.h>

#ifndef FFT_SIZE
#define FFT_SIZE    4096
#endif

#ifndef RING_SIZE
#define RING_SIZE   (FFT_SIZE * 256)  // Threading environment is unpredictable, allow 
                                      // "breathing" room for FFT
#endif

// Stores IQ samples
typedef struct {
  float I[RING_SIZE];
  float Q[RING_SIZE];
  size_t write_pos;
  size_t read_pos;
  pthread_mutex_t lock;
} Type_RingBuf;

#endif
