# stg-agent-proto

## 这是什么

`stg-agent-proto` 是东方 Project 系列引擎与 AI 智能体之间的观察/动作契约：一份与具体作品
无关的协议规范。契约本体是 `c/world.h`（观察结构 `ap_world_t` 及其子结构、常量）与
仓库根的 `SPEC.md`（协议正文，权威定义）。多个游戏（th06nc、…）、三种策略后端共用同一份
契约，各自实现自己的抽取器/编码器，但都对着这一份结构与常量编译、解码。

## 怎么用

- **游戏侧（C/DLL）**：把 `c/` 加进 include 路径，`#include "world.h"`，把游戏内存填进
  `ap_world_t`，交给编码器写出协议字节。
- **训练侧（Python）**：`python3 -m pip install -e '.[dev]'` 装好 `stgagent` 包后（用
  `python3 -m pip` 而不是 `pip`：两者可能是不同解释器），用
  `stgagent.consts` 里的常量对照协议，或用 `stgagent.log.read_log(path)` 读取
  `.stglog` 日志文件做解码/回放。

## 怎么测

- Python 侧：`pytest`。
- C 侧：`make -C c test-host`（宿主编译自检，不依赖具体游戏）。
