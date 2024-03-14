#include "ssplayer_player.h"
#include "ssplayer_packet.h"
#include "ssplayer_frame.h"
#include "ssplayer_audio.h"

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

// sdk audio input/outout param
#define AUDIO_INPUT_SAMPRATE 48000
#define AUDIO_INPUT_CHLAYOUT AV_CH_LAYOUT_MONO
#define AUDIO_INPUT_SAMPFMT AV_SAMPLE_FMT_S16

#define AUDIO_OUTPUT_SAMPRATE E_MI_AUDIO_SAMPLE_RATE_48000
#define AUDIO_OUTPUT_CHLAYOUT E_MI_AUDIO_SOUND_MODE_MONO
#define AUDIO_OUTPUT_SAMPFMT E_MI_AUDIO_BIT_WIDTH_16

#define MI_AUDIO_SAMPLE_PER_FRAME 1024

#define MI_AUDIO_MAX_DATA_SIZE 25000

#define MI_AUDIO_MAX_SAMPLES_PER_FRAME 2048
#define MI_AUDIO_MAX_FRAME_NUM 6

#define MI_AO_PCM_BUF_SIZE_BYTE (MI_AUDIO_MAX_SAMPLES_PER_FRAME * MI_AUDIO_MAX_FRAME_NUM * 2 * 4)

static int  fda;
static void sdl_audio_callback(void *opaque, uint8_t *stream, int len);
extern AVPacket flush_pkt;

// 从packet_queue中取一个packet，解码生成frame
static int audio_decode_frame(AVCodecContext *p_codec_ctx, packet_queue_t *p_pkt_queue, AVFrame *frame)
{
    int ret;

    while (1)
    {
        AVPacket pkt;

        while (1)
        {
            ret = avcodec_receive_frame(p_codec_ctx, frame);
            if (ret >= 0)
            {
                AVRational tb = (AVRational){1, frame->sample_rate};
                if (frame->pts != AV_NOPTS_VALUE)
                {
                    // printf("frame pts before convert : %d.\n", frame->pts);
                    frame->pts = av_rescale_q(frame->pts, p_codec_ctx->pkt_timebase, tb);
                }
                else
                {
                    // av_log(NULL, AV_LOG_WARNING, "frame->pts no\n");
                }

                return 1;
            }
            else if (ret == AVERROR_EOF)
            {
                av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): the decoder has been flushed\n");
                avcodec_flush_buffers(p_codec_ctx);
                return 0;
            }
            else if (ret == AVERROR(EAGAIN))
            {
                // av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): input is not accepted in the current
                // state\n");
                break;
            }
            else
            {
                av_log(NULL, AV_LOG_ERROR, "audio avcodec_receive_frame(): other errors\n");
                continue;
            }
        }
        if (packet_queue_get(p_pkt_queue, &pkt, true) < 0)
        {
            printf("audio packet_queue_get fail\n");
            return -1;
        }
        if (pkt.data == flush_pkt.data)
        {
            avcodec_flush_buffers(p_codec_ctx);
        }
        else
        {
            if (avcodec_send_packet(p_codec_ctx, &pkt) == AVERROR(EAGAIN))
            {
                av_log(NULL, AV_LOG_ERROR,
                       "receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
            }
            av_packet_unref(&pkt);
        }
    }
}

static int audio_decode_thread(void *arg)
{
    player_stat_t *is      = (player_stat_t *)arg;
    AVFrame *      p_frame = av_frame_alloc();
    frame_t *      af;

    int        got_frame = 0;
    AVRational tb;
    int        ret = 0;

    if (p_frame == NULL)
    {
        return AVERROR(ENOMEM);
    }

    while (1)
    {
        if (is->abort_request)
        {
            printf("audio decode thread exit\n");
            break;
        }
        got_frame = audio_decode_frame(is->p_acodec_ctx, &is->audio_pkt_queue, p_frame);
        if (got_frame < 0)
        {
            goto the_end;
        }

        if (got_frame)
        {
            tb = (AVRational){1, p_frame->sample_rate};

            if (!(af = frame_queue_peek_writable(&is->audio_frm_queue)))
                goto the_end;

            af->pts = (p_frame->pts == AV_NOPTS_VALUE) ? NAN : p_frame->pts * av_q2d(tb);

            af->pos      = p_frame->pkt_pos;
            af->duration = av_q2d((AVRational){p_frame->nb_samples, p_frame->sample_rate});
            av_frame_move_ref(af->frame, p_frame);
            frame_queue_push(&is->audio_frm_queue);
            // no need unref frame?
        }
    }

the_end:
    av_frame_free(&p_frame);
    return ret;
}

int open_audio_stream(player_stat_t *is)
{
    AVCodecContext *   p_codec_ctx;
    AVCodecParameters *p_codec_par = NULL;
    AVCodec *          p_codec     = NULL;
    int                ret;
    p_codec_par = is->p_audio_stream->codecpar;
    p_codec     = avcodec_find_decoder(p_codec_par->codec_id);
    if (p_codec == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "Cann't find codec!\n");
        return -1;
    }
    p_codec_ctx = avcodec_alloc_context3(p_codec);
    if (p_codec_ctx == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3() failed\n");
        return -1;
    }
    ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context() failed %d\n", ret);
        return -1;
    }
    ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avcodec_open2() failed %d\n", ret);
        return -1;
    }

    p_codec_ctx->pkt_timebase = is->p_audio_stream->time_base;
    is->p_acodec_ctx          = p_codec_ctx;

    CheckFuncResult(pthread_create(&is->audioDecode_tid, NULL, audio_decode_thread, is));

    return 0;
}

static int audio_resample(player_stat_t *is, int64_t audio_callback_time)
{
    int              data_size, resampled_data_size;
    int64_t          dec_channel_layout;
    av_unused double audio_clock0;
    int              wanted_nb_samples;
    frame_t *        af = NULL;
    frame_queue_t *  f  = &is->audio_frm_queue;

    if (is->paused)
        return -1;

    pthread_mutex_lock(&f->mutex);
    while (f->size - f->rindex_shown <= 0 && !f->pktq->abort_request)
    {
        printf("wait for audio frame\n");
        if (!is->audio_complete && is->eof && is->audio_pkt_queue.nb_packets == 0)
        {
            is->audio_complete = 1;
            if (is->audio_complete && is->video_complete)
                stream_seek(is, is->p_fmt_ctx->start_time, 0, 0);
        }
        pthread_cond_wait(&f->cond, &f->mutex);
    }
    pthread_mutex_unlock(&f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    af = &f->queue[(f->rindex + f->rindex_shown) % f->max_size];

    frame_queue_next(&is->audio_frm_queue);
    data_size = av_samples_get_buffer_size(NULL, af->frame->channels, af->frame->nb_samples, af->frame->format, 1);

    dec_channel_layout = (af->frame->channel_layout
                          && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout))
                             ? af->frame->channel_layout
                             : av_get_default_channel_layout(af->frame->channels);
    wanted_nb_samples = af->frame->nb_samples;
    if (af->frame->format != is->audio_param_src.fmt || dec_channel_layout != is->audio_param_src.channel_layout
        || af->frame->sample_rate != is->audio_param_src.freq)
    {
        swr_free(&is->audio_swr_ctx);
        // printf("in layout: %lld,format: %d,samrate:
        // %d\n",dec_channel_layout,af->frame->format,af->frame->sample_rate);
        // printf("out layout: %lld,format: %d,samrate:
        // %d\n",is->audio_param_tgt.channel_layout,is->audio_param_tgt.fmt,is->audio_param_tgt.freq);
        is->audio_swr_ctx = swr_alloc_set_opts(NULL, is->audio_param_tgt.channel_layout, is->audio_param_tgt.fmt,
                                               is->audio_param_tgt.freq, dec_channel_layout, af->frame->format,
                                               af->frame->sample_rate, 0, NULL);
        if (!is->audio_swr_ctx || swr_init(is->audio_swr_ctx) < 0)
        {
            av_log(
                NULL, AV_LOG_ERROR,
                "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                af->frame->sample_rate, av_get_sample_fmt_name(af->frame->format), af->frame->channels,
                is->audio_param_tgt.freq, av_get_sample_fmt_name(is->audio_param_tgt.fmt),
                is->audio_param_tgt.channels);
            swr_free(&is->audio_swr_ctx);
            return -1;
        }
        is->audio_param_src.channel_layout = dec_channel_layout;
        is->audio_param_src.channels       = af->frame->channels;
        is->audio_param_src.freq           = af->frame->sample_rate;
        is->audio_param_src.fmt            = af->frame->format;
    }

    if (is->audio_swr_ctx)
    {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;

        uint8_t **out       = &is->audio_frm_rwr;
        int       out_count = (int64_t)wanted_nb_samples * is->audio_param_tgt.freq / af->frame->sample_rate + 256;
        int       out_size =
            av_samples_get_buffer_size(NULL, is->audio_param_tgt.channels, out_count, is->audio_param_tgt.fmt, 0);
        int len2;
        if (out_size < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }

        av_fast_malloc(&is->audio_frm_rwr, &is->audio_frm_rwr_size, out_size);
        if (!is->audio_frm_rwr)
        {
            return AVERROR(ENOMEM);
        }
        len2 = swr_convert(is->audio_swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }

        if (len2 == out_count)
        {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->audio_swr_ctx) < 0)
                swr_free(&is->audio_swr_ctx);
        }

        is->p_audio_frm = is->audio_frm_rwr;

        resampled_data_size = len2 * is->audio_param_tgt.channels * av_get_bytes_per_sample(is->audio_param_tgt.fmt);
    }
    else
    {
        is->p_audio_frm     = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    if (!isnan(af->pts))
    {
        is->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
    }
    else
    {
        is->audio_clock = NAN;
    }
    // printf("after pts: %lf,clock: %lf\n",af->pts,is->audio_clock);
    is->audio_clock_serial = af->serial;

#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n", is->audio_clock - last_clock, is->audio_clock,
               audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}

static int audio_playing_thread(void *arg)
{
    player_stat_t *is = (player_stat_t *)arg;
    int            audio_size, len1, len;
    int            pause      = 0;
    int            last_pause = 0;

    while (1)
    {
        if (is->abort_request)
        {
            printf("audio play thread exit\n");
            break;
        }
        int64_t audio_callback_time = av_gettime_relative();

        audio_size = audio_resample(is, audio_callback_time);
        if (audio_size < 0)
        {
            /* if error, just output silence */

            pause = 1;
            if (pause != last_pause)
            {
                last_pause = pause;
                MI_AO_PauseChn(0, 0);
            }
            is->p_audio_frm = NULL;
            is->audio_frm_size =
                SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_param_tgt.frame_size * is->audio_param_tgt.frame_size;
        }
        else
        {
            pause = 0;
            if (pause != last_pause)
            {
                last_pause = pause;
                MI_AO_ResumeChn(0, 0);
            }
            is->audio_frm_size = audio_size;
        }

        if (is->p_audio_frm != NULL)
        {
            // memcpy(stream, (uint8_t *)is->p_audio_frm + is->audio_cp_index, len1);
            // printf("save audio len: %d\n",is->audio_frm_size);
            // write(fda, (uint8_t *)is->p_audio_frm + is->audio_cp_index, len1);
            // put audio stream to ss player
            int data_idx = 0, data_len = is->audio_frm_size;

            MI_AUDIO_Frame_t stAoSendFrame;
            MI_S32           s32RetSendStatus = 0;
            MI_AUDIO_DEV     AoDevId          = 0;
            MI_AO_CHN        AoChn            = 0;

            do
            {
                if (data_len <= MI_AUDIO_MAX_DATA_SIZE)
                {
                    stAoSendFrame.u32Len[0] = data_len;
                    stAoSendFrame.u32Len[1] = 0;
                }
                else
                {
                    stAoSendFrame.u32Len[0] = MI_AUDIO_MAX_DATA_SIZE;
                    stAoSendFrame.u32Len[1] = 0;
                }
                stAoSendFrame.apVirAddr[0] = &is->p_audio_frm[data_idx];
                stAoSendFrame.apVirAddr[1] = NULL;

                data_len -= MI_AUDIO_MAX_DATA_SIZE;
                data_idx += MI_AUDIO_MAX_DATA_SIZE;

                // read data and send to AO module
                // stAoSendFrame.u32Len = is->audio_frm_size;
                // stAoSendFrame.apVirAddr[0] = is->p_audio_frm;
                // stAoSendFrame.apVirAddr[1] = NULL;

                do
                {
                    s32RetSendStatus = MI_AO_SendFrame(AoDevId, AoChn, &stAoSendFrame, 128);
                } while (s32RetSendStatus == MI_AO_ERR_NOBUF);

                if (s32RetSendStatus != MI_SUCCESS)
                {
                    printf("[Warning]: MI_AO_SendFrame fail, error is 0x%x: \n", s32RetSendStatus);
                }
            } while (data_len > 0);
            // frame_queue_next(&is->audio_frm_queue);
        }

        is->audio_write_buf_size = is->audio_frm_size - is->audio_cp_index;
        if (!isnan(is->audio_clock))
        {
            set_clock_at(&is->audio_clk,
                         is->audio_clock - (double)(is->audio_hw_buf_size) / is->audio_param_tgt.bytes_per_sec,
                         is->audio_clock_serial, audio_callback_time / 1000000.0);
            // printf("audio clk: %lf,curtime: %ld,audio_callback_time:
            // %ld\n",is->audio_clock,is->audio_clock_serial,audio_callback_time);
            // printf("update clk pts: %lf,lud: %lf,dif:
            // %lf\n",is->audio_clk.pts,is->audio_clk.last_updated,is->audio_clk.pts_drift);
        }
    }

    return 0;
}
int ss_ao_Deinit(void)
{
    MI_AUDIO_DEV AoDevId = 0;
    MI_AO_CHN    AoChn   = 0;

    /* disable ao channel of */
    CheckFuncResult(MI_AO_DisableChn(AoDevId, AoChn));

    /* disable ao device */
    CheckFuncResult(MI_AO_Disable(AoDevId));

    return 0;
}

int ss_ao_Init(void)
{
    MI_AUDIO_Attr_t stSetAttr;
    MI_AUDIO_Attr_t stGetAttr;
    MI_AUDIO_DEV    AoDevId = 0;
    MI_AO_CHN       AoChn   = 0;

    MI_S32 s32SetVolumeDb;
    MI_S32 s32GetVolumeDb;

    // set Ao Attr struct
    memset(&stSetAttr, 0, sizeof(MI_AUDIO_Attr_t));
    stSetAttr.eBitwidth      = AUDIO_OUTPUT_SAMPFMT;
    stSetAttr.eWorkmode      = E_MI_AUDIO_MODE_I2S_MASTER;
    stSetAttr.u32FrmNum      = 6;
    stSetAttr.u32PtNumPerFrm = MI_AUDIO_SAMPLE_PER_FRAME;
    stSetAttr.u32ChnCnt      = 1;

    if (stSetAttr.u32ChnCnt == 2)
    {
        stSetAttr.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;
    }
    else if (stSetAttr.u32ChnCnt == 1)
    {
        stSetAttr.eSoundmode = AUDIO_OUTPUT_CHLAYOUT;
    }

    stSetAttr.eSamplerate = AUDIO_OUTPUT_SAMPRATE;

    /* set ao public attr*/
    CheckFuncResult(MI_AO_SetPubAttr(AoDevId, &stSetAttr));

    /* get ao device*/
    CheckFuncResult(MI_AO_GetPubAttr(AoDevId, &stGetAttr));

    /* enable ao device */
    CheckFuncResult(MI_AO_Enable(AoDevId));

    /* enable ao channel of device*/
    CheckFuncResult(MI_AO_EnableChn(AoDevId, AoChn));

    /* if test AO Volume */
    s32SetVolumeDb = -10;
    CheckFuncResult(MI_AO_SetVolume(AoDevId, 0, s32SetVolumeDb, E_MI_AO_GAIN_FADING_OFF));
    CheckFuncResult(MI_AO_SetVolume(AoDevId, 0, s32SetVolumeDb, E_MI_AO_GAIN_FADING_OFF));
    /* get AO volume */
    CheckFuncResult(MI_AO_GetVolume(AoDevId, 0, &s32GetVolumeDb));

    return SUCCESS;
}

static int open_audio_playing(void *arg)
{
    player_stat_t *is = (player_stat_t *)arg;

    is->audio_param_tgt.fmt            = AUDIO_INPUT_SAMPFMT;
    is->audio_param_tgt.freq           = AUDIO_INPUT_SAMPRATE;
    is->audio_param_tgt.channel_layout = AUDIO_INPUT_CHLAYOUT;

    is->audio_param_tgt.channels = av_get_channel_layout_nb_channels(is->audio_param_tgt.channel_layout);

    is->audio_param_tgt.frame_size =
        av_samples_get_buffer_size(NULL, is->audio_param_tgt.channels, 1, is->audio_param_tgt.fmt, 1);

    is->audio_param_tgt.bytes_per_sec = av_samples_get_buffer_size(
        NULL, is->audio_param_tgt.channels, is->audio_param_tgt.freq, is->audio_param_tgt.fmt, 1);

    if (is->audio_param_tgt.bytes_per_sec <= 0 || is->audio_param_tgt.frame_size <= 0)
    {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    is->audio_param_src   = is->audio_param_tgt;
    is->audio_hw_buf_size = MI_AO_PCM_BUF_SIZE_BYTE; // is->audio_param_tgt.frame_size;
    is->audio_frm_size    = 0;
    is->audio_cp_index    = 0;

    CheckFuncResult(pthread_create(&is->audioPlay_tid, NULL, audio_playing_thread, is));

    return 0;
}

static void sdl_audio_callback(void *opaque, uint8_t *stream, int len)
{
    player_stat_t *is = (player_stat_t *)opaque;
    int            audio_size, len1;

    int64_t audio_callback_time = av_gettime_relative();

    while (len > 0)
    {
        if (is->audio_cp_index >= (int)is->audio_frm_size)
        {
            audio_size = audio_resample(is, audio_callback_time);
            if (audio_size < 0)
            {
                /* if error, just output silence */
                is->p_audio_frm = NULL;
                is->audio_frm_size =
                    SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_param_tgt.frame_size * is->audio_param_tgt.frame_size;
            }
            else
            {
                is->audio_frm_size = audio_size;
            }
            is->audio_cp_index = 0;
        }
        len1 = is->audio_frm_size - is->audio_cp_index;
        if (len1 > len)
        {
            len1 = len;
        }
        if (is->p_audio_frm != NULL)
        {
            memcpy(stream, (uint8_t *)is->p_audio_frm + is->audio_cp_index, len1);
        }
        else
        {
            memset(stream, 0, len1);
        }

        len -= len1;
        stream += len1;
        is->audio_cp_index += len1;
    }
    is->audio_write_buf_size = is->audio_frm_size - is->audio_cp_index;
    if (!isnan(is->audio_clock))
    {
        set_clock_at(&is->audio_clk, is->audio_clock
                                         - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size)
                                               / is->audio_param_tgt.bytes_per_sec,
                     is->audio_clock_serial, audio_callback_time / 1000000.0);
    }
}

int open_audio(player_stat_t *is)
{
    if (is->audio_idx >= 0)
    {
        // init sdk audio
        CheckFuncResult(ss_ao_Init());

        open_audio_stream(is);

        open_audio_playing(is);
    }

    return 0;
}