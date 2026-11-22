/* Oberflaeche, Bedienlogik, Tasten-Zuordnung und Speichern. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bb_priv.h"
#include "bb_font.h"

void dsp_init(int sr);

enum { M_GRID, M_KIT, M_FX, M_LEARN, M_TEST, M_CHORD };

static int mode = M_GRID;
static int cur_r = 0, cur_c = 0, fx_sel = 0;
int ch_row = 0, ch_cur = 0;       /* Akkord-Seite: Zeile (0 = Step-Reihe) und Step-Cursor */
#define CH_ROWS 10
static int held[PAD_COUNT];
static int hold_ms[PAD_COUNT];
static uint32_t tms = 0;
static int quit_req = 0, dirty = 0, autosave_ms = 0;
static char datadir[512] = ".";

/* Tasten-Zuordnung (Rohcode -> logische Taste) */
static int kmap[PAD_COUNT];
static int learn_idx = 0;
static uint32_t learn_ignore_until = 0;
static int sel_used = 0;                 /* SELECT wurde als Kombination benutzt */
static char lastraw[48] = "-";            /* letzte Roh-Taste (Diagnose) */
static char learn_msg[48] = "";
static char toast[40] = ""; static int toast_ms = 0;
static int rawdown[16], nraw = 0; static uint32_t last_raw_ms = 0;
static const char *const PAD_LABEL[PAD_COUNT] = {"HOCH", "RUNTER", "LINKS", "RECHTS", "A", "B", "X", "Y", "L", "R", "SELECT", "START", "MENU"};

/* ------------------------------------------------------------------ */
/* Speichern / Laden                                                    */
/* ------------------------------------------------------------------ */
#define MAGIC 0xBEA7B0C3u
#define MAGIC_MAP 0xBEA7B0C4u   /* neue Kennung: alte bbmap.cfg wird ignoriert */

#define HDRN (5 + NFX + 9)

static void path_of(char *dst, size_t n, const char *name) {
  snprintf(dst, n, "%s/%s", datadir, name);
}

static int load_map(void) {
  char p[640]; path_of(p, sizeof p, "bbmap.cfg");
  FILE *f = fopen(p, "rb"); if (!f) return 0;
  unsigned magic = 0; int tmp[PAD_COUNT];
  int ok = fread(&magic, sizeof magic, 1, f) == 1 && magic == MAGIC_MAP && fread(tmp, sizeof tmp, 1, f) == 1;
  fclose(f);
  if (ok) memcpy(kmap, tmp, sizeof kmap);
  return ok;
}
static void save_map(void) {
  char p[640]; path_of(p, sizeof p, "bbmap.cfg");
  FILE *f = fopen(p, "wb"); if (!f) return;
  unsigned magic = MAGIC_MAP;
  fwrite(&magic, sizeof magic, 1, f); fwrite(kmap, sizeof kmap, 1, f);
  fclose(f);
}

void bb_save(void) {
  char p[640]; path_of(p, sizeof p, "bbsave.dat");
  FILE *f = fopen(p, "wb"); if (!f) return;
  unsigned magic = MAGIC; int hdr[HDRN];
  hdr[0] = bpm; hdr[1] = swing; hdr[2] = kit; hdr[3] = curPat; hdr[4] = fxOn;
  for (int i = 0; i < NFX; i++) hdr[5 + i] = fx[i];
  { int *c = hdr + 5 + NFX; c[0] = ch_on; c[1] = ch_inst; c[2] = ch_key; c[3] = ch_minor; c[4] = ch_sev; c[5] = ch_len; c[6] = ch_oct; c[7] = ch_vol; c[8] = ch_preset; }
  fwrite(&magic, sizeof magic, 1, f); fwrite(hdr, sizeof hdr, 1, f);
  fwrite(pats, sizeof pats, 1, f); fwrite(tp, sizeof tp, 1, f); fwrite(cps, sizeof cps, 1, f);
  fclose(f);
  dirty = 0;
}

static int load_save(void) {
  char p[640]; path_of(p, sizeof p, "bbsave.dat");
  FILE *f = fopen(p, "rb"); if (!f) return 0;
  unsigned magic = 0; int hdr[HDRN];
  static uint8_t tp_pats[NP][NT][NS], tcps[NP][NS]; static Track ttp[NT];
  int ok = fread(&magic, sizeof magic, 1, f) == 1 && magic == MAGIC && fread(hdr, sizeof hdr, 1, f) == 1 &&
           fread(tp_pats, sizeof tp_pats, 1, f) == 1 && fread(ttp, sizeof ttp, 1, f) == 1 && fread(tcps, sizeof tcps, 1, f) == 1;
  fclose(f);
  if (!ok) return 0;
  if (hdr[0] < 40 || hdr[0] > 240 || hdr[1] < 0 || hdr[1] > 60 || hdr[2] < 0 || hdr[2] >= NKITS || hdr[3] < 0 || hdr[3] >= NP) return 0;
  for (int i = 0; i < NFX; i++) if (hdr[5 + i] < 0 || hdr[5 + i] > FXMAX[i]) return 0;
  { const int *c = hdr + 5 + NFX;
    if (c[1] < 0 || c[1] >= NCI || c[2] < 0 || c[2] > 11 || c[5] < 0 || c[5] > 4 || c[6] < -1 || c[6] > 1 || c[7] < 0 || c[7] > 10 || c[8] < 0 || c[8] >= NCP) return 0;
    for (int a = 0; a < NP; a++) for (int s = 0; s < NS; s++) if (tcps[a][s] > 7) return 0; }
  for (int t = 0; t < NT; t++) if (ttp[t].vol < 0 || ttp[t].vol > 10 || ttp[t].tune < -12 || ttp[t].tune > 12) return 0;
  bpm = hdr[0]; swing = hdr[1]; kit = hdr[2]; curPat = hdr[3]; fxOn = hdr[4] ? 1 : 0;
  for (int i = 0; i < NFX; i++) fx[i] = hdr[5 + i];
  memcpy(pats, tp_pats, sizeof pats); memcpy(tp, ttp, sizeof tp); memcpy(cps, tcps, sizeof cps);
  { const int *c = hdr + 5 + NFX; ch_on = c[0] ? 1 : 0; ch_inst = c[1]; ch_key = c[2]; ch_minor = c[3] ? 1 : 0; ch_sev = c[4] ? 1 : 0; ch_len = c[5]; ch_oct = c[6]; ch_vol = c[7]; ch_preset = c[8]; }
  return 1;
}

/* ------------------------------------------------------------------ */
/* Pattern-Helfer                                                       */
/* ------------------------------------------------------------------ */
static void load_beat(int k, int pat) {
  for (int r = 0; r < NT; r++)
    for (int s = 0; s < NS; s++) {
      char c = KITS[k].beat[r][s];
      pats[pat][r][s] = c == '2' ? 2 : (c == '1' ? 1 : 0);
    }
}

static void clear_pattern(int pat) { memset(pats[pat], 0, sizeof pats[pat]); }

static void set_kit(int k, int audition) {
  kit = (k % NKITS + NKITS) % NKITS;
  bpm = KITS[kit].bpm; swing = KITS[kit].sw;
  load_beat(kit, curPat);
  dsp_chord_load(2 * kit, curPat);
  dirty = 1;
  if (audition && !playing) dsp_trigger(0, 1.0f, 0);
}

void bb_init(int sample_rate, const char *dir) {
  if (dir && *dir) snprintf(datadir, sizeof datadir, "%s", dir);
  dsp_init(sample_rate);
  cur_r = cur_c = fx_sel = 0; learn_msg[0] = 0; quit_req = dirty = autosave_ms = 0;
  memset(held, 0, sizeof held); memset(hold_ms, 0, sizeof hold_ms);
  playing = 0; play_step = -1; fxOn = 1; memcpy(fx, FXD, sizeof fx);
  memset(pats, 0, sizeof pats);
  load_beat(0, 0);
  memset(cps, 0, sizeof cps); ch_on = 1; ch_vol = 6; ch_row = 0; ch_cur = 0;
  dsp_chord_load(0, 0);
  bpm = KITS[0].bpm; swing = KITS[0].sw; kit = 0; curPat = 0; nextPat = -1;
  load_save();
  for (int i = 0; i < PAD_COUNT; i++) kmap[i] = -1;
  if (load_map()) mode = M_GRID; else { mode = M_LEARN; learn_idx = 0; }
  sel_used = 0; toast_ms = 0; nraw = 0;
}

void bb_test_identity_map(void) {
  for (int i = 0; i < PAD_COUNT; i++) kmap[i] = i;
  mode = M_GRID;
}

int bb_wants_quit(void) { return quit_req; }
int bb_test_mode(void) { return mode; }

/* ------------------------------------------------------------------ */
/* Eingabe                                                              */
/* ------------------------------------------------------------------ */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void say(const char *m) { snprintf(toast, sizeof toast, "%s", m); toast_ms = 1800; }

static void audition(void) {
  if (playing) return;
  dsp_trigger(0, 1.0f, 0.0f); dsp_trigger(3, 0.8f, 0.18f); dsp_trigger(1, 0.9f, 0.36f); dsp_trigger(3, 0.8f, 0.54f);
}

static void start_learn(void) {
  mode = M_LEARN; learn_idx = 0; learn_msg[0] = 0;
  for (int i = 0; i < PAD_COUNT; i++) kmap[i] = -1;
  memset(held, 0, sizeof held); nraw = 0;
}

/* Akkord zur Probe: Stufe am Cursor, sonst erste benutzte, sonst I */
static void chord_audition(int deg) {
  if (playing) return;
  if (deg < 1) { deg = cps[curPat][ch_cur]; }
  for (int s = 0; s < NS && deg < 1; s++) deg = cps[curPat][s];
  dsp_trigger(CH_AUDITION + (deg < 1 ? 1 : deg), 1.0f, 0);
}

static void chord_dir(int dx, int dy) {
  if (dy) { ch_row = (ch_row + dy + CH_ROWS) % CH_ROWS; return; }
  if (!dx) return;
  switch (ch_row) {
    case 0: ch_cur = (ch_cur + dx + NS) % NS; return;
    case 1: ch_inst = (ch_inst + dx + NCI) % NCI; break;
    case 2: ch_key = (ch_key + dx + 12) % 12; break;
    case 3: ch_minor = !ch_minor; break;
    case 4: ch_sev = !ch_sev; break;
    case 5: ch_len = clampi(ch_len + dx, 0, 4); break;
    case 6: ch_oct = clampi(ch_oct + dx, -1, 1); break;
    case 7: ch_vol = clampi(ch_vol + dx, 0, 10); break;
    case 8: ch_on = !ch_on; break;
    case 9: ch_preset = (ch_preset + dx + NCP) % NCP; dirty = 1; return;
  }
  dirty = 1;
  chord_audition(0);
}

static void do_dir(int b) {
  int dx = (b == PAD_RIGHT) - (b == PAD_LEFT), dy = (b == PAD_DOWN) - (b == PAD_UP);
  if (mode == M_LEARN || mode == M_TEST) return;
  if (mode == M_CHORD) { chord_dir(dx, dy); return; }
  if (mode == M_KIT) {
    int d = dy ? dy : dx;
    if (d) { kit = (kit + d + NKITS) % NKITS; dirty = 1; audition(); }
    return;
  }
  if (mode == M_FX) {
    if (dy) fx_sel = (fx_sel + dy + NFX) % NFX;
    if (dx) { fx[fx_sel] = clampi(fx[fx_sel] + dx, 0, FXMAX[fx_sel]); dirty = 1; }
    return;
  }
  if (held[PAD_SELECT]) {
    if (dy) bpm = clampi(bpm - dy, 40, 240);
    if (dx) swing = clampi(swing + dx * 2, 0, 60);
    dirty = 1;
  } else if (held[PAD_B]) {
    Track *t = &tp[cur_r];
    if (dy) t->vol = clampi(t->vol - dy, 0, 10);
    if (dx) t->tune = clampi(t->tune + dx, -12, 12);
    dirty = 1;
    dsp_trigger(cur_r, 0.8f, 0);
  } else {
    cur_c = (cur_c + dx + NS) % NS;
    cur_r = (cur_r + dy + NT) % NT;
  }
}

static void next_page(void) {
  /* Raster -> Akkorde -> Kit/Genre -> Effekte -> Raster */
  mode = (mode == M_GRID) ? M_CHORD : (mode == M_CHORD ? M_KIT : (mode == M_KIT ? M_FX : M_GRID));
}

static void do_press(int b) {
  if (mode == M_TEST) {
    if (b == PAD_START) { save_map(); mode = M_GRID; say("TASTEN GESPEICHERT"); }
    else if (b == PAD_SELECT) start_learn();
    else if (b == PAD_MENU) quit_req = 1;
    return;
  }
  if (b != PAD_SELECT && held[PAD_SELECT]) sel_used = 1;
  switch (b) {
    case PAD_UP: case PAD_DOWN: case PAD_LEFT: case PAD_RIGHT: do_dir(b); break;
    case PAD_A:
      if (mode == M_CHORD) {
        if (ch_row == 0) { uint8_t *c = &cps[curPat][ch_cur]; *c = (uint8_t)((*c + 1) % 8); dirty = 1; if (*c) chord_audition(*c); }
        else if (ch_row == 4) { ch_sev = !ch_sev; dirty = 1; chord_audition(0); }
        else if (ch_row == 8) { ch_on = !ch_on; dirty = 1; chord_audition(0); }
        else if (ch_row == 9) { dsp_chord_load(ch_preset, curPat); dirty = 1; say("AKKORDE GELADEN"); chord_audition(0); }
        else chord_audition(0);
        break;
      }
      if (mode == M_FX) { fxOn = !fxOn; dirty = 1; break; }
      if (mode == M_KIT) {
        load_beat(kit, curPat); dsp_chord_load(2 * kit, curPat); bpm = KITS[kit].bpm; swing = KITS[kit].sw; dirty = 1;
        { char m[40]; snprintf(m, sizeof m, "%s BEAT+AKKORDE GELADEN", KITS[kit].name); say(m); }
        break;
      }
      if (held[PAD_SELECT]) { load_beat(kit, curPat); dsp_chord_load(2 * kit, curPat); bpm = KITS[kit].bpm; swing = KITS[kit].sw; dirty = 1; say("BEAT+AKKORDE GELADEN"); break; }
      { uint8_t *c = &pats[curPat][cur_r][cur_c]; *c = (uint8_t)((*c + 1) % 3); dirty = 1;
        if (*c) dsp_trigger(cur_r, *c == 2 ? 1.0f : 0.62f, 0); }
      break;
    case PAD_B:
      if (mode == M_KIT || mode == M_FX || mode == M_CHORD) mode = M_GRID;
      break;
    case PAD_X:
      if (held[PAD_SELECT] && (mode == M_GRID || mode == M_CHORD)) { mode = (mode == M_CHORD) ? M_GRID : M_CHORD; break; }   /* Direktsprung */
      if (mode == M_CHORD) {
        if (ch_row == 0) { uint8_t *c = &cps[curPat][ch_cur]; *c = (uint8_t)((*c + 7) % 8); dirty = 1; if (*c) chord_audition(*c); }
        break;
      }
      if (mode == M_GRID) { tp[cur_r].mute = !tp[cur_r].mute; dirty = 1; }
      break;
    case PAD_Y:
      if (mode == M_CHORD) {
        if (held[PAD_SELECT]) memset(cps[curPat], 0, NS); else if (ch_row == 0) cps[curPat][ch_cur] = 0;
        dirty = 1; break;
      }
      if (mode == M_FX) { for (int i = 0; i < NFX; i++) fx[i] = FXD[i]; dirty = 1; break; }
      if (mode != M_GRID) break;
      if (held[PAD_SELECT]) clear_pattern(curPat); else memset(pats[curPat][cur_r], 0, NS);
      dirty = 1;
      break;
    case PAD_L: case PAD_R: {
      int d = (b == PAD_R) ? 1 : -1;
      if (held[PAD_SELECT] && mode == M_GRID) { set_kit(kit + d, 1); break; }
      int np = ((nextPat >= 0 && playing ? nextPat : curPat) + d + NP) % NP;
      if (playing) nextPat = np; else curPat = np;
      dirty = 1;
    } break;
    case PAD_START:
      if (held[PAD_SELECT] && mode != M_KIT) { mode = (mode == M_FX) ? M_GRID : M_FX; break; }
      dsp_request_play(playing ? 0 : 1);
      break;
    case PAD_SELECT: sel_used = 0; break;
    case PAD_MENU:
      if (held[PAD_SELECT]) start_learn(); else quit_req = 1;
      break;
  }
}

void bb_button(int b, int down) {
  if (b < 0 || b >= PAD_COUNT) return;
  if (mode == M_LEARN) return;
  if (down) {
    if (held[b]) return;
    held[b] = 1; hold_ms[b] = 0;
    do_press(b);
  } else {
    int was = held[b];
    held[b] = 0;
    if (was && b == PAD_SELECT && !sel_used && mode != M_TEST) next_page();   /* kurzes Antippen: naechste Seite */
  }
}

static void describe_raw(int code, char *dst, size_t n) {
  if (code >= 0x50000) snprintf(dst, n, "D-PAD ACHSE %d", code - 0x50000);
  else if (code >= 0x40000) snprintf(dst, n, "EVDEV %d TASTE %d", (code - 0x40000) >> 12, (code - 0x40000) & 0xFFF);
  else snprintf(dst, n, "SDL TASTE %d", code - 0x10000);
}

int bb_raw_key(int code, int down) {
  /* gedrueckt gehaltene Roh-Tasten verfolgen (zwei Quellen melden oft dieselbe Taste) */
  if (tms - last_raw_ms > 1500) nraw = 0;
  last_raw_ms = tms;
  int was_empty = (nraw == 0), idx = -1;
  for (int i = 0; i < nraw; i++) if (rawdown[i] == code) idx = i;
  if (down) { if (idx < 0 && nraw < 16) rawdown[nraw++] = code; describe_raw(code, lastraw, sizeof lastraw); }
  else if (idx >= 0) rawdown[idx] = rawdown[--nraw];

  if (mode == M_LEARN) {
    if (!down || !was_empty || idx >= 0 || tms < learn_ignore_until) return 1;
    for (int i = 0; i < learn_idx; i++)
      if (kmap[i] == code) { snprintf(learn_msg, sizeof learn_msg, "SCHON BELEGT: %s", PAD_LABEL[i]); return 1; }
    learn_msg[0] = 0;
    kmap[learn_idx++] = code;
    learn_ignore_until = tms + 400;
    if (learn_idx >= PAD_COUNT) { mode = M_TEST; memset(held, 0, sizeof held); }
    return 1;
  }
  for (int i = 0; i < PAD_COUNT; i++)
    if (kmap[i] == code) { bb_button(i, down); return 1; }
  return 0;
}

void bb_tick(int dt) {
  tms += (uint32_t)dt;
  if (toast_ms > 0) toast_ms -= dt;
  if (mode != M_LEARN && mode != M_TEST) {
    for (int b = PAD_UP; b <= PAD_RIGHT; b++) {
      if (!held[b]) continue;
      hold_ms[b] += dt;
      if (hold_ms[b] > 350) { hold_ms[b] -= 70; do_dir(b); }
    }
  }
  autosave_ms += dt;
  if (autosave_ms > 20000) { autosave_ms = 0; if (dirty) bb_save(); }
}

/* ------------------------------------------------------------------ */
/* Zeichnen                                                             */
/* ------------------------------------------------------------------ */
static uint16_t rgb(uint32_t c) {
  return (uint16_t)(((c >> 19) & 0x1F) << 11 | ((c >> 10) & 0x3F) << 5 | ((c >> 3) & 0x1F));
}
static uint16_t shade(uint32_t c, int pct) {
  int r = (int)((c >> 16) & 255) * pct / 100, g = (int)((c >> 8) & 255) * pct / 100, b = (int)(c & 255) * pct / 100;
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return rgb((uint32_t)(r << 16 | g << 8 | b));
}

static uint16_t *FB;

static void rect(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > BB_W) w = BB_W - x;
  if (y + h > BB_HGT) h = BB_HGT - y;
  for (int j = 0; j < h; j++) { uint16_t *p = FB + (y + j) * BB_W + x; for (int i = 0; i < w; i++) p[i] = c; }
}
static void frame(int x, int y, int w, int h, uint16_t c) {
  rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c); rect(x, y, 1, h, c); rect(x + w - 1, y, 1, h, c);
}
static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc; }
static void text(int x, int y, const char *s, int sc, uint16_t c) {
  for (; *s; s++, x += 6 * sc) {
    int ch = (unsigned char)*s;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 90) ch = '?';
    const unsigned char *g = FONT5X7[ch - 32];
    for (int cx = 0; cx < 5; cx++)
      for (int cy = 0; cy < 7; cy++)
        if (g[cx] >> cy & 1) rect(x + cx * sc, y + cy * sc, sc, sc, c);
  }
}
static void textc(int cx, int y, const char *s, int sc, uint16_t c) { text(cx - text_w(s, sc) / 2, y, s, sc, c); }

#define COL_BG 0x0e1117
#define COL_PANEL 0x181c25
#define COL_TXT 0xe6e9f0
#define COL_DIM 0x7d8596
#define COL_ACC 0xffb347

static void draw_pad_boxes(int highlight_held) {
  for (int i = 0; i < PAD_COUNT; i++) {
    int x = 40 + (i % 7) * 85, y = 330 + (i / 7) * 40;
    uint16_t c; uint16_t bg = rgb(COL_PANEL);
    if (highlight_held) { c = held[i] ? rgb(COL_BG) : rgb(0x7bed9f); if (held[i]) bg = rgb(0x7bed9f); }
    else c = i < learn_idx ? rgb(0x7bed9f) : (i == learn_idx ? rgb(COL_ACC) : rgb(0x3a4152));
    rect(x, y, 76, 30, bg); frame(x, y, 76, 30, c);
    textc(x + 38, y + 11, PAD_LABEL[i], 1, c);
  }
}

static void draw_learn(void) {
  rect(0, 0, BB_W, BB_HGT, rgb(COL_BG));
  textc(320, 24, "TASTEN EINLERNEN", 3, rgb(COL_ACC));
  textc(320, 70, "EINMALIGE EINRICHTUNG", 2, rgb(COL_DIM));
  if (learn_idx < PAD_COUNT) {
    textc(320, 130, "DRUECKE JETZT DIE TASTE:", 2, rgb(COL_TXT));
    textc(320, 175, PAD_LABEL[learn_idx], 6, rgb(0x7bed9f));
    char buf[32]; snprintf(buf, sizeof buf, "%d VON %d", learn_idx + 1, PAD_COUNT);
    textc(320, 250, buf, 2, rgb(COL_DIM));
  }
  if (learn_msg[0]) textc(320, 285, learn_msg, 2, rgb(0xff5a5f));
  draw_pad_boxes(0);
  textc(320, 420, "TASTE LOSLASSEN, BEVOR DU DIE NAECHSTE DRUECKST", 1, rgb(COL_DIM));
  { char b2[80]; snprintf(b2, sizeof b2, "LETZTES SIGNAL: %s", lastraw); textc(320, 440, b2, 1, rgb(COL_DIM)); }
}

static void draw_test(void) {
  rect(0, 0, BB_W, BB_HGT, rgb(COL_BG));
  textc(320, 24, "TASTENTEST", 3, rgb(COL_ACC));
  textc(320, 80, "DRUECKE ALLE TASTEN: DAS FELD LEUCHTET AUF", 2, rgb(COL_TXT));
  textc(320, 130, "LEUCHTET DIE RICHTIGE TASTE?", 2, rgb(COL_DIM));
  draw_pad_boxes(1);
  textc(320, 250, "ALLES RICHTIG: START = SPEICHERN", 2, rgb(0x7bed9f));
  textc(320, 285, "FALSCH: SELECT = NEU EINLERNEN", 2, rgb(0xff5a5f));
  { char b2[80]; snprintf(b2, sizeof b2, "LETZTES SIGNAL: %s", lastraw); textc(320, 440, b2, 1, rgb(COL_DIM)); }
}

static void draw_toast_at(int cx, int y, int h) {
  if (toast_ms <= 0) return;
  int w = text_w(toast, 2) + 24;
  rect(cx - w / 2, y, w, h, rgb(0x1f6f3f)); frame(cx - w / 2, y, w, h, rgb(0x7bed9f));
  textc(cx, y + (h - 14) / 2, toast, 2, rgb(COL_TXT));
}
static void draw_toast(void) { draw_toast_at(320, 352, 30); }

static void draw_kit(void) {
  rect(0, 0, BB_W, BB_HGT, rgb(COL_BG));
  rect(0, 0, BB_W, 44, rgb(COL_PANEL));
  text(12, 8, "KIT / GENRE", 3, rgb(COL_ACC));
  { char b[40]; snprintf(b, sizeof b, "PATTERN %c", 'A' + curPat); text(470, 14, b, 2, rgb(COL_TXT)); }
  for (int i = 0; i < NKITS; i++) {
    int y = 52 + i * 33, sel = (i == kit);
    rect(10, y, 620, 30, sel ? rgb(0x232938) : rgb(COL_PANEL));
    if (sel) frame(10, y, 620, 30, rgb(COL_ACC));
    text(24, y + 8, KITS[i].name, 2, sel ? rgb(COL_ACC) : rgb(COL_DIM));
    char b[32]; snprintf(b, sizeof b, "%d BPM", KITS[i].bpm);
    text(420, y + 8, b, 2, sel ? rgb(COL_TXT) : rgb(COL_DIM));
    if (sel) text(560, y + 8, "<<", 2, rgb(COL_ACC));
  }
  rect(0, 390, BB_W, 90, rgb(COL_PANEL));
  text(10, 398, "HOCH/RUNTER: KIT WAEHLEN (KLANG SOFORT)", 1, rgb(COL_TXT));
  text(10, 412, "A: BEAT DES KITS IN DAS AKTUELLE PATTERN LADEN", 1, rgb(0x7bed9f));
  text(10, 426, "L/R: PATTERN WECHSELN   START: PLAY/STOP   B: ZURUECK", 1, rgb(COL_DIM));
  text(10, 440, "SELECT ANTIPPEN: WEITER ZU EFFEKTEN", 1, rgb(COL_DIM));
  text(10, 454, "DER BEAT-LADEVORGANG LAEDT AUCH DIE PASSENDEN AKKORDE", 1, rgb(COL_DIM));
  draw_toast();
}

static void draw_chord(uint32_t now) {
  static const char *const LEN_N[5] = {"1 STEP", "2 STEPS", "4 STEPS", "8 STEPS", "HALTEN (16)"};
  static const char *const LBL[CH_ROWS] = {"", "SOUND", "TONART", "TONLEITER", "7ER-AKKORDE", "LAENGE", "OKTAVE", "LAUTSTAERKE", "AKKORDE", "VORLAGE (GENRE)"};
  static const uint32_t DEGC[8] = {0, 0xff7a7a, 0xffb347, 0xffe066, 0x7bed9f, 0x4cd7d0, 0x5aa9ff, 0xb18cff};
  rect(0, 0, BB_W, BB_HGT, rgb(COL_BG));
  rect(0, 0, BB_W, 44, rgb(COL_PANEL));
  text(12, 8, "AKKORDE", 3, rgb(COL_ACC));
  for (int q = 0; q < NP; q++) {
    int x = 250 + q * 40; int cur = q == curPat, nx = q == nextPat;
    uint16_t c = cur ? rgb(COL_ACC) : (nx && ((now / 250) & 1) ? rgb(0x7bed9f) : rgb(0x2a3040));
    rect(x, 8, 34, 30, c);
    char s[2] = {(char)('A' + q), 0};
    textc(x + 17, 16, s, 2, cur ? rgb(COL_BG) : rgb(COL_DIM));
  }
  if (playing) { rect(450, 12, 22, 22, rgb(0x7bed9f)); text(480, 14, "PLAY", 2, rgb(0x7bed9f)); }
  else { rect(450, 12, 7, 22, rgb(COL_DIM)); rect(462, 12, 7, 22, rgb(COL_DIM)); text(480, 14, "STOP", 2, rgb(COL_DIM)); }
  if (!ch_on) text(560, 14, "AUS", 2, rgb(0xff5a5f));

  /* Step-Reihe */
  const int LX = 10, LY = 52, LH = 56;
  int cover[NS]; memset(cover, 0, sizeof cover);
  for (int s = 0; s < NS; s++) {
    int d = cps[curPat][s];
    if (!d) continue;
    for (int k = 0; k < CH_LENS[ch_len] && s + k < NS; k++) { if (k > 0 && cps[curPat][s + k]) break; cover[s + k] = d; }
  }
  for (int s = 0; s < NS; s++) {
    int x = LX + s * 38 + (s / 4) * 4, d = cps[curPat][s];
    uint16_t bg = (s / 4) & 1 ? rgb(0x1d2230) : rgb(0x161a24);
    if (playing && play_step == s) bg = rgb(0x2f3850);
    rect(x, LY, 36, LH, bg);
    if (d) {
      rect(x + 1, LY + 1, 34, LH - 9, shade(DEGC[d], 45));
      char r[8], q[8]; dsp_chord_name(d, r, q);
      textc(x + 18, LY + 8, r, 2, rgb(COL_TXT));
      textc(x + 18, LY + 28, q, 1, rgb(COL_TXT));
    }
    if (cover[s]) rect(x + 1, LY + LH - 6, 34, 4, rgb(DEGC[cover[s]]));
    if (playing && play_step == s) frame(x, LY, 36, LH, rgb(0x8aa0d0));
    if (s == ch_cur) frame(x - 1, LY - 1, 38, LH + 2, ch_row == 0 ? rgb(0xffffff) : rgb(0x5a6070));
  }
  /* Info zum Cursor */
  { char r[8], q[8], b[48];
    int d = cps[curPat][ch_cur];
    if (d) { dsp_chord_name(d, r, q); snprintf(b, sizeof b, "STEP %d: %s %s", ch_cur + 1, r, q); }
    else snprintf(b, sizeof b, "STEP %d: -", ch_cur + 1);
    text(14, 114, b, 2, rgb(ch_row == 0 ? COL_ACC : COL_DIM)); }

  /* Parameter */
  for (int i = 1; i < CH_ROWS; i++) {
    int y = 136 + (i - 1) * 27, sel = i == ch_row;
    rect(10, y, 620, 25, sel ? rgb(0x232938) : rgb(COL_PANEL));
    if (sel) frame(10, y, 620, 25, rgb(COL_ACC));
    text(24, y + 5, LBL[i], 2, sel ? rgb(COL_TXT) : rgb(COL_DIM));
    char b[40]; uint16_t vc = sel ? rgb(COL_ACC) : rgb(COL_TXT);
    switch (i) {
      case 1: snprintf(b, sizeof b, "%s", CH_INST_NAMES[ch_inst]); break;
      case 2: snprintf(b, sizeof b, "%s", NOTE_NAMES[ch_key]); break;
      case 3: snprintf(b, sizeof b, "%s", ch_minor ? "MOLL" : "DUR"); break;
      case 4: snprintf(b, sizeof b, "%s", ch_sev ? "AN" : "AUS"); break;
      case 5: snprintf(b, sizeof b, "%s", LEN_N[ch_len]); break;
      case 6: snprintf(b, sizeof b, ch_oct ? "%+d" : "%d", ch_oct); break;
      case 7: snprintf(b, sizeof b, "%d", ch_vol); break;
      case 8: snprintf(b, sizeof b, "%s", ch_on ? "AN" : "AUS"); break;
      default: snprintf(b, sizeof b, "%s", CHP[ch_preset].name); break;
    }
    text(270, y + 5, b, 2, vc);
    if (i == 7) for (int k = 0; k < 10; k++) rect(400 + k * 20, y + 6, 16, 13, k < ch_vol ? rgb(COL_ACC) : rgb(0x2a3040));
    if (i == 9) text(560, y + 9, "A=LADEN", 1, rgb(0x7bed9f));
  }
  rect(0, 392, BB_W, 88, rgb(COL_PANEL));
  text(10, 398, "HOCH/RUNTER: ZEILE   LINKS/RECHTS: STEP ODER WERT AENDERN", 1, rgb(COL_TXT));
  text(10, 411, "STEP-REIHE: A = NAECHSTER AKKORD (I-VII)  X = ZURUECK  Y = STEP LEEREN", 1, rgb(0x7bed9f));
  text(10, 424, "VORLAGE: LINKS/RECHTS GENRE WAEHLEN, A LAEDT DIE AKKORDE INS PATTERN", 1, rgb(0x7bed9f));
  text(10, 437, "L/R: PATTERN   START: PLAY/STOP   SELECT+Y: ALLE AKKORDE LEEREN", 1, rgb(COL_DIM));
  text(10, 450, "SELECT ANTIPPEN: WEITER ZU KIT/GENRE   SELECT+X ODER B: ZURUECK ZUM RASTER", 1, rgb(COL_DIM));
  text(10, 463, "KIT/GENRE LADEN (SELECT+L/R ODER KIT-SEITE) LAEDT AUCH DIE AKKORDE", 1, rgb(COL_DIM));
  draw_toast_at(450, 111, 22);
}

static void draw_fx(uint32_t now) {
  static const char *const FXN[NFX] = {"FILTER", "RESO", "DLY MIX", "DLY TIME", "DLY FDBK", "VERB MIX", "VERB SIZE", "DRIVE", "CRUSH"};
  static const char *const DT[4] = {"1/16", "1/8", "3/16", "1/4"};
  static const char *const RS[4] = {"KLEIN", "MITTEL", "GROSS", "HALLE"};
  rect(0, 0, BB_W, BB_HGT, rgb(COL_BG));
  text(16, 12, "MASTER EFFEKTE", 3, rgb(COL_ACC));
  { const char *s = fxOn ? "AN" : "AUS"; text(560 - (fxOn ? 0 : 12), 14, s, 3, fxOn ? rgb(0x7bed9f) : rgb(0xff5a5f)); }
  for (int i = 0; i < NFX; i++) {
    int y = 60 + i * 38; int sel = i == fx_sel;
    rect(10, y, 620, 34, sel ? rgb(0x232938) : rgb(COL_PANEL));
    if (sel) frame(10, y, 620, 34, rgb(COL_ACC));
    text(24, y + 10, FXN[i], 2, sel ? rgb(COL_TXT) : rgb(COL_DIM));
    int n = FXMAX[i];
    int bw = n > 3 ? 24 : 60, gap = 4, x0 = 220;
    for (int k = 0; k <= n; k++) {
      int on = k <= fx[i] && !(i == 0 && 0);
      uint16_t c = on ? rgb(fxOn ? COL_ACC : 0x6b7280) : rgb(0x2a3040);
      if (k == 0 && n > 3) { rect(x0 + k * (bw + gap), y + 8, bw, 18, on ? c : rgb(0x2a3040)); }
      else rect(x0 + k * (bw + gap), y + 8, bw, 18, c);
    }
    char buf[24];
    if (i == 0) { int v = fx[0]; snprintf(buf, sizeof buf, "%s", v == 5 ? "OFFEN" : (v < 5 ? "TIEFPASS" : "HOCHPASS")); }
    else if (i == 3) snprintf(buf, sizeof buf, "%s", DT[fx[i]]);
    else if (i == 6) snprintf(buf, sizeof buf, "%s", RS[fx[i]]);
    else snprintf(buf, sizeof buf, "%d", fx[i]);
    if (i == 0) text(545, y + 14, buf, 1, rgb(COL_TXT)); else text(545, y + 10, buf, 2, rgb(COL_TXT));
  }
  textc(320, 410, "HOCH/RUNTER: WAHL   LINKS/RECHTS: WERT", 1, rgb(COL_DIM));
  textc(320, 424, "A: AN/AUS   Y: ZURUECKSETZEN   START: PLAY/STOP", 1, rgb(COL_DIM));
  textc(320, 438, "B ODER SELECT ANTIPPEN: ZURUECK ZUM RASTER", 1, rgb(COL_DIM));
  (void)now;
}

static void draw_grid(uint32_t now) {
  rect(0, 0, BB_W, BB_HGT, rgb(COL_BG));
  /* Kopfzeile */
  rect(0, 0, BB_W, 46, rgb(COL_PANEL));
  text(10, 6, KITS[kit].name, 3, rgb(COL_ACC));
  char buf[40];
  snprintf(buf, sizeof buf, "%d BPM", bpm); text(190, 8, buf, 2, rgb(COL_TXT));
  snprintf(buf, sizeof buf, "SW %d", swing); text(190, 28, buf, 1, rgb(COL_DIM));
  for (int p = 0; p < NP; p++) {
    int x = 330 + p * 40; int cur = p == curPat, nx = p == nextPat;
    uint16_t c = cur ? rgb(COL_ACC) : (nx && ((now / 250) & 1) ? rgb(0x7bed9f) : rgb(0x2a3040));
    rect(x, 8, 34, 30, c);
    char s[2] = {(char)('A' + p), 0};
    textc(x + 17, 16, s, 2, cur ? rgb(COL_BG) : rgb(COL_DIM));
  }
  if (playing) { rect(520, 12, 22, 22, rgb(0x7bed9f)); text(550, 14, "PLAY", 2, rgb(0x7bed9f)); }
  else { rect(520, 12, 7, 22, rgb(COL_DIM)); rect(532, 12, 7, 22, rgb(COL_DIM)); text(550, 14, "STOP", 2, rgb(COL_DIM)); }

  /* Raster */
  const int X0 = 84, CW = 30, RH = 28, Y0 = 54;
  int ps = play_step;
  for (int r = 0; r < NT; r++) {
    int y = Y0 + r * RH;
    uint32_t tc = TRACK_COLORS[r];
    int sel = (r == cur_r);
    uint16_t lc = tp[r].mute ? rgb(0x4a5060) : rgb(tc);
    if (sel) rect(0, y, 80, RH - 2, rgb(0x232938));
    text(6, y + 7, TRACK_NAMES[r], 2, lc);
    for (int s = 0; s < NS; s++) {
      int x = X0 + s * CW + (s / 4) * 4;
      uint8_t v = pats[curPat][r][s];
      uint16_t bg = (s / 4) & 1 ? rgb(0x1d2230) : rgb(0x161a24);
      if (ps == s && playing) bg = rgb(0x2f3850);
      rect(x, y, CW - 3, RH - 3, bg);
      if (v) {
        uint16_t c = tp[r].mute ? rgb(0x4a5060) : (v == 2 ? rgb(tc) : shade(tc, 60));
        rect(x + 1, y + 1, CW - 5, RH - 5, c);
        if (v == 2) rect(x + 9, y + 8, 8, 8, rgb(0xffffff));
      }
      if (ps == s && playing) frame(x, y, CW - 3, RH - 3, rgb(0x8aa0d0));
    }
    snprintf(buf, sizeof buf, "V%d", tp[r].vol);
    text(X0 + NS * CW + 16 + 2, y + 3, buf, 1, rgb(COL_DIM));
    snprintf(buf, sizeof buf, "%+d", tp[r].tune);
    text(X0 + NS * CW + 16 + 2, y + 14, buf, 1, rgb(COL_DIM));
  }
  /* Cursor */
  {
    int x = X0 + cur_c * CW + (cur_c / 4) * 4, y = Y0 + cur_r * RH;
    uint16_t c = held[PAD_B] ? rgb(0x7bed9f) : rgb(0xffffff);
    frame(x - 1, y - 1, CW - 1, RH - 1, c); frame(x, y, CW - 3, RH - 3, c);
  }
  /* Fuss */
  rect(0, 396, BB_W, 84, rgb(COL_PANEL));
  snprintf(buf, sizeof buf, "%s  VOL %d  TUNE %+d%s", TRACK_NAMES[cur_r], tp[cur_r].vol, tp[cur_r].tune, tp[cur_r].mute ? "  MUTE" : "");
  text(10, 402, buf, 2, rgb(TRACK_COLORS[cur_r]));
  if (!fxOn) text(560, 402, "FX AUS", 1, rgb(COL_DIM));
  { char b2[40]; snprintf(b2, sizeof b2, "AKK: %s %s", CH_INST_NAMES[ch_inst], ch_on ? "AN" : "AUS"); text(400, 406, b2, 1, rgb(COL_DIM)); }
  text(10, 424, "A: STEP AN/AKZENT/AUS   X: MUTE   Y: ZEILE LEEREN   L/R: PATTERN", 1, rgb(COL_DIM));
  text(10, 438, "B+PFEILE: VOL (HOCH/RUNTER) TUNE (LINKS/RECHTS)   START: PLAY/STOP", 1, rgb(COL_DIM));
  text(10, 452, "SELECT ANTIPPEN: AKKORDE -> KIT/GENRE -> EFFEKTE   SELECT+X: AKKORD-SEITE", 1, rgb(0x7bed9f));
  text(10, 466, "SELECT+PFEILE: BPM/SWING  SELECT+L/R: KIT  SELECT+Y: PATTERN LEEREN  MENU: ENDE", 1, rgb(COL_DIM));
}

void bb_draw(uint16_t *fb, uint32_t now) {
  FB = fb;
  if (mode == M_LEARN) draw_learn();
  else if (mode == M_TEST) draw_test();
  else if (mode == M_KIT) draw_kit();
  else if (mode == M_CHORD) draw_chord(now);
  else if (mode == M_FX) { draw_fx(now); draw_toast(); }
  else { draw_grid(now); draw_toast(); }
}
