#!/usr/bin/env bash
set -u
set -o pipefail

GREEN=$'\033[92m'
RED=$'\033[91m'
YELLOW=$'\033[93m'
CYAN=$'\033[96m'
RESET=$'\033[0m'

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
SEC_BUILD="$BUILD/security_tests"
TEST_SOURCE="$SCRIPT_DIR/RetroSpectrumSecurityTests.c"

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

if [[ ! -f "$ROOT/Makefile" || ! -f "$SRC/RetroSpectrum.c" || ! -f "$TEST_SOURCE" ]]; then
    printf '%sFAILED%s Expected tests/ beside src/ and Makefile, with RetroSpectrumSecurityTests.c in tests/.\n' "$RED" "$RESET"
    exit 1
fi

for command in make gcc pkg-config timeout; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf '%sFAILED%s Missing required command: %s\n' "$RED" "$RESET" "$command"
        exit 1
    fi
done

mkdir -p "$SEC_BUILD"
rm -f "$SEC_BUILD"/*.o "$SEC_BUILD"/security_* "$SEC_BUILD"/*.log

printf '%sBuilding the normal application first...%s\n' "$CYAN" "$RESET"
if [[ "${RS_SECURITY_SKIP_NORMAL_BUILD:-0}" == "1" ]]; then
    skip "normal RetroSpectrum build (RS_SECURITY_SKIP_NORMAL_BUILD=1)"
elif make -C "$ROOT" clean >/dev/null 2>&1 && make -C "$ROOT"; then
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

# Security tests intentionally use modest optimization so _FORTIFY_SOURCE=3 is
# active while ASan/UBSan still retain useful source locations.
CFLAGS=(
    -std=gnu11 -O1 -g3 -fcommon
    -Wall -Wextra -Wformat=2 -Wformat-security
    -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter
    -Wno-sign-compare -Wno-format-truncation -Wno-format-overflow
    -Werror=implicit-function-declaration
    -fno-omit-frame-pointer
    -fstack-protector-strong
    -fstack-clash-protection
    -D_FORTIFY_SOURCE=3
    -fsanitize=address,undefined
)

# Add control-flow protection where GCC supports it on the current target.
if printf 'int main(void){return 0;}\n' | gcc -x c - -fcf-protection=full -o /tmp/rs-sec-cf-test >/dev/null 2>&1; then
    CFLAGS+=(-fcf-protection=full)
    rm -f /tmp/rs-sec-cf-test
fi

declare -a PROJECT_INCLUDE_DIRS=("$SRC" "$ROOT")
while IFS= read -r -d '' header_dir; do
    already_added=0
    for existing_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
        if [[ "$existing_dir" == "$header_dir" ]]; then
            already_added=1
            break
        fi
    done
    if (( ! already_added )); then PROJECT_INCLUDE_DIRS+=("$header_dir"); fi
done < <(find "$ROOT" \
    -path "$BUILD" -prune -o \
    -path "$ROOT/.git" -prune -o \
    -type f -name '*.h' -printf '%h\0' | sort -zu)

for include_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
    CFLAGS+=("-I$include_dir")
done

printf '%sProject include directories used by security tests:%s\n' "$CYAN" "$RESET"
for include_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do
    printf '  %s\n' "$include_dir"
done

LDFLAGS=(
    -fsanitize=address,undefined
    -Wl,-z,relro,-z,now,-z,noexecstack
)

declare -a PKGS=(sdl2 SDL2_ttf SDL2_image fftw3 SoapySDR openssl)
declare -a PKG_CFLAGS=()
declare -a PKG_LIBS=()

for pkg in "${PKGS[@]}"; do
    if pkg-config --exists "$pkg"; then
        while IFS= read -r token; do [[ -n "$token" ]] && PKG_CFLAGS+=("$token"); done < <(pkg-config --cflags "$pkg" | xargs -n1)
        while IFS= read -r token; do [[ -n "$token" ]] && PKG_LIBS+=("$token"); done < <(pkg-config --libs "$pkg" | xargs -n1)
    else
        fail "pkg-config package unavailable: $pkg"
    fi
done

if pkg-config --exists sqlcipher; then
    while IFS= read -r token; do [[ -n "$token" ]] && PKG_CFLAGS+=("$token"); done < <(pkg-config --cflags sqlcipher | xargs -n1)
    while IFS= read -r token; do [[ -n "$token" ]] && PKG_LIBS+=("$token"); done < <(pkg-config --libs sqlcipher | xargs -n1)
else
    PKG_LIBS+=(-lsqlcipher)
fi
PKG_LIBS+=(-largon2 -lpthread -lm -ldl)

if (( fail_count > 0 )); then exit 1; fi
pass "security compiler and dependency flags resolved"

# Test-only RetroSpectrum object with production main renamed. Other target
# binaries need its globals but provide their own main from the security suite.
RENAMED_RETRO="$SEC_BUILD/RetroSpectrum_renamed.o"
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

if [[ -f "$SRC/world_map_bin_loader.c" ]]; then
    TARGET_SOURCES+=(world_map_bin_loader.c)
    TARGET_OBJECTS+=(NONE)
fi

run_target() {
    local index="$1"
    local source_name="${TARGET_SOURCES[$((index - 1))]}"
    local object_name="${TARGET_OBJECTS[$((index - 1))]}"
    local safe_name="${source_name%.c}"
    local test_obj="$SEC_BUILD/security_${safe_name}.o"
    local test_bin="$SEC_BUILD/security_${safe_name}"
    local compile_log="$SEC_BUILD/security_${safe_name}_compile.log"
    local runtime_log="$SEC_BUILD/security_${safe_name}_runtime.log"
    local -a objects=()

    printf '\n%s=== SECURITY TARGET: %s ===%s\n' "$CYAN" "$source_name" "$RESET"

    if ! gcc "${CFLAGS[@]}" "${PKG_CFLAGS[@]}" \
             -DRS_SECURITY_TARGET="$index" \
             -c "$TEST_SOURCE" -o "$test_obj" >"$compile_log" 2>&1; then
        cat "$compile_log"
        fail "$source_name security-test compilation"
        return
    fi
    pass "$source_name security-test compilation"

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

    if ! gcc "${LDFLAGS[@]}" -o "$test_bin" "${objects[@]}" "${PKG_LIBS[@]}" >"$compile_log" 2>&1; then
        cat "$compile_log"
        fail "$source_name security-test linking"
        return
    fi
    pass "$source_name security-test linking"

    # Confirm the generated test executable is not requesting an executable stack.
    if command -v readelf >/dev/null 2>&1; then
        if readelf -W -l "$test_bin" | grep -E 'GNU_STACK' | grep -q 'RWE'; then
            fail "$source_name executable-stack hardening"
        else
            pass "$source_name executable-stack hardening"
        fi
    fi

    if env \
        RS_SECURITY_PROJECT_ROOT="$ROOT" \
        RS_SECURITY_QUIET="${RS_SECURITY_QUIET:-0}" \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1" \
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        MALLOC_PERTURB_="${RS_SECURITY_MALLOC_PERTURB:-173}" \
        timeout "${RS_SECURITY_TIMEOUT:-180}" "$test_bin" 2>&1 | tee "$runtime_log"; then
        pass "$source_name adversarial runtime suite"
    else
        fail "$source_name adversarial runtime suite"
    fi
}

for index in $(seq 1 "${#TARGET_SOURCES[@]}"); do
    run_target "$index"
done

printf '\n%s=== SECURITY STATIC ANALYZERS ===%s\n' "$CYAN" "$RESET"

if command -v cppcheck >/dev/null 2>&1; then
    declare -a CPPCHECK_INCLUDES=()
    for include_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do CPPCHECK_INCLUDES+=("-I$include_dir"); done
    if cppcheck --enable=warning,performance,portability \
                --check-level=exhaustive \
                --error-exitcode=1 --inline-suppr \
                --suppress=missingIncludeSystem \
                -DOPENSSL_VERSION_NUMBER=0x30500000L \
                "${CPPCHECK_INCLUDES[@]}" "$SRC" 2>"$SEC_BUILD/cppcheck_security.log"; then
        pass "cppcheck security-oriented analysis"
    else
        cat "$SEC_BUILD/cppcheck_security.log"
        fail "cppcheck security-oriented analysis"
    fi
else
    skip "cppcheck not installed"
fi

if command -v clang >/dev/null 2>&1; then
    analyzer_failed=0
    declare -a CLANG_FLAGS=(-std=gnu11 -D_GNU_SOURCE -D_FORTIFY_SOURCE=3 -fdiagnostics-show-option)
    for include_dir in "${PROJECT_INCLUDE_DIRS[@]}"; do CLANG_FLAGS+=("-I$include_dir"); done
    for source in "${TARGET_SOURCES[@]}"; do
        log="$SEC_BUILD/clang_${source%.c}.log"
        if ! clang --analyze "${CLANG_FLAGS[@]}" "${PKG_CFLAGS[@]}" "$SRC/$source" \
             -Xanalyzer -analyzer-output=text >"$log" 2>&1; then
            cat "$log"
            analyzer_failed=1
            continue
        fi
        # Clang Static Analyzer findings are emitted as warnings tagged with
        # checker names such as [unix.Malloc] or [core.NullDereference], while
        # clang can still exit 0. Treat any analyzer checker finding as failure.
        if grep -Eq 'warning:.*\[[A-Za-z][A-Za-z0-9_.-]*\]' "$log"; then
            cat "$log"
            analyzer_failed=1
        fi
    done
    if (( analyzer_failed == 0 )); then pass "Clang Static Analyzer across all sources (no analyzer findings)"; else fail "Clang Static Analyzer across all sources (findings detected)"; fi
else
    skip "clang not installed"
fi

# GCC -fanalyzer catches ownership mistakes, NULL flows, double-free, stale
# pointers, and some out-of-bounds paths. Probe the option directly because
# `gcc -Q --help=common` reports supported options as disabled by default.
# Analyzer findings are warnings, so explicitly scan the diagnostic logs for
# -Wanalyzer-* findings instead of relying only on GCC's process exit status.
FANALYZER_PROBE="$SEC_BUILD/fanalyzer_probe.c"
printf 'int main(void) { return 0; }\n' >"$FANALYZER_PROBE"
if gcc -std=gnu11 -fanalyzer -fsyntax-only "$FANALYZER_PROBE" >/dev/null 2>&1; then
    analyzer_failed=0
    for source in "${TARGET_SOURCES[@]}"; do
        log="$SEC_BUILD/gcc_analyzer_${source%.c}.log"
        if ! gcc -std=gnu11 -O0 -fanalyzer -Wall -Wextra -fdiagnostics-show-option \
             "${PKG_CFLAGS[@]}" "${PROJECT_INCLUDE_DIRS[@]/#/-I}" \
             -fsyntax-only "$SRC/$source" >"$log" 2>&1; then
            cat "$log"
            analyzer_failed=1
            continue
        fi
        if grep -Eq '\[-Wanalyzer-[^]]+\]' "$log"; then
            cat "$log"
            analyzer_failed=1
        fi
    done
    if (( analyzer_failed == 0 )); then
        pass "GCC -fanalyzer across all sources"
    else
        fail "GCC -fanalyzer across all sources"
    fi
else
    skip "GCC -fanalyzer unavailable"
fi

if command -v flawfinder >/dev/null 2>&1; then
    # --error-level=3 makes Flawfinder return nonzero when it reports any
    # level-3-or-higher finding, instead of merely indicating that it ran.
    if flawfinder --minlevel=3 --error-level=3 --quiet "$SRC" >"$SEC_BUILD/flawfinder.log" 2>&1; then
        pass "flawfinder high-risk API scan (no level-3+ findings)"
    else
        cat "$SEC_BUILD/flawfinder.log"
        fail "flawfinder high-risk API scan (level-3+ findings detected)"
    fi
else
    skip "flawfinder not installed"
fi

printf '\n%sRetroSpectrum adversarial security test summary%s\n' "$CYAN" "$RESET"
printf '  %sPassed:  %d%s\n' "$GREEN" "$pass_count" "$RESET"
printf '  %sFailed:  %d%s\n' "$RED" "$fail_count" "$RESET"
printf '  %sSkipped: %d%s\n' "$YELLOW" "$skip_count" "$RESET"
printf '  Detailed logs: %s\n' "$SEC_BUILD"

if (( fail_count > 0 )); then exit 1; fi
exit 0
