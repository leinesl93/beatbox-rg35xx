/* Plattform-Schicht fuer das RG35XX unter MinUI: SDL 1.2 (Bild, Ton, Tasten)
 * plus optional direktes Lesen der Eingabegeraete (evdev) als Reserve.
 * Aufruf: beatbox [datenverzeichnis]   (Standard: Ordner der Programmdatei) */
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL/SDL.h>
#ifdef __linux__
#include <linux/input.h>
#endif
#include "bb.h"

#define MAX_EV 8
static int evfd[MAX_EV], nev = 0;

static void ev_open_all(void) {
#ifdef __linux__
  char path[64];
  for (int i = 0; i < 16 && nev < MAX_EV; i++) {
    snprintf(path, sizeof path, "/dev/input/event%d", i);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) evfd[nev++] = fd;
  }
#endif
}

static void ev_poll(void) {
#ifdef __linux__
  struct input_event e;
  for (int i = 0; i < nev; i++) {
    for (;;) {
      ssize_t n = read(evfd[i], &e, sizeof e);
      if (n != (ssize_t)sizeof e) break;
      if (e.type == EV_KEY && e.value != 2) bb_raw_key(0x40000 + e.code, e.value ? 1 : 0);
    }
  }
#endif
}

static void audio_cb(void *ud, Uint8 *stream, int len) {
  (void)ud;
  bb_audio_render((int16_t *)stream, len / 4);
}

static void log_msg(const char *m) { fprintf(stderr, "[beatbox] %s\n", m); fflush(stderr); }

int main(int argc, char **argv) {
  char dir[PATH_MAX] = ".";
  if (argc > 1) snprintf(dir, sizeof dir, "%s", argv[1]);
  else if (argv[0] && strrchr(argv[0], '/')) {
    snprintf(dir, sizeof dir, "%s", argv[0]);
    *strrchr(dir, '/') = 0;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) { log_msg(SDL_GetError()); return 1; }
  SDL_ShowCursor(0);
  SDL_Surface *screen = SDL_SetVideoMode(BB_W, BB_HGT, 16, SDL_SWSURFACE);
  if (!screen) { log_msg(SDL_GetError()); SDL_Quit(); return 1; }

  SDL_AudioSpec want, got;
  memset(&want, 0, sizeof want);
  want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
  int have_audio = SDL_OpenAudio(&want, &got) == 0;
  if (!have_audio) { log_msg("Audio konnte nicht geoeffnet werden"); got.freq = 44100; }
  else if (got.channels != 2 || got.format != AUDIO_S16SYS) log_msg("Audioformat weicht ab (erwartet 16 Bit Stereo)");

  bb_init(got.freq, dir);
  ev_open_all();
  if (have_audio) SDL_PauseAudio(0);

  static uint16_t fb[BB_W * BB_HGT];
  Uint32 last = SDL_GetTicks(), start = last;
  int run = 1;
  while (run && !bb_wants_quit()) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) run = 0;
      else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        int down = e.type == SDL_KEYDOWN;
        int code = e.key.keysym.sym ? (int)e.key.keysym.sym : 0x8000 + e.key.keysym.scancode;
        if (down && e.key.keysym.sym == SDLK_ESCAPE) run = 0;
        else bb_raw_key(0x10000 + code, down);
      }
    }
    ev_poll();
    Uint32 now = SDL_GetTicks();
    int dt = (int)(now - last); last = now;
    if (dt > 100) dt = 100;
    bb_tick(dt);
    bb_draw(fb, now - start);

    if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
    for (int y = 0; y < BB_HGT; y++) memcpy((Uint8 *)screen->pixels + y * screen->pitch, fb + y * BB_W, BB_W * 2);
    if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
    SDL_Flip(screen);

    Uint32 used = SDL_GetTicks() - now;
    if (used < 33) SDL_Delay(33 - used);   /* ca. 30 Bilder/s */
  }
  if (have_audio) { SDL_PauseAudio(1); SDL_CloseAudio(); }
  bb_save();
  SDL_Quit();
  return 0;
}
