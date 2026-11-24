/* SPDX-License-Identifier: GPL-3.0-only
 * Weißkasten-Test der Tonfabrik: bindet den Kern ein und prüft die Inhalte.
 *   1. Texte passen (Wörter, Zeilen, Namen), Regler gibt es im Kapitel schon, Werte sind erreichbar.
 *   2. Jeder Klang-Auftrag ist lösbar (genau nachgebaut: 3 Sterne) und nicht schon gelöst (Anfang: < 3).
 *   3. Ein Spieler-Roboter spielt alle 11 Kapitel durch: Sprechblasen weg, Aufgaben erfüllen, Aufträge
 *      nachbauen, Minispiele lösen. Am Ende: alle Kapitel geschafft. Bildschirmfotos pro Kapitel nach OUT. */
#include <stdio.h>
#include <string.h>
#include "../app/core/fab.c"

static int fails;
static uint32_t T, B, K;
static int32_t E[FAB_NE];
static uint16_t scr[FAB_W * FAB_H];
static void step(void)
{
    static int32_t lr[2 * 706];
    uint32_t d, s;
    fab_input(B, K, E, T);
    memset(E, 0, sizeof E);
    d = fab_dirty();
    for (s = 0; s < FAB_STRIPS; s++) if (d >> s & 1u) fab_draw(scr + s * FAB_W * FAB_STRIP_H, (int)s);
    fab_render(lr, 706, 4096);
    T += 16;
}
static void wt(uint32_t ms) { uint32_t e = T + ms; while (T < e) step(); }
static void tapb(int b) { B |= 1u << b; step(); step(); B &= ~(1u << b); step(); step(); }
static void tapk(int k) { K |= 1u << k; step(); step(); step(); K &= ~(1u << k); step(); step(); }
static void shot(const char *dir, const char *name)
{
    char p[512];
    FILE *f;
    int i;
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", FAB_W, FAB_H);
    for (i = 0; i < FAB_W * FAB_H; i++) {
        uint16_t c = scr[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
        fwrite(rgb, 3, 1, f);
    }
    fclose(f);
}
#define FAIL(...) do { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } while (0)

static void check_text(const char *where, const char *t, int maxw)
{
    const char *s = t;
    int w = 0;
    while (1) {
        int g = next_glyph(&s);
        if (g == 0 || g < 0) { if (w > maxw) FAIL("%s: Wort zu lang (%d) in \"%s\"", where, w, t); w = 0; if (g < 0) break; }
        else w++;
        if (g == '?' - 0x20 && *t && strchr(t, '?') == 0) FAIL("%s: unbekanntes Zeichen in \"%s\"", where, t);
    }
}
static void check_content(void)
{
    int c, j;
    for (c = 0; c < NCH; c++) {
        const op_t *o;
        if (text_len(CH_NAME[c]) > 19) FAIL("Kapitelname zu lang: %s", CH_NAME[c]);
        for (o = CHAPTERS[c]; o->op != O_END; o++) {
            if (o->t) check_text("Text", o->t, LW);
            if ((o->op == O_WAIT || o->op == O_WAITKEY || o->op == O_WAITBTN || o->op == O_WAITM || o->op == O_SEQGOAL)
                && text_len(o->t) > 20) FAIL("Kapitel %d: Anweisung zu lang: %s", c + 1, o->t);
            if (o->op == O_WAIT) {
                if (PARAM_CH[o->a] > c + 1) FAIL("Kapitel %d wartet auf Regler %d aus Kapitel %d", c + 1, o->a, PARAM_CH[o->a]);
                if ((o->b == CMP_GE || o->b == CMP_EQ || o->b == CMP_NEAR) && o->c > P_MAX[o->a]) FAIL("Kapitel %d: Wert %d unerreichbar", c + 1, o->c);
                if (o->b == CMP_GE || o->b == CMP_LE || o->b == CMP_FAR) FAIL("Kapitel %d: ungenaues Ziel \"%s\"", c + 1, o->t);
            }
            if (o->op == O_JOB && JOBS[o->a].ch != c + 1) FAIL("Auftrag %d steht in Kapitel %d, gehört zu %d", o->a, c + 1, JOBS[o->a].ch);
        }
    }
    for (j = 0; j < NJOBS; j++) {
        const job_t *jb = &JOBS[j];
        const uint8_t *s;
        check_text(jb->who, jb->call, LW);
        check_text(jb->who, jb->thanks, LW);
        if (text_len(jb->who) > 19) FAIL("Name zu lang: %s", jb->who);
        if (text_len(jb->name) > 9) FAIL("Klangname zu lang: %s", jb->name);
        if (jb->kind != JK_SOUND) continue;
        for (s = jb->set; s[0] != END_; s += 2) {
            if (s[0] >= NP) FAIL("%s: Parameter %d", jb->who, s[0]);
            else {
                if (s[1] > P_MAX[s[0]]) FAIL("%s: Wert %d > %d bei Parameter %d", jb->who, s[1], P_MAX[s[0]], s[0]);
                if (PARAM_CH[s[0]] > jb->ch && s[0] != P_DRONE && !(s[0] == P_W1 && s[1] == W_CLICK))
                    FAIL("%s (Kapitel %d) braucht Regler %d aus Kapitel %d", jb->who, jb->ch, s[0], PARAM_CH[s[0]]);
            }
        }
        /* lösbar und nicht geschenkt */
        G.unlocked_ch = jb->ch;
        G.free = 0;
        G.job = j;
        job_build();
        {
            int w, st0 = compare(tgt_patch, bench, &w), st1;
            patch_t cp;
            memcpy(cp, tgt_patch, sizeof cp);
            st1 = compare(tgt_patch, cp, &w);
            if (st1 != 3) FAIL("%s: genau nachgebaut nur %d Sterne", jb->who, st1);
            if (st0 == 3) FAIL("%s: schon am Anfang gelöst", jb->who);
            /* streng: jeder Regler, der zählt, 20 daneben -> durchgefallen; 4 daneben -> noch 3 Sterne */
            for (s = jb->set; s[0] != END_; s += 2) {
                int p = s[0], d;
                if (categorical(p) || p == P_DRONE || !relevant(p, tgt_patch, tgt_patch)) continue;
                for (d = -1; d <= 1; d += 2) {
                    int far = tgt_patch[p] + d * (p == P_HZ ? 8 : 20), near = tgt_patch[p] + d * (p == P_HZ ? 2 : 4);
                    if (far >= 0 && far <= 127) {
                        memcpy(cp, tgt_patch, sizeof cp); cp[p] = (uint8_t)far;
                        if (compare(tgt_patch, cp, &w) != 0) FAIL("%s: Regler %d um %d daneben besteht noch", jb->who, p, far - tgt_patch[p]);
                    }
                    if (near >= 0 && near <= 127) {
                        memcpy(cp, tgt_patch, sizeof cp); cp[p] = (uint8_t)near;
                        if (compare(tgt_patch, cp, &w) != 3) FAIL("%s: Regler %d knapp daneben keine 3 Sterne", jb->who, p);
                    }
                }
            }
            /* über die Regler erreichbar: Schritte von 2 ab P_INIT */
        }
    }
    /* Tonlager-Namen */
    for (j = 0; j < NPRESETS; j++) if (text_len(PRESETS[j].name) > 9) FAIL("Preset-Name zu lang: %s", PRESETS[j].name);
}

/* der Roboter: erfüllt, worauf das Spiel gerade wartet */
static int robot(void)
{
    int sc = G.screen, guard;
    if (G.dlg_on) { wt(150); tapb(FAB_B_SEL); return 1; }
    switch (sc) {
    case SC_WORK:
        if (G.job_on) {
            const job_t *jb = &JOBS[G.job];
            if (jb->kind == JK_SOUND) { memcpy(bench, tgt_patch, NP); bench[P_DRONE] = tgt_patch[P_DRONE]; }
            else {
                const uint8_t *s;
                for (s = jb->set; s[0] != END_; s += s[0] >= BPM_MIN ? 2 : 3) {
                    if (s[0] == BPM_MIN) CUR.song.bpm = s[1];
                    else if (s[0] == BPM_MAX) CUR.song.bpm = s[1];
                    else CUR.song.on[s[0]] = (uint16_t)(s[1] | s[2] << 8);
                }
            }
            wt(100);
            tapb(FAB_B_REC);
            return 1;
        }
        if (G.in_story) {
            const op_t *o = &CHAPTERS[G.ch - 1][G.pc];
            switch (o->op) {
            case O_WAIT:
                if (o->b == CMP_GE || o->b == CMP_EQ || o->b == CMP_NEAR) bench[o->a] = o->c;
                else if (o->b == CMP_LE) bench[o->a] = o->c;
                else bench[o->a] = (uint8_t)(P_INIT[o->a] + o->c <= P_MAX[o->a] ? P_INIT[o->a] + o->c : P_INIT[o->a] - o->c);
                wt(50);
                return 1;
            case O_WAITKEY: tapk(12); return 1;
            case O_WAITBTN: tapb(o->a); return 1;
            case O_WAITM: tapb(FAB_B_ENV); return 1;
            case O_SEQGOAL: CUR.song.on[o->a] = (uint16_t)(o->b | o->c << 8); wt(50); return 1;
            default: wt(50); return 1;
            }
        }
        return 0;
    case SC_CALL: wt(300); tapb(FAB_B_SEL); return 1;
    case SC_RESULT: wt(300); tapb(FAB_B_SEL); return 1;
    case SC_GAME:
        if (G.g_kind == G_HOCHTIEF) {
            guard = 0;
            while (G.screen == SC_GAME && G.g_phase != 1 && guard++ < 300) step();
            if (G.screen == SC_GAME) tapb(G.g_b > G.g_a ? FAB_B_OCTUP : FAB_B_OCTDN);
        } else if (G.g_kind == G_WELLEN) {
            guard = 0;
            while (G.screen == SC_GAME && G.g_phase != 1 && guard++ < 300) step();
            if (G.screen == SC_GAME) {
                while (G.g_sel != G.g_a) { E[FAB_E_SELECT] = 1; step(); step(); }
                tapb(FAB_B_SEL);
            }
        } else {
            int i;
            guard = 0;
            while (G.screen == SC_GAME && G.g_phase != 2 && guard++ < 1000) step();
            if (G.screen == SC_GAME) for (i = 0; i < G.g_len && G.screen == SC_GAME && G.g_phase == 2; i++) tapk(white_key[G.g_seq[i]]);
        }
        wt(100);
        return 1;
    default:
        return 0;
    }
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    int c;
    fab_init(42);
    check_content();
    printf("Inhalt geprüft: %d Aufträge, %d Kapitel\n", NJOBS, NCH);
    /* alle Kapitel durchspielen */
    fab_init(43);
    tapb(FAB_B_SEL); tapb(FAB_B_SEL);           /* Titel, neuer Platz: Kapitel 1 */
    for (c = 1; c <= NCH; c++) {
        int guard = 0, shot_done = 0;
        char nm[64];
        if (c > 1) {
            if (G.screen != SC_MAP) { FAIL("vor Kapitel %d nicht auf der Karte (Bildschirm %d)", c, G.screen); break; }
            while (G.cur != c - 1) { E[FAB_E_SELECT] = 1; step(); step(); }
            tapb(FAB_B_SEL);
        }
        if (G.ch != c) { FAIL("Kapitel %d startet nicht", c); break; }
        while (G.in_story && G.ch == c && guard++ < 4000) {
            if (!shot_done && G.screen == SC_WORK && G.job_on && !G.dlg_on && JOBS[G.job].kind == JK_SOUND) {
                wt(300);
                snprintf(nm, sizeof nm, "kapitel%02d-auftrag", c);
                shot(out, nm);
                shot_done = 1;
            }
            if (!robot()) wt(50);
        }
        if (guard >= 4000) { FAIL("Kapitel %d hängt bei Befehl %d (Bildschirm %d)", c, G.pc, G.screen); break; }
        printf("Kapitel %2d %-15s geschafft, Sterne gesamt %d\n", c, CH_NAME[c - 1], slot_stars(&CUR));
        if (c == NCH) { wt(300); shot(out, "urkunde"); }
    }
    if (CUR.ch_done != NCH) FAIL("am Ende %d Kapitel geschafft", CUR.ch_done);
    /* Auftrags-Modus: drei Zufallsaufträge auf SCHWER */
    tapb(FAB_B_SEL);
    if (G.screen != SC_MENU) FAIL("nach der Urkunde nicht im Menü");
    G.cur = 1; tapb(FAB_B_SEL);
    G.cur = 2; tapb(FAB_B_SEL);
    {
        int n = 0, guard = 0;
        while (n < 3 && guard++ < 2000) {
            if (G.screen == SC_RESULT) { n++; if (n == 1) shot(out, "auftrag-ergebnis"); }
            if (G.screen == SC_WORK && G.job_on && !G.dlg_on && n == 0) shot(out, "auftrag-schwer");
            robot();
        }
        printf("Auftrags-Modus: %d Aufträge, Punkte %d\n", n, CUR.score);
        if (n < 3) FAIL("Auftrags-Modus hängt");
    }
    printf(fails ? "%d FEHLER\n" : "alle Tests bestanden\n", fails);
    return fails != 0;
}
