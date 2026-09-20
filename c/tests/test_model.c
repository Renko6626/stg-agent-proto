/* test_model.c —— 模型 I/O（sa_model.c）：输入填充的口径 + 动作表 + 滞回。
 *
 * 判别力要点：每条过滤规则都要有「刚好在界内」与「刚好在界外」两个样本，否则把 < 写成 <=
 * 之类的偏一错误照样绿。动作表逐项硬编码期望位组合，与训练仓 actions.py 的 DIR_BUTTONS 对照。 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sa_model.h"

static ap_world_t  W;
static sa_model_in_t IN;

static void reset_world(void)
{
    memset(&W, 0, sizeof W);
    W.frame = 100;
    W.player.x = 0.0f;
    W.player.y = 384.0f;
    W.player.hit_radius = 1.25f;
    W.player.speed = 4.0f;
    W.player.speed_focus = 2.0f;
    W.player.focus = 0;
}

static void put_bullet(int i, float x, float y, float vx, float vy, float r, uint8_t collidable)
{
    W.bullets[i].x = x; W.bullets[i].y = y;
    W.bullets[i].vx = vx; W.bullets[i].vy = vy;
    W.bullets[i].radius = r;
    W.bullets[i].collidable = collidable;
    W.bullets[i].state = collidable ? 1 : 2;
}

static const float *brow(int row) { return IN.bullets + (size_t)row * SA_MODEL_BULLET_COLS; }
static const float *erow(int row) { return IN.enemies + (size_t)row * SA_MODEL_ENEMY_COLS; }

static void test_player_and_scalars(void)
{
    sa_model_fill_t st;
    reset_world();
    W.player.focus = 1;
    st = sa_model_fill(&W, -48.0f, 300.0f, 13, 1 << 20, NULL, &IN);
    assert(st.bullets == 0 && st.enemies == 0 && st.bullets_dropped == 0);
    /* player 五列的顺序就是图签名：x, y, hit_radius, speed, focus */
    assert(IN.player[0] == 0.0f && IN.player[1] == 384.0f);
    assert(IN.player[2] == 1.25f);
    /* 高速档，不是低速档 —— 训练侧 write_player 恒写 cfg.high_speed */
    assert(IN.player[3] == 4.0f);
    assert(IN.player[4] == 1.0f);
    assert(IN.target[0] == -48.0f && IN.target[1] == 300.0f);
    assert(IN.prev_action[0] == 13);

    W.player.focus = 0;
    sa_model_fill(&W, 0.0f, 0.0f, 0, 1 << 20, NULL, &IN);
    assert(IN.player[4] == 0.0f);
}

static void test_prev_action_is_clamped(void)
{
    reset_world();
    sa_model_fill(&W, 0.0f, 0.0f, -5, 1 << 20, NULL, &IN);
    assert(IN.prev_action[0] == 0);
    sa_model_fill(&W, 0.0f, 0.0f, 999, 1 << 20, NULL, &IN);
    assert(IN.prev_action[0] == 0);
    sa_model_fill(&W, 0.0f, 0.0f, SA_MODEL_ACTIONS - 1, 1 << 20, NULL, &IN);
    assert(IN.prev_action[0] == SA_MODEL_ACTIONS - 1);
}

static void test_bullet_columns_and_compaction(void)
{
    sa_model_fill_t st;
    reset_world();
    put_bullet(0, 10.0f, 340.0f, 0.5f, 2.5f, 4.0f, 1);
    put_bullet(1, -20.0f, 300.0f, 0.0f, 0.0f, 3.0f, 0);   /* 不参与碰撞 → 剔除 */
    put_bullet(2, -30.0f, 200.0f, -1.0f, 1.0f, 2.0f, 1);
    W.nbullets = 3;
    st = sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, NULL, &IN);
    assert(st.bullets == 2 && st.bullets_dropped == 0);
    /* 弹五列：x, y, vx, vy, radius；剔除后向前压实 */
    assert(brow(0)[0] == 10.0f && brow(0)[1] == 340.0f && brow(0)[2] == 0.5f
           && brow(0)[3] == 2.5f && brow(0)[4] == 4.0f);
    assert(brow(1)[0] == -30.0f && brow(1)[4] == 2.0f);
    assert(IN.bullets_mask[0] == 1 && IN.bullets_mask[1] == 1 && IN.bullets_mask[2] == 0);
    /* 未用行必须是零：图对它们靠掩码遮，但清零才可复现、才与 deploy_inputs 逐位同形 */
    for (int c = 0; c < SA_MODEL_BULLET_COLS; c++) assert(brow(2)[c] == 0.0f);
}

static void test_envelope_filter_boundaries(void)
{
    sa_model_fill_t st;
    reset_world();
    /* 训练包络 x ∈ [-256, 256]、y ∈ [-64, 512]：边界上要留下，越过一点就剔除。
     * 不过滤的话场外弹会被密度图钳进边缘格，把边缘两列顶高（设计 §5）。 */
    put_bullet(0,  256.0f, 100.0f, 0, 0, 2.0f, 1);   /* 右边界上 → 留 */
    put_bullet(1,  256.5f, 100.0f, 0, 0, 2.0f, 1);   /* 越界     → 剔 */
    put_bullet(2, -256.0f, 100.0f, 0, 0, 2.0f, 1);   /* 左边界上 → 留 */
    put_bullet(3, -256.5f, 100.0f, 0, 0, 2.0f, 1);   /* 越界     → 剔 */
    put_bullet(4,  0.0f,   -64.0f, 0, 0, 2.0f, 1);   /* 上边界上 → 留 */
    put_bullet(5,  0.0f,   -64.5f, 0, 0, 2.0f, 1);   /* 越界     → 剔 */
    put_bullet(6,  0.0f,   512.0f, 0, 0, 2.0f, 1);   /* 下边界上 → 留 */
    put_bullet(7,  0.0f,   512.5f, 0, 0, 2.0f, 1);   /* 越界     → 剔 */
    W.nbullets = 8;
    st = sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, NULL, &IN);
    assert(st.bullets == 4);
    assert(brow(0)[0] == 256.0f && brow(1)[0] == -256.0f);
    assert(brow(2)[1] == -64.0f && brow(3)[1] == 512.0f);
}

static void test_bullet_rows_overflow_is_counted_not_overrun(void)
{
    sa_model_fill_t st;
    reset_world();
    for (int i = 0; i < AP_MAX_BULLETS; i++) put_bullet(i, (float)(i % 100) - 50.0f, 200.0f, 0, 1.0f, 2.0f, 1);
    W.nbullets = AP_MAX_BULLETS;
    st = sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, NULL, &IN);
    assert(st.bullets == SA_MODEL_BULLET_ROWS);
    assert(st.bullets_dropped == AP_MAX_BULLETS - SA_MODEL_BULLET_ROWS);
    assert(IN.bullets_mask[SA_MODEL_BULLET_ROWS - 1] == 1);
}

static void test_counts_out_of_range_are_clamped(void)
{
    sa_model_fill_t st;
    reset_world();
    put_bullet(0, 0.0f, 300.0f, 0, 1.0f, 2.0f, 1);
    W.nbullets = -7;                      /* 抽取器违约：不许据此越界读 */
    W.nenemies = AP_MAX_ENEMIES + 99;
    st = sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, NULL, &IN);
    assert(st.bullets == 0);
    assert(st.enemies == 0);              /* 全是零行且 collidable = 0 → 一行都不留 */
}

static void test_enemy_columns(void)
{
    sa_model_fill_t st;
    reset_world();
    W.enemies[0].x = 20.0f; W.enemies[0].y = 80.0f;
    W.enemies[0].hit_w = 16.0f; W.enemies[0].hit_h = 24.0f;   /* hit_h 不进图（只读 hit_w） */
    W.enemies[0].boss = 1; W.enemies[0].collidable = 1;
    W.enemies[1].x = -50.0f; W.enemies[1].y = 40.0f;
    W.enemies[1].hit_w = 12.0f; W.enemies[1].collidable = 0;  /* 不参与碰撞 → 剔除 */
    W.enemies[2].x = 99.0f; W.enemies[2].y = 60.0f;
    W.enemies[2].hit_w = 8.0f; W.enemies[2].boss = 0; W.enemies[2].collidable = 1;
    W.nenemies = 3;
    st = sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, NULL, &IN);
    assert(st.enemies == 2);
    /* 敌六列：x, y, hit_w, boss, vx, vy（不给 track → 速度恒 0） */
    assert(erow(0)[0] == 20.0f && erow(0)[1] == 80.0f && erow(0)[2] == 16.0f && erow(0)[3] == 1.0f);
    assert(erow(0)[4] == 0.0f && erow(0)[5] == 0.0f);
    assert(erow(1)[0] == 99.0f && erow(1)[2] == 8.0f && erow(1)[3] == 0.0f);
    assert(IN.enemies_mask[0] == 1 && IN.enemies_mask[1] == 1 && IN.enemies_mask[2] == 0);
    for (int c = 0; c < SA_MODEL_ENEMY_COLS; c++) assert(erow(2)[c] == 0.0f);
}

static void put_enemy(int i, uint32_t id, float x, float y, uint8_t collidable)
{
    W.enemies[i].id = id;
    W.enemies[i].x = x; W.enemies[i].y = y;
    W.enemies[i].hit_w = 8.0f; W.enemies[i].hit_h = 8.0f;
    W.enemies[i].collidable = collidable;
    if (i + 1 > W.nenemies) W.nenemies = i + 1;
}

/* 敌人速度 = 按 id 对上一帧差分。口径逐条对照训练仓 envwrap.enemy_velocity。 */
static void test_enemy_velocity_by_id(void)
{
    static sa_model_track_t T;
    sa_model_fill_t st;

    /* 第一帧：没有上一帧 → 全 0（训练侧「新局第一步 valid = False」） */
    reset_world();
    sa_model_track_reset(&T);
    put_enemy(0, 0, 10.0f, 50.0f, 1);      /* id 0 是合法 id（th06nc 的 id = 槽下标，0 号槽就是 0） */
    put_enemy(1, 7, -30.0f, 60.0f, 1);
    put_enemy(2, 9, 100.0f, 70.0f, 0);     /* 出生动画中：不进图，但要记下坐标 */
    sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, &T, &IN);
    assert(erow(0)[4] == 0.0f && erow(0)[5] == 0.0f && erow(1)[4] == 0.0f && erow(1)[5] == 0.0f);

    /* 第二帧：**换了行**（id 7 挪到第 0 行）也得按 id 对上，不能按行号差分 */
    reset_world();
    W.frame = 101;
    put_enemy(0, 7, -28.5f, 63.0f, 1);     /* (+1.5, +3.0) */
    put_enemy(1, 0, 10.0f, 47.5f, 1);      /* (0, −2.5) */
    put_enemy(2, 9, 101.0f, 70.0f, 1);     /* 上一帧不可碰撞、这一帧可碰撞：照样有速度 (+1, 0) */
    put_enemy(3, 12, 0.0f, 0.0f, 1);       /* 新出现：0 */
    st = sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, &T, &IN);
    assert(st.enemies == 4);
    assert(erow(0)[4] == 1.5f && erow(0)[5] == 3.0f);
    assert(erow(1)[4] == 0.0f && erow(1)[5] == -2.5f);
    assert(erow(2)[4] == 1.0f && erow(2)[5] == 0.0f);
    assert(erow(3)[4] == 0.0f && erow(3)[5] == 0.0f);

    /* 第三帧：瞬移守卫。恰好 16 px 留，16.5 px 记 0；**任一轴**超了两轴都记 0 */
    reset_world();
    W.frame = 102;
    put_enemy(0, 7, -12.5f, 63.0f, 1);     /* dx = +16 → 留 */
    put_enemy(1, 0, 11.0f, 64.0f, 1);      /* dy = +16.5 → 整只记 0（dx = 1 也不留） */
    sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, &T, &IN);
    assert(erow(0)[4] == 16.0f && erow(0)[5] == 0.0f);
    assert(erow(1)[4] == 0.0f && erow(1)[5] == 0.0f);

    /* 断帧：frame 跳了一格 → 全 0，但状态照常更新，下一帧恢复 */
    reset_world();
    W.frame = 104;
    put_enemy(0, 7, -11.5f, 63.0f, 1);
    sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, &T, &IN);
    assert(erow(0)[4] == 0.0f);
    reset_world();
    W.frame = 105;
    put_enemy(0, 7, -10.5f, 64.0f, 1);
    sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, &T, &IN);
    assert(erow(0)[4] == 1.0f && erow(0)[5] == 1.0f);

    /* reset：帧号连续也不许差分（切模式 / 不可操作之后的第一帧） */
    sa_model_track_reset(&T);
    reset_world();
    W.frame = 106;
    put_enemy(0, 7, -9.5f, 65.0f, 1);
    sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, &T, &IN);
    assert(erow(0)[4] == 0.0f && erow(0)[5] == 0.0f);

    /* frame 回绕：0xFFFFFFFF → 0 算连续 */
    reset_world();
    W.frame = 0xFFFFFFFFu;
    put_enemy(0, 7, 0.0f, 0.0f, 1);
    sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, &T, &IN);
    reset_world();
    W.frame = 0;
    put_enemy(0, 7, 2.0f, 0.0f, 1);
    sa_model_fill(&W, 0.0f, 384.0f, 0, 1 << 20, &T, &IN);
    assert(erow(0)[4] == 2.0f);
}

static void test_action_table_matches_training_repo(void)
{
    /* 逐项对照训练仓 src/stgtrain/actions.py 的 DIR_BUTTONS：
     * 方向 0 不动 1 上 2 右上 3 右 4 右下 5 下 6 左下 7 左 8 左上（顺时针）。
     * SHOT 恒按，BOMB 永不置位，slow = action_id & 1。 */
    static const uint32_t DIR[9] = {
        0,
        AP_BTN_UP,
        AP_BTN_UP | AP_BTN_RIGHT,
        AP_BTN_RIGHT,
        AP_BTN_DOWN | AP_BTN_RIGHT,
        AP_BTN_DOWN,
        AP_BTN_DOWN | AP_BTN_LEFT,
        AP_BTN_LEFT,
        AP_BTN_UP | AP_BTN_LEFT,
    };
    for (int a = 0; a < SA_MODEL_ACTIONS; a++) {
        uint32_t want = DIR[a / 2] | AP_BTN_SHOT | ((a & 1) ? AP_BTN_SLOW : 0u);
        uint32_t got = sa_model_buttons(a);
        assert(got == want);
        assert(!(got & AP_BTN_BOMB));
        assert(got & AP_BTN_SHOT);
    }
    /* 越界 id 退成「不动 + 射击」，不读表外内存 */
    assert(sa_model_buttons(-1) == AP_BTN_SHOT);
    assert(sa_model_buttons(SA_MODEL_ACTIONS) == AP_BTN_SHOT);
}

static void test_pick_argmax_and_hysteresis(void)
{
    float logits[SA_MODEL_ACTIONS];
    for (int i = 0; i < SA_MODEL_ACTIONS; i++) logits[i] = 0.0f;
    logits[4] = 2.0f;
    /* 差值必须能被 float 精确表示，否则「边界」测不到等号：2.0f − 1.6f = 0.39999998 < 0.4f，
     * <= 与 < 给出同样的结果，把偏一错误放跑（2026-09-18 变异检验实证）。1.5 / 0.5 都是精确值。 */
    logits[9] = 1.5f;

    /* τ <= 0 → 纯 argmax，与上一步无关 */
    assert(sa_model_pick(logits, 9, 0.0f) == 4);
    assert(sa_model_pick(logits, 4, 0.0f) == 4);

    /* 训练仓 evaluate.py 的 hysteresis_action：max − logits[prev] <= τ 时保持上一步。
     * 差值恰好 0.5 —— τ=0.5 是**等号**边界，按 <= 必须保持（把 <= 写成 < 时本行红）。 */
    assert(sa_model_pick(logits, 9, 0.5f) == 9);
    assert(sa_model_pick(logits, 9, 0.75f) == 9);
    assert(sa_model_pick(logits, 9, 0.25f) == 4);
    /* 上一步就是最优时无论 τ 多大都不动 */
    assert(sa_model_pick(logits, 4, 4.0f) == 4);
    /* 上一步 id 非法时退成 argmax，不读越界 */
    assert(sa_model_pick(logits, -1, 9.0f) == 4);
    assert(sa_model_pick(logits, SA_MODEL_ACTIONS, 9.0f) == 4);
}

int main(void)
{
    test_player_and_scalars();
    test_prev_action_is_clamped();
    test_bullet_columns_and_compaction();
    test_envelope_filter_boundaries();
    test_bullet_rows_overflow_is_counted_not_overrun();
    test_counts_out_of_range_are_clamped();
    test_enemy_columns();
    test_enemy_velocity_by_id();
    test_action_table_matches_training_repo();
    test_pick_argmax_and_hysteresis();
    printf("test_model ok\n");
    return 0;
}
