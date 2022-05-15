#pragma  once
/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

 /**
  * @file
  * simple media player based on the FFmpeg libraries
  */

#include "config.h"
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>

extern "C"
{
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavutil/bprint.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
}

#include <assert.h>

#ifdef _WIN32 
#include "../utils/utils.h"
#else
#include "../utils/thread_utils.h"
#endif 


#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define QUEUE_ENOUGH_TIME    (10.0)
#define QUEUE_ENOUGH_PKG     (25 * (int)QUEUE_ENOUGH_TIME )

#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1

#define SWS_FLAG_4_PIXELFORMAT_UNKNOWN  SWS_BICUBIC



typedef struct MyAVPacketListNode {  // 扩展了 AVPacket，增加serial， 将来可以考虑改成继承 AVPacke，再套一个std::list
    AVPacket pkt;
    struct MyAVPacketListNode* next;
    int serial;
} MyAVPacketListNode;

class PacketQueue
{
public:
    MyAVPacketListNode* first_pkt, * last_pkt;
    int nb_packets;
    int size;
    int64_t total_duration;  // sum of each packet's duration in q
    int abort_request;
    int serial;

    static AVPacket flush_pkt;

    int static is_flush_pkt(const AVPacket& to_check);

    SimpleConditionVar cond;

    PacketQueue()
    {
        first_pkt = NULL;
        last_pkt = NULL;
        nb_packets = 0;
        size = 0;
        total_duration = 0;
        serial = 0;
        abort_request = 1;
    }

    // 接管pkt生命周期，put失败也释放掉
    int packet_queue_put(AVPacket* pkt);

    // 清空queue
    void packet_queue_flush();

    void packet_queue_destroy();

    void packet_queue_abort();

    void packet_queue_start();

    // return < 0 if aborted, 0 if no packet and > 0 if packet.  
    int packet_queue_get(AVPacket* pkt, int block, /*out*/ int* serial);

    int packet_queue_put_nullpacket(int stream_index);

protected:
    int packet_queue_put_private(AVPacket* pkt);

};

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))


class Clock {
public:
    double get_clock();
    double get_last_set_point() const
    {
        return last_updated;
    }

    void set_clock_at(double pts, int serial, double time);
    void set_clock(double pts, int serial);
    void set_clock_speed(double speed);
    double get_clock_speed() const
    {
        return speed;
    }
    void init_clock(int* queue_serial);
    void sync_clock_to_slave(Clock* slave); // 把slave时钟同步到自己

    double pts;           /* clock base */
    int paused;
    int serial;           /* clock is based on a packet with this serial */

protected:
    double pts_drift;     // clock base minus time at which we updated the clock. pts相对于外界时钟的偏移。
    double last_updated;  // 外界时钟的‘打点’
    double speed;

    int* queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
};

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
    AVFrame* frame;
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t pos;          /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    AVRational sample_aspect_ratio;
    int uploaded;
    int flip_v;
} Frame;

class FrameQueue
{
public:
    int frame_queue_init(PacketQueue* pktq, int max_size, int keep_last);
    void frame_queue_destory();
    void frame_queue_signal();

    Frame* frame_queue_peek();          // 无并发保护，获得‘读头’,逻辑上‘接下来要上屏的帧’
    Frame* frame_queue_peek_next();     // 无并发保护 ,‘接下来要上屏的帧’ 的‘后一帧’
    Frame* frame_queue_peek_last();     // 无并发保护，逻辑上 ‘已经上屏的帧’

    Frame* frame_queue_peek_readable(); // 有并发保护

    void frame_queue_next();        //  移动‘读头’，aka ‘出队列’

    Frame* frame_queue_peek_writable();   // 获得‘写头’，写完之后用 frame_queue_push 移动‘写头’。有并发保护
    void frame_queue_push();              // ‘入队列’

    static void unref_item(Frame* vp);

    // return the number of undisplayed frames in the queue 
    int frame_queue_nb_remaining();

    // return last shown position 
    int64_t frame_queue_last_pos();


    PacketQueue* pktq;
    SimpleConditionVar fq_signal;

    int is_last_frame_shown() const
    {
        return rindex_shown;
    }

protected:
    Frame queue[FRAME_QUEUE_SIZE];
    int rindex;         // 最初的‘读头’
    int rindex_shown;   // rindex指向的位置，是否曾经被 shown过
                        // rindex + rindex_shown 构成逻辑上的‘读头’ 
                        // 如果keeplast (重画最后一帧需要)，那么第一次‘移动读头’不能动rindex, 而是要 rindex_shown = 1。
                        // 这样 rindex + rindex_shown 是‘读头’ , 用 frame_queue_peek() 看;
                        // rindex 是‘刚刚画过的一帧’, 用 frame_queue_peek_last() 看;
                        // rindex + rindex_shown + 1 是‘读头’后面一帧, 用 frame_queue_peek_next() 看。
    int windex;
    int size;
    int max_size;
    int keep_last;

};

AString av_strerror2(int err);

int is_realtime(AVFormatContext* s);

inline int compute_mod(int a, int b)
{
    return a < 0 ? a % b + b : a % b;
}

AString codec_para_2_str(const AVCodecParameters * codec_para);

