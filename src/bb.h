/* Beatbox fuer das Anbernic RG35XX - oeffentliche Schnittstelle des Kerns.
 * Der Kern ist plattformunabhaengig (kein SDL). Die Plattform liefert Audio,
 * Bild und Tasten (siehe main_sdl.c). */
#ifndef BB_CORE_H
#define BB_CORE_H
#include <stdint.h>

#define BB_W 640
#define BB_HGT 480

enum {
  PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
  PAD_A, PAD_B, PAD_X, PAD_Y,
  PAD_L, PAD_R, PAD_SELECT, PAD_START, PAD_MENU,
  PAD_COUNT
};

void bb_init(int sample_rate, const char *datadir);
void bb_audio_render(int16_t *out, int frames);      /* stereo, interleaved */
void bb_draw(uint16_t *fb, uint32_t now_ms);          /* fb: 640*480 RGB565 */
void bb_tick(int dt_ms);                              /* ca. alle 16 ms */
int  bb_raw_key(int code, int down);                  /* Roh-Taste; 1 = verbraucht */
void bb_button(int btn, int down);                    /* logische Taste */
int  bb_wants_quit(void);
void bb_save(void);

/* nur fuer Tests */
void bb_test_identity_map(void);

#endif
