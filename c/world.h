/* world.h —— stg-agent-proto v1 的观察结构：与作品无关的「一帧世界快照」。
 * 抽取器把游戏内存填进来；策略后端只看它；编码器把它变成 OBS 字节。
 * 坐标：x 以关卡区中轴为 0（左负右正），y 以顶边为 0 向下为正，单位 px。关卡区 384×448。 */
#ifndef AP_WORLD_H
#define AP_WORLD_H
#include <stdint.h>

#ifndef AP_MAX_BULLETS
#define AP_MAX_BULLETS 640
#endif
#ifndef AP_MAX_ENEMIES
#define AP_MAX_ENEMIES 256
#endif
#ifndef AP_MAX_LASERS
#define AP_MAX_LASERS  64
#endif
#ifndef AP_MAX_ITEMS
#define AP_MAX_ITEMS   1024
#endif

typedef struct {
    float x, y;            /* 位置 */
    float vx, vy;          /* 每帧位移 */
    float radius;          /* 判定半径（不含自机半径） */
    uint8_t state;         /* 原值透传（th06nc：1 飞行） */
    uint8_t grazed;
    uint8_t collidable;    /* 引擎本帧会不会拿它撞自机 */
    uint16_t type;         /* 弹种；不知道填 0 */
} ap_bullet_t;

typedef struct {
    float x, y;
    float hit_w, hit_h;    /* 致死盒半宽半高（碰撞式里实际用的值，不是结构体原值） */
    int32_t hp, hp_max;
    uint8_t boss;
    uint8_t collidable;    /* 引擎本帧会不会拿它撞自机 */
    /* 后端给的标识，要求在该敌人**存活期间跨帧稳定**——消费者靠它追踪同一个敌人。
     * th06nc = 敌池槽下标；TH18 = 引擎的 enemy_id。抽取器必须填，不能留 0 让编码器编行号：
     * 行号下一帧就对应到别的敌人，是个会让跨帧追踪静默出错的陷阱。 */
    uint32_t id;
} ap_enemy_t;

/* 直线激光：从 (x,y) 沿 angle 伸出，致死盒是沿线 [start,end]、半高 half_h 的旋转矩形。
 * 判定式（消费者照此实现）：自机绕 (x,y) 旋转 −angle 进激光坐标系，钳到盒内，再比自机半径。
 * 每帧 end += speed，start = max(start, end − start_len)。 */
typedef struct {
    float x, y;            /* 旋转原点 */
    float angle;           /* 弧度，世界方向 (cos, sin) */
    float start, end;
    float start_len;
    float speed;
    float half_h;
    /* 整条激光自己的运动。th06nc 的 Laser 结构体里**没有**这两个量：引擎的激光 tick 只推进
     * endOffset，转动与平移全部由 ECL 逐帧改写（opcode 88 LASERROTATE：angle += arg；
     * opcode 90 LASEROFFSET：pos = enemy->position + arg）。所以只能由抽取器跨帧差分估计。
     * 扫射激光 = omega ≠ 0；跟着敌人走的激光 = (vx,vy) ≠ 0。 */
    float omega;           /* 绕 (x,y) 的角速度，弧度/帧 */
    float vx, vy;          /* 旋转原点的平移速度，px/帧 */
    int32_t t_active;      /* 还要几帧才开始杀人：0 = 现在就杀；>0 = 预警中 */
    uint8_t state;
} ap_laser_t;

typedef struct {
    float x, y;
    float hit_radius;
    float speed, speed_focus;   /* 本帧合法步长（正交），低速版 */
    uint8_t state;         /* 原值透传（th06nc：0 ALIVE 1 SPAWNING 2 DEAD 3 INVULNERABLE） */
    uint8_t focus;
    uint8_t lives, bombs;
    uint8_t life_frags, bomb_frags;
    uint16_t power;
    uint32_t score, graze;
} ap_player_t;

/* 掉落物。kind 是**契约统一枚举**（AP_ITEM_*），不是各作原值——各作抽取器负责映射，
 * 这样同一个模型换作品也认得「这是残机」。原值与映射表写在各作的 engine 文档里。 */
typedef struct {
    float x, y;
    float vx, vy;          /* 每帧位移。生成动画期间是按插值推出的等效速度，照样可外推 */
    uint8_t kind;          /* AP_ITEM_* */
    uint8_t homing;        /* 正朝自机飞来（越过自动回收线后全场道具都会这样） */
    uint8_t spawning;      /* 生成动画中：位置在两点间插值，不走常规重力 */
} ap_item_t;

typedef struct { float xmin, xmax, ymin, ymax; } ap_area_t;   /* 自机可动区（契约坐标系） */

typedef struct {
    uint32_t frame;
    uint32_t phase;
    ap_player_t player;
    ap_area_t area;
    int nbullets;
    ap_bullet_t bullets[AP_MAX_BULLETS];
    int nenemies;
    ap_enemy_t enemies[AP_MAX_ENEMIES];
    int nlasers;
    ap_laser_t lasers[AP_MAX_LASERS];
    int nitems;
    ap_item_t items[AP_MAX_ITEMS];
} ap_world_t;

#define AP_PHASE_IN_GAME             (1u << 0)
#define AP_PHASE_PAUSED              (1u << 1)
#define AP_PHASE_DIALOGUE            (1u << 2)
#define AP_PHASE_SHOP                (1u << 3)
#define AP_PHASE_BOMB_ACTIVE         (1u << 4)
#define AP_PHASE_SPELL_ACTIVE        (1u << 5)
#define AP_PHASE_PLAYER_CONTROLLABLE (1u << 6)
#define AP_PHASE_REPLAY_PLAYBACK     (1u << 7)

/* 掉落物种类（契约统一枚举）。各作原值 → 这里的映射由抽取器做。 */
#define AP_ITEM_UNKNOWN     0
#define AP_ITEM_POWER       1   /* 小火力 */
#define AP_ITEM_POINT       2   /* 点数 */
#define AP_ITEM_BIG_POWER   3   /* 大火力 */
#define AP_ITEM_BOMB        4   /* 炸弹 */
#define AP_ITEM_FULL_POWER  5   /* 满火力 */
#define AP_ITEM_LIFE        6   /* 残机 */
#define AP_ITEM_CANCEL      7   /* 消弹产生的点数 */
#define AP_ITEM_LIFE_PIECE  8   /* 残机碎片 */
#define AP_ITEM_BOMB_PIECE  9   /* 炸弹碎片 */

/* 动作位（bit 号）。0–6 与 stg-engine 的 BTN_* 冻结一致。 */
#define AP_BTN_UP          (1u << 0)
#define AP_BTN_DOWN        (1u << 1)
#define AP_BTN_LEFT        (1u << 2)
#define AP_BTN_RIGHT       (1u << 3)
#define AP_BTN_SHOT        (1u << 4)
#define AP_BTN_BOMB        (1u << 5)
#define AP_BTN_SLOW        (1u << 6)
#define AP_BTN_TIMESTOP    (1u << 7)
#define AP_BTN_CARD_USE    (1u << 8)
#define AP_BTN_CARD_SWITCH (1u << 9)

#endif
