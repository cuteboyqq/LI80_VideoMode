
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <poll.h>
#include <poll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
// #include "video.h"
// #include "packet.h"
// #include "frame.h"
// #include "player.h"
#include "ssplayer_video.h"
#include "ssplayer_packet.h"
#include "ssplayer_frame.h"
#include "ssplayer_player.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"

#include <libavutil/pixfmt.h>
// Alister add 2024-03-01
// #include "module_adas.h"

//#include "yolo_adas.hpp"
//#include "wnc_adas.hpp"

#define DISP_DEV 0
#define DISP_LAYER 0
#define DISP_INPUTPORT 0

#define MAINWND_W 720
#define MAINWND_H 1280
#define PANEL_ROTATE 1

static int      fdv;
extern AVPacket flush_pkt;
// frame_queue_t   video_frame_queue;
void convertYUV420PtoARGBandSave(AVFrame *yuvFrame, const char *filename)
{
    char       filename_yuv420[50]; // for decode video packet
    char       filename_argb[50];   // for decode video packet
    static int i, j;
#if 1
    AVFrame *p_frame_rgb = av_frame_alloc();

    p_frame_rgb->width  = 576;
    p_frame_rgb->height = 320;
#if 0
	snprintf(filename_yuv420, sizeof(filename_yuv420), "yuvframe_%04d.raw", i);
	FILE *file_yuv = fopen(filename_yuv420, "wb");
	printf("%s %d\n",__func__,__LINE__);
	
	fwrite(yuvFrame->data[0] ,1,yuvFrame->width * yuvFrame->height,file_yuv);
	fwrite(yuvFrame->data[1] ,1,(yuvFrame->width * yuvFrame->height)/2,file_yuv);
	i++;
#endif
    av_image_alloc(p_frame_rgb->data, p_frame_rgb->linesize, p_frame_rgb->width, p_frame_rgb->height, AV_PIX_FMT_RGB24,
                   1);

// struct SwsContext *swsContext =
//     sws_getContext(yuvFrame->width, yuvFrame->height, AV_PIX_FMT_NV21, p_frame_rgb->width, p_frame_rgb->height,
//                    AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

// sws_scale(swsContext, yuvFrame->data, yuvFrame->linesize, 0, yuvFrame->height, p_frame_rgb->data,
//           p_frame_rgb->linesize);

// fclose(file_yuv);
#if 0
	snprintf(filename_argb, sizeof(filename_argb), "rgbframe_%04d.raw", j);
    FILE* file = fopen(filename_argb, "wb");
	if (file) {
        for (int i = 0; i < p_frame_rgb->height; i++) {
           fwrite(p_frame_rgb->data[0] + i * p_frame_rgb->linesize[0], 1, p_frame_rgb->width * 3, file);
        }
		}
	j++;	
	fclose(file);
#endif
#endif
    // Create an ARGB frame
    // Cleanup
    // sws_freeContext(swsContext);
    av_freep(&p_frame_rgb->data[0]);
    av_frame_free(&p_frame_rgb);
}

// Alister add 2024-02-29
/*
purpose : Get the AVFrame *p_frame_rgb
*/
void convertYUV420PtoARGBandSave_ver2(AVFrame *yuvFrame, AVFrame *p_frame_rgb, const char *filename)
{
    static int i, j;
#if 1
// Alister note below code
// 	AVFrame* p_frame_rgb = av_frame_alloc();

// 	p_frame_rgb->width=576;
// 	p_frame_rgb->height=320;
#if 0
    char       filename_yuv420[50]; // for decode video packet
	snprintf(filename_yuv420, sizeof(filename_yuv420), "yuvframe_%04d.raw", i);
	FILE *file_yuv = fopen(filename_yuv420, "wb");
	printf("%s %d\n",__func__,__LINE__);

	fwrite(yuvFrame->data[0] ,1,yuvFrame->width * yuvFrame->height,file_yuv);
	fwrite(yuvFrame->data[1] ,1,(yuvFrame->width * yuvFrame->height)/2,file_yuv);
	i++;
#endif
    int av_im_al = av_image_alloc(p_frame_rgb->data, p_frame_rgb->linesize, p_frame_rgb->width, p_frame_rgb->height,
                                  AV_PIX_FMT_RGB24, 1);

    struct SwsContext *swsContext =
        sws_getContext(yuvFrame->width, yuvFrame->height, AV_PIX_FMT_NV21, p_frame_rgb->width, p_frame_rgb->height,
                       AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

    int scale = sws_scale(swsContext, yuvFrame->data, yuvFrame->linesize, 0, yuvFrame->height, p_frame_rgb->data,
                          p_frame_rgb->linesize);

// p_frame_rgb->data is what ADAS want as input
// fclose(file_yuv);
#if 0
    char       filename_argb[50];   // for decode video packet
	snprintf(filename_argb, sizeof(filename_argb), "rgbframe_%04d.raw", j);
    FILE* file = fopen(filename_argb, "wb");
	if (file) {
        for (int i = 0; i < p_frame_rgb->height; i++) {
           fwrite(p_frame_rgb->data[0] + i * p_frame_rgb->linesize[0], 1, p_frame_rgb->width * 3, file);
        }
		}
	j++;
	fclose(file);
#endif
// Alister need to convert 	p_frame_rgb->data ino cv::Mat format~~~~2024-03-01

#endif
    // // Create an ARGB frame
    // // Cleanup
    // sws_freeContext(swsContext);
    // av_freep(&p_frame_rgb->data[0]);
    // av_frame_free(&p_frame_rgb);
}

static int queue_picture(player_stat_t *is, AVFrame *src_frame, double pts, double duration, int64_t pos)
{
    frame_t *vp;

    if (!(vp = frame_queue_peek_writable(&is->video_frm_queue)))
        return -1;

    vp->sar      = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width  = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts      = pts;
    vp->duration = duration;
    vp->pos      = pos;
    // vp->serial = serial;

    // set_default_window_size(vp->width, vp->height, vp->sar);

    av_frame_move_ref(vp->frame, src_frame);

    // printf("before queue ridx: %d,widx: %d,size: %d,maxsize: %d\n
    // ",is->video_frm_queue.rindex,is->video_frm_queue.windex,is->video_frm_queue.size,is->video_frm_queue.max_size);
    frame_queue_push(&is->video_frm_queue);
    // printf("after queue ridx: %d,widx: %d,size: %d,maxsize: %d\n
    // ",is->video_frm_queue.rindex,is->video_frm_queue.windex,is->video_frm_queue.size,is->video_frm_queue.max_size);
    return 0;
}

static int64_t        pktpos, pktpts;
static struct timeval tvStart;
static bool           decoded;
static int video_decode_frame(AVCodecContext *p_codec_ctx, packet_queue_t *p_pkt_queue, AVFrame *frame)
{
    int ret;

    while (1)
    {
        AVPacket pkt;

        while (1)
        {
            // printf("get in video_decode_frame!\n");
            if (p_pkt_queue->abort_request)
            {
                return -1;
            }
            // p_codec_ctx->flags = 1;
            ret = avcodec_receive_frame(p_codec_ctx, frame);
            if (ret < 0)
            {
                if (ret == AVERROR_EOF)
                {
                    av_log(NULL, AV_LOG_INFO, "video avcodec_receive_frame(): the decoder has been fully flushed\n");
                    avcodec_flush_buffers(p_codec_ctx);
                    return 0;
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    // av_log(NULL, AV_LOG_INFO, "video avcodec_receive_frame(): output is not available in this state -
                    // "
                    //"user must try to send new input\n");
                    // av_log(NULL, AV_LOG_ERROR, "ret : %d, cann't fetch a frame, try again!\n", ret);
                    break;
                }
                else
                {
                    av_log(NULL, AV_LOG_ERROR, "video avcodec_receive_frame(): other errors\n");
                    continue;
                }
            }
            else
            {
                frame->pts = frame->best_effort_timestamp;
// printf("best_effort_timestamp : %lld.\n", frame->pts);
#if 0
                //printf("frame pos: %lld\n",frame->pkt_pos);
                if(frame->pkt_pos == pktpos || frame->pts == pktpts)
                {
                    struct timeval tv;
                    int64_t time;

                    gettimeofday(&tv, NULL);
                    time = ((int64_t)tv.tv_sec * 1000000 + tv.tv_usec) - ((int64_t)tvStart.tv_sec * 1000000 + tvStart.tv_usec);

                    printf("fps: %lld\n",time);
                    decoded = 0;
                }
#endif

                return 1;
            }
        }

        if (packet_queue_get(p_pkt_queue, &pkt, true) < 0)
        {
            printf("packet_queue_get fail\n");
            return -1;
        }

        if (pkt.data == flush_pkt.data)
        {
            avcodec_flush_buffers(p_codec_ctx);
        }
        else
        {
            if (pkt.data == NULL || pkt.size == 0)
            {
                p_codec_ctx->flags |= (1 << 5);
                printf("send a null paket to decoder\n");
            }
            else
            {
                p_codec_ctx->flags &= ~(1 << 5);
            }

#if 0
            //printf("pkt pos: %lld\n",pkt.pos);
            if(!decoded)
            {
                pktpos = pkt.pos;
                pktpts = pkt.pts;
                gettimeofday(&tvStart, NULL);
                decoded = 1;
                //printf("packet info : dts--%d,pts--%d\n", pkt.dts, pkt.pts);
            }
            //calctime = av_gettime_relative();
#endif
            // p_codec_ctx->flags = 2;
            if (avcodec_send_packet(p_codec_ctx, &pkt) == AVERROR(EAGAIN))
            {
                av_log(NULL, AV_LOG_ERROR,
                       "receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
            }
            av_packet_unref(&pkt);
        }
        // printf("exit out video_decode_frame!\n");
    }
}

static int video_decode_thread(void *arg)
{
    player_stat_t *is      = (player_stat_t *)arg;
    AVFrame *      p_frame = av_frame_alloc();

    double     pts;
    double     duration;
    int        ret;
    int        got_picture;
    AVRational tb         = is->p_video_stream->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->p_fmt_ctx, is->p_video_stream, NULL);
    char       filename[50]; // for decode video packet
    int        frame_number = 0;
    if (p_frame == NULL)
    {
        printf("av_frame_alloc() for p_frame failed\n");
        return AVERROR(ENOMEM);
    }

    printf("video time base : %f ms.\n", 1000 * av_q2d(tb));
    printf("frame rate num : %d. frame rate den : %d.\n", frame_rate.num, frame_rate.den);

    printf("get in video decode thread!\n");

    struct timeval trans_start, trans_end; // for decode video packet
    int64_t        time0, time1;

    gettimeofday(&trans_start, NULL);

    // AVFrame* argbFrame = av_frame_alloc();

    while (1)
    {
        //=====================================================
        // Alister add 2024-02-29
        AVFrame *p_frame_rgb = av_frame_alloc();
        p_frame_rgb->width   = 576;
        p_frame_rgb->height  = 320;
        //=====================================================

        if (is->abort_request)
        {
            printf("video decode thread exit\n");
            break;
        }
        got_picture = video_decode_frame(is->p_vcodec_ctx, &is->video_pkt_queue, p_frame);
        if (got_picture < 0)
        {
            printf("got pic fail\n");
            goto exit;
        }
        else if (got_picture > 0)
        {
#if 1
            convertYUV420PtoARGBandSave(p_frame, filename);
            // Alister add this function 2024-02-29, need to get frame : p_frame_rgb->data
            convertYUV420PtoARGBandSave_ver2(p_frame, p_frame_rgb, filename);
//========================================================================
// Alister's task :  input video frame into ADAS
// g_IpuIntfObject->m_opticalFlow->updateInputFrame_FromVideo(p_frame_rgb);
// g_IpuIntfObject->m_yoloADAS->updateInputFrame_FromVideo(p_frame_rgb);
// g_IpuIntfObject->m_yoloADAS->updateInputFrame_FromVideo(p_frame_rgb);
//=====================================================================

// Cleanup
// av_frame_free(&argbFrame);
#endif

            // printf(" %s %d get frame_number %d\n",__func__,__LINE__,frame_number);

            frame_number++;
            if (frame_number % 240 == 0)
            {
                printf(" %s %d get frame_number 240\n", __func__, __LINE__);

                gettimeofday(&trans_end, NULL);

                // Calculate time spent for the last set of 30 frames
                double elapsedTime =
                    (trans_end.tv_sec - trans_start.tv_sec) + (trans_end.tv_usec - trans_start.tv_usec) / 1000000.0;
                // double elapsedTime = (trans_end.tv_sec - trans_start.tv_sec);
                printf("Hi! Time spent for 240 %.6f seconds\n", elapsedTime);
                gettimeofday(&trans_start, NULL);
            }

            // printf(" %s %d write to file down  in video decode  !\n",__func__,__LINE__);

            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
            pts      = (p_frame->pts == AV_NOPTS_VALUE) ? NAN : p_frame->pts * av_q2d(tb);

            // printf("frame duration : %f. video frame clock : %f.\n", duration, pts);
            ret = queue_picture(is, p_frame, pts, duration, p_frame->pkt_pos);
            av_frame_unref(p_frame);
        }

        if (ret < 0)
        {
            goto exit;
        }

    } // End while

exit:
    av_frame_free(&p_frame);
    // Create an ARGB frame
    // Cleanup
    // sws_freeContext(swsContext);
    // av_freep(&p_frame_rgb->data[0]);
    // av_frame_free(&p_frame_rgb);

    return 0;
}

static int video_decode_thread_VideoMode(void *arg)
{
    player_stat_t *is      = (player_stat_t *)arg;
    AVFrame *      p_frame = av_frame_alloc();
    double         pts;
    double         duration;
    int            ret;
    int            got_picture;
    AVRational     tb         = is->p_video_stream->time_base;
    AVRational     frame_rate = av_guess_frame_rate(is->p_fmt_ctx, is->p_video_stream, NULL);
    char           filename[50]; // for decode video packet
    int            frame_number = 0;
    if (p_frame == NULL)
    {
        printf("av_frame_alloc() for p_frame failed\n");
        return AVERROR(ENOMEM);
    }

    printf("video time base : %f ms.\n", 1000 * av_q2d(tb));
    printf("frame rate num : %d. frame rate den : %d.\n", frame_rate.num, frame_rate.den);

    printf("get in video decode thread!\n");

    struct timeval trans_start, trans_end; // for decode video packet
    int64_t        time0, time1;

    gettimeofday(&trans_start, NULL);

    // AVFrame* argbFrame = av_frame_alloc();
    // Queue queue; // Failed define at ssplayer_video.h
    initQueue(&vfqueue);
    while (1)
    {
        //=====================================================
        // Alister add 2024-02-29
        AVFrame *p_frame_rgb = av_frame_alloc();
        p_frame_rgb->width   = 576;
        p_frame_rgb->height  = 320;
        //=====================================================

        if (is->abort_request)
        {
            printf("video decode thread exit\n");
            break;
        }
        got_picture = video_decode_frame(is->p_vcodec_ctx, &is->video_pkt_queue, p_frame);
        if (got_picture < 0)
        {
            printf("got pic fail\n");
            goto exit;
        }
        else if (got_picture > 0)
        {
#if 1
            convertYUV420PtoARGBandSave(p_frame, filename);
            // Alister add this function 2024-02-29, need to get frame : p_frame_rgb->data
            convertYUV420PtoARGBandSave_ver2(p_frame, p_frame_rgb, filename);

            enqueue(&vfqueue, p_frame_rgb);
// video_frame_queue.queue[frame_number].frame = p_frame_rgb;
// video_frame_queue.rindex                    = frame_number;
// video_frame_queue.windex                    = frame_number;

//========================================================================
// Alister's task :  input video frame into ADAS
// g_IpuIntfObject->m_opticalFlow->updateInputFrame_FromVideo(p_frame_rgb);
// g_IpuIntfObject->m_yoloADAS->updateInputFrame_FromVideo(p_frame_rgb);
// g_IpuIntfObject->m_yoloADAS->updateInputFrame_FromVideo(p_frame_rgb);
//=====================================================================

// Cleanup
// av_frame_free(&argbFrame);
#endif

            // printf(" %s %d get frame_number %d\n",__func__,__LINE__,frame_number);

            frame_number++;
            if (frame_number % 240 == 0)
            {
                printf(" %s %d get frame_number 240\n", __func__, __LINE__);

                gettimeofday(&trans_end, NULL);

                // Calculate time spent for the last set of 30 frames
                double elapsedTime =
                    (trans_end.tv_sec - trans_start.tv_sec) + (trans_end.tv_usec - trans_start.tv_usec) / 1000000.0;
                // double elapsedTime = (trans_end.tv_sec - trans_start.tv_sec);
                printf("Hi! Time spent for 240 %.6f seconds\n", elapsedTime);
                gettimeofday(&trans_start, NULL);
            }

            // printf(" %s %d write to file down  in video decode  !\n",__func__,__LINE__);

            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
            pts      = (p_frame->pts == AV_NOPTS_VALUE) ? NAN : p_frame->pts * av_q2d(tb);

            // printf("frame duration : %f. video frame clock : %f.\n", duration, pts);
            ret = queue_picture(is, p_frame, pts, duration, p_frame->pkt_pos);
            av_frame_unref(p_frame);
        }

        if (ret < 0)
        {
            goto exit;
        }

    } // End while

exit:
    av_frame_free(&p_frame);
    // Create an ARGB frame
    // Cleanup
    // sws_freeContext(swsContext);
    // av_freep(&p_frame_rgb->data[0]);
    // av_frame_free(&p_frame_rgb);

    return 0;
}

static double compute_target_delay(double delay, player_stat_t *is)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */

    /* if video is slave, we try to correct big delays by
       duplicating or deleting a frame */
    if (is->video_idx > 0 && is->audio_idx > 0)
        diff = get_clock(&is->video_clk) - get_clock(&is->audio_clk);
    else
        return delay;
    // printf("audio pts: %lf,video pts: %lf\n",is->audio_clk.pts,is->video_clk.pts);
    // printf("audio clock: %lf,video clock: %lf\n",get_clock(&is->audio_clk),get_clock(&is->video_clk));
    // printf("video pts: %lf,lu: %lf,curtime: %lf\n
    // ",is->video_clk.pts,is->video_clk.last_updated,av_gettime_relative() / 1000000.0);
    // printf("audio pts: %lf,lu: %lf,curtime: %lf\n
    // ",is->audio_clk.pts,is->audio_clk.last_updated,av_gettime_relative() / 1000000.0);
    // printf("video diff audio time: %lf\n",diff);

    sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
    if (!isnan(diff))
    {
        if (diff <= -sync_threshold)
            delay = FFMAX(0, delay + diff);
        else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
            delay = delay + diff;
        else if (diff >= sync_threshold)
            delay = 2 * delay;
    }

    // av_log(NULL, AV_LOG_INFO, "video: delay=%0.3lf A-V=%lf\n", delay, -diff);
    // printf("video: delay=%0.3f A-V=%f\n", delay, -diff);

    return delay;
}

static double vp_duration(player_stat_t *is, frame_t *vp, frame_t *nextvp)
{
    if (vp->serial == nextvp->serial)
    {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0)
            return vp->duration;
        else
            return duration;
    }
    else
    {
        return 0.0;
    }
}

static void update_video_pts(player_stat_t *is, double pts, int64_t pos, int serial)
{
    /* update current video pts */
    set_clock(&is->video_clk, pts, serial);
    //-sync_clock_to_slave(&is->extclk, &is->vidclk);
}

static void video_display(player_stat_t *is)
{
    frame_t *vp;
    uint8_t *frame_ydata, *frame_uvdata;

    // av_log(NULL, AV_LOG_ERROR, "rindex : %d, shownidex : %d, size : %d\n", is->video_frm_queue.rindex,
    // is->video_frm_queue.rindex_shown, is->video_frm_queue.size);
    vp = frame_queue_peek_last(&is->video_frm_queue);
    // printf("frame data addr : %p %p\n", vp->frame->data[0], vp->frame->data[1]);
    // vp->frame->linesize[AV_NUM_DATA_POINTERS] = {vp->frame->width, vp->frame->width, 0, 0, 0, 0, 0, 0};
    // printf("get vp disp ridx: %d,format: %d\n",is->video_frm_queue.rindex,vp->frame->format);

    // struct timeval trans_start, trans_tim, trans_end;
    // int64_t time0, time1;

    // gettimeofday(&trans_start, NULL);

    if (is->decoder_type == SOFT_DECODING && vp->frame->format != AV_PIX_FMT_NV12)
    {
#if (SW_SCALE)
        sws_scale(is->img_convert_ctx,                     // sws context
                  (const uint8_t *const *)vp->frame->data, // src slice
                  vp->frame->linesize,                     // src stride
                  0,                                       // src slice y
                  is->p_vcodec_ctx->height,                // src slice height
                  is->p_frm_yuv->data,                     // dst planes
                  is->p_frm_yuv->linesize                  // dst strides
                  );

        frame_ydata  = is->p_frm_yuv->data[0];
        frame_uvdata = is->p_frm_yuv->data[1];
#endif
    }
    else if (is->decoder_type == HARD_DECODING)
    {
        frame_ydata  = vp->frame->data[0];
        frame_uvdata = vp->frame->data[1];
    }

    // gettimeofday(&trans_tim, NULL);

    int ysize, index;
    ysize = vp->frame->width * vp->frame->height;
    // printf("save yuv width: %d,height: %d\n",vp->frame->width,vp->frame->height);

    // put stream to ss disp start
    MI_SYS_BUF_HANDLE hHandle;
    MI_SYS_ChnPort_t  pstSysChnPort;
    MI_SYS_BufConf_t  stBufConf;
    MI_SYS_BufInfo_t  stBufInfo;

    pstSysChnPort.eModId    = E_MI_MODULE_ID_DIVP; // E_MI_MODULE_ID_DISP;
    pstSysChnPort.u32ChnId  = 0;
    pstSysChnPort.u32DevId  = 0;
    pstSysChnPort.u32PortId = 0;

    memset(&stBufInfo, 0, sizeof(MI_SYS_BufInfo_t));
    memset(&stBufConf, 0, sizeof(MI_SYS_BufConf_t));

    stBufConf.eBufType             = E_MI_SYS_BUFDATA_FRAME;
    stBufConf.u64TargetPts         = 0;
    stBufConf.stFrameCfg.u16Width  = is->p_vcodec_ctx->width;
    stBufConf.stFrameCfg.u16Height = is->p_vcodec_ctx->height;
    stBufConf.stFrameCfg.eFormat   = vp->frame->format != AV_PIX_FMT_NV12 ? E_MI_SYS_PIXEL_FRAME_YUV422_YUYV
                                                                        : E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420;
    stBufConf.stFrameCfg.eFrameScanMode = E_MI_SYS_FRAME_SCAN_MODE_PROGRESSIVE;

    if (MI_SUCCESS == MI_SYS_ChnInputPortGetBuf(&pstSysChnPort, &stBufConf, &stBufInfo, &hHandle, -1))
    {
        stBufInfo.stFrameData.eCompressMode = E_MI_SYS_COMPRESS_MODE_NONE;
        stBufInfo.stFrameData.eFieldType    = E_MI_SYS_FIELDTYPE_NONE;
        stBufInfo.stFrameData.eTileMode     = E_MI_SYS_FRAME_TILE_MODE_NONE;
        stBufInfo.bEndOfStream              = FALSE;
        // printf("divp width : %d, height : %d\n", stBufInfo.stFrameData.u16Width, stBufInfo.stFrameData.u16Height);

        //向DIVP中填数据时必须按照stride大小填充
        if (stBufConf.stFrameCfg.eFormat == E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420)
        {
            if (stBufInfo.stFrameData.u32Stride[0] == vp->frame->width)
            {
                memcpy(stBufInfo.stFrameData.pVirAddr[0], frame_ydata, ysize);
                memcpy(stBufInfo.stFrameData.pVirAddr[1], frame_uvdata, ysize / 2);
            }
            else
            {
                for (index = 0; index < vp->frame->height; index++)
                {
                    memcpy(stBufInfo.stFrameData.pVirAddr[0] + index * stBufInfo.stFrameData.u32Stride[0],
                           frame_ydata + index * vp->frame->width, stBufInfo.stFrameData.u16Width);
                }

                for (index = 0; index < vp->frame->height / 2; index++)
                {
                    memcpy(stBufInfo.stFrameData.pVirAddr[1] + index * stBufInfo.stFrameData.u32Stride[1],
                           frame_uvdata + index * vp->frame->width, stBufInfo.stFrameData.u16Width);
                }
            }
        }
        else
        {
            memcpy(stBufInfo.stFrameData.pVirAddr[0], frame_ydata, ysize * 2);
        }

// printf("data0: %p,data1: %p,data2: %p\n",vp->frame->data[0],vp->frame->data[1],vp->frame->data[2]);
#if 0
        memcpy(stBufInfo.stFrameData.pVirAddr[0],vp->frame->data[0],ysize);
        memcpy(stBufInfo.stFrameData.pVirAddr[1],vp->frame->data[1],ysize/4);
        memcpy(stBufInfo.stFrameData.pVirAddr[1]+ysize/4,vp->frame->data[2],ysize/4);
#endif

        // FILE *fpread1 = fopen("pic_later.yuv", "a+");
        // fwrite(frame_ydata , 1, ysize, fpread1);
        // fwrite(frame_uvdata, 1, ysize / 2, fpread1);
        // fclose(fpread1);

        MI_SYS_ChnInputPortPutBuf(hHandle, &stBufInfo, FALSE);
    }
    // put stream to ss disp end
    // gettimeofday(&trans_end, NULL);
    // time0 = ((int64_t)trans_tim.tv_sec * 1000000 + trans_tim.tv_usec) - ((int64_t)trans_start.tv_sec * 1000000 +
    // trans_start.tv_usec);
    // time1 = ((int64_t)trans_end.tv_sec * 1000000 + trans_end.tv_usec) - ((int64_t)trans_tim.tv_sec * 1000000 +
    // trans_tim.tv_usec);
    // printf("yuv420p to nv12 : %lldus, send to divp : %lldus\n", time0, time1);
}

/* called to display each frame */
static void video_refresh(void *opaque, double *remaining_time)
{
    player_stat_t *is = (player_stat_t *)opaque;
    double         time;
    static bool    first_frame = true;

retry:

    if (is->paused)
        goto display;

    if (frame_queue_nb_remaining(&is->video_frm_queue) <= 0)
    {
        // nothing to do, no picture to display in the queue
        // printf("already last frame: %d\n",is->video_frm_queue.size);
        if (!is->video_complete && is->eof && is->video_pkt_queue.nb_packets == 0)
        {
            is->video_complete = 1;
            if (is->audio_complete && is->video_complete)
                stream_seek(is, is->p_fmt_ctx->start_time, 0, 0);
        }
        return;
    }

    double   last_duration, duration, delay;
    frame_t *vp, *lastvp;

    /* dequeue the picture */
    lastvp = frame_queue_peek_last(&is->video_frm_queue);
    vp     = frame_queue_peek(&is->video_frm_queue);
    // printf("refresh ridx: %d,rs:%d,widx: %d,size: %d,maxsize:
    // %d\n",is->video_frm_queue.rindex,is->video_frm_queue.rindex_shown,is->video_frm_queue.windex,is->video_frm_queue.size,is->video_frm_queue.max_size);
    // printf("lastpos: %ld,lastpts: %lf,vppos: %ld,vppts: %lf\n",lastvp->pos,lastvp->pts,vp->pos,vp->pts);

    if (first_frame)
    {
        is->frame_timer = av_gettime_relative() / 1000000.0;
        first_frame     = false;
    }

    /* compute nominal last_duration */
    last_duration = vp_duration(is, lastvp, vp);
    delay         = compute_target_delay(last_duration, is);
    // printf("last_duration: %lf,delay: %lf\n",last_duration,delay);
    time = av_gettime_relative() / 1000000.0;

    if (time < is->frame_timer + delay)
    {
        *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
        // printf("not ready play\n");
        return;
    }
    // printf("remaining time : %f. duration : %f.\n", *remaining_time, last_duration);

    is->frame_timer += delay;
    // printf("frame_timer : %0.6lf, video pts : %0.6lf, mremaining : %0.6lf\n", is->frame_timer, vp->pts,
    // *remaining_time);
    if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
    {
        is->frame_timer = time;
    }

    pthread_mutex_lock(&is->video_frm_queue.mutex);
    if (!isnan(vp->pts))
    {
        update_video_pts(is, vp->pts, vp->pos, vp->serial);
    }
    pthread_mutex_unlock(&is->video_frm_queue.mutex);

    if (frame_queue_nb_remaining(&is->video_frm_queue) > 1)
    {
        frame_t *nextvp = frame_queue_peek_next(&is->video_frm_queue);
        duration        = vp_duration(is, vp, nextvp);

        if (time > is->frame_timer + duration)
        {
            frame_queue_next(&is->video_frm_queue);
            // av_log(NULL, AV_LOG_INFO, "discard current frame!\n");
            goto retry;
        }
    }

    // AVRational frame_rate = av_guess_frame_rate(is->p_fmt_ctx, is->p_video_stream, NULL);
    //*remaining_time = av_q2d((AVRational){frame_rate.den, frame_rate.num});    //no sync
    // printf("remaining time : %f.\n", (*remaining_time) * AV_TIME_BASE);

    frame_queue_next(&is->video_frm_queue);

    if (is->step && !is->paused)
    {
        stream_toggle_pause(is);
    }

display:
    // video_display(is);

    return;
}

static int video_playing_thread(void *arg)
{
    player_stat_t *is             = (player_stat_t *)arg;
    double         remaining_time = 0.0;
    printf("video_playing_thread in\n");
    while (1)
    {
        frame_queue_next(&is->video_frm_queue);
#if 1 // need keep otherwise occur unsorted double linked list corrupted
        if (is->abort_request)
        {
            printf("video play thread exit\n");
            break;
        }
        if (remaining_time > 0.0)
        {
            // printf("delay time: %lf\n",remaining_time);
            av_usleep((unsigned)(remaining_time * 1000000.0));
        }
        remaining_time = REFRESH_RATE;
        // av_usleep(10*1000);
        video_refresh(is, &remaining_time);
#endif
    }

    return 0;
}
#if 0
MI_S32 ST_Sys_Bind(ST_Sys_BindInfo_t *pstBindInfo)
{
    MI_SYS_BindChnPort(&pstBindInfo->stSrcChnPort, &pstBindInfo->stDstChnPort, \
        pstBindInfo->u32SrcFrmrate, pstBindInfo->u32DstFrmrate);

    return MI_SUCCESS;
}

MI_S32 ST_Sys_UnBind(ST_Sys_BindInfo_t *pstBindInfo)
{
    MI_SYS_UnBindChnPort(&pstBindInfo->stSrcChnPort, &pstBindInfo->stDstChnPort);

    return MI_SUCCESS;
}
#endif
#if 0
MI_S32 sstar_enable_display(MI_U16 srcWidth, MI_U16 srcHeight, MI_U16 dstWidth, MI_U16 dstHeight)
{
    MI_DISP_DEV dispDev = DISP_DEV;
    MI_DISP_LAYER dispLayer = DISP_LAYER;
    MI_U32 u32InputPort = DISP_INPUTPORT;
    MI_DISP_PubAttr_t stPubAttr;
    MI_DISP_InputPortAttr_t stInputPortAttr;

    //init divp
    MI_DIVP_ChnAttr_t stDivpChnAttr;
    MI_DIVP_OutputPortAttr_t stDivpOutAttr;
    ST_Sys_BindInfo_t stBindInfo;

    memset(&stDivpChnAttr, 0, sizeof(MI_DIVP_ChnAttr_t));
    stDivpChnAttr.bHorMirror            = FALSE;
    stDivpChnAttr.bVerMirror            = FALSE;
    stDivpChnAttr.eDiType               = E_MI_DIVP_DI_TYPE_OFF;
    stDivpChnAttr.eRotateType           = E_MI_SYS_ROTATE_NONE;
    stDivpChnAttr.eTnrLevel             = E_MI_DIVP_TNR_LEVEL_OFF;
    stDivpChnAttr.stCropRect.u16X       = 0;
    stDivpChnAttr.stCropRect.u16Y       = 0;
    stDivpChnAttr.stCropRect.u16Width   = srcWidth;
    stDivpChnAttr.stCropRect.u16Height  = srcHeight;
    stDivpChnAttr.u32MaxWidth           = srcWidth;
    stDivpChnAttr.u32MaxHeight          = srcHeight;
    MI_DIVP_CreateChn(0, &stDivpChnAttr);
    MI_DIVP_StartChn(0);

    memset(&stDivpOutAttr, 0, sizeof(stDivpOutAttr));
    stDivpOutAttr.eCompMode          = E_MI_SYS_COMPRESS_MODE_NONE;
    stDivpOutAttr.ePixelFormat       = E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420;
#if (PANEL_ROTATE)
    stDivpOutAttr.u32Width           = dstHeight;
    stDivpOutAttr.u32Height          = dstWidth;
#else
    stDivpOutAttr.u32Width           = dstWidth;
    stDivpOutAttr.u32Height          = dstHeight;
#endif
    MI_DIVP_SetOutputPortAttr(0, &stDivpOutAttr);

#if (PANEL_ROTATE)
    memset(&stDivpChnAttr, 0, sizeof(MI_DIVP_ChnAttr_t));
    stDivpChnAttr.bHorMirror            = FALSE;
    stDivpChnAttr.bVerMirror            = FALSE;
    stDivpChnAttr.eDiType               = E_MI_DIVP_DI_TYPE_OFF;
    stDivpChnAttr.eRotateType           = E_MI_SYS_ROTATE_270;
    stDivpChnAttr.eTnrLevel             = E_MI_DIVP_TNR_LEVEL_OFF;
    stDivpChnAttr.stCropRect.u16X       = 0;
    stDivpChnAttr.stCropRect.u16Y       = 0;
    stDivpChnAttr.stCropRect.u16Width   = dstHeight;
    stDivpChnAttr.stCropRect.u16Height  = dstWidth;
    stDivpChnAttr.u32MaxWidth           = dstHeight;
    stDivpChnAttr.u32MaxHeight          = dstWidth;
    MI_DIVP_CreateChn(1, &stDivpChnAttr);
    MI_DIVP_StartChn(1);

    memset(&stDivpOutAttr, 0, sizeof(stDivpOutAttr));
    stDivpOutAttr.eCompMode          = E_MI_SYS_COMPRESS_MODE_NONE;
    stDivpOutAttr.ePixelFormat       = E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420;
    stDivpOutAttr.u32Width           = dstWidth;
    stDivpOutAttr.u32Height          = dstHeight;
    MI_DIVP_SetOutputPortAttr(1, &stDivpOutAttr);
#endif

    memset(&stPubAttr, 0, sizeof(MI_DISP_PubAttr_t));
    stPubAttr.eIntfSync  = E_MI_DISP_OUTPUT_USER;
    stPubAttr.eIntfType  = E_MI_DISP_INTF_MIPIDSI;
    stPubAttr.u32BgColor = YUYV_BLACK;
    MI_DISP_SetPubAttr(dispDev, &stPubAttr);
    MI_DISP_Enable(dispDev);
    MI_DISP_BindVideoLayer(dispLayer, dispDev);
    MI_DISP_EnableVideoLayer(dispLayer);

    memset(&stInputPortAttr, 0, sizeof(MI_DISP_InputPortAttr_t));
    MI_DISP_GetInputPortAttr(dispLayer, u32InputPort, &stInputPortAttr);
    stInputPortAttr.stDispWin.u16X      = 0;
    stInputPortAttr.stDispWin.u16Y      = 0;
    stInputPortAttr.stDispWin.u16Width  = dstWidth;
    stInputPortAttr.stDispWin.u16Height = dstHeight;
    stInputPortAttr.u16SrcWidth         = dstWidth;
    stInputPortAttr.u16SrcHeight        = dstHeight;
    MI_DISP_SetInputPortAttr(dispLayer, u32InputPort, &stInputPortAttr);
    MI_DISP_EnableInputPort(dispLayer, u32InputPort);

    MI_PANEL_Init(E_MI_PNL_INTF_MIPI_DSI);

#if (PANEL_ROTATE)
    memset(&stBindInfo, 0, sizeof(ST_Sys_BindInfo_t));
    stBindInfo.stSrcChnPort.eModId = E_MI_MODULE_ID_DIVP;
    stBindInfo.stSrcChnPort.u32DevId = 0;
    stBindInfo.stSrcChnPort.u32ChnId = 0;
    stBindInfo.stSrcChnPort.u32PortId = 0;
    stBindInfo.stDstChnPort.eModId = E_MI_MODULE_ID_DIVP;
    stBindInfo.stDstChnPort.u32DevId = 0;
    stBindInfo.stDstChnPort.u32ChnId = 1;
    stBindInfo.stDstChnPort.u32PortId = 0;
    stBindInfo.u32SrcFrmrate = 0;
    stBindInfo.u32DstFrmrate = 0;
    ST_Sys_Bind(&stBindInfo);
#endif

    memset(&stBindInfo, 0, sizeof(ST_Sys_BindInfo_t));
    stBindInfo.stSrcChnPort.eModId = E_MI_MODULE_ID_DIVP;
    stBindInfo.stSrcChnPort.u32DevId = 0;
#if (PANEL_ROTATE)
    stBindInfo.stSrcChnPort.u32ChnId = 1;
#else
    stBindInfo.stSrcChnPort.u32ChnId = 0;
#endif
    stBindInfo.stSrcChnPort.u32PortId = 0;
    stBindInfo.stDstChnPort.eModId = E_MI_MODULE_ID_DISP;
    stBindInfo.stDstChnPort.u32DevId = 0;
    stBindInfo.stDstChnPort.u32ChnId = 0;
    stBindInfo.stDstChnPort.u32PortId = 0;
    stBindInfo.u32SrcFrmrate = 0;
    stBindInfo.u32DstFrmrate = 0;
    ST_Sys_Bind(&stBindInfo);

    return 0;
}
#endif
#if 0
void sstar_disable_display(void)
{
    MI_DISP_DEV dispDev = DISP_DEV;
    MI_DISP_LAYER dispLayer = DISP_LAYER;
    MI_U32 u32InputPort = DISP_INPUTPORT;

    ST_Sys_BindInfo_t stBindInfo;

    MI_PANEL_DeInit();

    memset(&stBindInfo, 0, sizeof(ST_Sys_BindInfo_t));
    stBindInfo.stSrcChnPort.eModId = E_MI_MODULE_ID_DIVP;
    stBindInfo.stSrcChnPort.u32DevId = 0;
#if (PANEL_ROTATE)
    stBindInfo.stSrcChnPort.u32ChnId = 1;
#else
    stBindInfo.stSrcChnPort.u32ChnId = 0;
#endif
    stBindInfo.stSrcChnPort.u32PortId = 0;
    stBindInfo.stDstChnPort.eModId = E_MI_MODULE_ID_DISP;
    stBindInfo.stDstChnPort.u32DevId = 0;
    stBindInfo.stDstChnPort.u32ChnId = 0;
    stBindInfo.stDstChnPort.u32PortId = 0;
    ST_Sys_UnBind(&stBindInfo);

#if (PANEL_ROTATE)
    memset(&stBindInfo, 0, sizeof(ST_Sys_BindInfo_t));
    stBindInfo.stSrcChnPort.eModId = E_MI_MODULE_ID_DIVP;
    stBindInfo.stSrcChnPort.u32DevId = 0;
    stBindInfo.stSrcChnPort.u32ChnId = 0;
    stBindInfo.stSrcChnPort.u32PortId = 0;
    stBindInfo.stDstChnPort.eModId = E_MI_MODULE_ID_DIVP;
    stBindInfo.stDstChnPort.u32DevId = 0;
    stBindInfo.stDstChnPort.u32ChnId = 1;
    stBindInfo.stDstChnPort.u32PortId = 0;
    ST_Sys_UnBind(&stBindInfo);
#endif

    MI_DIVP_StopChn(0);
    MI_DIVP_DestroyChn(0);

#if (PANEL_ROTATE)
    MI_DIVP_StopChn(1);
    MI_DIVP_DestroyChn(1);
#endif

    MI_DISP_DisableInputPort(dispLayer, u32InputPort);
    MI_DISP_DisableVideoLayer(dispLayer);
    MI_DISP_UnBindVideoLayer(dispLayer, dispDev);
    MI_DISP_Disable(dispDev);
}
#endif
static int open_video_playing(void *arg)
{
    player_stat_t *     is = (player_stat_t *)arg;
    int                 ret;
    int                 buf_size;
    uint8_t *           buffer = NULL;
    AVPixFmtDescriptor *desc;

#if 0
    buf_size = av_image_get_buffer_size(AV_PIX_FMT_NV12,
                                        is->p_vcodec_ctx->width,
                                        is->p_vcodec_ctx->height,
                                        1
                                        );
    //printf("alloc size: %d,width: %d,height: %d\n",buf_size,is->p_vcodec_ctx->width,is->p_vcodec_ctx->height);

    buffer = (uint8_t *)av_malloc(buf_size);
    if (buffer == NULL)
    {
        printf("av_malloc() for buffer failed\n");
        return -1;
    }

    ret = av_image_fill_arrays(is->p_frm_yuv->data,     // dst data[]
                               is->p_frm_yuv->linesize, // dst linesize[]
                               buffer,                  // src buffer
                               AV_PIX_FMT_NV12,         // pixel format
                               is->p_vcodec_ctx->width, // width
                               is->p_vcodec_ctx->height,// height
                               1                        // align
                               );
    if (ret < 0)
    {
        printf("av_image_fill_arrays() failed %d\n", ret);
        return -1;;
    }

    desc = av_pix_fmt_desc_get(is->p_vcodec_ctx->pix_fmt);
    printf("video prefix format : %s.\n", desc->name);

#if (SW_SCALE)
    is->img_convert_ctx = sws_getContext(is->p_vcodec_ctx->width,   // src width
                                         is->p_vcodec_ctx->height,  // src height
                                         is->p_vcodec_ctx->pix_fmt, // src format
                                         is->p_vcodec_ctx->width,   // dst width
                                         is->p_vcodec_ctx->height,  // dst height
                                         AV_PIX_FMT_NV12,           // dst format
                                         SWS_POINT,                 // flags
                                         NULL,                      // src filter
                                         NULL,                      // dst filter
                                         NULL                       // param
                                         );
    if (is->img_convert_ctx == NULL)
    {
        printf("sws_getContext() failed\n");
        return -1;
    }
#endif
#endif
    CheckFuncResult(pthread_create(&is->videoPlay_tid, NULL, video_playing_thread, is));

    return 0;
}

static int open_video_stream(player_stat_t *is)
{
    AVCodecParameters *p_codec_par = NULL;
    AVCodec *          p_codec     = NULL;
    AVCodecContext *   p_codec_ctx = NULL;
    AVStream *         p_stream    = is->p_video_stream;
    int                ret;

    p_codec_par = p_stream->codecpar;

    if (is->decoder_type == HARD_DECODING)
    {
        switch (p_codec_par->codec_id)
        {
        case AV_CODEC_ID_H264:
            p_codec = avcodec_find_decoder_by_name("ssh264");
            break;
            break;
        case AV_CODEC_ID_HEVC:
            p_codec = avcodec_find_decoder_by_name("sshevc");
            break;
            break;
        case AV_CODEC_ID_MJPEG:
            p_codec = avcodec_find_decoder_by_name("ssmjpeg");
            break;
            break;
        default:
            p_codec          = avcodec_find_decoder(p_codec_par->codec_id);
            is->decoder_type = SOFT_DECODING;
            break;
        }
    }
    else if (is->decoder_type == SOFT_DECODING)
    {
        switch (p_codec_par->codec_id)
        {
        case AV_CODEC_ID_H264:
            p_codec = avcodec_find_decoder_by_name("h264");
            break;
            break;
        case AV_CODEC_ID_HEVC:
            p_codec = avcodec_find_decoder_by_name("hevc");
            break;
            break;
        case AV_CODEC_ID_MJPEG:
            p_codec = avcodec_find_decoder_by_name("mjpeg");
            break;
            break;
        default:
            p_codec = avcodec_find_decoder(p_codec_par->codec_id);
            break;
        }
    }

    if (p_codec == NULL)
    {
        printf("Cann't find codec!\n");
        return -1;
    }
    printf("open codec: %s\n", p_codec->name);

    p_codec_ctx = avcodec_alloc_context3(p_codec);
    if (p_codec_ctx == NULL)
    {
        printf("avcodec_alloc_context3() failed\n");
        return -1;
    }
    ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
    if (ret < 0)
    {
        printf("avcodec_parameters_to_context() failed\n");
        return -1;
    }

    p_codec_ctx->flags  = p_codec_ctx->width;
    p_codec_ctx->flags2 = p_codec_ctx->height;

    ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
    if (ret < 0)
    {
        printf("avcodec_open2() failed %d\n", ret);
        return -1;
    }

    // if (!avcodec_is_open(p_codec_ctx) || !av_codec_is_decoder(p_codec_ctx->codec))
    //    printf("init avcodec failed!\n");

    is->p_vcodec_ctx         = p_codec_ctx;
    is->p_vcodec_ctx->debug  = true;
    is->p_vcodec_ctx->flags  = 0;
    is->p_vcodec_ctx->flags2 = 0;
    printf("codec width: %d,height: %d\n", is->p_vcodec_ctx->width, is->p_vcodec_ctx->height);

    // CheckFuncResult(sstar_enable_display(is->p_vcodec_ctx->width,is->p_vcodec_ctx->height, MAINWND_W, MAINWND_H));

    // CheckFuncResult(pthread_create(&is->videoDecode_tid, NULL, video_decode_thread, is));
    CheckFuncResult(pthread_create(&is->videoDecode_tid, NULL, video_decode_thread_VideoMode, is));

    return 0;
}

int open_video(player_stat_t *is)
{
    if (is->video_idx >= 0)
    {
        open_video_stream(is);
        sleep(1);
        open_video_playing(is);
    }

    return 0;
}
