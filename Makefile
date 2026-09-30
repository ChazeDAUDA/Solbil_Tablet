TARGET  := solbil-tablet
BUILD   := build

CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -O2 -Iinclude $(shell sdl2-config --cflags)
LDLIBS  := $(shell sdl2-config --libs) -lSDL2_image -lSDL2_ttf -lm

SRCS    := $(wildcard src/*.c)
OBJS    := $(SRCS:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/$(TARGET)

$(BUILD)/$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)

$(BUILD)/%.o: src/%.c $(wildcard include/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: all
	./$(BUILD)/$(TARGET)

dry: all
	./$(BUILD)/$(TARGET) -n

clean:
	rm -rf $(BUILD)

.PHONY: all run dry clean
