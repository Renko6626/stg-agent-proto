/* sa_model.c —— 见 sa_model.h。纯计算，无堆分配、无平台依赖、无 x87（只用 float 比较与赋值）。 */
#include <string.h>
#include "sa_model.h"

/* 动作表 v1（训练仓 src/stgtrain/actions.py 的 DIR_BUTTONS，顺时针）：
 * 0 不动 1 上 2 右上 3 右 4 右下 5 下 6 左下 7 左 8 左上。改这张表 = 旧 checkpoint 作废。 */
static const uint32_t SA_DIR_BUTTONS[9] = {
    0u,
    AP_BTN_UP,
    AP_BTN_UP | AP_BTN_RIGHT,
    AP_BTN_RIGHT,
    AP_BTN_DOWN | AP_BTN_RIGHT,
    AP_BTN_DOWN,
    AP_BTN_DOWN | AP_BTN_LEFT,
    AP_BTN_LEFT,
    AP_BTN_UP | AP_BTN_LEFT,
};

static int clamp_count(int n, int cap)
{
    if (n < 0) return 0;
    return n > cap ? cap : n;
}

static int in_envelope(float x, float y)
{
    return x >= -SA_MODEL_ENV_HALF_W && x <= SA_MODEL_ENV_HALF_W
        && y >= SA_MODEL_ENV_Y_MIN && y <= SA_MODEL_ENV_Y_MAX;
}

sa_model_fill_t sa_model_fill(const ap_world_t *w, float target_x, float target_y,
                              int prev_action, sa_model_in_t *in)
{
    sa_model_fill_t st;
    int i, nb, ne, row;

    st.bullets = st.enemies = st.bullets_dropped = 0;
    memset(in, 0, sizeof *in);

    in->player[0] = w->player.x;
    in->player[1] = w->player.y;
    in->player[2] = w->player.hit_radius;
    /* 高速档，不是 speed_focus：训练侧 write_player 恒写 cfg.high_speed，与 focus 无关。
     * 特征化用它算「假设朝锚点走」的自机速度，低速档从来没进过观测。 */
    in->player[3] = w->player.speed;
    in->player[4] = w->player.focus ? 1.0f : 0.0f;

    in->target[0] = target_x;
    in->target[1] = target_y;
    in->prev_action[0] = (prev_action >= 0 && prev_action < SA_MODEL_ACTIONS) ? prev_action : 0;

    nb = clamp_count(w->nbullets, AP_MAX_BULLETS);
    for (i = 0, row = 0; i < nb; i++) {
        const ap_bullet_t *b = &w->bullets[i];
        float *dst;
        if (!b->collidable) continue;            /* 出生动画 / 消失中的弹引擎本帧不拿它撞自机 */
        if (!in_envelope(b->x, b->y)) continue;  /* 训练包络外，见 sa_model.h */
        if (row >= SA_MODEL_BULLET_ROWS) { st.bullets_dropped++; continue; }
        dst = in->bullets + (size_t)row * SA_MODEL_BULLET_COLS;
        dst[0] = b->x;
        dst[1] = b->y;
        dst[2] = b->vx;
        dst[3] = b->vy;
        dst[4] = b->radius;
        in->bullets_mask[row] = 1;
        row++;
    }
    st.bullets = row;

    /* 敌人**不按位置过滤**：训练侧 enemies_mask 只看行数与 collidable
     * （stg-engine 的敌人越界边距是 256，实际上不构成过滤），这里照样。 */
    ne = clamp_count(w->nenemies, AP_MAX_ENEMIES);
    for (i = 0, row = 0; i < ne; i++) {
        const ap_enemy_t *e = &w->enemies[i];
        float *dst;
        if (!e->collidable) continue;
        if (row >= SA_MODEL_ENEMY_ROWS) break;
        dst = in->enemies + (size_t)row * SA_MODEL_ENEMY_COLS;
        dst[0] = e->x;
        dst[1] = e->y;
        /* hit_h 不进图：danger_topk_v2 把敌人当成半径 hit_w 的圆（迁移差异登记 §9 第 2 条）。 */
        dst[2] = e->hit_w;
        dst[3] = e->boss ? 1.0f : 0.0f;
        in->enemies_mask[row] = 1;
        row++;
    }
    st.enemies = row;

    return st;
}

uint32_t sa_model_buttons(int action_id)
{
    int dir;
    if (action_id < 0 || action_id >= SA_MODEL_ACTIONS) return AP_BTN_SHOT;
    dir = action_id / 2;
    return SA_DIR_BUTTONS[dir] | AP_BTN_SHOT | ((action_id & 1) ? AP_BTN_SLOW : 0u);
}

int sa_model_pick(const float *logits, int prev_action, float hysteresis)
{
    int best = 0, i;
    for (i = 1; i < SA_MODEL_ACTIONS; i++)
        if (logits[i] > logits[best]) best = i;
    if (hysteresis > 0.0f && prev_action >= 0 && prev_action < SA_MODEL_ACTIONS
        && logits[best] - logits[prev_action] <= hysteresis)
        return prev_action;
    return best;
}
