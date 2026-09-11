/* sa_log.c —— .stglog 写入器。fopen 一次，静态 setvbuf 缓冲，帧内零堆分配；每 60 帧 fflush。 */
#include <stdio.h>
#include <string.h>
#include "stgagent.h"

static FILE   *g_f;
static uint8_t g_rec[SA_OBS_CAP + 32];
static char    g_vbuf[64 * 1024];
static uint32_t g_frames;

int sa_log_is_open(void) { return g_f != NULL; }

int sa_log_open(const char *path, const sa_desc_t *d)
{
    static char js[16 * 1024];
    int n, len;
    if (g_f) sa_log_close();
    g_f = fopen(path, "wb");
    if (!g_f) return 0;
    setvbuf(g_f, g_vbuf, _IOFBF, sizeof g_vbuf);
    n = sa_hello_json(d, js, sizeof js);
    if (n < 0) { fclose(g_f); g_f = NULL; return 0; }
    len = sa_record(SA_MSG_HELLO, (const uint8_t *)js, n, g_rec, sizeof g_rec);
    fwrite(g_rec, 1, (size_t)len, g_f);
    g_frames = 0;
    return 1;
}

void sa_log_frame(const ap_world_t *w, uint32_t buttons, uint32_t flags)
{
    static uint8_t obs[SA_OBS_CAP];
    uint8_t act[12];
    int n, len;
    if (!g_f) return;
    n = sa_obs_encode(w, obs, sizeof obs);
    if (n < 0) return;
    len = sa_record(SA_MSG_OBS, obs, n, g_rec, sizeof g_rec);
    fwrite(g_rec, 1, (size_t)len, g_f);
    sa_act_encode(w->frame, buttons, flags, act);
    len = sa_record(SA_MSG_ACT, act, 12, g_rec, sizeof g_rec);
    fwrite(g_rec, 1, (size_t)len, g_f);
    if (++g_frames % 60 == 0) fflush(g_f);
}

void sa_log_close(void)
{
    int len;
    if (!g_f) return;
    len = sa_record(SA_MSG_BYE, NULL, 0, g_rec, sizeof g_rec);
    fwrite(g_rec, 1, (size_t)len, g_f);
    fclose(g_f);
    g_f = NULL;
}
