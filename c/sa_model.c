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

void sa_model_track_reset(sa_model_track_t *t) { t->valid = 0; t->n = 0; }

/* 上一帧里 id 相同的那只的下标，没有返回 −1。敌池最多 256、场上通常十几只，线性找就够。
 * 先试同下标：抽取器按池序收，绝大多数帧同一只敌就在同一行。 */
static int track_find(const sa_model_track_t *t, int hint, uint32_t id)
{
    int j;
    if (hint < t->n && t->id[hint] == id) return hint;
    for (j = 0; j < t->n; j++)
        if (t->id[j] == id) return j;
    return -1;
}

static float fabs_f(float v) { return v < 0.0f ? -v : v; }

sa_model_fill_t sa_model_fill(const ap_world_t *w, float target_x, float target_y,
                              int prev_action, int dir_held, sa_model_track_t *track, sa_model_in_t *in)
{
    sa_model_fill_t st;
    int i, nb, ne, row, have_prev;

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
    in->dir_held[0] = dir_held > 0 ? dir_held : 0;

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
    have_prev = track && track->valid && w->frame == track->frame + 1u;
    for (i = 0, row = 0; i < ne; i++) {
        const ap_enemy_t *e = &w->enemies[i];
        float *dst;
        if (!e->collidable) continue;
        if (row >= SA_MODEL_ENEMY_ROWS) break;
        dst = in->enemies + (size_t)row * SA_MODEL_ENEMY_COLS;
        dst[0] = e->x;
        dst[1] = e->y;
        /* hit_h 不进图：danger_topk 把敌人当成半径 hit_w 的圆（迁移差异登记 §9 第 2 条）。 */
        dst[2] = e->hit_w;
        dst[3] = e->boss ? 1.0f : 0.0f;
        /* 速度 = 对上一帧同 id 那只的位移（训练仓 envwrap.enemy_velocity，frame_skip = 1）。
         * memset 已经把这两列清零，对不上 / 断帧 / 瞬移都落在「保持 0」上。 */
        if (have_prev) {
            int j = track_find(track, i, e->id);
            if (j >= 0) {
                float dx = e->x - track->x[j], dy = e->y - track->y[j];
                if (fabs_f(dx) <= SA_MODEL_TELEPORT_PX && fabs_f(dy) <= SA_MODEL_TELEPORT_PX) {
                    dst[4] = dx;
                    dst[5] = dy;
                }
            }
        }
        in->enemies_mask[row] = 1;
        row++;
    }
    st.enemies = row;

    /* 记下本帧**全部**敌人（不论 collidable）：出生动画里的敌下一帧变成可碰撞时，
     * 训练侧照样有它上一帧的坐标（env 的敌表不按 collidable 过滤，掩码是另一回事）。 */
    if (track) {
        for (i = 0; i < ne; i++) {
            track->id[i] = w->enemies[i].id;
            track->x[i] = w->enemies[i].x;
            track->y[i] = w->enemies[i].y;
        }
        track->n = ne;
        track->frame = w->frame;
        track->valid = 1;
    }

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
