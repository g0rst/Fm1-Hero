#define _POSIX_C_SOURCE 199309L
/* SPDX-License-Identifier: GPL-3.0-only
 * Die Last des Klangs im schlimmsten Fall: alle neun Stimmen klingen (sechs Spuren mit FM, zwei Schwingern,
 * Rauschen, Filter mit Hüllkurve, Wackler, Kratz, Echo und Hall, dazu Werktisch, Kunde und ein Geräusch). Gemessen
 * wird die Zeit pro Sample auf dem Rechner; mit callgrind (valgrind --tool=callgrind) die Instruktionen. */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "../app/core/fab.c"

int main(int argc, char **argv)
{
    static int32_t lr[2 * 256];
    int v, i, n = argc > 1 ? atoi(argv[1]) : 2000;
    struct timespec a, b;
    double ns;
    fab_init(1);
    for (v = 0; v < NV; v++) {
        uint8_t *p = v < NTR ? SL[0].song.patch[v] : v == V_BENCH ? bench : v == V_TARGET ? tgt_patch : sfx_patch;
        make_patch(p, 0);
        p[P_W1] = W_SAW; p[P_W2] = W_NOISE; p[P_MIX2] = 80; p[P_FM] = 40; p[P_FMENV] = 40;
        p[P_CUT] = 60; p[P_RES] = 80; p[P_FENV] = 100; p[P_S1] = 127; p[P_S2] = 60; p[P_DRIVE] = 60;
        p[P_LDEP] = 60; p[P_LTGT] = L_CUT; p[P_ECHO] = 60; p[P_HALL] = 60;
        V[v].p = p;
        v_on(&V[v], 48.0f + (float)v * 3.0f, 1.0f);
    }
    fab_render(lr, 256, 4096);
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (i = 0; i < n; i++) fab_render(lr, 256, 4096);
    clock_gettime(CLOCK_MONOTONIC, &b);
    ns = ((double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec)) / (n * 256.0);
    printf("Klang, schlimmster Fall (9 Stimmen): %.0f ns pro Sample auf diesem Rechner (Budget am Gerät: 22 676 ns)\n", ns);
    return 0;
}
