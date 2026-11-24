/* SPDX-License-Identifier: GPL-3.0-only
 * Die Tonfabrik: ein Lernspiel über Synthesizer für den M-VAVE FM-1.
 *
 * Der Kern ist portables C99 ohne libc und ohne Plattform-Aufrufe: dieselben Dateien laufen auf dem FM-1
 * (app/app.c auf fm1-base), im Browser (sim/, WebAssembly) und in den Host-Tests (tests/).
 *   synth.c  Klang: Produktionsstraße (OSC, FM, VCF, VCA, ENV, LFO), sechs Spuren, Sequencer, Echo, Hall, Oszi
 *   gfx.c    Bild: 120 x 120 Pixel in 16 Farben, doppelt so groß aufs Display; Schrift, Figuren, Symbole
 *   game.c   Spiel: Bildschirme, Story, Aufträge, Minispiele, Livemodus, LEDs, Spielstände
 *   story.inc Texte und Aufträge (nur Daten)
 *
 * Threads: fab_render läuft im Audio-Kontext, alles andere in der Hauptschleife. */
#ifndef FAB_CORE_H
#define FAB_CORE_H
#include <stdint.h>

#define FAB_SR 44100
#define FAB_W 240
#define FAB_H 240
#define FAB_STRIP_H 48                  /* das Display wird in 5 Streifen zu 48 Zeilen gezeichnet */
#define FAB_STRIPS (FAB_H / FAB_STRIP_H)
#define FAB_NKEYS 27                    /* F3 .. G5 */

/* Bedienelemente in der Reihenfolge von fm1-base (plat.h); app.c prüft das beim Übersetzen */
enum { FAB_B_FX, FAB_B_SEL, FAB_B_ENV, FAB_B_LFO, FAB_B_EDIT, FAB_B_GLO, FAB_B_HOME, FAB_B_SAVE,
       FAB_B_ARP, FAB_B_SEQ, FAB_B_PLAY, FAB_B_REC, FAB_B_OCTDN, FAB_B_OCTUP, FAB_NB };
#define FAB_LED_PLAY_GREEN (1u << FAB_NB)   /* das zweite, grüne Licht von PLAY */
enum { FAB_E_SELECT, FAB_E_ALGO, FAB_E_PRESET, FAB_E_K1, FAB_E_K2, FAB_E_K3, FAB_E_K4, FAB_NE };

/* ---- Lebenslauf ---- */
void fab_init(uint32_t seed);
void fab_panic(void);
void fab_dev(int on);
/* wo das Spiel gerade steht, als Text (für Fehlermeldungen aus dem Entwicklermodus); gibt die Länge */
int fab_context(char *out, int max);                   /* Entwicklermodus für Platz 3 (nur die Browser-Version) */

/* ---- Audio-Kontext: n Stereo-Frames, 24 Bit (Q23 in int32), Lautstärke gain_q12 (0..4096) ---- */
void fab_render(int32_t *lr, uint32_t n, uint32_t gain_q12);
void fab_midi_note(uint32_t channel, uint32_t note, uint32_t velocity);   /* velocity 0: aus */

/* ---- Hauptschleife ---- */
void fab_input(uint32_t buttons, uint32_t keys, const int32_t enc[FAB_NE], uint32_t now_ms);
uint32_t fab_dirty(void);               /* Bit s: Streifen s muss neu gezeichnet werden */
void fab_draw(uint16_t *px, int strip); /* 240 x 48 Pixel RGB565 */
/* Lichter: hell, glimmend, mittel; glow 1 leise, 2 kräftig */
void fab_lights(uint32_t *buttons, uint32_t *keys, uint32_t *dim_buttons, uint32_t *dim_keys,
                uint32_t *mid_buttons, uint32_t *mid_keys, int *glow);
void fab_message(const char *a, const char *b);

/* ---- Spielstände: drei Plätze, jeder ein Objekt ---- */
#define FAB_NSLOTS 3
#define FAB_OBJ_MAX 1536u
uint32_t fab_store_get(int slot, void *dst, uint32_t max);   /* gepackt: Länge, 0 = zu klein */
int fab_store_set(int slot, const void *src, uint32_t len); /* 0 = geladen */
uint32_t fab_save_wanted(void);         /* Bit k: Platz k soll geschrieben werden */
void fab_store_saved(int slot);

/* ---- für Tests und Simulator ---- */
int fab_state(int what);
enum { FAB_Q_SCREEN, FAB_Q_SLOT, FAB_Q_CHAPTER, FAB_Q_STEP, FAB_Q_MACHINE, FAB_Q_STARS, FAB_Q_PLAYING, FAB_Q_DIALOG,
       FAB_Q_JOB, FAB_Q_GAME,              /* (GAME: die richtige Antwort im Minispiel, für Tests) */
       FAB_Q_PARAM = 100 };             /* + Parameter: sein Wert am Werktisch */
#endif
