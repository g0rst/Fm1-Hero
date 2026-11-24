/* SPDX-License-Identifier: GPL-3.0-only
 * Die Tonfabrik, Bild: 120 x 120 Bildpunkte in 16 Farben, jeder doppelt so groß aufs 240 x 240-Display. Das spart
 * Speicher (14 KB statt 112 KB) und gibt den Pixel-Look. Teil von fab.c. */

#define GW 120
#define GH 120
static uint8_t fb[GW * GH];
/* das Oszi in voller Auflösung (240 Punkte breit): eine zweite Ebene über den Zeilen 46..149 des Displays;
 * 0xFF heißt durchsichtig. Kurven sind so doppelt so fein wie der Pixel-Look drumherum. */
#define HS_Y0 46
#define HS_H 104
static uint8_t hfb[2 * GW * HS_H];
static uint8_t hs_on;
static void hclear(void)
{
    int i;
    for (i = 0; i < 2 * GW * HS_H; i++) hfb[i] = 0xFF;
    hs_on = 0;
}
static void hpx(int x, int y, int c)
{
    y -= HS_Y0;
    if ((unsigned)x < 2u * GW && (unsigned)y < HS_H) hfb[y * 2 * GW + x] = (uint8_t)c;
}
static void hline(int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        int e2;
        hpx(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void hcut(int y, int h)          /* was über dem Oszi liegt (Sprechblase), deckt es auch in voller Auflösung zu */
{
    int r;
    for (r = 2 * y; r < 2 * (y + h); r++)
        if (r >= HS_Y0 && r < HS_Y0 + HS_H) {
            int i;
            for (i = 0; i < 2 * GW; i++) hfb[(r - HS_Y0) * 2 * GW + i] = 0xFF;
        }
}

/* die 16 Farben (Index -> RGB565) */
#define RGB565(r, g, b) (uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3)
static const uint16_t PAL[16] = {
    RGB565(0x0b, 0x0b, 0x14), RGB565(0x1d, 0x2b, 0x53), RGB565(0x7e, 0x25, 0x53), RGB565(0x00, 0x87, 0x51),
    RGB565(0xab, 0x52, 0x36), RGB565(0x5f, 0x57, 0x4f), RGB565(0xc2, 0xc3, 0xc7), RGB565(0xff, 0xf1, 0xe8),
    RGB565(0xff, 0x00, 0x4d), RGB565(0xff, 0xa3, 0x00), RGB565(0xff, 0xec, 0x27), RGB565(0x00, 0xe4, 0x36),
    RGB565(0x29, 0xad, 0xff), RGB565(0x83, 0x76, 0x9c), RGB565(0xff, 0x77, 0xa8), RGB565(0xff, 0xcc, 0xaa),
};
enum { C_BLK, C_NAVY, C_PLUM, C_DGRN, C_BRN, C_DGRY, C_LGRY, C_WHT, C_RED, C_ORG, C_YEL, C_GRN, C_BLU, C_LAV, C_PNK, C_PEACH };

static void px(int x, int y, int c)
{
    if ((unsigned)x < GW && (unsigned)y < GH) fb[y * GW + x] = (uint8_t)c;
}
static void rect(int x, int y, int w, int h, int c)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GW) w = GW - x;
    if (y + h > GH) h = GH - y;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            fb[(y + j) * GW + x + i] = (uint8_t)c;
}
static void frame(int x, int y, int w, int h, int c)
{
    rect(x, y, w, 1, c);
    rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c);
    rect(x + w - 1, y, 1, h, c);
}
static void line(int x0, int y0, int x1, int y1, int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void round_box(int x, int y, int w, int h, int fill, int edge)   /* ein Kasten mit runden Ecken */
{
    rect(x + 1, y + 1, w - 2, h - 2, fill);
    rect(x + 2, y, w - 4, 1, edge);
    rect(x + 2, y + h - 1, w - 4, 1, edge);
    rect(x, y + 2, 1, h - 4, edge);
    rect(x + w - 1, y + 2, 1, h - 4, edge);
    px(x + 1, y + 1, edge); px(x + w - 2, y + 1, edge); px(x + 1, y + h - 2, edge); px(x + w - 2, y + h - 2, edge);
}

/* ------------------------------------------------------------------ Schrift --- */
/* 5 x 7, Spalten, Bit 0 oben: 0x20 .. 0x5F, dann Ä Ö Ü ß und Zeichen (Stern, Note, Play, Aufnahme, Pfeile,
 * Herz, Blitz) */
static const uint8_t FONT[64 + 14][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x78,0x15,0x14,0x15,0x78},{0x38,0x45,0x44,0x45,0x38},{0x3C,0x41,0x40,0x41,0x3C},{0x7E,0x01,0x45,0x4A,0x30},
    {0x24,0x1C,0x0F,0x1C,0x24},{0x30,0x30,0x3F,0x01,0x02},{0x7F,0x3E,0x1C,0x08,0x00},{0x1C,0x3E,0x3E,0x3E,0x1C},
    {0x08,0x1C,0x2A,0x08,0x08},{0x08,0x08,0x2A,0x1C,0x08},{0x04,0x02,0x7F,0x02,0x04},{0x10,0x20,0x7F,0x20,0x10},
    {0x0C,0x1E,0x3C,0x1E,0x0C},{0x00,0x4C,0x3A,0x19,0x00},
};
/* Sonderzeichen im Text */
#define T_STAR "\x01"
#define T_NOTE "\x02"
#define T_PLAY "\x03"
#define T_REC "\x04"
#define T_LEFT "\x05"
#define T_RIGHT "\x06"
#define T_UP "\x0b"
#define T_DOWN "\x0c"
#define T_HEART "\x0e"
#define T_BOLT "\x0f"

/* das nächste Zeichen aus UTF-8: Glyphe, oder -1 am Ende */
static int next_glyph(const char **ps)
{
    const uint8_t *s = (const uint8_t *)*ps;
    int c = *s, g;
    if (!c) return -1;
    s++;
    if (c == 0xC3) {
        int d = *s;
        if (d) s++;
        *ps = (const char *)s;
        switch (d) {
        case 0x84: case 0xA4: return 64;      /* Ä */
        case 0x96: case 0xB6: return 65;      /* Ö */
        case 0x9C: case 0xBC: return 66;      /* Ü */
        case 0x9F: return 67;                 /* ß */
        default: return '?' - 0x20;
        }
    }
    *ps = (const char *)s;
    switch (c) {
    case 1: return 68; case 2: return 69; case 3: return 70; case 4: return 71; case 5: return 72; case 6: return 73;
    case 11: return 74; case 12: return 75; case 14: return 76; case 15: return 77;
    default: break;
    }
    if (c >= 'a' && c <= 'z') c -= 32;
    g = c >= 0x20 && c <= 0x5F ? c - 0x20 : '?' - 0x20;
    return g;
}
static int text_len(const char *s)
{
    int n = 0;
    while (next_glyph(&s) >= 0) n++;
    return n;
}
static int text(int x, int y, const char *s, int c, int sc)
{
    int g;
    while ((g = next_glyph(&s)) >= 0) {
        int col, row;
        for (col = 0; col < 5; col++)
            for (row = 0; row < 7; row++)
                if (FONT[g][col] >> row & 1) {
                    if (sc == 1) px(x + col, y + row, c);
                    else rect(x + col * sc, y + row * sc, sc, sc, c);
                }
        x += 6 * sc;
    }
    return x;
}
static void text_c(int cx, int y, const char *s, int c, int sc)   /* mittig um cx */
{
    text(cx - (6 * sc * text_len(s) - sc) / 2, y, s, c, sc);
}
static void text_sh(int x, int y, const char *s, int c, int sh, int sc)   /* mit Schatten */
{
    text(x + sc, y + sc, s, sh, sc);
    text(x, y, s, c, sc);
}
static char *put_int(char *d, int v)
{
    char t[12];
    int n = 0;
    if (v < 0) { *d++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}
static char *put_str(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    *d = 0;
    return d;
}

/* ------------------------------------------------------------------ Bilder --- */
/* Pixelbilder als Zeichen: '.' durchsichtig, '0'..'9' 'a'..'f' die Farben */
typedef struct { uint8_t w, h; const char *const *rows; } sprite_t;
static void spr(const sprite_t *s, int x, int y, int flip)
{
    int i, j;
    for (j = 0; j < s->h; j++)
        for (i = 0; i < s->w; i++) {
            char ch = s->rows[j][flip ? s->w - 1 - i : i];
            int c;
            if (ch == '.' || ch == ' ') continue;
            c = ch >= 'a' ? ch - 'a' + 10 : ch - '0';
            px(x + i, y + j, c);
        }
}
static void spr2(const sprite_t *s, int x, int y)   /* doppelt so groß */
{
    int i, j;
    for (j = 0; j < s->h; j++)
        for (i = 0; i < s->w; i++) {
            char ch = s->rows[j][i];
            if (ch == '.' || ch == ' ') continue;
            rect(x + 2 * i, y + 2 * j, 2, 2, ch >= 'a' ? ch - 'a' + 10 : ch - '0');
        }
}
/* Einfarbige Symbole: '#' gesetzt */
static void icon(const char *const *rows, int w, int h, int x, int y, int c)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (rows[j][i] == '#') px(x + i, y + j, c);
}

/* Herr Vogl, der Specht und Vorarbeiter: gelber Helm, roter Schopf, schwarz-weiß, großer Schnabel */
static const char *const VOGL0[] = {
    ".....aaaaa......",
    "...aaaaaaaaa....",
    "..aaa7aaaaaaa...",
    "..aaaaaaaaaaa...",
    ".99999999999999.",
    "..8888888888....",
    "..877777777.....",
    "..87700777......",
    "..87700777aaaa..",
    "..8777777999999.",
    "..087777........",
    ".0008777........",
    ".00007777.......",
    "000007777.......",
    "0000777777......",
    "0000777777......",
    ".000777777......",
    ".0000777777.....",
    "..00007777......",
    "...0000000......",
    "....9...9.......",
    "...99..99.......",
};
static const char *const VOGL1[] = {               /* Schnabel offen: er spricht */
    ".....aaaaa......",
    "...aaaaaaaaa....",
    "..aaa7aaaaaaa...",
    "..aaaaaaaaaaa...",
    ".99999999999999.",
    "..8888888888....",
    "..877777777.....",
    "..87700777aaa...",
    "..87700777..aaa.",
    "..8777777.......",
    "..0877779999....",
    ".000877.........",
    ".00007777.......",
    "000007777.......",
    "0000777777......",
    "0000777777......",
    ".000777777......",
    ".0000777777.....",
    "..00007777......",
    "...0000000......",
    "....9...9.......",
    "...99..99.......",
};
static const char *const VOGL2[] = {               /* er freut sich: Augen zu, Flügel hoch */
    ".....aaaaa......",
    "...aaaaaaaaa....",
    "..aaa7aaaaaaa...",
    "..aaaaaaaaaaa...",
    ".99999999999999.",
    "..8888888888....",
    "..877777777.....",
    "..8700077777....",
    "..87777777aaaa..",
    "..8777777999999.",
    "0.087777........",
    "000087777.......",
    ".00007777.......",
    "..000777777.....",
    "..00777777......",
    "..00777777......",
    ".000777777......",
    ".0000777777.....",
    "..00007777......",
    "...0000000......",
    "....9...9.......",
    "...99..99.......",
};
static const sprite_t SPR_VOGL[3] = {{16, 22, VOGL0}, {16, 22, VOGL1}, {16, 22, VOGL2}};

/* die Kundinnen und Kunden, 12 x 12 */
static const char *const CL_TROMMEL[] = {
    "..7......7..", "...7....7...", "....7..7....", "..88888888..", ".8777777778.", ".8888888888.",
    ".8a8a8a8a88.", ".88a8a8a8a8.", ".8888888888.", ".8777777778.", "..88888888..", "............"};
static const char *const CL_KUH[] = {
    "..6......6..", "..66....66..", "..77777777..", ".7707777077.", "777777777777", ".7700777777.",
    "..77777777..", "..eeeeeeee..", ".eee0ee0eee.", ".eeeeeeeeee.", "..eeeeeeee..", "............"};
static const char *const CL_VOGEL[] = {
    "............", "....ccc.....", "...c0ccc....", "..9ccccc....", "...cccccc...", "...ccc77cc..",
    "....cc777cc.", ".....ccccccc", "......9.9...", "......9.9...", "............", "............"};
static const char *const CL_MUSEUM[] = {
    ".....66.....", "...666666...", ".6666666666.", "666666666666", ".77..77..77.", ".77..77..77.",
    ".77..77..77.", ".77..77..77.", ".77..77..77.", "666666666666", "666666666666", "............"};
static const char *const CL_WECKER[] = {
    ".88......88.", "8888....8888", "..88888888..", ".8777777778.", "877707777778", "877770777778",
    "877777077778", "877777777778", ".8777777778.", "..88888888..", ".8........8.", "............"};
static const char *const CL_SCHIFF[] = {
    ".....7......", ".....77.....", ".....777....", ".....7777...", ".....7......", "888888888888",
    ".8888888888.", "..88888888..", "cccccccccccc", ".cc.cc.cc.cc", "............", "............"};
static const char *const CL_PFEIFE[] = {
    "............", "............", "..aaaaaaa...", ".aaaaaaaaa..", "aa0aaaaaaaaa", "aaaaaaaaaaaa",
    ".aaaaaaaaa..", "..aaaaa.....", "...aaa......", "............", "............", "............"};
static const char *const CL_FLOETE[] = {
    "...........9", "..........9.", ".........9..", "........99..", ".......9.9..", "......99....",
    ".....9......", "....99......", "...9........", "..99........", ".9..........", "9..........."};
static const char *const CL_PADS[] = {               /* Videospiel */
    "............", "............", "..55555555..", ".5555555555.", "556555558555", "566655585855",
    "556555558555", "555555555555", ".555....555.", "..5......5..", "............", "............"};
static const char *const CL_THEATER[] = {
    "888......888", "8888....8888", "88888..88888", "88888..88888", "88888..88888", "8888....8888",
    "8888....8888", "888......888", "888......888", "88........88", "aaaaaaaaaaaa", "aaaaaaaaaaaa"};
static const char *const CL_BUCH[] = {
    "............", ".cccc..cccc.", "c7777cc7777c", "c7555cc5557c", "c7777cc7777c", "c7555cc5557c",
    "c7777cc7777c", "c7555cc5557c", "c7777cc7777c", ".cccccccccc.", "......c.....", "............"};
static const char *const CL_GITARRE[] = {
    "..........55", ".........55.", "........44..", ".......44...", "..999.44....", ".9999944....",
    "999909999...", "9999099999..", ".99999999...", "..999999....", "...999......", "............"};
static const char *const CL_GLOCKE[] = {
    ".....44.....", ".....aa.....", "....aaaa....", "...aaaaaa...", "...aaaaaa...", "..aaaaaaaa..",
    "..aaaaaaaa..", ".aaaaaaaaaa.", "aaaaaaaaaaaa", ".....99.....", ".....99.....", "............"};
static const char *const CL_ORGEL[] = {
    "..6.6.6.6...", ".66.66.66.6.", ".66.66.66.66", ".66666666666", ".66666666666", "444444444444",
    "477777777774", "470707070774", "477777777774", "444444444444", "4..........4", "............"};
static const char *const CL_GEIGE[] = {
    ".......0....", "......00....", ".....04.....", "....44......", "...4444.....", "..440044....",
    "..444444..7.", "...4444..7..", "..444444.7..", "..440044....", "...4444.....", "............"};
static const char *const CL_GEIST[] = {
    "....7777....", "..77777777..", ".7777777777.", ".7700770077.", ".7700770077.", ".7777777777.",
    ".7777007777.", ".7777777777.", ".7777777777.", ".7777777777.", ".77.77.77.7.", "............"};
static const char *const CL_TEDDY[] = {
    ".44......44.", "4f44....44f4", "444444444444", ".4444444444.", ".4404444044.", ".4444444444.",
    ".4444ff4444.", ".444f00f444.", "..4444444...", "...444444...", "............", "............"};
static const char *const CL_DISCO[] = {
    "......6.....", "......6.....", "....6666....", "...6c6e66...", "..66e6a6c6..", "..6c6a6e66..",
    "..66e6c6a6..", "...6a6e66...", "....6666....", "............", "............", "............"};
static const char *const CL_FEUER[] = {
    "....cccc....", "...c7cccc...", "...cccccc...", "888888888888", "877788887778", "877788887778",
    "888888888888", "888888888888", ".00......00.", "0550....0550", ".00......00.", "............"};
static const char *const CL_BIENE[] = {
    "...77..77...", "..7777777...", "...7777.....", "..aa00aa00..", ".0aa00aa00a.", ".0aa00aa00a0",
    "..aa00aa00..", "............", "............", "............", "............", "............"};
static const char *const CL_UFO[] = {
    "............", ".....cc.....", "....c77c....", "...cc77cc...", ".6666666666.", "666a66a66a66",
    ".6666666666.", "...b....b...", "..b......b..", ".b........b.", "............", "............"};
static const char *const CL_ROBOTER[] = {
    ".....8......", ".....6......", "..66666666..", "..6cc66cc6..", "..6cc66cc6..", "..66666666..",
    "..6a7a7a76..", "..66666666..", "...666666...", ".66666666666", ".6.666666..6", "...6....6..."};
static const char *const CL_HOEHLE[] = {
    "............", "....5555....", "..55555555..", ".5550000555.", "555000000555", "550000000055",
    "5500d0d00055", "55000d000055", "550000000055", "555555555555", "............", "............"};
static const char *const CL_KIRCHE[] = {
    ".....7......", "....777.....", ".....7......", "....666.....", "...66666....", "...6c6c6....",
    "..6666666...", ".666666666..", ".66c666c66..", ".66c644c66..", ".6666446666.", "............"};
static const char *const CL_STADT[] = {
    "..88....cc..", ".8888..cccc.", "..66....66..", "..66.aa.66..", "..66.aa.66..", "aaaaaaaaaaaa",
    "a7a7a7a7a7a7", "aaaaaaaaaaaa", "..9......9..", "..9......9..", "..9......9..", "............"};
static const char *const CL_MOND[] = {
    ".......aaa..", ".....aaa....", "....aaa.....", "...aaa......", "...aaa......", "...aaa......",
    "...aaaa.....", "....aaaa....", ".....aaaaa..", ".......aaa..", "............", "............"};
static const char *const CL_PIANO[] = {
    "............", "000000000000", "070707007070", "070707007070", "077777777770", "077777777770",
    "000000000000", "0..........0", "............", "............", "............", "............"};
static const char *const CL_LOEWE[] = {
    "..99999999..", ".9999999999.", "999aaaaaa999", "99a0aaaa0a99", "99aaaaaaaa99", "99aaa00aaa99",
    "99aa0000aa99", "999aaaaaa999", ".9999999999.", "..99999999..", "............", "............"};
static const char *const CL_MUENZE[] = {
    "....aaaa....", "..aaaaaaaa..", ".aa99999aaa.", ".aa9aaaa9aa.", "aaa9aaa99aaa", "aaa9aaa9aaaa",
    "aaa9aaa99aaa", ".aa9aaaa9aa.", ".aa99999aaa.", "..aaaaaaaa..", "....aaaa....", "............"};
static const char *const CL_FISCH[] = {
    "............", "............", "....9999....", "9..999999...", "99999999909.", "999999999999",
    "99999999999.", "9..999999...", "....9999....", "............", "cc.cc.cc.cc.", "............"};
static const char *const CL_RAKETE[] = {
    ".....88.....", "....8778....", "....7777....", "....7cc7....", "....7cc7....", "....7777....",
    "...877778...", "..88777788..", "..8.8888.8..", ".....99.....", "....9aa9....", ".....99....."};
static const char *const CL_TELEFON[] = {
    "............", "..88888888..", ".8888888888.", "888......888", "88..8888..88", "...888888...",
    "..88777788..", "..88777788..", "..88888888..", "..88888888..", "............", "............"};
static const char *const CL_KATZE[] = {
    ".5........5.", ".55......55.", ".5555555555.", ".55b5555b55.", ".5555555555.", "555555e55555",
    ".5555555555.", "..55555555..", "............", "............", "............", "............"};
static const char *const CL_HUND[] = {
    "44........44", "444......444", "444444444444", ".4404444044.", ".4444444444.", ".4444004444.",
    "..44400444..", "...44ee44...", "....4ee4....", "............", "............", "............"};
static const char *const CL_FROSCH[] = {
    "..bb....bb..", ".b70b..b70b.", ".bbbbbbbbbb.", "bbbbbbbbbbbb", "bb88888888bb", ".bbbbbbbbbb.",
    "..bbbbbbbb..", ".bb......bb.", "............", "............", "............", "............"};
enum { CL_TROM, CL_KUH_, CL_VOG, CL_MUS, CL_WECK, CL_SCHI, CL_PFEI, CL_FLOE, CL_PADS_, CL_THEA, CL_BUCH_, CL_GITA,
       CL_GLOC, CL_ORGE, CL_GEIG, CL_GEIS, CL_TEDD, CL_DISC, CL_FEUE, CL_BIEN, CL_UFO_, CL_ROBO, CL_HOEH, CL_KIRC,
       CL_STAD, CL_MOND_, CL_PIAN, CL_LOEW, CL_MUEN, CL_FISC, CL_RAKE, CL_TELE, CL_KATZ, CL_HUND_, CL_FROS, NCL };
static const char *const *const CLIENT_ART[NCL] = {
    CL_TROMMEL, CL_KUH, CL_VOGEL, CL_MUSEUM, CL_WECKER, CL_SCHIFF, CL_PFEIFE, CL_FLOETE, CL_PADS, CL_THEATER, CL_BUCH,
    CL_GITARRE, CL_GLOCKE, CL_ORGEL, CL_GEIGE, CL_GEIST, CL_TEDDY, CL_DISCO, CL_FEUER, CL_BIENE, CL_UFO, CL_ROBOTER,
    CL_HOEHLE, CL_KIRCHE, CL_STADT, CL_MOND, CL_PIANO, CL_LOEWE, CL_MUENZE, CL_FISCH, CL_RAKETE, CL_TELEFON,
    CL_KATZE, CL_HUND, CL_FROSCH};
static void client_icon(int c, int x, int y, int big)
{
    sprite_t s;
    s.w = 12; s.h = 12; s.rows = CLIENT_ART[c];
    if (big) spr2(&s, x, y); else spr(&s, x, y, 0);
}

/* die Maschinen, 11 x 9, einfarbig */
static const char *const IC_OSC[] = {
    "...........", "..##.......", ".#..#......", "#....#.....", "......#...#", ".......#.#.", "........#..",
    "...........", "..........."};
static const char *const IC_KLOPF[] = {
    "...........", "#....#....#", "#....#....#", "#....#....#", "#....#....#", "###########", "...........",
    "...........", "..........."};
static const char *const IC_OSC2[] = {
    "...........", "..##...##..", ".#..#.#..#.", "#....#....#", "...........", "...#...#...", "..#.#.#.#..",
    ".#...#...#.", "..........."};
static const char *const IC_FM[] = {
    "...........", ".###...###.", "#...#.#...#", "#...###...#", "#...#.#...#", ".###...###.", "...........",
    "...........", "..........."};
static const char *const IC_VCF[] = {
    "###########", ".#.#.#.#.#.", "..#######..", "...#.#.#...", "....###....", ".....#.....", ".....#.....",
    ".....#.....", "..........."};
static const char *const IC_VCA[] = {
    "..#######..", ".#.......#.", "#.........#", "#.###.###.#", "#.#.#.#.#.#", "#.###.###.#", "#.........#",
    "###########", "..........."};
static const char *const IC_ENV[] = {
    "...........", "...#.......", "..#.#......", "..#..#.....", ".#....####.", ".#.......#.", "#.........#",
    "###########", "..........."};
static const char *const IC_LFO[] = {
    "...........", ".##...##...", "#..#.#..#.#", "....#....#.", "...........", "#.#.#.#.#.#", "...........",
    "...........", "..........."};
static const char *const IC_FX[] = {
    "...#...#...", "#...#...#..", "##..#...#..", "###.#...#..", "##..#...#..", "#...#...#..", "...#...#...",
    "...........", "..........."};
static const char *const IC_SPK[] = {
    "....#......", "...##..#...", "####.#..#..", "#..#.#..#..", "#..#.#..#..", "####.#..#..", "...##..#...",
    "....#......", "..........."};
static const char *const IC_BOLT[] = {
    "......##...", ".....##....", "....##.....", "...######..", ".....##....", "....##.....", "...##......",
    "..##.......", "..........."};
static const char *const IC_SEQ[] = {
    "###.###.###", "###.###.###", "...........", "###.###.###", "###.###.###", "...........", "###.###.###",
    "###.###.###", "..........."};
static const char *const IC_LOCK[] = {
    "...###.....", "..#...#....", "..#...#....", ".#######...", ".###.###...", ".###.###...", ".#######...",
    "...........", "..........."};
static const char *const IC_BOOK[] = {
    "...........", ".####.####.", "#....#....#", "#.##.#.##.#", "#....#....#", "#.##.#.##.#", "#....#....#",
    ".####.####.", "..........."};
static const char *const IC_GAME[] = {
    "...........", ".#########.", "#.#.....#.#", "###..#.#..#", "#.#.....#.#", "#...###...#", ".##.....##.",
    "...........", "..........."};

/* Mini-Wellen für die Regler, 9 x 5 */
static void mini_wave(int w, int x, int y, int c)
{
    static const char *const MW[7][5] = {
        {".##......", "#..#.....", "....#...#", ".....#..#", "......##."},   /* Sinus */
        {"..#......", ".#.#.....", "#...#...#", ".....#.#.", "......#.."},   /* Dreieck */
        {"...#....#", "..##...##", ".#.#..#.#", "#..#.#..#", "...##...."},   /* Säge */
        {"####.....", "#..#.....", "#..#...#.", "...#...#.", "...#####."},   /* Rechteck */
        {"#..#.#...", ".#...#.#.", "..#.#..#.", "#....#..#", ".#.#..#.#"},   /* Rauschen */
        {"#....#...", "#....#...", "#....#...", "#....#...", "#########"},   /* Klick */
        {"#........", "#...#####", "#...#....", "#...#....", "#####...."},   /* (LFO eckig/Zufall) */
    };
    icon(MW[w], 9, 5, x, y, c);
}
