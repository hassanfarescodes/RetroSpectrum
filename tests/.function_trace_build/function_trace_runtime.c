#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define NO_INSTRUMENT __attribute__((no_instrument_function))
#define TRACE_BUFFER_RECORDS 256u
#define TRACE_SEEN_SLOTS 16384u

typedef struct {
    uint64_t sequence;
    uint64_t function_address;
    uint64_t thread_id;
} TraceRecord;

static int trace_fd = -1;
static pthread_key_t trace_tls_key;
static int trace_tls_key_ready = 0;
static _Atomic uint64_t trace_sequence = 0;
static _Atomic uintptr_t trace_seen[TRACE_SEEN_SLOTS];

static _Thread_local TraceRecord trace_buffer[TRACE_BUFFER_RECORDS];
static _Thread_local size_t trace_buffer_count = 0;
static _Thread_local uint64_t trace_thread_id = 0;
static _Thread_local int trace_thread_registered = 0;

static int trace_write_all(const void *buffer, size_t bytes) NO_INSTRUMENT;
static void trace_flush_tls(void) NO_INSTRUMENT;
static void trace_tls_destructor(void *value) NO_INSTRUMENT;
static void trace_register_thread(void) NO_INSTRUMENT;
static int trace_mark_first(uintptr_t function_address) NO_INSTRUMENT;
static void trace_record_entry(void *function_address) NO_INSTRUMENT;
static void trace_init(void) __attribute__((constructor(101), no_instrument_function));
static void trace_shutdown(void) __attribute__((destructor(101), no_instrument_function));

static int trace_write_all(const void *buffer, size_t bytes) {
    const unsigned char *p = (const unsigned char *)buffer;

    while (bytes > 0u) {
        ssize_t written = write(trace_fd, p, bytes);
        if (written > 0) {
            p += (size_t)written;
            bytes -= (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return 0;
    }
    return 1;
}

static void trace_flush_tls(void) {
    if (trace_fd < 0 || trace_buffer_count == 0u) {
        return;
    }

    (void)trace_write_all(trace_buffer, trace_buffer_count * sizeof(trace_buffer[0]));
    trace_buffer_count = 0u;
}

static void trace_tls_destructor(void *value) {
    (void)value;
    trace_flush_tls();
    trace_thread_registered = 0;
}

static void trace_register_thread(void) {
    long tid;

    if (trace_thread_registered) {
        return;
    }

    tid = syscall(SYS_gettid);
    trace_thread_id = (uint64_t)(tid > 0 ? tid : 0);
    trace_thread_registered = 1;

    if (trace_tls_key_ready) {
        (void)pthread_setspecific(trace_tls_key, (void *)(uintptr_t)1u);
    }
}

static int trace_mark_first(uintptr_t function_address) {
    size_t slot;
    size_t probe;

    /* Function addresses are aligned, so discard low zero bits and mix them. */
    slot = (size_t)(((function_address >> 4u) ^ (function_address >> 17u)) &
                    (TRACE_SEEN_SLOTS - 1u));

    for (probe = 0u; probe < TRACE_SEEN_SLOTS; ++probe) {
        uintptr_t expected = 0u;
        uintptr_t current = atomic_load_explicit(&trace_seen[slot], memory_order_relaxed);

        if (current == function_address) {
            return 0;
        }

        if (current == 0u &&
            atomic_compare_exchange_strong_explicit(&trace_seen[slot], &expected,
                                                    function_address,
                                                    memory_order_relaxed,
                                                    memory_order_relaxed)) {
            return 1;
        }

        if (expected == function_address) {
            return 0;
        }

        slot = (slot + 1u) & (TRACE_SEEN_SLOTS - 1u);
    }

    /* If the fixed table ever fills, drop the event rather than producing
       an unbounded trace or blocking the application. */
    return 0;
}

static void trace_record_entry(void *function_address) {
    TraceRecord *record;
    uintptr_t address;

    if (trace_fd < 0 || function_address == NULL) {
        return;
    }

    address = (uintptr_t)function_address;
    if (!trace_mark_first(address)) {
        return;
    }

    if (!trace_thread_registered) {
        trace_register_thread();
    }

    record = &trace_buffer[trace_buffer_count++];
    record->sequence = atomic_fetch_add_explicit(&trace_sequence, 1u, memory_order_relaxed);
    record->function_address = (uint64_t)address;
    record->thread_id = trace_thread_id;

    if (trace_buffer_count == TRACE_BUFFER_RECORDS) {
        trace_flush_tls();
    }
}

static void trace_init(void) {
    const char *path = getenv("RETROSPECTRUM_TRACE_FILE");

    if (path == NULL || path[0] == '\0') {
        return;
    }

    trace_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0600);
    if (trace_fd < 0) {
        return;
    }

    if (pthread_key_create(&trace_tls_key, trace_tls_destructor) == 0) {
        trace_tls_key_ready = 1;
    }
}

static void trace_shutdown(void) {
    trace_flush_tls();

    if (trace_fd >= 0) {
        (void)close(trace_fd);
        trace_fd = -1;
    }

    if (trace_tls_key_ready) {
        (void)pthread_key_delete(trace_tls_key);
        trace_tls_key_ready = 0;
    }
}

void __cyg_profile_func_enter(void *this_fn, void *call_site) NO_INSTRUMENT;
void __cyg_profile_func_exit(void *this_fn, void *call_site) NO_INSTRUMENT;

void __cyg_profile_func_enter(void *this_fn, void *call_site) {
    (void)call_site;
    trace_record_entry(this_fn);
}

void __cyg_profile_func_exit(void *this_fn, void *call_site) {
    /* Entry-only tracing intentionally avoids doubling tracing overhead. */
    (void)this_fn;
    (void)call_site;
}
