/* TI-Nspire entry point for nTemple */
#include <os.h>
#include "game.h"

static int read_keys(void)
{
    int k = 0;
    if (isKeyPressed(KEY_NSPIRE_7) || isKeyPressed(KEY_NSPIRE_LEFT)) k |= K_LANE_L;
    if (isKeyPressed(KEY_NSPIRE_9) || isKeyPressed(KEY_NSPIRE_RIGHT)) k |= K_LANE_R;
    if (isKeyPressed(KEY_NSPIRE_8) || isKeyPressed(KEY_NSPIRE_UP)) k |= K_JUMP;
    if (isKeyPressed(KEY_NSPIRE_5) || isKeyPressed(KEY_NSPIRE_ENTER)) k |= K_START;
    if (isKeyPressed(KEY_NSPIRE_4)) k |= K_TURN_L;
    if (isKeyPressed(KEY_NSPIRE_6)) k |= K_TURN_R;
    if (isKeyPressed(KEY_NSPIRE_CLICK)) k |= K_START;
    if (isKeyPressed(KEY_NSPIRE_ESC)) k |= K_QUIT;
    return k;
}

/* A fresh map every run needs a seed that is actually different every run.
   Two cheap sources of noise:
     - the file clock (one second resolution, so the second you launched in),
     - whatever pixels the OS left on the panel before we took it over. */
static uint32_t entropy(void)
{
    const uint16_t *fb = tr_framebuffer();
    struct nuc_stat st;
    uint32_t h = 2166136261u;
    int i;

    for (i = 0; i < 4096; i++) {
        h ^= fb[(uint32_t)i * 7919u % (320u * 240u)];
        h *= 16777619u;
    }
    if (nuc_stat("ntemple.hi", &st) != 0 && nuc_stat("ntemple.tns", &st) != 0)
        st.st_mtime = 0;
    h = (h ^ st.st_mtime) * 16777619u;
    return h;
}

int main(void)
{
    if (!lcd_init(SCR_320x240_565)) return 1;
    tr_seed(entropy());
    tr_init();
    for (;;) {
        int keys = read_keys();
        if (keys & K_QUIT) break;
        tr_update(keys);
        tr_render();
        lcd_blit(tr_framebuffer(), SCR_320x240_565);
        msleep(33);
    }
    lcd_init(SCR_320x240_16);
    return 0;
}
