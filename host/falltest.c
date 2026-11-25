/* Diagnostic: run into a corner without turning and film the fall. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "../game.c"

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;
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

/* jump early enough that the arc peaks at the wall */
static int jump_lead(void)
{
    int v = P.speed / 30;      /* units per frame */
    if (v < 8) v = 8;
    return v * 11;
}

/* survive obstacles, but never press 4 or 6 */
static int dodge(void)
{
    int keys = 0, i, s = (int)(P.s >> 16);
    for (i = 0; i < nobj; i++) {
        Obj *o = &objs[i];
        int d = o->s - s, full;
        if (o->used || o->type == OB_COIN) continue;
        if (d < 0 || d > jump_lead()) continue;
        full = (o->type == OB_JUMPWALL);
        if (!full && o->lane != P.lane_target) continue;
        if (o->type == OB_LOW || o->type == OB_JUMPWALL) {
            if (P.y == 0) keys |= K_JUMP;   /* release in the air so the next press counts */
        }
        else keys |= (o->lane != 0) ? (o->lane > 0 ? K_LANE_L : K_LANE_R) :
                     (((s / 100) & 1) ? K_LANE_L : K_LANE_R);
    }
    return keys;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "prev";
    char path[256];
    int frame, shot = 0, started = 0;

    tr_init();
    for (frame = 0; frame < 900; frame++) {
        int keys = 0;
        if (frame == 3) keys |= K_START;
        if (P.state == ST_PLAY) keys |= dodge();   /* dodges hazards, never turns */
        tr_update(keys);
        tr_render();
        if (P.state == ST_PLAY) started = 1;
        if (!started) continue;

        /* film the approach, then the whole fall */
        if (P.state == ST_PLAY) {
            int si = seg_index_for((int)(P.s >> 16), 0);
            int end = seg[si].s0 + seg[si].len;
            int d = end - (int)(P.s >> 16);
            if (shot == 0 && d < 900 && d > 780) {
                snprintf(path, sizeof path, "%s/fall-00.ppm", dir);
                dump_ppm(path); shot = 1;
            }
            if (shot == 1 && d < 300 && d > 180) {
                snprintf(path, sizeof path, "%s/fall-01.ppm", dir);
                dump_ppm(path); shot = 2;
            }
        } else if (P.state == ST_FALL && shot >= 2 && shot < 2 + 6 &&
                   P.fall_t == 2 + (shot - 2) * 5) {
            snprintf(path, sizeof path, "%s/fall-%02d.ppm", dir, shot);
            dump_ppm(path);
            shot++;
        }
        if (shot >= 8) break;
    }
    fprintf(stderr, "state=%d fell=%d shots=%d\n", P.state, P.fell, shot);
    return 0;
}
