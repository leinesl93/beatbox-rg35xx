# Beatbox fuer RG35XX (MinUI) -- Bauen, Testen, Pak erstellen
#
#   make pak     Programm fuer das Geraet bauen und dist/Beatbox.pak anlegen
#                (im MinUI-Toolchain-Container; siehe build_rg35xx.sh / GitHub-Workflow)
#   make test    Kern am Rechner testen (kein SDL noetig), schreibt WAV-Dateien nach build/test
#   make clean

# Standardwerte der MinUI-Toolchain (Container savant/minui-toolchain:rg35xx);
# werden von der Umgebung ueberschrieben, falls dort schon gesetzt.
TC      ?= /opt/rg35xx-toolchain
CROSS_COMPILE ?= $(if $(wildcard $(TC)/usr/bin/arm-buildroot-linux-gnueabihf-gcc),$(TC)/usr/bin/arm-buildroot-linux-gnueabihf-,)
PREFIX  ?= $(if $(wildcard $(TC)/usr/arm-buildroot-linux-gnueabihf/sysroot/usr),$(TC)/usr/arm-buildroot-linux-gnueabihf/sysroot/usr,)

CC      = $(CROSS_COMPILE)gcc
SDLCONF ?= $(firstword $(wildcard $(PREFIX)/bin/sdl-config) $(shell command -v sdl-config 2>/dev/null))

ifneq ($(SDLCONF),)
SDL_CFLAGS := $(shell $(SDLCONF) --cflags)
SDL_LIBS   := $(shell $(SDLCONF) --libs)
else
SDL_CFLAGS := -I$(PREFIX)/include/SDL -I$(PREFIX)/include
SDL_LIBS   := -L$(PREFIX)/lib -lSDL
endif

OPT     = -O2 -ffast-math -fno-math-errno -std=gnu99 -Wall
CORE    = src/bb_dsp.c src/bb_app.c

all: build/beatbox

build/beatbox: src/main_sdl.c $(CORE) src/*.h
	@mkdir -p build
	$(CC) $(OPT) $(CFLAGS) $(SDL_CFLAGS) -o $@ src/main_sdl.c $(CORE) $(SDL_LIBS) -lm -lpthread
	@echo "gebaut: $@"

pak: build/beatbox
	rm -rf dist/Beatbox.pak && mkdir -p dist/Beatbox.pak
	cp build/beatbox dist/Beatbox.pak/beatbox
	cp pak/launch.sh pak/README.txt dist/Beatbox.pak/
	chmod +x dist/Beatbox.pak/beatbox dist/Beatbox.pak/launch.sh
	@echo "fertig: dist/Beatbox.pak"

test: build/bb_test
	@mkdir -p build/test
	./build/bb_test build/test

build/bb_test: src/main_test.c $(CORE) src/*.h
	@mkdir -p build
	gcc -O2 -ffast-math -fno-math-errno -std=gnu99 -Wall -Wextra -o $@ src/main_test.c $(CORE) -lm

clean:
	rm -rf build dist

.PHONY: all pak test clean
