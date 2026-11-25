/*
 * nTemple for TI-Nspire (Ndless SDK)
 *
 * A pseudo-3D endless runner in the spirit of Temple Run,
 * renamed nTemple:
 * 3 lanes, auto-run, jump, coins, 90-degree turns you must steer into.
 *
 * Controls (calculator keys):
 *   7 = move left     9 = move right
 *   8 = jump          5 = start / restart
 *   4 = turn left     6 = turn right
 *   ESC = quit
 *
 * The same source builds for macOS as a preview renderer (see host/preview.c).
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "font_data.h"
#include "game.h"

/* ------------------------------------------------------------------ */
/* framebuffer + colour helpers                                        */
/* ------------------------------------------------------------------ */
#define SW 320
#define SH 240

static uint16_t fb[SW * SH];

#define C(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static inline uint16_t dimm(uint16_t c, int k)
{
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = (r * (8 - k)) >> 3;
    g = (g * (8 - k)) >> 3;
    b = (b * (8 - k)) >> 3;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline void hline(int y, int x0, int x1, uint16_t c)
{
    uint16_t *p;
    if (y < 0 || y >= SH) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= SW) x1 = SW - 1;
    if (x0 > x1) return;
    p = fb + y * SW + x0;
    while (x0++ <= x1) *p++ = c;
}

static void fill_rect(int x, int y, int w, int h, uint16_t c)
{
    int j;
    for (j = 0; j < h; j++) hline(y + j, x, x + w - 1, c);
}

/* convex polygon with integer screen coords, incremental scanline fill
   (no division inside the scan loop -- important on the ARM926) */
static void fill_poly(int *px, int *py, int n, uint16_t c)
{
    int ymin, ymax, i, y;
    int ey0[8], ey1[8], ex0[8], edx[8], eon[8];

    ymin = ymax = py[0];
    for (i = 1; i < n; i++) {
        if (py[i] < ymin) ymin = py[i];
        if (py[i] > ymax) ymax = py[i];
    }
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int x0 = px[i], y0 = py[i], x1 = px[j], y1 = py[j];
        if (y0 > y1) { int t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        ey0[i] = y0; ey1[i] = y1; ex0[i] = x0;
        edx[i] = (y1 > y0) ? (int)(((long long)(x1 - x0) * 65536) / (y1 - y0)) : 0;
        eon[i] = (y1 > y0);
    }
    if (ymin < 0) ymin = 0;
    if (ymax > SH - 1) ymax = SH - 1;
    for (y = ymin; y <= ymax; y++) {
        int xmin = 32767, xmax = -32768;
        for (i = 0; i < n; i++) {
            int x;
            if (!eon[i] || y < ey0[i] || y >= ey1[i]) continue;
            x = ex0[i] + (int)(((long long)(y - ey0[i]) * edx[i]) >> 16);
            if (x < xmin) xmin = x;
            if (x > xmax) xmax = x;
        }
        if (xmin <= xmax) hline(y, xmin, xmax, c);
    }
}

/* ------------------------------------------------------------------ */
/* text                                                                */
/* ------------------------------------------------------------------ */
static void draw_text(int x, int y, const char *s, uint16_t col, uint16_t shadow)
{
    while (*s) {
        int ch = *s++ - FONT_FIRST;
        const unsigned char *g;
        int row, bx;
        if (ch < 0 || ch >= FONT_COUNT) ch = 0;
        g = font_bits[ch];
        for (row = 0; row < FONT_H; row++) {
            for (bx = 0; bx < FONT_W; bx++) {
                if (g[row] & (1 << (FONT_W - 1 - bx))) {
                    fb[(y + row + 1) * SW + x + bx] = shadow;
                }
            }
        }
        for (row = 0; row < FONT_H; row++) {
            for (bx = 0; bx < FONT_W; bx++) {
                if (g[row] & (1 << (FONT_W - 1 - bx)))
                    fb[(y + row) * SW + x + bx] = col;
            }
        }
        x += FONT_W;
    }
}

static void draw_text_center(int y, const char *s, uint16_t col, uint16_t shadow)
{
    draw_text(SW / 2 - (int)strlen(s) * FONT_W / 2, y, s, col, shadow);
}

static int text_width(const char *s) { return (int)strlen(s) * FONT_W; }

/* ------------------------------------------------------------------ */
/* fixed point + camera math                                           */
/* ------------------------------------------------------------------ */
#ifndef FOCAL
#define FOCAL 330
#endif
#define NEARZ 48
#define DRAW_FAR 5733   /* was 4300; a third further */
#define STEP 150
static int pit_cos, pit_sin;

#ifndef CAM_BACK
#define CAM_BACK 430
#endif
#ifndef CAM_HEIGHT
#define CAM_HEIGHT 215
#endif
#ifndef PITCH_Q16
#define PITCH_Q16 2400   /* ~13 deg down */
#endif

#ifndef HALF_W
#define HALF_W 380      /* corridor half width */
#endif
#define WALL_H 760      /* wall / ceiling height */
#ifndef LANE_W
#define LANE_W 190      /* lane spacing */
#endif

typedef struct { int x, y, z; } V3;   /* camera space: x right, y up, z forward */

static int cam_x, cam_z, cam_y;       /* world position of the camera */
static int cam_cos, cam_sin;          /* Q16 */
static int cam_yaw_q16;               /* 65536 = full turn */

#include "sin_table.h"

static inline int isin(int a) { return sin_tab[(a >> 8) & 255]; }
static inline int icos(int a) { return sin_tab[((a >> 8) + 64) & 255]; }

/* world direction for heading 0..3 : +z, +x, -z, -x */
static const int DIRX[4] = {0, 1, 0, -1};
static const int DIRZ[4] = {1, 0, -1, 0};

static inline void heading_right(int dir, int *rx, int *rz)
{
    *rx = DIRZ[dir];
    *rz = -DIRX[dir];
}

/* world -> camera */
static inline V3 to_cam(int x, int y, int z)
{
    V3 v;
    int dx = x - cam_x, dz = z - cam_z;
    int px = (int)(((long long)dx * cam_cos - (long long)dz * cam_sin) >> 16);
    int pz = (int)(((long long)dx * cam_sin + (long long)dz * cam_cos) >> 16);
    int py = y - cam_y;
    v.x = px;
    v.y = (int)(((long long)py * pit_cos + (long long)pz * pit_sin) >> 16);
    v.z = (int)(((long long)pz * pit_cos - (long long)py * pit_sin) >> 16);
    return v;
}

static int clip_near(const V3 *in, int n, V3 *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        V3 a = in[i], b = in[(i + 1) % n];
        int ain = a.z >= NEARZ, bin = b.z >= NEARZ;
        if (ain) out[m++] = a;
        if (ain != bin) {
            long long t = ((long long)(NEARZ - a.z) * 4096) / (b.z - a.z);
            V3 c;
            c.x = a.x + (int)(((long long)(b.x - a.x) * t) >> 12);
            c.y = a.y + (int)(((long long)(b.y - a.y) * t) >> 12);
            c.z = NEARZ;
            out[m++] = c;
        }
        if (m >= 7) break;
    }
    return m;
}

static V3 polybuf_a[8];
static uint16_t g_outline;   /* 0 = no outline */

static void draw_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, e2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        if (x0 >= 0 && x0 < SW && y0 >= 0 && y0 < SH) fb[y0 * SW + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

/* draw a world-space convex polygon (up to 4 verts used here) */
static void draw_world_poly(const V3 *world_pts, int n, uint16_t col)
{
    V3 cam[8];
    int i, m;
    int sx[8], sy[8];
    for (i = 0; i < n; i++)
        cam[i] = to_cam(world_pts[i].x, world_pts[i].y, world_pts[i].z);
    m = clip_near(cam, n, polybuf_a);
    if (m < 3) return;
    for (i = 0; i < m; i++) {
        int z = polybuf_a[i].z;
        long long px = (long long)polybuf_a[i].x * FOCAL / z;
        long long py = (long long)polybuf_a[i].y * FOCAL / z;
        if (px > 4000) px = 4000;
        if (px < -4000) px = -4000;
        if (py > 4000) py = 4000;
        if (py < -4000) py = -4000;
        sx[i] = SW / 2 + (int)px;
        sy[i] = SH / 2 - (int)py;
    }
#ifdef DEBUG_POLY
    if (m >= 3) {
        int mnx = sx[0], mxx = sx[0], mny = sy[0], mxy = sy[0], q;
        for (q = 1; q < m; q++) { if (sx[q]<mnx) mnx=sx[q]; if (sx[q]>mxx) mxx=sx[q];
                                  if (sy[q]<mny) mny=sy[q]; if (sy[q]>mxy) mxy=sy[q]; }
        printf("col=%04x bbox=(%d,%d)-(%d,%d)\n", col, mnx, mny, mxx, mxy);
    }
#endif
    fill_poly(sx, sy, m, col);
    if (g_outline) {
        int k;
        for (k = 0; k < m; k++) {
            int j = (k + 1) % m;
            draw_line(sx[k], sy[k], sx[j], sy[j], g_outline);
        }
    }
}

static void quad(int x1, int y1, int z1, int x2, int y2, int z2,
                 int x3, int y3, int z3, int x4, int y4, int z4, uint16_t col)
{
    V3 p[4];
    p[0].x = x1; p[0].y = y1; p[0].z = z1;
    p[1].x = x2; p[1].y = y2; p[1].z = z2;
    p[2].x = x3; p[2].y = y3; p[2].z = z3;
    p[3].x = x4; p[3].y = y4; p[3].z = z4;
    draw_world_poly(p, 4, col);
}

/* ------------------------------------------------------------------ */
/* track                                                               */
/* ------------------------------------------------------------------ */
#define MAXSEG 4096
typedef struct {
    int x, z;     /* start of segment (world units) */
    int s0;       /* distance where it starts */
    int len;
    int dir;
    int turn;     /* +1 = 90 deg left at the end, -1 = right */
} Seg;

static Seg seg[MAXSEG];
static int nseg;

#define MAXOBJ 2048
/* OB_JUMPWALL spans the whole corridor: no dodging, only a jump gets you over */
enum { OB_BLOCK, OB_LOW, OB_COIN, OB_JUMPWALL };
typedef struct {
    int s;
    int8_t lane;
    uint8_t type;
    uint8_t used;
} Obj;
static Obj objs[MAXOBJ];
static int nobj;

/* ------------------------------------------------------------------ */
/* RNG                                                                 */
/* ------------------------------------------------------------------ */
static uint32_t rng_state = 0x12345678u;
static uint32_t seed_entropy = 0;
static inline uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static inline int rnd_range(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

/* Mother of all seeds: the host feeds in whatever noise it can find (clock,
   leftover screen content, how long the player stared at the title). */
void tr_seed(uint32_t v)
{
    seed_entropy = v ^ 0x9e3779b9u;
}

/* ------------------------------------------------------------------ */
/* difficulty curve                                                    */
/* ------------------------------------------------------------------ */
/* Speed is a function of *distance*, not of elapsed frames.  That way the
   curve is identical on every machine, and the map generator can tell
   exactly how fast the player will be moving when he gets to a given spot --
   which is what makes a fair difficulty ramp possible. */
#define SPEED_MIN 900
#define SPEED_MAX 2100
#define SPEED_RAMP 35000     /* distance at which half of the gain is spent */

static int speed_at(int s)
{
    int v;
    if (s < 0) s = 0;
    v = SPEED_MIN + (int)((int64_t)1200 * s / (s + SPEED_RAMP));
    if (v > SPEED_MAX) v = SPEED_MAX;
    return v;
}

/* How hard the track is at distance s, in percent.  Climbs fast in the first
   half minute, then flattens: the last stretch of the bar belongs to people
   who survive a couple of minutes. */
static int diff_pct(int s)
{
    int d;
    if (s < 0) s = 0;
    d = (int)((int64_t)s * 100 / (s + 16000));
    if (d > 92) d = 92;
    return d;
}

/* The corner window is a slice of *time*, so at top speed it covers more
   ground.  Nothing is ever spawned inside it -- jumping over a wall while
   also having to hit the turn key is not a skill, it is a coin flip. */
static int turn_window_at(int spd)
{
    int w = spd * 23 / 30;
    if (w < 620) w = 620;
    if (w > DRAW_FAR - 400) w = DRAW_FAR - 400;
    return w;
}

/* ------------------------------------------------------------------ */
/* player / game state                                                 */
/* ------------------------------------------------------------------ */
enum { ST_TITLE, ST_PLAY, ST_FALL, ST_CRASH, ST_OVER, ST_QUIT };

typedef struct {
    int64_t s;        /* 16.16 distance travelled */
    int lane_target;  /* -1,0,1 */
    int32_t lat;      /* 16.16 lateral offset */
    int32_t y;        /* 16.16 height above floor */
    int32_t vy;
    int state;
    int score;
    int coins;
    int best;
    int t;            /* frames alive */
    int crash_t;
    int turned;       /* consumed the pending turn */
    int cur_seg;      /* segment the player is in */
    int flash;        /* turn success flash */
    int prev_keys;
    int speed;
    int spd_lvl;      /* speed notch last announced, for the "FASTER!" flash */
    int boost;
    int64_t total_s;
    /* falling off the track */
    int fall_t;
    int fx, fz;       /* world position while airborne over the void */
    int ledge_x, ledge_z;   /* camera spot on the rim, frozen while he drops */
    int fdir;         /* heading he keeps while falling */
    int32_t fy, fvy;
    int fell;         /* died by falling, not by an obstacle */
} Player;

static Player P;

static int seg_index_for(int s, int hint)
{
    int i = hint;
    if (i < 0) i = 0;
    while (i > 0 && s < seg[i].s0) i--;
    while (i < nseg - 1 && s >= seg[i].s0 + seg[i].len) i++;
    return i;
}

static void path_at(int s, int *x, int *z, int *dir)
{
    /* everything asks for points near the camera, so start from where he is
       instead of scanning the whole (now very long) segment list */
    int i = seg_index_for(s, P.cur_seg);
    int loc = s - seg[i].s0;
    int rx, rz;
    if (loc < 0) loc = 0;
    if (loc > seg[i].len) loc = seg[i].len;
    heading_right(seg[i].dir, &rx, &rz);
    *x = seg[i].x + DIRX[seg[i].dir] * loc;
    *z = seg[i].z + DIRZ[seg[i].dir] * loc;
    *dir = seg[i].dir;
    (void)rx; (void)rz;
}

/* Which way does the corner at the END of segment i actually bend, seen from
 * the runner's seat?  +1 = the track hooks to his right (key 6),
 * -1 = to his left (key 4).  Everything that has to agree about a corner
 * (geometry, key gate, on-screen arrow) goes through this one function, so
 * the arrow can never point away from the road. */
static int corner_dir(int i)
{
    int d0, d1, rx, rz, dot;
    if (i < 0) i = 0;
    if (i >= nseg) i = nseg - 1;
    d0 = seg[i].dir;
    d1 = (i + 1 < nseg) ? seg[i + 1].dir
                        : ((d0 + (seg[i].turn > 0 ? 3 : 1)) & 3);
    rx = DIRZ[d0];          /* right-hand vector of the current heading */
    rz = -DIRX[d0];
    dot = DIRX[d1] * rx + DIRZ[d1] * rz;
    return (dot > 0) ? 1 : -1;
}

static void add_segment(void)
{
    Seg *last;
    Seg *n;
    int ex, ez, dir, turn, spd;
    if (nseg >= MAXSEG) return;
    last = &seg[nseg - 1];
    ex = last->x + DIRX[last->dir] * last->len;
    ez = last->z + DIRZ[last->dir] * last->len;
    turn = (rnd() & 1) ? 1 : -1;
    dir = (last->dir + (turn > 0 ? 3 : 1)) & 3;
    n = &seg[nseg];
    n->x = ex;
    n->z = ez;
    n->s0 = last->s0 + last->len;
    spd = speed_at(n->s0);
    /* A segment has to hold its content *plus* the corner window at the end,
       where nothing may be spawned.  Sizing it as "content + window" instead
       of a flat range keeps the obstacle density the same at every speed --
       otherwise the corners silently swallow a third of the track late in the
       run and the game gets *easier* while the bar says it is harder. */
    n->len = turn_window_at(spd) + 320 + rnd_range(2400, 4200);
    n->dir = dir;
    n->turn = (rnd() & 1) ? 1 : -1;
    nseg++;
}

static void add_obj(int s, int lane, int type)
{
    if (nobj >= MAXOBJ) return;
    objs[nobj].s = s;
    objs[nobj].lane = (int8_t)lane;
    objs[nobj].type = (uint8_t)type;
    objs[nobj].used = 0;
    nobj++;
}

/* Everything behind the player is dead weight: drop it so the table cannot
   fill up and silently stop spawning on a long run. */
static void compact_objs(int keep_from)
{
    int i, j = 0;
    for (i = 0; i < nobj; i++)
        if (objs[i].s >= keep_from) objs[j++] = objs[i];
    nobj = j;
}

static void add_coin_line(int s, int lane, int n, int step)
{
    int k;
    for (k = 0; k < n; k++) add_obj(s + k * step, lane, OB_COIN);
}

static int other_lane(int lane)
{
    int l = rnd_range(-1, 1);
    while (l == lane) l = rnd_range(-1, 1);
    return l;
}

/* Coins read best where the player already is, so bias them towards the
   middle lane -- a trail over in lane 3 that nobody can reach is just decor. */
static int coin_lane(void)
{
    int r = rnd_range(0, 3);
    return (r == 0) ? -1 : (r == 1) ? 1 : 0;
}

static int coin_lane_avoiding(int blocked)
{
    if (blocked != 0 && (rnd() & 1)) return 0;
    return other_lane(blocked);
}

/* ------------------------------------------------------------------ */
/* map generation                                                      */
/* ------------------------------------------------------------------ */
/* The track is built out of "slots".  One slot = one thing that asks the
   player to do something, and the gap to the next slot shrinks with the
   difficulty as measured in *frames* (36 at the start, 23 near the top).
   The distance between slots therefore grows even though they come faster,
   which is what keeps the thing playable at 2100 units/s. */
enum { F_COINS, F_SINGLE, F_DOUBLE, F_GATE, F_SLALOM, F_BREATHE, F_NFEAT };

static int slot_gap(int s)
{
    int gf = 36 - 15 * diff_pct(s) / 100;
    int g = speed_at(s) * gf / 30;
    if (g < 950) g = 950;
    return g;
}

static int pick_feature(int d, int gate_ok)
{
    int w[F_NFEAT], tot = 0, i, r;
    w[F_COINS]   = 24 - d / 7;           /* fewer freebies once it gets busy */
    w[F_SINGLE]  = 42;
    w[F_DOUBLE]  = 5 + d / 6;            /* two lanes shut: pick the free one */
    w[F_GATE]    = gate_ok ? 11 : 0;     /* full width, jump or die          */
    w[F_SLALOM]  = (d > 22) ? (d - 22) / 5 : 0;
    w[F_BREATHE] = 13 - d / 9;
    for (i = 0; i < F_NFEAT; i++) {
        if (w[i] < 0) w[i] = 0;
        tot += w[i];
    }
    r = rnd_range(0, tot - 1);
    for (i = 0; i < F_NFEAT; i++) {
        if (r < w[i]) return i;
        r -= w[i];
    }
    return F_SINGLE;
}

static int last_gate = -1;   /* s of the previous full-width wall, -1 = none */

static void populate_segment(int idx)
{
    Seg *sg = &seg[idx];
    int s, end, gate_ok, last = -1;

    /* never inside the corner window, plus a little room to line up */
    end = sg->s0 + sg->len - turn_window_at(speed_at(sg->s0 + sg->len)) - 320;
    s = sg->s0 + (idx == 0 ? 1600 : 700);
    gate_ok = (sg->len > 3000);

    while (s < end) {
        int gap = slot_gap(s);
        int f = pick_feature(diff_pct(s), gate_ok);

        gap += gap * rnd_range(-18, 25) / 100;

        /* don't let the heavy features clump up, that reads as a wall of noise */
        if (f == last && (f == F_DOUBLE || f == F_SLALOM || f == F_GATE))
            f = F_SINGLE;
        last = f;

        switch (f) {
        case F_SINGLE: {
            int lane = rnd_range(-1, 1);
            int low = (rnd_range(0, 99) < 45);   /* roughly half can be jumped */
            add_obj(s, lane, low ? OB_LOW : OB_BLOCK);
            if (rnd_range(0, 2) && s + 520 < end) add_coin_line(s + 170, coin_lane_avoiding(lane), 4, 105);
            break;
        }
        case F_DOUBLE: {
            int free_lane = rnd_range(-1, 1);
            int low = (rnd_range(0, 99) < 25);   /* both low: jump, don't dodge */
            int l;
            for (l = -1; l <= 1; l++) {
                if (l == free_lane) continue;
                add_obj(s, l, low ? OB_LOW : OB_BLOCK);
            }
            if (rnd_range(0, 2) && s + 520 < end) add_coin_line(s + 170, free_lane, 4, 105);
            break;
        }
        case F_GATE: {
            int gl = rnd_range(-1, 1);
            /* two walls in a row is a rhythm, not a challenge: keep ~3 s between
               them so each one still lands as an event */
            if (last_gate >= 0 && s - last_gate < speed_at(s) * 3) {
                int lane = rnd_range(-1, 1);
                add_obj(s, lane, (rnd_range(0, 99) < 45) ? OB_LOW : OB_BLOCK);
                break;
            }
            last_gate = s;
            add_obj(s, 0, OB_JUMPWALL);
            /* collect them mid-air */
            if (s + 520 < end) add_coin_line(s + 240, gl, 3, 130);
            gate_ok = 0;
            /* he is off the ground for ~22 frames: give him a landing strip */
            gap += speed_at(s) * 22 / 30;
            break;
        }
        case F_SLALOM: {
            int a = rnd_range(-1, 1);
            int b = other_lane(a);
            int step = gap * 6 / 10, k;
            if (s + step * 2 > end) {       /* no room, demote to a single */
                int lane = rnd_range(-1, 1);
                add_obj(s, lane, (rnd_range(0, 99) < 45) ? OB_LOW : OB_BLOCK);
                break;
            }
            for (k = 0; k < 3; k++) add_obj(s + k * step, (k & 1) ? a : b, OB_BLOCK);
            gap = step * 3;
            break;
        }
        case F_COINS:
            if (s + 620 < end) add_coin_line(s, coin_lane(), rnd_range(4, 6), 110);
            break;
        case F_BREATHE:
        default:
            gap = gap * 3 / 2;      /* empty stretch to breathe and re-centre */
            break;
        }
        s += gap;
    }
}

static int build_no;

static void build_track(void)
{
    int i;
    /* entropy from the machine, the frame he pressed start on, and a counter so
       that restarting twice from the exact same frame still gives a new map */
    uint32_t sd = seed_entropy
                ^ ((uint32_t)P.t * 40503u)
                ^ ((uint32_t)++build_no * 2654435761u)
                ^ 0x5bf03635u;
    if (!sd) sd = 0x9e3779b9u;
    rng_state = sd;
    for (i = 0; i < 8; i++) rnd();      /* stir the pot before using it */

    nseg = 0;
    nobj = 0;
    last_gate = -1;
    seg[0].x = 0;
    seg[0].z = 0;
    seg[0].s0 = 0;
    seg[0].len = 3600;
    seg[0].dir = 0;
    seg[0].turn = 1;
    nseg = 1;
    populate_segment(0);
    for (i = 0; i < 30; i++) {
        add_segment();
        populate_segment(nseg - 1);
    }
}

/* ------------------------------------------------------------------ */
/* sound-free feedback helpers                                         */
/* ------------------------------------------------------------------ */
static void reset_player(void)
{
    int i;
    P.s = 0;
    P.lane_target = 0;
    P.lat = 0;
    P.y = 0;
    P.vy = 0;
    P.score = 0;
    P.coins = 0;
    P.t = 0;
    P.crash_t = 0;
    P.turned = 0;
    P.cur_seg = 0;
    P.flash = 0;
    P.speed = SPEED_MIN;
    P.spd_lvl = 0;
    P.boost = 0;
    P.prev_keys = 0;
    for (i = 0; i < nobj; i++) objs[i].used = 0;
    /* start a bit into the first segment */
    P.s = 200 << 16;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* update                                                              */
/* ------------------------------------------------------------------ */
#ifndef HOST_PREVIEW
static void best_load(void)
{
    FILE *f = fopen("ntemple.hi", "r");
    if (f) {
        if (fscanf(f, "%d", &P.best) != 1) P.best = 0;
        fclose(f);
    }
    if (P.best < 0) P.best = 0;
}

static void best_save(void)
{
    FILE *f = fopen("ntemple.hi", "w");
    if (f) {
        fprintf(f, "%d\n", P.best);
        fclose(f);
    }
}
#else
static void best_load(void) {}
static void best_save(void) {}
#endif

static void crash(void)
{
    if (P.state != ST_PLAY) return;
    P.state = ST_CRASH;
    P.crash_t = 0;
    P.fell = 0;
    if (P.score > P.best) { P.best = P.score; best_save(); }
}

/* Missed (or ignored) the corner: he keeps running straight off the end of
   the track and drops into the void. */
static void fall_off(void)
{
    Seg *cs = &seg[P.cur_seg];
    int px, pz, d, lat;
    if (P.state != ST_PLAY) return;
    /* he runs off the end of the segment he was on, still heading the old way */
    px = cs->x + DIRX[cs->dir] * cs->len;
    pz = cs->z + DIRZ[cs->dir] * cs->len;
    d = cs->dir;
    lat = (int)(P.lat >> 16);
    px += DIRZ[d] * lat;
    pz += -DIRX[d] * lat;
    P.state = ST_FALL;
    P.fall_t = 0;
    P.fx = px;
    P.fz = pz;
    P.fdir = d;
    P.ledge_x = px - ((cam_sin * CAM_BACK) >> 16);
    P.ledge_z = pz - ((cam_cos * CAM_BACK) >> 16);
    /* if he was mid-jump, take him down to a height the camera can still see */
    P.fy = (P.y > (140 << 16)) ? (140 << 16) : P.y;
    P.fvy = 0;
    P.fell = 1;
    if (P.score > P.best) { P.best = P.score; best_save(); }
}

static int turn_window(void);

static void update_play(int keys, int pressed)
{
    Seg *cs;
    int i, s_int;

    P.t++;
    P.speed = speed_at((int)(P.s >> 16));
    if (P.speed / 100 > P.spd_lvl) P.boost = 44;    /* hit a new notch */
    P.spd_lvl = P.speed / 100;
    if (P.boost > 0) P.boost--;

    /* lateral smoothing */
    {
        int32_t target = P.lane_target * (LANE_W << 16);
        int32_t d = target - P.lat;
        int32_t step = (1300 << 16) / 30 * 2;
        if (d > step) d = step;
        if (d < -step) d = -step;
        P.lat += d;
    }

    /* jump physics */
    if (P.y > 0 || P.vy > 0) {
        P.vy -= (5200 << 16) / 30;
        P.y += P.vy / 30;
        if (P.y <= 0) {
            P.y = 0;
            P.vy = 0;
        }
    }
    if (P.flash > 0) P.flash--;

    /* advance */
    P.s += ((int64_t)P.speed << 16) / 30;

    s_int = (int)(P.s >> 16);
    if (P.cur_seg >= nseg) P.cur_seg = nseg - 1;
    cs = &seg[P.cur_seg];

    /* ---- turn gate ---- */
    {
        int end = cs->s0 + cs->len;
        int window_start = end - turn_window();
        if (!P.turned) {
            int want = corner_dir(P.cur_seg);   /* +1 = press 6, -1 = press 4 */
            if (s_int >= window_start && s_int <= end) {
                int tk = pressed | keys;   /* forgiving: holding the key counts */
                if (want > 0 ? (tk & K_TURN_R) : (tk & K_TURN_L)) {
                    P.turned = 1;
                    P.flash = 14;
                }
            }
            if (s_int > end) { fall_off(); return; }
        } else if (s_int > end) {
            P.cur_seg++;
            P.turned = 0;
        }
    }

    /* keep the track ahead */
    while (seg[nseg - 1].s0 + seg[nseg - 1].len < s_int + DRAW_FAR + 3000 && nseg < MAXSEG) {
        add_segment();
        populate_segment(nseg - 1);
    }

    /* ---- collisions ---- */
    for (i = 0; i < nobj; i++) {
        Obj *o = &objs[i];
        int ds, dl, full;
        if (o->used) continue;
        ds = o->s - s_int;
        if (ds > 120) continue;
        if (ds < -80) continue;
        full = (o->type == OB_JUMPWALL);
        dl = o->lane * LANE_W - (int)(P.lat >> 16);
        if (dl < 0) dl = -dl;
        if (o->type == OB_COIN) {
            if (dl < 120) {
                o->used = 1;
                P.coins++;
                P.score += 5;
            }
            continue;
        }
        if (!full && dl > 110) continue;
        {   /* hit checks */
            int ph = (int)(P.y >> 16);
            int hit = 0;
            if (o->type == OB_BLOCK) hit = 1;
            else if (o->type == OB_LOW) hit = (ph < 130);
            else if (o->type == OB_JUMPWALL) hit = (ph < 130);
            if (hit) {
#ifdef DEBUG_LOG
                printf("HIT type=%d s=%d lane=%d ph=%d\n", o->type, s_int, o->lane, ph);
#endif
                crash();
                return;
            }
        }
    }

    /* ---- score from distance ---- */
    P.score = s_int / 90 + P.coins * 5;
    if (P.score > P.best) P.best = P.score;

    /* recycling: the object table must never fill up on a long run */
    if (nobj > MAXOBJ * 3 / 4) compact_objs(s_int - 2500);
}

static void update_fall(void)
{
    int fwd;
    P.fall_t++;
    /* he keeps his momentum for a few frames, then tumbles and drops */
    fwd = P.speed / 30;
    if (P.fall_t > 5) {
        int k = 40 - P.fall_t;
        if (k < 0) k = 0;
        fwd = fwd * k / 40;
    }
    P.fx += DIRX[P.fdir] * fwd;
    P.fz += DIRZ[P.fdir] * fwd;
    P.fvy -= (5000 << 16) / 30;
    P.fy += P.fvy / 30;
    if (P.y > 0) P.y += P.vy / 30;   /* still carrying a jump */
    if (P.fall_t > 34) {
        P.state = ST_OVER;
        P.t = 0;
    }
}

static void update(int keys, int pressed)
{
    if (keys & K_QUIT) {
        P.state = ST_QUIT;
        return;
    }
    switch (P.state) {
    case ST_TITLE:
        if (pressed & K_START) {
            build_track();
            reset_player();
            P.state = ST_PLAY;
        }
        break;
    case ST_PLAY:
        if (pressed & K_LANE_L) P.lane_target--;
        if (pressed & K_LANE_R) P.lane_target++;
        if (P.lane_target < -1) P.lane_target = -1;
        if (P.lane_target > 1) P.lane_target = 1;
        if ((pressed & K_JUMP) && P.y == 0) {
            P.vy = (1900 << 16);
        }
        update_play(keys, pressed);
        break;
    case ST_CRASH:
        P.crash_t++;
        P.s += ((int64_t)P.speed << 16) / 30;
        if (P.crash_t > 16) P.state = ST_OVER;
        break;
    case ST_FALL:
        update_fall();
        break;
    case ST_OVER:
        if (pressed & K_START) {
            build_track();
            reset_player();
            P.state = ST_PLAY;
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* rendering                                                           */
/* ------------------------------------------------------------------ */
static uint16_t COL_FLOOR_A, COL_FLOOR_B, COL_WALL_A, COL_WALL_B, COL_PILLAR,
                COL_CEIL, COL_SKY, COL_GOLD, COL_GOLD_D, COL_OBS, COL_OBS_D,
                COL_TEXT, COL_TEXT_D, COL_ACCENT, COL_SHADOW, COL_WHITE,
                COL_SKY_TOP, COL_SKY_MID, COL_SKY_HOR, COL_SUN, COL_HILL_FAR, COL_HILL_NEAR,
                COL_EDGE, COL_EDGE_D, COL_TURN, COL_OBS_HI;

static void init_palette(void)
{
    COL_FLOOR_A = C(214, 194, 160);
    COL_FLOOR_B = C(150, 134, 108);
    COL_WALL_A = C(150, 128, 112);
    COL_WALL_B = C(124, 104, 92);
    COL_PILLAR = C(186, 166, 140);
    COL_CEIL = C(64, 56, 52);
    COL_SKY = C(26, 22, 30);
    COL_GOLD = C(255, 206, 64);
    COL_GOLD_D = C(196, 140, 24);
    COL_OBS = C(196, 64, 52);
    COL_OBS_D = C(130, 36, 30);
    COL_SKY_TOP = C(38, 56, 110);
    COL_SKY_MID = C(110, 140, 190);
    COL_SKY_HOR = C(250, 196, 148);
    COL_SUN = C(255, 236, 190);
    COL_HILL_FAR = C(96, 104, 140);
    COL_HILL_NEAR = C(58, 62, 96);
    COL_EDGE = C(96, 80, 70);
    COL_EDGE_D = C(58, 48, 44);
    COL_TURN = C(224, 154, 52);      /* chevron painted on a corner plaza */
    COL_OBS_HI = C(232, 132, 96);    /* lighter top on the jump wall */
    COL_TEXT = C(245, 238, 220);
    COL_TEXT_D = C(20, 16, 18);
    COL_ACCENT = C(120, 220, 180);
    COL_SHADOW = C(10, 8, 10);
    COL_WHITE = C(255, 255, 255);
}

static void dimm_row(int y);

static int lerp565(uint16_t a, uint16_t b, int t)
{
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + (br - ar) * t / 256;
    int g = ag + (bg - ag) * t / 256;
    int bl = ab + (bb - ab) * t / 256;
    return (r << 11) | (g << 5) | bl;
}

/* soft disc (sun / moon), cheap: scanline with a circle test */
static void draw_disc(int cx, int cy, int r, uint16_t col)
{
    int y;
    for (y = -r; y <= r; y++) {
        int dx = 0, rr = r * r - y * y;
        while ((dx + 1) * (dx + 1) <= rr) dx++;
        hline(cy + y, cx - dx, cx + dx, col);
    }
}

static void draw_hill(int cx, int base_y, int half_w, int h, uint16_t col)
{
    int px[3], py[3];
    px[0] = cx - half_w; py[0] = base_y;
    px[1] = cx;          py[1] = base_y - h;
    px[2] = cx + half_w; py[2] = base_y;
    fill_poly(px, py, 3, col);
}

static void draw_background(void)
{
    int y, horizon = 118;
    int pan = cam_yaw_q16 >> 8;         /* 0..255 across a full turn */
    int i;

    for (y = 0; y < SH; y++) {
        uint16_t c;
        if (y < horizon) {
            int t = (y * 256) / horizon;
            c = (t < 150) ? lerp565(COL_SKY_TOP, COL_SKY_MID, t * 256 / 150)
                          : lerp565(COL_SKY_MID, COL_SKY_HOR, (t - 150) * 256 / 106);
        } else {
            int t = ((y - horizon) * 256) / (SH - horizon);
            c = lerp565(C(70, 84, 58), C(26, 34, 26), t);
        }
        hline(y, 0, SW - 1, c);
    }
    /* sun, drifts a bit with the view */
    draw_disc(84 - (pan - 128) / 3, 46, 13, COL_SUN);
    /* two parallax layers of distant hills */
    for (i = -1; i < 4; i++) {
        int x = i * 150 - ((pan * 3) / 2) % 150 + 60;
        draw_hill(x, horizon, 96, 34, COL_HILL_FAR);
    }
    for (i = -1; i < 3; i++) {
        int x = i * 190 - pan * 2 % 190 + 20;
        draw_hill(x + 40, horizon, 130, 18, COL_HILL_NEAR);
    }
}

/* camera yaw smoothing */
static int pitch_cur;       /* eases down while he falls */

static void update_camera(void)
{
    int s_cam;
    int px, pz, pdir, prx, prz;
    int lat = (int)(P.lat >> 16);
    int target, diff, pt;

    /* look further down when he drops off the edge */
    pt = (P.state == ST_FALL) ? PITCH_Q16 + 700 : PITCH_Q16;
    pitch_cur += (pt - pitch_cur) / 10;
    pit_cos = icos(pitch_cur);
    pit_sin = isin(pitch_cur);

    if (P.state == ST_FALL) {
        /* the camera stops on the rim and watches him drop away */
        cam_yaw_q16 = P.fdir * 16384;
        cam_cos = icos(cam_yaw_q16);
        cam_sin = isin(cam_yaw_q16);
        cam_x = P.ledge_x;
        cam_z = P.ledge_z;
        /* the rim camera follows his drop at half speed, so he stays in frame
           for a moment and then sinks out of the bottom */
        cam_y = CAM_HEIGHT + (int)(P.fy >> 16) / 2;
        if (cam_y < -900) cam_y = -900;
        return;
    }

    /* the camera looks along the section the runner is on, quickly eased */
    if (P.cur_seg >= nseg) P.cur_seg = nseg - 1;
    target = seg[P.cur_seg].dir * 16384;
    diff = target - cam_yaw_q16;
    while (diff > 32768) diff -= 65536;
    while (diff < -32768) diff += 65536;
    cam_yaw_q16 = (cam_yaw_q16 + diff / 2) & 65535;

    cam_cos = icos(cam_yaw_q16);
    cam_sin = isin(cam_yaw_q16);

    /* runner position on the track, then the camera sits straight behind it */
    s_cam = (int)(P.s >> 16);
    path_at(s_cam, &px, &pz, &pdir);
    heading_right(pdir, &prx, &prz);
    px += prx * lat;
    pz += prz * lat;

    cam_x = px - ((cam_sin * CAM_BACK) >> 16);
    cam_z = pz - ((cam_cos * CAM_BACK) >> 16);
    cam_y = CAM_HEIGHT + (int)(P.y >> 16) / 3;
}

/* draw one box in world space, given base centre and size */
static void draw_box(int cx, int cz, int w, int d, int y0, int y1, uint16_t top, uint16_t side, uint16_t front)
{
    int hw = w / 2, hd = d / 2;
    /* front face (facing -z relative to travel is not always right, but the
       corridor is axis aligned so a box's 4 sides can just all be drawn back
       to front; we use the two visible ones by testing camera position) */
    int dx = cam_x - cx, dz = cam_z - cz;
    /* top (or bottom, when the camera is below the box) */
    if (cam_y <= y0) {
        quad(cx - hw, y0, cz - hd, cx + hw, y0, cz - hd,
             cx + hw, y0, cz + hd, cx - hw, y0, cz + hd, side);
    } else {
        quad(cx - hw, y1, cz - hd, cx + hw, y1, cz - hd,
             cx + hw, y1, cz + hd, cx - hw, y1, cz + hd, top);
    }
    if (dz > 0) {  /* camera behind (-z side) : draw that face */
        quad(cx - hw, y0, cz - hd, cx + hw, y0, cz - hd,
             cx + hw, y1, cz - hd, cx - hw, y1, cz - hd, front);
    } else {
        quad(cx - hw, y0, cz + hd, cx + hw, y0, cz + hd,
             cx + hw, y1, cz + hd, cx - hw, y1, cz + hd, front);
    }
    if (dx > 0) {
        quad(cx - hw, y0, cz - hd, cx - hw, y0, cz + hd,
             cx - hw, y1, cz + hd, cx - hw, y1, cz - hd, side);
    } else {
        quad(cx + hw, y0, cz - hd, cx + hw, y0, cz + hd,
             cx + hw, y1, cz + hd, cx + hw, y1, cz - hd, side);
    }
}

/* a slab that spans the entire corridor at distance s (no lane can dodge it) */
static void draw_gate(int s, int depth, int y0, int y1,
                      uint16_t top, uint16_t side, uint16_t front)
{
    int px, pz, d;
    path_at(s, &px, &pz, &d);
    if (DIRX[d] == 0)
        draw_box(px, pz, 2 * HALF_W - 26, depth, y0, y1, top, side, front);
    else
        draw_box(px, pz, depth, 2 * HALF_W - 26, y0, y1, top, side, front);
}

static void seg_pt(int i, int sloc, int *x, int *z)
{
    *x = seg[i].x + DIRX[seg[i].dir] * sloc;
    *z = seg[i].z + DIRZ[seg[i].dir] * sloc;
}

static void draw_floor_strip(int i, int s0, int s1, uint16_t fcol, uint16_t ecol)
{
    int x1, z1, x2, z2, rx, rz, wi = 22;
    int dirx = DIRX[seg[i].dir], dirz = DIRZ[seg[i].dir];
    rx = DIRZ[seg[i].dir];
    rz = -dirx;
    seg_pt(i, s0 - seg[i].s0, &x1, &z1);
    seg_pt(i, s1 - seg[i].s0, &x2, &z2);

    quad(x1 - (rx * HALF_W), 0, z1 - (rz * HALF_W),
         x1 + (rx * HALF_W), 0, z1 + (rz * HALF_W),
         x2 + (rx * HALF_W), 0, z2 + (rz * HALF_W),
         x2 - (rx * HALF_W), 0, z2 - (rz * HALF_W), fcol);

    /* stone borders, clamped to the real corridor (not the extended ends) */
    {
        int b0 = s0, b1 = s1, bx1, bz1, bx2, bz2;
        if (b0 < seg[i].s0) b0 = seg[i].s0;
        if (b1 > seg[i].s0 + seg[i].len) b1 = seg[i].s0 + seg[i].len;
        /* keep the kerb away from the corner elbow so it does not cut across */
        if (i > 0 && b0 < seg[i].s0 + HALF_W) b0 = seg[i].s0 + HALF_W;
        if (i + 1 < nseg && b1 > seg[i].s0 + seg[i].len - HALF_W)
            b1 = seg[i].s0 + seg[i].len - HALF_W;
        if (b0 < b1) {
            seg_pt(i, b0 - seg[i].s0, &bx1, &bz1);
            seg_pt(i, b1 - seg[i].s0, &bx2, &bz2);
            quad(bx1 - (rx * HALF_W), 1, bz1 - (rz * HALF_W),
                 bx1 - (rx * (HALF_W - wi)), 1, bz1 - (rz * (HALF_W - wi)),
                 bx2 - (rx * (HALF_W - wi)), 1, bz2 - (rz * (HALF_W - wi)),
                 bx2 - (rx * HALF_W), 1, bz2 - (rz * HALF_W), ecol);
            quad(bx1 + (rx * HALF_W), 1, bz1 + (rz * HALF_W),
                 bx1 + (rx * (HALF_W - wi)), 1, bz1 + (rz * (HALF_W - wi)),
                 bx2 + (rx * (HALF_W - wi)), 1, bz2 + (rz * (HALF_W - wi)),
                 bx2 + (rx * HALF_W), 1, bz2 + (rz * HALF_W), ecol);
        }
    }
    (void)dirx; (void)dirz;
}

/* The stone plaza that fills a junction.  Both corridors are axis aligned, so
   the junction is simply a 2*HALF_W square centred on the corner point: the
   road visibly stops and hooks away, instead of the T shaped overlap the old
   per-segment extensions produced. */
/* an arrow painted flat on the floor, tip pointing along dir nd */
static void draw_chevron(int cx, int cz, int nd, int len, int half, int dk)
{
    int fx = DIRX[nd], fz = DIRZ[nd];
    int rx = DIRZ[nd], rz = -DIRX[nd];
    V3 p[3];
    p[0].x = cx - fx * len / 2 + rx * half;
    p[0].z = cz - fz * len / 2 + rz * half;
    p[1].x = cx + fx * len / 2;
    p[1].z = cz + fz * len / 2;
    p[2].x = cx - fx * len / 2 - rx * half;
    p[2].z = cz - fz * len / 2 - rz * half;
    p[0].y = p[1].y = p[2].y = 2;
    g_outline = dimm(COL_EDGE_D, dk);
    draw_world_poly(p, 3, dimm(COL_TURN, dk));
    g_outline = 0;
}

static void draw_plaza(int i, int send, int dk)
{
    int qx, qz, r = HALF_W - 22;
    seg_pt(i, send - seg[i].s0, &qx, &qz);

    quad(qx - HALF_W, 0, qz - HALF_W, qx + HALF_W, 0, qz - HALF_W,
         qx + HALF_W, 0, qz + HALF_W, qx - HALF_W, 0, qz + HALF_W,
         dimm(COL_EDGE, dk));
    quad(qx - r, 1, qz - r, qx + r, 1, qz - r,
         qx + r, 1, qz + r, qx - r, 1, qz + r,
         dimm(COL_FLOOR_A, dk));

    /* a big arrow on the stone, pointing where the track goes next */
    if (i + 1 < nseg)
        draw_chevron(qx, qz, seg[i + 1].dir, 420, 100, dk);
}

static void draw_world(void)
{
    int s_cam = (int)(P.s >> 16) - CAM_BACK;
    int far = s_cam + DRAW_FAR;
    int i;
    int last;

    if (s_cam < 0) s_cam = 0;
    last = seg_index_for(far, nseg - 1);

    for (i = last; i >= 0; i--) {
        int from = seg[i].s0, to = seg[i].s0 + seg[i].len;
        int end = to;
        int s, px, pz;
        if (to < s_cam) break;
        if (from < s_cam) from = s_cam;
        if (to > far) to = far;
        for (s = to; s > from; ) {
            /* Coarse strips far away, fine ones under his feet.  Out there a
               strip is two pixels tall, so this costs nothing visually -- but it
               keeps the polygon count where it was before the draw distance grew. */
            int dist = s - s_cam;
            int step = (dist > DRAW_FAR / 2) ? STEP * 2 : STEP;
            int e = s - step;
            int depth_k = dist * 7 / DRAW_FAR;
            int strip = ((s / 170) & 1);
            if (e < from) e = from;
            if (depth_k > 7) depth_k = 7;
            draw_floor_strip(i, e, s,
                             dimm(strip ? COL_FLOOR_A : COL_FLOOR_B, depth_k),
                             dimm(strip ? COL_EDGE : COL_EDGE_D, depth_k));
            s = e;
        }
        if (i + 1 < nseg) {
            int k;
            /* chevrons painted along the run-up so the turn is readable early */
            for (k = 0; k < 3; k++) {
                int cs = end - 520 - k * 240;
                int dk;
                if (cs <= seg[i].s0 + 120) continue;
                if (cs < s_cam + 300 || cs > far - 100) continue;
                dk = (cs - s_cam) * 7 / DRAW_FAR;
                if (dk > 7) dk = 7;
                seg_pt(i, cs - seg[i].s0, &px, &pz);
                draw_chevron(px, pz, seg[i + 1].dir, 200, 58, dk);
            }
            if (end >= s_cam && end <= far) {
                int dk = (end - s_cam) * 7 / DRAW_FAR;
                if (dk > 7) dk = 7;
                draw_plaza(i, end, dk);
            }
        }
    }

    /* objects, far to near (the array is generated in increasing s) */
    for (i = nobj - 1; i >= 0; i--) {
        Obj *o = &objs[i];
        int ox, oz, od, orx, orz, cx, cz, k;
        if (o->used) continue;
        if (o->s < s_cam || o->s > far) continue;
        path_at(o->s, &ox, &oz, &od);
        orx = DIRZ[od];
        orz = -DIRX[od];
        cx = ox + orx * (o->lane * LANE_W);
        cz = oz + orz * (o->lane * LANE_W);
        k = (o->s - s_cam) * 7 / DRAW_FAR;
        if (k < 0) k = 0;
        if (k > 7) k = 7;
        switch (o->type) {
        case OB_BLOCK:
            draw_box(cx, cz, 150, 150, 0, 330, dimm(COL_OBS, k), dimm(COL_OBS_D, k), dimm(COL_OBS, k));
            break;
        case OB_LOW:
            draw_box(cx, cz, 170, 60, 0, 105, dimm(COL_OBS_D, k), dimm(COL_OBS_D, k), dimm(COL_OBS, k));
            break;
        case OB_JUMPWALL:      /* low wall across the track: jump it */
            draw_gate(o->s, 70, 0, 118, dimm(COL_OBS_HI, k), dimm(COL_OBS_D, k),
                      dimm(COL_OBS, k));
            break;
        case OB_COIN: {
            int bob = isin((P.t * 3000 + o->s * 7) & 65535) / 2600;
            int spin = icos((P.t * 4000 + o->s * 11) & 65535);
            int wid = 26 * (spin < 0 ? -spin : spin) / 65536;
            int cy = 150 + bob;
            quad(cx - wid, cy, cz, cx, cy - 34, cz,
                 cx + wid, cy, cz, cx, cy + 34, cz, dimm(COL_GOLD, k));
            quad(cx - wid / 2, cy + 8, cz, cx + wid / 2, cy + 8, cz,
                 cx + wid / 2, cy + 20, cz, cx - wid / 2, cy + 20, cz, dimm(COL_GOLD_D, k));
            break;
        }
        }
    }
}

/* box drawn in the camera's local frame: r = lateral, f = forward from camera */
static void local_box(int r0, int w, int f0, int d, int y0, int y1,
                      uint16_t top, uint16_t side, uint16_t front)
{
    int f1 = CAM_BACK + f0 - d, f2 = CAM_BACK + f0 + d;
    int r1 = r0 - w, r2 = r0 + w;
    int sn = cam_sin, cs = cam_cos;
#define LPX(r, f) (cam_x + (int)(((long long)sn * (f) + (long long)cs * (r)) >> 16))
#define LPZ(r, f) (cam_z + (int)(((long long)cs * (f) - (long long)sn * (r)) >> 16))
    int x1 = LPX(r1, f1), z1 = LPZ(r1, f1);
    int x2 = LPX(r2, f1), z2 = LPZ(r2, f1);
    int x3 = LPX(r2, f2), z3 = LPZ(r2, f2);
    int x4 = LPX(r1, f2), z4 = LPZ(r1, f2);

    /* far face */
    quad(x4, y0, z4, x3, y0, z3, x3, y1, z3, x4, y1, z4, side);
    /* sides */
    quad(x1, y0, z1, x4, y0, z4, x4, y1, z4, x1, y1, z1, side);
    quad(x2, y0, z2, x3, y0, z3, x3, y1, z3, x2, y1, z2, side);
    /* top / bottom */
    if (cam_y <= y0)
        quad(x1, y0, z1, x2, y0, z2, x3, y0, z3, x4, y0, z4, side);
    else
        quad(x1, y1, z1, x2, y1, z2, x3, y1, z3, x4, y1, z4, top);
    /* near face */
    quad(x1, y0, z1, x2, y0, z2, x2, y1, z2, x1, y1, z1, front);
#undef LPX
#undef LPZ
}

/* one block of the falling runner, placed in world space around his fall
   position (the chase camera stays behind, so he has to leave it behind) */
static void fall_box(int r0, int w, int f0, int d, int y0, int y1,
                     uint16_t top, uint16_t side, uint16_t front)
{
    int d0 = P.fdir;
    int cx = P.fx + DIRZ[d0] * r0 + DIRX[d0] * f0;
    int cz = P.fz - DIRX[d0] * r0 + DIRZ[d0] * f0;
    if (DIRX[d0] == 0)
        draw_box(cx, cz, w * 2, d * 2, y0, y1, top, side, front);
    else
        draw_box(cx, cz, d * 2, w * 2, y0, y1, top, side, front);
}

/* tumbling pose while he drops into the void */
static void draw_player_fall(void)
{
    int y = (int)(P.fy >> 16);
    uint16_t skin = C(232, 186, 140), shirt = C(72, 116, 200), pants = C(40, 58, 120),
             shoe = C(36, 30, 34), hair = C(52, 36, 28);
    int flail = isin(P.fall_t * 2600) / 6000;

    g_outline = COL_SHADOW;
    fall_box(-15, 10, 10 + flail, 12, y, y + 46, pants, pants, pants);
    fall_box(15, 10, 10 - flail, 12, y, y + 46, pants, pants, pants);
    fall_box(-15, 12, 22 + flail, 16, y - 10, y + 4, shoe, shoe, shoe);
    fall_box(15, 12, 22 - flail, 16, y - 10, y + 4, shoe, shoe, shoe);
    fall_box(0, 27, 0, 15, y + 44, y + 98, shirt, shirt, shirt);
    fall_box(-34, 7, -6 + flail, 9, y + 62, y + 104, skin, skin, skin);
    fall_box(34, 7, -6 - flail, 9, y + 62, y + 104, skin, skin, skin);
    fall_box(0, 15, 0, 13, y + 98, y + 128, skin, skin, skin);
    fall_box(0, 16, -2, 13, y + 122, y + 134, hair, hair, hair);
    g_outline = 0;
}

/* runner, always drawn in the camera frame so he never looks sideways */
static void draw_player(void)
{
    int y = (int)(P.y >> 16);
    int lat = 0;   /* camera tracks the lane, so the runner stays centred */
    uint16_t skin = C(232, 186, 140), shirt = C(72, 116, 200), pants = C(40, 58, 120),
             shoe = C(36, 30, 34), hair = C(52, 36, 28);
    int leg_phase = (P.t * 5) & 15;
    int swing = (leg_phase < 8) ? leg_phase * 3 : (16 - leg_phase) * 3;

    if (P.state == ST_FALL) {
        draw_player_fall();
        return;
    }

    g_outline = COL_SHADOW;

    local_box(lat - 15, 10, swing, 12, y, y + 46, pants, pants, pants);
    local_box(lat + 15, 10, -swing, 12, y, y + 46, pants, pants, pants);
    local_box(lat - 15, 12, swing, 16, y, y + 12, shoe, shoe, shoe);
    local_box(lat + 15, 12, -swing, 16, y, y + 12, shoe, shoe, shoe);
    local_box(lat, 27, 0, 15, y + 44, y + 98, shirt, shirt, shirt);
    local_box(lat - 34, 7, -swing, 9, y + 50, y + 92, skin, skin, skin);
    local_box(lat + 34, 7, swing, 9, y + 40, y + 82, skin, skin, skin);
    local_box(lat, 15, 0, 13, y + 98, y + 128, skin, skin, skin);
    local_box(lat, 16, -2, 13, y + 122, y + 134, hair, hair, hair);

    g_outline = 0;
}

static void draw_hud(void)
{
    char buf[32];
    fill_rect(0, 0, SW, 25, C(24, 20, 24));
    hline(25, 0, SW - 1, C(96, 82, 70));
    sprintf(buf, "%d", P.score);
    draw_text(6, 4, "SCORE", COL_TEXT, COL_TEXT_D);
    draw_text(6, 14, buf, COL_GOLD, COL_TEXT_D);

    sprintf(buf, "%d", P.coins);
    draw_text(SW - 6 - text_width("COINS"), 4, "COINS", COL_TEXT, COL_TEXT_D);
    draw_text(SW - 6 - text_width(buf), 14, buf, COL_GOLD, COL_TEXT_D);

    /* speed multiplier: 1.0 at the start, up to 2.3 at the very top.
       Upper case only -- the baked font stops at 'Z'. */
    {
        int m = P.speed * 10 / SPEED_MIN;
        sprintf(buf, "SPD %d.%d", m / 10, m % 10);
        draw_text(SW / 2 - text_width(buf) / 2, 9, buf, COL_ACCENT, COL_TEXT_D);
    }

    if (P.flash) {
        draw_text_center(30, P.flash > 7 ? "TURN!" : "TURN!", COL_ACCENT, COL_TEXT_D);
    }
    if (P.boost > 6 && P.state == ST_PLAY) {
        int w = text_width("FASTER!");
        fill_rect(SW / 2 - w / 2 - 4, SH - 17, w + 8, 14, COL_TEXT_D);
        draw_text_center(SH - 14, "FASTER!", COL_GOLD, COL_TEXT_D);
    }
}

static void draw_arrow(int cx, int cy, int dir, uint16_t col);

/* the corner window is a fixed slice of time, not distance: at top speed you
   still get the same ~0.75 s to react */
static int turn_window(void) { return turn_window_at(P.speed); }

/* blinking "press 4 / 6" prompt when approaching a corner */
static void draw_turn_prompt(void)
{
    int si = seg_index_for((int)(P.s >> 16), P.cur_seg);
    int end = seg[si].s0 + seg[si].len;
    int s = (int)(P.s >> 16);
    if (P.state != ST_PLAY || P.turned) return;
    if (s < end - turn_window()) return;
    if ((P.t >> 2) & 1) return;

    if (corner_dir(si) < 0) {      /* the road hooks to his left: key 4 */
        draw_arrow(SW / 2 - 74, 78, -1, COL_ACCENT);
        draw_text(SW / 2 - 86, 86, "4", COL_WHITE, COL_TEXT_D);
        draw_text(SW / 2 - 46, 86, "<< TURN", COL_ACCENT, COL_TEXT_D);
    } else {                       /* hooks to his right: key 6 */
        draw_arrow(SW / 2 + 74, 78, 1, COL_ACCENT);
        draw_text(SW / 2 + 80, 86, "6", COL_WHITE, COL_TEXT_D);
        draw_text(SW / 2 + 18, 86, "TURN >>", COL_ACCENT, COL_TEXT_D);
    }
}

static void draw_arrow(int cx, int cy, int dir, uint16_t col)
{
    /* dir -1 left, +1 right : chevron made of two thick strokes */
    int i;
    for (i = 0; i < 3; i++) {
        int off = i * 5;
        if (dir < 0) {
            int px[3], py[3];
            px[0] = cx + 18 + off; py[0] = cy - 18;
            px[1] = cx - 4 - off; py[1] = cy;
            px[2] = cx + 18 + off; py[2] = cy + 18;
            fill_poly(px, py, 3, col);
        } else {
            int px[3], py[3];
            px[0] = cx - 18 - off; py[0] = cy - 18;
            px[1] = cx + 4 + off; py[1] = cy;
            px[2] = cx - 18 - off; py[2] = cy + 18;
            fill_poly(px, py, 3, col);
        }
    }
}

static void draw_overlay(void)
{
    int i;
    if (P.state == ST_TITLE) {
        fill_rect(0, 70, SW, 104, COL_SHADOW);
        draw_text_center(84, "NTEMPLE", COL_GOLD, COL_TEXT_D);
        draw_text_center(104, "NDLESS EDITION", COL_TEXT, COL_TEXT_D);
        draw_text_center(128, "7/9 LANE   8 JUMP", COL_TEXT, COL_TEXT_D);
        draw_text_center(142, "4/6 TURN AT CORNERS", COL_ACCENT, COL_TEXT_D);
        if ((P.t >> 4) & 1) draw_text_center(164, "PRESS 5 TO START", COL_WHITE, COL_TEXT_D);
    } else if (P.state == ST_OVER) {
        char buf[40];
        for (i = 0; i < SH; i += 2) dimm_row(i);
        fill_rect(20, 74, SW - 40, 92, COL_SHADOW);
        draw_text_center(84, P.fell ? "FELL OFF!" : "GAME OVER", COL_OBS, COL_TEXT_D);
        sprintf(buf, "SCORE %d", P.score);
        draw_text_center(104, buf, COL_TEXT, COL_TEXT_D);
        sprintf(buf, "COINS %d   BEST %d", P.coins, P.best);
        draw_text_center(116, buf, COL_GOLD, COL_TEXT_D);
        if ((P.t >> 4) & 1) draw_text_center(142, "PRESS 5 TO RUN AGAIN", COL_WHITE, COL_TEXT_D);
    }
}

/* helper for the game-over dimming */
static void dimm_row(int y)
{
    int x;
    uint16_t *p = fb + y * SW;
    for (x = 0; x < SW; x++) p[x] = dimm(p[x], 5);
}

/* ------------------------------------------------------------------ */
/* public entry points                                                 */
/* ------------------------------------------------------------------ */
void tr_init(void)
{
    build_no = 0;      /* a session is replayable from its seed alone */
    init_palette();
    build_track();
    reset_player();
    P.state = ST_TITLE;
    best_load();
    cam_yaw_q16 = 0;
    cam_cos = icos(0);
    cam_sin = isin(0);
    pitch_cur = PITCH_Q16;
    pit_cos = icos(PITCH_Q16);
    pit_sin = isin(PITCH_Q16);
    P.best = 0;
}

uint16_t *tr_framebuffer(void) { return fb; }
int tr_state(void) { return P.state; }
int tr_score(void) { return P.score; }

void tr_update(int keys)
{
    int pressed = keys & ~P.prev_keys;
    P.prev_keys = keys;
    update(keys, pressed);
    if (P.state != ST_PLAY) P.t++;
}

void tr_render(void)
{
    draw_background();
    update_camera();
    draw_world();
    if (P.state == ST_PLAY || P.state == ST_CRASH || P.state == ST_FALL) {
        draw_player();
        if (P.state == ST_PLAY) draw_turn_prompt();
    }
    if (P.state != ST_TITLE) draw_hud();
    draw_overlay();
}
