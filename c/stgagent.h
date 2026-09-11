/* stgagent.h —— stg-agent-proto v1 的 C 侧公开 API。
 * 与加载方式无关、与作品无关、与位宽无关；只依赖 C 标准库。静态缓冲，钩子内零堆分配。 */
#ifndef STGAGENT_H
#define STGAGENT_H
#include <stdint.h>
#include "world.h"
#include "sa_layout.h"

#define SA_PROTO_VERSION 1
enum { SA_MSG_HELLO = 1, SA_MSG_OBS = 2, SA_MSG_ACT = 3, SA_MSG_CTRL = 4, SA_MSG_BYE = 5 };
#define SA_ACT_PASSTHROUGH 1u
#define SA_ACT_HUMAN       2u

/* 一帧 OBS 的最大字节数：头 9 + 每表 (3 + cap×stride) */
#define SA_OBS_CAP (9 + (3 + SA_PLAYER_STRIDE) + (3 + AP_MAX_BULLETS * SA_BULLET_STRIDE) \
                      + (3 + AP_MAX_ENEMIES * SA_ENEMY_STRIDE) + (3 + AP_MAX_LASERS * SA_LASER_STRIDE) \
                      + (3 + AP_MAX_ITEMS * SA_ITEM_STRIDE))

typedef struct {
    const char *backend;      /* "th06nc" / "th18.v1.00a" / … */
    const char *policy;       /* "builtin" / "onnx:<file>" / "ipc" / "manual" */
    const char *obs_timing;   /* "prev-frame-final" / "mid-frame" */
    uint16_t    tick_hz;
    uint32_t    action_bits;  /* 本作支持的动作位掩码 */
    ap_area_t   area;         /* 自机可动区 */
} sa_desc_t;

typedef struct { uint32_t frame, buttons, flags; } sa_act_t;

int32_t  sa_fx(float v);          /* Q16.16，rint 后钳 i32 */
uint16_t sa_bam(float rad);       /* 弧度 → BAM u16（回绕） */

int  sa_hello_json(const sa_desc_t *d, char *out, int cap);          /* 字节数（不含 NUL）；溢出 −1 */
int  sa_obs_encode(const ap_world_t *w, uint8_t *out, int cap);      /* 字节数；放不下 −1 */
void sa_act_encode(uint32_t frame, uint32_t buttons, uint32_t flags, uint8_t out[12]);
int  sa_act_decode(const uint8_t *p, int n, sa_act_t *out);          /* 1 ok / 0 bad */
int  sa_record(uint8_t type, const uint8_t *payload, int len, uint8_t *out, int cap); /* 封帧；总长或 −1 */

/* ---- 日志（sa_log.c）：HELLO 一条，然后每帧 OBS + ACT，BYE 收尾 ---- */
int  sa_log_open(const char *path, const sa_desc_t *d);              /* 1 ok / 0 fail */
int  sa_log_is_open(void);
void sa_log_frame(const ap_world_t *w, uint32_t buttons, uint32_t flags);
void sa_log_close(void);
#endif
