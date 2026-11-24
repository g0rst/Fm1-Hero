/* SPDX-License-Identifier: GPL-3.0-only
 * Die Browser-Seite der Tonfabrik: derselbe Kern wie in der Firmware (app/core/fab.c), ohne libc nach WebAssembly
 * übersetzt (sim/build.sh), mit einem Bildspeicher und einem Audio-Block, die die Seite liest. */
#include "../app/core/fab.h"

#define EXPORT(n) __attribute__((export_name(#n))) n

void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned long n)
{
    unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
    return d;
}

static uint16_t fb[FAB_W * FAB_H];
static int32_t pcm[2 * 128];
static float out[2 * 128];
static uint32_t lights[7];
static uint8_t obj[FAB_OBJ_MAX];

void EXPORT(sim_init)(uint32_t seed) { fab_init(seed); }
uint16_t *EXPORT(sim_fb)(void) { return fb; }
float *EXPORT(sim_out)(void) { return out; }
uint32_t *EXPORT(sim_lights)(void) { return lights; }
uint8_t *EXPORT(sim_obj)(void) { return obj; }

void EXPORT(sim_audio)(uint32_t gain_q12)   /* 128 Stereo-Frames nach sim_out, -1..1 */
{
    int i;
    fab_render(pcm, 128, gain_q12);
    for (i = 0; i < 256; i++) out[i] = (float)pcm[i] * (1.0f / 8388608.0f);
}

/* ein Bild: das Panel hinein, die geänderten Streifen gezeichnet; gibt die gezeichneten Streifen */
uint32_t EXPORT(sim_frame)(uint32_t buttons, uint32_t keys, int32_t e0, int32_t e1, int32_t e2, int32_t e3,
                           int32_t e4, int32_t e5, int32_t e6, uint32_t now_ms)
{
    int32_t enc[FAB_NE];
    uint32_t dirty, s;
    int glow;
    enc[0] = e0; enc[1] = e1; enc[2] = e2; enc[3] = e3; enc[4] = e4; enc[5] = e5; enc[6] = e6;
    fab_input(buttons, keys, enc, now_ms);
    fab_lights(&lights[0], &lights[1], &lights[2], &lights[3], &lights[4], &lights[5], &glow);
    lights[6] = (uint32_t)glow;
    dirty = fab_dirty();
    for (s = 0; s < FAB_STRIPS; s++)
        if (dirty >> s & 1u) fab_draw(fb + s * FAB_W * FAB_STRIP_H, (int)s);
    return dirty;
}

/* die Spielstände, Platz für Platz */
uint32_t EXPORT(sim_save)(void) { return fab_save_wanted(); }
uint32_t EXPORT(sim_store_get)(int slot) { return fab_store_get(slot, obj, sizeof obj); }
int EXPORT(sim_store_set)(int slot, uint32_t len) { return fab_store_set(slot, obj, len); }
void EXPORT(sim_store_saved)(int slot) { fab_store_saved(slot); }
int EXPORT(sim_state)(int what) { return fab_state(what); }
