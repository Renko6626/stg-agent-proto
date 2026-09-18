/* test_onnx.c —— ORT 管管（sa_onnx.c）。
 *
 * 分两半：
 *
 * 1. **不需要 ORT 的部分**，`make test-host` 总是跑：库加载不上要安静失败并给出可读原因；
 *    没打开会话时 `sa_onnx_run` 不能崩。这一半覆盖的正是玩家最可能踩的两种情况
 *    （忘了拷 onnxruntime.dll / 拷错了文件）。
 * 2. **要真 ORT 的部分**，靠环境变量开：
 *      SA_ONNX_TEST_LIB      libonnxruntime.so 的路径
 *      SA_ONNX_TEST_MODEL    一张签名正确的图
 *      SA_ONNX_TEST_BADMODEL 一张签名不对的图（可选）
 *    没给就打印 skip 并返回 0 —— 宿主机器上不强求装 ORT。见 Makefile 的 test-onnx-live。 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sa_onnx.h"

static ap_world_t W;

static void test_missing_library(void)
{
    char err[512] = "";
    sa_onnx_set_library("./__sa_onnx_no_such_library__.so");
    assert(sa_onnx_open("./__no_such_model__.onnx", err, sizeof err) == 0);
    assert(!sa_onnx_is_open());
    /* 报错要点出库名，玩家才知道少拷了哪个文件 */
    assert(strstr(err, "__sa_onnx_no_such_library__") != NULL);
    assert(strstr(err, "游戏目录") != NULL);
    assert(strcmp(err, sa_onnx_last_error()) == 0);
    printf("  missing library: %s\n", err);
}

static void test_library_without_ort_symbol(void)
{
    char err[512] = "";
    /* 一个存在但不是 ORT 的动态库：拿得到句柄、拿不到 OrtGetApiBase。
     * 平台上找不到 libc.so.6 就跳过（只是换一条错误路径，不是核心断言）。 */
    sa_onnx_set_library("libc.so.6");
    if (sa_onnx_open("./__no_such_model__.onnx", err, sizeof err) != 0) {
        printf("  (意外打开了？)\n");
        sa_onnx_close();
        return;
    }
    if (strstr(err, "加载") != NULL) {
        printf("  skip library-without-symbol：本机没有 libc.so.6\n");
        return;
    }
    assert(strstr(err, "OrtGetApiBase") != NULL);
    printf("  not-an-ORT library: %s\n", err);
}

static void test_run_without_open(void)
{
    sa_onnx_close();
    assert(!sa_onnx_is_open());
    assert(sa_onnx_run() == NULL);
    assert(strstr(sa_onnx_last_error(), "未打开") != NULL);
}

static void fill_a_frame(void)
{
    sa_model_in_t *in;
    memset(&W, 0, sizeof W);
    W.player.x = 0.0f; W.player.y = 384.0f;
    W.player.hit_radius = 1.25f; W.player.speed = 4.0f; W.player.speed_focus = 2.0f;
    W.nbullets = 3;
    W.bullets[0].x = 8.0f;   W.bullets[0].y = 300.0f; W.bullets[0].vy = 3.0f;
    W.bullets[0].radius = 4.0f; W.bullets[0].collidable = 1;
    W.bullets[1].x = -40.0f; W.bullets[1].y = 340.0f; W.bullets[1].vx = 1.0f; W.bullets[1].vy = 1.0f;
    W.bullets[1].radius = 3.0f; W.bullets[1].collidable = 1;
    W.bullets[2].x = 300.0f; W.bullets[2].y = 100.0f;   /* 包络外，会被剔除 */
    W.bullets[2].radius = 3.0f; W.bullets[2].collidable = 1;
    W.nenemies = 1;
    W.enemies[0].x = 0.0f; W.enemies[0].y = 96.0f;
    W.enemies[0].hit_w = 16.0f; W.enemies[0].hit_h = 16.0f;
    W.enemies[0].boss = 1; W.enemies[0].collidable = 1;

    in = sa_onnx_inputs();
    sa_model_fill(&W, 0.0f, 384.0f, 0, in);
}

static void test_live_follows_anchor(void);

static int test_live(const char *lib, const char *model)
{
    char err[512] = "";
    const float *logits;
    int i, id, all_same = 1;

    sa_onnx_set_library(lib);
    if (!sa_onnx_open(model, err, sizeof err)) {
        printf("  live 打开失败：%s\n", err);
        return 1;
    }
    assert(sa_onnx_is_open());

    fill_a_frame();
    logits = sa_onnx_run();
    assert(logits != NULL);
    for (i = 0; i < SA_MODEL_ACTIONS; i++) {
        assert(isfinite(logits[i]));           /* 全空掩码也不许出 NaN；这里更不许 */
        if (logits[i] != logits[0]) all_same = 0;
    }
    assert(!all_same && "18 路 logits 全相等 —— 输入大概没真的喂进去");
    id = sa_model_pick(logits, 0, 0.0f);
    assert(id >= 0 && id < SA_MODEL_ACTIONS);
    printf("  live ok：argmax=%d buttons=0x%02x logits[0]=%.4f\n",
           id, sa_model_buttons(id), (double)logits[0]);

    /* 原地重填再跑一次：同一份 OrtValue 指着同一块静态缓冲，不需要重建张量 */
    fill_a_frame();
    sa_onnx_inputs()->target[0] = 120.0f;
    logits = sa_onnx_run();
    assert(logits != NULL);

    test_live_follows_anchor();

    sa_onnx_close();
    assert(!sa_onnx_is_open());
    return 0;
}

/* 空场 + 一个锚点，模型该朝锚点走。这是整条链（填数组 → 图 → 动作表）唯一的**语义**闸门：
 * 前面那些只证明「跑通了」，这条才证明「接对了」—— 列序错、x/y 调包、动作表错位都会让它红。
 * 判据宽松到只看象限，避免绑死某个 checkpoint 的细节。 */
static void put_player(float px, float py)
{
    memset(&W, 0, sizeof W);
    W.player.x = px; W.player.y = py;
    W.player.hit_radius = 1.25f; W.player.speed = 4.0f; W.player.speed_focus = 2.0f;
}

/* 用当前的 W（由 put_player 起手、调用方可再加弹）跑一帧，返回方向编号。 */
static int dir_of(float tx, float ty)
{
    const float *logits;
    sa_model_fill(&W, tx, ty, 0, sa_onnx_inputs());
    logits = sa_onnx_run();
    assert(logits != NULL);
    return sa_model_pick(logits, 0, 0.0f) / 2;   /* 只要方向，不管 slow */
}

static int in_set(int d, const int *set, int n)
{
    int i;
    for (i = 0; i < n; i++) if (d == set[i]) return 1;
    return 0;
}

static void test_live_follows_anchor(void)
{
    /* 方向编号见 sa_model.c 的 SA_DIR_BUTTONS：1 上 2 右上 3 右 4 右下 5 下 6 左下 7 左 8 左上 */
    static const int RIGHT[3] = { 2, 3, 4 }, LEFT[3] = { 6, 7, 8 };
    static const int UP[3] = { 8, 1, 2 }, DOWN[3] = { 4, 5, 6 };
    int d;

    put_player(0.0f, 384.0f);
    d = dir_of(0.0f, 384.0f);
    assert(d == 0 && "已在锚点上又没有弹，应该不动");

    put_player(0.0f, 384.0f);
    d = dir_of(150.0f, 384.0f);
    assert(in_set(d, RIGHT, 3) && "锚点在右，应朝右");

    put_player(0.0f, 384.0f);
    d = dir_of(-150.0f, 384.0f);
    assert(in_set(d, LEFT, 3) && "锚点在左，应朝左");

    put_player(0.0f, 384.0f);
    d = dir_of(0.0f, 240.0f);
    assert(in_set(d, UP, 3) && "锚点在上（y 小），应朝上");

    put_player(0.0f, 240.0f);
    d = dir_of(0.0f, 430.0f);
    assert(in_set(d, DOWN, 3) && "锚点在下（y 大），应朝下");

    /* 正上方一颗直落弹压着自机，锚点就在脚下：待着等死是错的，必须让开 */
    put_player(0.0f, 384.0f);
    W.nbullets = 1;
    W.bullets[0].x = 0.0f; W.bullets[0].y = 304.0f; W.bullets[0].vy = 4.0f;
    W.bullets[0].radius = 4.0f; W.bullets[0].collidable = 1;
    d = dir_of(0.0f, 384.0f);
    assert(d != 0 && "头上有直落弹还不动");
    printf("  live 跟锚点 ok（右/左/上/下/原地/让弹 六项）\n");
}

static int test_live_bad_signature(const char *lib, const char *bad)
{
    char err[512] = "";
    sa_onnx_set_library(lib);
    if (sa_onnx_open(bad, err, sizeof err) != 0) {
        printf("  签名不符的图竟然被接受了\n");
        sa_onnx_close();
        return 1;
    }
    assert(strstr(err, "签名不符") != NULL);
    printf("  bad signature rejected: %s\n", err);
    return 0;
}

int main(void)
{
    const char *lib = getenv("SA_ONNX_TEST_LIB");
    const char *model = getenv("SA_ONNX_TEST_MODEL");
    const char *bad = getenv("SA_ONNX_TEST_BADMODEL");
    int rc = 0;

    test_missing_library();
    test_library_without_ort_symbol();
    test_run_without_open();

    if (lib && model) {
        rc |= test_live(lib, model);
        if (bad) rc |= test_live_bad_signature(lib, bad);
    } else {
        printf("  skip live：未设 SA_ONNX_TEST_LIB / SA_ONNX_TEST_MODEL\n");
    }

    printf(rc ? "test_onnx FAILED\n" : "test_onnx ok\n");
    return rc;
}
