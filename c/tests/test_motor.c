/* test_motor.c —— 手部运动层（sa_motor.c）。场景与训练仓 tests/test_motor.py **一一对应**：
 * 两边各写各的实现，押同一组期望序列，语义分叉就会有一边红。
 *
 * 判别力：随机量要押范围**且**押散布 —— 只押范围的话，退化成恒取下界的实现照样绿，
 * 而「确定性最短保持」正是设计明确否掉的方案。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sa_motor.h"

typedef struct { sa_motor_t m; int prev, held, slow_held; } hand_t;

static void hand(hand_t *h, int hl, int hh, int dl, int dh, uint32_t seed)
{
    sa_motor_init(&h->m, hl, hh, dl, dh, seed);
    h->prev = 0; h->held = h->slow_held = SA_MOTOR_HELD_NEVER;
}

static int step(hand_t *h, int want)
{
    int out = sa_motor_apply(&h->m, want, h->prev, h->held, h->slow_held);
    h->held = sa_motor_next_held(h->prev, out, h->held);
    h->slow_held = sa_motor_next_slow_held(h->prev, out, h->slow_held);
    h->prev = out;
    return out;
}

/* 依次想按这些**方向**（低速关），核对实际执行的方向序列 */
static void expect_dirs(hand_t *h, const int *wants, const int *expect, int n, const char *what)
{
    for (int i = 0; i < n; i++) {
        int got = step(h, wants[i] * 2) / 2;
        if (got != expect[i]) { printf("FAIL %s：第 %d 帧想 %d，执行 %d，应为 %d\n", what, i, wants[i], got, expect[i]); assert(0); }
    }
}
#define SEQ(h, W, E, what) expect_dirs(h, W, E, (int)(sizeof W / sizeof W[0]), what)

int main(void)
{
    hand_t h;

    { int w[] = {3}, e[] = {3};
      hand(&h, 5, 5, 0, 0, 1); SEQ(&h, w, e, "新局第一下不受限"); }

    { int w[] = {3, 7, 7, 7, 7}, e[] = {3, 3, 3, 7, 7};
      hand(&h, 3, 3, 0, 0, 1); SEQ(&h, w, e, "hold=3：执行满 3 帧才放行"); }

    { int w[] = {3, 0, 0, 0, 0}, e[] = {3, 3, 3, 3, 0};
      hand(&h, 4, 4, 0, 0, 1); SEQ(&h, w, e, "松开也是一次变向：1 帧点按做不出来"); }

    { int w[] = {3, 7, 3, 3, 3, 3}, e[] = {3, 3, 3, 3, 3, 3};
      hand(&h, 3, 3, 0, 0, 1); SEQ(&h, w, e, "中途想换又反悔：没有迟到的变向"); }

    { int w[] = {3, 3, 3, 3}, e[] = {0, 0, 3, 3};
      hand(&h, 0, 0, 2, 2, 1); SEQ(&h, w, e, "delay=2"); }

    { int w[] = {3, 7, 7, 7, 7}, e[] = {0, 0, 0, 7, 7};
      hand(&h, 0, 0, 2, 2, 1); SEQ(&h, w, e, "改主意重抽延迟"); }

    { int w[] = {3, 0, 3, 3, 3}, e[] = {0, 0, 0, 0, 3};
      hand(&h, 0, 0, 2, 2, 1); SEQ(&h, w, e, "撤销请求后要重新等"); }

    { int w[] = {3, 3, 7, 7, 7, 7, 7}, e[] = {0, 3, 3, 3, 3, 7, 7};
      hand(&h, 4, 4, 1, 1, 1); SEQ(&h, w, e, "延迟与最短保持要同时满足"); }

    /* 低速位直通：方向被锁在 3，低速照策略给的走 */
    hand(&h, 5, 5, 0, 0, 1);
    assert(step(&h, 6) == 6 && step(&h, 7) == 7 && step(&h, 6) == 6 && step(&h, 15) == 7);

    /* reset：清锁与待生效请求；调用方同时把 prev / held 清成新局值 */
    hand(&h, 9, 9, 0, 0, 1);
    step(&h, 6);
    assert(step(&h, 14) / 2 == 3);                 /* 被锁着 */
    sa_motor_reset(&h.m); h.prev = 0; h.held = h.slow_held = SA_MOTOR_HELD_NEVER;
    assert(step(&h, 14) / 2 == 7);

    /* 段长随机：落在 [2, 6]、两端都取得到、五个取值都有相当的份额 */
    {
        int hist[8] = {0}, n = 4000;
        hand_t r; hand(&r, 2, 6, 0, 0, 12345);
        for (int k = 0; k < n; k++) {
            int len = 0;
            sa_motor_reset(&r.m); r.prev = 0; r.held = r.slow_held = SA_MOTOR_HELD_NEVER;
            step(&r, 6);                           /* 换到方向 3，抽一个 L */
            for (int t = 1; t < 10; t++) { if (step(&r, 14) / 2 == 7) { len = t; break; } }
            assert(len >= 2 && len <= 6);
            hist[len]++;
        }
        for (int k = 2; k <= 6; k++) assert(hist[k] > n / 5 * 7 / 10 && hist[k] < n / 5 * 13 / 10);
    }

    /* 同种子同序列 */
    {
        hand_t a, b; hand(&a, 2, 6, 0, 2, 77); hand(&b, 2, 6, 0, 2, 77);
        uint32_t x = 1;
        for (int k = 0; k < 2000; k++) { x = x * 1664525u + 1013904223u; int w = (int)((x >> 8) % 18); assert(step(&a, w) == step(&b, w)); }
    }

    /* 端到端不变量：乱按 2 万帧，按出去的每一段方向都不短于 hold 下界 */
    {
        hand_t r; hand(&r, 2, 6, 0, 2, 9);
        uint32_t x = 7; int run = 99, prev_dir = 0, shortest = 99;
        for (int k = 0; k < 20000; k++) {
            x = x * 1664525u + 1013904223u;
            int d = step(&r, (int)((x >> 8) % 18)) / 2;
            if (d != prev_dir) { if (run < shortest) shortest = run; run = 1; } else run++;
            prev_dir = d;
        }
        assert(shortest >= 2);
    }

    /* 计数器式随机数：与训练仓 tests/test_motor.py 共用的黄金值（seed 12345、env 0、t = 0..11）。
     * 两边各写各的实现押同一组数 —— 这一条红了，就是 C 与 Python 的随机流不再逐位同源。 */
    {
        static const int HOLD[12]  = {2, 3, 4, 6, 4, 2, 6, 5, 6, 6, 4, 6};
        static const int DELAY[12] = {2, 1, 2, 2, 0, 0, 0, 2, 2, 1, 2, 0};
        for (uint32_t t = 0; t < 12; t++) {
            assert(sa_motor_draw(12345, 0, t, SA_MOTOR_STREAM_HOLD, 2, 6) == HOLD[t]);
            assert(sa_motor_draw(12345, 0, t, SA_MOTOR_STREAM_DELAY, 0, 2) == DELAY[t]);
        }
        assert(sa_motor_draw(1, 0, 7, 0, 5, 5) == 5);
        /* 低速位那两路（stream 2 / 3） */
        static const int SHOLD[12]  = {6, 5, 6, 5, 5, 5, 3, 2, 4, 5, 4, 5};
        static const int SDELAY[12] = {1, 1, 2, 1, 2, 0, 1, 0, 2, 0, 1, 0};
        for (uint32_t t = 0; t < 12; t++) {
            assert(sa_motor_draw(12345, 0, t, SA_MOTOR_STREAM_SLOW_HOLD, 2, 6) == SHOLD[t]);
            assert(sa_motor_draw(12345, 0, t, SA_MOTOR_STREAM_SLOW_DELAY, 0, 2) == SDELAY[t]);
        }
    }

    /* ---- N3：低速位也过运动层（场景与训练仓 tests/test_motor.py 的 N3 一节一一对应）---- */
    hand(&h, 3, 3, 0, 0, 1); sa_motor_set_slow(&h.m, 1);
    /* 方向一直是 3；低速：按下（第一下放行）→ 想松被锁两帧 → 第 4 帧放行 */
    assert(step(&h, 7) == 7 && step(&h, 6) == 7 && step(&h, 6) == 7 && step(&h, 6) == 6 && step(&h, 6) == 6);

    /* 两路互不牵连：方向刚换被锁着，不妨碍低速键立刻按下；反过来也一样 */
    hand(&h, 4, 4, 0, 0, 1); sa_motor_set_slow(&h.m, 1);
    assert(step(&h, 3 * 2 + 1) == 7 && step(&h, 7 * 2 + 0) == 7);      /* 两路都想换，两路都被锁 */
    hand(&h, 4, 4, 0, 0, 1); sa_motor_set_slow(&h.m, 1);
    step(&h, 3 * 2);
    assert(step(&h, 3 * 2 + 1) == 7);                                  /* 低速位没动过 → 它那一路仍是第一下 */
    assert(step(&h, 7 * 2 + 1) == 7);                                  /* 方向还在自己的锁里 */

    /* 低速位的延迟与撤回 */
    hand(&h, 0, 0, 2, 2, 1); sa_motor_set_slow(&h.m, 1);
    assert(step(&h, 1) % 2 == 0 && step(&h, 1) % 2 == 0 && step(&h, 1) % 2 == 1 && step(&h, 1) % 2 == 1);
    hand(&h, 0, 0, 2, 2, 1); sa_motor_set_slow(&h.m, 1);
    assert(step(&h, 1) % 2 == 0 && step(&h, 0) % 2 == 0 && step(&h, 1) % 2 == 0 && step(&h, 1) % 2 == 0 && step(&h, 1) % 2 == 1);

    /* 开不开低速那一路，方向这一路逐位不变；开了之后乱按的低速位大部分时候被挡、且没有短于 2 帧的低速段 */
    {
        hand_t a, b; hand(&a, 2, 6, 0, 2, 5); hand(&b, 2, 6, 0, 2, 5); sa_motor_set_slow(&b.m, 1);
        uint32_t x = 3; int blocked = 0, run = 99, prev_slow = 0, shortest = 99;
        for (int k = 0; k < 20000; k++) {
            x = x * 1664525u + 1013904223u;
            int w = (int)((x >> 8) % 18), oa = step(&a, w), ob = step(&b, w);
            assert(oa / 2 == ob / 2);
            assert(oa % 2 == w % 2);
            blocked += (ob % 2) != (w % 2);
            if (ob % 2 != prev_slow) { if (run < shortest) shortest = run; run = 1; } else run++;
            prev_slow = ob % 2;
        }
        assert(blocked > 5000 && shortest >= 2);
    }

    /* 非法参数与非法 id */
    hand(&h, 5, 2, -1, 3, 0);                      /* 区间非法 → 该项不起作用 */
    assert(h.m.hold_lo == 0 && h.m.hold_hi == 0 && h.m.delay_lo == 0 && h.m.delay_hi == 0);
    assert(step(&h, 999) == 0 && step(&h, -3) == 0);
    assert(sa_motor_next_held(6, 6, SA_MOTOR_HELD_NEVER) == SA_MOTOR_HELD_NEVER);
    assert(sa_motor_next_held(6, 7, 5) == 6 && "只换低速位不算换方向");
    assert(sa_motor_next_held(6, 14, 5) == 1);

    printf("test_motor ok\n");
    return 0;
}
