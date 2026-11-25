/* Diagnostic: approach a forced left / right corner at several distances. */
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

#define CORNER_S 1600

static void setup(int turndir)
{
    int i;
    build_track();
    reset_player();
    P.state = ST_PLAY;
    for (i = 0; i < nobj; i++) objs[i].s = -100000;
    seg[0].x = 0; seg[0].z = 0; seg[0].s0 = 0; seg[0].len = CORNER_S;
    seg[0].dir = 0; seg[0].turn = turndir;
    seg[1].x = 0; seg[1].z = CORNER_S; seg[1].s0 = CORNER_S; seg[1].len = 2400;
    seg[1].dir = (turndir > 0 ? 3 : 1) & 3; seg[1].turn = 1;
    nseg = 2;
    P.cur_seg = 0;
    P.turned = 0;
    P.t = 0;
    P.lane_target = 0;
    P.lat = 0;
    P.speed = 1500;        /* so the prompt is on for every distance shown */
    cam_yaw_q16 = 0;
    cam_cos = icos(0);
    cam_sin = isin(0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "prev";
    char path[256];
    int k, f;
    static const int dist[4] = { 1300, 800, 400, 150 };

    tr_init();
    for (k = 0; k < 2; k++) {
        int turndir = (k == 0) ? 1 : -1;
        for (f = 0; f < 4; f++) {
            setup(turndir);
            P.s = (int64_t)(CORNER_S - dist[f]) << 16;
            tr_render();
            snprintf(path, sizeof path, "%s/turn-%s-d%d.ppm", dir, turndir > 0 ? "L" : "R", dist[f]);
            dump_ppm(path);
        }
    }
    return 0;
}
