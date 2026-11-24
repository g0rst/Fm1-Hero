/* SPDX-License-Identifier: GPL-3.0-only
 * Die Tonfabrik: ein Lernspiel über Synthesizer für den M-VAVE FM-1. Eine Übersetzungseinheit aus vier Teilen
 * (fab.h sagt, was wo steht). */
#include "fab.h"

#include "synth.inc"
#include "gfx.inc"
#include "story.inc"
#include "game.inc"

/* ------------------------------------------------------------------ Schnittstelle --- */
void fab_init(uint32_t seed)
{
    int k, w = 0, b = 0, i;
    rnd_state = seed ? seed : 0x1234567u;
    for (i = 0; i < (int)sizeof G; i++) ((uint8_t *)&G)[i] = 0;
    save_mask = 0;
    syn_init();
    hints_init();
    for (k = 0; k < FAB_NKEYS; k++) {
        if (is_black(k)) { if (b < 11) black_key[b++] = (uint8_t)k; }
        else if (w < 16) white_key[w++] = (uint8_t)k;
    }
    for (i = 0; i < FAB_NSLOTS; i++) {
        slot_t *s = &SL[i];
        int j;
        for (j = 0; j < (int)sizeof *s; j++) ((uint8_t *)s)[j] = 0;
    }
    make_patch(bench, 0);
    V[V_BENCH].p = bench;
    G.held_note_key = -1;
    G.held_step = -1;
    G.screen = SC_TITLE;
    dirty_bits = (1u << FAB_STRIPS) - 1u;
}

void fab_dev(int on) { dev_mode = (uint8_t)(on != 0); }

void fab_panic(void) { syn_alloff(); seq_play = 0; }

void fab_render(int32_t *lr, uint32_t n, uint32_t gain_q12) { fab_render_core(lr, n, gain_q12); }

void fab_midi_note(uint32_t channel, uint32_t note, uint32_t velocity)
{
    int v = G.screen == SC_LIVE ? G.track : V_BENCH;
    (void)channel;
    if (velocity) v_on(&V[v], (float)(note & 127u), (float)velocity * (1.0f / 127.0f));
    else v_off(&V[v]);
}

void fab_input(uint32_t buttons, uint32_t keys, const int32_t enc[FAB_NE], uint32_t now_ms)
{
    int i;
    G.now = now_ms;
    G.fr++;
    G.bpress = buttons & ~G.btn;
    G.brel = G.btn & ~buttons;
    G.kpress = keys & ~G.keys;
    G.krel = G.keys & ~keys;
    G.btn = buttons;
    G.keys = keys;
    for (i = 0; i < FAB_NB; i++)
        if (G.bpress >> i & 1u) G.bdown_at[i] = now_ms;
    for (i = 0; i < FAB_NE; i++) G.enc[i] = enc[i];
    dev_skip();
    if (G.screen == SC_WORK) input_work();
    else if (G.screen == SC_LIVE) input_live();
    else input_screen();
    compute_lights();
    render();
}

uint32_t fab_dirty(void)
{
    uint32_t d = dirty_bits;
    dirty_bits = 0;
    return d;
}

void fab_draw(uint16_t *out, int strip)
{
    int y, x;
    const uint8_t *src = fb + strip * GW * (GH / FAB_STRIPS);
    for (y = 0; y < GH / FAB_STRIPS; y++) {
        uint16_t *r0 = out + (2 * y) * FAB_W, *r1 = r0 + FAB_W;
        for (x = 0; x < GW; x++) {
            uint16_t c = PAL[src[y * GW + x] & 15];
            r0[2 * x] = r0[2 * x + 1] = r1[2 * x] = r1[2 * x + 1] = c;
        }
    }
}

void fab_lights(uint32_t *buttons, uint32_t *keys, uint32_t *dim_buttons, uint32_t *dim_keys,
                uint32_t *mid_buttons, uint32_t *mid_keys, int *glow)
{
    *buttons = L_b; *keys = L_k; *dim_buttons = L_db; *dim_keys = L_dk;
    *mid_buttons = L_mb; *mid_keys = L_mk; *glow = L_glow;
}

void fab_message(const char *a, const char *b)
{
    int i;
    for (i = 0; a && a[i] && i < 23; i++) G.msg_a[i] = a[i];
    G.msg_a[i] = 0;
    for (i = 0; b && b[i] && i < 23; i++) G.msg_b[i] = b[i];
    G.msg_b[i] = 0;
    G.msg_t = G.now ? G.now : 1;
}

uint32_t fab_store_get(int slot, void *dst, uint32_t max)
{
    uint32_t i;
    if (slot < 0 || slot >= FAB_NSLOTS || max < sizeof(slot_t)) return 0;
    for (i = 0; i < sizeof(slot_t); i++) ((uint8_t *)dst)[i] = ((const uint8_t *)&SL[slot])[i];
    return sizeof(slot_t);
}

int fab_store_set(int slot, const void *src, uint32_t len)
{
    uint32_t i;
    slot_t tmp;
    if (slot < 0 || slot >= FAB_NSLOTS || len != sizeof(slot_t)) return -1;
    for (i = 0; i < len; i++) ((uint8_t *)&tmp)[i] = ((const uint8_t *)src)[i];
    if (tmp.magic != SLOT_MAGIC || !tmp.used || tmp.ch_done > NCH) return -1;
    SL[slot] = tmp;
    return 0;
}

uint32_t fab_save_wanted(void) { return save_mask; }
void fab_store_saved(int slot) { save_mask &= (uint8_t)~(1u << slot); }

static const char *const SCREEN_NAME[] = {"TITEL", "PLATZWAHL", "MENÜ", "KARTE", "WERKSTATT", "ANRUF", "ERGEBNIS",
                                           "MINISPIEL", "AUFTRAGS-MODUS", "LIVE", "MINISPIELE", "URKUNDE"};
int fab_context(char *out, int max)
{
    char b[1024], *e = b;
    int i, n;
    e = put_int(put_str(e, "Platz "), G.slot + 1);
    e = put_str(put_str(e, " | Bildschirm: "), SCREEN_NAME[G.screen]);
    if (G.in_story || G.screen == SC_MAP) {
        int ch = G.screen == SC_MAP ? G.cur + 1 : G.ch;
        e = put_str(put_str(put_int(put_str(e, " | Kapitel "), ch), " "), CH_NAME[ch - 1]);
        if (G.in_story) {
            const op_t *o = &CHAPTERS[G.ch - 1][G.pc];
            static const char *const OPN[] = {"SAGT", "WUSSTEST DU", "FREUT SICH", "NEUE MASCHINE", "WÄHLT", "SETZT",
                                              "NEU", "WARTET AUF REGLER", "WARTET AUF TASTE", "WARTET AUF KNOPF",
                                              "WARTET AUF MASCHINE", "AUFTRAG", "BILD", "SCHRITTE", "WARTET AUF SCHRITTE",
                                              "LEERT", "ENDE"};
            e = put_int(put_str(e, ", Schritt "), G.pc + 1);
            e = put_str(put_str(put_str(e, " ("), OPN[o->op]), ")");
            if (o->t) e = put_str(put_str(put_str(e, ": \""), o->t), "\"");
        }
    }
    if (G.dlg_on && G.dlg_page) {
        int k = 0;
        e = put_str(e, " | Sprechblase: \"");
        while (G.dlg_page[k] && k < 200) *e++ = G.dlg_page[k++];
        e = put_str(e, "\"");
    }
    if (G.job_on || G.screen == SC_CALL || G.screen == SC_RESULT || G.screen == SC_GAME) {
        const job_t *j = &JOBS[G.job];
        e = put_str(put_str(put_str(put_int(put_str(e, " | Auftrag "), G.job), ": "), j->who), "");
        if (j->name[0]) e = put_str(put_str(put_str(e, " ("), j->name), ")");
        if (G.free) e = put_str(put_int(put_str(e, ", Auftrags-Modus Stufe "), G.diff + 1), "");
        if (G.job_on && j->kind == JK_SOUND) {
            int w, st = compare(tgt_patch, bench, &w);
            e = put_int(put_str(e, ", jetzt "), st);
            e = put_str(e, " Sterne");
            if (w >= 0) e = put_int(put_str(e, ", am weitesten weg: Regler "), w);
        }
    }
    if (G.screen == SC_WORK || G.screen == SC_LIVE) {
        const uint8_t *p = edit_patch();
        e = put_str(put_str(e, " | Maschine: "), MACH[G.mach].name);
        e = put_str(e, " | Regler:");
        for (i = 0; i < 4; i++) {
            int prm = knob_param(G.mach, i);
            char v[12];
            if (prm < 0) continue;
            value_text(prm, p[prm], v);
            e = put_str(put_str(put_str(put_str(e, " "), MACH[G.mach].lab[i]), "="), v);
        }
        e = put_str(e, " | Patch:");
        for (i = 0; i < NP; i++) e = put_int(put_str(e, i ? "," : " "), p[i]);
        if (G.job_on && JOBS[G.job].kind == JK_SOUND) {
            e = put_str(e, " | Ziel:");
            for (i = 0; i < NP; i++) e = put_int(put_str(e, i ? "," : " "), tgt_patch[i]);
        }
    }
    n = (int)(e - b);
    if (n > max - 1) n = max - 1;
    for (i = 0; i < n; i++) out[i] = b[i];
    out[n] = 0;
    return n;
}

int fab_state(int what)
{
    if (what >= FAB_Q_PARAM && what < FAB_Q_PARAM + NP) return edit_patch()[what - FAB_Q_PARAM];
    switch (what) {
    case FAB_Q_SCREEN: return G.screen;
    case FAB_Q_SLOT: return G.slot;
    case FAB_Q_CHAPTER: return G.ch;
    case FAB_Q_STEP: return G.pc;
    case FAB_Q_MACHINE: return G.mach;
    case FAB_Q_STARS: return slot_stars(&CUR);
    case FAB_Q_PLAYING: return seq_play;
    case FAB_Q_DIALOG: return G.dlg_on;
    case FAB_Q_JOB: return G.job_on ? G.job : -1;
    case FAB_Q_GAME: return G.g_kind == G_HOCHTIEF ? G.g_b > G.g_a : G.g_kind == G_WELLEN ? G.g_a : G.g_phase;
    default: return -1;
    }
}
