/* sa_model.h —— 模型 I/O：`ap_world_t` → ONNX 图的输入数组，以及 logits → 动作位。
 *
 * 与作品无关、与加载方式无关，只依赖 C 标准库；th06nc 与 TH18 的桥共用同一份。
 * 与**图版本**强绑定：图由训练仓 `src/stgtrain/export_onnx.py` 导出，列序与 dtype 就是那里的
 * `INPUT_NAMES` / `graph_signature()`。动图签名要同时 bump 两边的 GRAPH_VERSION。
 *
 * **图版本 2（2026-09-20）**：`enemies` 四列 → 六列，多出 `vx, vy`（danger_topk_v3 的敌人速度）。
 * 协议与 `ap_enemy_t` 都不带敌人速度（训练 env 也不导出），两边都是**按 id 跨帧差分**出来的；
 * 这里的口径逐条对照训练仓 `envwrap.enemy_velocity`，状态放在调用方持有的 `sa_model_track_t` 里。
 * v2 特征化的图（实验 F）按同一套六列签名重导后照样能装 —— 那两列进了图没人读。
 *
 * **图版本 3（2026-09-20）**：多一个输入 `dir_held i64[1]` = 当前方向已经**实际执行**了多少帧
 * （danger_topk_v4，实验 N 的手部运动层；口径见 `sa_motor.h`）。只有在运动层下练出来的图才有这个输入；
 * 版本 2 的七输入图照样能装（`sa_onnx_has_held()` 说的是装上的这张图属于哪一种）。
 * 版本 3 的图里，`prev_action` 的含义是上一步**实际执行**的动作（运动层之后的），不是策略想按的。
 *
 * **图版本 4（2026-09-21）**：再多一个输入 `slow_held i64[1]` = 当前低速位（按着 / 松着）已经实际执行了多少帧
 * （danger_topk_v5，实验 N3 / M：低速键也过运动层）。图版本 = 2 + held 输入的个数，三种图 `sa_onnx` 都收。
 *
 * 设计与决策记录：renkolab `docs/superpowers/specs/2026-09-18-th06nc-onnx-policy-design.md`。
 * 特征化（topk / 密度图 / 归一化）**不在这里** —— 它在图里（那份设计的 D2），本文件只填原始表。 */
#ifndef SA_MODEL_H
#define SA_MODEL_H
#include <stdint.h>
#include "world.h"

#define SA_MODEL_GRAPH_VERSION 4     /* 同时兼容版本 2（七输入）与 3（八输入） */

/* 图签名。行数是各池容量上限，列序见下面每个字段的注释。 */
#define SA_MODEL_BULLET_ROWS 640
#define SA_MODEL_BULLET_COLS 5     /* x, y, vx, vy, radius */
#define SA_MODEL_ENEMY_ROWS  256
#define SA_MODEL_ENEMY_COLS  6     /* x, y, hit_w, boss, vx, vy */
#define SA_MODEL_PLAYER_COLS 5     /* x, y, hit_radius, speed, focus */
#define SA_MODEL_ACTIONS     18    /* 动作表 v1：action_id = 方向 × 2 + slow */

/* 训练包络：训练侧的弹只可能存在于这个范围里（stg-engine `crates/stg-core/src/world.rs` 的
 * `OOB_MARGIN = 64`），而抽取器收的是池里全部活着的弹。特征化的密度图对格外坐标是**钳位**
 * 而不是丢弃，所以不过滤的话场外弹会全部堆进边缘格。边界值本身算界内。 */
#define SA_MODEL_ENV_HALF_W  256.0f
#define SA_MODEL_ENV_Y_MIN   (-64.0f)
#define SA_MODEL_ENV_Y_MAX   512.0f

/* 瞬移守卫：单帧位移（任一轴）超过它 = 瞬移而不是运动，速度记 0。
 * = 训练仓 `envwrap.TELEPORT_PX`。敌人正常移动远到不了这个量级（原作 boss 游走约 2.5 px/帧），
 * 而 boss 换位 / 槽位被新敌人复用都会造出上百 px 的假位移。 */
#define SA_MODEL_TELEPORT_PX 16.0f

/* 七个输入，逐字段对应图的输入名。整块可以静态分配（约 17 KB），钩子内不碰堆。 */
typedef struct {
    float   bullets[SA_MODEL_BULLET_ROWS * SA_MODEL_BULLET_COLS];   /* "bullets"      f32[640,5] */
    uint8_t bullets_mask[SA_MODEL_BULLET_ROWS];                     /* "bullets_mask" bool[640]  */
    float   enemies[SA_MODEL_ENEMY_ROWS * SA_MODEL_ENEMY_COLS];     /* "enemies"      f32[256,6] */
    uint8_t enemies_mask[SA_MODEL_ENEMY_ROWS];                      /* "enemies_mask" bool[256]  */
    float   player[SA_MODEL_PLAYER_COLS];                           /* "player"       f32[5]     */
    float   target[2];                                              /* "target"       f32[2]     */
    int64_t prev_action[1];                                         /* "prev_action"  i64[1]     */
    int64_t dir_held[1];                                            /* "dir_held"     i64[1]，图版本 ≥ 3 */
    int64_t slow_held[1];                                           /* "slow_held"    i64[1]，图版本 4   */
} sa_model_in_t;

/* 敌人速度差分用的跨帧状态：上一次 `sa_model_fill` 看到的全部敌人（不论 collidable）。
 * 调用方持有、清零即初始态。**断帧要清**（`sa_model_track_reset`）：不可操作 / 切模式 / 进回放
 * 之后的第一帧没有可信的「上一帧」—— 训练侧「新局第一步速度记 0」就是这个语义。
 * 此外 `sa_model_fill` 自己也验 `frame` 连续，漏清也不会拿几百帧前的坐标差分。 */
typedef struct {
    int      valid;
    uint32_t frame;
    int      n;
    uint32_t id[AP_MAX_ENEMIES];
    float    x[AP_MAX_ENEMIES], y[AP_MAX_ENEMIES];
} sa_model_track_t;

void sa_model_track_reset(sa_model_track_t *t);

typedef struct {
    int bullets;          /* 填进去的弹行数 */
    int enemies;          /* 填进去的敌行数 */
    int bullets_dropped;  /* 通过过滤但行数装不下而丢掉的弹（正常玩不该非零） */
} sa_model_fill_t;

/* 把一帧世界摊成图的输入。`target_*` 是锚点（协议坐标），`prev_action` 是上一步动作 id
 * （0–17，非法值按 0 处理）。未用行清零。`w->n*` 越界时按 [0, AP_MAX_*] 钳，不据此越界读。
 *
 * `dir_held` / `slow_held` 是当前方向 / 低速位已实际执行的帧数（新局 = `SA_MOTOR_HELD_NEVER`；负数按 0）。
 * 低版本的图没有这些输入，填了也没人读，所以调用方不必区分。
 *
 * `track` 是敌人速度的跨帧状态，本函数读完就地更新成本帧；传 `NULL` 则速度两列恒 0
 * （只给不关心速度的测试用 —— 真部署传 NULL 等于把 v3 的图喂成瞎子）。速度的口径：
 *   - 按 `ap_enemy_t.id` 对上一帧（id 相同的第一只）；对不上 = 新出现的敌，记 0；
 *   - 上一帧必须恰好是 `w->frame − 1`，否则全部记 0；
 *   - 任一轴位移 > `SA_MODEL_TELEPORT_PX` 记 0。 */
sa_model_fill_t sa_model_fill(const ap_world_t *w, float target_x, float target_y,
                              int prev_action, int dir_held, int slow_held,
                              sa_model_track_t *track, sa_model_in_t *in);

/* 动作 id → 动作位（`AP_BTN_*`）。SHOT 恒按、BOMB 永不置位（动作表 v1 屏蔽了它）。
 * 非法 id 退成「不动 + 射击」。 */
uint32_t sa_model_buttons(int action_id);

/* logits → 动作 id。`hysteresis > 0` 时：只有最优动作的 logit 比上一步动作高出**超过** τ 才换，
 * 否则保持上一步（训练仓 `evaluate.py` 的 `hysteresis_action`，逐条同义）。τ <= 0 即纯 argmax。
 * `prev_action` 非法时退成纯 argmax。 */
int sa_model_pick(const float *logits, int prev_action, float hysteresis);

#endif
