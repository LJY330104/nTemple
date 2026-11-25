/* The autopilot, shared by the screenshot harness and the soak test.
 * It is deliberately dumb -- it looks one obstacle ahead -- so that when it
 * survives a long run the map is genuinely fair, not "the bot was cheating". */
#ifndef BOT_H
#define BOT_H

/* a coin trail he is about to run through, with nothing big in the way */
static int coin_near(void)
{
    int s = (int)(P.s >> 16), i, n = 0;
    if (P.y > 40) return 0;
    for (i = 0; i < nobj; i++) {
        Obj *o = &objs[i];
        if (o->used || o->type != OB_JUMPWALL) continue;
        if (o->s - s > -60 && o->s - s < 900) return 0;   /* no wall in the shot */
    }
    for (i = 0; i < nobj; i++) {
        Obj *o = &objs[i];
        if (o->used || o->type != OB_COIN) continue;
        if (o->s - s < 240 || o->s - s > 900) continue;
        if (o->lane != P.lane_target) continue;
        n++;
    }
    return n >= 3;
}

/* jump early enough that the arc peaks at the wall */
static int jump_lead(void)
{
    int v = P.speed / 30;      /* units per frame */
    if (v < 8) v = 8;
    return v * 11;
}

/* is lane l blocked within +-260 units of s? */
static int lane_blocked_near(int lane, int s)
{
    int j;
    for (j = 0; j < nobj; j++) {
        if (objs[j].used || objs[j].type == OB_COIN) continue;
        if ((int)(objs[j].s - s) > -260 && (int)(objs[j].s - s) < 260 && objs[j].lane == lane)
            return 1;
    }
    return 0;
}

/* The game only honours a *press*, not a held key, so the bot has to let go
   between two presses -- otherwise a two-lane swerve silently does nothing. */
static int bot_cool;

/* nothing within `range` ahead in that lane? */
static int lane_clear(int lane, int s, int range)
{
    int j;
    for (j = 0; j < nobj; j++) {
        if (objs[j].used || objs[j].type == OB_COIN) continue;
        if (objs[j].lane != lane) continue;
        if (objs[j].s - s > -60 && objs[j].s - s < range) return 0;
    }
    return 1;
}

static int autopilot(void)
{
    int keys = 0;
    int s = (int)(P.s >> 16);
    int si = seg_index_for(s, P.cur_seg);
    int end = seg[si].s0 + seg[si].len;
    int i, best = -1, best_d = 1 << 30;

    if (bot_cool > 0) {      /* key deliberately released this frame */
        bot_cool--;
        return keys;
    }
    if (!P.turned && s >= end - turn_window() && s <= end) {
        keys |= (corner_dir(si) < 0) ? K_TURN_L : K_TURN_R;
        return keys;
    }

    /* the nearest thing that is actually in his way */
    for (i = 0; i < nobj; i++) {
        Obj *o = &objs[i];
        int d;
        if (o->used || o->type == OB_COIN) continue;
        d = o->s - s;
        if (d < 0 || d > jump_lead()) continue;
        if (o->type != OB_JUMPWALL && o->lane != P.lane_target) continue;
        if (d < best_d) { best_d = d; best = i; }
    }
    if (best < 0) return keys;

    {
        Obj *o = &objs[best];
        if (o->type == OB_LOW || o->type == OB_JUMPWALL) {
            if (P.y == 0) keys |= K_JUMP;   /* release in the air so the next press counts */
        } else {
            /* the middle first, then whichever side: that is also where the
               coin trails are, so the bot picks them up on the way past */
            static const int cand[3] = {0, -1, 1};
            int k, free_lane = -2;
            for (k = 0; k < 3 && free_lane == -2; k++)
                if (cand[k] != o->lane && !lane_blocked_near(cand[k], o->s))
                    free_lane = cand[k];
            if (free_lane == -2) free_lane = (o->lane == 0) ? 0 : -o->lane;
            if (free_lane != P.lane_target) {
                keys |= (free_lane < P.lane_target) ? K_LANE_L : K_LANE_R;
                bot_cool = 2;
            }
        }
    }
    return keys;
}

#endif
