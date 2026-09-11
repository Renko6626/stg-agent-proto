# stg-agent-proto v1

本文档是协议的权威定义。凡与源码不一致之处，以 `c/` 与 `src/stgagent/` 的实现为准，本文档
应随之更正——反过来不成立：不允许为了让文档好看而改代码语义。

## 0. 术语

- **后端**：观察的生产者。一个游戏 + 一种抽取方式，例如 `th06nc`、`th18.v1.00a`、`stg-engine`。
- **策略**：动作的生产者。`builtin`（后端自带 AI）/ `onnx:<file>`（本地跑模型）/ `ipc`（远端策略，
  见第 7 节）/ `manual`（人类操作，日志里仍会记录，见 `ACT.flags`）/ `mixed`（一次会话里换过策略）。
  `HELLO.policy` 只是**会话级标签**；一次会话中途可以换策略（例如玩家按键在手动与 AI 之间切换），
  所以**逐帧的真正出处以 `ACT.flags` 的 `HUMAN` 位为准**，消费者要按帧筛选就看它，不要按 `policy` 筛。
  后端只要有可能中途换策略，就该写 `mixed` 而不是开局那一种——写死一种会让人类数据被贴上算法标签。
- **消费者**：读 `.stglog` 日志或（未来）接 `ipc` 的一方，通常是训练/回放脚本。
- 全协议小端（little-endian）。
- 坐标系：x 以关卡区中轴为 0（左负右正），y 以顶边为 0、向下为正，单位 px。关卡区
  384×448（`field.half_w=192` 即半宽 192，`field.height=448`）。自机可动区（`move_area`）
  是这个区域的子集，随作品/角色不同。

## 1. 观察结构 `c/world.h`

`ap_world_t` 是与作品无关的「一帧世界快照」，由后端的抽取器填好后交给编码器。字段：

```
uint32_t frame;             // 帧号
uint32_t phase;             // 阶段位掩码，见下表
ap_player_t player;
ap_area_t  area;             // 本帧自机可动区（契约坐标系），float xmin,xmax,ymin,ymax
int nbullets;  ap_bullet_t bullets[AP_MAX_BULLETS];   // AP_MAX_BULLETS = 640
int nenemies;  ap_enemy_t  enemies[AP_MAX_ENEMIES];   // AP_MAX_ENEMIES = 256
int nlasers;   ap_laser_t  lasers[AP_MAX_LASERS];     // AP_MAX_LASERS  = 64
```

`ap_player_t`：`x,y`（位置）、`hit_radius`、`speed`/`speed_focus`（本帧合法步长，正交，
后者是低速/聚气速度）、`state`（原值透传；th06nc：0 ALIVE 1 SPAWNING 2 DEAD 3 INVULNERABLE）、
`focus`、`lives`、`bombs`、`life_frags`、`bomb_frags`、`power`、`score`、`graze`。

`ap_bullet_t`：`x,y`（位置）、`vx,vy`（每帧位移）、`radius`（判定半径，不含自机半径）、
`state`（原值透传；th06nc：1 = 飞行）、`grazed`、`collidable`（引擎本帧会不会拿它撞自机）、
`type`（弹种；不知道填 0）。**没有**单独存的 `speed`/`angle` 字段——这两个是编码时从
`vx,vy` 算出来的派生量，见第 3 节。

`ap_enemy_t`：`x,y`、`hit_w,hit_h`（致死盒半宽半高，是碰撞式里实际用的值，不是结构体原值）、
`hp,hp_max`、`boss`、`collidable`（引擎本帧会不会拿它撞自机）。**没有**单独的
`hurt_w`/`hurt_h`——编码时这两个 wire 字段直接照抄 `hit_w`/`hit_h`，见第 3 节。

`ap_laser_t`（直线激光：从 `(x,y)` 沿 `angle` 伸出，致死盒是沿线 `[start,end]`、半高
`half_h` 的旋转矩形；判定式：自机绕 `(x,y)` 旋转 `−angle` 进激光坐标系，钳到盒内，再比
自机半径；每帧 `end += speed`，`start = max(start, end − start_len)`）：
`x,y`（旋转原点）、`angle`（弧度，世界方向 `(cos,sin)`）、`start,end,start_len,speed,half_h`、
`omega`（绕 `(x,y)` 的角速度，弧度/帧；扫射激光 `omega ≠ 0`）、`vx,vy`（旋转原点的平移速度，
px/帧；跟着敌人走的激光 `(vx,vy) ≠ 0`；这两个量部分引擎的原结构体里没有，只能由抽取器跨帧
差分估计）、`t_active`（还要几帧才开始杀人：0 = 现在就杀，>0 = 预警中）、`state`。

`ap_area_t`：`xmin,xmax,ymin,ymax`（float，自机可动区，契约坐标系）。

**phase 位表**（`AP_PHASE_*`，bit 号）：

| bit | 名字 | 含义 |
| --- | --- | --- |
| 0 | `IN_GAME` | 在游戏内 |
| 1 | `PAUSED` | 暂停中 |
| 2 | `DIALOGUE` | 会话/过场中 |
| 3 | `SHOP` | 商店中 |
| 4 | `BOMB_ACTIVE` | 炸弹生效中 |
| 5 | `SPELL_ACTIVE` | 符卡生效中 |
| 6 | `PLAYER_CONTROLLABLE` | 自机本帧可操作 |
| 7 | `REPLAY_PLAYBACK` | 回放播放中 |

**动作位表**（`AP_BTN_*`，bit 号；0–6 与 stg-engine 的 `BTN_*` 冻结一致）：

| bit | 名字 |
| --- | --- |
| 0 | `UP` |
| 1 | `DOWN` |
| 2 | `LEFT` |
| 3 | `RIGHT` |
| 4 | `SHOT` |
| 5 | `BOMB` |
| 6 | `SLOW` |
| 7 | `TIMESTOP` |
| 8 | `CARD_USE` |
| 9 | `CARD_SWITCH` |

一个后端不必支持全部 10 个位；它在 HELLO 的 `actions` 里只声明自己真正用到的位
（见第 3 节），未声明的位消费者不应假定存在。

帧语义：一个 `ap_world_t` 是「上一帧的终态」还是「本帧执行中途」的快照，由 HELLO 的
`obs_timing` 字段声明（约定取值 `"prev-frame-final"` 或 `"mid-frame"`，解码器不校验
具体取值，只透传）。动作作用于本帧。一帧恰好产生一条 OBS + 一条 ACT（见第 6 节）。

## 2. 记录

所有消息（无论落盘还是走 socket）用同一种封帧：

```
u32 length     // 不含自身这 4 字节，即 1(type) + len(payload)
u8  type
<payload, length-1 字节>
```

`type` 取值：

| type | 名字 |
| --- | --- |
| 1 | HELLO |
| 2 | OBS |
| 3 | ACT |
| 4 | CTRL |
| 5 | BYE |

数值类型（HELLO 的 `number` 段与 `tables[].fields[].type` 用这些字符串具名）：

| 类型 | 编码 | 字节数 |
| --- | --- | --- |
| `fx` | Q16.16 定点，`i32` LE | 4 |
| `angle` | BAM，`u16` LE | 2 |
| `i32` | 有符号整数 LE | 4 |
| `u32` | 无符号整数 LE | 4 |
| `u16` | 无符号整数 LE | 2 |
| `u8` | 单字节 | 1 |

`fx`：`round(float * 65536)`，钳到 `i32` 范围（`INT32_MIN`/`INT32_MAX`）；还原
`float = i32 / 65536.0`。

`angle`（BAM，Binary Angular Measurement）：`round(rad * 65536 / 2π) & 0xFFFF`；
还原 `rad = u16 * 2π / 65536`。**`c/` 源码不得依赖 `M_PI`**——MSVC 的 `math.h` 在没有
提前 `#define _USE_MATH_DEFINES` 时不提供它，而本仓交叉编译到 Windows 的目标（见
`sa_encode.c` 顶部注释）恰好会踩中这一点。契约自己带一个 `SA_TWO_PI` 常量
（`6.283185307179586476925286766559`，2π 的最近 double，与 `2.0 * M_PI` 逐位相同），
不靠编译开关，也不给下游加负担。新写 C 后端照此办理，不要自己 `#include <math.h>`
里的 `M_PI`。

## 3. HELLO（JSON）

HELLO 的 payload 是一段 UTF-8 JSON，顶层键按以下顺序写出（`sa_hello_json` 是唯一生成
逻辑；消费者按名字取键，不应依赖顺序，但一致的顺序方便肉眼核对）：

```
proto, backend, policy, obs_timing, tick_hz,
field { half_w, height, origin, move_area { xmin, xmax, ymin, ymax } },
number { fx, angle },
actions [ { bit, name } ... ],
tables [ { id, name, cap, stride, fields [ { name, type, off } ... ] } ... ]
```

- `proto`：恒为 `1`（当前版本）。
- `backend` / `policy` / `obs_timing`：字符串，约定取值见第 0 节，解码器不校验取值本身，
  只要求字段存在。
- `field.half_w` / `field.height`：恒为 `192` / `448`（关卡区固定尺寸，与角色/难度无关）。
  `field.origin` 恒为字符串 `"center-top"`。
- `field.move_area`：自机可动区，来自 `sa_desc_t.area`（float），编码时**按 `(long)` 截断
  取整**（不是四舍五入）再写进 JSON——如果某个后端的可动区边界不是整数 px，HELLO 里看到
  的会比实际值略小（朝零截断）。
- `number.fx` / `number.angle`：恒为字符串 `"q16.16-i32-le"` / `"bam-u16-le"`，供消费者
  核对而非派生行为的字符串常量。
- `actions`：只列出该后端 `action_bits` 里实际置位的动作，按 bit 0→9 顺序；`name` 取自
  第 1 节的动作位表。
- `tables`：Tier 0 四张表固定都在，`spell` 等后端自定义表可选追加，追加不改变已存在表的
  `id`/字段。**规则：消费者按字段名字取值，不按顺序；三个后端（th06nc / th18 / stg-engine）
  的 Tier 0 表字段名与类型必须逐字相同**——新增字段允许，改名字/改类型不允许（否则触发
  第 8 节的 proto bump）。

### Tier 0 表（`c/sa_layout.h` + `c/sa_encode.c`，逐字段照抄）

**`player`**（`id=1`，`cap=1`，`stride=36`）：

| 字段 | 类型 | 偏移 |
| --- | --- | --- |
| `x` | `fx` | 0 |
| `y` | `fx` | 4 |
| `hit_radius` | `fx` | 8 |
| `speed` | `fx` | 12 |
| `speed_focus` | `fx` | 16 |
| `focus` | `u8` | 20 |
| `state` | `u8` | 21 |
| `lives` | `u8` | 22 |
| `bombs` | `u8` | 23 |
| `life_frags` | `u8` | 24 |
| `bomb_frags` | `u8` | 25 |
| `power` | `u16` | 26 |
| `score` | `u32` | 28 |
| `graze` | `u32` | 32 |

`player` 表的 `count` 恒为 1（单自机；编码器写死）。

**`bullets`**（`id=2`，`cap=640`，`stride=30`）：

| 字段 | 类型 | 偏移 |
| --- | --- | --- |
| `x` | `fx` | 0 |
| `y` | `fx` | 4 |
| `vx` | `fx` | 8 |
| `vy` | `fx` | 12 |
| `speed` | `fx` | 16 |
| `angle` | `angle` | 20 |
| `radius` | `fx` | 22 |
| `flags` | `u8` | 26 |
| `state` | `u8` | 27 |
| `type` | `u16` | 28 |

派生规则：`speed = hypot(vx, vy)`，`angle = bam(atan2(vy, vx))`——两者都是编码时算出来的，
`ap_bullet_t` 里没有独立存这两个量。`flags` 位组成：bit0 = `collidable`（引擎本帧会
不会拿它撞自机），bit1 = **圆形判定**，bit2 = `grazed`，其余位为 0。

bit1 在 v1 恒为 1：`ap_bullet_t` 只能表达圆判定，而 th06nc 的被弹判定本来就全是圆。
TH18 有非圆弹（引擎侧 `flags & 0x10`），接 TH18 时要先给 `ap_bullet_t` 加字段，
届时 bit1 才会出现 0。消费者**可以**依赖 bit1 的语义，但不要依赖它恒为 1。

**`enemies`**（`id=3`，`cap=256`，`stride=38`）：

| 字段 | 类型 | 偏移 |
| --- | --- | --- |
| `x` | `fx` | 0 |
| `y` | `fx` | 4 |
| `hurt_w` | `fx` | 8 |
| `hurt_h` | `fx` | 12 |
| `hit_w` | `fx` | 16 |
| `hit_h` | `fx` | 20 |
| `hp` | `i32` | 24 |
| `hp_max` | `i32` | 28 |
| `flags` | `u16` | 32 |
| `id` | `u32` | 34 |

派生规则：`hurt_w`/`hurt_h` 与 `hit_w`/`hit_h` **当前逐字节相同**——都直接来自
`ap_enemy_t.hit_w`/`hit_h`，编码器没有区分「视觉受击盒」与「判定盒」两套数值，四个字段
现在总是两两相等，不要指望它们分叉。`flags` 位组成：bit0 = `boss`，bit4（`0x10`）=
`collidable`，其余位为 0。

**`id` 是后端给的标识，要求在该敌人存活期间跨帧稳定**——消费者靠它追踪同一个敌人。
它直接透传 `ap_enemy_t.id`，编码器不代填。各后端的取值：th06nc = 敌池槽下标，
TH18 = 引擎的 `enemy_id`。槽会在敌人死后被复用，所以 `id` 只在一条生命周期内唯一，
不是全局永久句柄；要区分「同槽的前后两个敌人」，消费者需结合 `id` 断档与 `hp_max` 变化判断。

**`lasers`**（`id=4`，`cap=64`，`stride=48`）：

| 字段 | 类型 | 偏移 |
| --- | --- | --- |
| `x` | `fx` | 0 |
| `y` | `fx` | 4 |
| `angle` | `angle` | 8 |
| `start` | `fx` | 10 |
| `end` | `fx` | 14 |
| `start_len` | `fx` | 18 |
| `speed` | `fx` | 22 |
| `half_h` | `fx` | 26 |
| `omega` | `fx` | 30 |
| `vx` | `fx` | 34 |
| `vy` | `fx` | 38 |
| `t_active` | `i32` | 42 |
| `state` | `u8` | 46 |
| `type` | `u8` | 47 |

`type` 字段当前编码器恒写 0（`ap_laser_t` 没有 `type` 概念，占位保留）。

**`items`**（`id=5`，`cap=1024`，`stride=18`）——掉落物：

| 字段 | 类型 | 偏移 |
| --- | --- | --- |
| `x` | `fx` | 0 |
| `y` | `fx` | 4 |
| `vx` | `fx` | 8 |
| `vy` | `fx` | 12 |
| `kind` | `u8` | 16 |
| `flags` | `u8` | 17 |

`kind` 是**契约统一枚举**，不是后端原值——这是掉落物与 `bullets.type` 的关键区别：
弹种对策略没有可移植含义，掉落物种类有（「那是残机」在任何一作都同样重要），
所以由各作抽取器负责把原值映射过来，映射表写在各作的 engine 文档里。

| 值 | 含义 |
| --- | --- |
| 0 | 未知 |
| 1 | 小火力 |
| 2 | 点数 |
| 3 | 大火力 |
| 4 | 炸弹 |
| 5 | 满火力 |
| 6 | 残机 |
| 7 | 消弹产生的点数 |

`flags`：bit0 = 正朝自机飞来（自动回收中），bit1 = 生成动画中。生成动画期间引擎按两点插值移动，
不走常规重力，`vx`/`vy` 给的是折算出来的等效每帧位移，消费者照常外推即可。

`spell`：Tier 0 之外的可选表，v1 未定义具体布局；测试固件只验证了「HELLO 里没声明的表，
解码后就不出现在 `Observation.tables` 里」这一条（不是错误，是缺省）。

## 4. OBS（二进制）

payload 结构：

```
u32 frame
u32 phase
u8  table_count            // 当前实现恒为 4：Tier 0 四表逐一写头，即使某表 count=0
repeat table_count 次:
    u8  id                  // 对应 HELLO tables[].id
    u16 count               // 本帧该表的行数，count ≤ 对应表的 cap
    count × stride 字节      // stride 取自 HELLO 里该表的 stride，逐行紧密排列
```

写侧（`sa_obs_encode`）在 `nbullets/nenemies/nlasers/nitems` 越界（负数或超过
`AP_MAX_BULLETS/ENEMIES/LASERS`）或缓冲区不够大时返回 `-1`，不写任何字节；不做「截断到
cap」这种静默丢行为。

解码规则（`stgagent.obs.decode_obs`）：

- payload 长度不足 9 字节（header）即拒绝。
- 每张表的 3 字节表头（`id` + `count`）读不全即拒绝。
- `id` 不在 HELLO 声明的表里即拒绝（未知表 id 是错误，不是跳过）。
- `count > cap`（HELLO 里声明的该表 `cap`）即拒绝。
- `count × stride` 字节读不全即拒绝。
- **以上每种情况必须抛可捕获的错误；不能让 `struct.error` 泄漏出去**——Python 版对每种
  截断/越界情形都统一抛 `ValueError`（`struct.error` 不是 `ValueError` 的子类，会被
  `except ValueError` 漏网，这是训练侧批量读日志时会踩的真实的坑：进程崩溃留下的半截
  `.stglog`，末尾记录看起来「完整」但内部字段数比声明的少，必须能被同一种异常类型捞住）。
  新写解码器的语言若异常体系不同，也要保证「所有截断情况归一为一种可捕获类型」这条规则。

## 5. ACT（12 字节）

```
u32 frame
u32 buttons     // 位见第 1 节动作位表
u32 flags       // bit0 = PASSTHROUGH，bit1 = HUMAN，其余位保留（当前写 0）
```

`flags`：`PASSTHROUGH`（`0x1`）表示本条动作是后端自身逻辑直接放行的（策略未接管，例如
菜单/过场帧），`HUMAN`（`0x2`）表示这一帧是人类操作而非策略/回放产生的；两位互不排斥，
可以同时置位。解码侧长度不为 12 即拒绝（C 的 `sa_act_decode` 返回 `0`；Python 的
`decode_act` 抛 `ValueError`）。

## 6. 日志 `.stglog`

一份日志是若干条第 2 节记录首尾相接：**HELLO 一条（文件第一条，写在 `sa_log_open` 时）
→ 每帧一对 OBS + ACT（同一 `frame`，`sa_log_frame` 里先写 OBS 后写 ACT）→ BYE 收尾**
（`sa_log_close` 时写，payload 为空；进程异常退出时可以没有 BYE，文件就停在最后一条完整
记录处）。写侧每 60 帧 `fflush` 一次，不是每帧落盘。

读取器（`stgagent.log.read_log` + `framing.iter_records`）分两层容错：

1. **封帧层**：一条记录如果连 `length` 字段都不全，或声明的 `length` 超出文件剩余字节，
   直接停止读取（`return`，不抛异常）——这是应对「进程崩溃，最后一条记录写了一半」的
   正常路径，不是错误。
2. **载荷层**：一条记录长度本身是完整的，但其内部结构（OBS 的表头/行数、ACT 的 12 字节）
   与自己声明的不符，则抛 `ValueError`（见第 4、5 节）——这种情况视为日志损坏，不是正常
   截断。

配对规则：第一条记录必须是 HELLO，否则 `read_log` 直接抛 `ValueError`（`"log must start
with HELLO"`）。此后遇到 OBS 就新开一帧；遇到 ACT，只有当**最后一帧还没配到 ACT 且
`ACT.frame == 该帧 OBS.frame`** 时才把它挂到那一帧，否则这条 ACT 被静默丢弃（既不报错也不
挂到别的帧）。遇到 BYE 立即停止读取，BYE 之后即使还有记录也不再处理。

## 7. ipc（本机锁步，v1 未实现，规则先定）

`ipc` 策略走本机 IPC 做实时锁步交互；v1 只把规则定下来，`c/`、`src/stgagent/` 均未实现
编解码或传输，`SA_MSG_CTRL` 目前只是一个保留的 type 值，没有产出/消费它的代码。规则：

- 引擎发出 `OBS(n)` 后等待对应的 `ACT(n)`；超时则沿用上一条已生效的动作继续跑。
- `timeout_ms = 0` 表示不等：取当前已收到的最新一条 ACT，不比对帧号。
- 若收到的 ACT 帧号大于当前等待的 `n`（`frame > n`），视为协议错误，断开连接。
- 等待一条记录跨多次读取拼完整（分片到达）时，单条记录的拼接等待上限 100 ms。
- `CTRL` 消息用于运行时协商，字段：`timeout_ms`（上面的超时）、`passthrough`（是否强制
  `ACT.flags` 的 `PASSTHROUGH`）、`tables`（本次会话只要哪些表，减少带宽）。具体 payload
  编码留待实现时再定，本节只锁定语义。

## 8. 版本

`proto` 是 HELLO 里的一个字段，当前恒为 `1`。新增表、新增字段（挂在已有 stride 之后，
不影响已有偏移）**不需要**升版本号——消费者按名字取字段，天然向后兼容。**修改已有字段的
偏移、类型、单位或含义**（哪怕字段名不变）**必须**升 `proto`,让旧消费者能一眼看出协议
变了而不是默默读错数据。
