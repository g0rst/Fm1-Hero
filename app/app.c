/* SPDX-License-Identifier: GPL-3.0-only
 * Die Tonfabrik auf dem M-VAVE FM-1: der Leim zwischen der Plattform von fm1-base und dem Kern (core/fab.c, als
 * eigene -O2-Einheit gebaut: fm1.toml o2). Teil des Unity-Builds von fm1-base, nach der Plattform: lcd.c und gfx.c
 * sind sichtbar. Aufgebaut wie ro:MINImal (g0rst/ro-MINImal), das auf einem FM-1 läuft. */
#include "core/fab.h"

/* der Kern nennt die Bedienelemente in der Reihenfolge von plat.h */
_Static_assert((int)FAB_B_FX == B_FX && (int)FAB_B_SEL == B_SEL && (int)FAB_B_ENV == B_ENV && (int)FAB_B_LFO == B_LFO &&
               (int)FAB_B_EDIT == B_EDIT && (int)FAB_B_GLO == B_GLO && (int)FAB_B_HOME == B_HOME && (int)FAB_B_SAVE == B_SAVE &&
               (int)FAB_B_ARP == B_ARP && (int)FAB_B_SEQ == B_SEQ && (int)FAB_B_PLAY == B_PLAY && (int)FAB_B_REC == B_REC &&
               (int)FAB_B_OCTDN == B_OCTDN && (int)FAB_B_OCTUP == B_OCTUP && (int)FAB_NB == NB, "Tasten");
_Static_assert((int)FAB_E_SELECT == EN_SELECT && (int)FAB_E_ALGO == EN_ALGO && (int)FAB_E_PRESET == EN_PRESET &&
               (int)FAB_E_K1 == EN_K1 && (int)FAB_E_K4 == EN_K4 && (int)FAB_NE == NE, "Drehregler");
_Static_assert(FAB_NKEYS == NKEYS, "Keys");
_Static_assert(FAB_LED_PLAY_GREEN == LED_PLAY_GREEN, "grünes Licht von PLAY");
_Static_assert(FAB_OBJ_MAX <= PLAT_STORE_MAX, "ein Spielstand passt in ein Speicherobjekt");
_Static_assert(FAB_W * FAB_STRIP_H <= CV_MAX, "ein Streifen passt auf die Leinwand");

/* ---- die Flash-Karte: was die App schreiben darf, beim Übersetzen geprüft ----
 * Jedes Objekt ist ein Paar 4-KiB-Sektoren. Die FM-1-Regeln kommen von fm1-base (plat.h: der Datenbereich; das
 * Staging des Updates und seine Einträge ab 0xE0000), die SMK-37-Regeln vom Porting-Playbook (fm1-to-smk37
 * AGENTS.md, harte Regel 1 und Schritt 3). Diese Asserts sind die Erlaubnisliste; keine davon aufweiten, damit
 * etwas passt. */
#define FAB_PAIR 0x2000u
#define FAB_IN(a, lo, hi) ((a) >= (lo) && (a) + FAB_PAIR <= (hi))
#define FAB_OFF(a, lo, hi) ((a) + FAB_PAIR <= (lo) || (a) >= (hi))
/* nie beschrieben, auf keinem der beiden Boards: Kopf und Bootloader, Bluetooth-Kopplung, die Werkskalibrierung des
 * SMK, die Werksressourcen für den Weg zurück, Schlüssel und MAC */
#define FAB_SAFE(a) (FAB_OFF(a, 0x00000u, 0x04000u) && FAB_OFF(a, 0xC0000u, 0xC2000u) && \
                     FAB_OFF(a, 0xF4000u, 0xFE000u) && FAB_OFF(a, 0xFF000u, 0x100000u))
/* die eigenen Objekte: im Datenbereich von fm1-base (FM-1) und im Fenster des Playbooks (SMK-37) */
#define FAB_OWN(a) (FAB_IN(a, 0x97000u, 0xE0000u) && FAB_IN(a, 0xB1000u, 0xBB000u) && FAB_SAFE(a))
_Static_assert(FAB_OWN(FAB_ADDR_SLOT0) && FAB_OWN(FAB_ADDR_SLOT1) && FAB_OWN(FAB_ADDR_SLOT2),
               "Flash-Karte: ein Spielstand außerhalb des erlaubten Fensters");
_Static_assert(FAB_IN(FAB_ADDR_PANEL, 0x97000u, 0xE0000u) && FAB_SAFE(FAB_ADDR_PANEL),
               "Flash-Karte: die Panel-Kalibrierung außerhalb des FM-1-Datenbereichs");
_Static_assert((FAB_ADDR_SLOT0 | FAB_ADDR_SLOT1 | FAB_ADDR_SLOT2 | FAB_ADDR_PANEL) % 0x1000u == 0u,
               "Flash-Karte: ein Objekt nicht auf einer Sektorgrenze");
_Static_assert(FAB_ADDR_SLOT1 >= FAB_ADDR_SLOT0 + FAB_PAIR && FAB_ADDR_SLOT2 >= FAB_ADDR_SLOT1 + FAB_PAIR &&
               (FAB_ADDR_PANEL >= FAB_ADDR_SLOT2 + FAB_PAIR || FAB_ADDR_PANEL + FAB_PAIR <= FAB_ADDR_SLOT0),
               "Flash-Karte: zwei Objekte überlappen");
_Static_assert(OBJ_NOBJ == 4, "Flash-Karte: ein neues Objekt braucht oben seinen Platz und seine Asserts");

static uint8_t slot_buf[FAB_OBJ_MAX];
static const uint8_t PLAT_OBJ[FAB_NSLOTS] = {OBJ_SLOT0, OBJ_SLOT1, OBJ_SLOT2};

/* ------------------------------------------------------------------ Audio --- */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if ((st & 0xF0u) == 0x90u)
            fab_midi_note(st & 15u, d1, d2);
        else if ((st & 0xF0u) == 0x80u)
            fab_midi_note(st & 15u, d1, 0);
    }
    fab_render(lr, n, master_q12);
}

void app_panic(void) { fab_panic(); }

/* ------------------------------------------------------------------ Start --- */
void app_init(void)
{
    int k, n;
    fab_init(plat_ticks24() ^ 0x544F4E46u);
    for (k = 0; k < FAB_NSLOTS; k++) {          /* jeder für sich: ein kaputter Spielstand lässt die anderen */
        n = plat_store_load(PLAT_OBJ[k], slot_buf, sizeof slot_buf);
        if (n > 0)
            (void)fab_store_set(k, slot_buf, (uint32_t)n);
    }
}

/* ------------------------------------------------------------------ Oberfläche --- */
static void input(void)
{
    int32_t enc[FAB_NE];
    uint32_t i, b, k, db, dk, mb, mk;
    int glow;
    for (i = 0; i < FAB_NE; i++)
        enc[i] = plat_enc((int)i);
    fab_input(plat_buttons(), plat_keys(), enc, plat_ms());
    fab_lights(&b, &k, &db, &dk, &mb, &mk, &glow);
    plat_lights(b, k, db, dk, glow);
    plat_lights_mid(mb, mk);
}

void app_input_only(void) { input(); }

void app_say(const char *a, const char *b) { fab_message(a, b); }

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
}

void app_frame(void)
{
    uint32_t dirty, s, mask;
    int k, failed = 0;
    input();
    mask = fab_save_wanted();                    /* nur die Spielstände, die sich geändert haben */
    for (k = 0; k < FAB_NSLOTS; k++)
        if (mask >> k & 1u) {
            uint32_t n = fab_store_get(k, slot_buf, sizeof slot_buf);
            if (n && plat_store_save(PLAT_OBJ[k], slot_buf, n) == 0)
                fab_store_saved(k);
            else
                failed = 1;
        }
    if (failed)
        fab_message("SPEICHERN", "FEHLER");
    dirty = fab_dirty();
    for (s = 0; s < FAB_STRIPS; s++)
        if (dirty >> s & 1u) {
            uint32_t i;
            cv_begin(FAB_W, FAB_STRIP_H, C_BLACK);
            fab_draw(cv_px, (int)s);
            for (i = 0; i < FAB_W * FAB_STRIP_H; i++)   /* die Leinwand hält die Pixel in der Byte-Reihenfolge des Panels */
                cv_px[i] = swap16(cv_px[i]);
            cv_blit(0, s * FAB_STRIP_H);
        }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    static const struct { const char *name; int q; } Q[] = {
        {"screen", FAB_Q_SCREEN}, {"chapter", FAB_Q_CHAPTER}, {"step", FAB_Q_STEP}, {"machine", FAB_Q_MACHINE},
        {"stars", FAB_Q_STARS}, {"playing", FAB_Q_PLAYING}, {"dialog", FAB_Q_DIALOG}, {"hz", FAB_Q_PARAM + 2},
    };
    uint32_t i;
    for (i = 0; i < sizeof Q / sizeof Q[0]; i++)
        if (!strcmp(what, Q[i].name)) {
            *got = fab_state(Q[i].q);
            return 1;
        }
    return 0;
}
#endif
