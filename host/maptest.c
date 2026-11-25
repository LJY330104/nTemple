/* Map generator self-test.
 *
 * "Playable" is not a vibe, it is a set of invariants.  This walks a few
 * hundred freshly seeded maps and checks them:
 *
 *   1. no cluster of obstacles ever blocks all three lanes without a
 *      jumpable full-width wall,
 *   2. consecutive clusters are always far enough apart in *frames* for a
 *      lane change (or a jump followed by a landing) to be physically
 *      possible,
 *   3. nothing is ever spawned inside a corner window,
 *   4. two full-width walls are never adjacent,
 *   5. no coin sits inside a wall the player cannot pass through,
 *   6. every seed really produces a different map,
 *   7. the speed / difficulty curves only ever go the right way.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../game.c"

static int fails;
static const char *cur = "?";

#define FAIL(...) do { \
    if (fails < 12) { printf("FAIL seed %s: ", cur); printf(__VA_ARGS__); printf("\n"); } \
    fails++; \
} while (0)

static Obj sorted[MAXOBJ];

static int cmp_obj(const void *pa, const void *pb)
{
    const Obj *a = (const Obj *)pa, *b = (const Obj *)pb;
    if (a->s != b->s) return a->s < b->s ? -1 : 1;
    return (int)a->lane - (int)b->lane;
}

#define CLUSTER_SPAN 90      /* obstacles this close are one "thing to deal with" */
#define MIN_FRAMES 11        /* a lane change takes 2, an eye-blink needs more */

/* frames needed to travel from a to b at the current speed */
static int frames_between(int s1, int s2)
{
    int spd = speed_at(s1);
    if (s2 <= s1) return 0;
    return (int)((int64_t)(s2 - s1) * 30 / spd);
}

/* same, but integrating the speed ramp (for the density statistics) */
static int frames_over(int a, int b)
{
    int s, f = 0;
    for (s = a; s < b; s += 500) {
        int hi = s + 500 < b ? s + 500 : b;
        f += frames_between(s, hi);
    }
    return f;
}

static void check_map(void)
{
    int i, n = 0;
    int prev_end = -1, prev_gate = 0;

    for (i = 0; i < nobj; i++)
        if (objs[i].type != OB_COIN) sorted[n++] = objs[i];
    qsort(sorted, n, sizeof(Obj), cmp_obj);

    /* ---- 1/4/2: walk clusters in order ---- */
    i = 0;
    while (i < n) {
        int start = sorted[i].s, end = start, j;
        int hard[3] = {0, 0, 0}, low[3] = {0, 0, 0}, gate = 0, free_lane = -1;

        for (j = i; j < n && sorted[j].s - start <= CLUSTER_SPAN; j++) {
            int l = sorted[j].lane + 1;
            if (sorted[j].type == OB_JUMPWALL) gate = 1;
            else if (sorted[j].type == OB_LOW) low[l] = 1;
            else hard[l] = 1;
            if (sorted[j].s > end) end = sorted[j].s;
        }

        if (!gate) {
            int l;
            for (l = 0; l < 3; l++) if (!hard[l]) free_lane = l;
            if (free_lane < 0)
                FAIL("all three lanes hard-blocked at s=%d", start);
        }

        if (i > 0) {
            int fr = frames_between(prev_end, start);
            int need = prev_gate ? 30 : MIN_FRAMES;
            if (fr < need)
                FAIL("%d frames between s=%d and s=%d (need %d%s)",
                     fr, prev_end, start, need, prev_gate ? ", after a wall" : "");
        }
        if (gate && prev_gate && frames_between(prev_end, start) < 60)
            FAIL("two full-width walls %d apart at s=%d",
                 start - prev_end, start);

        prev_end = end;
        prev_gate = gate;
        i = j;
    }

    /* ---- 5: coins never sit inside a hard obstacle ---- */
    for (i = 0; i < nobj; i++) {
        int j;
        if (objs[i].type != OB_COIN) continue;
        for (j = 0; j < nobj; j++) {
            if (objs[j].type == OB_COIN) continue;
            if (objs[j].lane != objs[i].lane) continue;
            if (objs[j].s - objs[i].s > 70 || objs[i].s - objs[j].s > 70) continue;
            FAIL("coin inside obstacle at s=%d lane=%d", objs[i].s, (int)objs[i].lane);
            break;
        }
    }

    /* ---- 3: corner windows stay clear of things that can kill him ---- */
    for (i = 0; i + 1 < nseg; i++) {
        int end = seg[i].s0 + seg[i].len;
        int from = end - turn_window_at(speed_at(end));
        int j;
        for (j = 0; j < nobj; j++) {
            if (objs[j].type == OB_COIN) continue;   /* coins are a freebie */
            if (objs[j].s >= from && objs[j].s <= end) {
                FAIL("obstacle type %d at s=%d is inside the corner window (%d..%d)",
                     objs[j].type, objs[j].s, from, end);
                break;
            }
        }
    }
}

static uint32_t map_hash(void)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < nobj && i < 400; i++) {
        h = (h ^ (uint32_t)(objs[i].s + 7 * (objs[i].lane + 1) + 31 * objs[i].type)) * 16777619u;
        h ^= h >> 15;
    }
    return h;
}

int main(void)
{
    enum { NSEED = 300 };
    static uint32_t hashes[NSEED];
    int seed, i, nseg_min = 1 << 30, nobj_max = 0, distinct = 0;
    long long obs_total = 0, obs_tail = 0, obs_end = 0;

    for (seed = 0; seed < NSEED; seed++) {
        char name[32];
        tr_seed((uint32_t)seed * 7919u + 12345u);
        P.t = 0;
        build_track();
        snprintf(name, sizeof name, "%d", seed);
        cur = name;

        if (nseg < 26) FAIL("only %d segments generated", nseg);
        if (nobj >= MAXOBJ) FAIL("object table full (%d)", nobj);
        if (nseg < nseg_min) nseg_min = nseg;
        if (nobj > nobj_max) nobj_max = nobj;
        check_map();

        /* the first 30k units are meant to be the gentle part, the tail the hard
           part: count obstacles per second to see the ramp actually happens */
        for (i = 0; i < nobj; i++) {
            if (objs[i].type == OB_COIN) continue;
            if (objs[i].s < 30000) obs_total++;
            else if (objs[i].s >= 60000 && objs[i].s < 90000) obs_tail++;
        }
        hashes[seed] = map_hash();

        /* push the generator out to the endgame and measure the top of the ramp */
        if (seed == 0) {
            while (nseg < MAXSEG && seg[nseg - 1].s0 + seg[nseg - 1].len < 400000) {
                add_segment();
                populate_segment(nseg - 1);
            }
            for (i = 0; i < nobj; i++) {
                if (objs[i].type == OB_COIN) continue;
                if (objs[i].s >= 300000 && objs[i].s < 330000) obs_end++;
            }
            cur = "0 (deep)";
            check_map();
        }
    }

    for (seed = 0; seed < NSEED; seed++) {
        int j, dup = 0;
        for (j = 0; j < seed; j++) if (hashes[j] == hashes[seed]) dup = 1;
        if (!dup) distinct++;
    }

    /* ---- 7: curves ---- */
    {
        int s, prev_spd = 0, prev_diff = -1;
        for (s = 0; s <= 2000000; s += 1000) {
            int spd = speed_at(s), d = diff_pct(s);
            if (spd < prev_spd) { printf("FAIL speed dips at s=%d\n", s); fails++; }
            if (d < prev_diff) { printf("FAIL difficulty dips at s=%d\n", s); fails++; }
            if (spd > SPEED_MAX) { printf("FAIL speed over cap at s=%d\n", s); fails++; }
            prev_spd = spd;
            prev_diff = d;
        }
        if (speed_at(0) != SPEED_MIN) { printf("FAIL speed(0) != %d\n", SPEED_MIN); fails++; }
    }

    printf("seeds            : %d\n", NSEED);
    printf("distinct maps    : %d / %d\n", distinct, NSEED);
    printf("segments built   : min %d\n", nseg_min);
    printf("objects per map  : max %d (table %d)\n", nobj_max, MAXOBJ);
    printf("obstacles /1000f : %.1f start, %.1f at 60-90k, %.1f at 300-330k\n",
           (double)obs_total * 1000.0 / (NSEED * frames_over(0, 30000)),
           (double)obs_tail * 1000.0 / (NSEED * frames_over(60000, 90000)),
           (double)obs_end * 1000.0 / frames_over(300000, 330000));
    printf("speed 0/100k/1M  : %d / %d / %d\n",
           speed_at(0), speed_at(100000), speed_at(1000000));

    if (distinct != NSEED) { printf("FAIL seed collisions\n"); fails++; }
    if (fails) {
        printf("\n%d PROBLEM(S)\n", fails);
        return 1;
    }
    printf("\nall map invariants hold\n");
    return 0;
}
