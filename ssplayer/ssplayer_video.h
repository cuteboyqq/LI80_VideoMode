#ifndef __VIDEO_H__
#define __VIDEO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"

#include "ssplayer_player.h"

#include "mi_disp.h"
#include "mi_disp_datatype.h"
//#include "mi_divp.h"
//#include "mi_divp_datatype.h"
#include "mi_panel.h"
#include "mi_panel_datatype.h"

#define HARD_DECODING 1
#define SOFT_DECODING 0

#define MAKE_YUYV_VALUE(y, u, v) ((y) << 24) | ((u) << 16) | ((y) << 8) | (v)
#define YUYV_BLACK MAKE_YUYV_VALUE(0, 128, 128)
#define YUYV_WHITE MAKE_YUYV_VALUE(255, 128, 128)
#define YUYV_RED MAKE_YUYV_VALUE(76, 84, 255)
#define YUYV_GREEN MAKE_YUYV_VALUE(149, 43, 21)
#define YUYV_BLUE MAKE_YUYV_VALUE(29, 225, 107)



// typedef struct ST_Sys_BindInfo_s
// {
//     MI_SYS_ChnPort_t stSrcChnPort;
//     MI_SYS_ChnPort_t stDstChnPort;
//     MI_U32           u32SrcFrmrate;
//     MI_U32           u32DstFrmrate;
// } ST_Sys_BindInfo_t;

int open_video(player_stat_t *is);
int ss_disp_Init(AVCodecContext *pVideoCodeCtx);
int ss_disp_Deinit(AVCodecContext *pVideoCodeCtx);
void sstar_disable_display(void);
void convertYUV420PtoARGBandSave_ver2(AVFrame *yuvFrame, AVFrame *p_frame_rgb, const char *filename);
void convertYUV420PtoARGBandSave(AVFrame *yuvFrame, const char *filename);
Queue vfqueue;
#ifdef __cplusplus
}
#endif

#endif
