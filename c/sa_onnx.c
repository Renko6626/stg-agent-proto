/* sa_onnx.c —— 见 sa_onnx.h。 */
#include <stdio.h>
#include <string.h>
#include "sa_onnx.h"
#include "vendor/onnxruntime_c_api.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/* 运行时按 ORT_API_VERSION 往下试到这里。我们只用一批很老的 API（CreateEnv / CreateSession /
 * CreateTensorWithDataAsOrtValue / Run），所以拿到更低版本的 OrtApi 照样能跑；这条只是让
 * 玩家机器上恰好装了个稍旧的 onnxruntime.dll 时也不至于直接不认。 */
#define SA_ONNX_MIN_API 11

#define SA_ONNX_NINPUTS 7

static const char *const IN_NAMES[SA_ONNX_NINPUTS] = {
    "bullets", "bullets_mask", "enemies", "enemies_mask", "player", "target", "prev_action",
};
static const char *const OUT_NAMES[1] = { "logits" };

/* 每个输入的元素类型、形状与落地缓冲。顺序与 IN_NAMES 一致，也就是图签名的顺序。 */
typedef struct {
    ONNXTensorElementDataType type;
    int64_t dims[2];
    size_t  ndim;
    void   *data;
    size_t  bytes;
} sa_in_spec_t;

static sa_model_in_t G_in;                       /* 静态输入缓冲，约 17 KB */
static float         G_logits[SA_MODEL_ACTIONS];
/* G_err 比任何单条消息都宽裕；插入外来字符串（库名 / 路径 / ORT 消息）时一律用 %.Ns 限宽，
 * 否则 -Wformat-truncation 会（正确地）指出 512 塞不进 512。 */
static char          G_err[1024];
static char          G_lib[256] = SA_ONNX_DEFAULT_LIB;

static struct {
    void              *lib;
    const OrtApi      *api;
    OrtEnv            *env;
    OrtSessionOptions *opts;
    OrtSession        *sess;
    OrtMemoryInfo     *mem;
    OrtValue          *in[SA_ONNX_NINPUTS];
    OrtValue          *out;
    int                open;
} G;

static void spec_table(sa_in_spec_t s[SA_ONNX_NINPUTS])
{
    const ONNXTensorElementDataType F = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    const ONNXTensorElementDataType B = ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
    const ONNXTensorElementDataType I = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
    sa_in_spec_t t[SA_ONNX_NINPUTS] = {
        { F, { SA_MODEL_BULLET_ROWS, SA_MODEL_BULLET_COLS }, 2, G_in.bullets,      sizeof G_in.bullets },
        { B, { SA_MODEL_BULLET_ROWS, 0 },                    1, G_in.bullets_mask, sizeof G_in.bullets_mask },
        { F, { SA_MODEL_ENEMY_ROWS, SA_MODEL_ENEMY_COLS },   2, G_in.enemies,      sizeof G_in.enemies },
        { B, { SA_MODEL_ENEMY_ROWS, 0 },                     1, G_in.enemies_mask, sizeof G_in.enemies_mask },
        { F, { SA_MODEL_PLAYER_COLS, 0 },                    1, G_in.player,       sizeof G_in.player },
        { F, { 2, 0 },                                       1, G_in.target,       sizeof G_in.target },
        { I, { 1, 0 },                                       1, G_in.prev_action,  sizeof G_in.prev_action },
    };
    memcpy(s, t, sizeof t);
}

/* ---- 平台：动态加载 ---- */

#ifdef _WIN32
static void *dl_open(const char *path)
{
    wchar_t w[512];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, (int)(sizeof w / sizeof w[0])) == 0) return NULL;
    return (void *)LoadLibraryW(w);
}
static void *dl_sym(void *h, const char *name) { return (void *)GetProcAddress((HMODULE)h, name); }
static void  dl_close(void *h) { if (h) FreeLibrary((HMODULE)h); }
#else
static void *dl_open(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *dl_sym(void *h, const char *name) { return dlsym(h, name); }
static void  dl_close(void *h) { if (h) dlclose(h); }
#endif

/* ---- 错误处理 ----
 * 约定：先 `snprintf(G_err, ...)` 填好原因，再 `return fail(err, errcap);` 抄给调用方。
 * 不做「把格式串当参数传」的辅助函数 —— 那种写法迟早把 %d 喂给 char*。 */

static int fail(char *err, int errcap)
{
    if (err && errcap > 0) snprintf(err, (size_t)errcap, "%s", G_err);
    return 0;
}

/* 把 OrtStatus 的消息抄进 G_err 并释放它。返回 0（调用方直接 `return ort_fail(...)`）。 */
static int ort_fail(OrtStatus *st, char *err, int errcap, const char *what)
{
    snprintf(G_err, sizeof G_err, "%.64s：%.700s", what,
             (st && G.api) ? G.api->GetErrorMessage(st) : "（无消息）");
    if (st && G.api) G.api->ReleaseStatus(st);
    return fail(err, errcap);
}

static void release_all(void)
{
    int i;
    if (G.api) {
        for (i = 0; i < SA_ONNX_NINPUTS; i++) {
            if (G.in[i]) G.api->ReleaseValue(G.in[i]);
            G.in[i] = NULL;
        }
        if (G.out) { G.api->ReleaseValue(G.out); G.out = NULL; }
        if (G.mem) { G.api->ReleaseMemoryInfo(G.mem); G.mem = NULL; }
        if (G.sess) { G.api->ReleaseSession(G.sess); G.sess = NULL; }
        if (G.opts) { G.api->ReleaseSessionOptions(G.opts); G.opts = NULL; }
        if (G.env) { G.api->ReleaseEnv(G.env); G.env = NULL; }
    }
    if (G.lib) { dl_close(G.lib); G.lib = NULL; }
    G.api = NULL;
    G.open = 0;
}

/* ---- 签名校验 ---- */

static const char *type_name(ONNXTensorElementDataType t)
{
    switch (t) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: return "float32";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:  return "bool";
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: return "int64";
    default: return "其他";
    }
}

static int check_one_input(size_t i, const sa_in_spec_t *want, char *err, int errcap)
{
    OrtStatus *st;
    OrtAllocator *alloc = NULL;
    OrtTypeInfo *ti = NULL;
    const OrtTensorTypeAndShapeInfo *si = NULL;
    ONNXTensorElementDataType got_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    size_t got_ndim = 0;
    /* ndim > 容量时 GetDimensions 不会被调用（形状本来就已经判定不符），清零免得消息里读到垃圾。 */
    int64_t got_dims[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const size_t DIMCAP = sizeof got_dims / sizeof got_dims[0];
    char *name = NULL;
    size_t d;
    int ok = 1;

    if ((st = G.api->GetAllocatorWithDefaultOptions(&alloc)) != NULL)
        return ort_fail(st, err, errcap, "取默认分配器失败");
    if ((st = G.api->SessionGetInputName(G.sess, i, alloc, &name)) != NULL)
        return ort_fail(st, err, errcap, "取输入名失败");
    if (strcmp(name, IN_NAMES[i]) != 0) {
        snprintf(G_err, sizeof G_err, "图签名不符：第 %d 个输入名是 %.128s，应为 %.32s",
                 (int)i, name, IN_NAMES[i]);
        alloc->Free(alloc, name);
        return fail(err, errcap);
    }
    alloc->Free(alloc, name);

    if ((st = G.api->SessionGetInputTypeInfo(G.sess, i, &ti)) != NULL)
        return ort_fail(st, err, errcap, "取输入类型失败");
    if ((st = G.api->CastTypeInfoToTensorInfo(ti, &si)) != NULL || si == NULL) {
        G.api->ReleaseTypeInfo(ti);
        return ort_fail(st, err, errcap, "输入不是张量");
    }
    if ((st = G.api->GetTensorElementType(si, &got_type)) != NULL
        || (st = G.api->GetDimensionsCount(si, &got_ndim)) != NULL) {
        G.api->ReleaseTypeInfo(ti);
        return ort_fail(st, err, errcap, "取输入形状失败");
    }
    if (got_ndim <= DIMCAP)
        st = G.api->GetDimensions(si, got_dims, got_ndim);
    G.api->ReleaseTypeInfo(ti);
    if (st != NULL) return ort_fail(st, err, errcap, "取输入维度失败");

    if (got_type != want->type || got_ndim != want->ndim) ok = 0;
    for (d = 0; ok && d < want->ndim; d++)
        if (got_dims[d] != want->dims[d]) ok = 0;
    if (!ok) {
        char g[96], e[96];
        int n = snprintf(g, sizeof g, "%s[", type_name(got_type));
        for (d = 0; d < got_ndim && d < DIMCAP && n < (int)sizeof g - 8; d++)
            n += snprintf(g + n, sizeof g - (size_t)n, d ? ",%lld" : "%lld", (long long)got_dims[d]);
        snprintf(g + n, sizeof g - (size_t)n, "]");
        n = snprintf(e, sizeof e, "%s[", type_name(want->type));
        for (d = 0; d < want->ndim && n < (int)sizeof e - 8; d++)
            n += snprintf(e + n, sizeof e - (size_t)n, d ? ",%lld" : "%lld", (long long)want->dims[d]);
        snprintf(e + n, sizeof e - (size_t)n, "]");
        snprintf(G_err, sizeof G_err,
                 "图签名不符：输入 %.32s 是 %.96s，应为 %.96s（图与 sa_model.h 的版本不匹配？）",
                 IN_NAMES[i], g, e);
        return fail(err, errcap);
    }
    return 1;
}

static int check_signature(const sa_in_spec_t *specs, char *err, int errcap)
{
    OrtStatus *st;
    size_t n = 0, i;

    if ((st = G.api->SessionGetInputCount(G.sess, &n)) != NULL)
        return ort_fail(st, err, errcap, "取输入个数失败");
    if (n != SA_ONNX_NINPUTS) {
        snprintf(G_err, sizeof G_err, "图签名不符：图有 %d 个输入，应为 %d",
                 (int)n, SA_ONNX_NINPUTS);
        return fail(err, errcap);
    }
    for (i = 0; i < SA_ONNX_NINPUTS; i++)
        if (!check_one_input(i, &specs[i], err, errcap)) return 0;

    if ((st = G.api->SessionGetOutputCount(G.sess, &n)) != NULL)
        return ort_fail(st, err, errcap, "取输出个数失败");
    if (n != 1) {
        snprintf(G_err, sizeof G_err, "图签名不符：图有 %d 个输出，应为 1", (int)n);
        return fail(err, errcap);
    }
    return 1;
}

/* ---- 公开接口 ---- */

void sa_onnx_set_library(const char *path)
{
    snprintf(G_lib, sizeof G_lib, "%s", path && *path ? path : SA_ONNX_DEFAULT_LIB);
}

int sa_onnx_is_open(void) { return G.open; }

sa_model_in_t *sa_onnx_inputs(void) { return &G_in; }

const char *sa_onnx_last_error(void) { return G_err; }

int sa_onnx_open(const char *model_path, char *err, int errcap)
{
    const OrtApiBase *(ORT_API_CALL *get_base)(void);
    const OrtApiBase *base;
    sa_in_spec_t specs[SA_ONNX_NINPUTS];
    OrtStatus *st;
    int ver, i;
#ifdef _WIN32
    wchar_t wpath[512];
#endif

    if (G.open) return 1;
    G_err[0] = '\0';
    memset(&G, 0, sizeof G);
    memset(&G_in, 0, sizeof G_in);
    memset(G_logits, 0, sizeof G_logits);

    G.lib = dl_open(G_lib);
    if (!G.lib) {
        snprintf(G_err, sizeof G_err, "加载 %.200s 失败：把它放到游戏目录（与 exe 同层）", G_lib);
        return fail(err, errcap);
    }
    /* 取函数指针只能走 void* 中转：ISO C 不许 void* ↔ 函数指针直转，-Wpedantic 会叫。 */
    {
        void *sym = dl_sym(G.lib, "OrtGetApiBase");
        if (!sym) {
            snprintf(G_err, sizeof G_err, "%.200s 里没有 OrtGetApiBase（不是 ONNX Runtime？）", G_lib);
            release_all();
            return fail(err, errcap);
        }
        memcpy(&get_base, &sym, sizeof sym);
    }
    base = get_base();
    if (!base) {
        snprintf(G_err, sizeof G_err, "%.200s 的 OrtGetApiBase 返回空", G_lib);
        release_all();
        return fail(err, errcap);
    }
    for (ver = ORT_API_VERSION; ver >= SA_ONNX_MIN_API && G.api == NULL; ver--)
        G.api = base->GetApi((uint32_t)ver);
    if (!G.api) {
        snprintf(G_err, sizeof G_err, "%.200s 的 API 版本太旧：要求 >= %d（本仓头文件是 v%d）",
                 G_lib, SA_ONNX_MIN_API, ORT_API_VERSION);
        release_all();
        return fail(err, errcap);
    }

    if ((st = G.api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "sa_onnx", &G.env)) != NULL) {
        ort_fail(st, err, errcap, "CreateEnv 失败");
        release_all();
        return 0;
    }
    if ((st = G.api->CreateSessionOptions(&G.opts)) != NULL) {
        ort_fail(st, err, errcap, "CreateSessionOptions 失败");
        release_all();
        return 0;
    }
    /* 单线程、顺序执行：这是在游戏的逻辑帧回调里跑，不许自己开线程池抢核。
     * 这几个 Set* 都返回 OrtStatus* 且带 warn_unused_result，必须逐个检查。 */
    if ((st = G.api->SetIntraOpNumThreads(G.opts, 1)) != NULL
        || (st = G.api->SetInterOpNumThreads(G.opts, 1)) != NULL
        || (st = G.api->SetSessionExecutionMode(G.opts, ORT_SEQUENTIAL)) != NULL
        || (st = G.api->SetSessionGraphOptimizationLevel(G.opts, ORT_ENABLE_ALL)) != NULL) {
        ort_fail(st, err, errcap, "设置会话选项失败");
        release_all();
        return 0;
    }

#ifdef _WIN32
    if (MultiByteToWideChar(CP_UTF8, 0, model_path, -1, wpath,
                            (int)(sizeof wpath / sizeof wpath[0])) == 0) {
        snprintf(G_err, sizeof G_err, "模型路径转宽字符失败：%.256s", model_path);
        release_all();
        return fail(err, errcap);
    }
    st = G.api->CreateSession(G.env, wpath, G.opts, &G.sess);
#else
    st = G.api->CreateSession(G.env, model_path, G.opts, &G.sess);
#endif
    if (st != NULL) {
        ort_fail(st, err, errcap, "打开模型失败");
        release_all();
        return 0;
    }

    spec_table(specs);
    if (!check_signature(specs, err, errcap)) {
        release_all();
        return 0;
    }

    if ((st = G.api->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &G.mem)) != NULL) {
        ort_fail(st, err, errcap, "CreateCpuMemoryInfo 失败");
        release_all();
        return 0;
    }
    for (i = 0; i < SA_ONNX_NINPUTS; i++) {
        st = G.api->CreateTensorWithDataAsOrtValue(G.mem, specs[i].data, specs[i].bytes,
                                                   specs[i].dims, specs[i].ndim, specs[i].type,
                                                   &G.in[i]);
        if (st != NULL) {
            ort_fail(st, err, errcap, "建输入张量失败");
            release_all();
            return 0;
        }
    }
    {
        int64_t odims[1] = { SA_MODEL_ACTIONS };
        st = G.api->CreateTensorWithDataAsOrtValue(G.mem, G_logits, sizeof G_logits, odims, 1,
                                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &G.out);
        if (st != NULL) {
            ort_fail(st, err, errcap, "建输出张量失败");
            release_all();
            return 0;
        }
    }

    /* dummy Run：把 ORT 的 arena 与各算子的内部缓冲一次性预热出来，此后稳态不再分配（设计 D4）。
     * 输入此刻全零 —— 全零掩码是安全的（模型对「一行都没有」输出 0，不出 NaN）。 */
    G.open = 1;
    if (sa_onnx_run() == NULL) {
        if (err && errcap > 0) snprintf(err, (size_t)errcap, "%s", G_err);
        release_all();
        return 0;
    }
    return 1;
}

const float *sa_onnx_run(void)
{
    OrtStatus *st;
    if (!G.open) {
        snprintf(G_err, sizeof G_err, "会话未打开");
        return NULL;
    }
    st = G.api->Run(G.sess, NULL, IN_NAMES, (const OrtValue *const *)G.in, SA_ONNX_NINPUTS,
                    OUT_NAMES, 1, &G.out);
    if (st != NULL) {
        snprintf(G_err, sizeof G_err, "Run 失败：%.700s", G.api->GetErrorMessage(st));
        G.api->ReleaseStatus(st);
        return NULL;
    }
    return G_logits;
}

void sa_onnx_close(void) { release_all(); }
