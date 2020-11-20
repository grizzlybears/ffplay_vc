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
#include <signal.h>
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

#include <SDL.h>
#include <SDL_thread.h>


#include <assert.h>

#include "../utils/utils.h"


#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

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

static unsigned sws_flags = SWS_BICUBIC;


typedef struct MyAVPacketList {  // 扩展了 AVPacket，增加serial， 将来可以考虑改成继承 AVPacke，再套一个std::list
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
} MyAVPacketList;

class PacketQueue 
{
public:
    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int64_t duration;
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
        duration = 0;
        serial = 0;
        abort_request = 1;
    }

    // 接管pkt生命周期，put失败也释放掉
    int packet_queue_put( AVPacket* pkt);

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

typedef struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

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
    void sync_clock_to_slave( Clock* slave); // 把slave时钟同步到自己

    double pts;           /* clock base */
    int paused;
    int serial;           /* clock is based on a packet with this serial */

protected:
    double pts_drift;     // clock base minus time at which we updated the clock. pts相对于外界时钟的偏移。
    double last_updated;  // 外界时钟的‘打点’
    double speed;
    
    int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} ;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
    AVFrame *frame;
    AVSubtitle sub;
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t pos;          /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    AVRational sar;
    int uploaded;
    int flip_v;
} Frame;

class FrameQueue 
{
public:
    int frame_queue_init(PacketQueue* pktq, int max_size, int keep_last);
    void frame_queue_destory();
    void frame_queue_signal();

    Frame* frame_queue_peek();          // 获得‘读头’，读完之后用 frame_queue_next 移动‘读头’。无并发保护
    Frame* frame_queue_peek_next();     // 无并发保护 
    Frame* frame_queue_peek_last();     // 无并发保护
    Frame* frame_queue_peek_readable(); // 有并发保护

    void frame_queue_next();        //  ‘出队列’
    
    Frame* frame_queue_peek_writable();   // 获得‘写头’，写完之后用 frame_queue_push 移动‘写头’。有并发保护
    void frame_queue_push();              // ‘入队列’

    static void unref_item(Frame* vp);

    // return the number of undisplayed frames in the queue 
    int frame_queue_nb_remaining();

    // return last shown position 
    int64_t frame_queue_last_pos();


    PacketQueue* pktq;
    SimpleConditionVar cond;

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
                        // rindex + rindex_shown 是‘读头’后面一帧, 用 frame_queue_peek_next() 看。
    int windex;
    int size;
    int max_size;
    int keep_last;
    
};

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

class Decoder 
    :public BaseThread
{
public:
    virtual void decoder_init( AVCodecContext* avctx, PacketQueue* queue, SimpleConditionVar* empty_queue_cond);
    /// return: 
    //      negative    -- failed.
    //      0           -- EoF
    //      positive    -- got frame
    virtual int decoder_decode_frame(AVFrame* frame, AVSubtitle* sub);

    virtual void decoder_destroy();
    virtual void decoder_abort(FrameQueue* fq);
    virtual int decoder_start( int (*fn)(void*), const char* thread_name, void* arg);

    AVCodecContext* avctx; // take owner ship
    int pkt_serial;
    int finished;

    int64_t start_pts;
    AVRational start_pts_tb;

protected:    
    PacketQueue* queue; 

    AVPacket pending_pkt;
    int is_packet_pending;
    
    int64_t next_pts;
    AVRational next_pts_tb;

    SimpleConditionVar* empty_queue_cond;  // just ref, dont take owner ship
};

class VideoState
    :public BaseThread //  stream reader thread 
{
public:
    int open(const char* filename, AVInputFormat* iformat);
    // 关闭VS，释放内部资源
    void close();
    
    // called to display each frame 
    void video_refresh(double* remaining_time);

    void stream_cycle_channel(int codec_type);
public:
    int abort_request;
    int force_refresh;
    int paused;

    int queue_attachments_req; //某些流带有一个‘包含封面帧的包’，在初始load以及seek的时候，就先扔一个‘包含封面帧的包’进队
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int read_pause_return;  // 'reader loop'中， 遇到'pause' req, av_read_pause 这个API的返回值
    AVFormatContext * format_context;

    Clock audclk;
    Clock vidclk;
    Clock extclk;

    FrameQueue pictq;
    FrameQueue subpq;
    FrameQueue sampq;

    Decoder auddec;
    Decoder viddec;
    Decoder subdec;
       

    int av_sync_type;
    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity

    int audio_stream;
    double audio_clock;
    int audio_clock_serial;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
    uint8_t *audio_buf;
    uint8_t *audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    int audio_volume;
    int muted;
    struct AudioParams audio_src;
    struct AudioParams audio_tgt;  // audio_open 返回，环境要求的audio params
    struct SwrContext *swr_ctx;

    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;


    int frame_drops_early;
    int frame_drops_late;
    SDL_Texture *sub_texture;   // 字幕图片
    SDL_Texture *vid_texture;   // 视频图片 //todo: 这些应该移到Render中去

    int subtitle_stream;
    AVStream *subtitle_st;
    PacketQueue subtitleq;

    double frame_timer;
    double frame_last_returned_time;
    double frame_last_filter_delay;
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;    
    struct SwsContext *img_convert_ctx;
    struct SwsContext *sub_convert_ctx;
        
    int width, height, xleft, ytop;

    int eof;
    int step; // 单帧模式


    int last_video_stream, last_audio_stream, last_subtitle_stream;

    SimpleConditionVar continue_read_thread;

protected:    
    virtual unsigned run();  //  stream reader thread 
    static int decode_interrupt_cb(void* ctx);

    AVInputFormat* iformat;   // 指定容器格式，ref only
    char* filename; // 播放的文件 or url
    int realtime;   // 是否是实时流
    
    int last_paused; // 之前一次reader loop的时候，是否是paused


    // open a given stream. Return 0 if OK 
    int stream_component_open( int stream_index);
    void stream_component_close( int stream_index);


    // 'reader thread' section {{{
    int open_stream_file();
    int open_streams();

    int  read_loop_check_pause(); // return: > 0 -- shoud 'continue', 0 -- go on current iteration, < 0 -- error exit loop
    int  read_loop_check_seek();  // return: > 0 -- shoud 'continue', 0 -- go on current iteration, < 0 -- error exit loop

    int is_pkt_in_play_range( AVPacket* pkt);
    double stream_ts_to_second(int64_t ts, int stream_index);

    // }}} 'reader thread' section
};

/* options specified by the user */
extern AVInputFormat * opt_file_iformat;
extern const char * opt_input_filename;
extern const char * g_window_title;
extern int g_default_width  ;
extern int g_default_height ;

extern int opt_audio_disable;
extern int opt_subtitle_disable;
extern float opt_seek_interval;
extern int opt_alwaysontop;
extern int opt_startup_volume ;
extern int opt_show_status;
extern int opt_av_sync_type;
extern int64_t opt_start_time;  // 命令行 -ss ，由 av_parse_time 解析为 microseconds
extern int64_t opt_duration;    // 命令行 -t  ，由 av_parse_time 解析为 microseconds
extern int decoder_reorder_pts ;
extern int autoexit;
extern int opt_framedrop;
extern int opt_infinite_buffer;
extern const char * opt_audio_codec_name;
extern const char * opt_subtitle_codec_name;
extern const char * opt_video_codec_name;
extern double rdftspeed ;
extern int64_t cursor_last_shown;
extern int cursor_hidden ;

extern int autorotate ;
extern int opt_full_screen;

/* current context */
extern int64_t audio_callback_time;


#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)


extern const struct TextureFormatEntry {
    enum AVPixelFormat format;
    int texture_fmt;
} sdl_texture_format_map[];


inline int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2)
{
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

inline int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
    if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
        return channel_layout;
    else
        return 0;
}


class Render
{
public:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_RendererInfo renderer_info;
    SDL_AudioDeviceID audio_dev;
    int fullscreen;
    int screen_width;
    int screen_height;
    int screen_left;
    int screen_top;
    Render()
    {
        window = NULL;
        renderer = NULL;
        audio_dev = 0;
        renderer_info = { 0 };

        screen_width = 0;
        screen_height = 0;
        fullscreen = 0;

        screen_left = SDL_WINDOWPOS_CENTERED;
        screen_top = SDL_WINDOWPOS_CENTERED;
    }

    ~Render()
    {
        safe_release();
    }

    int init(int opt_audio_disable, int opt_alwaysontop);

    void toggle_full_screen();
    
    int create_window(const char* title, int x, int y, int w, int h, Uint32 flags);

    void show_window(const char * title, int w, int h, int left, int top, int fullscreen);

    void clear_render();
    void draw_render();

    void close_audio();

    void safe_release();

    void fill_rectangle(int x, int y, int w, int h);
    void set_default_window_size(int width, int height, AVRational sar);

    int realloc_texture(SDL_Texture** texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);

    static void get_sdl_pix_fmt_and_blendmode(int format, Uint32* sdl_pix_fmt, SDL_BlendMode* sdl_blendmode);
    static void set_sdl_yuv_conversion_mode(AVFrame* frame);
    static int upload_texture(SDL_Texture** tex, AVFrame* frame, struct SwsContext** img_convert_ctx);
};

extern Render g_render;


void calculate_display_rect(SDL_Rect* rect,
    int scr_xleft, int scr_ytop, int scr_width, int scr_height,
    int pic_width, int pic_height, AVRational pic_sar);

void video_image_display(VideoState* is);
Frame*  get_current_subtitle_frame(VideoState* is, Frame* current_video_frame);

inline int compute_mod(int a, int b)
{
    return a < 0 ? a%b + b : a%b;
}

//  }}} render section 



void do_exit(VideoState* is);
void sigterm_handler(int sig);

int video_open(VideoState* is);
/* display the current picture, if any */
void video_display(VideoState* is);


int get_master_sync_type(VideoState* is);
/* get the current master clock value */
double get_master_clock(VideoState* is);
void check_external_clock_speed(VideoState* is);



/* seek in the stream */
void stream_seek(VideoState* is, int64_t pos, int64_t rel, int seek_by_bytes);

/* pause or resume the video */
void stream_toggle_pause(VideoState* is);
void toggle_pause(VideoState* is);


void toggle_mute(VideoState* is);

void update_volume(VideoState* is, int sign, double step);

void step_to_next_frame(VideoState* is);

double compute_target_delay(double delay, VideoState* is);

double vp_duration(VideoState* is, Frame* vp, Frame* nextvp);

void update_video_pts(VideoState* is, double pts, int64_t pos, int serial);

void print_stream_status(VideoState* is);

int queue_picture(VideoState* is, AVFrame* src_frame, double pts, double duration, int64_t pos, int serial);

int get_video_frame(VideoState* is, AVFrame* frame);


int audio_thread(void* arg);

int video_thread(void* arg);

int subtitle_thread(void* arg);

/* copy samples for viewing in editor window */
void update_sample_display(VideoState* is, short* samples, int samples_size);

/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
int synchronize_audio(VideoState* is, int nb_samples);

/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
int audio_decode_frame(VideoState* is);

/* prepare a new audio buffer */
void sdl_audio_callback(void* opaque, Uint8* stream, int len);

int audio_open(void* opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams* audio_hw_params);



int stream_has_enough_packets(AVStream* st, int stream_id, PacketQueue* queue);

int is_realtime(AVFormatContext* s);


/* this thread gets the stream from the disk or the network */
int read_thread(void* arg);

void seek_chapter(VideoState* is, int incr);

