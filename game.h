#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* input bit mask */
#define K_LANE_L 0x01
#define K_LANE_R 0x02
#define K_JUMP   0x04
#define K_TURN_L 0x10
#define K_TURN_R 0x20
#define K_START  0x40
#define K_QUIT   0x80

void tr_init(void);
void tr_seed(uint32_t entropy);
void tr_update(int keys);
void tr_render(void);
uint16_t *tr_framebuffer(void);
int tr_state(void);
int tr_score(void);

#endif
