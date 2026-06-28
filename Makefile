# ============================================================================
# RetroSpectrum Makefile
# Project layout:
#   include/*.h
#   src/*.c
#   build/
#
# Important:
#   - RetroSpectrum.c is the main application entry point.
#   - MapDashboard.c contains the dashboard/map/case logic.
#   - world_map_bin_loader.c is included inside MapDashboard.c with
#     WORLD_MAP_NO_DEMO, so DO NOT compile it separately.
#   - RetroSpectrum_dashboard_only.c / map-only backups are not compiled.
# ============================================================================

CC      := gcc
TARGET  := build/retrospectrum

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

SRCS := \
	$(SRC_DIR)/RetroSpectrum.c \
	$(SRC_DIR)/MapDashboard.c \
	$(SRC_DIR)/GUIs.c \
	$(SRC_DIR)/AnalysisWorkstation.c \
	$(SRC_DIR)/ClassificationWorkstation.c \
	$(SRC_DIR)/CaseManagementWorkstation.c \
	$(SRC_DIR)/DecodeWorkstation.c

OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

CFLAGS  := -Wall -Wextra -O2 -std=c11 -I$(INC_DIR)
LDFLAGS :=
LDLIBS  := -lhackrf -lfftw3 -lSDL2 -lSDL2_ttf -lSDL2_image -lm -lpthread

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
	@$(CC) $(CFLAGS) -c $< -o $@ > "$(LOG_FILE)" 2>&1 || { printf "\n"; cat "$(LOG_FILE)"; exit 1; }
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
