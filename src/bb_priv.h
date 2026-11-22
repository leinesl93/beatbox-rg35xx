#ifndef BB_PRIV_H
#define BB_PRIV_H
#include "bb.h"

#define NT 12      /* Spuren */
#define NS 16      /* Steps */
#define NP 4       /* Patterns */
#define NFX 9      /* Effekt-Parameter */
#define NKITS 10

/* Kit-Parameter (entsprechen der Web-Version) */
typedef struct { float f0, f1, pd, ad, click, drive; } KKick;
typedef struct { float hp, lp, nd, na, tf, tf1, ta; } KSnare;
typedef struct { float bp, q, dec; } KClap;
typedef struct { float hp, dec, amp; } KHat;
typedef struct { int w; float f0, f1, dec; } KTom;
typedef struct { float f[2], dec, amp; } KCow;
typedef struct { int w; float f, lp0, lp, q, dec, sub, gl, drive; } KBass;
typedef struct { float f, hp, dec, amp; } KRim;
typedef struct { float bp, q, dec, amp, atk; } KShake;
typedef struct { float hp, dec, amp; } KCrash;
typedef struct { int w; float f; int nch; int ch[4]; float lp0, lp, q, dec, amp; } KStab;

typedef struct {
  const char *name;
  int bpm, sw;
  float lp;        /* Kit-Tiefpass in Hz, 0 = aus */
  int crackle;     /* Vinyl-Knistern */
  float echo;      /* Delay-Preset (0..1) */
  KKick kick; KSnare snare; KClap clap; KHat hat, open; KTom tom; KCow cow;
  KBass bass; KRim rim; KShake shake; KCrash crash; KStab stab;
  const char *beat[NT];
} Kit;

typedef struct { int tune, vol, mute; } Track;

extern Kit KITS[NKITS];
extern uint8_t pats[NP][NT][NS];
extern Track tp[NT];
extern volatile int bpm, swing, kit, curPat, nextPat, playing, play_step;
extern int fx[NFX], fxOn;
extern const int FXD[NFX], FXMAX[NFX];
extern const char *const TRACK_NAMES[NT];
extern const uint32_t TRACK_COLORS[NT];

void dsp_init(int sr);
void dsp_trigger(int track, float vel, float delay_sec);  /* aus dem UI-Thread */
void dsp_request_play(int on);                            /* 1 = Start, 0 = Stop */

#endif
