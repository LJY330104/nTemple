/* Host preview: builds the exact same game.c on macOS and dumps frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../game.c"

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", SW, SH);
    for (i = 0; i < SW * SH; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(((c >> 11) & 0x1F) * 255 / 31);
        rgb[1] = (unsigned char)(((c >> 5) & 0x3F) * 255 / 63);
        rgb[2] = (unsigned char)((c & 0x1F) * 255 / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

#include "bot.h"

/* airborne, and the thing he is clearing is actually a low wall or a gate */
static int jumping_over(void)
{
    int s = (int)(P.s >> 16), i;
    if ((P.y >> 16) < 140) return 0;
    for (i = 0; i < nobj; i++) {
        Obj *o = &objs[i];
        int d = o->s - s;
        if (o->used) continue;
        if (o->type != OB_LOW && o->type != OB_JUMPWALL) continue;
        if (d > 40 && d < 460) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int frame, total = 1600;
    int got_turn = 0, got_coin = 0, got_jump = 0, got_over = 0, got_title = 0, got_approach = 0;
    int got_gate = 0, got_fall = 0, seq = 0;
    const char *outdir = "prev";
    char path[256];

    if (argc > 1) outdir = argv[1];
    if (argc > 2) total = atoi(argv[2]);

    tr_init();
    for (frame = 0; frame < total; frame++) {
        int keys = 0;
        if (frame == 30) keys |= K_START;   /* key 5: start */
        if (frame == 60) keys |= K_JUMP;
        if (P.state == ST_PLAY) keys |= autopilot();
        if (frame >= total - 220) keys &= ~(K_TURN_L | K_TURN_R);
        /* at a fixed frame, miss the next corner on purpose so the film also
           covers the fall + the "FELL OFF!" panel */
        if (frame >= 430 && P.state == ST_PLAY && P.y == 0) {
            int si = seg_index_for((int)(P.s >> 16), 0);
            P.cur_seg = si;
            P.turned = 0;
            P.s = (int64_t)(seg[si].s0 + seg[si].len + 20) << 16;
        }
        tr_update(keys);
        tr_render();

        if (!got_title && P.state == ST_TITLE && frame == 20) {
            snprintf(path, sizeof path, "%s/01-title.ppm", outdir);
            dump_ppm(path);
            got_title = 1;
        }
        if (!got_coin && P.state == ST_PLAY && coin_near()) {
            snprintf(path, sizeof path, "%s/02-coins.ppm", outdir);
            dump_ppm(path);
            got_coin = 1;
        }
        if (!got_jump && P.state == ST_PLAY && jumping_over()) {
            snprintf(path, sizeof path, "%s/03-jump.ppm", outdir);
            dump_ppm(path);
            got_jump = 1;
        }
        if (!got_approach && P.state == ST_PLAY) {
            int si = seg_index_for((int)(P.s >> 16), 0);
            int end = seg[si].s0 + seg[si].len;
            int dist = end - (int)(P.s >> 16);
            if (!P.turned && dist > 300 && dist < 900 && ((P.t >> 2) & 1) == 0) {
                snprintf(path, sizeof path, "%s/06-approach.ppm", outdir);
                dump_ppm(path);
                got_approach = 1;
            }
        }
        /* a film strip of the last stretch into a corner */
        if (seq < 8 && P.state == ST_PLAY) {
            int si2 = seg_index_for((int)(P.s >> 16), 0);
            int end2 = seg[si2].s0 + seg[si2].len;
            int dist = end2 - (int)(P.s >> 16);
            if (seq == 0 && dist < 620) seq = 1;
            if (seq > 0 && (seq - 1) % 1 == 0 && P.t % 8 == 0) {
                snprintf(path, sizeof path, "%s/seq-%02d.ppm", outdir, seq - 1);
                dump_ppm(path);
                seq++;
            }
        }
        if (!got_turn && P.state == ST_PLAY && P.flash > 8) {
            snprintf(path, sizeof path, "%s/04-turn.ppm", outdir);
            dump_ppm(path);
            got_turn = 1;
        }
        if (got_gate != 3 && P.state == ST_PLAY) {
            int j;
            for (j = 0; j < nobj; j++) {
                int ty = objs[j].type, bit;
                int d = objs[j].s - (int)(P.s >> 16);
                if (objs[j].used || ty != OB_JUMPWALL) continue;
                if (d > 560 || d < 300) continue;
                bit = 1;
                if (got_gate & bit) continue;
                snprintf(path, sizeof path, "%s/07-gate-%d.ppm", outdir, bit);
                dump_ppm(path);
                got_gate |= bit;
                break;
            }
        }
        if (P.state == ST_FALL) {
            int bx0 = 9999, bx1 = -1, by0 = 9999, by1 = -1, xx, yy, nn = 0;
            for (yy = 0; yy < SH; yy++) for (xx = 0; xx < SW; xx++)
                if (fb[yy * SW + xx] == C(72, 116, 200)) {
                    nn++;
                    if (xx < bx0) bx0 = xx; if (xx > bx1) bx1 = xx;
                    if (yy < by0) by0 = yy; if (yy > by1) by1 = yy;
                }
            fprintf(stderr, "FALL t=%d fy=%d cam=(%d,%d,%d) pitch=%d px=%d bbox=(%d,%d)-(%d,%d)\n",
                    P.fall_t, (int)(P.fy >> 16), cam_x, cam_y, cam_z, pitch_cur, nn, bx0, by0, bx1, by1);
        }
        if (!got_fall && P.state == ST_FALL && P.fall_t == 2) {
            snprintf(path, sizeof path, "%s/08-fall.ppm", outdir);
            dump_ppm(path);
            got_fall = 1;
        }
        if (!got_over && P.state == ST_OVER) {
            snprintf(path, sizeof path, "%s/05-over.ppm", outdir);
            dump_ppm(path);
            got_over = 1;
        }
    }
    fprintf(stderr, "state=%d score=%d coins=%d best=%d\n", P.state, P.score, P.coins, P.best);
    return 0;
}
