# ---- loom — build -------------------------------------------------------
CC      ?= cc
CFLAGS  += -std=c99 -Wall -Wextra -O2 -MMD -MP

RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS   := $(shell pkg-config --libs   raylib 2>/dev/null)

ifeq ($(strip $(RAYLIB_LIBS)),)
  # Fallback when pkg-config has no raylib entry (typical on Fedora/Nobara).
  RAYLIB_LIBS := -lraylib -lGL -lpthread -ldl -lrt -lX11
endif

CFLAGS += $(RAYLIB_CFLAGS)

TARGET := loom
SRCS   := main.c menu.c settings.c game.c grid.c audio.c paths.c
OBJS   := $(SRCS:.c=.o)
DEPS   := $(OBJS:.o=.d)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(RAYLIB_LIBS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)

-include $(DEPS)

.PHONY: run clean
