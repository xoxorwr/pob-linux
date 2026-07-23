CC ?= clang
CFLAGS = -std=c99 -D_GNU_SOURCE -O3 -flto -Wall -Wno-unused-result -Wno-unused-variable -Wno-unused-function \
         -Wno-missing-braces -Wno-unused-but-set-variable \
         -Isrc -Idep -Idep/glad \
         -Idep/luajit/src
DBG_CFLAGS = -std=c99 -D_GNU_SOURCE -O0 -g -fno-omit-frame-pointer -Wall \
             -Wno-unused-result -Wno-unused-variable -Wno-unused-function \
             -Wno-missing-braces -Wno-unused-but-set-variable \
             -Isrc -Idep -Idep/glad \
             -Idep/luajit/src
DBG_LDFLAGS = -Ldep/luajit/src -lluajit -Wl,-rpath,'$$ORIGIN/dep/luajit/src' \
              $(shell pkg-config --libs glfw3 glesv2 egl zlib libwebp libcurl 2>/dev/null) \
              -lm -lpthread -ldl -luuid
LDFLAGS = -Ldep/luajit/src -lluajit -Wl,-rpath,'$$ORIGIN/dep/luajit/src' \
          $(shell pkg-config --libs glfw3 glesv2 egl zlib libwebp libcurl 2>/dev/null) \
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
       src/ui_subscript.c \
       src/lcurl.c

OBJS = $(SRCS:.c=.o)
TARGET = pob

all: luajit $(TARGET)

debug: luajit-debug $(TARGET)-debug

luajit:
	git submodule update --init dep/luajit
	$(MAKE) -C dep/luajit BUILDMODE=static -j$(shell nproc)

luajit-debug:
	git submodule update --init dep/luajit
	$(MAKE) -C dep/luajit BUILDMODE=static CFLAGS="-O0 -g" -j$(shell nproc)

$(TARGET): $(OBJS)
	$(CC) -flto -o $@ $^ $(LDFLAGS)

$(TARGET)-debug:
	$(CC) $(DBG_CFLAGS) -o $@ $(SRCS) $(DBG_LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)-debug

clean-all: clean
	-$(MAKE) -C dep/luajit clean

.PHONY: all clean clean-all luajit
