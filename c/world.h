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
} ap_enemy_t;

/* 直线激光：从 (x,y) 沿 angle 伸出，致死盒在沿线 [start,end]、半高 half_h 的旋转矩形。
 * 每帧 end += speed，start = max(start, end − start_len)。 */
typedef struct {
    float x, y;
    float angle;           /* 弧度 */
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
} ap_world_t;

#define AP_PHASE_IN_GAME             (1u << 0)
#define AP_PHASE_PAUSED              (1u << 1)
#define AP_PHASE_DIALOGUE            (1u << 2)
#define AP_PHASE_SHOP                (1u << 3)
#define AP_PHASE_BOMB_ACTIVE         (1u << 4)
#define AP_PHASE_SPELL_ACTIVE        (1u << 5)
#define AP_PHASE_PLAYER_CONTROLLABLE (1u << 6)
#define AP_PHASE_REPLAY_PLAYBACK     (1u << 7)

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
