CC = gcc

CFLAGS = -Wall -Wextra -O2 -std=c11 -Iinclude
LDLIBS = -lhackrf -lfftw3 -lSDL2 -lSDL2_ttf -lm -lpthread

SRC = src/RetroSpectrum.c src/GUIs.c src/ClassificationWorkstation.c src/AnalysisWorkstation.c
OBJ = $(SRC:src/%.c=build/%.o)

TARGET = RetroSpectrum

YELLOW = \033[1;33m
GREEN  = \033[1;32m
RESET  = \033[0m

TOTAL_STEPS := $(shell echo $$(($(words $(OBJ)) + 1)))
STEP_FILE = build/.build_step

all: init_progress $(TARGET)
	@printf "$(GREEN)[100%%] Build complete: ./$(TARGET)$(RESET)\n"

init_progress:
	@mkdir -p build
	@echo 0 > $(STEP_FILE)

$(TARGET): $(OBJ)
	@step=$$(cat $(STEP_FILE)); \
	step=$$((step + 1)); \
	echo $$step > $(STEP_FILE); \
	percent=$$((step * 100 / $(TOTAL_STEPS))); \
	printf "$(YELLOW)[%3d%%] Linking $(TARGET)$(RESET)\n" $$percent
	$(CC) $(OBJ) -o $(TARGET) $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p build
	@step=$$(cat $(STEP_FILE) 2>/dev/null || echo 0); \
	step=$$((step + 1)); \
	echo $$step > $(STEP_FILE); \
	percent=$$((step * 100 / $(TOTAL_STEPS))); \
	printf "$(YELLOW)[%3d%%] Compiling $<$(RESET)\n" $$percent
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f build/*.o build/.build_step $(TARGET)
	@printf "$(GREEN)Clean complete$(RESET)\n"
