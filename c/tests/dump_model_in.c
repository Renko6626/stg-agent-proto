/* dump_model_in.c —— 给 Python 侧的对拍夹具：同一批世界，一边录成 .stglog，一边把
 * `sa_model_fill` 填出来的数组原样倒出来。
 *
 *     dump_model_in <out.stglog> <out.bin>
 *
 * Python 侧（tests/test_model_parity.py）读 .stglog、用 tools/check_model_parity.py 的
 * `fill_inputs` 重算，再与 .bin 逐元素比。**世界只在这里定义一次**，所以两边比的是同一个东西；
 * 这是「C 侧的填数组」与「Python 侧的填数组」唯一的真对拍 —— 两边各写一套单测是测不出
 * 口径分叉的（列序写反、包络边界差一、speed 取了 focus 档，都能各自自洽）。
 *
 * .bin 的格式：每帧顺序写下面几块，不写结构体本身（免得受 padding 影响）——
 *   float target[2] · int64 prev_action · int32 nb · int32 ne
 *   float bullets[640*5] · uint8 bullets_mask[640]
 *   float enemies[256*4] · uint8 enemies_mask[256] · float player[5] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sa_model.h"
#include "../stgagent.h"

static ap_world_t    W;
static sa_model_in_t IN;

static void base_world(uint32_t frame)
{
    memset(&W, 0, sizeof W);
    W.frame = frame;
    W.phase = AP_PHASE_IN_GAME | AP_PHASE_PLAYER_CONTROLLABLE;
    W.area.xmin = -184.0f; W.area.xmax = 184.0f;
    W.area.ymin = 16.0f;   W.area.ymax = 432.0f;
    W.player.x = 0.0f; W.player.y = 384.0f;
    W.player.hit_radius = 1.25f;
    W.player.speed = 4.0f; W.player.speed_focus = 2.0f;   /* 两档不同 → 取错档就露馅 */
    W.player.lives = 3;
    W.player.state = 0;
}

static void put_bullet(int i, float x, float y, float vx, float vy, float r, int collidable)
{
    W.bullets[i].x = x; W.bullets[i].y = y;
    W.bullets[i].vx = vx; W.bullets[i].vy = vy;
    W.bullets[i].radius = r;
    W.bullets[i].collidable = (uint8_t)collidable;
    W.bullets[i].state = collidable ? 1 : 2;
    if (i + 1 > W.nbullets) W.nbullets = i + 1;
}

static void put_enemy(int i, float x, float y, float hw, float hh, int boss, int collidable)
{
    W.enemies[i].x = x; W.enemies[i].y = y;
    W.enemies[i].hit_w = hw; W.enemies[i].hit_h = hh;   /* hw != hh → 取错列就露馅 */
    W.enemies[i].boss = (uint8_t)boss;
    W.enemies[i].collidable = (uint8_t)collidable;
    W.enemies[i].hp = 100; W.enemies[i].hp_max = 200;
    W.enemies[i].id = (uint32_t)(i + 1);
    if (i + 1 > W.nenemies) W.nenemies = i + 1;
}

/* 第 `k` 个场景。返回该帧用的锚点与上一步动作。 */
static void scenario(int k, float *ax, float *ay, int *prev)
{
    int i;
    base_world((uint32_t)k);
    *ax = 0.0f; *ay = 384.0f; *prev = 0;
    switch (k) {
    case 0:                                  /* 空场 */
        break;
    case 1:                                  /* 混合 collidable：偶数留、奇数剔 */
        for (i = 0; i < 8; i++)
            put_bullet(i, (float)(i * 17 - 60), 200.0f + (float)i * 9.0f,
                       0.5f * (float)i, 2.0f, 3.0f + 0.25f * (float)i, (i % 2) == 0);
        *prev = 5;
        break;
    case 2:                                  /* 包络八个边界样本：边界上留、越过一点剔 */
        put_bullet(0,  256.0f, 100.0f, 0, 0, 2.0f, 1);
        put_bullet(1,  256.5f, 100.0f, 0, 0, 2.0f, 1);
        put_bullet(2, -256.0f, 100.0f, 0, 0, 2.0f, 1);
        put_bullet(3, -256.5f, 100.0f, 0, 0, 2.0f, 1);
        put_bullet(4,    0.0f, -64.0f, 0, 0, 2.0f, 1);
        put_bullet(5,    0.0f, -64.5f, 0, 0, 2.0f, 1);
        put_bullet(6,    0.0f, 512.0f, 0, 0, 2.0f, 1);
        put_bullet(7,    0.0f, 512.5f, 0, 0, 2.0f, 1);
        *prev = 17;
        break;
    case 3:                                  /* 敌人：boss / 非 boss / 不参与碰撞，hit_w != hit_h */
        put_enemy(0,  20.0f,  80.0f, 16.0f, 24.0f, 1, 1);
        put_enemy(1, -50.0f,  40.0f, 12.0f,  8.0f, 0, 0);
        put_enemy(2,  99.0f,  60.0f,  8.0f, 32.0f, 0, 1);
        put_bullet(0, 10.0f, 340.0f, 0.0f, 2.5f, 4.0f, 1);
        *ax = 20.0f;                         /* boss 正下方 */
        *prev = 9;
        break;
    case 4:                                  /* focus 开 + 自机不在中央 + 锚点偏斜 */
        W.player.focus = 1;
        W.player.x = -77.0f; W.player.y = 299.0f;
        put_bullet(0, -70.0f, 250.0f, 1.0f, 3.0f, 5.5f, 1);
        put_enemy(0, -120.0f, 120.0f, 24.0f, 24.0f, 1, 1);
        *ax = -120.0f; *ay = 400.0f;
        *prev = 12;
        break;
    case 5:                                  /* 满池：640 颗全 collidable，行数刚好装满 */
        for (i = 0; i < AP_MAX_BULLETS; i++)
            put_bullet(i, (float)((i % 96) * 4 - 190), (float)((i / 96) * 40 + 20),
                       0.0f, 1.5f, 2.0f, 1);
        break;
    default:
        break;
    }
}

#define NSCEN 6

static void wr(FILE *f, const void *p, size_t n)
{
    if (fwrite(p, 1, n, f) != n) { perror("fwrite"); exit(1); }
}

int main(int argc, char **argv)
{
    sa_desc_t d;
    FILE *bin;
    int k;
    float ax0, ay0;
    int prev0;

    if (argc != 3) { fprintf(stderr, "用法：dump_model_in <out.stglog> <out.bin>\n"); return 2; }

    scenario(0, &ax0, &ay0, &prev0);   /* 先跑一个场景把 area 填好，HELLO 要用它 */
    d.backend = "c-dump";
    d.policy = "onnx:dump";
    d.obs_timing = "prev-frame-final";
    d.tick_hz = 60;
    d.action_bits = AP_BTN_UP | AP_BTN_DOWN | AP_BTN_LEFT | AP_BTN_RIGHT
                  | AP_BTN_SHOT | AP_BTN_BOMB | AP_BTN_SLOW;
    d.area = W.area;
    if (!sa_log_open(argv[1], &d)) { fprintf(stderr, "sa_log_open 失败\n"); return 1; }

    bin = fopen(argv[2], "wb");
    if (!bin) { perror("fopen"); return 1; }

    for (k = 0; k < NSCEN; k++) {
        float ax, ay;
        int prev, id;
        sa_model_fill_t st;
        int64_t prev64;
        int32_t nb, ne;

        scenario(k, &ax, &ay, &prev);
        st = sa_model_fill(&W, ax, ay, prev, &IN);
        /* ACT 里写「假设模型选了 prev」的按钮位：Python 侧不比这个，只用来让日志成形。 */
        id = prev;
        sa_log_frame(&W, sa_model_buttons(id), 0);

        prev64 = IN.prev_action[0];
        nb = (int32_t)st.bullets;
        ne = (int32_t)st.enemies;
        wr(bin, IN.target, sizeof IN.target);
        wr(bin, &prev64, sizeof prev64);
        wr(bin, &nb, sizeof nb);
        wr(bin, &ne, sizeof ne);
        wr(bin, IN.bullets, sizeof IN.bullets);
        wr(bin, IN.bullets_mask, sizeof IN.bullets_mask);
        wr(bin, IN.enemies, sizeof IN.enemies);
        wr(bin, IN.enemies_mask, sizeof IN.enemies_mask);
        wr(bin, IN.player, sizeof IN.player);
    }

    fclose(bin);
    sa_log_close();
    printf("dump_model_in: %d 个场景 → %s + %s\n", NSCEN, argv[1], argv[2]);
    return 0;
}
