/* Headless-Test des Kerns: rendert jedes Kit als WAV, prueft Pegel/NaN,
 * erzeugt Screenshots (PPM) und misst die Rechenzeit.
 * Aufruf: ./bb_test <ausgabeverzeichnis> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bb_priv.h"

#define SR 44100

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FEHLER: " __VA_ARGS__); printf("\n"); } } while (0)

static void write_wav(const char *path, const int16_t *d, int frames) {
  FILE *f = fopen(path, "wb"); if (!f) return;
  unsigned n = (unsigned)frames * 4, v;
  unsigned short s;
  fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
  v = SR; fwrite(&v, 4, 1, f); v = SR * 4; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(d, 4, (size_t)frames, f); fclose(f);
}

static void write_ppm(const char *path, const uint16_t *fb) {
  FILE *f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", BB_W, BB_HGT);
  for (int i = 0; i < BB_W * BB_HGT; i++) {
    uint16_t p = fb[i];
    unsigned char c[3] = {(unsigned char)(((p >> 11) & 31) * 255 / 31), (unsigned char)(((p >> 5) & 63) * 255 / 63), (unsigned char)((p & 31) * 255 / 31)};
    fwrite(c, 1, 3, f);
  }
  fclose(f);
}

typedef struct { double peak, rms; int nan; } Stat;
static Stat stat_of(const int16_t *d, int frames, int from) {
  Stat s = {0, 0, 0}; double acc = 0; int n = 0;
  for (int i = from; i < frames; i++) {
    double v = d[2 * i] / 32768.0;
    if (fabs(v) > s.peak) s.peak = fabs(v);
    acc += v * v; n++;
  }
  s.rms = sqrt(acc / (n ? n : 1));
  return s;
}

static void render(int16_t *buf, int frames) {
  int done = 0;
  while (done < frames) { int n = frames - done > 512 ? 512 : frames - done; bb_audio_render(buf + 2 * done, n); done += n; }
}

static void press(int b) { bb_button(b, 1); bb_button(b, 0); }

int main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : ".";
  char p[512];
  snprintf(p, sizeof p, "%s/data", out);
  char cmd[1100]; snprintf(cmd, sizeof cmd, "mkdir -p '%s'; rm -f '%s'/*", p, p); if (system(cmd)) return 2;

  bb_init(SR, p);
  /* Ohne Zuordnung muss der Lern-Modus erscheinen */
  static uint16_t fb[BB_W * BB_HGT];
  bb_draw(fb, 0);
  snprintf(p, sizeof p, "%s/shot_learn0.ppm", out); write_ppm(p, fb);
  /* Lernen simulieren: 13 verschiedene Codes, dazwischen Zeit vergehen lassen */
  for (int i = 0; i < PAD_COUNT; i++) {
    bb_raw_key(100 + i, 1); bb_raw_key(100 + i, 0);
    if (i == 5) { bb_draw(fb, 0); snprintf(p, sizeof p, "%s/shot_learn1.ppm", out); write_ppm(p, fb); }
    for (int t = 0; t < 40; t++) bb_tick(16);
  }
  CHECK(bb_raw_key(103, 1) == 1, "gelernte Taste wird nicht verbraucht");
  bb_raw_key(103, 0);
  CHECK(bb_raw_key(9999, 1) == 0, "unbekannte Taste wird verbraucht");
  bb_test_identity_map();

  /* --- alle Kits rendern --- */
  int secs = 8, frames = SR * secs;
  int16_t *buf = (int16_t *)malloc((size_t)frames * 4);
  printf("Kit        BPM  Peak   RMS    \n");
  for (int k = 0; k < NKITS; k++) {
    bb_init(SR, "/tmp/bb_nodata_xyz");
    bb_test_identity_map();
    bb_button(PAD_SELECT, 1);
    for (int i = 0; i < k; i++) press(PAD_R);
    bb_button(PAD_SELECT, 0);
    CHECK(kit == k, "Kit %d nicht gesetzt (kit=%d)", k, kit);
    bb_button(PAD_START, 1); bb_button(PAD_START, 0);
    render(buf, frames);
    Stat s = stat_of(buf, frames, SR / 2);
    printf("%-9s %4d  %.3f  %.3f\n", KITS[k].name, bpm, s.peak, s.rms);
    CHECK(s.peak > 0.1, "%s: zu leise (Peak %.3f)", KITS[k].name, s.peak);
    CHECK(s.peak < 0.99, "%s: uebersteuert (Peak %.3f)", KITS[k].name, s.peak);
    CHECK(s.rms > 0.02 && s.rms < 0.5, "%s: RMS unplausibel %.3f", KITS[k].name, s.rms);
    snprintf(p, sizeof p, "%s/kit_%02d_%s.wav", out, k, KITS[k].name);
    write_wav(p, buf, frames);
    bb_button(PAD_START, 1); bb_button(PAD_START, 0);
    render(buf, SR / 2);
  }


  /* --- jede Spur in jedem Kit einzeln --- */
  printf("Einzelspur-Tests\n");
  { int bad = 0;
    for (int k = 0; k < NKITS; k++) {
      for (int t = 0; t < NT; t++) {
        bb_init(SR, "/tmp/bb_nodata_xyz"); bb_test_identity_map();
        kit = k; memset(pats, 0, sizeof pats);
        dsp_trigger(t, 1.0f, 0);
        render(buf, SR);
        Stat s = stat_of(buf, SR, 0);
        int nanv = 0; for (int i = 0; i < SR * 2; i++) if (buf[i] == -32768) nanv++;
        if (s.peak < 0.02 || s.peak > 0.98 || nanv) { bad++; printf("  %s/%s: peak %.3f\n", KITS[k].name, TRACK_NAMES[t], s.peak); }
      }
    }
    CHECK(bad == 0, "%d Einzelspuren auffaellig", bad); }

  /* --- Effekte --- */
  printf("Effekt-Tests (Kit HIPHOP)\n");
  bb_init(SR, "/tmp/bb_nodata_xyz"); bb_test_identity_map();
  press(PAD_START);
  render(buf, SR * 4);
  Stat base = stat_of(buf, SR * 4, SR);
  extern int fx[]; extern int fxOn;
  struct { const char *n; int idx, val; } tests[] = {
    {"Tiefpass 1", 0, 1}, {"Hochpass 9", 0, 9}, {"Delay Mix 8", 2, 8}, {"Verb Mix 10", 5, 10},
    {"Drive 10", 7, 10}, {"Crush 10", 8, 10}
  };
  for (unsigned t = 0; t < sizeof tests / sizeof *tests; t++) {
    for (int i = 0; i < NFX; i++) fx[i] = FXD[i];
    fx[tests[t].idx] = tests[t].val;
    render(buf, SR);          /* einschwingen */
    render(buf, SR * 4);
    Stat s = stat_of(buf, SR * 4, SR);
    printf("  %-12s peak %.3f rms %.3f (Basis rms %.3f)\n", tests[t].n, s.peak, s.rms, base.rms);
    CHECK(s.peak <= 1.0 && s.rms > 0.003, "Effekt %s: Pegel unplausibel", tests[t].n);
    for (int i = 0; i < SR * 4 * 2; i++) if (buf[i] == 32767 && buf[i + (i + 1 < SR * 8 ? 1 : 0)] == 32767) { CHECK(0, "Effekt %s: Clipping-Serie", tests[t].n); break; }
  }
  for (int i = 0; i < NFX; i++) fx[i] = 10 > FXMAX[i] ? FXMAX[i] : 10;  /* Extremwerte, alles auf Maximum */
  render(buf, SR); render(buf, SR * 4);
  { Stat s = stat_of(buf, SR * 4, 0); printf("  ALLES MAX   peak %.3f rms %.3f\n", s.peak, s.rms); CHECK(s.peak <= 1.0, "alles max: Clipping"); }
  for (int i = 0; i < NFX; i++) fx[i] = FXD[i];
  fxOn = 0; fx[7] = 10; render(buf, SR);
  fxOn = 1;
  press(PAD_START);
  render(buf, SR * 6);   /* Nachklang abklingen lassen */
  { Stat s = stat_of(buf, SR * 6, SR * 5); printf("  Stille nach Stop: rms %.6f\n", s.rms); CHECK(s.rms < 0.005, "Nachklang zu laut"); }

  /* --- Bedienung --- */
  printf("Bedienungs-Tests\n");
  snprintf(p, sizeof p, "%s/data", out);
  bb_init(SR, p); bb_test_identity_map();
  press(PAD_RIGHT); press(PAD_DOWN);
  press(PAD_A);                       /* cur = (1,1) */
  CHECK(pats[0][1][1] == 1, "A setzt Step nicht");
  press(PAD_A); CHECK(pats[0][1][1] == 2, "A Akzent");
  press(PAD_A); CHECK(pats[0][1][1] == 0, "A aus");
  int b0 = bpm;
  bb_button(PAD_SELECT, 1); press(PAD_UP); press(PAD_UP); bb_button(PAD_SELECT, 0);
  CHECK(bpm == b0 - 0 + 0 || bpm == b0 + 0 || 1, "");
  CHECK(bpm != b0, "BPM nicht geaendert");
  bb_button(PAD_B, 1); press(PAD_UP); press(PAD_RIGHT); press(PAD_RIGHT); bb_button(PAD_B, 0);
  CHECK(tp[1].tune == 2 && tp[1].vol == 9, "Vol/Tune: tune=%d vol=%d", tp[1].tune, tp[1].vol);
  press(PAD_X); CHECK(tp[1].mute == 1, "Mute");
  press(PAD_Y); { int sum = 0; for (int s = 0; s < NS; s++) sum += pats[0][1][s]; CHECK(sum == 0, "Zeile leeren"); }
  bb_button(PAD_SELECT, 1); press(PAD_START); bb_button(PAD_SELECT, 0);
  bb_draw(fb, 1000); snprintf(p, sizeof p, "%s/shot_fx.ppm", out); write_ppm(p, fb);
  press(PAD_RIGHT); press(PAD_DOWN); press(PAD_RIGHT); press(PAD_RIGHT);
  CHECK(fx[0] == 6 && fx[1] == 2, "FX-Seite Werte %d %d", fx[0], fx[1]);
  bb_button(PAD_SELECT, 1); press(PAD_START); bb_button(PAD_SELECT, 0);
  press(PAD_L); CHECK(curPat == 3, "Pattern zurueck");
  bb_save();
  int sv_bpm = bpm, sv_pat = curPat;
  bb_init(SR, "/tmp/bb_nodata_xyz");
  CHECK(bpm != sv_bpm || curPat != sv_pat, "Leeres Verzeichnis liefert Daten");
  snprintf(p, sizeof p, "%s/data", out);
  bb_init(SR, p);
  CHECK(bpm == sv_bpm && curPat == sv_pat, "Laden: bpm %d/%d pat %d/%d", bpm, sv_bpm, curPat, sv_pat);
  CHECK(tp[1].tune == 2 && fx[0] == 6, "Laden: tune/fx");
  bb_test_identity_map();
  curPat = 0;
  /* Rueckwaerts: Beat laden, Pattern wechseln waehrend Wiedergabe */
  press(PAD_START);
  render(buf, 512);
  press(PAD_R); CHECK(nextPat == 1, "Pattern queue");
  render(buf, SR * 4);
  CHECK(curPat == 1 && nextPat == -1, "Pattern-Wechsel am Taktende (cur=%d next=%d)", curPat, nextPat);
  press(PAD_START);
  render(buf, 4096);
  CHECK(!playing, "Stop");

  /* --- Screenshots --- */
  bb_init(SR, "/tmp/bb_nodata_xyz"); bb_test_identity_map();
  press(PAD_START); render(buf, SR * 1); bb_draw(fb, 0);
  press(PAD_RIGHT); press(PAD_RIGHT); press(PAD_DOWN); press(PAD_DOWN);
  render(buf, SR / 5); bb_draw(fb, 500);
  snprintf(p, sizeof p, "%s/shot_grid.ppm", out); write_ppm(p, fb);

  /* --- Rechenzeit --- */
  bb_init(SR, "/tmp/bb_nodata_xyz"); bb_test_identity_map();
  press(PAD_START);
  for (int i = 0; i < NT; i++) for (int s = 0; s < NS; s++) pats[0][i][s] = 1;  /* Worst case: alles gleichzeitig */
  for (int i = 0; i < NFX; i++) fx[i] = (i == 0) ? 5 : FXMAX[i] / 2;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  render(buf, SR * 6);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double sec = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
  printf("Rechenzeit Worst-Case: %.3f s fuer 6 s Audio (Faktor %.1fx Echtzeit)\n", sec, 6.0 / sec);

  free(buf);
  printf(g_fail ? "\n%d FEHLER\n" : "\nALLE TESTS OK\n", g_fail);
  return g_fail ? 1 : 0;
}
