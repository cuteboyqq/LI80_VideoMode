#ifndef __PLAYER_H__
#define __PLAYER_H__

// #include "wnc_adas.hpp" //Alister add 2024-03-12

#ifdef __cplusplus
extern "C"{
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
#include <libavutil/time.h>
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/opt.h>

#include "mi_common.h"
#include "mi_common_datatype.h"
#include "mi_sys.h"
#include "mi_sys_datatype.h"

#define SUCCESS 0
#define FAIL 1

#define CheckFuncResult(result)                                          \
    if (result != SUCCESS)                                               \
    {                                                                    \
        printf("[%s %d]exec function failed\n", __FUNCTION__, __LINE__); \
        return FAIL;                                                     \
    }

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 15

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

//===========alister===========
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <libavutil/frame.h>
// Node structure for the linked list
typedef struct Node {
    AVFrame* data;
    struct Node* next;
} Node;

// Queue structure
typedef struct Queue {
    Node* front;
    Node* rear;
    pthread_mutex_t mutex;
} Queue;


// // Main function to test the queue
// int main() {
//     Queue queue;
//     initQueue(&queue);

//     // Assume you have AVFrame* frame declared and initialized elsewhere
//     enqueue(&queue, frame);

//     // Dequeue and use the AVFrame* pointer
//     AVFrame* dequeuedFrame = dequeue(&queue);
//     if (dequeuedFrame != NULL) {
//         // Use the dequeued frame
//         // Remember to free the frame when you're done using it
//     }

//     return 0;
// }
//==============================

enum
{
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct
{
    double pts;
    double pts_drift;
    double last_updated;
    double speed;
    int    serial;
    int    paused;
    int *  queue_serial;
} play_clock_t;

typedef struct
{
    int                 freq;
    int                 channels;
    int64_t             channel_layout;
    enum AVSampleFormat fmt;
    int                 frame_size;
    int                 bytes_per_sec;
} audio_param_t;

typedef struct packet_queue_t
{
    AVPacketList *  first_pkt, *last_pkt;
    int             nb_packets;
    int             size;
    int64_t         duration;
    int             abort_request;
    int             serial;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} packet_queue_t;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct
{
    AVFrame *  frame;
    int        serial;
    double     pts;      /* presentation timestamp for the frame */
    double     duration; /* estimated duration of the frame */
    int64_t    pos;
    int        width;
    int        height;
    int        format;
    AVRational sar;
    int        uploaded;
    int        flip_v;
} frame_t;

typedef struct
{
    frame_t         queue[FRAME_QUEUE_SIZE];
    int             rindex;
    int             windex;
    int             size;
    int             max_size;
    int             keep_last;
    int             rindex_shown;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    packet_queue_t *pktq;
} frame_queue_t;

typedef struct
{
    char *           filename;
    AVFormatContext *p_fmt_ctx;
    AVStream *       p_audio_stream;
    AVStream *       p_video_stream;
    AVCodecContext * p_acodec_ctx;
    AVCodecContext * p_vcodec_ctx;

    int audio_idx;
    int video_idx;
    // sdl_video_t sdl_video;

    play_clock_t audio_clk;
    play_clock_t video_clk;
    play_clock_t extclk;
    double       frame_timer;

    packet_queue_t audio_pkt_queue;
    packet_queue_t video_pkt_queue;
    frame_queue_t  audio_frm_queue;
    frame_queue_t  video_frm_queue;

#if (SW_SCALE)
    struct SwsContext *img_convert_ctx;
#endif
    struct SwrContext *audio_swr_ctx;
    AVFrame *          p_frm_yuv;
    AVFrame *          pF; // patch for malloc(): memory corruption

    audio_param_t audio_param_src;
    audio_param_t audio_param_tgt;
    int           audio_hw_buf_size;
    uint8_t *     p_audio_frm;
    uint8_t *     audio_frm_rwr;
    unsigned int  audio_frm_size;
    unsigned int  audio_frm_rwr_size;
    int           audio_cp_index;
    int           audio_write_buf_size;
    double        audio_clock;
    int           audio_clock_serial;

    int     abort_request;
    int     paused;
    int     last_paused;
    int     read_pause_return;
    int     step;
    int     eof;
    int     audio_complete, video_complete;
    int     seek_req;
    int     seek_flags;
    int     av_sync_type;
    int64_t seek_pos;
    int64_t seek_rel;

    pthread_cond_t continue_read_thread;
    pthread_t      read_tid;

    pthread_t audioDecode_tid;
    pthread_t audioPlay_tid;
    pthread_t videoDecode_tid;
    pthread_t videoPlay_tid;

    int decoder_type;
    // Alister add 2024-03-12
    // WNC_ADAS *g_IpuIntfObject_player;
    // std::deque<AVFrame*> m_inputVideoFrameBuffer;
} player_stat_t;



int player_running(const char *p_input_file, char *type);
static player_stat_t *player_init(const char *p_input_file);
static int player_deinit(player_stat_t *is);
double get_clock(play_clock_t *c);
void set_clock_at(play_clock_t *c, double pts, int serial, double time);
void set_clock(play_clock_t *c, double pts, int serial);
void stream_toggle_pause(player_stat_t *is);
void stream_seek(player_stat_t *is, int64_t pos, int64_t rel, int seek_by_bytes);
// extern WNC_ADAS *g_IpuIntfObject; // Alister add 2024-03-08
void initQueue(Queue* queue);
void enqueue(Queue *queue, AVFrame *data);
AVFrame *dequeue(Queue *queue);
#ifdef __cplusplus
}
#endif

#endif
