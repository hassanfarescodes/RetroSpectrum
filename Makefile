CC = gcc

CFLAGS = -Wall -Wextra -O2 -std=c11 -Iinclude
LDLIBS = -lhackrf -lfftw3 -lSDL2 -lSDL2_ttf -lm -lpthread

SRC = src/RetroSpectrum.c src/GUIs.c
OBJ = $(SRC:src/%.c=build/%.o)

TARGET = RetroSpectrum

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDLIBS)

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f build/*.o $(TARGET)
