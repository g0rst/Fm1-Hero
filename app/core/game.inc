/* SPDX-License-Identifier: GPL-3.0-only
 * Die Tonfabrik, Spiel: Bildschirme, Story, Aufträge, Minispiele, Livemodus, Lichter, Spielstände. Teil von fab.c. */

/* in welchem Kapitel ein Regler dazukommt */
static const uint8_t PARAM_CH[NP] = {
    2, 2, 1, 7, 2,
    7, 7, 7, 7,
    10, 10, 10,
    5, 5, 5, 5,
    3, 3,
    4, 4, 4, 4,
    5, 5, 5, 5,
    6, 6, 6, 6,
    8, 8, 8, 8,
    99,
};
enum { VK_BAR, VK_WAVE, VK_OCT, VK_HZ, VK_SIGN, VK_FTYPE, VK_LTGT, VK_LWAVE, VK_RATIO };
static uint8_t vkind(int p)
{
    switch (p) {
    case P_W1: case P_W2: return VK_WAVE;
    case P_OCT1: case P_OCT2: return VK_OCT;
    case P_HZ: return VK_HZ;
    case P_PITCH: case P_DET: case P_FENV: return VK_SIGN;
    case P_FTYPE: return VK_FTYPE;
    case P_LTGT: return VK_LTGT;
    case P_LWAVE: return VK_LWAVE;
    case P_RATIO: return VK_RATIO;
    default: return VK_BAR;
    }
}
static int categorical(int p)
{
    int k = vkind(p);
    return k == VK_WAVE || k == VK_OCT || k == VK_FTYPE || k == VK_LTGT || k == VK_LWAVE || k == VK_RATIO;
}

typedef struct {
    const char *name, *en;
    const char *const *ic;
    int8_t k[4];
    const char *lab[4];
} mach_t;
static const mach_t MACH[NM] = {
    {"SCHWINGER 1", "OSC", IC_OSC, {P_W1, P_OCT1, P_PITCH, P_PW}, {"WELLE", "OKTAV", "HÜPF", "PULS"}},
    {"SCHWINGER 2", "OSC 2", IC_OSC2, {P_W2, P_OCT2, P_DET, P_MIX2}, {"WELLE", "OKTAV", "VERST", "MENGE"}},
    {"FM-ZAUBER", "FM", IC_FM, {P_FM, P_RATIO, P_FMENV, -1}, {"STÄRK", "VERH", "HÜLLK", ""}},
    {"FILTER", "VCF", IC_VCF, {P_CUT, P_RES, P_FENV, P_FTYPE}, {"HELL", "PFIFF", "HÜLLK", "TYP"}},
    {"TOR", "VCA", IC_VCA, {P_VOL, P_DRIVE, -1, -1}, {"LAUT", "KRATZ", "", ""}},
    {"HÜLLKURVE", "ENV", IC_ENV, {P_A1, P_D1, P_S1, P_R1}, {"START", "FALL", "HALTE", "ENDE"}},
    {"HÜLLKURVE 2", "ENV 2", IC_ENV, {P_A2, P_D2, P_S2, P_R2}, {"START", "FALL", "HALTE", "ENDE"}},
    {"WACKLER", "LFO", IC_LFO, {P_LRATE, P_LDEP, P_LTGT, P_LWAVE}, {"TEMPO", "STÄRK", "ZIEL", "FORM"}},
    {"ECHO + HALL", "FX", IC_FX, {P_ECHO, P_ETIME, P_EFB, P_HALL}, {"ECHO", "ZEIT", "OFT", "HALL"}},
};
static const uint8_t MAIN_ROW[] = {M_OSC1, M_OSC2, M_FM, M_VCF, M_VCA, M_FX};
static const uint8_t MOD_ROW[] = {M_ENV1, M_ENV2, M_LFO};
static const uint8_t SEL_ORDER[NM] = {M_OSC1, M_OSC2, M_FM, M_VCF, M_VCA, M_ENV1, M_ENV2, M_LFO, M_FX};

/* ------------------------------------------------------------------ Spielstand --- */
#define SLOT_MAGIC 0x54464231u          /* "TFB1" */
typedef struct {
    uint32_t magic;
    uint8_t used, ch_done, diff, rsv;
    uint8_t stars[(NJOBS + 3) / 4];     /* 2 Bit je Auftrag: die besten Sterne */
    uint16_t played, score;
    song_t song;
} slot_t;
static slot_t SL[FAB_NSLOTS];
static uint8_t save_mask;
#define CUR (SL[G.slot])

static int get_stars(const slot_t *s, int j) { return s->stars[j >> 2] >> ((j & 3) * 2) & 3; }
static void put_stars(slot_t *s, int j, int v)
{
    if (v > get_stars(s, j)) {
        s->stars[j >> 2] = (uint8_t)((s->stars[j >> 2] & ~(3 << ((j & 3) * 2))) | v << ((j & 3) * 2));
    }
}
static int slot_stars(const slot_t *s)
{
    int j, n = 0;
    for (j = 0; j < NJOBS; j++) n += get_stars(s, j);
    return n;
}
static int ch_stars(const slot_t *s, int ch, int *max)
{
    int j, n = 0;
    *max = 0;
    for (j = 0; j < NJOBS; j++)
        if (JOBS[j].ch == ch) { n += get_stars(s, j); *max += 3; }
    return n;
}

static void make_patch(uint8_t *p, const uint8_t *set)
{
    int i;
    for (i = 0; i < NP; i++) p[i] = P_INIT[i];
    for (; set && set[0] != END_; set += 2)
        if (set[0] < NP) p[set[0]] = set[1];
}
static void song_default(song_t *s)
{
    int t, i;
    static const uint8_t BASS[16] = {1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    static const int8_t BASSN[16] = {0, 0, 0, 0, 0, 0, 7, 0, 3, 0, 0, 3, 0, 0, 5, 0};
    for (i = 0; i < (int)sizeof *s; i++) ((uint8_t *)s)[i] = 0;
    s->bpm = 100; s->swing = 50; s->len = 16;
    for (t = 0; t < NTR; t++) {
        s->level[t] = 110;
        make_patch(s->patch[t], PRESETS[t < 4 ? t : t == 4 ? 4 : 6].set);
    }
    s->sound[0] = 4; s->sound[1] = 6;
    s->on[0] = 0x1111; s->on[1] = 0x1010; s->on[2] = 0x4444;
    for (i = 0; i < 16; i++)
        if (BASS[i]) { s->on[3] |= (uint16_t)(1u << i); s->note[3][i] = BASSN[i]; }
}

/* ------------------------------------------------------------------ Zustand --- */
enum { SC_TITLE, SC_SLOTS, SC_MENU, SC_MAP, SC_WORK, SC_CALL, SC_RESULT, SC_GAME, SC_DIFF, SC_LIVE, SC_GAMES,
       SC_END };
enum { D_LEICHT, D_MITTEL, D_SCHWER };
static struct {
    uint32_t now, fr;
    uint32_t btn, keys, bpress, brel, kpress, krel;
    uint32_t bdown_at[FAB_NB];
    int32_t enc[FAB_NE];
    int screen, cur, slot, kb_oct, held_note_key;
    /* Story */
    int ch, pc, in_story, story_seq;
    uint32_t op_t0;
    /* Werkstatt */
    int mach, view, new_mach;
    uint32_t new_t0;
    uint8_t unlocked_ch;                /* Kapitel, bis zu dem Regler sichtbar sind */
    /* Auftrag */
    int job, job_on, free, diff, tries, last_stars, worst;
    uint32_t tgt_off_at;
    int8_t tgt_wave[120], tgt_zeit[120], own_wave[120], own_zeit[120];
    uint32_t pv_hash, pv_t, live_until;
    uint16_t pv_bin, tgt_bin;
    uint8_t pv_done;
    /* Dialog */
    const char *dlg, *dlg_page, *dlg_next;
    int dlg_on, dlg_shown, dlg_total, dlg_who, dlg_pose, dlg_fact;
    uint32_t dlg_t;
    /* Ruf und Ergebnis */
    int call_phase;
    uint32_t call_t0, res_t0;
    /* Minispiel */
    int g_kind, g_round, g_ok, g_bad, g_phase, g_a, g_b, g_sel, g_len, g_pos;
    uint8_t g_seq[8];
    uint32_t g_t0;
    /* Live */
    int track, live_fab, rec, held_step, step_edited;
    uint32_t home_t;
    char msg_a[24], msg_b[24];
    uint32_t msg_t;
    int confirm_home;
} G;
static patch_t bench, story_bench;
/* Entwicklermodus (nur die Browser-Version schaltet ihn ein, fab_dev): der dritte Platz hat alles offen, ARP
 * überspringt den aktuellen Schritt. Die Plätze 1 und 2 spielen wie immer. */
#define DEV_SLOT 2
static uint8_t dev_mode;
#define DEV (dev_mode && G.slot == DEV_SLOT)
static uint8_t white_key[16], black_key[11];   /* Tastennummer der 16 weißen und 11 schwarzen Tasten */

static int is_black(int k) { int pc = (53 + k) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
static int white_index(int k)
{
    int i;
    for (i = 0; i < 16; i++) if (white_key[i] == k) return i;
    return -1;
}
static int black_index(int k)
{
    int i;
    for (i = 0; i < 11; i++) if (black_key[i] == k) return i;
    return -1;
}

static int param_ok(int p) { return PARAM_CH[p] <= G.unlocked_ch; }
static int mach_ok(int m)
{
    int i;
    if (m == M_OSC1) return 1;
    if (m == M_ENV2) return G.unlocked_ch >= 5;
    for (i = 0; i < 4; i++)
        if (MACH[m].k[i] >= 0 && param_ok(MACH[m].k[i])) return 1;
    return 0;
}
static uint8_t *edit_patch(void) { return G.screen == SC_LIVE ? CUR.song.patch[G.track] : bench; }
static int knob_param(int m, int i)
{
    if (m == M_OSC1 && i == 0 && edit_patch()[P_DRONE]) return P_HZ;
    return MACH[m].k[i];
}

/* ------------------------------------------------------------------ Dialog --- */
#define LW 19
#define NL 4
/* bricht den Text ab s in Zeilen um: Anfang und Länge (Bytes) jeder Zeile; gibt den Anfang der nächsten Seite */
static const char *wrap(const char *s, const char *ls[NL], int ll[NL], int *nlines)
{
    int n = 0;
    while (*s == ' ') s++;
    while (*s && n < NL) {
        const char *st = s, *best = 0;
        int g = 0;
        const char *q = s;
        while (*q && *q != '\n') {
            const char *w = q;
            int gw;
            while (*q == ' ') q++;
            w = q;
            gw = 0;
            while (*q && *q != ' ' && *q != '\n') { const char *t = q; next_glyph(&t); q = t; gw++; }
            if (g + (g ? 1 : 0) + gw > LW && g) { q = w; break; }
            g += (g ? 1 : 0) + gw;
            best = q;
            if (g >= LW) break;
        }
        if (!best) best = q;
        ls[n] = st;
        ll[n] = (int)(best - st);
        n++;
        s = best;
        while (*s == ' ') s++;
        if (*s == '\n') s++;
    }
    *nlines = n;
    return s;
}
static void say(const char *t, int who, int pose)
{
    G.dlg = t;
    G.dlg_page = t;
    G.dlg_on = 1;
    G.dlg_shown = 0;
    G.dlg_who = who;
    G.dlg_pose = pose;
    G.dlg_t = G.now;
    G.dlg_fact = 0;
    {
        const char *ls[NL];
        int ll[NL], n, i;
        G.dlg_next = wrap(t, ls, ll, &n);
        G.dlg_total = 0;
        for (i = 0; i < n; i++) {
            const char *a = ls[i];
            while (a < ls[i] + ll[i] && next_glyph(&a) >= 0) G.dlg_total++;
        }
    }
}
static void dlg_input(void)
{
    if (!G.dlg_on) return;
    if (G.dlg_shown < G.dlg_total) {
        int want = (int)((G.now - G.dlg_t) / 28u);
        if (want > G.dlg_shown) {
            if (G.dlg_who == 0 && (want / 3) != (G.dlg_shown / 3))
                sfx(SFX_TOK, 84 + (int)(rnd() % 9u));
            else if (G.dlg_who > 0 && (want / 4) != (G.dlg_shown / 4))
                sfx(SFX_BLIP, 70 + (int)(rnd() % 12u));
            G.dlg_shown = want > G.dlg_total ? G.dlg_total : want;
        }
    }
    if (G.bpress >> FAB_B_SEL & 1u) {
        if (G.dlg_shown < G.dlg_total)
            G.dlg_shown = G.dlg_total;
        else if (*G.dlg_next) {
            const char *n = G.dlg_next;
            int who = G.dlg_who, pose = G.dlg_pose, fact = G.dlg_fact;
            say(n, who, pose);
            G.dlg_fact = fact;
        } else {
            G.dlg_on = 0;
            sfx(SFX_BLIP, 84);
        }
    }
}
static void draw_dialog(int y)
{
    const char *ls[NL];
    int ll[NL], n, i, left = G.dlg_shown;
    int bc = G.dlg_fact ? C_YEL : C_WHT;
    round_box(1, y, 118, 36, bc, C_BLK);
    wrap(G.dlg_page, ls, ll, &n);
    for (i = 0; i < n && left > 0; i++) {
        char buf[64];
        int k = 0;
        const char *a = ls[i];
        while (a < ls[i] + ll[i] && left > 0 && k < 60) {
            const char *b = a;
            next_glyph(&b);
            while (a < b) buf[k++] = *a++;
            left--;
        }
        buf[k] = 0;
        text(4, y + 3 + i * 8, buf, C_BLK, 1);
    }
    /* Sprecher */
    if (G.dlg_who == 0) {
        int pose = G.dlg_pose == 1 ? 2 : (G.dlg_shown < G.dlg_total && (G.fr / 4) & 1) ? 1 : 0;
        rect(8, y + 35, 4, 1, bc);
        px(9, y + 36, bc); px(8, y + 36, C_BLK); px(11, y + 36, C_BLK);
        spr(&SPR_VOGL[pose], 2, y + 37, 0);
        if (G.dlg_fact) text(22, y + 44, "WUSSTEST DU'S?", C_YEL, 1);
        else text(22, y + 44, "HERR VOGL", C_WHT, 1);
    } else {
        client_icon(G.dlg_who - 1, 3, y + 38, 0);
        text(18, y + 41, JOBS[G.job].who, C_WHT, 1);
    }
    if (G.dlg_shown >= G.dlg_total && (G.fr / 15) & 1)
        text(92, y + 52, "SEL" T_RIGHT, C_YEL, 1);
}

/* ------------------------------------------------------------------ Lichter --- */
static uint32_t L_b, L_k, L_db, L_dk, L_mb, L_mk;
static int L_glow = 1;
static int blink(int hz) { return (int)((G.now * (uint32_t)hz / 500u) & 1u); }

/* ------------------------------------------------------------------ Vergleich --- */
static const char *HINT_HI[NP], *HINT_LO[NP];
static void hints_init(void)
{
    HINT_HI[P_OCT1] = "DER TON IST ZU HOCH. DREH DIE OKTAVE RUNTER!";
    HINT_LO[P_OCT1] = "DER TON IST ZU TIEF. DREH DIE OKTAVE HOCH!";
    HINT_HI[P_HZ] = "DU KLOPFST ZU SCHNELL. DREH K1 ETWAS ZURÜCK!";
    HINT_LO[P_HZ] = "DU KLOPFST ZU LANGSAM. DREH K1 WEITER!";
    HINT_HI[P_PW] = "DAS RECHTECK IST ZU SCHMAL.";
    HINT_LO[P_PW] = "DAS RECHTECK MUSS SCHMALER SEIN. K4!";
    HINT_HI[P_PITCH] = "DER TONSPRUNG IST ZU GROSS.";
    HINT_LO[P_PITCH] = "DER TONSPRUNG IST ZU KLEIN ODER FALSCH HERUM.";
    HINT_HI[P_OCT2] = "SCHWINGER 2 IST ZU HOCH.";
    HINT_LO[P_OCT2] = "SCHWINGER 2 IST ZU TIEF.";
    HINT_HI[P_DET] = "SCHWINGER 2 IST FALSCH VERSTIMMT.";
    HINT_LO[P_DET] = "SCHWINGER 2 IST FALSCH VERSTIMMT.";
    HINT_HI[P_MIX2] = "ZU VIEL VON SCHWINGER 2.";
    HINT_LO[P_MIX2] = "ES FEHLT SCHWINGER 2. K4 AUF!";
    HINT_HI[P_FM] = "ZU VIEL FM-ZAUBER.";
    HINT_LO[P_FM] = "MEHR FM-ZAUBER!";
    HINT_HI[P_RATIO] = "DAS FM-VERHÄLTNIS STIMMT NICHT.";
    HINT_LO[P_RATIO] = "DAS FM-VERHÄLTNIS STIMMT NICHT.";
    HINT_HI[P_FMENV] = "DIE FM-HÜLLKURVE IST ZU STARK.";
    HINT_LO[P_FMENV] = "DIE FM-HÜLLKURVE IST ZU SCHWACH.";
    HINT_HI[P_CUT] = "ZU HELL! DREH DEN FILTER ZU.";
    HINT_LO[P_CUT] = "ZU DUMPF! MACH DEN FILTER AUF.";
    HINT_HI[P_RES] = "ZU VIEL PFIFF IM FILTER.";
    HINT_LO[P_RES] = "DER FILTER BRAUCHT MEHR PFIFF.";
    HINT_HI[P_FENV] = "DER FILTER BEWEGT SICH ZU STARK.";
    HINT_LO[P_FENV] = "DER FILTER MUSS SICH MEHR BEWEGEN. K3!";
    HINT_HI[P_FTYPE] = "FALSCHER FILTER-TYP.";
    HINT_LO[P_FTYPE] = "FALSCHER FILTER-TYP.";
    HINT_HI[P_VOL] = "ZU LAUT! DAS TOR ETWAS ZU.";
    HINT_LO[P_VOL] = "ZU LEISE! MACH DAS TOR WEITER AUF.";
    HINT_HI[P_DRIVE] = "ZU KRATZIG!";
    HINT_LO[P_DRIVE] = "DA FEHLT KRATZ! TOR K2.";
    HINT_HI[P_A1] = "DER TON STARTET ZU LANGSAM.";
    HINT_LO[P_A1] = "DER TON STARTET ZU SCHNELL. MEHR START!";
    HINT_HI[P_D1] = "DER TON FÄLLT ZU LANGSAM.";
    HINT_LO[P_D1] = "DER TON FÄLLT ZU SCHNELL.";
    HINT_HI[P_S1] = "DER TON HÄLT ZU LAUT.";
    HINT_LO[P_S1] = "DER TON HÄLT ZU LEISE. HALTEN HOCH!";
    HINT_HI[P_R1] = "DER TON KLINGT ZU LANGE NACH.";
    HINT_LO[P_R1] = "DER TON HÖRT ZU SCHNELL AUF. MEHR ENDE!";
    HINT_HI[P_A2] = "HÜLLKURVE 2 STARTET ZU LANGSAM.";
    HINT_LO[P_A2] = "HÜLLKURVE 2 STARTET ZU SCHNELL.";
    HINT_HI[P_D2] = "HÜLLKURVE 2 FÄLLT ZU LANGSAM.";
    HINT_LO[P_D2] = "HÜLLKURVE 2 FÄLLT ZU SCHNELL.";
    HINT_HI[P_S2] = "HÜLLKURVE 2 HÄLT ZU HOCH.";
    HINT_LO[P_S2] = "HÜLLKURVE 2 HÄLT ZU NIEDRIG.";
    HINT_HI[P_R2] = "HÜLLKURVE 2: ENDE ZU LANG.";
    HINT_LO[P_R2] = "HÜLLKURVE 2: ENDE ZU KURZ.";
    HINT_HI[P_LRATE] = "DER WACKLER IST ZU SCHNELL.";
    HINT_LO[P_LRATE] = "DER WACKLER IST ZU LANGSAM.";
    HINT_HI[P_LDEP] = "ES WACKELT ZU STARK.";
    HINT_LO[P_LDEP] = "ES MUSS MEHR WACKELN!";
    HINT_HI[P_LTGT] = "DER WACKLER HAT DAS FALSCHE ZIEL.";
    HINT_LO[P_LTGT] = "DER WACKLER HAT DAS FALSCHE ZIEL.";
    HINT_HI[P_LWAVE] = "DER WACKLER HAT DIE FALSCHE FORM.";
    HINT_LO[P_LWAVE] = "DER WACKLER HAT DIE FALSCHE FORM.";
    HINT_HI[P_ECHO] = "ZU VIEL ECHO.";
    HINT_LO[P_ECHO] = "MEHR ECHO!";
    HINT_HI[P_ETIME] = "DAS ECHO KOMMT ZU SPÄT.";
    HINT_LO[P_ETIME] = "DAS ECHO KOMMT ZU FRÜH.";
    HINT_HI[P_EFB] = "DAS ECHO KOMMT ZU OFT.";
    HINT_LO[P_EFB] = "DAS ECHO MUSS ÖFTER ZURÜCKKOMMEN.";
    HINT_HI[P_HALL] = "ZU VIEL HALL.";
    HINT_LO[P_HALL] = "MEHR HALL!";
}
static const char *const WAVE_HINT[NW] = {
    "PROBIER DEN SINUS: GANZ WEICH UND RUND.", "PROBIER DAS DREIECK: WEICH, ABER HELLER.",
    "PROBIER DIE SÄGE: SCHARF UND HELL.", "PROBIER DAS RECHTECK: HOHL.", "PROBIER DAS RAUSCHEN!", "KLOPFEN!"};

/* Sterne für einen Regler: 3 gut, 0 weit daneben */
static int pstars(int p, int a, int b)
{
    int d = a > b ? a - b : b - a;
    if (categorical(p)) return d ? 0 : 3;
    if (p == P_HZ) return d <= 2 ? 3 : d <= 4 ? 2 : d <= 6 ? 1 : 0;   /* 1 Schritt: etwa 6 % der Frequenz */
    return d <= 5 ? 3 : d <= 10 ? 2 : d <= 16 ? 1 : 0;
}
static int relevant(int p, const uint8_t *t, const uint8_t *b)
{
    if (!param_ok(p) || p == P_DRONE) return 0;
    if (p == P_PW) return t[P_W1] == W_SQR;
    if (p == P_HZ) return t[P_DRONE];
    if (p == P_OCT1) return !t[P_DRONE];
    if (p == P_W2 || p == P_OCT2 || p == P_DET) return (t[P_MIX2] > 8 || b[P_MIX2] > 8) && !t[P_RATIO];
    if (p == P_RATIO) return t[P_FM] || t[P_FMENV] || b[P_FM] || b[P_FMENV];
    if (p == P_A2 || p == P_D2 || p == P_S2 || p == P_R2) {
        int tu = t[P_FENV] != 64 || t[P_PITCH] != 64 || t[P_FMENV];
        int bu = b[P_FENV] < 56 || b[P_FENV] > 72 || b[P_PITCH] < 56 || b[P_PITCH] > 72 || b[P_FMENV] > 8;
        return tu || bu;
    }
    if (p == P_LRATE || p == P_LTGT || p == P_LWAVE) return t[P_LDEP] > 8 || b[P_LDEP] > 8;
    if (p == P_ETIME || p == P_EFB) return t[P_ECHO] > 8 || b[P_ECHO] > 8;
    if (p == P_D1) return t[P_S1] < 118 || b[P_S1] < 118;
    return 1;
}
static int compare(const uint8_t *t, const uint8_t *b, int *worst)
{
    int p, st = 3, wd = -1;
    *worst = -1;
    for (p = 0; p < NP; p++) {
        int s, d;
        if (!relevant(p, t, b)) continue;
        s = pstars(p, b[p], t[p]);
        d = b[p] > t[p] ? b[p] - t[p] : t[p] - b[p];
        if (categorical(p) && d) d = 1000;
        if (s < st || (s == st && d > wd)) { st = s; wd = d; *worst = p; }
    }
    if (st == 3) *worst = -1;
    return st;
}
static int param_mach(int p, int *knob);
static void set_mach(int m);
static const char *const MACH_SHORT[NM];
static const char *hint_for(int p)
{
    static char hb[96];
    const char *h;
    int d;
    if (p < 0) return "SUPER! DAS PASST GENAU.";
    if (p == P_W1) return WAVE_HINT[tgt_patch[P_W1]];
    if (p == P_W2) return "SCHWINGER 2 BRAUCHT EINE ANDERE WELLE.";
    h = bench[p] > tgt_patch[p] ? HINT_HI[p] : HINT_LO[p];
    d = bench[p] > tgt_patch[p] ? bench[p] - tgt_patch[p] : tgt_patch[p] - bench[p];
    {
        char *e = hb;
        int knob = 0, m = param_mach(p, &knob);
        if (!categorical(p))
            e = put_str(e, d <= (p == P_HZ ? 4 : 10) ? "FAST! NUR EIN BISSCHEN: " : d >= 40 ? "NOCH GANZ WEIT WEG. " : "");
        e = put_str(e, h);
        if (m >= 0) {                   /* wo: Maschine und Regler; die Maschine blinkt in der Leiste */
            e = put_str(put_str(e, " SELECT: "), MACH_SHORT[m]);
            e = put_int(put_str(e, ", K"), knob);
            put_str(e, ".");
            G.new_mach = m;
            G.new_t0 = G.now;
        }
    }
    return hb;
}

/* ------------------------------------------------------------------ Auftrag --- */
static int job_chapter(void) { return G.free ? (CUR.ch_done < 1 ? 1 : CUR.ch_done) : JOBS[G.job].ch; }
static void job_build(void)
{
    const job_t *j = &JOBS[G.job];
    int p;
    make_patch(tgt_patch, j->set);
    if (G.free) {                       /* jedes Mal etwas anders: ein oder zwei Regler verschoben */
        int n = 1 + (int)(rnd() % 2u), tries = 0;
        while (n > 0 && tries++ < 20) {
            const uint8_t *s = j->set;
            int cnt = 0, pick, k = 0;
            for (; s[0] != END_; s += 2) if (!categorical(s[0]) && s[0] != P_DRONE && param_ok(s[0])) cnt++;
            if (!cnt) break;
            pick = (int)(rnd() % (uint32_t)cnt);
            for (s = j->set; s[0] != END_; s += 2)
                if (!categorical(s[0]) && s[0] != P_DRONE && param_ok(s[0]) && k++ == pick) {
                    int v = tgt_patch[s[0]] + (int)(rnd() % 31u) - 15;
                    tgt_patch[s[0]] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                    n--;
                    break;
                }
        }
    }
    /* der Werktisch: was das Kind schon kennt, steht auf Anfang; der Rest ist schon fertig */
    for (p = 0; p < NP; p++)
        bench[p] = param_ok(p) ? P_INIT[p] : tgt_patch[p];
    bench[P_DRONE] = tgt_patch[P_DRONE];
    if (bench[P_DRONE]) bench[P_HZ] = 0;
    {                                   /* klingt der Anfang schon richtig, ist der Ton "kaputt": andere Welle */
        int w;
        if (compare(tgt_patch, bench, &w) == 3 && param_ok(P_W1))
            bench[P_W1] = tgt_patch[P_W1] == W_SAW ? W_SQR : W_SAW;
    }
    syn_offline(tgt_patch, j->note ? j->note : 60, G.tgt_wave, G.tgt_zeit, 441);
    G.tgt_bin = 441;
    G.pv_hash = 0;
}
static void tgt_play(void)
{
    const job_t *j = &JOBS[G.job];
    syn_off(V_TARGET);
    syn_on(V_TARGET, j->note ? j->note : 60, 110);
    G.tgt_off_at = G.now + (tgt_patch[P_DRONE] ? 1500u : 600u);
    scope_v = V_TARGET;
}

/* ------------------------------------------------------------------ Bildschirm wechseln --- */
static void go(int sc)
{
    G.screen = sc;
    G.cur = 0;
    G.confirm_home = 0;
    if (sc != SC_WORK && sc != SC_LIVE) { syn_off(V_BENCH); }
    if (sc != SC_LIVE && !G.story_seq) seq_play = 0;
}
static void story_next(void);
static void job_start(int j)
{
    if (G.in_story && G.screen == SC_WORK) { int i; for (i = 0; i < NP; i++) story_bench[i] = bench[i]; }
    G.job = j;
    G.tries = 0;
    if (JOBS[j].kind == JK_GAME) {
        G.g_kind = JOBS[j].set[0];
        G.g_round = 0; G.g_ok = 0; G.g_bad = 0; G.g_phase = 0; G.g_t0 = G.now; G.g_sel = 0; G.g_pos = 0;
        G.g_len = G.g_kind == G_NACHSPIEL ? 3 : 0;
        go(SC_GAME);
        say(JOBS[j].call, 0, 0);
        return;
    }
    G.call_phase = 0;
    G.call_t0 = G.now;
    go(SC_CALL);
    sfx(SFX_RING, 88);
}

/* ------------------------------------------------------------------ Story --- */
static void story_begin(int ch)
{
    G.ch = ch;
    G.pc = 0;
    G.in_story = 1;
    G.story_seq = 0;
    G.free = 0;
    G.unlocked_ch = (uint8_t)ch;
    G.mach = M_OSC1;
    G.view = 0;
    G.job_on = 0;
    make_patch(bench, 0);
    V[V_BENCH].p = bench;
    seq_fx_from = V_BENCH;
    scope_v = V_BENCH;
    go(SC_WORK);
    G.op_t0 = G.now;
    story_next();
}
/* führt die Befehle aus, die nicht warten; hält beim ersten, der wartet */
static void story_next(void)
{
    for (;;) {
        const op_t *o = &CHAPTERS[G.ch - 1][G.pc];
        G.op_t0 = G.now;
        switch (o->op) {
        case O_SAY: say(o->t, 0, 0); return;
        case O_HAPPY: say(o->t, 0, 1); sfx(SFX_STAR, 84); return;
        case O_FACT: say(o->t, 0, 0); G.dlg_fact = 1; return;
        case O_UNLOCK: G.new_mach = o->a; G.new_t0 = G.now; G.mach = o->a; sfx(SFX_STAR, 79); break;
        case O_SEL: set_mach(o->a); break;
        case O_SET: bench[o->a] = o->b; break;
        case O_RESET: make_patch(bench, 0); break;
        case O_VIEW: G.view = o->a; break;
        case O_SEQCLR: CUR.song.on[0] = CUR.song.on[1] = 0; CUR.song.bpm = 100; break;
        case O_SEQ: {
            int t;
            G.story_seq = 1; G.track = 0; G.live_fab = 0; G.held_step = -1; seq_fx_from = 0; scope_v = 0;
            S_song = &CUR.song;
            for (t = 0; t < NTR; t++) V[t].p = CUR.song.patch[t];
            break;
        }
        case O_JOB: job_start(o->a); return;
        case O_END: {
            int done = CUR.ch_done;
            if (G.ch > done) CUR.ch_done = (uint8_t)G.ch;
            save_mask |= (uint8_t)(1u << G.slot);
            G.in_story = 0;
            G.story_seq = 0;
            seq_play = 0;
            bench[P_DRONE] = 0;
            go(G.ch == NCH && done < NCH ? SC_END : SC_MAP);
            G.cur = G.ch < NCH ? G.ch : NCH - 1;
            return;
        }
        default: return;                /* WAIT...: warten */
        }
        G.pc++;
    }
}
static int story_wait_done(const op_t *o)
{
    switch (o->op) {
    case O_WAIT: {
        int v = bench[o->a];
        switch (o->b) {
        case CMP_GE: return v >= o->c;
        case CMP_LE: return v <= o->c;
        case CMP_EQ: return v == o->c;
        case CMP_NEAR: { int d = v - o->c, t = o->a == P_HZ ? 1 : 2; return d >= -t && d <= t; }
        default: { int d = v - P_INIT[o->a]; return (d < 0 ? -d : d) >= o->c; }
        }
    }
    case O_WAITKEY: return G.kpress != 0;
    case O_WAITBTN: return G.bpress >> o->a & 1u;
    case O_WAITM: return G.mach == o->a;
    case O_SEQGOAL: return CUR.song.on[o->a] == (uint16_t)(o->b | o->c << 8);
    default: return 1;
    }
}
/* zu welcher Maschine ein Regler gehört (und welcher Knopf, 1..4); -1: keiner */
static int param_mach(int p, int *knob)
{
    int m, i;
    if (p == P_HZ) { if (knob) *knob = 1; return M_OSC1; }
    for (m = 0; m < NM; m++)
        for (i = 0; i < 4; i++)
            if (MACH[m].k[i] == p) { if (knob) *knob = i + 1; return m; }
    return -1;
}
static const char *const MACH_SHORT[NM] = {"SCHWINGER", "SCHWINGER 2", "FM-ZAUBER", "FILTER", "TOR", "HÜLLKURVE",
                                           "HÜLLKURVE 2", "WACKLER", "ECHO+HALL"};
/* die Maschine, auf die die Geschichte gerade wartet, wenn sie nicht gewählt ist; sonst -1 */
static int story_need_mach(void)
{
    const op_t *o;
    int m;
    if (!G.in_story || G.dlg_on || G.job_on || G.story_seq) return -1;
    o = &CHAPTERS[G.ch - 1][G.pc];
    if (o->op == O_WAITM) return o->a != G.mach ? o->a : -1;
    if (o->op != O_WAIT) return -1;
    m = param_mach(o->a, 0);
    return m >= 0 && m != G.mach ? m : -1;
}
static const char *story_instr(void)
{
    const op_t *o;
    static char sel[32];
    int need;
    if (!G.in_story || G.dlg_on || G.job_on) return 0;
    o = &CHAPTERS[G.ch - 1][G.pc];
    need = story_need_mach();
    if (o->op == O_WAIT && need >= 0) {   /* erst die richtige Maschine wählen */
        put_str(put_str(put_str(sel, "SELECT: "), MACH_SHORT[need]), "!");
        return sel;
    }
    if (o->op == O_WAIT || o->op == O_WAITKEY || o->op == O_WAITBTN || o->op == O_WAITM || o->op == O_SEQGOAL)
        return o->t;
    return 0;
}
static int story_waiting_btn(void)
{
    const op_t *o;
    if (!G.in_story || G.dlg_on || G.job_on) return -1;
    o = &CHAPTERS[G.ch - 1][G.pc];
    if (o->op == O_WAITBTN) return o->a;
    if (o->op == O_WAITM && o->a == M_ENV2) return FAB_B_ENV;
    return -1;
}
/* nach einem Auftrag oder Minispiel aus der Story */
static void back_to_story(void)
{
    int i;
    G.job_on = 0;
    G.unlocked_ch = (uint8_t)G.ch;
    for (i = 0; i < NP; i++) bench[i] = story_bench[i];
    V[V_BENCH].p = bench;
    scope_v = G.story_seq ? 0 : V_BENCH;
    go(SC_WORK);
    G.pc++;
    story_next();
}

/* ------------------------------------------------------------------ Regler drehen --- */
static int turn(uint8_t *p, int param, int det)
{
    int v = p[param], mx = P_MAX[param], step;
    if (!det) return 0;
    if (param == P_DRONE) return 0;
    step = mx > 10 ? (det > 1 || det < -1 ? det * 3 : det) : (det > 0 ? 1 : -1);   /* eine Raste: genau 1 */
    v += step;
    if (v < 0) v = 0;
    if (v > mx) v = mx;
    if (v == p[param]) return 0;
    p[param] = (uint8_t)v;
    return 1;
}

/* ------------------------------------------------------------------ Werkstatt: Eingabe --- */
static int key_note(int k) { return 53 + k + 12 * G.kb_oct; }
static void play_keys(int voice)
{
    int k;
    for (k = 0; k < FAB_NKEYS; k++) {
        if (G.kpress >> k & 1u) {
            if (edit_patch()[P_DRONE]) continue;
            syn_on(voice, key_note(k), 100);
            G.held_note_key = k;
            if (voice == V_BENCH) scope_v = V_BENCH;
            if (G.screen == SC_LIVE && G.rec && seq_play) {
                int s = seq_step < 16 ? seq_step : 0;
                CUR.song.on[G.track] |= (uint16_t)(1u << s);
                CUR.song.note[G.track][s] = (int8_t)(key_note(k) - 60);
            }
        }
        if (G.krel >> k & 1u && k == G.held_note_key) {
            int j, other = -1;
            for (j = 0; j < FAB_NKEYS; j++) if (G.keys >> j & 1u) other = j;
            if (other >= 0) { syn_on(voice, key_note(other), 100); G.held_note_key = other; }
            else { syn_off(voice); G.held_note_key = -1; }
        }
    }
    if (G.bpress >> FAB_B_OCTUP & 1u && G.kb_oct < 2) { G.kb_oct++; sfx(SFX_BLIP, 84); }
    if (G.bpress >> FAB_B_OCTDN & 1u && G.kb_oct > -2) { G.kb_oct--; sfx(SFX_BLIP, 72); }
}
/* das passende Bild zur Maschine: Klangfarbe als WELLE, Lautstärke und Bewegung als ZEIT */
static int auto_view(int m)
{
    const uint8_t *p = edit_patch();
    if (m == M_OSC1 && p[P_DRONE]) return p[P_HZ] < 64;
    if (m == M_VCA || m == M_ENV1 || m == M_ENV2 || m == M_FX) return 1;
    if (m == M_LFO) return p[P_LTGT] != L_PITCH;
    return 0;
}
static void set_mach(int m)
{
    G.live_until = 0;                   /* das Bild zeigt gleich die Vorschau der neuen Maschine */
    G.mach = m;
    G.view = auto_view(m);
}
static void select_mach(int dir)
{
    int i, idx = 0, n = 0;
    uint8_t list[NM];
    for (i = 0; i < NM; i++) if (mach_ok(SEL_ORDER[i])) list[n++] = SEL_ORDER[i];
    for (i = 0; i < n; i++) if (list[i] == G.mach) idx = i;
    idx = (idx + dir + n * 4) % n;
    set_mach(list[idx]);
    sfx(SFX_BLIP, 76 + idx * 2);
}
static void fabrik_input(int voice)
{
    int i;
    uint8_t *p = edit_patch();
    if (G.enc[FAB_E_SELECT]) select_mach(G.enc[FAB_E_SELECT] > 0 ? 1 : -1);
    if (G.bpress >> FAB_B_EDIT & 1u) select_mach(1);
    if (G.bpress >> FAB_B_ENV & 1u && mach_ok(M_ENV1)) {
        set_mach(G.mach == M_ENV1 && mach_ok(M_ENV2) ? M_ENV2 : M_ENV1);
        sfx(SFX_BLIP, 80);
    }
    if (G.bpress >> FAB_B_LFO & 1u && mach_ok(M_LFO)) { set_mach(M_LFO); sfx(SFX_BLIP, 80); }
    if (G.bpress >> FAB_B_FX & 1u && mach_ok(M_FX)) { set_mach(M_FX); sfx(SFX_BLIP, 80); }
    for (i = 0; i < 4; i++) {
        int prm = knob_param(G.mach, i);
        if (prm >= 0 && param_ok(prm)) turn(p, prm, G.enc[FAB_E_K1 + i]);
    }
    play_keys(voice);
}

/* ------------------------------------------------------------------ Werkstatt: Bild --- */
static void draw_knob(int cx, int cy, int frac1000, int c)
{
    int i;
    for (i = 0; i < 32; i++) {
        float a = (float)i / 32.0f;
        px(cx + (int)(sin1(a) * 6.5f), cy - (int)(sin1(a + 0.25f) * 6.5f), C_LGRY);
    }
    rect(cx - 4, cy - 4, 9, 9, C_DGRY);
    rect(cx - 5, cy - 3, 11, 7, C_DGRY);
    rect(cx - 3, cy - 5, 7, 11, C_DGRY);
    {
        float a = -0.375f + 0.75f * (float)frac1000 / 1000.0f;
        line(cx, cy, cx + (int)(sin1(a) * 5.0f + (sin1(a) > 0 ? 0.5f : -0.5f)),
             cy - (int)(sin1(a + 0.25f) * 5.0f + (sin1(a + 0.25f) > 0 ? 0.5f : -0.5f)), c);
    }
}
static void value_text(int p, int v, char *b)
{
    static const char *const FT[3] = {"TIEF", "BAND", "HOCH"};
    static const char *const LT[3] = {"TON", "HELL", "LAUT"};
    static const char *const RT[8] = {"AUS", "0.5", "1", "2", "3", "4", "5", "7"};
    b[0] = 0;
    switch (vkind(p)) {
    case VK_OCT: if (v > 2) put_str(b, "+"); put_int(b + (v > 2), v - 2); break;
    case VK_HZ: {
        float hz = klopf_hz(v);
        if (!v) { put_str(b, "AUS"); break; }
        if (hz < 10.0f) { char *e = put_int(b, (int)hz); *e++ = '.'; put_int(e, (int)(hz * 10.0f) % 10); }
        else put_int(b, (int)(hz + 0.5f));
        if (text_len(b) <= 3) put_str(b + text_len(b), "HZ");
        break;
    }
    case VK_FTYPE: put_str(b, FT[v % 3]); break;
    case VK_LTGT: put_str(b, LT[v % 3]); break;
    case VK_RATIO: put_str(b, RT[v & 7]); break;
    case VK_SIGN: if (v > 64) put_str(b, "+"); put_int(b + (v > 64), v - 64); break;
    default: put_int(b, v); break;
    }
}
static void draw_knobs(const uint8_t *p, int y)
{
    int i;
    rect(0, y, GW, GH - y, C_NAVY);
    for (i = 0; i < 4; i++) {
        int prm = knob_param(G.mach, i), cx = 15 + i * 30;
        const char *lab = MACH[G.mach].lab[i];
        if (i & 1) rect(i * 30, y, 30, GH - y, C_BLK);
        if (G.mach == M_OSC1 && prm == P_HZ) lab = "KLOPF";
        if (prm < 0 || !param_ok(prm)) {
            if (lab[0]) text_c(cx, y + 1, lab, C_DGRY, 1);
            text_c(cx, y + 18, "-", C_DGRY, 1);
            continue;
        }
        text_c(cx, y + 1, lab, C_LGRY, 1);
        {
            int v = p[prm], mx = P_MAX[prm];
            draw_knob(cx, y + 15, mx ? v * 1000 / mx : 0, C_YEL);
            if (vkind(prm) == VK_WAVE || vkind(prm) == VK_LWAVE) {
                int w = vkind(prm) == VK_WAVE ? v : (v == 0 ? 0 : v == 1 ? 3 : v == 2 ? 2 : 4);
                mini_wave(w, cx - 4, y + 25, C_GRN);
            } else {
                char b[12];
                value_text(prm, v, b);  /* die genaue Zahl: Aufträge und Aufgaben nennen Zahlen */
                text_c(cx, y + 23, b, C_GRN, 1);
                if (vkind(prm) == VK_BAR) {
                    rect(cx - 12, y + 32, 25, 2, C_DGRY);
                    rect(cx - 12, y + 32, 1 + v * 24 / 127, 2, C_GRN);
                }
            }
        }
    }
}
/* die Maschinen als eine Leiste: die Kette des Klangs (Strom bis Lautsprecher), dann die Steuer-Maschinen */
static void draw_band(const uint8_t *p, int y)
{
    int i, n = 0, nm = 0, x, w, ends, active = V[scope_v].s1 != E_IDLE;
    uint8_t vis[6], mod[3];
    rect(0, y, GW, 13, C_BLK);
    for (i = 0; i < 6; i++) if (mach_ok(MAIN_ROW[i])) vis[n++] = MAIN_ROW[i];
    for (i = 0; i < 3; i++) if (mach_ok(MOD_ROW[i])) mod[nm++] = MOD_ROW[i];
    ends = n + nm <= 7;                 /* Platz für Blitz und Lautsprecher */
    w = n + nm >= 9 ? 12 : 13;
    x = (GW - (n + nm + 2 * ends) * w - (nm ? 4 : 0)) / 2;
    if (ends) { icon(IC_BOLT, 11, 9, x + 1, y + 2, C_YEL); x += w; }
    for (i = 0; i < n + nm; i++) {
        int m = i < n ? vis[i] : mod[i - n];
        int sel = m == G.mach, isnew = (m == G.new_mach && G.now - G.new_t0 < 3000u) || m == story_need_mach();
        int ec = sel ? C_YEL : isnew && blink(4) ? C_WHT : i < n ? C_DGRY : C_LAV;
        if (i == n && nm) { rect(x + 1, y + 2, 1, 9, C_DGRY); x += 4; }
        round_box(x, y, w, 13, sel ? C_PLUM : C_NAVY, ec);
        if (m == M_VCA || m == M_ENV1) {   /* das Tor geht mit dem Ton auf und wieder zu */
            float lv = V[scope_v].e1 * (m == M_VCA ? (float)p[P_VOL] * (1.0f / 127.0f) : 1.0f);
            int hh = (int)(lv * 11.0f + 0.5f);
            if (V[scope_v].s1 == E_IDLE) hh = 0;
            if (hh > 11) hh = 11;
            if (hh) rect(x + 1, y + 12 - hh, w - 2, hh, C_DGRN);
        }
        icon(m == M_OSC1 && p[P_DRONE] ? IC_KLOPF : MACH[m].ic, 11, 9, x + 1, y + 2, sel ? C_WHT : C_LGRY);
        if (m == M_ENV2) text(x + 8, y + 5, "2", C_YEL, 1);
        if (active && i < n && (G.fr / 4u) % (uint32_t)n == (uint32_t)i) px(x + w / 2, y + 12, C_GRN);   /* der Ton läuft durch */
        x += w;
    }
    if (ends) icon(IC_SPK, 11, 9, x + 1, y + 2, active ? C_WHT : C_LGRY);
}
/* Vorschau: der eigene Klang, ohne Taste berechnet, kurz nachdem sich ein Regler bewegt hat */
static uint16_t view_bin(void) { return (uint16_t)(G.mach == M_LFO || G.mach == M_FX ? 882 : 441); }
static void preview_update(void)
{
    const uint8_t *p = edit_patch();
    uint32_t h = 2166136261u;
    int i, note = G.job_on && JOBS[G.job].note ? JOBS[G.job].note : 60;
    uint16_t bin = view_bin();
    for (i = 0; i < NP; i++) h = (h ^ p[i]) * 16777619u;
    h = (h ^ bin) * 16777619u;
    h = (h ^ (uint32_t)note) * 16777619u;
    if (bin != zeit_bin) { zeit_bin = bin; zeit_restart = 1; }
    if (h != G.pv_hash) { G.pv_hash = h; G.pv_t = G.now; G.pv_done = 0; }
    if (!G.pv_done && G.now - G.pv_t > 180u) {   /* erst, wenn der Regler kurz still steht */
        syn_offline(p, note, G.own_wave, G.own_zeit, bin);
        G.pv_bin = bin;
        G.pv_done = 1;
        if (G.job_on && JOBS[G.job].kind == JK_SOUND && G.tgt_bin != bin) {
            syn_offline(tgt_patch, note, G.tgt_wave, G.tgt_zeit, bin);
            G.tgt_bin = bin;
        }
    }
    if (V[scope_v].s1 != E_IDLE) G.live_until = G.now + 1500u;
}
static int peak8(const int8_t *a, int n)
{
    int i, m = 8;
    for (i = 0; i < n; i++) { int v = a[i] < 0 ? -a[i] : a[i]; if (v > m) m = v; }
    return m;
}
static void draw_scope(int y, int h, int show_own, int ghost)
{
    int i, cy = y + h / 2;
    int amp = h / 2 - 4;
    int live = V[scope_v].s1 != E_IDLE || (int32_t)(G.live_until - G.now) > 0;
    round_box(0, y, GW, h, C_BLK, C_DGRN);
    for (i = 4; i < GW - 4; i += 4) { px(i, cy, C_DGRN); px(i, cy - amp, C_NAVY); px(i, cy + amp, C_NAVY); }
    for (i = 30; i < GW; i += 30) { int yy; for (yy = y + 3; yy < y + h - 3; yy += 4) px(i, yy, C_NAVY); }
    if (!show_own && !ghost) {
        text_c(60, cy - 3, "NUR MIT DEN OHREN!", C_LGRY, 1);
        return;
    }
    if (G.view == 0) {
        /* Welle: jede Spur auf volle Höhe, damit man die Form auch bei leisen Tönen sieht */
        if (ghost) {
            int prev = 0, pk = peak8(G.tgt_wave, 120);
            for (i = 0; i < 116; i++) {
                int yy = cy - G.tgt_wave[i] * amp / pk;
                if (i && (i & 1)) line(2 + i - 1, prev, 2 + i, yy, C_PNK);
                prev = yy;
            }
        }
        if (show_own && V[scope_v].s1 != E_IDLE) {    /* was gerade klingt */
            uint32_t w = scope_w, st = w - 4000u, s;
            float inc = V[scope_v].inc1, span, pk = 0.02f;
            int prev = cy;
            int found = 0;
            span = inc > 0.0f ? 2.0f / inc : 120.0f;   /* zwei ganze Schwingungen */
            if (span > 3800.0f) span = 3800.0f;
            if (span < 30.0f) span = 30.0f;
            for (s = w - (uint32_t)span - 20u; s != st; s--)   /* der letzte Nulldurchgang nach oben, der passt */
                if (scope_buf[(s - 1u) & (SCOPE_N - 1)] <= 0.0f && scope_buf[s & (SCOPE_N - 1)] > 0.0f) { found = 1; break; }
            st = found ? s : w - (uint32_t)span - 10u;
            for (i = 0; i < 116; i++) {
                float v = scope_buf[(st + (uint32_t)((float)i * span / 116.0f)) & (SCOPE_N - 1)];
                if (fabsf_(v) > pk) pk = fabsf_(v);
            }
            for (i = 0; i < 116; i++) {
                float v = scope_buf[(st + (uint32_t)((float)i * span / 116.0f)) & (SCOPE_N - 1)] / pk;
                int yy = cy - (int)(v * (float)amp);
                if (i) { line(2 + i - 1, prev, 2 + i, yy, C_GRN); px(2 + i, yy + 1, C_DGRN); }
                prev = yy;
            }
            if (inc > 0.0f) {
                char b[12];
                put_str(put_int(b, (int)(inc * FAB_SR + 0.5f)), " HZ");
                text(36, y + 2, b, C_GRN, 1);
            }
        } else if (show_own && G.pv_done) {           /* sonst: die Vorschau */
            int prev = cy, pk = peak8(G.own_wave, 120);
            for (i = 0; i < 116; i++) {
                int yy = cy - G.own_wave[i] * amp / pk;
                if (i) line(2 + i - 1, prev, 2 + i, yy, C_GRN);
                prev = yy;
            }
            text(36, y + 2, "VORSCHAU", C_DGRN, 1);
        }
        text(3, y + 2, "WELLE", C_DGRN, 1);
    } else {
        /* Zeit: Lautstärke von links nach rechts; der eigene Ton rollt mit, solange er klingt */
        int base = y + h - 3, top = h - 9, secs = G.pv_bin == 882 ? 2 : 1;
        for (i = 0; i <= 116; i += (secs == 1 ? 29 : 29)) { int yy; for (yy = base; yy > base - 3; yy--) px(2 + i, yy, C_DGRN); }
        if (ghost)
            for (i = 0; i < 116; i++) {
                int g = G.tgt_zeit[i] * top / 128;
                if (g) { px(2 + i, base - g, C_PNK); px(2 + i, base - g + 1, C_PNK); }
            }
        if (show_own && live && zeit_count) {
            /* ab dem Anschlag: bis 116 Abschnitte 1:1, bis 232 zusammengeschoben (Anfang bleibt sichtbar), dann rollend */
            uint32_t n = zeit_count, sc = n > 116u ? 2u : 1u, st = n > 232u ? n - 232u : 0u, k;
            int xx = 2;
            for (k = st; k < n; k += sc, xx++) {
                int a = zeit_ring[k & (ZEIT_R - 1)], b2 = k + 1 < n && sc == 2 ? zeit_ring[(k + 1) & (ZEIT_R - 1)] : 0;
                int g = (a > b2 ? a : b2) * top / 128;
                if (g) rect(xx, base - g, 1, g, C_GRN);
            }
            if (V[scope_v].s1 != E_IDLE) rect(xx, y + 3, 1, h - 6, C_WHT);   /* jetzt */
            if (sc == 2) text(GW - 20, base - 9, secs == 2 ? "4 S" : "2 S", C_DGRN, 1);
        } else if (show_own && G.pv_done) {
            for (i = 0; i < 116; i++) {
                int g = G.own_zeit[i] * top / 128;
                if (g) rect(2 + i, base - g, 1, g, C_DGRN);
                if (g) px(2 + i, base - g, C_GRN);
            }
            text(36, y + 2, "VORSCHAU", C_DGRN, 1);
        }
        text(3, y + 2, "ZEIT", C_DGRN, 1);
        if (!(show_own && live && zeit_count > 116u)) text(GW - 20, base - 9, secs == 2 ? "2 S" : "1 S", C_DGRN, 1);
    }
    if (ghost) text(GW - 32, y + 2, "KUNDE", C_PNK, 1);
}
static void draw_top(const char *t, int c)
{
    rect(0, 0, GW, 9, C_NAVY);
    text(2, 1, t, c, 1);
}
static void draw_status(int y)
{
    const char *ins = story_instr();
    rect(0, y, GW, 9, C_BLK);           /* eine eigene Zeile unter dem Oszi: sie verdeckt nichts */
    if (ins) {
        int n = text_len(ins);
        rect(1, y, GW - 2, 9, C_NAVY);
        if (n <= 18) {
            if (blink(1)) text(2, y + 1, T_RIGHT, C_YEL, 1);
            text(8 + (112 - n * 6) / 2, y + 1, ins, C_YEL, 1);
        } else
            text_c(60, y + 1, ins, blink(1) ? C_YEL : C_ORG, 1);
    } else if (G.job_on) {
        rect(1, y, GW - 2, 9, C_NAVY);
        text(3, y + 1, T_PLAY "KUNDE", C_PNK, 1);
        text(62, y + 1, T_REC "LIEFERN", C_ORG, 1);
    } else {
        text(2, y + 1, "SEL: WELLE/ZEIT", C_DGRY, 1);
        if (G.kb_oct) {
            char o[8] = "OCT";
            o[3] = G.kb_oct > 0 ? '+' : '-'; o[4] = (char)('0' + (G.kb_oct > 0 ? G.kb_oct : -G.kb_oct)); o[5] = 0;
            text(GW - 32, y + 1, o, C_ORG, 1);
        }
    }
}
/* wie nah der Klang schon ist (Geschichte und "leicht"): 0..3 Sterne, laufend */
static int live_match(void)
{
    int w;
    if (!G.job_on || JOBS[G.job].kind != JK_SOUND || (G.free && G.diff != D_LEICHT)) return -1;
    return compare(tgt_patch, bench, &w);
}
static void draw_work(void)
{
    char t[32];
    const uint8_t *p = edit_patch();
    int show_own = 1, ghost = G.job_on, m = live_match();
    preview_update();
    if (G.job_on && !G.in_story && G.diff == D_SCHWER) show_own = ghost = 0;
    put_str(put_str(put_str(put_str(t, MACH[G.mach].name), " ("), MACH[G.mach].en), ")");
    if (G.mach == M_OSC1 && p[P_DRONE]) put_str(t, "DER KLOPFER");
    if (m >= 0) put_str(t, MACH[G.mach].name);
    draw_top(t, C_WHT);
    if (m >= 0) {
        int k;
        for (k = 0; k < 3; k++) text(GW - 20 + k * 6, 1, T_STAR, k < m ? C_YEL : C_DGRY, 1);
    }
    draw_band(p, 10);
    draw_scope(23, 52, show_own, ghost);
    draw_status(75);
    draw_knobs(p, 84);
    if (G.dlg_on) draw_dialog(60);
    if (G.confirm_home) { round_box(10, 40, 100, 20, C_RED, C_WHT); text_c(60, 47, "HOME = ZURÜCK", C_WHT, 1); }
}

/* ------------------------------------------------------------------ Schritte (Takt und Live) --- */
static const uint8_t TR_COL[NTR] = {C_RED, C_ORG, C_YEL, C_BLU, C_GRN, C_PNK};
static const char *const TR_NAME[NTR] = {"TROMMEL", "KLATSCH", "ZISCH", "BASS", "FREI 1", "FREI 2"};
static const char *sound_name(int idx)
{
    if (idx < NPRESETS) return PRESETS[idx].name;
    idx -= NPRESETS;
    return idx < NJOBS ? JOBS[idx].name : "?";
}
/* das Tonlager: Grundklänge und die Klänge der geschafften Aufträge */
static int sound_next(int idx, int dir)
{
    int n = NPRESETS + NJOBS, i;
    for (i = 0; i < n; i++) {
        idx = (idx + dir + n) % n;
        if (idx < NPRESETS) return idx;
        if (JOBS[idx - NPRESETS].kind == JK_SOUND && get_stars(&CUR, idx - NPRESETS)) return idx;
    }
    return idx;
}
static void sound_load(int t, int idx)
{
    if (idx < NPRESETS) make_patch(CUR.song.patch[t], PRESETS[idx].set);
    else make_patch(CUR.song.patch[t], JOBS[idx - NPRESETS].set);
}
static void steps_input(void)
{
    int k;
    song_t *s = &CUR.song;
    for (k = 0; k < FAB_NKEYS; k++) {
        int wi = white_index(k), bi = black_index(k);
        if (G.kpress >> k & 1u) {
            if (wi >= 0) { G.held_step = wi; G.step_edited = 0; }
            else if (bi >= 0 && bi < NTR) { G.track = bi; sfx(SFX_BLIP, 72 + bi * 2); scope_v = (uint8_t)bi; seq_fx_from = (uint8_t)bi; }
        }
        if (G.krel >> k & 1u && wi >= 0 && wi == G.held_step) {
            if (!G.step_edited) {
                s->on[G.track] ^= (uint16_t)(1u << wi);
                if (s->on[G.track] >> wi & 1u && !seq_play) syn_on(G.track, 60 + s->note[G.track][wi], 100);
            }
            G.held_step = -1;
        }
    }
    if (G.held_step >= 0 && G.enc[FAB_E_K1]) {
        int n = s->note[G.track][G.held_step] + (G.enc[FAB_E_K1] > 0 ? 1 : -1);
        if (n < -24) n = -24;
        if (n > 24) n = 24;
        s->note[G.track][G.held_step] = (int8_t)n;
        s->on[G.track] |= (uint16_t)(1u << G.held_step);
        G.step_edited = 1;
        syn_on(G.track, 60 + n, 100);
    }
    if (G.enc[FAB_E_K2]) {
        int b = s->bpm + G.enc[FAB_E_K2] * 2;
        s->bpm = (uint8_t)(b < 50 ? 50 : b > 180 ? 180 : b);
    }
    if (G.enc[FAB_E_K3]) {
        int l = s->level[G.track] + G.enc[FAB_E_K3] * 3;
        s->level[G.track] = (uint8_t)(l < 0 ? 0 : l > 127 ? 127 : l);
    }
    if (G.enc[FAB_E_K4]) {
        int w = s->swing + G.enc[FAB_E_K4];
        s->swing = (uint8_t)(w < 50 ? 50 : w > 75 ? 75 : w);
    }
    if (G.enc[FAB_E_SELECT] || G.enc[FAB_E_ALGO]) {
        int d = G.enc[FAB_E_SELECT] + G.enc[FAB_E_ALGO];
        G.track = (G.track + (d > 0 ? 1 : -1) + NTR) % NTR;
        scope_v = (uint8_t)G.track; seq_fx_from = (uint8_t)G.track;
    }
    if (G.enc[FAB_E_PRESET] && G.track >= 4) {
        int i = G.track - 4;
        s->sound[i] = (uint8_t)sound_next(s->sound[i], G.enc[FAB_E_PRESET] > 0 ? 1 : -1);
        sound_load(G.track, s->sound[i]);
        syn_on(G.track, 60, 100);
    }
    if (G.bpress >> FAB_B_SEL & 1u && !G.in_story) s->mute ^= (uint8_t)(1u << G.track);
    if (G.bpress >> FAB_B_PLAY & 1u) {
        if (seq_play) { seq_play = 0; syn_alloff(); }
        else { seq_step = 255; seq_acc = 0; seq_play = 1; }
    }
}
static void draw_steps(int y, const uint16_t *ghost_on)
{
    int t, i;
    song_t *s = &CUR.song;
    for (t = 0; t < NTR; t++) {
        int yy = y + t * 9, sel = t == G.track, mute = s->mute >> t & 1u;
        if (sel) rect(0, yy - 1, GW, 9, C_NAVY);
        rect(1, yy, 6, 7, mute ? C_DGRY : TR_COL[t]);
        if (sel) text(1, yy, T_RIGHT, C_BLK, 1);
        for (i = 0; i < 16; i++) {
            int x = 9 + i * 7, on = s->on[t] >> i & 1u, c;
            int ph = seq_play && seq_step == i;
            c = on ? (mute ? C_LGRY : TR_COL[t]) : ((i >> 2) & 1) ? C_DGRY : C_BLK + 1;
            if (!on && ghost_on && ghost_on[t] >> i & 1u && blink(2)) c = C_LAV;
            rect(x, yy, 6, 7, c);
            if (ph) frame(x - 1, yy - 1, 8, 9, C_WHT);
            if (on && s->note[t][i] && !mute) px(x + 2, yy + 3 - (s->note[t][i] > 0 ? 1 : -1), C_BLK);
        }
    }
}
static void draw_step_knobs(int y)
{
    char b[16];
    song_t *s = &CUR.song;
    rect(0, y, GW, GH - y, C_NAVY);
    text_c(15, y + 2, "TON", G.held_step >= 0 ? C_LGRY : C_DGRY, 1);
    if (G.held_step >= 0) { int n = s->note[G.track][G.held_step]; if (n > 0) put_str(b, "+"); put_int(b + (n > 0), n); }
    else put_str(b, "-");
    text_c(15, y + 18, b, G.held_step >= 0 ? C_GRN : C_DGRY, 1);
    text_c(45, y + 2, "TEMPO", C_LGRY, 1);
    put_int(b, s->bpm);
    text_c(45, y + 12, b, C_GRN, 2);
    text_c(45, y + 28, "BPM", C_DGRY, 1);
    text_c(75, y + 2, "LAUT", C_LGRY, 1);
    draw_knob(75, y + 17, s->level[G.track] * 1000 / 127, C_YEL);
    text_c(105, y + 2, "SWING", C_LGRY, 1);
    put_int(b, s->swing);
    text_c(105, y + 18, b, C_GRN, 1);
}
static void draw_live(void)
{
    char t[40];
    song_t *s = &CUR.song;
    if (G.live_fab) {
        draw_work();
        put_str(put_str(t, "SPUR "), TR_NAME[G.track]);
        rect(0, 0, GW, 9, C_NAVY);
        text(2, 1, t, TR_COL[G.track], 1);
        text(GW - 38, 1, G.rec ? T_REC "AUF" : "SEQ" T_RIGHT, G.rec ? C_RED : C_DGRY, 1);
        return;
    }
    rect(0, 0, GW, GH, C_BLK);
    draw_top(G.in_story ? "SCHRITTMASCHINE" : "LIVE", C_WHT);
    text(GW - 14, 1, seq_play ? T_PLAY : "II", seq_play ? C_GRN : C_LGRY, 1);
    draw_steps(12, 0);
    rect(0, 66, GW, 15, C_BLK);
    put_str(t, TR_NAME[G.track]);
    text(2, 67, t, TR_COL[G.track], 1);
    if (G.track >= 4) {
        put_str(put_str(t, T_LEFT), sound_name(s->sound[G.track - 4]));
        text(52, 67, t, C_WHT, 1);
        text(GW - 7, 67, T_RIGHT, C_WHT, 1);
    }
    if (s->mute >> G.track & 1u) text(52, 67, "STUMM", C_RED, 1);
    {
        const char *ins = story_instr();
        if (G.job_on) text(2, 75, "GLO=INFO " T_REC "=LIEFERN", C_ORG, 1);
        else if (ins) text(2, 75, ins, blink(1) ? C_YEL : C_ORG, 1);
        else if (!G.in_story) text(2, 75, "SEL:STUMM EDIT:BAUEN", C_DGRY, 1);
    }
    draw_step_knobs(82);
}

/* ------------------------------------------------------------------ Titel, Plätze, Menü, Karte --- */
static void draw_factory(int x, int y)
{
    int i;
    rect(x, y + 10, 40, 22, C_BRN);
    for (i = 0; i < 4; i++) {           /* Sägezahndach: auch die Fabrik ist eine Welle */
        line(x + i * 10, y + 10, x + i * 10 + 9, y + 4, C_ORG);
        line(x + i * 10 + 9, y + 4, x + i * 10 + 9, y + 10, C_ORG);
    }
    rect(x + 30, y - 6, 5, 12, C_DGRY);
    for (i = 0; i < 3; i++) rect(x + 4 + i * 12, y + 16, 7, 6, C_YEL);
    rect(x + 16, y + 24, 8, 8, C_NAVY);
    {
        int k;
        for (k = 0; k < 3; k++) {           /* Noten steigen aus dem Schornstein */
            int ph = (int)((G.fr + (uint32_t)k * 40u) % 120u);
            int nx = x + 32 + (int)(sin1((float)ph / 40.0f) * 4.0f), ny = y - 8 - ph / 4;
            text(nx, ny, T_NOTE, ph < 60 ? C_WHT : C_LGRY, 1);
        }
    }
}
static void draw_title(void)
{
    int i;
    rect(0, 0, GW, GH, C_NAVY);
    for (i = 0; i < 30; i++) px((int)((uint32_t)i * 37u % 120u), (int)((uint32_t)i * 23u % 60u), C_LAV);
    rect(0, 90, GW, 30, C_DGRN);
    draw_factory(14, 56);
    spr2(&(sprite_t){16, 22, (G.fr / 30u) & 1 ? VOGL0 : VOGL2}, 70, 46);
    text_sh(9, 8, "TON", C_YEL, C_PLUM, 3);
    text_sh(9, 32, "FABRIK", C_ORG, C_PLUM, 2);
    if ((G.fr / 20u) & 1u) text_c(60, 106, "SEL DRÜCKEN", C_WHT, 1);
}
static const uint8_t SLOT_ICON[3] = {CL_KATZ, CL_HUND_, CL_FROS};
static void draw_slots(void)
{
    int i;
    rect(0, 0, GW, GH, C_NAVY);
    draw_top("WER SPIELT?", C_YEL);
    for (i = 0; i < FAB_NSLOTS; i++) {
        int y = 14 + i * 32, sel = i == G.cur;
        char b[24];
        round_box(4, y, 112, 28, sel ? C_PLUM : C_BLK, sel ? C_YEL : C_DGRY);
        client_icon(SLOT_ICON[i], 8, y + 2, 1);
        if (dev_mode && i == DEV_SLOT) {
            text(38, y + 5, "ENTWICKLER", C_ORG, 1);
            text(38, y + 15, "ALLES OFFEN", C_LGRY, 1);
        } else if (!SL[i].used) {
            text(38, y + 10, "NEU", sel ? C_WHT : C_LGRY, 1);
        } else {
            put_int(put_str(b, "KAPITEL "), SL[i].ch_done < NCH ? SL[i].ch_done + 1 : NCH);
            text(38, y + 5, b, C_WHT, 1);
            put_int(put_str(b, T_STAR " "), slot_stars(&SL[i]));
            text(38, y + 15, b, C_YEL, 1);
        }
    }
    text_c(60, 112, "LÖSCHEN: SAVE 3 S", C_DGRY, 1);
}
typedef struct { const char *name; const char *const *ic; int need; } menu_t;
static const menu_t MENU[4] = {
    {"GESCHICHTE", IC_BOOK, 0}, {"AUFTRÄGE", IC_FX, 2}, {"MINISPIELE", IC_GAME, 1}, {"LIVE", IC_SEQ, 9}};
static void draw_menu(void)
{
    int i;
    rect(0, 0, GW, GH, C_NAVY);
    draw_top("TONFABRIK", C_YEL);
    client_icon(SLOT_ICON[G.slot], GW - 14, 0, 0);
    for (i = 0; i < 4; i++) {
        int y = 14 + i * 25, sel = i == G.cur, open = CUR.ch_done >= MENU[i].need;
        round_box(8, y, 104, 21, sel ? C_PLUM : C_BLK, sel ? C_YEL : C_DGRY);
        icon(open ? MENU[i].ic : IC_LOCK, 11, 9, 14, y + 6, open ? C_WHT : C_DGRY);
        text(30, y + 7, MENU[i].name, open ? C_WHT : C_DGRY, 1);
    }
    if (CUR.ch_done < MENU[G.cur].need) {
        char b[32];
        put_int(put_str(b, "NACH KAPITEL "), MENU[G.cur].need);
        text_c(60, 112, b, C_ORG, 1);
    }
}
static void draw_map(void)
{
    int i;
    char b[24];
    rect(0, 0, GW, GH, C_BLK);
    draw_top("DIE FABRIKHALLEN", C_YEL);
    for (i = 0; i < NCH; i++) {
        int x = 2 + (i % 4) * 29, y = 12 + (i / 4) * 30, sel = i == G.cur, open = i <= CUR.ch_done, mx;
        int st = ch_stars(&CUR, i + 1, &mx);
        round_box(x, y, 27, 27, sel ? C_PLUM : open ? C_NAVY : C_BLK, sel ? C_YEL : open ? C_LAV : C_DGRY);
        put_int(b, i + 1);
        text_c(x + 14, y + 4, b, open ? C_WHT : C_DGRY, 2);
        if (!open) icon(IC_LOCK, 11, 9, x + 9, y + 18, C_DGRY);
        else if (i < CUR.ch_done) {
            int s3 = mx ? (st * 3 + mx - 1) / mx : 3;
            int k;
            for (k = 0; k < 3; k++) text(x + 2 + k * 8, y + 19, T_STAR, k < s3 ? C_YEL : C_DGRY, 1);
        }
    }
    text_c(60, 104, CH_NAME[G.cur], G.cur <= CUR.ch_done ? C_WHT : C_DGRY, 1);
    text_c(60, 112, G.cur <= CUR.ch_done ? "SEL = LOS" : "NOCH ZU", C_DGRY, 1);
}
static void draw_diff(void)
{
    static const char *const N[3] = {"LEICHT", "MITTEL", "SCHWER"};
    static const char *const D[3] = {"OSZI + TIPPS", "NUR OSZI", "NUR OHREN"};
    int i;
    rect(0, 0, GW, GH, C_NAVY);
    draw_top("AUFTRÄGE", C_YEL);
    for (i = 0; i < 3; i++) {
        int y = 16 + i * 30, sel = i == G.cur;
        round_box(8, y, 104, 26, sel ? C_PLUM : C_BLK, sel ? C_YEL : C_DGRY);
        text(14, y + 4, N[i], C_WHT, 1);
        text(14, y + 14, D[i], C_LGRY, 1);
        {
            int k;
            for (k = 0; k <= i; k++) text(92 + k * 6, y + 4, T_STAR, C_YEL, 1);
        }
    }
    {
        char b[24];
        put_int(put_str(b, T_STAR " PUNKTE: "), CUR.score);
        text_c(60, 110, b, C_YEL, 1);
    }
}
static void draw_games_menu(void)
{
    static const char *const N[3] = {"HOCH ODER TIEF?", "WELCHE WELLE?", "SPIEL MIR NACH!"};
    static const int J[3] = {J_HOCHTIEF, J_WELLEN, J_NACHSPIEL};
    int i;
    rect(0, 0, GW, GH, C_NAVY);
    draw_top("MINISPIELE", C_YEL);
    for (i = 0; i < 3; i++) {
        int y = 16 + i * 30, sel = i == G.cur, open = CUR.ch_done >= JOBS[J[i]].ch;
        int k, st = get_stars(&CUR, J[i]);
        round_box(6, y, 108, 26, sel ? C_PLUM : C_BLK, sel ? C_YEL : C_DGRY);
        text(12, y + 4, N[i], open ? C_WHT : C_DGRY, 1);
        if (!open) icon(IC_LOCK, 11, 9, 96, y + 12, C_DGRY);
        else for (k = 0; k < 3; k++) text(12 + k * 8, y + 15, T_STAR, k < st ? C_YEL : C_DGRY, 1);
    }
}

/* ------------------------------------------------------------------ Anruf und Ergebnis --- */
static void draw_call(void)
{
    const job_t *j = &JOBS[G.job];
    rect(0, 0, GW, GH, C_NAVY);
    if (G.call_phase == 0) {
        int sh = (G.fr / 3u) & 1u ? 1 : -1;
        draw_top(G.free ? "NEUER AUFTRAG" : "AUFTRAG", C_YEL);
        client_icon(CL_TELE, 48 + sh, 30, 1);
        if ((G.fr / 15u) & 1u) text_c(60, 62, "RING RING!", C_WHT, 2);
        if ((G.fr / 15u) & 1u) text_c(60, 100, "SEL = ABNEHMEN", C_YEL, 1);
    } else {
        draw_top(j->who, C_YEL);
        client_icon(j->client, 48, 14, 1);
        if (G.dlg_on) draw_dialog(52);
    }
}
static void draw_bigstar(int cx, int cy, int on, int pop)
{
    int s = pop ? 4 : 3;
    text(cx - 3 * s + (on ? 0 : 0), cy - 3 * s, T_STAR, on ? C_YEL : C_DGRY, s);
}
static void draw_result(void)
{
    const job_t *j = &JOBS[G.job];
    int i, t = (int)(G.now - G.res_t0);
    rect(0, 0, GW, GH, C_NAVY);
    draw_top(j->kind == JK_GAME ? "GESCHAFFT!" : "GELIEFERT!", C_YEL);
    if (j->kind != JK_GAME) client_icon(j->client, 4, 14, 1);
    for (i = 0; i < 3; i++) {
        int on = i < G.last_stars && t > 400 + i * 400;
        draw_bigstar(48 + i * 26 + (j->kind == JK_GAME ? -10 : 0), 26, on, on && t < 400 + i * 400 + 150);
    }
    if (G.dlg_on) draw_dialog(52);
    else if ((G.fr / 15u) & 1u) text_c(60, 108, "SEL = WEITER", C_YEL, 1);
}

/* ------------------------------------------------------------------ Minispiele --- */
static void g_tone(int note, int wave)
{
    make_patch(tgt_patch, 0);
    tgt_patch[P_W1] = (uint8_t)wave;
    tgt_patch[P_D1] = 70; tgt_patch[P_S1] = 90; tgt_patch[P_R1] = 30;
    syn_off(V_TARGET);
    syn_on(V_TARGET, note, 110);
    G.tgt_off_at = G.now + 450u;
}
static void game_finish(void)
{
    int st;
    if (G.g_kind == G_NACHSPIEL) st = G.g_len >= 7 ? 3 : G.g_len >= 6 ? 2 : G.g_len >= 5 ? 1 : 0;
    else st = G.g_bad == 0 ? 3 : G.g_bad == 1 ? 2 : G.g_bad <= 2 ? 1 : 0;
    G.last_stars = st;
    if (st) {
        put_stars(&CUR, G.job, st);
        save_mask |= (uint8_t)(1u << G.slot);
    }
    G.res_t0 = G.now;
    go(SC_RESULT);
    if (st) { say(JOBS[G.job].thanks, 0, 1); sfx(SFX_STAR, 84); }
    else { say("DAS WAR KNAPP. KOMM, NOCH EINMAL!", 0, 0); sfx(SFX_BAD, 60); }
}
static void game_input(void)
{
    int k;
    if (G.dlg_on) return;
    switch (G.g_kind) {
    case G_HOCHTIEF:
        /* Phase 0: zwei Töne spielen; 1: Antwort; 2: Rückmeldung */
        if (G.g_phase == 0) {
            static const int IV[5] = {12, 7, 5, 3, 2};
            uint32_t t = G.now - G.g_t0;
            if (G.g_pos == 0) {
                int iv = IV[G.g_round % 5];
                if (!G.g_len) {          /* (g_len 0: dieselben zwei Töne noch einmal) */
                    G.g_a = 60 + (int)(rnd() % 8u);
                    G.g_b = G.g_a + ((rnd() & 1u) ? iv : -iv);
                }
                g_tone(G.g_a, W_TRI);
                G.g_pos = 1;
            } else if (G.g_pos == 1 && t >= 700u) { g_tone(G.g_b, W_TRI); G.g_pos = 2; }
            else if (G.g_pos == 2 && t >= 1300u) G.g_phase = 1;
        } else if (G.g_phase == 1) {
            int up = G.bpress >> FAB_B_OCTUP & 1u, dn = G.bpress >> FAB_B_OCTDN & 1u;
            if (G.bpress >> FAB_B_PLAY & 1u) { G.g_phase = 0; G.g_t0 = G.now; G.g_pos = 0; G.g_len = 1; }
            if (up || dn) {
                int right = (G.g_b > G.g_a) == (up != 0);
                if (right) { G.g_ok++; sfx(SFX_STAR, 84); } else { G.g_bad++; sfx(SFX_BAD, 60); }
                G.g_sel = right;
                G.g_phase = 2;
                G.g_t0 = G.now;
            }
        } else if (G.now - G.g_t0 > 900u) {
            if (++G.g_round >= 5) { game_finish(); return; }
            G.g_phase = 0;
            G.g_pos = 0;
            G.g_len = 0;
            G.g_t0 = G.now;
        }
        break;
    case G_WELLEN:
        if (G.g_phase == 0) {
            G.g_a = (int)(rnd() % 4u);
            if (G.g_round == 4) G.g_a = W_NOISE - 1 + (int)(rnd() % 2u);   /* auch mal Rauschen */
            g_tone(57 + (int)(rnd() % 6u), G.g_a);
            G.tgt_off_at = G.now + 900u;
            G.g_phase = 1;
        } else if (G.g_phase == 1) {
            int d = G.enc[FAB_E_SELECT] + G.enc[FAB_E_K1];
            if (d) { G.g_sel = (G.g_sel + (d > 0 ? 1 : -1) + 5) % 5; sfx(SFX_BLIP, 76 + G.g_sel * 2); }
            if (G.bpress >> FAB_B_PLAY & 1u) { g_tone(60, G.g_a); G.tgt_off_at = G.now + 900u; }
            if (G.bpress >> FAB_B_SEL & 1u) {
                int right = G.g_sel == G.g_a;
                if (right) { G.g_ok++; sfx(SFX_STAR, 84); } else { G.g_bad++; sfx(SFX_BAD, 60); }
                G.g_b = right;
                G.g_phase = 2;
                G.g_t0 = G.now;
            }
        } else if (G.now - G.g_t0 > 1000u) {
            if (++G.g_round >= 5) { game_finish(); return; }
            G.g_phase = 0;
        }
        break;
    default:                            /* NACHSPIEL: Lichter zeigen, das Kind spielt nach */
        if (G.g_phase == 0) {
            int i;
            for (i = 0; i < 8; i++) G.g_seq[i] = (uint8_t)(4 + rnd() % 8u);   /* weiße Tasten 4..11 */
            G.g_phase = 1; G.g_pos = 0; G.g_t0 = G.now; G.g_a = -1;
        } else if (G.g_phase == 1) {    /* vorspielen */
            uint32_t t = G.now - G.g_t0;
            int i = (int)(t / 550u);
            if (i < G.g_len && i != G.g_a) { g_tone(53 + white_key[G.g_seq[i]], W_TRI); G.g_a = i; }
            if (i >= G.g_len) { G.g_phase = 2; G.g_pos = 0; }
        } else if (G.g_phase == 2) {    /* nachspielen */
            for (k = 0; k < FAB_NKEYS; k++)
                if (G.kpress >> k & 1u) {
                    int wi = white_index(k);
                    g_tone(53 + k, W_TRI);
                    if (wi == G.g_seq[G.g_pos]) {
                        if (++G.g_pos >= G.g_len) {
                            sfx(SFX_STAR, 84);
                            G.g_len++;
                            G.g_phase = 3; G.g_t0 = G.now; G.g_b = 1;
                            if (G.g_len > 8) { G.g_len = 8; game_finish(); return; }
                        }
                    } else {
                        sfx(SFX_BAD, 60);
                        G.g_phase = 3; G.g_t0 = G.now; G.g_b = 0;
                    }
                    break;
                }
        } else if (G.now - G.g_t0 > 1000u) {
            if (!G.g_b) { game_finish(); return; }
            G.g_phase = 1; G.g_t0 = G.now; G.g_a = -1;
        }
        break;
    }
}
static void draw_game(void)
{
    char b[24];
    int i;
    rect(0, 0, GW, GH, C_BLK);
    draw_top(G.g_kind == G_HOCHTIEF ? "HOCH ODER TIEF?" : G.g_kind == G_WELLEN ? "WELCHE WELLE?" : "SPIEL MIR NACH!", C_YEL);
    if (G.g_kind != G_NACHSPIEL)
        for (i = 0; i < 5; i++) text(32 + i * 12, 12, i < G.g_round ? T_STAR : "-", i < G.g_round ? C_YEL : C_DGRY, 1);
    switch (G.g_kind) {
    case G_HOCHTIEF: {
        uint32_t t = G.now - G.g_t0;
        int a = G.g_phase == 0 && t < 600u, bb = G.g_phase == 0 && t >= 700u;
        text(28, 34, T_NOTE, a ? C_WHT : C_DGRY, 3);
        text(70, 34, T_NOTE, bb ? C_WHT : C_DGRY, 3);
        text(46, 34, T_RIGHT, C_DGRY, 2);
        if (G.g_phase == 1) {
            text_c(60, 64, "DER ZWEITE TON IST...", C_WHT, 1);
            text(4, 80, "OCT-", C_BLU, 1); text(4, 90, "TIEFER", C_BLU, 1);
            text(84, 80, "OCT+", C_ORG, 1); text(80, 90, "HÖHER", C_ORG, 1);
            text_c(60, 108, T_PLAY " = NOCHMAL", C_DGRY, 1);
        } else if (G.g_phase == 2)
            text_c(60, 72, G.g_sel ? "RICHTIG!" : "OH NEIN!", G.g_sel ? C_GRN : C_RED, 2);
        else text_c(60, 72, "HÖR GUT ZU...", C_LGRY, 1);
        break;
    }
    case G_WELLEN:
        for (i = 0; i < 5; i++) {
            int x = 4 + i * 23, sel = i == G.g_sel;
            round_box(x, 40, 21, 21, sel ? C_PLUM : C_NAVY, sel ? C_YEL : C_DGRY);
            mini_wave(i, x + 6, 48, sel ? C_WHT : C_LGRY);
        }
        if (G.g_phase == 2) text_c(60, 72, G.g_b ? "RICHTIG!" : "OH NEIN!", G.g_b ? C_GRN : C_RED, 2);
        else {
            static const char *const WN[5] = {"SINUS", "DREIECK", "SÄGE", "RECHTECK", "RAUSCHEN"};
            text_c(60, 68, WN[G.g_sel], C_WHT, 1);
            text_c(60, 92, "SELECT DREHEN, SEL", C_DGRY, 1);
            text_c(60, 102, T_PLAY " = NOCHMAL", C_DGRY, 1);
        }
        break;
    default:
        put_int(put_str(b, "LÄNGE "), G.g_len);
        text_c(60, 20, b, C_WHT, 1);
        for (i = 0; i < G.g_len; i++) {
            int on = G.g_phase == 2 ? i < G.g_pos : G.g_phase == 1 ? (int)((G.now - G.g_t0) / 550u) == i : 0;
            text(60 - G.g_len * 5 + i * 10, 40, T_NOTE, on ? C_YEL : C_DGRY, 1);
        }
        text_c(60, 64, G.g_phase == 1 ? "SCHAU UND HÖR ZU..." : G.g_phase == 2 ? "JETZT DU!" : G.g_b ? "SUPER!" : "UPS!",
               G.g_phase == 3 && !G.g_b ? C_RED : C_WHT, 1);
        text_c(60, 100, "DIE TASTEN LEUCHTEN", C_DGRY, 1);
        break;
    }
    if (G.dlg_on) draw_dialog(60);
}

static void draw_end(void)
{
    int i;
    rect(0, 0, GW, GH, C_PEACH);
    frame(2, 2, GW - 4, GH - 4, C_BRN);
    frame(4, 4, GW - 8, GH - 8, C_ORG);
    text_c(60, 10, "URKUNDE", C_BRN, 2);
    text_c(60, 30, "MEISTERIN ODER", C_BLK, 1);
    text_c(60, 39, "MEISTER DER", C_BLK, 1);
    text_c(60, 48, "TONFABRIK", C_PLUM, 2);
    client_icon(SLOT_ICON[G.slot], 20, 70, 1);
    spr(&SPR_VOGL[(G.fr / 20u) & 1 ? 2 : 0], 80, 72, 1);
    for (i = 0; i < 3; i++) text(48 + i * 9, 76, T_STAR, C_ORG, 1);
    {
        char b[24];
        put_int(put_str(b, T_STAR " "), slot_stars(&CUR));
        text_c(60, 104, b, C_BRN, 1);
    }
}

/* ------------------------------------------------------------------ Eingabe je Bildschirm --- */
static void live_enter(void)
{
    int t;
    G.unlocked_ch = (uint8_t)(CUR.ch_done < 1 ? 1 : CUR.ch_done);
    for (t = 0; t < NTR; t++) V[t].p = CUR.song.patch[t];
    S_song = &CUR.song;
    G.track = 0;
    G.live_fab = 0;
    G.held_step = -1;
    G.rec = 0;
    G.mach = M_OSC1;
    seq_fx_from = 0;
    scope_v = 0;
    go(SC_LIVE);
}
static void deliver(void)
{
    int st, w = -1;
    const job_t *j = &JOBS[G.job];
    if (j->kind == JK_BEAT) {
        const uint8_t *s = j->set;
        st = 3;
        for (; s[0] != END_; s += s[0] >= BPM_MIN ? 2 : 3) {
            if (s[0] == BPM_MIN) { if (CUR.song.bpm < s[1]) st = st > 1 ? 1 : st; }
            else if (s[0] == BPM_MAX) { if (CUR.song.bpm > s[1]) st = st > 1 ? 1 : st; }
            else if (CUR.song.on[s[0]] != (uint16_t)(s[1] | s[2] << 8)) st = 0;
        }
    } else
        st = compare(tgt_patch, bench, &w);
    G.worst = w;
    G.tries++;
    if (!st) {
        sfx(SFX_BAD, 55);
        if (!G.free || G.diff == D_LEICHT)
            say(j->kind == JK_BEAT ? "DAS PASST NOCH NICHT. SCHAU NOCHMAL GENAU HIN!" : hint_for(w), 0, 0);
        else
            say("NOCH NICHT GANZ. HÖR NOCHMAL GENAU HIN!", 0, 0);
        return;
    }
    G.last_stars = st;
    bench[P_DRONE] = 0;
    put_stars(&CUR, G.job, st);
    if (G.free) { CUR.score = (uint16_t)(CUR.score + st * (1 + G.diff)); CUR.played++; }
    save_mask |= (uint8_t)(1u << G.slot);
    syn_alloff();
    seq_play = 0;
    G.res_t0 = G.now;
    go(SC_RESULT);
    say(j->thanks, 1 + j->client, 0);
    G.dlg_who = 1 + j->client;
    sfx(SFX_STAR, 84);
}
static void free_next(void)
{
    int pick[NJOBS], n = 0, j;
    for (j = 0; j < NJOBS; j++)
        if (JOBS[j].kind == JK_SOUND && JOBS[j].ch <= CUR.ch_done && j != G.job) pick[n++] = j;
    if (!n) { go(SC_MENU); return; }
    G.free = 1;
    G.in_story = 0;
    G.unlocked_ch = (uint8_t)CUR.ch_done;
    job_start(pick[rnd() % (uint32_t)n]);
}

static void input_work(void)
{
    int bw;
    if (G.tgt_off_at && (int32_t)(G.now - G.tgt_off_at) >= 0) {
        syn_off(V_TARGET);
        G.tgt_off_at = 0;
        scope_v = G.story_seq ? (uint8_t)G.track : V_BENCH;
    }
    if (G.bpress >> FAB_B_HOME & 1u) {
        if (G.confirm_home) {
            syn_alloff();
            seq_play = 0;
            bench[P_DRONE] = 0;
            G.job_on = 0;
            G.in_story = 0;
            G.story_seq = 0;
            go(G.free ? SC_DIFF : SC_MAP);
            if (!G.free) G.cur = G.ch - 1;
            return;
        }
        G.confirm_home = 1;
        G.home_t = G.now;
    }
    if (G.confirm_home && G.now - G.home_t > 2000u) G.confirm_home = 0;
    if (G.dlg_on) { dlg_input(); if (!G.dlg_on && G.in_story && !G.job_on) { G.pc++; story_next(); } return; }
    bw = story_waiting_btn();
    if (G.story_seq) steps_input();
    else fabrik_input(V_BENCH);
    if (G.job_on) {
        if (G.bpress >> FAB_B_PLAY & 1u && JOBS[G.job].kind == JK_SOUND) tgt_play();
        if (G.bpress >> FAB_B_REC & 1u) deliver();
        if (G.bpress >> FAB_B_GLO & 1u && (!G.free || G.diff == D_LEICHT)) {
            int w;
            if (JOBS[G.job].kind == JK_BEAT) say(JOBS[G.job].call, 0, 0);
            else { compare(tgt_patch, bench, &w); say(hint_for(w), 0, 0); }
        }
        if (G.bpress >> FAB_B_SEL & 1u && !G.story_seq) G.view ^= 1;
    } else if (G.in_story) {
        const op_t *o = &CHAPTERS[G.ch - 1][G.pc];
        if (G.bpress >> FAB_B_SEL & 1u && !G.story_seq && bw != FAB_B_SEL) G.view ^= 1;
        if (story_wait_done(o)) {
            sfx(SFX_STAR, 88);
            G.pc++;
            story_next();
        }
    }
}
static void input_live(void)
{
    if (G.bpress >> FAB_B_HOME & 1u) { seq_play = 0; syn_alloff(); go(SC_MENU); G.cur = 3; save_mask |= (uint8_t)(1u << G.slot); return; }
    if (G.bpress >> FAB_B_SEQ & 1u || (G.bpress >> FAB_B_EDIT & 1u && !G.live_fab)) {
        G.live_fab ^= 1;
        G.held_step = -1;
        if (G.live_fab) set_mach(M_OSC1);
        return;
    }
    if (G.bpress >> FAB_B_SAVE & 1u) { save_mask |= (uint8_t)(1u << G.slot); fab_message("GESPEICHERT", ""); }
    if (G.live_fab) {
        fabrik_input(G.track);
        if (G.bpress >> FAB_B_PLAY & 1u) {
            if (seq_play) { seq_play = 0; syn_alloff(); } else { seq_step = 255; seq_acc = 0; seq_play = 1; }
        }
        if (G.bpress >> FAB_B_REC & 1u) G.rec ^= 1;
        if (G.bpress >> FAB_B_SEL & 1u) G.view ^= 1;
    } else
        steps_input();
}

/* Entwicklermodus: ARP überspringt, was gerade dran ist (Sprechblase, Aufgabe, Auftrag mit 3 Sternen, Minispiel) */
static void dev_skip(void)
{
    const job_t *j;
    if (!DEV || !(G.bpress >> FAB_B_ARP & 1u)) return;
    G.bpress &= ~(1u << FAB_B_ARP);
    sfx(SFX_BLIP, 96);
    switch (G.screen) {
    case SC_WORK:
        if (G.dlg_on) {
            G.dlg_on = 0;
            if (G.in_story && !G.job_on) { G.pc++; story_next(); }
        } else if (G.job_on) {
            j = &JOBS[G.job];
            if (j->kind == JK_SOUND) { int i; for (i = 0; i < NP; i++) bench[i] = tgt_patch[i]; }
            else {
                const uint8_t *s;
                for (s = j->set; s[0] != END_; s += s[0] >= BPM_MIN ? 2 : 3) {
                    if (s[0] == BPM_MIN || s[0] == BPM_MAX) CUR.song.bpm = s[1];
                    else CUR.song.on[s[0]] = (uint16_t)(s[1] | s[2] << 8);
                }
            }
            deliver();
        } else if (G.in_story) { G.pc++; story_next(); }
        break;
    case SC_CALL:
        if (G.call_phase == 0) { syn_off(V_SFX); G.call_phase = 1; say(JOBS[G.job].call, 1 + JOBS[G.job].client, 0); }
        G.dlg_on = 0;                   /* der nächste Schritt öffnet die Werkstatt */
        G.bpress |= 1u << FAB_B_SEL;
        break;
    case SC_GAME:
        G.dlg_on = 0;
        G.g_bad = 0;
        G.g_len = 8;
        game_finish();
        break;
    case SC_RESULT:
        G.dlg_on = 0;
        G.bpress |= 1u << FAB_B_SEL;
        break;
    default:
        break;
    }
}
static void input_screen(void)
{
    int dsel = G.enc[FAB_E_SELECT] + G.enc[FAB_E_K1];
    int ok = G.bpress >> FAB_B_SEL & 1u, back = G.bpress >> FAB_B_HOME & 1u;
    switch (G.screen) {
    case SC_TITLE:
        if (ok || G.kpress) { go(SC_SLOTS); sfx(SFX_STAR, 84); }
        break;
    case SC_SLOTS:
        if (dsel) { G.cur = (G.cur + (dsel > 0 ? 1 : -1) + FAB_NSLOTS) % FAB_NSLOTS; sfx(SFX_BLIP, 76 + G.cur * 3); }
        if (back) go(SC_TITLE);
        if (G.btn >> FAB_B_SAVE & 1u && G.now - G.bdown_at[FAB_B_SAVE] > 3000u && SL[G.cur].used) {
            slot_t *s = &SL[G.cur];
            int i;
            for (i = 0; i < (int)sizeof *s; i++) ((uint8_t *)s)[i] = 0;
            save_mask |= (uint8_t)(1u << G.cur);
            sfx(SFX_BAD, 50);
            G.bdown_at[FAB_B_SAVE] = G.now;
        }
        if (ok) {
            G.slot = G.cur;
            if (!CUR.used) {
                int keep = G.slot;
                slot_t *s = &CUR;
                int i;
                for (i = 0; i < (int)sizeof *s; i++) ((uint8_t *)s)[i] = 0;
                s->magic = SLOT_MAGIC;
                s->used = 1;
                song_default(&s->song);
                G.slot = keep;
                save_mask |= (uint8_t)(1u << G.slot);
                if (DEV) { s->ch_done = NCH; go(SC_MENU); }
                else story_begin(1);
            } else {
                if (DEV) CUR.ch_done = NCH;
                go(SC_MENU);
            }
            sfx(SFX_STAR, 88);
        }
        break;
    case SC_MENU:
        if (dsel) { G.cur = (G.cur + (dsel > 0 ? 1 : -1) + 4) % 4; sfx(SFX_BLIP, 76 + G.cur * 3); }
        if (back) { go(SC_SLOTS); G.cur = G.slot; }
        if (ok) {
            if (CUR.ch_done < MENU[G.cur].need) { sfx(SFX_BAD, 50); break; }
            switch (G.cur) {
            case 0: go(SC_MAP); G.cur = CUR.ch_done < NCH ? CUR.ch_done : NCH - 1; break;
            case 1: go(SC_DIFF); G.cur = CUR.diff; break;
            case 2: go(SC_GAMES); break;
            default: live_enter(); break;
            }
            sfx(SFX_BLIP, 88);
        }
        break;
    case SC_MAP:
        if (dsel) { G.cur = (G.cur + dsel + NCH * 4) % NCH; sfx(SFX_BLIP, 70 + G.cur); }
        if (G.enc[FAB_E_ALGO]) G.cur = (G.cur + (G.enc[FAB_E_ALGO] > 0 ? 4 : -4) + NCH * 4) % NCH;
        if (back) { go(SC_MENU); }
        if (ok) {
            if (G.cur <= CUR.ch_done) { sfx(SFX_STAR, 84); story_begin(G.cur + 1); }
            else sfx(SFX_BAD, 50);
        }
        break;
    case SC_DIFF:
        if (dsel) { G.cur = (G.cur + (dsel > 0 ? 1 : -1) + 3) % 3; sfx(SFX_BLIP, 76 + G.cur * 3); }
        if (back) { go(SC_MENU); G.cur = 1; }
        if (ok) { G.diff = G.cur; CUR.diff = (uint8_t)G.cur; G.job = -1; free_next(); }
        break;
    case SC_GAMES: {
        static const int J[3] = {J_HOCHTIEF, J_WELLEN, J_NACHSPIEL};
        if (dsel) { G.cur = (G.cur + (dsel > 0 ? 1 : -1) + 3) % 3; sfx(SFX_BLIP, 76 + G.cur * 3); }
        if (back) { go(SC_MENU); G.cur = 2; }
        if (ok) {
            if (CUR.ch_done >= JOBS[J[G.cur]].ch) { G.in_story = 0; G.free = 0; job_start(J[G.cur]); }
            else sfx(SFX_BAD, 50);
        }
        break;
    }
    case SC_CALL:
        if (G.call_phase == 0) {
            if (ok || G.now - G.call_t0 > 6000u) {
                syn_off(V_SFX);
                G.call_phase = 1;
                say(JOBS[G.job].call, 1 + JOBS[G.job].client, 0);
            }
        } else {
            dlg_input();
            if (!G.dlg_on) {
                const job_t *j = &JOBS[G.job];
                G.unlocked_ch = (uint8_t)job_chapter();
                G.job_on = 1;
                G.mach = M_OSC1;
                go(SC_WORK);
                if (j->kind == JK_BEAT) {
                    G.story_seq = 1;
                    S_song = &CUR.song;
                    { int t; for (t = 0; t < NTR; t++) V[t].p = CUR.song.patch[t]; }
                } else {
                    job_build();
                    V[V_BENCH].p = bench;
                    scope_v = V_BENCH;
                    G.view = tgt_patch[P_DRONE] ? (tgt_patch[P_HZ] < 64) : (j->ch == 4 ? 1 : 0);
                    if (!G.free && j->ch <= 2 && G.tries == 0)
                        say(T_PLAY " PLAY: SO SOLL ES KLINGEN. BAU ES NACH UND LIEFERE MIT " T_REC " REC!", 0, 0);
                }
            }
        }
        break;
    case SC_RESULT:
        if (G.dlg_on) { dlg_input(); break; }
        if (ok) {
            if (G.last_stars == 0) {     /* Minispiel verloren: noch einmal */
                job_start(G.job);
                break;
            }
            if (G.in_story) back_to_story();
            else if (G.free) free_next();
            else { go(SC_GAMES); }
        }
        break;
    case SC_GAME:
        if (back) {
            syn_alloff();
            if (G.in_story) { G.in_story = 0; go(SC_MAP); G.cur = G.ch - 1; } else go(SC_GAMES);
            break;
        }
        if (G.tgt_off_at && (int32_t)(G.now - G.tgt_off_at) >= 0) { syn_off(V_TARGET); G.tgt_off_at = 0; }
        if (G.dlg_on) { dlg_input(); if (!G.dlg_on) G.g_t0 = G.now; break; }
        game_input();
        break;
    case SC_END:
        if (ok) { go(SC_MENU); }
        break;
    }
}

/* ------------------------------------------------------------------ Lichter je Bildschirm --- */
static void compute_lights(void)
{
    int k, bw;
    uint32_t b = 0, kk = 0, db = 0, dk = 0, mb = 0, mk = 0;
    L_glow = 1;
    switch (G.screen) {
    case SC_TITLE:                      /* Lauflicht über die Tasten */
        kk = 1u << (G.fr / 4u % FAB_NKEYS);
        dk = (1u << FAB_NKEYS) - 1u;
        if (blink(1)) b |= 1u << FAB_B_SEL;
        break;
    case SC_SLOTS: case SC_MENU: case SC_MAP: case SC_DIFF: case SC_GAMES: case SC_END:
        b |= 1u << FAB_B_SEL;
        db |= 1u << FAB_B_HOME;
        if (G.screen == SC_SLOTS) db |= 1u << FAB_B_SAVE;
        dk = 0;
        break;
    case SC_CALL:
        if (G.call_phase == 0) { if (blink(3)) b |= 1u << FAB_B_SEL; if (blink(6)) kk = (1u << FAB_NKEYS) - 1u; }
        else if (G.dlg_shown >= G.dlg_total && blink(2)) b |= 1u << FAB_B_SEL;
        break;
    case SC_RESULT: {
        int t = (int)(G.now - G.res_t0), s;
        for (s = 0; s < G.last_stars; s++)       /* die Sterne als Lauflicht */
            if (t > 400 + s * 400) { int i; for (i = 0; i < 5; i++) kk |= 1u << (white_key[s * 5 + i]); }
        if (t > 400 + G.last_stars * 400 && t < 2400 && blink(4)) kk = (1u << FAB_NKEYS) - 1u;
        if (G.last_stars) b |= FAB_LED_PLAY_GREEN; else if (blink(2)) b |= 1u << FAB_B_PLAY;
        if (G.dlg_shown >= G.dlg_total && blink(2)) b |= 1u << FAB_B_SEL;
        break;
    }
    case SC_GAME:
        if (G.dlg_on) { if (blink(2)) b |= 1u << FAB_B_SEL; break; }
        if (G.g_kind == G_HOCHTIEF && G.g_phase == 1) { b |= 1u << FAB_B_OCTUP | 1u << FAB_B_OCTDN; db |= 1u << FAB_B_PLAY; }
        if (G.g_kind == G_HOCHTIEF && G.g_phase == 2) b |= G.g_sel ? FAB_LED_PLAY_GREEN : 1u << FAB_B_PLAY;
        if (G.g_kind == G_WELLEN && G.g_phase == 1) { if (blink(1)) b |= 1u << FAB_B_SEL; db |= 1u << FAB_B_PLAY; }
        if (G.g_kind == G_WELLEN && G.g_phase == 2) b |= G.g_b ? FAB_LED_PLAY_GREEN : 1u << FAB_B_PLAY;
        if (G.g_kind == G_NACHSPIEL) {
            for (k = 4; k < 12; k++) dk |= 1u << white_key[k];
            if (G.g_phase == 1) {
                uint32_t t = G.now - G.g_t0;
                int i = (int)(t / 550u);
                if (i < G.g_len && t % 550u < 400u) kk |= 1u << white_key[G.g_seq[i]];
            }
            if (G.g_phase == 3) b |= G.g_b ? FAB_LED_PLAY_GREEN : 1u << FAB_B_PLAY;
        }
        kk |= G.keys;
        break;
    case SC_WORK: case SC_LIVE: {
        int fab = G.screen == SC_WORK ? !G.story_seq : G.live_fab;
        if (G.dlg_on) { if (G.dlg_shown >= G.dlg_total && blink(2)) b |= 1u << FAB_B_SEL; break; }
        db |= 1u << FAB_B_HOME;
        if (fab) {
            dk = (1u << FAB_NKEYS) - 1u;
            kk = G.keys;
            if (mach_ok(M_ENV1)) db |= 1u << FAB_B_ENV;
            if (mach_ok(M_LFO)) db |= 1u << FAB_B_LFO;
            if (mach_ok(M_FX)) db |= 1u << FAB_B_FX;
            if (G.mach == M_ENV1 || G.mach == M_ENV2) mb |= 1u << FAB_B_ENV;
            if (G.mach == M_LFO) mb |= 1u << FAB_B_LFO;
            if (G.mach == M_FX) mb |= 1u << FAB_B_FX;
            db |= 1u << FAB_B_OCTUP | 1u << FAB_B_OCTDN | 1u << FAB_B_EDIT | 1u << FAB_B_SEL;
            if (G.kb_oct > 0) mb |= 1u << FAB_B_OCTUP;
            if (G.kb_oct < 0) mb |= 1u << FAB_B_OCTDN;
            /* der Klopfer: die Tasten blinken mit jedem Stoß */
            if (edit_patch()[P_DRONE] && edit_patch()[P_HZ] && klopf_hz(edit_patch()[P_HZ]) < 14.0f) {
                if (V[V_BENCH].ph1 < 0.3f) kk = (1u << FAB_NKEYS) - 1u;
                dk = 0;
            }
            if (G.job_on && JOBS[G.job].kind == JK_SOUND && JOBS[G.job].note) {   /* die Taste des Kunden */
                int tk = JOBS[G.job].note - 53 - 12 * G.kb_oct;
                if (tk >= 0 && tk < FAB_NKEYS && blink(1)) mk |= 1u << tk;
            }
        } else {                        /* Schritte: weiße Tasten, schwarze für die Spuren */
            song_t *s = &CUR.song;
            for (k = 0; k < 16; k++) {
                uint32_t bit = 1u << white_key[k];
                int on = s->on[G.track] >> k & 1u;
                if (on) kk |= bit; else dk |= bit;
                if (seq_play && seq_step == k) { if (on) kk &= ~bit; else kk |= bit; }
            }
            for (k = 0; k < NTR; k++) {
                uint32_t bit = 1u << black_key[k];
                if (k == G.track) kk |= bit;
                else if (!(s->mute >> k & 1u)) { if (trig_flash >> k & 1u) mk |= bit; else dk |= bit; }
            }
            if (seq_play) b |= FAB_LED_PLAY_GREEN; else db |= 1u << FAB_B_PLAY;
            db |= 1u << FAB_B_SEQ;
        }
        if (G.screen == SC_LIVE) { db |= 1u << FAB_B_SEQ | 1u << FAB_B_SAVE; if (G.rec) b |= 1u << FAB_B_REC; else db |= 1u << FAB_B_REC; if (seq_play) b |= FAB_LED_PLAY_GREEN; else db |= 1u << FAB_B_PLAY; }
        if (G.job_on) {
            if (JOBS[G.job].kind == JK_SOUND) b |= 1u << FAB_B_PLAY;
            db |= 1u << FAB_B_REC;
            if (!G.free || G.diff == D_LEICHT) db |= 1u << FAB_B_GLO;
        }
        bw = story_waiting_btn();
        if (bw >= 0) { if (blink(4)) b |= 1u << bw; else b &= ~(1u << bw); }
        break;
    }
    }
    trig_flash = 0;
    L_b = b; L_k = kk; L_db = db & ~b; L_dk = dk & ~kk; L_mb = mb & ~b; L_mk = mk & ~kk;
}

/* ------------------------------------------------------------------ Zeichnen --- */
static uint32_t strip_hash[FAB_STRIPS], dirty_bits;
static void render(void)
{
    int s;
    switch (G.screen) {
    case SC_TITLE: draw_title(); break;
    case SC_SLOTS: draw_slots(); break;
    case SC_MENU: draw_menu(); break;
    case SC_MAP: draw_map(); break;
    case SC_WORK: if (G.story_seq) draw_live(); else draw_work(); break;
    case SC_CALL: draw_call(); break;
    case SC_RESULT: draw_result(); break;
    case SC_GAME: draw_game(); break;
    case SC_DIFF: draw_diff(); break;
    case SC_LIVE: draw_live(); break;
    case SC_GAMES: draw_games_menu(); break;
    default: draw_end(); break;
    }
    if (G.msg_t && G.now - G.msg_t < 1500u) {
        round_box(10, 50, 100, 20, C_GRN, C_WHT);
        text_c(60, 57, G.msg_a, C_BLK, 1);
    }
    if (G.screen == SC_WORK && G.story_seq && G.dlg_on) draw_dialog(60);
    for (s = 0; s < FAB_STRIPS; s++) {      /* nur geänderte Streifen ans Display */
        uint32_t h = 2166136261u;
        int i, n = GW * (GH / FAB_STRIPS);
        const uint8_t *q = fb + s * n;
        for (i = 0; i < n; i++) h = (h ^ q[i]) * 16777619u;
        if (h != strip_hash[s]) { strip_hash[s] = h; dirty_bits |= 1u << s; }
    }
}
