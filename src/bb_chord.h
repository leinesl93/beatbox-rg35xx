/* Akkord-Engine (wird von bb_dsp.c eingebunden): 8 Klaenge, bis zu 4 Akkord-Stimmen.
 * Jeder Akkord besteht aus 4 Toenen; Stufe 1..7 der gewaehlten Tonleiter. */

uint8_t cps[NP][NS];
int ch_on = 1, ch_inst = 1, ch_key = 9, ch_minor = 1, ch_sev = 1, ch_len = 2, ch_oct = 0, ch_vol = 6, ch_preset = 0;
const int CH_LENS[5] = {1, 2, 4, 8, 16};
const char *const CH_INST_NAMES[NCI] = {"PIANO", "RHODES", "PAD", "STRINGS", "ORGAN", "PLUCK", "SUPER SAW", "BELL"};
const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* Vorlagen: Name, Klang, Tonart, Moll, 7er, Laenge-Index, Oktave, 16 Steps (Stufe 1..7, '.' = keiner) */
const ChPreset CHP[NCP] = {
  {"HIPHOP RHODES",     1, 9, 1, 1, 2, 0, "1...4...6...5..."},
  {"HIPHOP PIANO",      0, 5, 1, 1, 1, 0, "1.....1.4.....4."},
  {"TECHNO STAB",       5, 9, 1, 0, 0, 0, "..1...1...1...6."},
  {"TECHNO DRONE",      2, 0, 1, 1, 3, 0, "1.......7......."},
  {"LOFI RHODES",       1, 5, 0, 1, 2, 0, "1...6...2...5..."},
  {"LOFI PIANO",        0, 9, 1, 1, 2, 0, "1...4...3...6..."},
  {"HOUSE PIANO",       0, 9, 1, 1, 1, 0, "1..1..1.4..4..4."},
  {"HOUSE ORGAN",       4, 2, 1, 0, 1, 0, "..1...1...4...4."},
  {"TRAP BELLS",        7, 1, 1, 0, 3, 1, "1.......6......."},
  {"TRAP PAD",          2, 5, 1, 1, 4, 0, "1.......7......."},
  {"DNB PAD",           2, 4, 1, 1, 3, 0, "1.......6......."},
  {"DNB KEYS",          1, 9, 1, 1, 1, 0, "1..1....4..4...."},
  {"ELECTRO SAW",       6, 9, 1, 0, 1, 0, "1.1...1.4.4...4."},
  {"ELECTRO BELLS",     7, 7, 1, 0, 2, 1, "1...3...4...5..."},
  {"DUB ORGAN",         4, 2, 1, 0, 0, 0, "..1...1...1...1."},
  {"DUB PAD",           2, 2, 1, 1, 3, 0, "1.......4......."},
  {"SYNTHWAVE SAW",     6, 9, 1, 0, 2, 0, "1...6...3...7..."},
  {"SYNTHWAVE STRINGS", 3, 4, 1, 0, 2, 0, "1...4...6...7..."},
  {"DISCO PIANO",       0, 7, 0, 1, 1, 0, "1..1..4..4..5.5."},
  {"DISCO STRINGS",     3, 7, 0, 1, 3, 0, "1.......4......."},
};

void dsp_chord_load(int p, int pat) {
  if (p < 0 || p >= NCP || pat < 0 || pat >= NP) return;
  const ChPreset *c = &CHP[p];
  ch_inst = c->inst; ch_key = c->key; ch_minor = c->minor; ch_sev = c->sev; ch_len = c->len; ch_oct = c->oct;
  ch_preset = p;
  for (int s = 0; s < NS; s++) { char ch = c->steps[s]; cps[pat][s] = (ch >= '1' && ch <= '7') ? (uint8_t)(ch - '0') : 0; }
}

static const int SC_MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, SC_MIN[7] = {0, 2, 3, 5, 7, 8, 10};
static int scale_note(int i) { return (ch_minor ? SC_MIN : SC_MAJ)[i % 7] + 12 * (i / 7); }

void dsp_chord_notes(int deg, int out[4]) {
  int d = (deg < 1 ? 1 : (deg > 7 ? 7 : deg)) - 1;
  int base = 48 + ch_key + 12 * ch_oct;
  int r = scale_note(d), sh = r > 6 ? -12 : 0;
  out[0] = base + r + sh;
  out[1] = base + scale_note(d + 2) + sh;
  out[2] = base + scale_note(d + 4) + sh;
  out[3] = base + (ch_sev ? scale_note(d + 6) : r + 12) + sh;
}

void dsp_chord_name(int deg, char *root, char *qual) {
  int d = (deg < 1 ? 1 : (deg > 7 ? 7 : deg)) - 1, r = scale_note(d);
  int t = scale_note(d + 2) - r, f = scale_note(d + 4) - r, s = scale_note(d + 6) - r;
  const char *q;
  if (!ch_sev) q = (t == 4) ? "MAJ" : (f == 6 ? "DIM" : "MIN");
  else if (t == 4) q = (s == 11) ? "MAJ7" : "7";
  else if (f == 6) q = (s == 9) ? "DIM7" : "M7B5";
  else q = "MIN7";
  strcpy(root, NOTE_NAMES[((ch_key + r) % 12 + 12) % 12]);
  strcpy(qual, q);
}

/* ------------------------------------------------------------------ */
typedef struct { int type, nsaw; float atk, dec, sus, rel, amp, lp0, lp1, lpt, det; } CInst;
static const CInst CI[NCI] = {
  /* PIANO   */ {0, 0, 0.002f, 1.0f, 1.0f, 0.18f, 0.32f, 0, 0, 0, 0},
  /* RHODES  */ {1, 0, 0.003f, 1.0f, 1.0f, 0.30f, 0.55f, 0, 0, 0, 0},
  /* PAD     */ {2, 2, 0.40f, 1.5f, 0.85f, 0.90f, 0.30f, 500, 2200, 1.8f, 8},
  /* STRINGS */ {2, 3, 0.20f, 1.0f, 0.90f, 0.50f, 0.24f, 1400, 4200, 0.9f, 9},
  /* ORGAN   */ {3, 0, 0.008f, 1.0f, 1.0f, 0.06f, 0.30f, 0, 0, 0, 0},
  /* PLUCK   */ {2, 2, 0.003f, 0.12f, 0.0f, 0.10f, 0.80f, 6000, 700, 0.22f, 10},
  /* SUPER   */ {2, 3, 0.010f, 0.50f, 0.55f, 0.25f, 0.28f, 5000, 2200, 0.5f, 14},
  /* BELL    */ {4, 0, 0.002f, 1.0f, 1.0f, 0.70f, 0.45f, 0, 0, 0, 0},
};

#define NCV 4
typedef struct {
  int active, inst, stage; long gateN, n;
  float env, atkStep, decM, relM, dt, vel;
  float f[4], ph[4][5], pa[4][5], pm[4][5], inc[4][3];
  Bq bq; float cut, cmul;
  long age;
} CVoice;
static CVoice CV[NCV];
static long cv_age = 0;
static float s_cg = 0.0f;

static inline float fsin(float p) {          /* Sinus, Phase in Perioden, schnelle Naeherung */
  int i = (int)p; p -= (float)i; if (p < 0.0f) p += 1.0f;
  if (p > 0.5f) p -= 1.0f;
  float u = 2.0f * p, y = 4.0f * u * (1.0f - fabsf(u));
  return 0.225f * (y * fabsf(y) - y) + y;
}
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }
static float t60(float sec) { return powf(0.001f, 1.0f / (sec * (float)SR)); }

static void chord_trigger(int deg, long gate, float vel) {
  if (deg < 1 || deg > 7) return;
  for (int i = 0; i < NCV; i++) if (CV[i].active && CV[i].gateN > 0) CV[i].gateN = 0;   /* alter Akkord klingt aus */
  int pick = -1;
  for (int i = 0; i < NCV; i++) if (!CV[i].active) { pick = i; break; }
  if (pick < 0) { long o = CV[0].age; pick = 0; for (int i = 1; i < NCV; i++) if (CV[i].age < o) { o = CV[i].age; pick = i; } }
  CVoice *v = &CV[pick];
  const CInst *I = &CI[ch_inst];
  memset(v, 0, sizeof *v);
  v->active = 1; v->inst = ch_inst; v->age = ++cv_age; v->vel = vel; v->gateN = gate < 1 ? 1 : gate;
  v->dt = 1.0f / (float)SR;
  v->atkStep = 1.0f / (I->atk * (float)SR);
  v->decM = expf(-1.0f / (I->dec * (float)SR));
  v->relM = t60(I->rel);
  int notes[4]; dsp_chord_notes(deg, notes);
  for (int n = 0; n < 4; n++) {
    float f = 440.0f * powf(2.0f, (float)(notes[n] - 69) / 12.0f);
    float sc = powf(2.0f, -(float)(notes[n] - 60) / 24.0f);
    if (sc < 0.35f) sc = 0.35f;
    if (sc > 2.0f) sc = 2.0f;
    v->f[n] = f;
    switch (I->type) {
      case 0: { /* Klavier: 5 Teiltoene, hoehere klingen schneller ab */
        static const float A[5] = {1.0f, 0.55f, 0.32f, 0.18f, 0.10f};
        float br = 1.25f - (float)(notes[n] - 48) / 48.0f; if (br < 0.5f) br = 0.5f;
        for (int k = 0; k < 5; k++) {
          v->pa[n][k] = A[k] * (k ? br : 1.0f) * (f * (float)(k + 1) < 0.45f * SR ? 1.0f : 0.0f);
          v->pm[n][k] = t60(2.8f * sc / (1.0f + 0.9f * (float)k));
        }
      } break;
      case 1: { /* E-Piano: FM mit abklingendem Index plus Zinken-Anschlag */
        float idx = (2.4f / 6.2832f) * (1.3f - (float)(notes[n] - 48) / 60.0f);
        v->pa[n][0] = 1.0f; v->pm[n][0] = t60(2.6f * sc);
        v->pa[n][1] = idx;  v->pm[n][1] = t60(0.9f * sc);
        v->pa[n][2] = f * 14.0f < 0.45f * SR ? 0.5f : 0.0f; v->pm[n][2] = t60(0.15f);
      } break;
      case 2: { /* verstimmte Saegezaehne */
        for (int j = 0; j < I->nsaw; j++) {
          float cents = ((float)j - (float)(I->nsaw - 1) * 0.5f) * I->det;
          v->inc[n][j] = f * powf(2.0f, cents / 1200.0f) * v->dt;
          v->ph[n][j] = (float)((n * 7 + j * 3) % 10) / 10.0f;
        }
      } break;
      case 3: break;
      case 4: { /* Glocke: unharmonische Teiltoene */
        static const float R[4] = {1.0f, 2.76f, 5.40f, 8.93f}, A[4] = {1.0f, 0.6f, 0.35f, 0.2f}, T[4] = {3.0f, 2.0f, 1.2f, 0.7f};
        for (int k = 0; k < 4; k++) {
          v->pa[n][k] = A[k] * (f * R[k] < 0.45f * SR ? 1.0f : 0.0f);
          v->pm[n][k] = t60(T[k] * sc);
        }
      } break;
    }
  }
  if (I->type == 2) {
    v->cut = I->lp0;
    v->cmul = powf(I->lp1 / I->lp0, 16.0f / (I->lpt * (float)SR));
    bq_set(&v->bq, 0, v->cut, 0.8f);
  }
}

static float cv_proc(CVoice *v) {
  const CInst *I = &CI[v->inst];
  if (v->gateN > 0) {
    v->gateN--;
    if (v->stage == 0) { v->env += v->atkStep; if (v->env >= 1.0f) { v->env = 1.0f; v->stage = 1; } }
    else v->env = I->sus + (v->env - I->sus) * v->decM;
  } else {
    v->env *= v->relM;
    if (v->env < 0.0004f) { v->active = 0; return 0.0f; }
  }
  float x = 0.0f, dt = v->dt;
  switch (I->type) {
    case 0: {
      static const float PR[5] = {1.0f, 2.0005f, 3.0016f, 4.003f, 5.005f};
      for (int n = 0; n < 4; n++) for (int k = 0; k < 5; k++) {
        float *ph = &v->ph[n][k];
        *ph = wrap1(*ph + v->f[n] * PR[k] * dt);
        x += v->pa[n][k] * fsin(*ph);
        v->pa[n][k] *= v->pm[n][k];
      }
      x *= 0.30f;
    } break;
    case 1: {
      for (int n = 0; n < 4; n++) {
        float inc = v->f[n] * dt;
        v->ph[n][0] = wrap1(v->ph[n][0] + inc);
        v->ph[n][1] = wrap1(v->ph[n][1] + inc);
        v->ph[n][2] = wrap1(v->ph[n][2] + inc * 14.0f);
        float c = fsin(v->ph[n][0] + v->pa[n][1] * fsin(v->ph[n][1]));
        x += c * v->pa[n][0] + 0.15f * v->pa[n][2] * fsin(v->ph[n][2]);
        v->pa[n][0] *= v->pm[n][0]; v->pa[n][1] *= v->pm[n][1]; v->pa[n][2] *= v->pm[n][2];
      }
      x *= 0.45f;
    } break;
    case 2: {
      for (int n = 0; n < 4; n++) for (int j = 0; j < I->nsaw; j++) {
        float inc = v->inc[n][j], *ph = &v->ph[n][j];
        *ph = wrap1(*ph + inc);
        x += 2.0f * (*ph) - 1.0f - polyblep(*ph, inc);
      }
      x *= 1.0f / (float)I->nsaw * 0.5f;
      if ((v->n & 15) == 0 && v->cut != I->lp1) {
        v->cut *= v->cmul;
        if ((I->lp1 > I->lp0 && v->cut > I->lp1) || (I->lp1 < I->lp0 && v->cut < I->lp1)) v->cut = I->lp1;
        bq_set(&v->bq, 0, v->cut, 0.8f);
      }
      x = bq_run(&v->bq, x);
    } break;
    case 3: {
      static const float OA[4] = {1.0f, 0.7f, 0.45f, 0.3f};
      for (int n = 0; n < 4; n++) for (int k = 0; k < 4; k++) {
        float *ph = &v->ph[n][k];
        *ph = wrap1(*ph + v->f[n] * (float)(k + 1) * dt);
        x += OA[k] * fsin(*ph);
      }
      x *= 0.22f;
    } break;
    case 4: {
      static const float R[4] = {1.0f, 2.76f, 5.40f, 8.93f};
      for (int n = 0; n < 4; n++) for (int k = 0; k < 4; k++) {
        float *ph = &v->ph[n][k];
        *ph = wrap1(*ph + v->f[n] * R[k] * dt);
        x += v->pa[n][k] * fsin(*ph);
        v->pa[n][k] *= v->pm[n][k];
      }
      x *= 0.30f;
    } break;
  }
  v->n++;
  return x * v->env * I->amp * v->vel;
}
