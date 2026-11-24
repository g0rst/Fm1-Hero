/* SPDX-License-Identifier: GPL-3.0-only
 * Host-Tests der Tonfabrik: spielt Kapitel 1 ganz durch (mit Minispiel und drei Aufträgen), den Takt aus Kapitel 9,
 * das Speichern und den Livemodus, mit dem echten Kern. Schreibt Bildschirmfotos (PPM) und live.wav nach OUT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fab.h"

enum { SC_TITLE, SC_SLOTS, SC_MENU, SC_MAP, SC_WORK, SC_CALL, SC_RESULT, SC_GAME, SC_DIFF, SC_LIVE };
enum { P_HZ = 2 };
static const char *OUT;
static uint32_t now_ms, btns, keys;
static int32_t enc[FAB_NE];
static uint16_t screen[FAB_W * FAB_H];
static FILE *wav;
static uint32_t wav_frames;
static double peak, sumsq;
static long nsamp;
static int fails, shots;

static void audio(uint32_t ms)
{
    static int32_t lr[2 * 441];
    uint32_t i, n = ms * 441 / 10;
    while (n) {
        uint32_t k = n > 441 ? 441 : n;
        fab_render(lr, k, 4096);
        for (i = 0; i < 2 * k; i++) {
            double v = lr[i] / 8388608.0;
            if (fabs(v) > peak) peak = fabs(v);
            sumsq += v * v;
            nsamp++;
            if (wav) { int16_t s = (int16_t)(lr[i] >> 8); fwrite(&s, 2, 1, wav); }
        }
        if (wav) wav_frames += k;
        n -= k;
    }
}
static void frame(void)
{
    uint32_t d, s;
    fab_input(btns, keys, enc, now_ms);
    memset(enc, 0, sizeof enc);
    d = fab_dirty();
    for (s = 0; s < FAB_STRIPS; s++)
        if (d >> s & 1u) fab_draw(screen + s * FAB_W * FAB_STRIP_H, (int)s);
    audio(16);
    now_ms += 16;
}
static void wait_ms(uint32_t ms) { uint32_t end = now_ms + ms; while (now_ms < end) frame(); }
static void tap(int b) { btns |= 1u << b; frame(); frame(); btns &= ~(1u << b); frame(); frame(); }
static void key(int k, uint32_t ms) { keys |= 1u << k; wait_ms(ms); keys &= ~(1u << k); frame(); frame(); }
static void turn(int e, int n)
{
    int i, s = n > 0 ? 1 : -1;
    for (i = 0; i != n; i += s) { enc[e] = s; frame(); }
}
static void shot(const char *name)
{
    char p[512];
    FILE *f;
    int i;
    snprintf(p, sizeof p, "%s/%02d-%s.ppm", OUT, ++shots, name);
    f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", FAB_W, FAB_H);
    for (i = 0; i < FAB_W * FAB_H; i++) {
        uint16_t c = screen[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
        fwrite(rgb, 3, 1, f);
    }
    fclose(f);
}
#define EXPECT(what, val) do { int g_ = fab_state(what); if (g_ != (val)) { printf("FAIL %s:%d: %s = %d, erwartet %d\n", __FILE__, __LINE__, #what, g_, (int)(val)); fails++; } } while (0)
static void talk(void)                    /* SEL, bis keine Sprechblase mehr offen ist */
{
    int n = 0;
    wait_ms(200);
    while (fab_state(FAB_Q_DIALOG) && n++ < 60) { wait_ms(250); tap(FAB_B_SEL); }
    wait_ms(100);
}
/* ein Auftrag: abnehmen, zuhören, nachbauen (fn), liefern; gibt die Sterne */
static int job(void (*build)(void), const char *name)
{
    char nm[64];
    int st0 = fab_state(FAB_Q_STARS), st;
    if (fab_state(FAB_Q_SCREEN) != SC_CALL) { printf("FAIL: kein Anruf vor %s (Bildschirm %d)\n", name, fab_state(FAB_Q_SCREEN)); fails++; return 0; }
    wait_ms(400);
    tap(FAB_B_SEL);
    talk();
    EXPECT(FAB_Q_SCREEN, SC_WORK);
    tap(FAB_B_PLAY);
    wait_ms(700);
    build();
    wait_ms(500);
    snprintf(nm, sizeof nm, "%s-gebaut", name);
    shot(nm);
    tap(FAB_B_REC);
    wait_ms(1800);
    EXPECT(FAB_Q_SCREEN, SC_RESULT);
    snprintf(nm, sizeof nm, "%s-geliefert", name);
    shot(nm);
    talk();
    st = fab_state(FAB_Q_STARS) - st0;
    printf("  %-10s %d Sterne\n", name, st);
    tap(FAB_B_SEL);
    wait_ms(200);
    return st;
}
static void b_wecker(void) { turn(FAB_E_K1, 41); }
static void b_nebel(void) { turn(FAB_E_K1, 78); }
static void b_pfeife(void) { turn(FAB_E_K1, 120); }
static void steps(int track_black, const int *on, int n)
{
    int i;
    static const int WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
    static const int BLACK[6] = {1, 3, 5, 8, 10, 13};
    key(BLACK[track_black], 60);
    for (i = 0; i < n; i++) key(WHITE[on[i]], 60);
}
static void save_all(uint8_t store[3][FAB_OBJ_MAX], uint32_t len[3])
{
    uint32_t m = fab_save_wanted();
    int k;
    for (k = 0; k < 3; k++)
        if (m >> k & 1u) { len[k] = fab_store_get(k, store[k], FAB_OBJ_MAX); fab_store_saved(k); }
}

int main(int argc, char **argv)
{
    static uint8_t store[3][FAB_OBJ_MAX];
    static uint32_t store_len[3];
    int i, total = 0;
    OUT = argc > 1 ? argv[1] : ".";
    fab_init(1234);
    wait_ms(300);
    shot("titel");
    EXPECT(FAB_Q_SCREEN, SC_TITLE);
    tap(FAB_B_SEL);
    shot("plaetze");
    tap(FAB_B_SEL);                       /* neuer Platz: Kapitel 1 beginnt */
    EXPECT(FAB_Q_CHAPTER, 1);
    wait_ms(1200);
    shot("vogl");
    printf("Kapitel 1\n");
    talk();
    shot("klopfer");
    EXPECT(FAB_Q_PARAM + P_HZ, 0);
    turn(FAB_E_K1, 12);                   /* 1 Hz */
    wait_ms(1500);
    shot("klopf-1hz");
    talk();
    turn(FAB_E_K1, 38);                   /* 10 Hz */
    wait_ms(1000);
    shot("klopf-9hz");
    talk();
    turn(FAB_E_K1, 15);                   /* 25 Hz: ein Ton */
    talk();
    turn(FAB_E_K1, 48);                   /* 440 Hz */
    wait_ms(500);
    shot("hoher-ton");
    talk();
    turn(FAB_E_K1, -35);                  /* 55 Hz */
    talk();
    key(10, 300);
    talk();
    tap(FAB_B_OCTUP);
    talk();
    EXPECT(FAB_Q_SCREEN, SC_GAME);
    shot("minispiel");
    talk();
    for (i = 0; i < 5; i++) {
        wait_ms(1500);
        tap(fab_state(FAB_Q_GAME) ? FAB_B_OCTUP : FAB_B_OCTDN);
        wait_ms(1000);
    }
    EXPECT(FAB_Q_SCREEN, SC_RESULT);
    shot("minispiel-ergebnis");
    talk();
    tap(FAB_B_SEL);
    talk();
    total += job(b_wecker, "wecker");
    total += job(b_nebel, "nebelhorn");
    total += job(b_pfeife, "pfeife");
    talk();
    EXPECT(FAB_Q_SCREEN, SC_MAP);
    shot("karte");
    if (total != 9) { printf("FAIL: Kapitel 1 mit %d statt 9 Sternen\n", total); fails++; }
    save_all(store, store_len);
    if (!store_len[0]) { printf("FAIL: nichts gespeichert\n"); fails++; }
    printf("Pegel bis hier: Spitze %.3f, RMS %.4f\n", peak, sqrt(sumsq / (nsamp ? nsamp : 1)));
    if (peak > 1.0 || peak < 0.02) { printf("FAIL: Pegel\n"); fails++; }

    /* Kapitel 9: der Takt (Spielstand: 8 Kapitel geschafft) */
    printf("Kapitel 9\n");
    store[0][5] = 8;
    fab_init(5);
    if (fab_store_set(0, store[0], store_len[0]) != 0) { printf("FAIL: laden\n"); fails++; }
    tap(FAB_B_SEL); tap(FAB_B_SEL);       /* Titel, Platz */
    EXPECT(FAB_Q_SCREEN, SC_MENU);
    tap(FAB_B_SEL);                       /* Geschichte */
    EXPECT(FAB_Q_SCREEN, SC_MAP);
    shot("karte-9");
    tap(FAB_B_SEL);
    EXPECT(FAB_Q_CHAPTER, 9);
    talk();
    tap(FAB_B_SEQ);
    talk();
    tap(FAB_B_PLAY);
    { static const int K[4] = {0, 4, 8, 12}; steps(0, K, 4); }
    wait_ms(600);
    shot("takt-trommel");
    talk();
    { static const int K[2] = {4, 12}; steps(1, K, 2); }
    talk();
    EXPECT(FAB_Q_SCREEN, SC_CALL);
    tap(FAB_B_SEL); talk();
    /* Marsch: Trommel 1 und 9, Klatsch 5 und 13, Tempo 110 */
    { static const int K[2] = {4, 12}; steps(0, K, 2); }
    turn(FAB_E_K2, 6);                    /* Tempo: 2 BPM pro Raste */
    wait_ms(300);
    shot("marsch");
    tap(FAB_B_REC);
    wait_ms(1500);
    EXPECT(FAB_Q_SCREEN, SC_RESULT);
    talk(); tap(FAB_B_SEL); wait_ms(300);
    EXPECT(FAB_Q_SCREEN, SC_CALL);
    tap(FAB_B_SEL); talk();
    { static const int K[1] = {8}; steps(0, K, 1); }
    { static const int K[2] = {4, 12}; steps(1, K, 2); }
    turn(FAB_E_K2, -25);
    tap(FAB_B_REC);
    wait_ms(1500);
    EXPECT(FAB_Q_SCREEN, SC_RESULT);
    talk(); tap(FAB_B_SEL);
    talk();
    EXPECT(FAB_Q_SCREEN, SC_MAP);
    save_all(store, store_len);
    printf("  Kapitel geschafft: %d\n", store[0][5]);
    if (store[0][5] != 9) { printf("FAIL: Kapitel 9 nicht geschafft\n"); fails++; }

    /* Livemodus mit allen Kapiteln */
    printf("Live\n");
    {
        char p[512];
        store[0][5] = 11;
        fab_init(7);
        fab_store_set(0, store[0], store_len[0]);
        tap(FAB_B_SEL); tap(FAB_B_SEL);
        turn(FAB_E_SELECT, 3);
        shot("menue");
        tap(FAB_B_SEL);
        EXPECT(FAB_Q_SCREEN, SC_LIVE);
        snprintf(p, sizeof p, "%s/live.wav", OUT);
        wav = fopen(p, "wb");
        fseek(wav, 44, SEEK_SET);
        peak = 0;
        tap(FAB_B_PLAY);
        EXPECT(FAB_Q_PLAYING, 1);
        wait_ms(3000);
        shot("live");
        tap(FAB_B_SEQ);
        key(12, 400);
        shot("live-fabrik");
        tap(FAB_B_SEQ);
        wait_ms(3000);
        {
            uint32_t bytes = wav_frames * 4u, hdr[11];
            fseek(wav, 0, SEEK_SET);
            memcpy(&hdr[0], "RIFF", 4); hdr[1] = 36 + bytes; memcpy(&hdr[2], "WAVE", 4); memcpy(&hdr[3], "fmt ", 4);
            hdr[4] = 16; hdr[5] = 1 | 2 << 16; hdr[6] = 44100; hdr[7] = 44100 * 4; hdr[8] = 4 | 16 << 16;
            memcpy(&hdr[9], "data", 4); hdr[10] = bytes;
            fwrite(hdr, 4, 11, wav);
            fclose(wav);
            wav = 0;
        }
        printf("  Spitze %.3f\n", peak);
        if (peak < 0.05 || peak > 1.0) { printf("FAIL: Live-Pegel\n"); fails++; }
    }
    printf(fails ? "%d FEHLER\n" : "alle Tests bestanden\n", fails);
    return fails != 0;
}
