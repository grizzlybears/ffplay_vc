#pragma  once

#include "SimpleAvCommon.h"

#include <SDL.h>
#include <SDL_thread.h>

typedef struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

enum {
    AV_SYNC_AUDIO_MASTER = 0, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

class VideoState;
class SimpleAVDecoder;
class Render;

struct StreamParam // cache some initial param from AVStream
{
public:
    AVRational  time_base;
    int64_t     start_time;
    int         stream_index;   // AVPacket::stream_index.  
                                // 原ffplay的设计来自 AVFormat/AVStream，但是如果format和decoder分离，这里可以固定 V流用0， A流用1。
    AVRational  guessed_vframe_rate;  // 只有V流用到
};

class Decoder 
    :public BaseThread  //decoder thread
{
public:
    Decoder(SimpleAVDecoder* av_decoder)
    {
        _av_decoder = av_decoder;
        inited = 0; 
        avctx = NULL;
    }
    virtual ~Decoder() {}
    static AVCodecContext* create_codec(AVFormatContext* format_context, int stream_id);
    virtual int decoder_init( AVCodecContext* avctx, int stream_id, AVStream* stream, SimpleConditionVar* empty_queue_cond);
    virtual void decoder_destroy();

    virtual int decoder_start(); //启动decoder thread. 
    virtual void decoder_abort();  // 指示decoder thread 退出，并等待其退出


    int stream_has_enough_packets(); 

    /// return: 
    //      negative    -- failed.
    //      0           -- EoF
    //      positive    -- got frame
    virtual int decoder_decode_frame(AVFrame* frame, AVSubtitle* sub);
        

    AVCodecContext* avctx; // take owner ship
    int pkt_serial;        // 当前pkt的serial 
    int finished;

    int64_t start_pts;              // todo: 源自 _vs->format_context ，需要self-contain
    AVRational start_pts_timebase;

    int is_inited() const
    {
        return inited;
    }
    StreamParam  stream_param;

protected:
    int inited;
    SimpleAVDecoder* _av_decoder; 

    Render* get_render();
    
    AVPacket pending_pkt;  // avcodec_send_packet 遇到E_AGAIN，需要暂存
    int is_packet_pending;
    
    int64_t    next_pts;
    AVRational next_pts_timebase;

    SimpleConditionVar* empty_pkt_queue_cond;  // just ref, dont take owner ship

    virtual void on_got_new_frame(AVFrame* frame) = 0;

public:
    static void onetime_global_init();
    FrameQueue  frame_q;        // 原 VideoState:: pictq/sampq/subpq
    PacketQueue packet_q;       // 原 VideoState:: videoq/audioq/subtitleq
    //int         stream_id;      // 原 VideoState:: video_stream/audio_stream/subtitle_stream 
    //AVStream*   stream;         // 原 VideoState:: video_st/audio_st/subtitle_st   todo: 需要self-contain
    Clock       stream_clock;   // 原 VideoState:: vidclk/audclk/(null)
};

class VideoDecoder
    :public Decoder
{
public:
    typedef Decoder MyBase;
    VideoDecoder(SimpleAVDecoder* av_decoder):MyBase(av_decoder)
    {
        img_convert_ctx = NULL;
        width = height = xleft =  ytop = 0;
        stream_param.guessed_vframe_rate = av_make_q(25, 1); 
        frame_timer = 0;
    }

    int width, height, xleft, ytop;

    double frame_timer; // frame_timer 是‘当前显示帧’,理论上应该‘上屏’的时刻。 
                        // 比如 根据pts, 当前帧应该在 00:05:38.04 显示，但实际上'刷新显示操作'发生在 00:05:38.05，即物理上该帧于00:05:38.05‘上屏’
                        // 我们还是认为 frame_timer == 00:05:38.04
    struct SwsContext* img_convert_ctx;
    virtual int decoder_init(AVCodecContext* avctx, int stream_id, AVStream* stream, SimpleConditionVar* empty_queue_cond);
    virtual void decoder_destroy();

    virtual  ThreadRetType  thread_main();  // BaseThread method 

    int get_video_frame( AVFrame* frame);  // 返回 <0 表示退出解码线程
    int queue_picture(AVFrame* src_frame, double pts, double duration, int64_t pos, int serial);
       

    // display the current picture, if any 
    void video_display();
protected:
    
    virtual void on_got_new_frame(AVFrame* frame);
    int video_open(); // open the window for showing video
    void video_image_display();
};

class AudioDecoder
    :public Decoder
{
public:
    typedef Decoder MyBase;
    AudioDecoder(SimpleAVDecoder* av_decoder) :MyBase(av_decoder)
    {
        audio_buf = audio_buf1 = NULL;
        audio_buf_size = audio_buf1_size = 0;
        swr_ctx = NULL;   
        audio_callback_time = 0;
        audio_volume = 100;
    }
    virtual int decoder_init(AVCodecContext* avctx, int stream_id, AVStream* stream, SimpleConditionVar* empty_queue_cond);
    virtual  ThreadRetType thread_main();  // BaseThread method 
    virtual void decoder_destroy();

    int muted;
    int audio_volume;  //  volume [0 , 100]

    double audio_diff_cum; /* used for AV difference average computation, in sync_audio() */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;

    struct AudioParams audio_src;

    double audio_clock;         // pts of 'last decoded Frame' + frame duration, use this to update this->stream_clock in SDL cb.
    int audio_clock_serial;

protected:
    
    int audio_hw_buf_size;
    uint8_t* audio_buf;
    uint8_t* audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    unsigned int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    
    
    struct AudioParams audio_tgt;  // audio_open 返回，环境要求的audio params
    struct SwrContext* swr_ctx;

    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;

    /* current context */
    int64_t audio_callback_time;

    int audio_open(void* opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams* audio_hw_params);
    /* prepare a new audio buffer */
    static void sdl_audio_callback(void* opaque, Uint8* stream, int len);

    void handle_sdl_audio_cb(Uint8* stream, int len);
    
    /**
     * Decode one audio frame and return its uncompressed size.
     *
     * The processed audio frame is decoded, converted if required, and
     * stored in this->audio_buf, with size in bytes given by the return
     * value.
     */
    int audio_decode_frame();

    virtual void on_got_new_frame(AVFrame* frame);
};

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

    int64_t cursor_last_shown;
    int cursor_hidden;

    CString window_title;
    SDL_Texture* sub_texture;   // 字幕画布
    SDL_Texture* vid_texture;   // 视频画布

    Render()
    {
        window = NULL;
        renderer = NULL;
        vid_texture = sub_texture = NULL;
        audio_dev = 0;
        renderer_info = { 0 };

        screen_width = 640; //default value
        screen_height = 480;//default value
        screen_left = SDL_WINDOWPOS_CENTERED;
        screen_top = SDL_WINDOWPOS_CENTERED;

        fullscreen = 0;
        cursor_hidden = 0; 
        cursor_last_shown = 0;
    }

    ~Render()
    {
        safe_release();
    }

    int init(int audio_disable, int alwaysontop);

    void toggle_full_screen();

    int create_window(const char* title, int x, int y, int w, int h, Uint32 flags);

    void show_window(const char* title, int w, int h, int left, int top, int fullscreen);

    int upload_texture(SDL_Texture** tex, AVFrame* frame, struct SwsContext** img_convert_ctx);

    void show_texture(const Frame* video_frame, const SDL_Rect& rect, int show_subtitle);

    void clear_render();
    void draw_render();

    void close_audio();

    void safe_release();

    void fill_rectangle(int x, int y, int w, int h);
    void set_default_window_size(int width, int height, AVRational sar);

    int realloc_texture(SDL_Texture** texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);

    static void get_sdl_pix_fmt_and_blendmode(int format, Uint32* sdl_pix_fmt, SDL_BlendMode* sdl_blendmode);
    static void set_sdl_yuv_conversion_mode(AVFrame* frame);
    
    static void calculate_display_rect(SDL_Rect* rect,
        int scr_xleft, int scr_ytop, int scr_width, int scr_height,
        int pic_width, int pic_height, AVRational pic_sar);
};

class SimpleAVDecoder
{
public:
    SimpleAVDecoder(VideoState * vs)
        :auddec(this), viddec(this)
    {
        this->extclk.init_clock(&this->extclk.serial);
        frame_drops_early = frame_drops_late = 0;
        paused = step = 0;
        force_refresh = 0;
        realtime = 0;
        show_status = -1;
        decoder_reorder_pts = -1;
    }

    Render render;
    
    // 返回 bit0 代表V opened ， bit1 代表A opened 
    int   open_stream_from_avformat(AVFormatContext* format_context, /*in,hold*/SimpleConditionVar* notify_reader, int* vstream_id, int* astream_id);
    int   open_stream(); // todo: 需要给出的参数是 A/V， codec_id, StreamParam 
    int   get_opened_streams_mask();   // 返回 bit0 代表V， bit1 代表A
    void  close_all_stream();

    Clock extclk;
    void check_external_clock_speed();  // 调节外部时钟速度以适应流速

    AudioDecoder    auddec;
    VideoDecoder    viddec;

    // called to display each frame (from event loop )
    void video_refresh(double* remaining_time);
    void prepare_picture_for_display(double* remaining_time);


    int get_master_sync_type();
    int av_sync_type;
    int realtime;   // 是否是实时流

    /* get the current master clock value */
    double get_master_clock();

    // return the wanted number of samples to get better sync if sync_type is video or external master clock
    int synchronize_audio(int nb_samples);

    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    
    // {{  some ffplay cmd line opt  
    int decoder_reorder_pts;
    int show_status;
    // }}  some ffplay cmd line opt  

    int frame_drops_early;
    int frame_drops_late;

    int force_refresh;   // 是否需要‘draw frame’

    // decoder status section {{
    int paused;
    int internal_toggle_pause();

    int step; // 单帧模式

    void toggle_mute();
    void update_volume(int sign, double step);
    // }} decoder status section

    void discard_buffer(double seek_target = NAN); // 用于在seek后清cache。如果是按时间seek，则应顺手给出 seek_target (以秒为单位)
    int  is_buffer_full();
    void feed_null_pkt(); // todo: 作用不明，待研究
    void feed_pkt(AVPacket* pkt); // 向解码器喂数据包, take ownership。如果不是感兴趣的包，则释放。
protected:
    
    void print_stream_status();

    double vp_duration(Frame* vp, Frame* nextvp); //  refs 'max_frame_duration'
    double compute_target_delay(double delay);
    void update_video_clock(double pts, int64_t pos, int serial);

};

class VideoState
    :public BaseThread //  stream reader thread 
{
public:
    VideoState()
        :av_decoder(this)
    {
        format_context = NULL;
        eof = 0;
        abort_request = 0;
        paused = 0; 
        last_paused = 0;
        seek_req = 0;
        infinite_buffer = -1;
        streamopt_start_time = streamopt_duration = AV_NOPTS_VALUE;
        streamopt_autoexit = 0;
    }

    int open_input_stream(const char* filename, AVInputFormat* iformat);
    
    void close_input_stream(); 

    // {{{ stream operation section
    void stream_seek( int64_t pos, int64_t rel, int seek_by_bytes);

    void toggle_pause();
    
    void step_to_next_frame();

    void seek_chapter( int incr);

    // }}} stream operation section
        
public:
    // format status section {{
    int paused;
    // }}  format status section

    int abort_request;

    // {{  some ffplay cmd line opt  
    
    int64_t streamopt_start_time;  // 命令行 -ss ，由 av_parse_time 解析为 microseconds
    int64_t streamopt_duration;    // 命令行 -t  ，由 av_parse_time 解析为 microseconds
    int     streamopt_autoexit;
    // }}

    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    AVFormatContext * format_context;

    SimpleAVDecoder av_decoder;

    int eof;

    int last_video_stream, last_audio_stream ;

    SimpleConditionVar continue_read_thread;

protected:    
    int infinite_buffer; 

    virtual  ThreadRetType thread_main();  //  stream reader thread 
    static int decode_interrupt_cb(void* ctx);

    AVInputFormat* iformat;   // 命令行指定容器格式，ref only
    CString file_to_play; // 播放的文件 or url
    
    int last_paused; // 之前一次reader loop的时候，是否是paused

    int open_stream_file();

    // 'reader thread' section {{{
    int  read_loop_check_pause(); // return: > 0 -- shoud 'continue', 0 -- go on current iteration, < 0 -- error exit loop
    int  read_loop_check_seek();  // return: > 0 -- shoud 'continue', 0 -- go on current iteration, < 0 -- error exit loop

    int is_pkt_in_play_range( AVPacket* pkt);
    double stream_ts_to_second(int64_t ts, int stream_index);

    // }}} 'reader thread' section
};


#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

extern const struct TextureFormatEntry {
    enum AVPixelFormat format;
    int texture_fmt;
} sdl_texture_format_map[];


/**
 * Print an error message to stderr, indicating filename and a human
 * readable description of the error code err.
 *
 * If strerror_r() is not available the use of this function in a
 * multithreaded application may be unsafe.
 *
 * @see av_strerror()
 */
void print_error(const char* filename, int err);
