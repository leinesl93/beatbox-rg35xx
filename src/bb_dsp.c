/* Klang-Engine: Stimmen, Kits, Master-Effekte und Sequencer.
 * Laeuft im Audio-Thread; die Oberflaeche spricht nur ueber die globalen
 * Parameter und dsp_trigger()/dsp_request_play() mit ihr. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "bb_priv.h"

#define PI 3.14159265358979f

/* ------------------------------------------------------------------ */
/* Globaler Zustand                                                     */
/* ------------------------------------------------------------------ */
uint8_t pats[NP][NT][NS];
Track tp[NT];
volatile int bpm = 96, swing = 0, kit = 0, curPat = 0, nextPat = -1, playing = 0, play_step = -1;
int fx[NFX] = {5, 0, 0, 2, 4, 0, 1, 0, 0};
int fxOn = 1;
const int FXD[NFX] = {5, 0, 0, 2, 4, 0, 1, 0, 0};
const int FXMAX[NFX] = {10, 10, 10, 3, 10, 10, 3, 10, 10};

const char *const TRACK_NAMES[NT] = {"KICK", "SNARE", "CLAP", "HAT", "OPEN", "TOM", "COWBL", "BASS", "RIM", "SHAKE", "CRASH", "STAB"};
const uint32_t TRACK_COLORS[NT] = {0xff5a5f, 0xffb347, 0xffe066, 0x7bed9f, 0x4cd7d0, 0x5aa9ff, 0xb18cff, 0xff7ad9, 0xe5e9ff, 0xa3e635, 0xfdba74, 0x93c5fd};

static int SR = 44100;

/* ------------------------------------------------------------------ */
/* Kits                                                                 */
/* ------------------------------------------------------------------ */
Kit KITS[NKITS];

static const char *const E16 = "................";
static const char *const BEATS[NKITS][NT] = {
  /* HIPHOP  */ {"2......1..1.....", "....2.......2...", "............1...", "1.1.1.1.1.1.1.1.", "...............1", "................", "................", "1..1......1.....", ".......1........", "................", "................", "2.....1........."},
  /* TECHNO  */ {"2...2...2...2...", "................", "....1.......1...", ".1.1.1.1.1.1.1.1", "..1...1...1...1.", "..............1.", "...........1....", ".1.1..1..1.1..1.", "...1...1...1...1", ".1...1...1...1..", "................", "..2...1...2...1."},
  /* LOFI    */ {"1......1...1....", "....1.......1...", "................", "1.1.1.1.1.1.1.1.", "..............1.", "................", "................", "1.....1.....1...", "...........1....", ".1...1...1...1..", "................", "2.......1......."},
  /* HOUSE   */ {"2...2...2...2...", "................", "....1.......1...", "1...1...1...1...", "..1...1...1...1.", "................", "...1.......1....", "1..1..1...1.1...", ".1...1...1...1..", ".1.1.1.1.1.1.1.1", "................", "..1..1..1..1..1."},
  /* TRAP    */ {"2.....1...1.....", "........2.......", "........1.......", "1.1.1.1.1.1.1111", "......1.........", "................", "................", "2.....1...1.....", "....1.......1...", "................", "................", "2.......1.....1."},
  /* DNB     */ {"2.........1.....", "....2.......2...", "................", "1.1.1.1.1.1.1.1.", "..............1.", "................", "................", "2.........1.....", "................", ".1.1.1.1.1.1.1.1", "................", "..1.......1....."},
  /* ELECTRO */ {"2..1..1...1..1..", "....1.......1...", "....1.......1...", ".1.1.1.1.1.1.1.1", "..1.......1.....", ".............1..", "..1..1..1..1..1.", "2..1..1...1..1..", "................", "................", "................", "...1.......1...."},
  /* DUB     */ {"........2.......", "........1.......", "................", "1.1.1.1.1.1.1.1.", "................", "................", "................", "2......1..1.....", "....1.......1...", "................", "................", "..1...1...1...1."},
  /* SYNTHW  */ {"2...2...2...2...", "....2.......2...", "....1.......1...", "1.1.1.1.1.1.1.1.", "................", "..............1.", "................", "1.1.1.1.1.1.1.1.", "................", "................", "................", "2.......1......."},
  /* DISCO   */ {"2...2...2...2...", "....1.......1...", "....1.......1...", ".1.1.1.1.1.1.1.1", "..1...1...1...1.", "................", "................", "1..1.1..1..1.1..", "................", ".1.1.1.1.1.1.1.1", "................", "1.......1.1....."},
};

static void kit_defaults(Kit *k) {
  memset(k, 0, sizeof *k);
  k->kick  = (KKick){160, 46, 0.13f, 0.5f, 0.35f, 0};
  k->snare = (KSnare){1200, 0, 0.2f, 0.75f, 230, 120, 0.7f};
  k->clap  = (KClap){1400, 1.1f, 0.22f};
  k->hat   = (KHat){7000, 0.05f, 0.55f};
  k->open  = (KHat){6500, 0.35f, 0.5f};
  k->tom   = (KTom){0, 210, 95, 0.35f};
  k->cow   = (KCow){{540, 800}, 0.32f, 0.45f};
  k->bass  = (KBass){3, 55, 0, 420, 1, 0.3f, 1, 0, 0};
  k->rim   = (KRim){450, 2500, 0.04f, 0.6f};
  k->shake = (KShake){6500, 0.8f, 0.1f, 0.45f, 0.012f};
  k->crash = (KCrash){3500, 1.2f, 0.5f};
  k->stab  = (KStab){1, 220, 4, {0, 3, 7, 10}, 2200, 900, 1, 0.45f, 0.3f};
}

static void kit_init_all(void) {
  Kit *k;
  for (int i = 0; i < NKITS; i++) {
    kit_defaults(&KITS[i]);
    for (int r = 0; r < NT; r++) KITS[i].beat[r] = BEATS[i][r];
  }
  /* 0 HIPHOP */
  k = &KITS[0]; k->name = "HIPHOP"; k->bpm = 90; k->sw = 12;
  /* 1 TECHNO */
  k = &KITS[1]; k->name = "TECHNO"; k->bpm = 128;
  k->kick = (KKick){230, 48, 0.08f, 0.3f, 0.5f, 5};
  k->snare.hp = 1800; k->snare.nd = 0.14f; k->snare.na = 0.7f; k->snare.tf = 250; k->snare.tf1 = 150; k->snare.ta = 0.4f;
  k->clap = (KClap){1200, 1.3f, 0.18f};
  k->hat = (KHat){9000, 0.04f, 0.5f}; k->open = (KHat){8000, 0.22f, 0.45f};
  k->tom.f0 = 330; k->tom.f1 = 200; k->tom.dec = 0.18f;
  k->cow.f[0] = 600; k->cow.f[1] = 845; k->cow.dec = 0.15f; k->cow.amp = 0.35f;
  k->bass.lp0 = 1800; k->bass.lp = 180; k->bass.q = 9; k->bass.dec = 0.22f; k->bass.sub = 0.4f;
  k->rim = (KRim){800, 3000, 0.03f, 0.5f};
  k->stab.w = 3; k->stab.nch = 3; k->stab.ch[0] = 0; k->stab.ch[1] = 7; k->stab.ch[2] = 12;
  k->stab.lp0 = 3500; k->stab.lp = 900; k->stab.q = 4; k->stab.dec = 0.18f; k->stab.amp = 0.3f;
  /* 2 LOFI */
  k = &KITS[2]; k->name = "LOFI"; k->bpm = 78; k->sw = 24; k->lp = 4200; k->crackle = 1; k->echo = 0.15f;
  k->kick.f0 = 120; k->kick.f1 = 50; k->kick.pd = 0.1f; k->kick.ad = 0.42f; k->kick.click = 0.12f;
  k->snare = (KSnare){900, 5000, 0.22f, 0.7f, 200, 110, 0.8f};
  k->clap = (KClap){1100, 0.9f, 0.25f};
  k->hat = (KHat){5000, 0.06f, 0.4f}; k->open = (KHat){5000, 0.3f, 0.4f};
  k->tom.w = 1; k->tom.f0 = 190; k->tom.f1 = 90; k->tom.dec = 0.4f;
  k->cow.f[0] = 480; k->cow.f[1] = 720; k->cow.dec = 0.4f; k->cow.amp = 0.3f;
  k->bass.w = 1; k->bass.lp = 300; k->bass.q = 0.7f; k->bass.dec = 0.55f; k->bass.sub = 0.8f;
  k->shake.amp = 0.3f;
  k->stab.w = 0; k->stab.nch = 4; k->stab.lp0 = 1600; k->stab.lp = 900; k->stab.dec = 0.9f; k->stab.amp = 0.35f;
  /* 3 HOUSE */
  k = &KITS[3]; k->name = "HOUSE"; k->bpm = 124; k->sw = 6;
  k->kick = (KKick){170, 50, 0.1f, 0.35f, 0.3f, 1.5f};
  k->snare.hp = 1500; k->snare.nd = 0.17f; k->snare.na = 0.7f; k->snare.tf = 240; k->snare.tf1 = 130; k->snare.ta = 0.5f;
  k->clap = (KClap){1300, 1.2f, 0.22f};
  k->hat = (KHat){8000, 0.05f, 0.5f}; k->open = (KHat){7500, 0.35f, 0.5f};
  k->tom.f0 = 240; k->tom.f1 = 130; k->tom.dec = 0.25f;
  k->cow.dec = 0.2f; k->cow.amp = 0.4f;
  k->bass.lp0 = 700; k->bass.lp = 250; k->bass.q = 3; k->bass.dec = 0.2f; k->bass.sub = 0.6f;
  k->stab.w = 3; k->stab.nch = 4; k->stab.ch[0] = 0; k->stab.ch[1] = 4; k->stab.ch[2] = 7; k->stab.ch[3] = 11;
  k->stab.lp0 = 3200; k->stab.lp = 1200; k->stab.q = 2; k->stab.dec = 0.22f; k->stab.amp = 0.28f;
  /* 4 TRAP */
  k = &KITS[4]; k->name = "TRAP"; k->bpm = 140; k->echo = 0.12f;
  k->kick = (KKick){140, 50, 0.05f, 0.5f, 0.2f, 1.5f};
  k->snare.hp = 2500; k->snare.nd = 0.16f; k->snare.na = 0.8f; k->snare.tf = 300; k->snare.tf1 = 180; k->snare.ta = 0.5f;
  k->clap = (KClap){1800, 1.2f, 0.3f};
  k->hat = (KHat){10000, 0.03f, 0.5f}; k->open = (KHat){9500, 0.25f, 0.45f};
  k->tom.f0 = 150; k->tom.f1 = 70; k->tom.dec = 0.5f;
  k->cow.f[0] = 560; k->cow.f[1] = 830; k->cow.dec = 0.25f; k->cow.amp = 0.4f;
  k->bass.w = 0; k->bass.lp = 900; k->bass.q = 0.7f; k->bass.dec = 1.0f; k->bass.sub = 0; k->bass.gl = 1.8f; k->bass.drive = 2.5f;
  k->rim = (KRim){900, 3500, 0.03f, 0.5f};
  k->stab.w = 0; k->stab.f = 440; k->stab.nch = 3; k->stab.lp0 = 3500; k->stab.lp = 3000; k->stab.dec = 0.7f; k->stab.amp = 0.28f;
  /* 5 DNB */
  k = &KITS[5]; k->name = "DNB"; k->bpm = 172;
  k->kick = (KKick){200, 52, 0.07f, 0.26f, 0.45f, 2};
  k->snare.hp = 1800; k->snare.nd = 0.18f; k->snare.na = 0.85f; k->snare.tf = 260; k->snare.tf1 = 170; k->snare.ta = 0.6f;
  k->clap = (KClap){1500, 1.2f, 0.2f};
  k->hat = (KHat){9000, 0.035f, 0.5f}; k->open = (KHat){8500, 0.2f, 0.45f};
  k->tom.f0 = 260; k->tom.f1 = 120; k->tom.dec = 0.22f;
  k->bass.lp0 = 900; k->bass.lp = 140; k->bass.q = 5; k->bass.dec = 0.35f; k->bass.sub = 0.8f; k->bass.drive = 1.5f;
  k->stab.w = 3; k->stab.nch = 3; k->stab.lp0 = 2500; k->stab.lp = 700; k->stab.q = 3; k->stab.dec = 0.3f; k->stab.amp = 0.28f;
  /* 6 ELECTRO */
  k = &KITS[6]; k->name = "ELECTRO"; k->bpm = 126;
  k->kick = (KKick){150, 44, 0.12f, 0.6f, 0.2f, 2};
  k->snare.hp = 1500; k->snare.nd = 0.22f; k->snare.na = 0.8f; k->snare.tf = 240; k->snare.tf1 = 140; k->snare.ta = 0.5f;
  k->clap = (KClap){1500, 1.4f, 0.3f};
  k->hat = (KHat){9000, 0.03f, 0.5f}; k->open = (KHat){8500, 0.28f, 0.45f};
  k->tom.f0 = 200; k->tom.f1 = 80; k->tom.dec = 0.45f;
  k->cow.f[0] = 560; k->cow.f[1] = 845; k->cow.dec = 0.3f; k->cow.amp = 0.45f;
  k->bass.w = 2; k->bass.lp = 600; k->bass.q = 2; k->bass.dec = 0.25f; k->bass.sub = 0.6f;
  k->stab.w = 3; k->stab.nch = 3; k->stab.ch[0] = 0; k->stab.ch[1] = 7; k->stab.ch[2] = 12;
  k->stab.lp0 = 4000; k->stab.lp = 1200; k->stab.q = 1; k->stab.dec = 0.2f; k->stab.amp = 0.28f;
  /* 7 DUB */
  k = &KITS[7]; k->name = "DUB"; k->bpm = 74; k->lp = 6000; k->echo = 0.45f;
  k->kick.f0 = 120; k->kick.f1 = 45; k->kick.pd = 0.1f; k->kick.ad = 0.55f; k->kick.click = 0.1f;
  k->snare.hp = 1500; k->snare.nd = 0.14f; k->snare.na = 0.7f; k->snare.tf = 300; k->snare.tf1 = 200; k->snare.ta = 0.6f;
  k->hat = (KHat){7500, 0.04f, 0.4f}; k->open = (KHat){6500, 0.3f, 0.4f};
  k->bass.w = 0; k->bass.lp = 500; k->bass.q = 1; k->bass.dec = 0.55f; k->bass.sub = 0.6f; k->bass.drive = 1.2f;
  k->rim = (KRim){520, 2500, 0.06f, 0.8f};
  k->stab.w = 2; k->stab.nch = 3; k->stab.lp0 = 1800; k->stab.lp = 900; k->stab.q = 2; k->stab.dec = 0.12f; k->stab.amp = 0.22f;
  /* 8 SYNTHW */
  k = &KITS[8]; k->name = "SYNTHW"; k->bpm = 100; k->echo = 0.3f;
  k->kick = (KKick){170, 50, 0.1f, 0.4f, 0.3f, 1.2f};
  k->snare.hp = 1100; k->snare.nd = 0.35f; k->snare.na = 0.9f; k->snare.tf = 220; k->snare.tf1 = 130; k->snare.ta = 0.6f;
  k->clap = (KClap){1300, 1.0f, 0.35f};
  k->hat = (KHat){8000, 0.05f, 0.45f}; k->open = (KHat){7000, 0.3f, 0.5f};
  k->tom.f0 = 220; k->tom.f1 = 100; k->tom.dec = 0.5f;
  k->bass.lp0 = 900; k->bass.lp = 300; k->bass.q = 2; k->bass.dec = 0.2f; k->bass.sub = 0.5f;
  k->stab.w = 3; k->stab.nch = 4; k->stab.lp0 = 3000; k->stab.lp = 1500; k->stab.q = 1; k->stab.dec = 0.7f; k->stab.amp = 0.3f;
  /* 9 DISCO */
  k = &KITS[9]; k->name = "DISCO"; k->bpm = 118; k->sw = 8;
  k->kick = (KKick){150, 48, 0.1f, 0.35f, 0.3f, 0.8f};
  k->snare.hp = 1400; k->snare.nd = 0.2f; k->snare.na = 0.7f; k->snare.tf = 230; k->snare.tf1 = 130; k->snare.ta = 0.6f;
  k->hat = (KHat){8000, 0.045f, 0.5f}; k->open = (KHat){7500, 0.3f, 0.5f};
  k->bass.lp = 650; k->bass.q = 1.5f; k->bass.dec = 0.16f; k->bass.sub = 0.7f;
  k->stab.w = 3; k->stab.nch = 4; k->stab.ch[0] = 0; k->stab.ch[1] = 4; k->stab.ch[2] = 7; k->stab.ch[3] = 10;
  k->stab.lp0 = 3500; k->stab.lp = 1500; k->stab.q = 1; k->stab.dec = 0.15f; k->stab.amp = 0.25f;
  (void)E16;
}

/* ------------------------------------------------------------------ */
/* Filter                                                               */
/* ------------------------------------------------------------------ */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Bq;

static inline float flush(float x) { return (x > -1e-15f && x < 1e-15f) ? 0.0f : x; }

static inline float bq_run(Bq *q, float x) {
  float y = q->b0 * x + q->z1;
  q->z1 = flush(q->b1 * x - q->a1 * y + q->z2);
  q->z2 = flush(q->b2 * x - q->a2 * y);
  return y;
}

/* type: 0 Tiefpass, 1 Hochpass, 2 Bandpass; Q linear */
static void bq_set(Bq *q, int type, float f, float Q) {
  float fmax = 0.45f * SR;
  if (f < 10.0f) f = 10.0f;
  if (f > fmax) f = fmax;
  if (Q < 0.0001f) Q = 0.0001f;
  float w = 2.0f * PI * f / SR, c = cosf(w), s = sinf(w), al = s / (2.0f * Q), a0 = 1.0f + al;
  float b0, b1, b2;
  if (type == 0) { b0 = (1.0f - c) * 0.5f; b1 = 1.0f - c; b2 = b0; }
  else if (type == 1) { b0 = (1.0f + c) * 0.5f; b1 = -(1.0f + c); b2 = b0; }
  else { b0 = al; b1 = 0.0f; b2 = -al; }
  q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0;
  q->a1 = -2.0f * c / a0; q->a2 = (1.0f - al) / a0;
}

static float dbq(float qdb) { return powf(10.0f, qdb / 20.0f); }

/* ------------------------------------------------------------------ */
/* Schichten und Stimmen                                                */
/* ------------------------------------------------------------------ */
typedef struct {
  int src;                     /* 0 Sinus, 1 Dreieck, 2 Rechteck, 3 Saege, 4 Rauschen */
  float f, f1, fstep, phase;
  long sweepN, n;
  Bq bq[2]; int nbq;
  int sweepF; float cut, cstep16, fq; long fsn;
  int envType; float peak, amp, dmul; long atkN, decN;
  int indrive, active;
  unsigned rng;
} Layer;

typedef struct {
  int active, track, nl, choke, isopen;
  float drive, chokeG;
  long age;
  Layer L[8];
} Voice;

#define NV 40
static Voice VO[NV];
static long age_counter = 0;

static float polyblep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

static Layer *L_new(Voice *v) {
  Layer *l = &v->L[v->nl++];
  memset(l, 0, sizeof *l);
  l->active = 1;
  l->rng = 0x9E3779B9u ^ (unsigned)(v->age * 2654435761u) ^ ((unsigned)v->nl * 7919u);
  if (l->rng == 0) l->rng = 1;
  return l;
}
static void L_osc(Layer *l, int src, float f0, float f1, float sweepSec) {
  l->src = src; l->f = f0; l->f1 = f0; l->fstep = 1.0f; l->sweepN = 0;
  if (f1 > 0.0f && sweepSec > 0.0f) {
    l->f1 = f1; l->sweepN = (long)(sweepSec * SR); if (l->sweepN < 1) l->sweepN = 1;
    l->fstep = powf(f1 / f0, 1.0f / (float)l->sweepN);
  }
}
static void L_noise(Layer *l) { l->src = 4; }
static void L_filt(Layer *l, int type, float f, float q) { bq_set(&l->bq[l->nbq], type, f, q); l->nbq++; }
static void L_sweep(Layer *l, float c0, float c1, float sec, float qlin) {
  l->sweepF = 1; l->cut = c0; l->fq = qlin; l->fsn = (long)(sec * SR); if (l->fsn < 1) l->fsn = 1;
  l->cstep16 = powf(powf(c1 / c0, 1.0f / (float)l->fsn), 16.0f);
}
static void L_env(Layer *l, float peak, float dec, float atk) {
  if (peak < 0.0002f) peak = 0.0002f;
  l->peak = peak;
  l->atkN = (long)(atk * SR); if (l->atkN < 1) l->atkN = 1;
  l->decN = (long)(dec * SR); if (l->decN < 1) l->decN = 1;
  l->dmul = powf(0.0001f / peak, 1.0f / (float)l->decN);
  l->amp = 0.0001f;
}

static float clap_env(Layer *l) {
  float t = (float)l->n / SR, v = l->peak;
  const float lo = 0.12f;
  if (t < 0.010f) return v * powf(lo, t / 0.010f);
  if (t < 0.012f) return v * lo;
  if (t < 0.022f) return v * powf(lo, (t - 0.012f) / 0.010f);
  if (t < 0.024f) return v * lo;
  if (t < 0.034f) return v * powf(lo, (t - 0.024f) / 0.010f);
  if (t < 0.036f) return v * lo;
  float u = (t - 0.036f) / ((float)l->decN / SR);
  if (u >= 1.0f) { l->active = 0; return 0.0f; }
  return 0.9f * v * powf(0.0001f / (0.9f * v), u);
}

static float L_proc(Layer *l) {
  float x, dt = l->f / SR;
  switch (l->src) {
    case 0: x = sinf(2.0f * PI * l->phase); break;
    case 1: x = (l->phase < 0.25f) ? 4.0f * l->phase : (l->phase < 0.75f ? 2.0f - 4.0f * l->phase : 4.0f * l->phase - 4.0f); break;
    case 2: {
      float p2 = l->phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
      x = (l->phase < 0.5f ? 1.0f : -1.0f) + polyblep(l->phase, dt) - polyblep(p2, dt);
    } break;
    case 3: x = 2.0f * l->phase - 1.0f - polyblep(l->phase, dt); break;
    default:
      l->rng ^= l->rng << 13; l->rng ^= l->rng >> 17; l->rng ^= l->rng << 5;
      x = (float)(int)l->rng * (1.0f / 2147483648.0f);
  }
  l->phase += dt; if (l->phase >= 1.0f) l->phase -= 1.0f;
  if (l->sweepN) { if (l->n < l->sweepN) l->f *= l->fstep; else l->f = l->f1; }
  if (l->sweepF && l->n < l->fsn && (l->n & 15) == 0) {
    bq_set(&l->bq[0], 0, l->cut, l->fq);
    l->cut *= l->cstep16;
  }
  for (int i = 0; i < l->nbq; i++) x = bq_run(&l->bq[i], x);
  float a;
  if (l->envType == 0) {
    if (l->n < l->atkN) a = 0.0001f + (l->peak - 0.0001f) * (float)(l->n + 1) / (float)l->atkN;
    else { l->amp = (l->n == l->atkN) ? l->peak : l->amp * l->dmul; a = l->amp; }
    if (l->n >= l->atkN + l->decN) l->active = 0;
  } else {
    a = clap_env(l);
  }
  l->n++;
  return x * a;
}

static Voice *v_alloc(int track) {
  int pick = -1;
  for (int i = 0; i < NV; i++) if (!VO[i].active) { pick = i; break; }
  if (pick < 0) { long oldest = VO[0].age; pick = 0; for (int i = 1; i < NV; i++) if (VO[i].age < oldest) { oldest = VO[i].age; pick = i; } }
  Voice *v = &VO[pick];
  memset(v, 0, sizeof *v);
  v->active = 1; v->track = track; v->age = ++age_counter; v->chokeG = 1.0f;
  return v;
}

static void choke_open(void) {
  for (int i = 0; i < NV; i++) if (VO[i].active && VO[i].isopen) VO[i].choke = 1;
}

static void trig_voice(int tr, float vel) {
  Track *t = &tp[tr];
  if (t->vol <= 0) return;
  const Kit *P = &KITS[kit];
  float k = powf(2.0f, t->tune / 12.0f);
  float v = vel * powf(t->vol / 10.0f, 1.4f);
  if (tr == 3) choke_open();
  Voice *vc = v_alloc(tr);
  Layer *l;
  switch (tr) {
    case 0: { /* KICK */
      const KKick *p = &P->kick;
      l = L_new(vc); L_osc(l, 0, p->f0 * k, p->f1 * k, p->pd); L_env(l, v, p->ad, 0.001f); l->indrive = 1;
      vc->drive = p->drive;
      if (p->click > 0.0f) { l = L_new(vc); L_noise(l); L_filt(l, 0, 3000, dbq(1.0f)); L_env(l, v * p->click, 0.01f, 0.001f); }
    } break;
    case 1: { /* SNARE */
      const KSnare *p = &P->snare;
      l = L_new(vc); L_noise(l); L_filt(l, 1, p->hp * k, dbq(1.0f));
      if (p->lp > 0.0f) L_filt(l, 0, p->lp, dbq(1.0f));
      L_env(l, v * p->na, p->nd, 0.001f);
      l = L_new(vc); L_osc(l, 1, p->tf * k, p->tf1 * k, 0.1f); L_env(l, v * p->ta, 0.11f, 0.001f);
    } break;
    case 2: { /* CLAP */
      const KClap *p = &P->clap;
      l = L_new(vc); L_noise(l); L_filt(l, 2, p->bp * k, p->q);
      l->envType = 1; l->peak = v < 0.0002f ? 0.0002f : v; l->decN = (long)(p->dec * SR); if (l->decN < 1) l->decN = 1;
    } break;
    case 3: case 4: { /* HAT, OPEN */
      const KHat *p = (tr == 3) ? &P->hat : &P->open;
      l = L_new(vc); L_noise(l); L_filt(l, 1, p->hp * k, dbq(1.0f)); L_env(l, v * p->amp, p->dec, 0.001f);
      if (tr == 4) vc->isopen = 1;
    } break;
    case 5: { /* TOM */
      const KTom *p = &P->tom;
      l = L_new(vc); L_osc(l, p->w, p->f0 * k, p->f1 * k, 0.2f); L_env(l, v * 0.9f, p->dec, 0.001f);
    } break;
    case 6: { /* COWBELL */
      const KCow *p = &P->cow;
      for (int i = 0; i < 2; i++) {
        l = L_new(vc); L_osc(l, 2, p->f[i] * k, 0, 0); L_filt(l, 2, 760.0f * k, 1.2f); L_env(l, v * p->amp, p->dec, 0.001f);
      }
    } break;
    case 7: { /* BASS */
      const KBass *p = &P->bass;
      float q = dbq(p->q);
      l = L_new(vc);
      if (p->gl > 0.0f) L_osc(l, p->w, p->f * k * p->gl, p->f * k, 0.07f); else L_osc(l, p->w, p->f * k, 0, 0);
      if (p->lp0 > 0.0f) { L_filt(l, 0, p->lp0, q); L_sweep(l, p->lp0, p->lp, 0.15f, q); }
      else L_filt(l, 0, p->lp, q);
      L_env(l, v * 0.8f, p->dec, 0.005f); l->indrive = 1;
      if (p->sub > 0.0f) { l = L_new(vc); L_osc(l, 0, p->f * k, 0, 0); L_env(l, v * 0.8f * p->sub, p->dec, 0.005f); l->indrive = 1; }
      vc->drive = p->drive;
    } break;
    case 8: { /* RIM */
      const KRim *p = &P->rim;
      for (int i = 0; i < 2; i++) {
        l = L_new(vc); L_osc(l, 2, p->f * (i ? 1.9f : 1.0f) * k, 0, 0); L_filt(l, 2, p->f * 2.0f * k, 1.5f); L_env(l, v * p->amp, p->dec, 0.001f);
      }
      l = L_new(vc); L_noise(l); L_filt(l, 1, p->hp * k, dbq(1.0f)); L_env(l, v * p->amp * 0.5f, 0.02f, 0.001f);
    } break;
    case 9: { /* SHAKER */
      const KShake *p = &P->shake;
      l = L_new(vc); L_noise(l); L_filt(l, 2, p->bp * k, p->q); L_env(l, v * p->amp, p->dec, p->atk);
    } break;
    case 10: { /* CRASH */
      const KCrash *p = &P->crash;
      static const float cf[5] = {800, 1230, 1700, 2250, 3100};
      l = L_new(vc); L_noise(l); L_filt(l, 1, p->hp * k, dbq(1.0f)); L_env(l, v * p->amp, p->dec, 0.002f);
      for (int i = 0; i < 5; i++) {
        l = L_new(vc); L_osc(l, 2, cf[i] * k, 0, 0); L_filt(l, 2, 7000.0f * k, 0.7f); L_env(l, v * p->amp * 0.25f, p->dec * 0.6f, 0.002f);
      }
    } break;
    case 11: { /* STAB (Akkord) */
      const KStab *p = &P->stab;
      float dec = p->dec, sw = dec < 0.3f ? dec : 0.3f, q = dbq(p->q);
      for (int i = 0; i < p->nch && i < 4; i++) {
        float det = powf(2.0f, ((i & 1) ? 6.0f : -6.0f) / 1200.0f);
        l = L_new(vc);
        L_osc(l, p->w, p->f * k * powf(2.0f, p->ch[i] / 12.0f) * det, 0, 0);
        L_filt(l, 0, p->lp0, q); L_sweep(l, p->lp0, p->lp, sw, q);
        L_env(l, v * p->amp, dec, 0.004f);
      }
    } break;
    default: vc->active = 0; return;
  }
}

static float voice_proc(Voice *v) {
  float vs = 0.0f, vd = 0.0f;
  int any = 0;
  for (int i = 0; i < v->nl; i++) {
    Layer *l = &v->L[i];
    if (!l->active) continue;
    float y = L_proc(l);
    if (l->indrive) vd += y; else vs += y;
    any = 1;
  }
  if (v->drive > 0.0f) vd = tanhf(vd * v->drive) / tanhf(v->drive);
  float out = vs + vd;
  if (v->choke) { out *= v->chokeG; v->chokeG *= 0.9972f; if (v->chokeG < 0.001f) any = 0; }
  if (!any) v->active = 0;
  return out;
}

/* ------------------------------------------------------------------ */
/* Trigger-Warteschlange (UI -> Audio)                                  */
/* ------------------------------------------------------------------ */
typedef struct { int track; float vel; long delay; } QEnt;
#define QN 64
static QEnt ring[QN];
static volatile int rhead = 0, rtail = 0;
typedef struct { int active, track; float vel; long long due; } Pend;
static Pend pend[16];

void dsp_trigger(int track, float vel, float delay_sec) {
  int next = (rhead + 1) % QN;
  if (next == rtail) return;
  ring[rhead].track = track; ring[rhead].vel = vel; ring[rhead].delay = (long)(delay_sec * SR);
  rhead = next;
}

static volatile int req_play = -1;
void dsp_request_play(int on) { req_play = on; }

/* ------------------------------------------------------------------ */
/* Master-Effekte                                                       */
/* ------------------------------------------------------------------ */
static Bq fhp, flp, klp, elp;
static float *dlyBuf = NULL; static int dlyN = 0, dlyW = 0;
static float s_lp = 20000, s_hp = 10, s_qlp = 0.7f, s_qhp = 0.7f, s_klp = 20000;
static float s_echo = 0, s_fb = 0, s_rev = 0, s_dt = 0.3f;

#define NC 8
#define NAP 4
static const int combL44[NC] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int apL44[NAP] = {556, 441, 341, 225};
static float *combB[NC], *apB[NAP];
static int combL[NC], combP[NC], apL[NAP], apP[NAP];
static float combLPs[NC], combG[NC];
static const float ROOMS[4] = {0.6f, 1.4f, 2.6f, 4.0f};
static const float DLYM[4] = {0.25f, 0.5f, 0.75f, 1.0f};
static int lastRoom = -1;
#define REV_IN 0.05f
#define REV_DAMP 0.3f

#define OUT_GAIN 1.8f
/* Kompressor */
static float cenv = 0.0f, c_atk, c_rel;

/* Sequencer */
static long long sclock = 0;
static double nextT = 0.0, stepLen = 0.0;
static long long fireAt = 0;
static int step = 0;

/* Knistern */
static unsigned crng = 12345u;

static void set_room(int r) {
  lastRoom = r;
  for (int i = 0; i < NC; i++) combG[i] = powf(10.0f, -3.0f * (float)combL[i] / (ROOMS[r] * (float)SR));
}

void dsp_init(int sr) {
  SR = sr > 8000 ? sr : 44100;
  kit_init_all();
  memset(VO, 0, sizeof VO);
  memset(pend, 0, sizeof pend);
  dlyN = 2 * SR + 8; dlyBuf = (float *)calloc((size_t)dlyN, sizeof(float)); dlyW = 0;
  float sc = (float)SR / 44100.0f;
  for (int i = 0; i < NC; i++) { combL[i] = (int)(combL44[i] * sc); combB[i] = (float *)calloc((size_t)combL[i], sizeof(float)); combP[i] = 0; combLPs[i] = 0; }
  for (int i = 0; i < NAP; i++) { apL[i] = (int)(apL44[i] * sc); apB[i] = (float *)calloc((size_t)apL[i], sizeof(float)); apP[i] = 0; }
  set_room(1);
  memset(&fhp, 0, sizeof fhp); memset(&flp, 0, sizeof flp); memset(&klp, 0, sizeof klp); memset(&elp, 0, sizeof elp);
  bq_set(&fhp, 1, 10, dbq(0.7f)); bq_set(&flp, 0, 20000, dbq(0.7f)); bq_set(&klp, 0, 20000, dbq(0.7f));
  bq_set(&elp, 0, 2400, dbq(0.7f));
  c_atk = expf(-1.0f / (0.003f * SR)); c_rel = expf(-1.0f / (0.15f * SR));
  for (int t = 0; t < NT; t++) { tp[t].tune = 0; tp[t].vol = 8; tp[t].mute = 0; }
  tp[2].vol = 7; tp[3].vol = 6; tp[4].vol = 6; tp[6].vol = 5; tp[7].vol = 7; tp[8].vol = 6; tp[9].vol = 5; tp[10].vol = 5; tp[11].vol = 5;
}

static inline float smooth(float cur, float tgt, float c) { return cur + (tgt - cur) * c; }

static void update_params(void) {
  const int *f = fxOn ? fx : FXD;
  const Kit *P = &KITS[kit];
  float c32 = 1.0f - expf(-32.0f / (0.03f * SR));
  float c20 = 1.0f - expf(-32.0f / (0.02f * SR));
  float c50 = 1.0f - expf(-32.0f / (0.05f * SR));
  float v = (float)f[0], q = 0.7f + f[1] * 1.2f;
  float tlp = v < 5 ? 200.0f * powf(100.0f, v / 5.0f) : 20000.0f;
  float thp = v > 5 ? 20.0f * powf(200.0f, (v - 5.0f) / 5.0f) : 10.0f;
  float tql = v < 5 ? q : 0.7f, tqh = v > 5 ? q : 0.7f;
  s_lp = smooth(s_lp, tlp, c32); s_hp = smooth(s_hp, thp, c32);
  s_qlp = smooth(s_qlp, tql, c32); s_qhp = smooth(s_qhp, tqh, c32);
  bq_set(&flp, 0, s_lp, dbq(s_qlp)); bq_set(&fhp, 1, s_hp, dbq(s_qhp));
  s_klp = smooth(s_klp, P->lp > 0 ? P->lp : 20000.0f, c20);
  bq_set(&klp, 0, s_klp, dbq(0.7f));
  s_echo = smooth(s_echo, f[2] / 10.0f * 0.8f, c20);
  s_fb = smooth(s_fb, f[4] / 10.0f * 0.85f, c20);
  s_rev = smooth(s_rev, f[5] / 10.0f * 0.9f, c20);
  float dt = 60.0f / (float)bpm * DLYM[f[3]];
  if (dt < 0.03f) dt = 0.03f;
  if (dt > 1.9f) dt = 1.9f;
  s_dt = smooth(s_dt, dt, c50);
  if (f[6] != lastRoom) set_room(f[6]);
}

static void fire_step(void) {
  if (step == 0 && nextPat >= 0) { curPat = nextPat; nextPat = -1; }
  for (int r = 0; r < NT; r++) {
    int s = pats[curPat][r][step];
    if (s && !tp[r].mute) trig_voice(r, s == 2 ? 1.0f : 0.62f);
  }
  play_step = step;
  step = (step + 1) % NS;
  stepLen = 60.0 / (double)bpm / 4.0 * (double)SR;
  nextT += stepLen;
  fireAt = (long long)(nextT + ((step & 1) ? (double)swing / 100.0 * stepLen : 0.0));
}

void bb_audio_render(int16_t *out, int frames) {
  for (int i = 0; i < frames; i++) {
    if ((i & 31) == 0) {
      update_params();
      int rq = req_play;
      if (rq >= 0) {
        req_play = -1;
        if (rq == 1 && !playing) { playing = 1; step = 0; nextT = (double)sclock + 0.06 * SR; fireAt = (long long)nextT; }
        else if (rq == 0 && playing) {
          playing = 0; play_step = -1;
          if (nextPat >= 0) { curPat = nextPat; nextPat = -1; }
        }
      }
    }
    while (rtail != rhead) {
      QEnt *e = &ring[rtail];
      for (int p = 0; p < 16; p++) if (!pend[p].active) { pend[p].active = 1; pend[p].track = e->track; pend[p].vel = e->vel; pend[p].due = sclock + e->delay; break; }
      rtail = (rtail + 1) % QN;
    }
    for (int p = 0; p < 16; p++) if (pend[p].active && pend[p].due <= sclock) { pend[p].active = 0; trig_voice(pend[p].track, pend[p].vel); }
    if (playing && sclock >= fireAt) fire_step();

    float m = 0.0f;
    for (int vi = 0; vi < NV; vi++) if (VO[vi].active) m += voice_proc(&VO[vi]);
    m *= 0.85f;

    const int *f = fxOn ? fx : FXD;

    /* Delay */
    float rp = (float)dlyW - s_dt * SR;
    while (rp < 0) rp += (float)dlyN;
    int i0 = (int)rp; float fr = rp - (float)i0; int i1 = i0 + 1; if (i1 >= dlyN) i1 = 0; if (i0 >= dlyN) i0 = 0;
    float dy = dlyBuf[i0] * (1.0f - fr) + dlyBuf[i1] * fr;
    float eo = bq_run(&elp, dy);
    dlyBuf[dlyW] = flush(s_echo * m + eo * s_fb);
    if (++dlyW >= dlyN) dlyW = 0;

    /* Hall */
    float rin = s_rev * m * REV_IN, racc = 0.0f;
    if (s_rev > 0.0005f || 1) {
      for (int c = 0; c < NC; c++) {
        float y = combB[c][combP[c]];
        combLPs[c] = flush(y * (1.0f - REV_DAMP) + combLPs[c] * REV_DAMP);
        combB[c][combP[c]] = flush(rin + combLPs[c] * combG[c]);
        if (++combP[c] >= combL[c]) combP[c] = 0;
        racc += y;
      }
      for (int a = 0; a < NAP; a++) {
        float vv = apB[a][apP[a]];
        float o = -racc + vv;
        apB[a][apP[a]] = flush(racc + vv * 0.5f);
        if (++apP[a] >= apL[a]) apP[a] = 0;
        racc = o;
      }
    }

    float x = m + eo + racc;

    /* Drive, Crush */
    if (f[7] > 0 || f[8] > 0) { if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f; }
    if (f[7] > 0) {
      float a = 1.0f + f[7] * 0.8f, comp = 1.0f / (1.0f + (a - 1.0f) * 0.18f);
      x = tanhf(x * a) / tanhf(a) * comp;
    }
    if (f[8] > 0) { float L = powf(2.0f, 15.0f - f[8] * 1.2f); x = roundf(x * L) / L; }

    x = bq_run(&fhp, x);
    x = bq_run(&flp, x);
    x = bq_run(&klp, x);

    /* Vinyl-Knistern */
    if (playing && KITS[kit].crackle) {
      crng ^= crng << 13; crng ^= crng >> 17; crng ^= crng << 5;
      float n = (float)(int)crng * (1.0f / 2147483648.0f);
      x += n * 0.012f * 0.5f;
      crng ^= crng << 13; crng ^= crng >> 17; crng ^= crng << 5;
      if ((crng & 0xFFFF) < 39) { crng ^= crng << 13; crng ^= crng >> 17; crng ^= crng << 5; x += (float)(int)crng * (1.0f / 2147483648.0f) * 0.9f * 0.5f; }
    }

    /* Kompressor (Limiter-artig) und Ausgangspegel */
    float lv = fabsf(x);
    cenv = lv > cenv ? lv + (cenv - lv) * c_atk : lv + (cenv - lv) * c_rel;
    float db = 20.0f * log10f(cenv + 1e-9f), over = db + 12.0f, gr;
    const float knee = 6.0f, slope = 1.0f / 12.0f - 1.0f;
    if (2.0f * over < -knee) gr = 0.0f;
    else if (2.0f * fabsf(over) <= knee) gr = slope * (over + knee / 2.0f) * (over + knee / 2.0f) / (2.0f * knee);
    else gr = slope * over;
    x *= powf(10.0f, gr / 20.0f) * OUT_GAIN;
    x = tanhf(x);

    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    int16_t s16 = (int16_t)(x * 32000.0f);
    out[2 * i] = s16; out[2 * i + 1] = s16;
    sclock++;
  }
}
