#include <math.h>
#include <stdio.h>
#include <string.h>
#include "stgagent.h"

static const sa_field_t PF[] = {
    {"x","fx",P_X},{"y","fx",P_Y},{"hit_radius","fx",P_HIT_R},{"speed","fx",P_SPEED},{"speed_focus","fx",P_SPEED_F},
    {"focus","u8",P_FOCUS},{"state","u8",P_STATE},{"lives","u8",P_LIVES},{"bombs","u8",P_BOMBS},
    {"life_frags","u8",P_LFRAG},{"bomb_frags","u8",P_BFRAG},{"power","u16",P_POWER},{"score","u32",P_SCORE},{"graze","u32",P_GRAZE} };
static const sa_field_t BF[] = {
    {"x","fx",B_X},{"y","fx",B_Y},{"vx","fx",B_VX},{"vy","fx",B_VY},{"speed","fx",B_SPEED},{"angle","angle",B_ANGLE},
    {"radius","fx",B_RADIUS},{"flags","u8",B_FLAGS},{"state","u8",B_STATE},{"type","u16",B_TYPE} };
static const sa_field_t EF[] = {
    {"x","fx",E_X},{"y","fx",E_Y},{"hurt_w","fx",E_HURT_W},{"hurt_h","fx",E_HURT_H},{"hit_w","fx",E_HIT_W},{"hit_h","fx",E_HIT_H},
    {"hp","i32",E_HP},{"hp_max","i32",E_HP_MAX},{"flags","u16",E_FLAGS},{"id","u32",E_ID} };
static const sa_field_t LF[] = {
    {"x","fx",L_X},{"y","fx",L_Y},{"angle","angle",L_ANGLE},{"start","fx",L_START},{"end","fx",L_END},{"start_len","fx",L_START_LEN},
    {"speed","fx",L_SPEED},{"half_h","fx",L_HALF_H},{"omega","fx",L_OMEGA},{"vx","fx",L_VX},{"vy","fx",L_VY},
    {"t_active","i32",L_T_ACTIVE},{"state","u8",L_STATE},{"type","u8",L_TYPE} };

const sa_table_t SA_TABLES[4] = {
    { SA_T_PLAYER,  "player",  1,              SA_PLAYER_STRIDE, PF, (int)(sizeof PF / sizeof PF[0]) },
    { SA_T_BULLETS, "bullets", AP_MAX_BULLETS, SA_BULLET_STRIDE, BF, (int)(sizeof BF / sizeof BF[0]) },
    { SA_T_ENEMIES, "enemies", AP_MAX_ENEMIES, SA_ENEMY_STRIDE,  EF, (int)(sizeof EF / sizeof EF[0]) },
    { SA_T_LASERS,  "lasers",  AP_MAX_LASERS,  SA_LASER_STRIDE,  LF, (int)(sizeof LF / sizeof LF[0]) },
};

static const char *ACTION_NAMES[10] = { "UP","DOWN","LEFT","RIGHT","SHOT","BOMB","SLOW","TIMESTOP","CARD_USE","CARD_SWITCH" };

int32_t sa_fx(float v)
{
    double d = rint((double)v * 65536.0);
    if (d > 2147483647.0) return INT32_MAX;
    if (d < -2147483648.0) return INT32_MIN;
    return (int32_t)d;
}
uint16_t sa_bam(float rad)
{
    long long i = (long long)rint((double)rad * (65536.0 / (2.0 * M_PI)));
    return (uint16_t)(i & 0xFFFF);
}

static void le32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void le16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put_fx(uint8_t *r, int o, float v)  { le32(r + o, (uint32_t)sa_fx(v)); }

/* ---- HELLO ---- */
static int cat(char *out, int cap, int n, const char *s)
{
    int l = (int)strlen(s);
    if (n < 0 || n + l >= cap) return -1;
    memcpy(out + n, s, (size_t)l + 1);
    return n + l;
}
static int catl(char *out, int cap, int n, const char *fmt, long v)
{
    char tmp[32];
    snprintf(tmp, sizeof tmp, fmt, v);
    return cat(out, cap, n, tmp);
}
int sa_hello_json(const sa_desc_t *d, char *out, int cap)
{
    int n = 0, i, j, first;
    n = cat(out, cap, n, "{\"proto\":1,\"backend\":\""); n = cat(out, cap, n, d->backend);
    n = cat(out, cap, n, "\",\"policy\":\"");            n = cat(out, cap, n, d->policy);
    n = cat(out, cap, n, "\",\"obs_timing\":\"");        n = cat(out, cap, n, d->obs_timing);
    n = catl(out, cap, n, "\",\"tick_hz\":%ld,", d->tick_hz);
    n = cat(out, cap, n, "\"field\":{\"half_w\":192,\"height\":448,\"origin\":\"center-top\",\"move_area\":{");
    n = catl(out, cap, n, "\"xmin\":%ld,", (long)d->area.xmin); n = catl(out, cap, n, "\"xmax\":%ld,", (long)d->area.xmax);
    n = catl(out, cap, n, "\"ymin\":%ld,", (long)d->area.ymin); n = catl(out, cap, n, "\"ymax\":%ld}},", (long)d->area.ymax);
    n = cat(out, cap, n, "\"number\":{\"fx\":\"q16.16-i32-le\",\"angle\":\"bam-u16-le\"},\"actions\":[");
    for (i = 0, first = 1; i < 10; i++) {
        if (!(d->action_bits & (1u << i))) continue;
        n = cat(out, cap, n, first ? "" : ","); first = 0;
        n = catl(out, cap, n, "{\"bit\":%ld,\"name\":\"", i); n = cat(out, cap, n, ACTION_NAMES[i]); n = cat(out, cap, n, "\"}");
    }
    n = cat(out, cap, n, "],\"tables\":[");
    for (i = 0; i < 4; i++) {
        const sa_table_t *t = &SA_TABLES[i];
        n = cat(out, cap, n, i ? "," : "");
        n = catl(out, cap, n, "{\"id\":%ld,\"name\":\"", t->id); n = cat(out, cap, n, t->name);
        n = catl(out, cap, n, "\",\"cap\":%ld,", t->cap);        n = catl(out, cap, n, "\"stride\":%ld,\"fields\":[", t->stride);
        for (j = 0; j < t->nfields; j++) {
            n = cat(out, cap, n, j ? ",{\"name\":\"" : "{\"name\":\""); n = cat(out, cap, n, t->fields[j].name);
            n = cat(out, cap, n, "\",\"type\":\"");                     n = cat(out, cap, n, t->fields[j].type);
            n = catl(out, cap, n, "\",\"off\":%ld}", t->fields[j].off);
        }
        n = cat(out, cap, n, "]}");
    }
    return cat(out, cap, n, "]}");
}

/* ---- OBS ---- */
static uint8_t *table_hdr(uint8_t *p, uint8_t id, int count) { p[0] = id; le16(p + 1, (uint16_t)count); return p + 3; }

int sa_obs_encode(const ap_world_t *w, uint8_t *out, int cap)
{
    int i, need = 9 + (3 + SA_PLAYER_STRIDE) + (3 + w->nbullets * SA_BULLET_STRIDE)
                    + (3 + w->nenemies * SA_ENEMY_STRIDE) + (3 + w->nlasers * SA_LASER_STRIDE);
    uint8_t *p = out, *r;
    if (need > cap || w->nbullets > AP_MAX_BULLETS || w->nenemies > AP_MAX_ENEMIES || w->nlasers > AP_MAX_LASERS) return -1;
    memset(out, 0, (size_t)need);
    le32(p, w->frame); le32(p + 4, w->phase); p[8] = 4; p += 9;

    r = table_hdr(p, SA_T_PLAYER, 1);
    put_fx(r, P_X, w->player.x); put_fx(r, P_Y, w->player.y); put_fx(r, P_HIT_R, w->player.hit_radius);
    put_fx(r, P_SPEED, w->player.speed); put_fx(r, P_SPEED_F, w->player.speed_focus);
    r[P_FOCUS] = w->player.focus; r[P_STATE] = w->player.state; r[P_LIVES] = w->player.lives; r[P_BOMBS] = w->player.bombs;
    r[P_LFRAG] = w->player.life_frags; r[P_BFRAG] = w->player.bomb_frags;
    le16(r + P_POWER, w->player.power); le32(r + P_SCORE, w->player.score); le32(r + P_GRAZE, w->player.graze);
    p = r + SA_PLAYER_STRIDE;

    r = table_hdr(p, SA_T_BULLETS, w->nbullets);
    for (i = 0; i < w->nbullets; i++, r += SA_BULLET_STRIDE) {
        const ap_bullet_t *b = &w->bullets[i];
        put_fx(r, B_X, b->x); put_fx(r, B_Y, b->y); put_fx(r, B_VX, b->vx); put_fx(r, B_VY, b->vy);
        put_fx(r, B_SPEED, hypotf(b->vx, b->vy)); le16(r + B_ANGLE, sa_bam(atan2f(b->vy, b->vx)));
        put_fx(r, B_RADIUS, b->radius);
        r[B_FLAGS] = (uint8_t)((b->collidable ? 1 : 0) | 2 | (b->grazed ? 4 : 0));
        r[B_STATE] = b->state; le16(r + B_TYPE, b->type);
    }
    p = r;

    r = table_hdr(p, SA_T_ENEMIES, w->nenemies);
    for (i = 0; i < w->nenemies; i++, r += SA_ENEMY_STRIDE) {
        const ap_enemy_t *e = &w->enemies[i];
        put_fx(r, E_X, e->x); put_fx(r, E_Y, e->y);
        put_fx(r, E_HURT_W, e->hit_w); put_fx(r, E_HURT_H, e->hit_h); put_fx(r, E_HIT_W, e->hit_w); put_fx(r, E_HIT_H, e->hit_h);
        le32(r + E_HP, (uint32_t)e->hp); le32(r + E_HP_MAX, (uint32_t)e->hp_max);
        le16(r + E_FLAGS, (uint16_t)((e->boss ? 1 : 0) | (e->collidable ? 0x10 : 0)));
        le32(r + E_ID, (uint32_t)i);
    }
    p = r;

    r = table_hdr(p, SA_T_LASERS, w->nlasers);
    for (i = 0; i < w->nlasers; i++, r += SA_LASER_STRIDE) {
        const ap_laser_t *l = &w->lasers[i];
        put_fx(r, L_X, l->x); put_fx(r, L_Y, l->y); le16(r + L_ANGLE, sa_bam(l->angle));
        put_fx(r, L_START, l->start); put_fx(r, L_END, l->end); put_fx(r, L_START_LEN, l->start_len);
        put_fx(r, L_SPEED, l->speed); put_fx(r, L_HALF_H, l->half_h);
        put_fx(r, L_OMEGA, l->omega); put_fx(r, L_VX, l->vx); put_fx(r, L_VY, l->vy);
        le32(r + L_T_ACTIVE, (uint32_t)l->t_active); r[L_STATE] = l->state; r[L_TYPE] = 0;
    }
    p = r;
    return (int)(p - out);
}

/* ---- ACT / 封帧 ---- */
void sa_act_encode(uint32_t frame, uint32_t buttons, uint32_t flags, uint8_t out[12])
{ le32(out, frame); le32(out + 4, buttons); le32(out + 8, flags); }

int sa_act_decode(const uint8_t *p, int n, sa_act_t *out)
{
    if (n != 12) return 0;
    out->frame   = p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24;
    out->buttons = p[4] | p[5]<<8 | p[6]<<16 | (uint32_t)p[7]<<24;
    out->flags   = p[8] | p[9]<<8 | p[10]<<16 | (uint32_t)p[11]<<24;
    return 1;
}

int sa_record(uint8_t type, const uint8_t *payload, int len, uint8_t *out, int cap)
{
    if (5 + len > cap) return -1;
    le32(out, (uint32_t)len + 1); out[4] = type;
    if (len) memcpy(out + 5, payload, (size_t)len);
    return 5 + len;
}
