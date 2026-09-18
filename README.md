# stg-agent-proto

## 这是什么

`stg-agent-proto` 是东方 Project 系列引擎与 AI 智能体之间的观察/动作契约：一份与具体作品
无关的协议规范。契约本体是 `c/world.h`（观察结构 `ap_world_t` 及其子结构、常量）与
仓库根的 `SPEC.md`（协议正文，权威定义）。多个游戏（th06nc、…）、三种策略后端共用同一份
契约，各自实现自己的抽取器/编码器，但都对着这一份结构与常量编译、解码。

**`c/sa_model.*` 与 `c/sa_onnx.*` 不是契约本体**，是搭在契约上的 `onnx` 策略后端：前者把
`ap_world_t` 摊成图的输入数组、把 logits 变回动作位；后者是 ORT 管管（惰性加载、静态张量、
签名校验）。两者都与作品无关（th06nc 与 TH18 共用），但与**图版本**强绑定 —— 图由训练仓
`stg-rl-train` 的 `stgtrain.export_onnx` 导出。改协议要看 `SPEC.md`，改模型接口看 renkolab 的
`docs/superpowers/specs/2026-09-18-th06nc-onnx-policy-design.md`。
`c/vendor/` 是逐字照抄的第三方头文件（ORT 的 C API），见那里的 README。

## 怎么用

- **游戏侧（C/DLL）**：把 `c/` 加进 include 路径，`#include "world.h"`，把游戏内存填进
  `ap_world_t`，交给编码器写出协议字节。
- **训练侧（Python）**：`python3 -m pip install -e '.[dev]'` 装好 `stgagent` 包后（用
  `python3 -m pip` 而不是 `pip`：两者可能是不同解释器），用
  `stgagent.consts` 里的常量对照协议，或用 `stgagent.log.read_log(path)` 读取
  `.stglog` 日志文件做解码/回放。

## 怎么测

- Python 侧：`pytest`。
- C 侧：`make -C c test-host`（宿主编译自检，不依赖具体游戏）。接真 ORT 的那几条另开：
  `make -C c test-onnx-live SA_ONNX_TEST_LIB=… SA_ONNX_TEST_MODEL=…`（库可以直接用 pip 装的
  onnxruntime 包里的 `site-packages/onnxruntime/capi/libonnxruntime.so.*`）。
