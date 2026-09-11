/* sa_layout.h —— v1 表布局。HELLO 从这里生成，编码器按这里写；两者不许各写一份。 */
#ifndef SA_LAYOUT_H
#define SA_LAYOUT_H
#include <stdint.h>

typedef struct { const char *name; const char *type; uint16_t off; } sa_field_t;
typedef struct { uint8_t id; const char *name; uint16_t cap; uint16_t stride;
                 const sa_field_t *fields; int nfields; } sa_table_t;

enum { SA_T_PLAYER = 1, SA_T_BULLETS = 2, SA_T_ENEMIES = 3, SA_T_LASERS = 4, SA_T_ITEMS = 5 };
#define SA_PLAYER_STRIDE 36
#define SA_BULLET_STRIDE 30
#define SA_ENEMY_STRIDE  38
#define SA_LASER_STRIDE  48
#define SA_ITEM_STRIDE   18

/* 字段偏移（编码器用的具名常量，与下面的表逐一对应） */
enum { P_X = 0, P_Y = 4, P_HIT_R = 8, P_SPEED = 12, P_SPEED_F = 16, P_FOCUS = 20, P_STATE = 21, P_LIVES = 22,
       P_BOMBS = 23, P_LFRAG = 24, P_BFRAG = 25, P_POWER = 26, P_SCORE = 28, P_GRAZE = 32 };
enum { B_X = 0, B_Y = 4, B_VX = 8, B_VY = 12, B_SPEED = 16, B_ANGLE = 20, B_RADIUS = 22, B_FLAGS = 26, B_STATE = 27, B_TYPE = 28 };
enum { E_X = 0, E_Y = 4, E_HURT_W = 8, E_HURT_H = 12, E_HIT_W = 16, E_HIT_H = 20, E_HP = 24, E_HP_MAX = 28, E_FLAGS = 32, E_ID = 34 };
enum { L_X = 0, L_Y = 4, L_ANGLE = 8, L_START = 10, L_END = 14, L_START_LEN = 18, L_SPEED = 22, L_HALF_H = 26,
       L_OMEGA = 30, L_VX = 34, L_VY = 38, L_T_ACTIVE = 42, L_STATE = 46, L_TYPE = 47 };

enum { I_X = 0, I_Y = 4, I_VX = 8, I_VY = 12, I_KIND = 16, I_FLAGS = 17 };

extern const sa_table_t SA_TABLES[5];   /* player, bullets, enemies, lasers, items */
#endif
