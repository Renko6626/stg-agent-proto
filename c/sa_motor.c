/* sa_motor.c —— 见 sa_motor.h。纯整数运算，无堆分配、无平台依赖。 */
#include "sa_motor.h"

#define SA_MOTOR_ACTIONS 18

/* lowbias32（Chris Wellons）。uint32 回绕 = 训练侧 int64 里「乘完掩回 32 位」，逐位相同。 */
static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* U{lo..hi}。区间宽度 ≤ 120，取模偏差可忽略（< 3e-8）。 */
int sa_motor_draw(uint32_t seed, uint32_t env, uint32_t t, uint32_t stream, int lo, int hi)
{
    uint32_t x;
    if (hi <= lo) return lo;
    x = seed + env * 0x9E3779B1u + t * 0x85EBCA6Bu + stream * 0xC2B2AE35u;
    return lo + (int)(mix32(x) % (uint32_t)(hi - lo + 1));
}

void sa_motor_init(sa_motor_t *m, int hold_lo, int hold_hi, int delay_lo, int delay_hi, uint32_t seed)
{
    if (hold_lo < 0 || hold_hi < hold_lo) hold_lo = hold_hi = 0;
    if (delay_lo < 0 || delay_hi < delay_lo) delay_lo = delay_hi = 0;
    m->hold_lo = hold_lo; m->hold_hi = hold_hi;
    m->delay_lo = delay_lo; m->delay_hi = delay_hi;
    m->slow = 0;
    m->seed = seed;
    m->t = 0;
    sa_motor_reset(m);
}

void sa_motor_set_slow(sa_motor_t *m, int on) { m->slow = on ? 1 : 0; }

static void chan_reset(sa_motor_chan_t *c) { c->need = 0; c->pend = -1; c->wait = 0; }

void sa_motor_reset(sa_motor_t *m)
{
    chan_reset(&m->dir);
    chan_reset(&m->slw);
}

/* 一路通道走一帧：想要 want、正在执行 cur（已执行 held 帧）。返回本帧实际执行的取值。
 * = 训练仓 MotorLayer._channel，逐条同义。 */
static int chan_step(sa_motor_chan_t *c, int want, int cur, int held, int hold_draw, int delay_draw)
{
    int diff = want != cur, go;
    if (diff && want != c->pend) c->wait = delay_draw;   /* 新意图（或改了主意）→ 重抽延迟 */
    go = diff && c->wait <= 0 && held >= c->need;
    if (diff && !go && c->wait > 0) c->wait--;
    if (go) c->need = hold_draw;
    c->pend = (diff && !go) ? want : -1;                 /* 想回当前取值 = 撤销；放行了也清掉 */
    return go ? want : cur;
}

int sa_motor_apply(sa_motor_t *m, int want, int prev_exec, int held, int slow_held)
{
    uint32_t t = m->t++;     /* 抽样值只由 (seed, t, stream) 决定，与「哪几步真发生了变向」无关 */
    int dir, slow;
    if (want < 0 || want >= SA_MOTOR_ACTIONS) want = 0;
    if (prev_exec < 0 || prev_exec >= SA_MOTOR_ACTIONS) prev_exec = 0;
    dir = chan_step(&m->dir, want / 2, prev_exec / 2, held,
                    sa_motor_draw(m->seed, 0, t, SA_MOTOR_STREAM_HOLD,  m->hold_lo,  m->hold_hi),
                    sa_motor_draw(m->seed, 0, t, SA_MOTOR_STREAM_DELAY, m->delay_lo, m->delay_hi));
    slow = want % 2;
    if (m->slow)
        slow = chan_step(&m->slw, slow, prev_exec % 2, slow_held,
                         sa_motor_draw(m->seed, 0, t, SA_MOTOR_STREAM_SLOW_HOLD,  m->hold_lo,  m->hold_hi),
                         sa_motor_draw(m->seed, 0, t, SA_MOTOR_STREAM_SLOW_DELAY, m->delay_lo, m->delay_hi));
    return dir * 2 + slow;
}

int sa_motor_next_held(int prev_exec, int exec, int held)
{
    if (prev_exec / 2 != exec / 2) return 1;
    return held >= SA_MOTOR_HELD_NEVER ? SA_MOTOR_HELD_NEVER : held + 1;
}

int sa_motor_next_slow_held(int prev_exec, int exec, int slow_held)
{
    if (prev_exec % 2 != exec % 2) return 1;
    return slow_held >= SA_MOTOR_HELD_NEVER ? SA_MOTOR_HELD_NEVER : slow_held + 1;
}
