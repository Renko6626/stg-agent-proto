#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../stgagent.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24; }

int main(void)
{
    static ap_world_t w;
    static uint8_t file[SA_OBS_CAP * 4];
    const char *path = "build/test_log.stglog";
    sa_desc_t d = { "c-test", "builtin", "prev-frame-final", 60, 0x7f, { -184, 184, 16, 432 } };
    FILE *f; long len; int pos, i;

    assert(!sa_log_is_open());
    assert(sa_log_open(path, &d) == 1 && sa_log_is_open());
    memset(&w, 0, sizeof w);
    for (i = 0; i < 2; i++) { w.frame = (uint32_t)i; sa_log_frame(&w, 0x10, i == 1 ? SA_ACT_HUMAN : 0); }
    sa_log_close();
    assert(!sa_log_is_open());

    f = fopen(path, "rb"); assert(f);
    len = (long)fread(file, 1, sizeof file, f); fclose(f);
    /* HELLO */
    pos = 0; assert(file[pos + 4] == SA_MSG_HELLO && memcmp(file + pos + 5, "{\"proto\":1", 10) == 0);
    pos += 4 + (int)rd32(file + pos);
    /* 帧 0：OBS + ACT */
    assert(file[pos + 4] == SA_MSG_OBS && rd32(file + pos + 5) == 0); pos += 4 + (int)rd32(file + pos);
    assert(file[pos + 4] == SA_MSG_ACT && rd32(file + pos) == 13 && rd32(file + pos + 9) == 0x10 && rd32(file + pos + 13) == 0);
    pos += 4 + (int)rd32(file + pos);
    /* 帧 1 */
    assert(file[pos + 4] == SA_MSG_OBS && rd32(file + pos + 5) == 1); pos += 4 + (int)rd32(file + pos);
    assert(file[pos + 4] == SA_MSG_ACT && rd32(file + pos + 13) == SA_ACT_HUMAN); pos += 4 + (int)rd32(file + pos);
    /* BYE */
    assert(file[pos + 4] == SA_MSG_BYE && rd32(file + pos) == 1); pos += 5;
    assert(pos == len);
    /* 没开时调用是空操作 */
    sa_log_frame(&w, 0, 0); sa_log_close();
    puts("test_log ok");
    return 0;
}
