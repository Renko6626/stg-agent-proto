#include <stdio.h>
#include <string.h>
#include "../stgagent.h"

int main(int argc, char **argv)
{
    static ap_world_t w;
    sa_desc_t d = { "c-sample", "builtin", "prev-frame-final", 60, 0x7f, { -184, 184, 16, 432 } };
    if (argc < 2) { fprintf(stderr, "usage: write_sample_log <out.stglog>\n"); return 2; }
    if (!sa_log_open(argv[1], &d)) return 3;
    for (uint32_t f = 0; f < 3; f++) {
        memset(&w, 0, sizeof w);
        w.frame = f; w.phase = AP_PHASE_IN_GAME | AP_PHASE_PLAYER_CONTROLLABLE;
        w.player.x = 0.0f; w.player.y = 400.0f; w.player.lives = 3; w.player.power = 100;
        w.area.xmin = -184; w.area.xmax = 184; w.area.ymin = 16; w.area.ymax = 432;
        w.nbullets = 1; w.bullets[0].x = 10.0f; w.bullets[0].y = 20.0f + (float)f; w.bullets[0].vy = 1.0f;
        w.bullets[0].radius = 4.0f; w.bullets[0].state = 1; w.bullets[0].collidable = 1;
        sa_log_frame(&w, AP_BTN_SHOT, f == 1 ? SA_ACT_HUMAN : 0);
    }
    sa_log_close();
    return 0;
}
