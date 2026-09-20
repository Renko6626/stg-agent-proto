/* sa_motor.h —— 手部运动层：策略只说「想按哪个方向」，实际按出去的由这里决定。
 *
 * 训练仓 `stg-rl-train` 的 `envwrap.MotorLayer`（实验 N1 / N2）的 C 孪生，逐条同义。与作品无关，
 * th06nc 与 TH18 的桥共用。为什么要有它：没有这层约束练出来的模型，活命建立在「精确向右移动 1 帧」
 * 这类人手做不到的操作上（训练仓 docs/experiments.md「J 的活命建立在精确到帧的操作上」）。
 *
 * 两种**随机**约束：
 *   hold  = [lo, hi]：每换一次方向，为新的一段抽一个最短长度 L ~ U{lo..hi}；这一段没执行满 L 帧不许再换。
 *                     L 对模型不可见 —— 想点一下，出来 2 帧还是 6 帧自己说了不算。
 *   delay = [lo, hi]：想换方向时抽一个延迟 d ~ U{lo..hi}，d 帧后才生效；期间改主意就重抽，改回当前方向则撤销。
 * 默认只锁方向、低速位直通（N1 / N2）。`sa_motor_set_slow(m, 1)`（N3 / M）让低速位走**同一套规则的另一路通道**：
 * 各自的锁、待生效请求、随机抽样，与方向互不牵连 —— 按 shift 的手指和按方向的手指是两根。
 * 动作 id = 方向 × 2 + slow（动作表 v1）。
 *
 * **部署侧为什么也要跑它**：在运动层下练出来的模型已经学会留余量，但 DLL 不加这层的话，它想点 1 帧时
 * 真的会点出 1 帧，回放里照样不像人；而且模型的两个输入（上一步动作、当前方向已执行帧数）说的都是
 * **实际执行的**动作，得有人按同一口径记账。参数要与训练时一致（导出的 manifest 里有 `motor` 一项）。
 *
 * 随机数是**基于计数器**的：第 t 步、第 stream 路的抽样值 = mix32(seed + env·C1 + t·C2 + stream·C3) 取模，
 * 与训练仓 `envwrap.counter_draw` **逐位相同**（同一个公式，黄金值两边共用；部署侧 env 恒为 0）。
 * 不碰 libc 的 rand（宿主进程可能也在用），也没有随调用次数漂移的内部状态 —— 给定 (seed, t) 就能复现，
 * 所以日志里记下 seed 与起始 t，对拍工具将来可以把运动层逐帧重放出来。状态机语义由 tests/test_motor.c 押运
 * （场景与训练仓 tests/test_motor.py 一一对应）。 */
#ifndef SA_MOTOR_H
#define SA_MOTOR_H
#include <stdint.h>

/* 「当前方向已执行帧数」的新局值：很大 = 早就可以换了。与训练仓 DIR_HOLD_NEVER 同量级；
 * 图里会封顶到 16，所以具体多大无所谓，只要不溢出。 */
#define SA_MOTOR_HELD_NEVER (1 << 20)

/* 一路通道的状态。方向与低速位各一份，规则相同。 */
typedef struct {
    int need;                 /* 本段的最短长度 L；新局 0 = 第一下不受限 */
    int pend;                 /* 待生效的取值（方向 0–8 / 低速 0–1）；−1 = 没有 */
    int wait;                 /* 待生效的取值还要等几帧 */
} sa_motor_chan_t;

typedef struct {
    int hold_lo, hold_hi;     /* 每段最短保持帧数的抽样区间（闭）；两路共用 */
    int delay_lo, delay_hi;   /* 变向生效延迟的抽样区间（闭）；两路共用 */
    int slow;                 /* 1 = 低速位也过运动层（N3 / M）；0 = 直通（N1 / N2） */
    uint32_t seed;            /* 随机数的键 */
    uint32_t t;               /* 已走过的步数 = 随机数的计数器；reset 不清它（训练侧新局也不清） */
    sa_motor_chan_t dir, slw;
} sa_motor_t;

/* 区间非法（负数 / lo > hi）时按 [0, 0] 处理 = 该项约束不起作用。 */
void sa_motor_init(sa_motor_t *m, int hold_lo, int hold_hi, int delay_lo, int delay_hi, uint32_t seed);

/* 低速位过不过运动层。init 之后调；默认 0。 */
void sa_motor_set_slow(sa_motor_t *m, int on);

/* 新局 / 断帧（不可操作、切模式、进回放）：清锁与待生效请求。不重置计数器 t。 */
void sa_motor_reset(sa_motor_t *m);

/* 走一帧。`want` = 策略选的动作 id；`prev_exec` = 上一步实际执行的动作 id；
 * `held` / `slow_held` = 当前方向 / 低速位已执行帧数（`slow` 关着时 `slow_held` 不读）。
 * 返回本步实际执行的动作 id。非法 id 按 0 处理。 */
int sa_motor_apply(sa_motor_t *m, int want, int prev_exec, int held, int slow_held);

/* 执行完一步之后的记账：方向换了 → 1，否则 held + 1（封顶 SA_MOTOR_HELD_NEVER）。
 * 没开运动层也要照这个口径记 —— 图的 `dir_held` 输入与运动层开没开无关。 */
int sa_motor_next_held(int prev_exec, int exec, int held);

/* 同上，低速位那一路：低速位换了 → 1，否则 +1。图版本 4 的 `slow_held` 输入。 */
int sa_motor_next_slow_held(int prev_exec, int exec, int slow_held);

/* 计数器式抽样，U{lo..hi}。= 训练仓 `envwrap.counter_draw(seed, env, t, 1, stream, lo, hi)`。
 * stream：0 / 1 = 方向的最短保持 / 起手延迟，2 / 3 = 低速位的。四路各用各的 ⇒ 开不开低速那一路，
 * 方向这一路抽到的值都不变。 */
#define SA_MOTOR_STREAM_HOLD       0
#define SA_MOTOR_STREAM_DELAY      1
#define SA_MOTOR_STREAM_SLOW_HOLD  2
#define SA_MOTOR_STREAM_SLOW_DELAY 3
int sa_motor_draw(uint32_t seed, uint32_t env, uint32_t t, uint32_t stream, int lo, int hi);

#endif
