#!/usr/bin/env bash
set -u
set -o pipefail

GREEN=$'\033[92m'
RED=$'\033[91m'
YELLOW=$'\033[93m'
RESET=$'\033[0m'

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
TEST_BUILD="$BUILD/full_tests"
TEST_SOURCE="$SCRIPT_DIR/RetroSpectrumFullTests.c"

pass_count=0
fail_count=0
skip_count=0

pass() {
    pass_count=$((pass_count + 1))
    printf '%sPASSED%s %s\n' "$GREEN" "$RESET" "$1"
}

fail() {
    fail_count=$((fail_count + 1))
    printf '%sFAILED%s %s\n' "$RED" "$RESET" "$1"
}

skip() {
    skip_count=$((skip_count + 1))
    printf '%sSKIPPED%s %s\n' "$YELLOW" "$RESET" "$1"
}

if [[ ! -f "$ROOT/Makefile" || ! -f "$SRC/RetroSpectrum.c" ]]; then
    printf '%sFAILED%s Run this from a RetroSpectrum project where tests/ is beside src/ and Makefile.\n' "$RED" "$RESET"
    exit 1
fi

for command in make gcc pkg-config; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf '%sFAILED%s Missing required command: %s\n' "$RED" "$RESET" "$command"
        exit 1
    fi
done

mkdir -p "$TEST_BUILD"
rm -f "$TEST_BUILD"/*.o "$TEST_BUILD"/test_* "$TEST_BUILD"/*.log

printf 'Building the normal application first...\n'
if make -C "$ROOT" clean >/dev/null 2>&1 && make -C "$ROOT"; then
    pass "normal RetroSpectrum build"
else
    fail "normal RetroSpectrum build"
    exit 1
fi

declare -a EXPECTED_OBJECTS=(
    RetroSpectrum AuthScreen AuthAdmin ServerIdentity SecureNetwork
    DatabaseCrypto DataStore MapDashboard GUIs AnalysisWorkstation
    ClassificationWorkstation CaseManagementWorkstation DecodeWorkstation
    CorrelationWorkstation SecureFunctions
)

for object in "${EXPECTED_OBJECTS[@]}"; do
    if [[ ! -f "$BUILD/$object.o" ]]; then
        fail "missing build object: build/$object.o"
    fi
done
if (( fail_count > 0 )); then
    exit 1
fi
pass "all production object files found"

CFLAGS=(
    -std=gnu11 -O0 -g3 -fcommon
    -Wall -Wextra
    -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter
    -Wno-sign-compare -Wno-format-truncation -Wno-format-overflow
    -fno-omit-frame-pointer
    -fsanitize=address,undefined
)

# The normal Makefile may keep project headers outside src/ (for example,
# include/, headers/, or the project root). Discover every directory that
# actually contains a project header and add it to the test compiler path.
declare -a PROJECT_INCLUDE_DIRS=("$SRC" "$ROOT")
while IFS= read -r -d '' header_dir; do
    already_added=0
    for existing_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
        if [[ "$existing_dir" == "$header_dir" ]]; then
            already_added=1
            break
        fi
    done
    if (( ! already_added )); then
        PROJECT_INCLUDE_DIRS+=("$header_dir")
    fi
done < <(find "$ROOT" \
    -path "$BUILD" -prune -o \
    -path "$ROOT/.git" -prune -o \
    -type f -name '*.h' -printf '%h\0' | sort -zu)

for include_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
    CFLAGS+=("-I$include_dir")
done

printf 'Project include directories used by tests:\n'
for include_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
    printf '  %s\n' "$include_dir"
done

LDFLAGS=(
    -fsanitize=address,undefined
    -Wl,-z,relro,-z,now
)

declare -a PKGS=(sdl2 SDL2_ttf SDL2_image fftw3 SoapySDR openssl)
declare -a PKG_CFLAGS=()
declare -a PKG_LIBS=()

for pkg in "${PKGS[@]}"; do
    if pkg-config --exists "$pkg"; then
        while IFS= read -r token; do
            [[ -n "$token" ]] && PKG_CFLAGS+=("$token")
        done < <(pkg-config --cflags "$pkg" | xargs -n1)
        while IFS= read -r token; do
            [[ -n "$token" ]] && PKG_LIBS+=("$token")
        done < <(pkg-config --libs "$pkg" | xargs -n1)
    else
        fail "pkg-config package unavailable: $pkg"
    fi
done

if pkg-config --exists sqlcipher; then
    while IFS= read -r token; do
        [[ -n "$token" ]] && PKG_CFLAGS+=("$token")
    done < <(pkg-config --cflags sqlcipher | xargs -n1)
    while IFS= read -r token; do
        [[ -n "$token" ]] && PKG_LIBS+=("$token")
    done < <(pkg-config --libs sqlcipher | xargs -n1)
else
    PKG_LIBS+=(-lsqlcipher)
fi

PKG_LIBS+=(-largon2 -lpthread -lm -ldl)

if (( fail_count > 0 )); then
    exit 1
fi
pass "compiler and dependency flags resolved"

# The existing RetroSpectrum object contains the production main(). Compile a
# test-only copy with its main renamed so all of its globals remain available
# while each per-source test binary supplies its own main().
RENAMED_RETRO="$TEST_BUILD/RetroSpectrum_renamed.o"
if gcc "${CFLAGS[@]}" "${PKG_CFLAGS[@]}" \
       -Dmain=retrospectrum_application_main \
       -c "$SRC/RetroSpectrum.c" -o "$RENAMED_RETRO"; then
    pass "test-only RetroSpectrum object with renamed main"
else
    fail "test-only RetroSpectrum object with renamed main"
    exit 1
fi

declare -a TARGET_SOURCES=(
    AnalysisWorkstation.c
    AuthAdmin.c
    AuthScreen.c
    CaseManagementWorkstation.c
    ClassificationWorkstation.c
    CorrelationWorkstation.c
    DataStore.c
    DatabaseCrypto.c
    DecodeWorkstation.c
    GUIs.c
    MapDashboard.c
    RetroSpectrum.c
    SecureFunctions.c
    SecureNetwork.c
    ServerIdentity.c
)

declare -a TARGET_OBJECTS=(
    AnalysisWorkstation
    AuthAdmin
    AuthScreen
    CaseManagementWorkstation
    ClassificationWorkstation
    CorrelationWorkstation
    DataStore
    DatabaseCrypto
    DecodeWorkstation
    GUIs
    MapDashboard
    RetroSpectrum
    SecureFunctions
    SecureNetwork
    ServerIdentity
)

# Some older layouts contain this additional source. The current 15-source
# application generally keeps the world-map implementation in MapDashboard.c.
if [[ -f "$SRC/world_map_bin_loader.c" ]]; then
    TARGET_SOURCES+=(world_map_bin_loader.c)
    TARGET_OBJECTS+=(NONE)
fi

run_target() {
    local index="$1"
    local source_name="${TARGET_SOURCES[$((index - 1))]}"
    local object_name="${TARGET_OBJECTS[$((index - 1))]}"
    local safe_name="${source_name%.c}"
    local test_obj="$TEST_BUILD/test_${safe_name}.o"
    local test_bin="$TEST_BUILD/test_${safe_name}"
    local log="$TEST_BUILD/test_${safe_name}.log"
    local -a objects=()

    printf '\n=== %s ===\n' "$source_name"

    if ! gcc "${CFLAGS[@]}" "${PKG_CFLAGS[@]}" \
             -DRS_TEST_TARGET="$index" \
             -c "$TEST_SOURCE" -o "$test_obj" >"$log" 2>&1; then
        cat "$log"
        fail "$source_name test compilation"
        return
    fi
    pass "$source_name test compilation"

    if [[ "$source_name" == "world_map_bin_loader.c" ]]; then
        objects=("$test_obj")
    else
        objects=("$test_obj")
        for object in "${EXPECTED_OBJECTS[@]}"; do
            if [[ "$object" == "$object_name" || "$object" == "RetroSpectrum" ]]; then
                continue
            fi
            objects+=("$BUILD/$object.o")
        done
        if [[ "$source_name" != "RetroSpectrum.c" ]]; then
            objects+=("$RENAMED_RETRO")
        fi
    fi

    if ! gcc "${LDFLAGS[@]}" -o "$test_bin" "${objects[@]}" "${PKG_LIBS[@]}" >"$log" 2>&1; then
        cat "$log"
        fail "$source_name test linking"
        return
    fi
    pass "$source_name test linking"

    if env \
        RS_TEST_PROJECT_ROOT="$ROOT" \
        RS_TEST_QUIET="${RS_TEST_QUIET:-0}" \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1:strict_string_checks=1" \
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        timeout "${RS_TEST_TIMEOUT:-120}" "$test_bin" 2>&1 | tee "$log"; then
        pass "$source_name runtime suite"
    else
        fail "$source_name runtime suite"
    fi
}

for index in $(seq 1 "${#TARGET_SOURCES[@]}"); do
    run_target "$index"
done

printf '\n=== Optional external analyzers ===\n'
if command -v cppcheck >/dev/null 2>&1; then
    declare -a CPPCHECK_INCLUDES=()
    for include_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
        CPPCHECK_INCLUDES+=("-I$include_dir")
    done
    if cppcheck --enable=warning,performance,portability \
                --check-level=exhaustive \
                --error-exitcode=1 --inline-suppr \
                --suppress=missingIncludeSystem \
                -DOPENSSL_VERSION_NUMBER=0x30500000L \
                "${CPPCHECK_INCLUDES[@]}" \
                "$SRC" 2>"$TEST_BUILD/cppcheck.log"; then
        pass "cppcheck warning/performance/portability analysis"
    else
        cat "$TEST_BUILD/cppcheck.log"
        fail "cppcheck warning/performance/portability analysis"
    fi
else
    skip "cppcheck not installed"
fi

if command -v clang >/dev/null 2>&1; then
    analyzer_failed=0
    declare -a CLANG_ANALYZER_FLAGS=(-std=gnu11 -D_GNU_SOURCE)
    for include_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
        CLANG_ANALYZER_FLAGS+=("-I$include_dir")
    done
    for source in "${TARGET_SOURCES[@]}"; do
        if ! clang --analyze "${CLANG_ANALYZER_FLAGS[@]}" "${PKG_CFLAGS[@]}" "$SRC/$source" \
             -Xanalyzer -analyzer-output=text \
             >"$TEST_BUILD/clang_${source%.c}.log" 2>&1; then
            cat "$TEST_BUILD/clang_${source%.c}.log"
            analyzer_failed=1
        fi
    done
    if (( analyzer_failed == 0 )); then
        pass "Clang Static Analyzer across production sources"
    else
        fail "Clang Static Analyzer across production sources"
    fi
else
    skip "clang not installed"
fi

printf '\n%sRetroSpectrum exhaustive test summary%s\n' "$GREEN" "$RESET"
printf '  %sPassed:  %d%s\n' "$GREEN" "$pass_count" "$RESET"
printf '  %sFailed:  %d%s\n' "$RED" "$fail_count" "$RESET"
printf '  %sSkipped: %d%s\n' "$YELLOW" "$skip_count" "$RESET"
printf '  Detailed logs: %s\n' "$TEST_BUILD"

if (( fail_count > 0 )); then
    exit 1
fi
exit 0
