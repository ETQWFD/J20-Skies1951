# 《长空·1951》 Makefile - Linux native + Windows cross (mingw-w64)
NAME    := J20_Skies1951
CC      := gcc
WCC     := x86_64-w64-mingw32-gcc

CFLAGS  := -O2 -std=c11 -D_DEFAULT_SOURCE \
           -Wall -Wno-unused-variable -Wno-unused-function \
           -Wno-unused-but-set-variable -Wno-unused-result \
           -Wno-misleading-indentation -DFONT_EMBEDDED

SRC     := $(wildcard src/*.c)
OBJ_L   := $(patsubst src/%.c,build/obj_linux/%.o,$(SRC))
OBJ_W   := $(patsubst src/%.c,build/obj_win/%.o,$(SRC))

RL_L    := third_party/linux/lib/libraylib.a
RL_W    := third_party/windows/lib/libraylib.a
FONT_L  := third_party/font/fontdata_linux.o
FONT_W  := third_party/font/fontdata_win.o

LINUX_LIBS := third_party/linux/lib/libraylib.a -lGL -lm -lpthread -ldl -lrt \
              -lX11 -lXrandr -lXi -lXcursor -lXinerama -lasound
WIN_LIBS   := -lopengl32 -lgdi32 -lwinmm -lole32 -luuid -lcomdlg32 \
              -lcomctl32 -lshell32 -loleaut32 -static -lm

.PHONY: all linux windows clean
all: linux windows

linux: build/$(NAME)

build/$(NAME): $(OBJ_L) $(RL_L) $(FONT_L)
	@mkdir -p build
	$(CC) -o $@ $(OBJ_L) $(FONT_L) $(LINUX_LIBS)

build/obj_linux/%.o: src/%.c
	@mkdir -p build/obj_linux
	$(CC) $(CFLAGS) -Isrc -Ithird_party/linux/include -c $< -o $@

windows: build/$(NAME).exe

build/$(NAME).exe: $(OBJ_W) $(RL_W) $(FONT_W)
	@mkdir -p build
	$(WCC) -o $@ $(OBJ_W) $(FONT_W) third_party/windows/lib/libraylib.a $(WIN_LIBS)

build/obj_win/%.o: src/%.c
	@mkdir -p build/obj_win
	$(WCC) $(CFLAGS) -Isrc -Ithird_party/windows/include -c $< -o $@

clean:
	rm -rf build/obj_linux build/obj_win build/$(NAME) build/$(NAME).exe
