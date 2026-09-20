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
    m->seed = seed;
    m->t = 0;
    sa_motor_reset(m);
}

void sa_motor_reset(sa_motor_t *m)
{
    m->need = 0;
    m->pend = -1;
    m->wait = 0;
}

int sa_motor_apply(sa_motor_t *m, int want, int prev_exec, int held)
{
    int want_dir, slow, cur, diff, go, hold_draw, delay_draw;
    /* 与训练侧一样：每步两路各抽一个，用不用得上都消耗一个计数 —— 抽样值因此与「哪几步真发生了变向」无关 */
    hold_draw  = sa_motor_draw(m->seed, 0, m->t, SA_MOTOR_STREAM_HOLD,  m->hold_lo,  m->hold_hi);
    delay_draw = sa_motor_draw(m->seed, 0, m->t, SA_MOTOR_STREAM_DELAY, m->delay_lo, m->delay_hi);
    m->t++;
    if (want < 0 || want >= SA_MOTOR_ACTIONS) want = 0;
    if (prev_exec < 0 || prev_exec >= SA_MOTOR_ACTIONS) prev_exec = 0;
    want_dir = want / 2; slow = want % 2; cur = prev_exec / 2;
    diff = want_dir != cur;

    if (diff && want_dir != m->pend)                 /* 新意图（或改了主意）→ 重抽延迟 */
        m->wait = delay_draw;
    m->pend = diff ? want_dir : -1;                  /* 想回当前方向 = 撤销 */

    go = diff && m->wait <= 0 && held >= m->need;
    if (diff && !go && m->wait > 0) m->wait--;
    if (go) {
        m->need = hold_draw;
        m->pend = -1;
    }
    return (go ? want_dir : cur) * 2 + slow;
}

int sa_motor_next_held(int prev_exec, int exec, int held)
{
    if (prev_exec / 2 != exec / 2) return 1;
    return held >= SA_MOTOR_HELD_NEVER ? SA_MOTOR_HELD_NEVER : held + 1;
}
