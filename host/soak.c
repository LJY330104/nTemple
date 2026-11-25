/* Soak test: let the dumb autopilot play many freshly seeded maps until it
 * dies, and report how far it got and how it died.
 *
 * This is the counterweight to maptest.  maptest proves the *local* rules are
 * fair (a free lane always exists, there is always time to react); this proves
 * the rules add up to a run a human can survive at least as long as the bot.
 * If the bot starts dying at 8 seconds, the ramp is a cliff. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../game.c"
#include "bot.h"

static int run_seed(uint32_t seed, int max_frames, int *frames_out, int *score_out)
{
    int frame;
    tr_seed(seed);
    tr_init();
    P.state = ST_TITLE;

    for (frame = 0; frame < max_frames; frame++) {
        int keys = 0;
        if (frame == 2) keys |= K_START;
        else if (P.state == ST_PLAY) keys |= autopilot();
        tr_update(keys);
        if (P.state == ST_OVER) break;
    }
    *frames_out = frame;
    *score_out = P.score;
    return P.state == ST_OVER ? (P.fell ? 1 : 2) : 0;
}

int main(int argc, char **argv)
{
    enum { NSEED = 60 };
    int MAXFRAMES = 9000;      /* 5 minutes at 30 fps */
    int i, worst = 1 << 30, best = 0, sum = 0, crashes = 0, falls = 0, survived = 0;
    int worst_seed = 0;

    if (argc > 1) MAXFRAMES = atoi(argv[1]);

    for (i = 0; i < NSEED; i++) {
        int f, sc, how = run_seed((uint32_t)i * 2654435761u + 7u, MAXFRAMES, &f, &sc);
        sum += f;
        if (f < worst) { worst = f; worst_seed = i; }
        if (f > best) best = f;
        if (how == 1) falls++;
        else if (how == 2) crashes++;
        else survived++;
        if (f < 500) printf("  seed %2d died after %d frames (score %d, %s)\n",
                            i, f, sc, how == 1 ? "fell off" : how == 2 ? "hit something" : "?");
    }

    printf("runs             : %d (%d frames max each)\n", NSEED, MAXFRAMES);
    printf("frames survived  : avg %d, worst %d (seed %d), best %d\n",
           sum / NSEED, worst, worst_seed, best);
    printf("ended            : %d crashed, %d fell off, %d still going\n",
           crashes, falls, survived);
    printf("reached %d s without dying in %d/%d runs\n",
           MAXFRAMES / 30, survived, NSEED);

    /* the bot should at least get past the first half minute on every map */
    if (worst < 900) {
        printf("\nFAIL: bot died before 30 s on seed %d\n", worst_seed);
        return 1;
    }
    printf("\nall maps survivable past 30 s by the autopilot\n");
    return 0;
}
