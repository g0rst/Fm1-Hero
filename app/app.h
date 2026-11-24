/* SPDX-License-Identifier: GPL-3.0-only
 * Die Tonfabrik auf dem M-VAVE FM-1: die Hälfte des Vertrags mit fm1-base, die zur App gehört (base/device/unity.c
 * auf dem FM-1, base/host/plat_host.c im Host-Simulator). Das Spiel selbst ist core/fab.c. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TONFABRIK"
#define APP_TAGLINE "lernspiel synthesizer"
#define APP_SPLASH_RGB RGB(255, 163, 0)
#define APP_BLOCK 256u                   /* Frames pro Audio-Block: 5,8 ms */

/* Die Speicherobjekte: drei Spielstände und die Panel-Kalibrierung der Plattform. Jedes Objekt belegt zwei Sektoren
 * zu 4 KiB (A/B). Die Spielstände liegen bei 0xB1000..0xB6FFF, im selben Fenster wie bei ro:MINImal: auf dem FM-1 im
 * Datenbereich von fm1-base (0x97000..0xDFFFF), weg vom Staging des Updates ab 0xE0000 und von Feluccas Einstellungen
 * bei 0xFC000; auf dem SMK-37 im Fenster, das das Porting-Playbook für zusätzliche Objekte vorsieht. app.c prüft das
 * alles beim Übersetzen (die Asserts der Flash-Karte); nie ein Objekt ohne sie verschieben. */
enum { OBJ_SLOT0, OBJ_PANEL, OBJ_SLOT1, OBJ_SLOT2, OBJ_NOBJ };
#define FAB_ADDR_SLOT0 0xB1000u
#define FAB_ADDR_SLOT1 0xB3000u
#define FAB_ADDR_SLOT2 0xB5000u
#if defined BOARD_SMK37 && BOARD_SMK37
#error "Tonfabrik: noch keine Board-Schicht für das SMK-37 (der Platz von OBJ_PANEL ist dort nicht entschieden)"
#endif
#define FAB_ADDR_PANEL 0xDC000u          /* FM-1: die Panel-Kalibrierung, wo das Template von fm1-base sie hat */
#define APP_STORE_MAP {FAB_ADDR_SLOT0, FAB_ADDR_PANEL, FAB_ADDR_SLOT1, FAB_ADDR_SLOT2}

/* der Vertrag: die Plattform ruft diese */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* die Prüfungen des Host-Simulators (app/tests/basic.txt) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
