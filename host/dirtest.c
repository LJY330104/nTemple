/* Invariant check: for every corner, the way the road actually goes on screen
   must agree with the key that corner_dir() demands (4 = left, 6 = right). */
#include <stdio.h>
#include <stdint.h>

#include "../game.c"

/* screen x of a world point seen from a camera at the corner, facing dir d0 */
static int screen_x(int d0, int cx, int cz, int pt_x, int pt_z)
{
    int yaw = d0 * 16384;
    int cs = icos(yaw), sn = isin(yaw);

    V3 v;
    cam_x = cx; cam_z = cz; cam_y = CAM_HEIGHT;
    cam_cos = cs; cam_sin = sn;
    pitch_cur = PITCH_Q16;
    pit_cos = icos(PITCH_Q16);
    pit_sin = isin(PITCH_Q16);
    v = to_cam(pt_x, 0, pt_z);
    if (v.z < NEARZ) return 0x7fffffff;
    return SW / 2 + (int)((long long)v.x * FOCAL / v.z);
}

int main(void)
{
    int i, bad = 0, checked = 0;
    tr_init();
    build_track();
    for (i = 0; i + 1 < nseg; i++) {
        int d0 = seg[i].dir, d1 = seg[i + 1].dir;
        int qx = seg[i].x + DIRX[d0] * seg[i].len;
        int qz = seg[i].z + DIRZ[d0] * seg[i].len;
        int ax = qx - DIRX[d0] * 600, az = qz - DIRZ[d0] * 600;
        int bx = qx + DIRX[d1] * 600, bz = qz + DIRZ[d1] * 600;
        int tx = qx + DIRX[d0] * 600, tz = qz + DIRZ[d0] * 600;
        int sx_a = screen_x(d0, ax, az, tx, tz);   /* straight on */
        int sx_b = screen_x(d0, ax, az, bx, bz);   /* where the track goes */
        int on_screen_right = (sx_b > sx_a);
        int want_right = (corner_dir(i) > 0);
        checked++;
        if (on_screen_right != want_right) {
            bad++;
            printf("MISMATCH seg %d: dir %d -> %d, road goes %s, key asks %s"
                   " (sx %d -> %d)\n", i, d0, d1,
                   on_screen_right ? "RIGHT" : "LEFT",
                   want_right ? "6/RIGHT" : "4/LEFT", sx_a, sx_b);
        }
    }
    printf("%d corners checked, %d mismatched\n", checked, bad);
    return bad ? 1 : 0;
}
