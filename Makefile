# ============================================================================
# RetroSpectrum Makefile
# Project layout:
#   include/*.h
#   src/*.c
#   build/
#
# Important:
#   - RetroSpectrum.c is the dashboard entry point.
#   - world_map_bin_loader.c is included inside RetroSpectrum.c with
#     WORLD_MAP_NO_DEMO, so DO NOT compile it separately.
#   - RetroSpectrum_dashboard_only.c is an extra copy/backup, so DO NOT compile it.
# ============================================================================

CC      := gcc
TARGET  := build/retrospectrum

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

SRCS := \
	$(SRC_DIR)/RetroSpectrum.c \
	$(SRC_DIR)/GUIs.c \
	$(SRC_DIR)/AnalysisWorkstation.c \
	$(SRC_DIR)/ClassificationWorkstation.c

OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

CFLAGS  := -Wall -Wextra -O2 -std=c11 -I$(INC_DIR)
LDFLAGS :=
LDLIBS  := -lhackrf -lfftw3 -lSDL2 -lSDL2_ttf -lSDL2_image -lm -lpthread

MAP_SRC := $(SRC_DIR)/world_map.bin
MAP_DST := $(BUILD_DIR)/world_map.bin

.PHONY: all clean run assets

all: $(TARGET) assets

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

assets: | $(BUILD_DIR)
	@if [ -f "$(MAP_SRC)" ]; then cp "$(MAP_SRC)" "$(MAP_DST)"; fi
	@if [ -d "$(SRC_DIR)/flags" ]; then cp -r "$(SRC_DIR)/flags" "$(BUILD_DIR)/flags"; fi
	@if [ -d "flags" ]; then cp -r "flags" "$(BUILD_DIR)/flags"; fi

run: all
	cd $(BUILD_DIR) && ./retrospectrum

clean:
	rm -rf $(BUILD_DIR)/*.o $(TARGET) $(MAP_DST) $(BUILD_DIR)/flags
