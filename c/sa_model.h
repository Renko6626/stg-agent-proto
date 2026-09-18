/* sa_model.h —— 模型 I/O：`ap_world_t` → ONNX 图的输入数组，以及 logits → 动作位。
 *
 * 与作品无关、与加载方式无关，只依赖 C 标准库；th06nc 与 TH18 的桥共用同一份。
 * 与**图版本**强绑定：图由训练仓 `src/stgtrain/export_onnx.py` 导出，列序与 dtype 就是那里的
 * `INPUT_NAMES` / `graph_signature()`。换特征化器（如实验 G 的 danger_topk_v3 要加敌人速度）
 * 要同时 bump 两边的 GRAPH_VERSION。
 *
 * 设计与决策记录：renkolab `docs/superpowers/specs/2026-09-18-th06nc-onnx-policy-design.md`。
 * 特征化（topk / 密度图 / 归一化）**不在这里** —— 它在图里（那份设计的 D2），本文件只填原始表。 */
#ifndef SA_MODEL_H
#define SA_MODEL_H
#include <stdint.h>
#include "world.h"

#define SA_MODEL_GRAPH_VERSION 1

/* 图签名。行数是各池容量上限，列序见下面每个字段的注释。 */
#define SA_MODEL_BULLET_ROWS 640
#define SA_MODEL_BULLET_COLS 5     /* x, y, vx, vy, radius */
#define SA_MODEL_ENEMY_ROWS  256
#define SA_MODEL_ENEMY_COLS  4     /* x, y, hit_w, boss */
#define SA_MODEL_PLAYER_COLS 5     /* x, y, hit_radius, speed, focus */
#define SA_MODEL_ACTIONS     18    /* 动作表 v1：action_id = 方向 × 2 + slow */

/* 训练包络：训练侧的弹只可能存在于这个范围里（stg-engine `crates/stg-core/src/world.rs` 的
 * `OOB_MARGIN = 64`），而抽取器收的是池里全部活着的弹。特征化的密度图对格外坐标是**钳位**
 * 而不是丢弃，所以不过滤的话场外弹会全部堆进边缘格。边界值本身算界内。 */
#define SA_MODEL_ENV_HALF_W  256.0f
#define SA_MODEL_ENV_Y_MIN   (-64.0f)
#define SA_MODEL_ENV_Y_MAX   512.0f

/* 七个输入，逐字段对应图的输入名。整块可以静态分配（约 17 KB），钩子内不碰堆。 */
typedef struct {
    float   bullets[SA_MODEL_BULLET_ROWS * SA_MODEL_BULLET_COLS];   /* "bullets"      f32[640,5] */
    uint8_t bullets_mask[SA_MODEL_BULLET_ROWS];                     /* "bullets_mask" bool[640]  */
    float   enemies[SA_MODEL_ENEMY_ROWS * SA_MODEL_ENEMY_COLS];     /* "enemies"      f32[256,4] */
    uint8_t enemies_mask[SA_MODEL_ENEMY_ROWS];                      /* "enemies_mask" bool[256]  */
    float   player[SA_MODEL_PLAYER_COLS];                           /* "player"       f32[5]     */
    float   target[2];                                              /* "target"       f32[2]     */
    int64_t prev_action[1];                                         /* "prev_action"  i64[1]     */
} sa_model_in_t;

typedef struct {
    int bullets;          /* 填进去的弹行数 */
    int enemies;          /* 填进去的敌行数 */
    int bullets_dropped;  /* 通过过滤但行数装不下而丢掉的弹（正常玩不该非零） */
} sa_model_fill_t;

/* 把一帧世界摊成图的输入。`target_*` 是锚点（协议坐标），`prev_action` 是上一步动作 id
 * （0–17，非法值按 0 处理）。未用行清零。`w->n*` 越界时按 [0, AP_MAX_*] 钳，不据此越界读。 */
sa_model_fill_t sa_model_fill(const ap_world_t *w, float target_x, float target_y,
                              int prev_action, sa_model_in_t *in);

/* 动作 id → 动作位（`AP_BTN_*`）。SHOT 恒按、BOMB 永不置位（动作表 v1 屏蔽了它）。
 * 非法 id 退成「不动 + 射击」。 */
uint32_t sa_model_buttons(int action_id);

/* logits → 动作 id。`hysteresis > 0` 时：只有最优动作的 logit 比上一步动作高出**超过** τ 才换，
 * 否则保持上一步（训练仓 `evaluate.py` 的 `hysteresis_action`，逐条同义）。τ <= 0 即纯 argmax。
 * `prev_action` 非法时退成纯 argmax。 */
int sa_model_pick(const float *logits, int prev_action, float hysteresis);

#endif
