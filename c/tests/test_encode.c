#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../stgagent.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1]<<8); }

int main(void)
{
    static ap_world_t w;
    static uint8_t buf[SA_OBS_CAP];
    char js[8192];
    int n;

    assert(sa_fx(1.0f) == 65536 && sa_fx(-184.0f) == -184 * 65536);
    assert(sa_bam((float)M_PI) == 32768 && sa_bam((float)(-M_PI / 2)) == 49152);

    /* HELLO */
    sa_desc_t d = { "th06nc", "builtin", "prev-frame-final", 60, 0x7f, { -184, 184, 16, 432 } };
    n = sa_hello_json(&d, js, sizeof js);
    assert(n > 0 && (int)strlen(js) == n);
    assert(strstr(js, "\"proto\":1,\"backend\":\"th06nc\",\"policy\":\"builtin\""));
    assert(strstr(js, "\"move_area\":{\"xmin\":-184,\"xmax\":184,\"ymin\":16,\"ymax\":432}"));
    assert(strstr(js, "{\"bit\":6,\"name\":\"SLOW\"}") && !strstr(js, "TIMESTOP"));
    assert(strstr(js, "{\"id\":2,\"name\":\"bullets\",\"cap\":640,\"stride\":30,\"fields\":[{\"name\":\"x\",\"type\":\"fx\",\"off\":0}"));
    assert(strstr(js, "{\"name\":\"angle\",\"type\":\"angle\",\"off\":20}"));
    assert(sa_hello_json(&d, js, 64) == -1);

    /* OBS：1 弹 1 敌 1 激光 */
    memset(&w, 0, sizeof w);
    w.frame = 42; w.phase = 0x41;
    w.player.x = -184.0f; w.player.y = 400.0f; w.player.lives = 3; w.player.power = 128; w.player.score = 123456;
    w.nbullets = 1; w.bullets[0].x = 10.0f; w.bullets[0].y = 20.0f; w.bullets[0].vx = 0.0f; w.bullets[0].vy = 3.0f;
    w.bullets[0].radius = 4.0f; w.bullets[0].state = 1; w.bullets[0].grazed = 1; w.bullets[0].collidable = 1; w.bullets[0].type = 7;
    w.nenemies = 1; w.enemies[0].x = 0.0f; w.enemies[0].y = 100.0f; w.enemies[0].hit_w = 16.0f; w.enemies[0].hit_h = 16.0f;
    w.enemies[0].hp = 500; w.enemies[0].hp_max = 1000; w.enemies[0].boss = 1; w.enemies[0].collidable = 1;
    w.enemies[0].id = 4242;   /* 后端给的稳定标识，不是行号 */
    w.nlasers = 1; w.lasers[0].x = 50.0f; w.lasers[0].y = 60.0f; w.lasers[0].angle = (float)(M_PI / 2);
    w.lasers[0].start = 0.0f; w.lasers[0].end = 200.0f; w.lasers[0].half_h = 4.0f; w.lasers[0].t_active = 30; w.lasers[0].state = 0;
    w.lasers[0].omega = 0.5f; w.lasers[0].vx = -1.5f; w.lasers[0].vy = 2.0f;

    n = sa_obs_encode(&w, buf, sizeof buf);
    assert(n == 9 + (3 + 36) + (3 + 30) + (3 + 38) + (3 + 48));
    assert(rd32(buf) == 42 && rd32(buf + 4) == 0x41 && buf[8] == 4);
    assert(buf[9] == 1 && rd16(buf + 10) == 1);
    assert((int32_t)rd32(buf + 12) == -184 * 65536 && buf[12 + 22] == 3 && rd16(buf + 12 + 26) == 128 && rd32(buf + 12 + 28) == 123456);
    assert(buf[48] == 2 && rd16(buf + 49) == 1);
    assert((int32_t)rd32(buf + 51 + 16) == 3 * 65536);            /* speed = 3 */
    assert(rd16(buf + 51 + 20) == 16384);                         /* angle = +π/2 */
    assert(buf[51 + 26] == 0x07 && buf[51 + 27] == 1 && rd16(buf + 51 + 28) == 7);
    assert(buf[81] == 3 && rd16(buf + 82) == 1);
    assert((int32_t)rd32(buf + 84 + 8) == 16 * 65536 && (int32_t)rd32(buf + 84 + 24) == 500);
    assert(rd16(buf + 84 + 32) == 0x11 && rd32(buf + 84 + 34) == 4242);
    assert(buf[122] == 4 && rd16(buf + 123) == 1);
    assert(rd16(buf + 125 + 8) == 16384 && (int32_t)rd32(buf + 125 + 14) == 200 * 65536);
    assert((int32_t)rd32(buf + 125 + 30) == 32768 && (int32_t)rd32(buf + 125 + 34) == -98304   /* omega 0.5、vx −1.5 */
           && (int32_t)rd32(buf + 125 + 38) == 131072 && (int32_t)rd32(buf + 125 + 42) == 30);
    assert(sa_obs_encode(&w, buf, 100) == -1);

    /* ACT + 封帧 */
    uint8_t a[12]; sa_act_t act;
    sa_act_encode(7, 0x14, 2, a);
    assert(sa_act_decode(a, 12, &act) == 1 && act.frame == 7 && act.buttons == 0x14 && act.flags == 2);
    assert(sa_act_decode(a, 11, &act) == 0);
    uint8_t rec[32];
    assert(sa_record(3, a, 12, rec, sizeof rec) == 17 && rec[0] == 13 && rec[4] == 3 && rec[5] == 7);
    assert(sa_record(3, a, 12, rec, 10) == -1);
    /* 负数计数必须被拒：need 会随之缩小甚至变负，memset((size_t)need) 就是一次越界写；
     * 即便侥幸不炸，table_hdr 也会写出 count=65535 的记录，解码侧一读就报错、整轮训练中断。 */
    {
        static ap_world_t bad;
        memset(&bad, 0, sizeof bad);
        bad.nbullets = -1;
        assert(sa_obs_encode(&bad, buf, sizeof buf) == -1);
        memset(&bad, 0, sizeof bad);
        bad.nenemies = -1;
        assert(sa_obs_encode(&bad, buf, sizeof buf) == -1);
        memset(&bad, 0, sizeof bad);
        bad.nlasers = -1000000;
        assert(sa_obs_encode(&bad, buf, sizeof buf) == -1);
    }

    puts("test_encode ok");
    return 0;
}
