#!/usr/bin/env bash
set -euo pipefail

# RetroSpectrum test-only low-overhead whole-program function-call tracer.
#
# Put this file at:
#   tests/function_trace.sh
#
# Run it with normal RetroSpectrum arguments, for example:
#   bash tests/function_trace.sh -C -o /media/user/recordings/
#   bash tests/function_trace.sh -S -o /media/user/recordings/
#
# With no arguments it uses:
#   -S -o tests/.function_trace_records
#
# This script DOES NOT modify src/*.c or the normal build/ directory.
# It creates an isolated instrumented build under tests/.function_trace_build/.
#
# Tracing starts automatically when RetroSpectrum starts and stops automatically
# when RetroSpectrum exits. Each instrumented project function is recorded only
# the FIRST time it is reached during that run. Repeated hot-loop calls are
# discarded in memory and never written to the raw trace.
#
# Final readable output:
#   tests/function_trace.txt
#
# Format:
#   [FILENAME] [FUNCTION NAME]
#
# A sequence number and TID are retained in the raw trace. The readable output
# therefore acts as a compact first-seen map of the functions exercised.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
MAKEFILE="$ROOT_DIR/Makefile"
TRACE_BUILD="$SCRIPT_DIR/.function_trace_build"
TRACE_BIN="$TRACE_BUILD/retrospectrum_trace"
TRACE_RUNTIME="$TRACE_BUILD/function_trace_runtime.c"
TRACE_RUNTIME_OBJ="$TRACE_BUILD/function_trace_runtime.o"
RAW_TRACE="$SCRIPT_DIR/function_trace.raw"
TEXT_TRACE="$SCRIPT_DIR/function_trace.txt"

if [[ ! -f "$MAKEFILE" ]]; then
    echo "error: expected Makefile at: $MAKEFILE" >&2
    echo "Place this script in RetroSpectrum/tests/." >&2
    exit 1
fi

for cmd in gcc python3 addr2line pkg-config; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: required command not found: $cmd" >&2
        exit 1
    fi
done

mkdir -p "$TRACE_BUILD"
rm -f "$RAW_TRACE" "$TEXT_TRACE"

# Read the current Makefile's SRCS list so the test build follows the application
# source list without hardcoding or changing src/*.c.
mapfile -t SRCS < <(python3 - "$MAKEFILE" <<'PY'
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
lines = path.read_text(encoding="utf-8").splitlines()
variables = {}

for line in lines:
    stripped = line.strip()
    if not stripped or stripped.startswith("#"):
        continue
    match = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(?::=|=)\s*(.*)$", stripped)
    if match:
        variables[match.group(1)] = match.group(2).strip()

def expand_simple(value):
    for _ in range(32):
        old = value
        for name, replacement in variables.items():
            value = value.replace("$(" + name + ")", replacement)
        if value == old:
            break
    return value

collecting = False
parts = []
for line in lines:
    stripped = line.strip()
    if not collecting:
        match = re.match(r"^SRCS\s*:=\s*(.*)$", stripped)
        if not match:
            continue
        rhs = match.group(1)
        collecting = True
    else:
        rhs = stripped

    continued = rhs.endswith("\\")
    if continued:
        rhs = rhs[:-1].rstrip()

    rhs = expand_simple(rhs)
    if rhs:
        parts.extend(rhs.split())

    if not continued:
        break

for item in parts:
    print(item)
PY
)

if (( ${#SRCS[@]} == 0 )); then
    echo "error: could not read SRCS from $MAKEFILE" >&2
    exit 1
fi

cat > "$TRACE_RUNTIME" <<'C_EOF'
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
C_EOF

# Match the dependencies used by RetroSpectrum while using an isolated tracing
# build. Optimization is kept enabled so the instrumented application remains
# usable; source files themselves are not changed.
read -r -a PKG_CFLAGS <<< "$(pkg-config --cflags openssl sqlcipher SoapySDR)"
read -r -a PKG_LIBS <<< "$(pkg-config --libs openssl sqlcipher SoapySDR)"

CPPFLAGS=("-I$ROOT_DIR/include" "${PKG_CFLAGS[@]}")
TRACE_CFLAGS=(
    -Wall -Wextra -Wformat=2 -Wformat-security
    -O2 -g3 -std=c11
    -fstack-protector-strong -D_FORTIFY_SOURCE=3
    -fno-common -fno-pie -fno-omit-frame-pointer
    -finstrument-functions
)
RUNTIME_CFLAGS=(
    -Wall -Wextra -O2 -g3 -std=c11
    -fno-pie -fno-omit-frame-pointer
)
TRACE_LDFLAGS=(
    -no-pie
    -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
)
TRACE_LIBS=(
    -lfftw3 -lSDL2 -lSDL2_ttf -lSDL2_image -largon2
    "${PKG_LIBS[@]}"
    -lm -lpthread
)

OBJS=()

echo "Building isolated function-trace binary..."
for src in "${SRCS[@]}"; do
    abs_src="$ROOT_DIR/$src"
    if [[ ! -f "$abs_src" ]]; then
        echo "error: source listed by Makefile does not exist: $abs_src" >&2
        exit 1
    fi

    base="$(basename "${src%.c}")"
    obj="$TRACE_BUILD/${base}.o"
    echo "  $src"
    gcc "${CPPFLAGS[@]}" "${TRACE_CFLAGS[@]}" -c "$abs_src" -o "$obj"
    OBJS+=("$obj")
done

gcc "${RUNTIME_CFLAGS[@]}" -c "$TRACE_RUNTIME" -o "$TRACE_RUNTIME_OBJ"
gcc "${OBJS[@]}" "$TRACE_RUNTIME_OBJ" -o "$TRACE_BIN" \
    "${TRACE_LDFLAGS[@]}" "${TRACE_LIBS[@]}"

# Copy only runtime assets into the isolated trace-build directory.
if [[ -f "$ROOT_DIR/src/world_map.bin" ]]; then
    cp "$ROOT_DIR/src/world_map.bin" "$TRACE_BUILD/world_map.bin"
fi
rm -rf "$TRACE_BUILD/flags"
if [[ -d "$ROOT_DIR/src/flags" ]]; then
    cp -R "$ROOT_DIR/src/flags" "$TRACE_BUILD/flags"
elif [[ -d "$ROOT_DIR/flags" ]]; then
    cp -R "$ROOT_DIR/flags" "$TRACE_BUILD/flags"
fi

if (( $# == 0 )); then
    DEFAULT_RECORD_DIR="$SCRIPT_DIR/.function_trace_records"
    mkdir -p "$DEFAULT_RECORD_DIR"
    APP_ARGS=(-S -o "$DEFAULT_RECORD_DIR")
    echo
    echo "No RetroSpectrum arguments supplied; using test defaults:"
    echo "  mode:       server GUI (-S)"
    echo "  record dir: $DEFAULT_RECORD_DIR"
else
    APP_ARGS=("$@")
fi

echo
echo "Starting RetroSpectrum trace build."
printf 'Launch:'
printf ' %q' "$TRACE_BIN" "${APP_ARGS[@]}"
printf '\n'
echo "Tracing is ON automatically and remains on until RetroSpectrum exits."
echo "No keyboard input is required by this script."
echo

APP_PID=""
interrupted=0

stop_child() {
    interrupted=1
    if [[ -n "${APP_PID:-}" ]] && kill -0 "$APP_PID" 2>/dev/null; then
        kill -TERM "$APP_PID" 2>/dev/null || true
    fi
}
trap stop_child INT TERM

(
    cd "$TRACE_BUILD"
    exec env RETROSPECTRUM_TRACE_FILE="$RAW_TRACE" "$TRACE_BIN" "${APP_ARGS[@]}"
) &
APP_PID=$!

# Wait only for the application. There is no terminal read/poll loop, so the
# harness cannot print stray keypress characters or remain stuck waiting for
# Enter after the GUI closes.
set +e
wait "$APP_PID"
APP_STATUS=$?
set -e
APP_PID=""

trap - INT TERM

echo
if (( interrupted != 0 )); then
    echo "Trace run interrupted; resolving all records flushed before shutdown..."
else
    echo "RetroSpectrum exited with status $APP_STATUS; resolving function calls..."
fi

python3 - "$RAW_TRACE" "$TRACE_BIN" "$TEXT_TRACE" <<'PY'
import pathlib
import struct
import subprocess
import sys

raw_path = pathlib.Path(sys.argv[1])
binary = pathlib.Path(sys.argv[2])
out_path = pathlib.Path(sys.argv[3])
record_struct = struct.Struct("<QQQ")

if not raw_path.exists():
    out_path.write_text("# No functions were captured.\n", encoding="utf-8")
    print(f"No raw trace was created; wrote: {out_path}")
    raise SystemExit(0)

data = raw_path.read_bytes()
complete = len(data) - (len(data) % record_struct.size)
records = [record_struct.unpack_from(data, off)
           for off in range(0, complete, record_struct.size)]

if not records:
    out_path.write_text("# No functions were captured.\n", encoding="utf-8")
    print(f"No functions captured; wrote: {out_path}")
    raise SystemExit(0)

# There is at most one record per function, so this list is bounded by the
# number of project functions rather than by the number of runtime calls.
records.sort(key=lambda item: item[0])
addresses = [address for _sequence, address, _tid in records]
resolved = {}
chunk_size = 1000

for start in range(0, len(addresses), chunk_size):
    chunk = addresses[start:start + chunk_size]
    cmd = ["addr2line", "-f", "-C", "-e", str(binary), *[hex(a) for a in chunk]]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, check=True)
    lines = proc.stdout.splitlines()

    for i, address in enumerate(chunk):
        function = lines[2 * i].strip() if 2 * i < len(lines) else "??"
        location = lines[2 * i + 1].strip() if 2 * i + 1 < len(lines) else "??:0"
        file_part = location.rsplit(":", 1)[0] if ":" in location else location
        filename = "?" if file_part in {"??", "?"} else pathlib.Path(file_part).name
        resolved[address] = (filename, function)

with out_path.open("w", encoding="utf-8") as out:
    out.write("# RetroSpectrum Unique Function Trace\n")
    out.write("# Each instrumented project function appears once, in first-seen order.\n")
    out.write("# Format: [FILENAME] [FUNCTION NAME]\n\n")
    for _sequence, address, _tid in records:
        filename, function = resolved.get(address, ("?", "??"))
        out.write(f"[{filename}] [{function}]\n")

print(f"Captured {len(records)} unique project functions.")
print(f"Wrote: {out_path}")
PY
echo
echo "Readable trace: $TEXT_TRACE"
echo "Raw trace:      $RAW_TRACE"

# Preserve RetroSpectrum's exit status for normal runs. If the user interrupted
# the tracer, return success after producing the partial trace.
if (( interrupted != 0 )); then
    exit 0
fi
exit "$APP_STATUS"
