# ============================================================================
# RetroSpectrum Makefile
# ============================================================================

CC      := gcc
TARGET  := build/retrospectrum

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

OPENSSL_MIN_VERSION := 3.5.6
SQLCIPHER_MIN_VERSION := 4.6.1
OPENSSL_VERSION := $(shell pkg-config --modversion openssl 2>/dev/null)
# Debian/Parrot's sqlcipher.pc reports the embedded SQLite version (for example
# 3.46.1), not the SQLCipher release. Ask SQLCipher itself for cipher_version.
SQLCIPHER_VERSION := $(shell sqlcipher ':memory:' 'PRAGMA cipher_version;' 2>/dev/null | awk 'NR == 1 { print $$1 }')
OPENSSL_OK := $(shell pkg-config --atleast-version=$(OPENSSL_MIN_VERSION) openssl 2>/dev/null && echo 1 || echo 0)
SQLCIPHER_OK := $(shell \
	if pkg-config --exists sqlcipher 2>/dev/null && \
	   command -v sqlcipher >/dev/null 2>&1 && \
	   [ -n "$(SQLCIPHER_VERSION)" ] && \
	   dpkg --compare-versions "$(SQLCIPHER_VERSION)" ge "$(SQLCIPHER_MIN_VERSION)"; \
	then echo 1; else echo 0; fi)

ifeq ($(OPENSSL_OK),0)
$(error OpenSSL $(OPENSSL_MIN_VERSION)+ development files are required. Found: $(OPENSSL_VERSION))
endif
ifeq ($(SQLCIPHER_OK),0)
$(error SQLCipher $(SQLCIPHER_MIN_VERSION)+ runtime and development files are required. PRAGMA cipher_version reported: $(SQLCIPHER_VERSION))
endif

SRCS := \
	$(SRC_DIR)/RetroSpectrum.c \
	$(SRC_DIR)/AuthScreen.c \
	$(SRC_DIR)/AuthAdmin.c \
	$(SRC_DIR)/ServerIdentity.c \
	$(SRC_DIR)/SecureNetwork.c \
	$(SRC_DIR)/DatabaseCrypto.c \
	$(SRC_DIR)/DataStore.c \
	$(SRC_DIR)/MapDashboard.c \
	$(SRC_DIR)/GUIs.c \
	$(SRC_DIR)/AnalysisWorkstation.c \
	$(SRC_DIR)/ClassificationWorkstation.c \
	$(SRC_DIR)/CaseManagementWorkstation.c \
	$(SRC_DIR)/DecodeWorkstation.c

OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

CPPFLAGS := -I$(INC_DIR) $(shell pkg-config --cflags openssl sqlcipher)
CFLAGS  := -Wall -Wextra -Wformat=2 -Wformat-security -O2 -std=c11 \
           -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fPIE -fno-common
LDFLAGS := -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
LDLIBS  := -lhackrf -lfftw3 -lSDL2 -lSDL2_ttf -lSDL2_image -largon2 \
           $(shell pkg-config --libs openssl sqlcipher) -lm -lpthread

MAP_SRC := $(SRC_DIR)/world_map.bin
MAP_DST := $(BUILD_DIR)/world_map.bin

TOTAL_STEPS := $(shell expr $(words $(OBJS)) + 2)
BAR_WIDTH   := 30
COUNT_FILE  := $(BUILD_DIR)/.build_count
LOG_FILE    := $(BUILD_DIR)/.build_last_command.log

YELLOW := \033[33m
GREEN  := \033[32m
RESET  := \033[0m
CLEAR  := \033[K

define DRAW_PROGRESS
count=$$(cat "$(COUNT_FILE)" 2>/dev/null || echo 0); \
percent=$$((count * 100 / $(TOTAL_STEPS))); \
filled=$$((percent * $(BAR_WIDTH) / 100)); \
bar=""; \
i=0; \
while [ $$i -lt $$filled ]; do bar="$${bar}#"; i=$$((i + 1)); done; \
while [ $$i -lt $(BAR_WIDTH) ]; do bar="$${bar}-"; i=$$((i + 1)); done; \
if [ "$$percent" -ge 100 ]; then \
	printf "$(GREEN)\rBuild progress [%s] %3d%%$(RESET)" "$$bar" "$$percent"; \
else \
	printf "$(YELLOW)\rBuild progress [%s] %3d%%$(RESET)" "$$bar" "$$percent"; \
fi
endef

define STEP_DONE
count=$$(cat "$(COUNT_FILE)" 2>/dev/null || echo 0); \
count=$$((count + 1)); \
printf "%s\n" "$$count" > "$(COUNT_FILE)"; \
$(DRAW_PROGRESS)
endef

.PHONY: all clean run assets progress-start progress-done

all: progress-start $(TARGET) assets progress-done

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

progress-start: | $(BUILD_DIR)
	@printf "0\n" > "$(COUNT_FILE)"
	@printf "Reading build configuration...\n"
	@printf "Found $(words $(SRCS)) source files and 1 asset step.\n"
	@$(DRAW_PROGRESS)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	@printf "\r$(CLEAR)Linking executable: $@\n"
	@$(DRAW_PROGRESS)
	@$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS) > "$(LOG_FILE)" 2>&1 || { printf "\n"; cat "$(LOG_FILE)"; exit 1; }
	@rm -f "$(LOG_FILE)"
	@$(STEP_DONE)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@printf "\r$(CLEAR)Compiling: $< -> $@\n"
	@$(DRAW_PROGRESS)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@ > "$(LOG_FILE)" 2>&1 || { printf "\n"; cat "$(LOG_FILE)"; exit 1; }
	@rm -f "$(LOG_FILE)"
	@$(STEP_DONE)

assets: | $(BUILD_DIR)
	@printf "\r$(CLEAR)Installing runtime assets...\n"
	@$(DRAW_PROGRESS)
	@if [ -f "$(MAP_SRC)" ]; then \
		printf "\r$(CLEAR)Copying: $(MAP_SRC) -> $(MAP_DST)\n"; \
		$(DRAW_PROGRESS); \
		cp "$(MAP_SRC)" "$(MAP_DST)" > "$(LOG_FILE)" 2>&1 || { printf "\n"; cat "$(LOG_FILE)"; exit 1; }; \
	else \
		printf "\r$(CLEAR)Skipping: $(MAP_SRC) not found\n"; \
		$(DRAW_PROGRESS); \
	fi
	@if [ -d "$(SRC_DIR)/flags" ]; then \
		printf "\r$(CLEAR)Copying: $(SRC_DIR)/flags -> $(BUILD_DIR)/flags\n"; \
		$(DRAW_PROGRESS); \
		cp -r "$(SRC_DIR)/flags" "$(BUILD_DIR)/flags" > "$(LOG_FILE)" 2>&1 || { printf "\n"; cat "$(LOG_FILE)"; exit 1; }; \
	fi
	@if [ -d "flags" ]; then \
		printf "\r$(CLEAR)Copying: flags -> $(BUILD_DIR)/flags\n"; \
		$(DRAW_PROGRESS); \
		cp -r "flags" "$(BUILD_DIR)/flags" > "$(LOG_FILE)" 2>&1 || { printf "\n"; cat "$(LOG_FILE)"; exit 1; }; \
	fi
	@rm -f "$(LOG_FILE)"
	@$(STEP_DONE)

progress-done:
	@printf "$(GREEN)\rBuild progress [##############################] 100%%$(RESET)\n"
	@printf "Build complete: $(TARGET)\n"
	@rm -f "$(COUNT_FILE)"

run: all
	@cd $(BUILD_DIR) && ./retrospectrum

clean:
	@printf "Cleaning build outputs...\n"
	@rm -rf $(BUILD_DIR)/*.o $(TARGET) $(MAP_DST) $(BUILD_DIR)/flags $(COUNT_FILE) $(LOG_FILE)
