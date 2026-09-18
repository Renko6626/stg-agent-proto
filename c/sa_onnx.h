/* sa_onnx.h —— `onnx` 策略后端的 ORT 管管：建会话、静态张量、跑一帧。
 *
 * 只依赖 C 标准库 + 平台的动态加载（`LoadLibraryW` / `dlopen`）。**不链接 ORT 的 import lib**：
 * 运行时解析 `OrtGetApiBase` 即可，构建期只需要 `vendor/onnxruntime_c_api.h`。这样交叉编译到
 * Windows 不用加任何链接参数，而且 —— 更要紧的 —— 可以在**首帧惰性**初始化：本 DLL 是游戏静态
 * 导入表里的 CRT 代理，`DllMain` 里 `LoadLibrary` 会撞 loader lock。
 *
 * 分配纪律：会话与全部 `OrtValue` 都建在静态缓冲上，`sa_onnx_open` 末尾跑一次 dummy `Run`
 * 预热 arena，之后**稳态零分配**。（原「钩子内零堆分配」放宽为此，见设计 D4。）
 *
 * 用法：
 *
 *     if (!sa_onnx_is_open() && !sa_onnx_open(path, err, sizeof err)) { 记 err，退回 builtin; }
 *     sa_model_fill(&world, ax, ay, prev, sa_onnx_inputs());
 *     const float *logits = sa_onnx_run();
 *     int id = sa_model_pick(logits, prev, tau);
 *
 * 设计：renkolab `docs/superpowers/specs/2026-09-18-th06nc-onnx-policy-design.md` §6。 */
#ifndef SA_ONNX_H
#define SA_ONNX_H
#include "sa_model.h"

/* ORT 动态库的默认名字。Windows 上就放在游戏目录里，随发布包一起拖过去。 */
#ifdef _WIN32
#define SA_ONNX_DEFAULT_LIB "onnxruntime.dll"
#else
#define SA_ONNX_DEFAULT_LIB "libonnxruntime.so"
#endif

/* **路径编码契约：本文件的所有 `const char *` 路径都是 UTF-8。**
 *
 * Windows 侧用 `CP_UTF8` 转宽字符再交给 ORT。喂 ANSI（`CP_ACP`）进来的话，非 ASCII 字节会被
 * **静默**替换成 U+FFFD，拼出一条不存在的路径，而 ORT 只会说「File doesn't exist」——
 * 2026-09-18 实机就是这么失败的（中文游戏目录 + `GetModuleFileNameA`），错误信息把排错方向
 * 指反了大半天。所以 `sa_onnx_open` 会先验一遍编码再动手，见 `sa_onnx_path_is_utf8`。
 *
 * 调用方在 Windows 上的正确做法：`GetModuleFileNameW` / `GetPrivateProfileStringW` 取宽字符，
 * 再 `WideCharToMultiByte(CP_UTF8, ...)`。**不要**用 `-A` 版本的 Win32 API 拼路径。
 * POSIX 上路径原样透传，UTF-8 本来就是常态。 */

/* `path` 是不是合法 UTF-8（严格：拒 overlong、代理区、> U+10FFFF、孤立续字节）。
 * 纯 ASCII 恒真。空串恒真。宿主可测，负例就是 GBK 中文那类字节。 */
int sa_onnx_path_is_utf8(const char *path);

/* 覆盖要加载的 ORT 动态库路径（测试与排错用）。`NULL` 恢复默认。只在 `sa_onnx_open` 之前有效。 */
void sa_onnx_set_library(const char *path);

/* 建会话。`model_path` 是 `.onnx` 文件；失败返回 0 并把可读原因写进 `err`（保证 NUL 结尾）。
 * 会**逐项校验图的输入签名**（7 个输入的名字、元素类型、形状）与 `sa_model.h` 的常量相符 ——
 * 装错版本的图要在这里就响亮失败，而不是每帧算出一堆没意义的 logits。 */
int sa_onnx_open(const char *model_path, char *err, int errcap);

int sa_onnx_is_open(void);

/* 静态输入缓冲。`OrtValue` 已经指向它，所以**原地填**、不要换指针。 */
sa_model_in_t *sa_onnx_inputs(void);

/* 跑一帧，返回 `SA_MODEL_ACTIONS` 个 logits（静态缓冲，下一帧覆写）。失败返回 `NULL`。 */
const float *sa_onnx_run(void);

/* 最近一次失败的原因（`sa_onnx_run` 返回 NULL 时用它记日志）。没失败过则为空串。 */
const char *sa_onnx_last_error(void);

void sa_onnx_close(void);

#endif
