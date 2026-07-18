CC ?= gcc
CFLAGS = -std=c99 -D_GNU_SOURCE -O2 -Wall -Wno-unused-result -Wno-unused-variable -Wno-unused-function \
         -Wno-missing-braces -Wno-unused-but-set-variable \
         -Isrc -Idep -Idep/glad \
         $(shell pkg-config --cflags glfw3 luajit glesv2 egl zlib libwebp 2>/dev/null)
LDFLAGS = $(shell pkg-config --libs glfw3 luajit glesv2 egl zlib libwebp 2>/dev/null) \
          -lm -lpthread -ldl -luuid

SRCS = src/main.c \
       src/common.c \
       src/console.c \
       src/streams.c \
       src/sys_main.c \
       src/sys_video.c \
       src/sys_opengl.c \
       src/sys_console.c \
       src/core_config.c \
       src/core_video.c \
       src/core_image.c \
       src/core_main.c \
       src/r_main.c \
       src/r_texture.c \
       src/r_font.c \
       src/ui_main.c \
       src/ui_api.c \
       src/ui_subscript.c

OBJS = $(SRCS:.c=.o)
TARGET = pob

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
