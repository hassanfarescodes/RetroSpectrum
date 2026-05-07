CC := gcc

TARGET := RetroSpectrum

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

SRC := $(SRC_DIR)/RetroSpectrum.c
OBJ := $(BUILD_DIR)/RetroSpectrum.o

CFLAGS := -std=c11 -Wall -Wextra -O2 -I$(INC_DIR)
LDFLAGS := -lhackrf -lfftw3 -lm -lSDL2 -lSDL2_ttf -lpthread

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
