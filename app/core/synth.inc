/* SPDX-License-Identifier: GPL-3.0-only
 * Die Tonfabrik, Klang. Eine Stimme ist eine Produktionsstraße:
 *
 *   OSC 2 ──(FM)──> OSC 1 ──> MISCHER ──> VCF ──> VCA ──> KRATZ ──> Echo / Hall ──> Lautsprecher
 *                       ENV 1 -> VCA   ENV 2 -> Tonsprung, Filter, FM   LFO -> Ton, Filter oder Lautstärke
 *
 * Alle Einstellungen einer Stimme stehen als Bytes in einem Patch (P_*), 0..127 oder eine kleine Auswahl.
 * Teil von fab.c. */

/* ------------------------------------------------------------------ Patch --- */
enum {
    P_W1, P_OCT1, P_HZ, P_PITCH, P_PW,          /* OSC 1: Welle, Oktave, Hz des Klopfers, Tonsprung (64 = 0), Puls */
    P_W2, P_OCT2, P_DET, P_MIX2,                /* OSC 2: Welle, Oktave, Verstimmung (64 = 0), Menge */
    P_FM, P_RATIO, P_FMENV,                     /* FM: Stärke, Verhältnis (0 aus), Hüllkurve */
    P_CUT, P_RES, P_FENV, P_FTYPE,              /* VCF: Helligkeit, Resonanz, Hüllkurve (64 = 0), Typ */
    P_VOL, P_DRIVE,                             /* VCA: Lautstärke, Kratz */
    P_A1, P_D1, P_S1, P_R1,                     /* ENV 1 (Lautstärke) */
    P_A2, P_D2, P_S2, P_R2,                     /* ENV 2 (Tonsprung, Filter, FM) */
    P_LRATE, P_LDEP, P_LTGT, P_LWAVE,           /* LFO: Tempo, Stärke, Ziel, Form */
    P_ECHO, P_HALL, P_ETIME, P_EFB,             /* FX: Echo, Hall, Echo-Zeit, Wiederholungen */
    P_DRONE,                                    /* 1: der Klopfer läuft ohne Taste (Kapitel 1) */
    NP
};
enum { W_SIN, W_TRI, W_SAW, W_SQR, W_NOISE, W_CLICK, NW };
enum { F_LP, F_BP, F_HP };
enum { L_PITCH, L_CUT, L_VOL };
static const uint8_t P_MAX[NP] = {
    5, 4, 127, 127, 127,
    4, 4, 127, 127,
    127, 7, 127,
    127, 127, 127, 2,
    127, 127,
    127, 127, 127, 127,
    127, 127, 127, 127,
    127, 127, 2, 3,
    127, 127, 127, 127,
    1,
};
/* der Grundzustand am Werktisch: ein Sinus in der Mitte, offen, laut genug, kurzes Ausklingen */
static const uint8_t P_INIT[NP] = {
    W_SIN, 2, 70, 64, 0,
    W_SAW, 2, 64, 0,
    0, 0, 0,
    127, 0, 64, F_LP,
    100, 0,
    0, 64, 127, 24,
    0, 60, 0, 30,
    60, 0, L_PITCH, 0,
    0, 0, 60, 50,
    0,
};
typedef uint8_t patch_t[NP];

/* ------------------------------------------------------------------ Rechnen --- */
#define PI_F 3.14159265f
#define SIN_N 512
static float SIN_T[SIN_N + 1];

static float ex2(float x)                       /* 2^x, auf etwa 1e-4 */
{
    union { float f; uint32_t u; } v;
    int i;
    float f, p;
    if (x < -60.0f) x = -60.0f;
    if (x > 60.0f) x = 60.0f;
    i = (int)(x + 128.0f) - 128;
    f = x - (float)i;
    p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    v.f = p;
    v.u += (uint32_t)(i << 23);
    return v.f;
}
static float sin1(float t)                      /* sin(2 pi t) */
{
    float x;
    int i;
    t -= (float)(int)t;
    if (t < 0.0f) t += 1.0f;
    x = t * SIN_N;
    i = (int)x;
    return SIN_T[i] + (SIN_T[i + 1] - SIN_T[i]) * (x - (float)i);
}
static float fabsf_(float x) { return x < 0.0f ? -x : x; }
static float sat(float x)                       /* weich begrenzen, wie tanh */
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
static uint32_t rnd_state = 0x1234567u;
static uint32_t rnd(void)
{
    uint32_t x = rnd_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rnd_state = x;
}
static float noise1(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/* Werte aus Patch-Bytes */
static float env_time(int p) { return 0.0005f * ex2((float)p * (14.0f / 127.0f)); }   /* 0,5 ms .. 8 s */
static float klopf_hz(int p) { return 0.5f * ex2((float)p * (11.0f / 127.0f)); }       /* 0,5 .. 1024 Hz */
static float lfo_hz(int p) { return 0.1f * ex2((float)p * (7.7f / 127.0f)); }          /* 0,1 .. 20 Hz */
static float cut_hz(float semis) { return 30.0f * ex2(semis * (1.0f / 12.0f)); }
static const float RATIO[8] = {0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f};
static float note_hz(float n) { return 440.0f * ex2((n - 69.0f) * (1.0f / 12.0f)); }

/* ------------------------------------------------------------------ Stimme --- */
enum { E_IDLE, E_A, E_D, E_S, E_R };
typedef struct {
    const uint8_t *p;
    float ph1, ph2, lph, lval, lsh;
    float e1, e2;
    uint8_t s1, s2, gate, drone;
    float note, vel;
    float ic1, ic2, dc_x, dc_y;
    /* je 16 Samples neu */
    float inc1, inc2, pw, fmi, g, k, amp, a1i, a2i, d1c, d2c, r1c, r2c, s1l, s2l;
    uint32_t ns;
    uint32_t cnt;
} voice_t;

static float blep(float t, float dt)            /* PolyBLEP: rundet die Sprünge von Säge und Rechteck */
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static float wave(int w, float t, float dt, float pw, uint32_t *ns)
{
    float v, t2;
    switch (w) {
    case W_SIN: return sin1(t);
    case W_TRI: return t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
    case W_SAW: return 2.0f * t - 1.0f - blep(t, dt);
    case W_SQR:
        v = t < pw ? 1.0f : -1.0f;
        t2 = t - pw + 1.0f;
        t2 -= (float)(int)t2;
        return v + blep(t, dt) - blep(t2, dt);
    case W_NOISE: return noise1(ns);
    default:                                    /* KLICK: ein kurzer Stromstoß pro Schwingung */
        v = t < pw ? 1.0f : 0.0f;
        t2 = t - pw + 1.0f;
        t2 -= (float)(int)t2;
        return v + 0.5f * (blep(t, dt) - blep(t2, dt));
    }
}

static void v_control(voice_t *v)
{
    const uint8_t *p = v->p;
    float lv, semis, f1, f2, sc, fc, x, dep;
    int w;
    /* LFO */
    v->lph += lfo_hz(p[P_LRATE]) * (16.0f / FAB_SR);
    if (v->lph >= 1.0f) {
        v->lph -= 1.0f;
        v->lsh = noise1(&v->ns);
    }
    switch (p[P_LWAVE]) {
    case 0: lv = sin1(v->lph); break;
    case 1: lv = v->lph < 0.5f ? 1.0f : -1.0f; break;
    case 2: lv = 1.0f - 2.0f * v->lph; break;
    default: lv = v->lsh; break;
    }
    v->lval = lv;
    dep = (float)p[P_LDEP] * (1.0f / 127.0f);
    /* Tonhöhe */
    semis = v->note + (float)((int)p[P_OCT1] - 2) * 12.0f + (float)((int)p[P_PITCH] - 64) * (48.0f / 64.0f) * v->e2;
    if (p[P_LTGT] == L_PITCH)
        semis += lv * dep * dep * 12.0f;
    if (p[P_DRONE])
        f1 = !p[P_HZ] ? 0.0f : klopf_hz(p[P_HZ]) * ex2((semis - v->note - (float)((int)p[P_OCT1] - 2) * 12.0f) * (1.0f / 12.0f));
    else
        f1 = note_hz(semis);
    if (p[P_FM] && p[P_RATIO])
        f2 = f1 * RATIO[p[P_RATIO]];
    else {
        float d = (float)((int)p[P_DET] - 64) * (1.0f / 64.0f);
        f2 = f1 * ex2((float)((int)p[P_OCT2] - (int)p[P_OCT1]) + d * fabsf_(d) * (1.0f / 12.0f));
    }
    if (f1 > 18000.0f) f1 = 18000.0f;
    if (f2 > 18000.0f) f2 = 18000.0f;
    v->inc1 = f1 * (1.0f / FAB_SR);
    v->inc2 = f2 * (1.0f / FAB_SR);
    w = p[P_W1];
    if (w == W_CLICK) {
        v->pw = 0.0008f * f1;                   /* 0,8 ms Stoß, bis er die halbe Schwingung füllt */
        if (v->pw > 0.5f) v->pw = 0.5f;
    } else
        v->pw = 0.5f - (float)p[P_PW] * (0.45f / 127.0f);
    v->fmi = ((float)p[P_FM] * (3.0f / 127.0f) + (float)p[P_FMENV] * (5.0f / 127.0f) * v->e2) * 0.159f;
    /* Filter */
    sc = (float)p[P_CUT] * (110.0f / 127.0f) + (float)((int)p[P_FENV] - 64) * (84.0f / 64.0f) * v->e2;
    if (p[P_LTGT] == L_CUT)
        sc += lv * dep * 48.0f;
    fc = cut_hz(sc);
    if (fc < 20.0f) fc = 20.0f;
    if (fc > 18000.0f) fc = 18000.0f;
    x = fc * (1.0f / FAB_SR);                   /* tan(pi x) */
    v->g = sin1(x * 0.5f) / sin1(x * 0.5f + 0.25f);
    v->k = 2.0f - 1.94f * (float)p[P_RES] * (1.0f / 127.0f);
    /* Lautstärke */
    x = (float)p[P_VOL] * (1.0f / 127.0f);
    v->amp = x * x * v->vel;
    if (p[P_LTGT] == L_VOL)
        v->amp *= 1.0f - dep * (0.5f - 0.5f * lv);
    /* Hüllkurven */
    v->a1i = 1.0f / (env_time(p[P_A1]) * FAB_SR);
    v->d1c = ex2(-6.64f / (env_time(p[P_D1]) * FAB_SR));
    v->r1c = ex2(-6.64f / (env_time(p[P_R1]) * FAB_SR));
    v->s1l = (float)p[P_S1] * (1.0f / 127.0f);
    v->a2i = 1.0f / (env_time(p[P_A2]) * FAB_SR);
    v->d2c = ex2(-6.64f / (env_time(p[P_D2]) * FAB_SR));
    v->r2c = ex2(-6.64f / (env_time(p[P_R2]) * FAB_SR));
    v->s2l = (float)p[P_S2] * (1.0f / 127.0f);
}

static float env_step(float *e, uint8_t *s, float ai, float dc, float sl, float rc)
{
    switch (*s) {
    case E_A:
        *e += ai;
        if (*e >= 1.0f) { *e = 1.0f; *s = E_D; }
        break;
    case E_D:
        *e = sl + (*e - sl) * dc;
        if (*e - sl < 0.0005f) { *e = sl; *s = sl < 0.0005f ? E_IDLE : E_S; }
        break;
    case E_S:
        *e = sl;
        break;
    case E_R:
        *e *= rc;
        if (*e < 0.0002f) { *e = 0.0f; *s = E_IDLE; }
        break;
    default:
        *e = 0.0f;
    }
    return *e;
}

static void v_on(voice_t *v, float note, float vel)
{
    v->note = note;
    v->vel = vel;
    v->gate = 1;
    v->s1 = E_A;
    v->s2 = E_A;
    if (v->p[P_W1] == W_CLICK || v->p[P_DRONE])
        v->ph1 = 0.0f;
    v->cnt = 0;
}
static void v_off(voice_t *v)
{
    v->gate = 0;
    if (v->s1 != E_IDLE) v->s1 = E_R;
    if (v->s2 != E_IDLE) v->s2 = E_R;
}

/* ein Sample; 0 wenn still */
static float v_tick(voice_t *v)
{
    const uint8_t *p = v->p;
    float o1, o2, x, e1, y, hp, v1, v2, a1, a2, a3;
    int w1;
    if ((v->cnt++ & 15u) == 0u) {
        v_control(v);
        if (!v->gate) {                         /* losgelassen: auch eine Hüllkurve, die noch hält, klingt aus */
            if (v->s1 == E_S || v->s1 == E_D) v->s1 = E_R;
            if (v->s2 == E_S || v->s2 == E_D) v->s2 = E_R;
        }
    }
    e1 = env_step(&v->e1, &v->s1, v->a1i, v->d1c, v->s1l, v->r1c);
    env_step(&v->e2, &v->s2, v->a2i, v->d2c, v->s2l, v->r2c);
    v->ph2 += v->inc2;
    if (v->ph2 >= 1.0f) v->ph2 -= 1.0f;
    o2 = wave(p[P_W2] > W_NOISE ? W_SAW : p[P_W2], v->ph2, v->inc2, 0.5f, &v->ns);
    v->ph1 += v->inc1;
    if (v->ph1 >= 1.0f) v->ph1 -= 1.0f;
    w1 = p[P_W1];
    if (v->fmi > 0.0f) {
        float t = v->ph1 + v->fmi * o2;
        t -= (float)(int)t;
        if (t < 0.0f) t += 1.0f;
        o1 = w1 == W_SIN ? sin1(t) : wave(w1, t, v->inc1, v->pw, &v->ns);
        x = o1 + o2 * (float)p[P_MIX2] * (1.0f / 127.0f) * (p[P_RATIO] ? 0.0f : 1.0f);
    } else {
        o1 = wave(w1, v->ph1, v->inc1, v->pw, &v->ns);
        x = o1 + o2 * (float)p[P_MIX2] * (1.0f / 127.0f);
    }
    if (w1 == W_CLICK) {                        /* der Stoß ohne Gleichanteil */
        v->dc_y = x - v->dc_x + 0.9995f * v->dc_y;
        v->dc_x = x;
        x = v->dc_y * 1.4f;
    }
    /* VCF: Zustandsvariablen-Filter (TPT) */
    a1 = 1.0f / (1.0f + v->g * (v->g + v->k));
    a2 = v->g * a1;
    a3 = v->g * a2;
    {
        float v3 = x - v->ic2;
        v1 = a1 * v->ic1 + a2 * v3;
        v2 = v->ic2 + a2 * v->ic1 + a3 * v3;
        v->ic1 = 2.0f * v1 - v->ic1;
        v->ic2 = 2.0f * v2 - v->ic2;
    }
    switch (p[P_FTYPE]) {
    case F_BP: y = v1 * v->k; break;
    case F_HP: hp = x - v->k * v1 - v2; y = hp; break;
    default: y = v2; break;
    }
    /* VCA und Kratz */
    y *= e1 * v->amp;
    if (p[P_DRIVE]) {
        float d = (float)p[P_DRIVE] * (1.0f / 127.0f);
        y = sat(y * (1.0f + d * 12.0f)) * (0.7f - d * 0.25f) + y * (1.0f - d) * 0.3f;
    }
    return y * 0.5f;
}

/* ------------------------------------------------------------------ Spuren und Lied --- */
#define NTR 6                                   /* TROMMEL, KLATSCH, ZISCH, BASS und zwei freie */
enum { V_BENCH = NTR, V_TARGET, V_SFX, NV };
typedef struct {
    uint16_t on[NTR];                           /* Bit i: Schritt i spielt */
    int8_t note[NTR][16];                       /* Halbtöne um den Grundton der Spur */
    uint8_t mute, bpm, swing, len;
    uint8_t level[NTR];
    uint8_t sound[2];                           /* Spuren 5 und 6: Nummer im Tonlager */
    patch_t patch[NTR];
} song_t;

static voice_t V[NV];
static patch_t sfx_patch, tgt_patch;
static song_t *S_song;                          /* das Lied, das der Sequencer spielt */
static volatile uint8_t seq_play, seq_step = 255;
static volatile uint32_t seq_bar;
static uint32_t seq_acc, seq_gate_left[NTR];
static uint8_t seq_fx_from = V_BENCH;           /* Echo-Zeit und Wiederholungen aus diesem Patch */
static volatile uint8_t trig_flash;             /* Bit t: Spur t hat gerade gespielt (für die LEDs) */

/* Echo und Hall */
#if defined FAB_FM1_POOL && !defined PLAT_HOST
#define POOL __attribute__((section(".pool")))
#else
#define POOL
#endif
#define DLY_N 32768
static int16_t dly_buf[DLY_N] POOL;
static uint32_t dly_w;
static const uint16_t COMB_N[4] = {1116, 1188, 1277, 1356};
static const uint16_t AP_N[2] = {556, 441};
static int16_t comb_buf[4][1356] POOL;
static int16_t ap_buf[2][556] POOL;
static uint16_t comb_i[4], ap_i[2];
static float comb_lp[4], dly_lp, dly_time_s = 0.3f, dly_time_t = 0.3f, master_dc_x, master_dc_y, master_dc_xr, master_dc_yr;

/* das Oszi: was die gewählte Stimme gerade macht */
#define SCOPE_N 4096                    /* 93 ms: zwei Schwingungen auch beim tiefsten Ton (22 Hz) */
static float scope_buf[SCOPE_N];
static volatile uint32_t scope_w;
static volatile uint8_t scope_v = V_BENCH;
/* das Zeitbild: 120 Abschnitte zu 10 ms, ab dem Anschlag (der Klopfer: fortlaufend) */
/* Zeitbild: die Lautstärke in Abschnitten (10 ms, beim Wackler und Echo 20 ms), ab dem Anschlag und weiter, solange
 * der Ton klingt, auch nach dem Loslassen (dann sieht man das Tor zugehen); die Anzeige rollt mit */
#define ZEIT_N 120
#define ZEIT_R 256
static volatile uint8_t zeit_ring[ZEIT_R];
static volatile uint32_t zeit_count;
static volatile uint16_t zeit_bin = 441;
static uint32_t zeit_cnt, zeit_tail;
static float zeit_peak;
static volatile uint8_t zeit_restart;

/* Ereignisse von der Hauptschleife zum Audio: Stimme, Note, Stärke (0: aus), Art */
#define EVQ 64
static volatile uint32_t evq[EVQ];
static volatile uint32_t evq_w, evq_r;
enum { EV_ON, EV_OFF, EV_ALLOFF, EV_SFX };
static void ev_push(uint32_t kind, uint32_t voice, uint32_t note, uint32_t vel)
{
    uint32_t w = evq_w;
    if (w - evq_r >= EVQ) return;
    evq[w & (EVQ - 1)] = kind | voice << 4 | note << 8 | vel << 16;
    evq_w = w + 1;
}
static void syn_on(int v, int note, int vel) { ev_push(EV_ON, (uint32_t)v, (uint32_t)note & 127u, (uint32_t)vel & 127u); }
static void syn_off(int v) { ev_push(EV_OFF, (uint32_t)v, 0, 0); }
static void syn_alloff(void) { ev_push(EV_ALLOFF, 0, 0, 0); }

/* Geräusche der Oberfläche: Klopfen des Spechts, Piep, Sterne, Telefon */
enum { SFX_TOK, SFX_BLIP, SFX_BLOP, SFX_STAR, SFX_RING, SFX_BAD, SFX_NOTE };
static void sfx(int kind, int note) { ev_push(EV_SFX, (uint32_t)kind, (uint32_t)note & 127u, 100); }
static void sfx_set(int kind)
{
    uint8_t *p = sfx_patch;
    int i;
    for (i = 0; i < NP; i++) p[i] = P_INIT[i];
    p[P_S1] = 0;
    switch (kind) {
    case SFX_TOK:                               /* Holz: kurzer Sinus, der fällt, und ein Hauch Rauschen */
        p[P_W1] = W_TRI; p[P_PITCH] = 82; p[P_A2] = 0; p[P_D2] = 14; p[P_S2] = 0;
        p[P_W2] = W_NOISE; p[P_MIX2] = 50; p[P_CUT] = 100; p[P_D1] = 22; p[P_R1] = 20; p[P_VOL] = 92; break;
    case SFX_BLIP: p[P_W1] = W_SQR; p[P_D1] = 30; p[P_R1] = 20; p[P_VOL] = 60; p[P_CUT] = 90; break;
    case SFX_BLOP: p[P_W1] = W_SIN; p[P_PITCH] = 52; p[P_D2] = 40; p[P_S2] = 0; p[P_D1] = 40; p[P_R1] = 30; p[P_VOL] = 80; break;
    case SFX_STAR: p[P_W1] = W_SQR; p[P_PW] = 80; p[P_D1] = 66; p[P_R1] = 60; p[P_VOL] = 70; p[P_CUT] = 100;
                   p[P_ECHO] = 50; break;
    case SFX_RING: p[P_W1] = W_SQR; p[P_S1] = 127; p[P_D1] = 80; p[P_R1] = 10; p[P_VOL] = 52; p[P_CUT] = 80;
                   p[P_LTGT] = L_VOL; p[P_LDEP] = 127; p[P_LWAVE] = 1; p[P_LRATE] = 104; break;
    case SFX_BAD: p[P_W1] = W_SAW; p[P_PITCH] = 40; p[P_D2] = 70; p[P_S2] = 0; p[P_D1] = 70; p[P_R1] = 30;
                  p[P_VOL] = 60; p[P_CUT] = 60; break;
    default: p[P_W1] = W_TRI; p[P_D1] = 70; p[P_R1] = 50; p[P_VOL] = 80; break;
    }
}

static void voice_reset(voice_t *v, const uint8_t *p)
{
    uint32_t ns = v->ns ? v->ns : 0x9E3779B9u;
    *v = (voice_t){0};
    v->p = p;
    v->ns = ns;
    v_control(v);
}

static void syn_init(void)
{
    int i;
    for (i = 0; i <= SIN_N; i++) {              /* Sinus als Reihe, nur float (die Toolchain hat kein double) */
        float x = (float)i * (6.2831853f / (float)SIN_N), s, t;
        int k;
        if (x > 3.1415927f) x -= 6.2831853f;
        t = x;
        s = 0.0f;
        for (k = 1; k < 24; k += 2) {
            s += t;
            t *= -x * x / (float)((k + 1) * (k + 2));
        }
        SIN_T[i] = s;
    }
    for (i = 0; i < NV; i++) {
        V[i].ns = 0x9E3779B9u * (uint32_t)(i + 1);
        voice_reset(&V[i], P_INIT);
    }
    sfx_set(SFX_TOK);
    V[V_SFX].p = sfx_patch;
    V[V_TARGET].p = tgt_patch;
}

/* der Sequencer: im Audio-Kontext, auf das Sample genau */
static float step_len(int step)
{
    float base = (float)FAB_SR * 60.0f / (float)(S_song->bpm ? S_song->bpm : 100) / 4.0f;
    float sw = (float)S_song->swing * (1.0f / 100.0f);   /* 50..75 % */
    if (sw < 0.5f) sw = 0.5f;
    return (step & 1) ? base * 2.0f * (1.0f - sw) : base * 2.0f * sw;
}
static void seq_tick_step(void)
{
    int t, s;
    seq_step = (uint8_t)((seq_step + 1u) % (S_song->len ? S_song->len : 16u));
    s = seq_step;
    if (s == 0) seq_bar++;
    for (t = 0; t < NTR; t++)
        if (S_song->on[t] >> s & 1u && !(S_song->mute >> t & 1u)) {
            v_on(&V[t], 60.0f + (float)S_song->note[t][s], (float)S_song->level[t] * (1.0f / 127.0f));
            seq_gate_left[t] = (uint32_t)(step_len(s) * 0.6f);
            trig_flash |= (uint8_t)(1u << t);
        }
}

static void events(void)
{
    while (evq_r != evq_w) {
        uint32_t e = evq[evq_r & (EVQ - 1)], kind = e & 15u, v = e >> 4 & 15u, note = e >> 8 & 127u, vel = e >> 16 & 127u;
        evq_r++;
        if (kind == EV_ON && v < NV) {
            v_on(&V[v], (float)note, (float)vel * (1.0f / 127.0f));
            if (v == scope_v) zeit_restart = 1;
        } else if (kind == EV_OFF && v < NV)
            v_off(&V[v]);
        else if (kind == EV_ALLOFF) {
            int i;
            for (i = 0; i < NV; i++) {
                V[i].gate = 0; V[i].s1 = V[i].s2 = E_IDLE; V[i].e1 = V[i].e2 = 0.0f; V[i].drone = 0;
            }
        } else if (kind == EV_SFX) {
            sfx_set((int)v);
            v_on(&V[V_SFX], (float)note, (float)vel * (1.0f / 127.0f));
        }
    }
}

static void fab_render_core(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    uint32_t i;
    int k;
    float gain = (float)gain_q12 * (1.0f / 4096.0f) * 8388607.0f;
    const uint8_t *fxp;
    events();
    /* der Klopfer: der Werktisch läuft ohne Taste, solange sein Patch es will */
    {
        voice_t *v = &V[V_BENCH];
        if (v->p[P_DRONE] && !v->drone) { v->drone = 1; v_on(v, 60.0f, 1.0f); zeit_restart = 1; }
        if (!v->p[P_DRONE] && v->drone) { v->drone = 0; v_off(v); }
    }
    fxp = seq_fx_from < NV ? V[seq_fx_from].p : P_INIT;
    dly_time_t = 0.05f + (float)fxp[P_ETIME] * (0.69f / 127.0f);
    for (i = 0; i < n; i++) {
        float l = 0.0f, r = 0.0f, es = 0.0f, hs = 0.0f, x, d, h;
        int c;
        if (seq_play && S_song) {
            if (seq_acc == 0) {
                seq_tick_step();
                seq_acc = (uint32_t)step_len(seq_step);
            }
            seq_acc--;
            for (k = 0; k < NTR; k++)
                if (seq_gate_left[k] && --seq_gate_left[k] == 0)
                    v_off(&V[k]);
        }
        for (k = 0; k < NV; k++) {
            voice_t *v = &V[k];
            float y, pan;
            if (v->s1 == E_IDLE)
                continue;
            y = v_tick(v);
            if (k == scope_v) {
                scope_buf[scope_w & (SCOPE_N - 1)] = y;
                scope_w = scope_w + 1;
            }
            pan = k == 2 ? 0.35f : k == 4 ? -0.3f : k == 5 ? 0.3f : 0.0f;   /* Zisch und die freien Spuren etwas zur Seite */
            l += y * (1.0f - pan);
            r += y * (1.0f + pan);
            es += y * (float)v->p[P_ECHO] * (1.0f / 127.0f);
            hs += y * (float)v->p[P_HALL] * (1.0f / 127.0f);
        }
        /* Zeitbild */
        {
            float y = scope_buf[(scope_w - 1u) & (SCOPE_N - 1)];
            if (V[scope_v].s1 == E_IDLE) y = 0.0f;
            if (zeit_restart) {
                zeit_restart = 0;
                zeit_count = 0; zeit_cnt = 0; zeit_peak = 0.0f; zeit_tail = 30;
            }
            if (fabsf_(y) > zeit_peak) zeit_peak = fabsf_(y);
            if (++zeit_cnt >= zeit_bin) {
                float pk = zeit_peak * 300.0f;
                zeit_cnt = 0;
                if (pk > 127.0f) pk = 127.0f;
                if (V[scope_v].s1 != E_IDLE) zeit_tail = 30;   /* nach dem Verklingen noch 30 Abschnitte Stille */
                if (zeit_tail) {
                    zeit_tail--;
                    zeit_ring[zeit_count & (ZEIT_R - 1)] = (uint8_t)pk;
                    zeit_count = zeit_count + 1;
                }
                zeit_peak = 0.0f;
            }
        }
        /* Echo */
        dly_time_s += (dly_time_t - dly_time_s) * 0.0002f;
        {
            float pos = (float)dly_w - dly_time_s * FAB_SR;
            int ip;
            float fr;
            while (pos < 0.0f) pos += DLY_N;
            ip = (int)pos;
            fr = pos - (float)ip;
            d = ((float)dly_buf[ip & (DLY_N - 1)] * (1.0f - fr) + (float)dly_buf[(ip + 1) & (DLY_N - 1)] * fr) * (1.0f / 16384.0f);
            dly_lp += (d - dly_lp) * 0.45f;
            x = es + dly_lp * (float)fxp[P_EFB] * (0.92f / 127.0f);
            if (x > 1.9f) x = 1.9f;
            if (x < -1.9f) x = -1.9f;
            dly_buf[dly_w & (DLY_N - 1)] = (int16_t)(x * 16384.0f);
            dly_w++;
        }
        /* Hall: vier Kammfilter und zwei Allpässe (Schroeder) */
        h = 0.0f;
        for (c = 0; c < 4; c++) {
            int16_t *b = comb_buf[c];
            float o = (float)b[comb_i[c]] * (1.0f / 8192.0f);
            comb_lp[c] += (o - comb_lp[c]) * 0.5f;
            x = hs * 0.25f + comb_lp[c] * 0.84f;
            if (x > 3.9f) x = 3.9f;
            if (x < -3.9f) x = -3.9f;
            b[comb_i[c]] = (int16_t)(x * 8192.0f);
            if (++comb_i[c] >= COMB_N[c]) comb_i[c] = 0;
            h += o;
        }
        for (c = 0; c < 2; c++) {
            int16_t *b = ap_buf[c];
            float o = (float)b[ap_i[c]] * (1.0f / 8192.0f);
            x = h + o * 0.5f;
            if (x > 3.9f) x = 3.9f;
            if (x < -3.9f) x = -3.9f;
            b[ap_i[c]] = (int16_t)(x * 8192.0f);
            h = o - x * 0.5f;
            if (++ap_i[c] >= AP_N[c]) ap_i[c] = 0;
        }
        l += d * 0.8f + h * 0.35f;
        r += d * 0.8f - h * 0.35f;
        /* Summe: ohne Gleichanteil, weich begrenzt */
        master_dc_y = l - master_dc_x + 0.9993f * master_dc_y;
        master_dc_x = l;
        master_dc_yr = r - master_dc_xr + 0.9993f * master_dc_yr;
        master_dc_xr = r;
        l = sat(master_dc_y * 0.8f) * 0.9f;
        r = sat(master_dc_yr * 0.8f) * 0.9f;
        lr[2 * i] = (int32_t)(l * gain);
        lr[2 * i + 1] = (int32_t)(r * gain);
    }
}

/* Ein Klang einmal ohne Lautsprecher: sein Oszibild (eine Schwingung bei der Note, 120 Punkte) und sein Zeitbild
 * (Note 0,6 s gehalten, dann losgelassen; 120 Abschnitte zu 10 ms). In der Hauptschleife, einmal pro Auftrag. */
static void syn_offline(const uint8_t *p, int note, int8_t *wave_out, int8_t *zeit_out, uint32_t bin, int periods)
{
    static voice_t tv;
    uint32_t i, hold = 60u * bin, total = ZEIT_N * bin;   /* die halbe Zeit gehalten, dann losgelassen */
    float pk = 0.0f;
    static float buf[4096];
    uint32_t bw = 0;
    tv.ns = 0x51ED27u;
    voice_reset(&tv, p);
    v_on(&tv, (float)note, 1.0f);
    for (i = 0; i < total; i++) {
        float y;
        if (i == hold && !p[P_DRONE]) v_off(&tv);
        y = v_tick(&tv);
        if (fabsf_(y) > pk) pk = fabsf_(y);
        if (i % bin == bin - 1) {
            float q = pk * 300.0f;
            zeit_out[i / bin] = (int8_t)(q > 127.0f ? 127.0f : q);
            pk = 0.0f;
        }
        if (i >= 4410u && bw < 4096u)           /* nach 100 ms, wenn sich der Anschlag gelegt hat */
            buf[bw++] = y;
    }
    /* eine Schwingung aus dem Puffer: ab einem Nulldurchgang nach oben */
    {
        float f = tv.inc1 > 0.0f ? tv.inc1 : 0.01f, per = 1.0f / f, span;
        uint32_t s0 = 0, j;
        span = per * (float)periods;            /* ganze Schwingungen, so viele wie gezoomt */
        if (span > 3900.0f) span = 3900.0f;
        if (span < 30.0f) span = 30.0f;
        for (j = 1; j + (uint32_t)span < 4096u; j++)
            if (buf[j - 1] <= 0.0f && buf[j] > 0.0f) { s0 = j; break; }
        for (j = 0; j < 120; j++) {
            uint32_t idx = s0 + (uint32_t)((float)j * span / 120.0f);
            float y = idx < 4096 ? buf[idx] * 300.0f : 0.0f;
            wave_out[j] = (int8_t)(y > 127.0f ? 127 : y < -127.0f ? -127 : y);
        }
    }
}
