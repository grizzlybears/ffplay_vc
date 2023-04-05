#pragma  once

#include "SimpleAvCommon.h"

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
class RenderBase;

typedef enum   // SimpleAVDecoder spec : V stream is 1, A stream is 2
{
    PSI_BAD   =  0,
    PSI_VIDEO =  1,
    PSI_AUDIO =  2,
}PsuedoStreamId ; 

struct AVPacketExtra  // 'decoder' could be seperated from 'parser' (even on diff host).
                      // we may need some extra info when feeding AVPacket to 'decoder'
{
    int v_or_a; // must be PsuedoStreamId 

    AVPacketExtra()
        : v_or_a(PSI_BAD)
    {
    }
};

struct StreamParam // cache some initial param from AVStream
{
public:
    AVRational  time_base;      // min time unit, from ffmoeg
    int64_t     start_time;
    AVRational  guessed_vframe_rate;  // only used in V stream
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
        eos = 0;
    }
    virtual ~Decoder() {}
    friend SimpleAVDecoder;

    static AVCodecContext* create_codec_directly( const AVCodecParameters * codec_para, const StreamParam* extra_para );
    virtual int decoder_init( AVCodecContext* avctx, const StreamParam* extra_para);
    virtual void decoder_destroy();

    int v_or_a; // must be PsuedoStreamId 
    int eos;
    int is_inited() const
    {
        return inited;
    }
    static void onetime_global_init();
   
protected:
    int inited;
    StreamParam  stream_param;
    SimpleAVDecoder* _av_decoder; 
    AVCodecContext* avctx; // take owner ship
    
    FrameQueue  frame_q;        //  VideoState:: pictq/sampq/subpq
    PacketQueue packet_q;       //  VideoState:: videoq/audioq/subtitleq
    Clock       stream_clock;   //  VideoState:: vidclk/audclk/(null)

    RenderBase* get_render();
    
    AVPacket pending_pkt;  // when avcodec_send_packet returns E_AGAIN，we need to hold the pkt.
    int is_packet_pending;
    int pkt_serial;        // 'serial' of current pkt  
    int finished;
    
    int64_t start_pts; 
    AVRational start_pts_timebase;
    int64_t    next_pts;
    AVRational next_pts_timebase;

    virtual void on_got_new_frame(AVFrame* frame) = 0;
 
    virtual int decoder_start();  // start decoder thread. 
    virtual void decoder_abort(); // signal decoder thread to quit, and wait.

    int buffered_enough_packets();  
    /// return: 
    //      negative    -- failed.
    //      0           -- EoF
    //      positive    -- got frame
    virtual int decoder_decode_frame(AVFrame* frame, AVSubtitle* sub);
};

class VideoDecoder
    :public Decoder
{
public:
    typedef Decoder MyBase;
    VideoDecoder(SimpleAVDecoder* av_decoder):MyBase(av_decoder)
    {
        stream_param.guessed_vframe_rate = av_make_q(25, 1); 
        frame_timer = 0; 
        v_or_a = PSI_VIDEO;
    }
    friend SimpleAVDecoder;

    virtual int decoder_init(AVCodecContext* avctx, const StreamParam* extra_para);
    virtual void decoder_destroy();

protected:
    
    virtual void on_got_new_frame(AVFrame* frame);
    void video_image_display(); 
    
    double frame_timer; // when SHOULD ‘current showing picture’ be put on screen.
                        // i.e.  'pts' of ‘current showing picture’ is 00:05:38.04,
                        // then even it was put on to screen at 00:05:38.05
                        // still we have  frame_timer == 00:05:38.04
    
    virtual  ThreadRetType  thread_main();  // BaseThread method 
    
    void video_display(); // display the current picture, if any  
    
    int get_video_frame( AVFrame* frame);  //  <0 means 'quit decorder thread'
    int queue_picture(AVFrame* src_frame, double pts, double duration, int64_t pos, int serial);
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
        v_or_a = PSI_AUDIO;
    }
    friend SimpleAVDecoder;
    virtual int decoder_init(AVCodecContext* avctx, const StreamParam* extra_para);
    virtual void decoder_destroy();

    void handle_audio_cb(uint8_t* stream, int len);

protected:
    int muted;
    int audio_volume;  //  volume [0 , 100]

    double audio_diff_cum; /* used for AV difference average computation, in sync_audio() */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;

    struct AudioParams audio_src;

    double audio_clock;         // pts of 'last decoded Frame' + frame duration, use this to update this->stream_clock in SDL cb.
    int audio_clock_serial;
   
    int audio_hw_buf_size;
    uint8_t* audio_buf;
    uint8_t* audio_buf1;    // if 'convertion' is needed,  here is the converted audio buf
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    unsigned int audio_buf_index; // in bytes, 'written head' in audio_buf */
    
    struct AudioParams audio_tgt;  // filled by 'audio_open', hw env demanded 'audio params'
    struct SwrContext* swr_ctx;

    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;

    /* current context */
    int64_t audio_callback_time;
    
    /**
     * Decode one audio frame and return its uncompressed size.
     *
     * The processed audio frame is decoded, converted if required, and
     * stored in this->audio_buf, with size in bytes given by the return
     * value.
     */
    int audio_decode_frame();

    virtual void on_got_new_frame(AVFrame* frame);
    
    virtual  ThreadRetType thread_main();  // BaseThread method 
};

class RenderBase
{
public:   
    RenderBase()
    { 
        inited = 0;
    }

    virtual ~RenderBase()
    {
    }

    static  RenderBase* create_sdl_render();
    
    virtual int init(int audio_disable, int alwaysontop) = 0; 
    virtual int is_initialized() const
    {
        return inited;
    }
    int inited;
    virtual void safe_release() = 0;

    virtual void toggle_full_screen() = 0;
    virtual int  is_window_shown()const {return window_shown;}
    virtual int create_window(const char* title, int x, int y, int w, int h, uint32_t flags) = 0;
    virtual void show_window(int fullscreen) = 0;
    virtual void set_default_window_size(int width, int height, AVRational sar) = 0;
 
    
    virtual void clear_render() = 0;
    virtual void draw_render()  = 0;    
    virtual void upload_and_draw_frame(Frame* video_frame) = 0;

    virtual int open_audio( AudioDecoder* decoder, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams* audio_hw_params) = 0;
    static void sdl_audio_callback(void* opaque, uint8_t* stream, int len);// prepare a new audio buffer 

    virtual void mix_audio( uint8_t * dst, const uint8_t * src, uint8_t len, int volume /* [0 - 100]*/ ) = 0;
    virtual void pause_audio(int pause_on ) = 0;
    virtual void close_audio() = 0;
    
    AString window_title;
    
    int screen_width;
    int screen_height;
    int screen_left;
    int screen_top;

    int64_t cursor_last_shown;
    int cursor_hidden;
protected:
    int fullscreen;
    int window_shown;
 
};


class SimpleAVDecoder
{
public:
    SimpleAVDecoder()
        :auddec(this), viddec(this)
    {
        this->extclk.init_clock(&this->extclk.serial);
        frame_drops_early = frame_drops_late = 0;
        paused = step = 0;
        force_refresh = 0;
        realtime = 0;
        show_status = -1;
        decoder_reorder_pts = -1;  
        render = NULL;
		max_frame_duration = 10;
    } 
    virtual ~SimpleAVDecoder();

    friend AudioDecoder; friend  VideoDecoder;

    RenderBase*  render;
    
    // mask:  bit0  -- V opened ， bit1 -- A opened 
    int   open_stream_from_avformat(AVFormatContext* format_context,  int* vstream_id, int* astream_id);

    // Return:  0 -- success, non-zero -- error.
    int   open_stream(const AVCodecParameters * codec_para, const StreamParam* extra_para); 

    int   get_opened_streams_mask();  // mask:  bit0  -- V opened ， bit1 -- A opened 
    void  close_all_stream();
    
    // called to display each frame (from event loop )
    void video_refresh(double* remaining_time);

    int  get_master_sync_type() const;
    void set_master_sync_type(int how);
    int realtime;   // is 'realtime' stream or not

    double get_master_clock();  // get the time value of master clock 
    Clock* get_decoder_clock(); // get the master clock itself
        
    // {{  some ffplay cmd line opt  
    int decoder_reorder_pts;
    int show_status;
    // }}  some ffplay cmd line opt  
    
    int is_stalled();

    // decoder status section {{
    int   is_drawing_needed() const{ return force_refresh;}  
    void  toggle_need_drawing(int need_drawing);
    int internal_toggle_pause();
    void toggle_step(int step_mode);
    void toggle_mute();
    void mute(int should_mute) {
        auddec.muted =  should_mute;
    }
    int is_muted()  {
        return auddec.muted;
    }
    int is_paused() const { return this->paused; }
    void update_volume(int delta );
    // }} decoder status section

    void discard_buffer(double seek_target = NAN); // clear cach for 'seek'. If seek by time, also spec the 'seek_target'  ( in unit of 'second')
    int  is_buffer_full();
    void feed_null_pkt(); // 
    void feed_pkt(AVPacket* pkt, const AVPacketExtra* extra  ); // take ownership of 'pkt'
	
	AudioDecoder    auddec;
	VideoDecoder    viddec;

protected:

    int av_sync_type;
    Clock extclk;
    void check_external_clock_speed();  // adjust external to sync to the speed of stream

    // decoder status section {{
    int force_refresh;   // is there 'frame' waiting for drawing?
    int paused;
    int step; // frame by frame mode 
    // }} decoder status section
    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    // {{ statistics
    int frame_drops_early;
    int frame_drops_late;
    // }} statistics
   
    void print_stream_status();

    void prepare_picture_for_display(double* remaining_time);

    double vp_duration(Frame* vp, Frame* nextvp); //  refs 'max_frame_duration'
    double compute_target_delay(double delay);
    void update_video_clock(double pts, int64_t pos, int serial);

    // return the wanted number of samples to get better sync if sync_type is video or external master clock
    int synchronize_audio(int nb_samples);
};

class ParserCB
{
public:
	virtual void on_eof(const char* file_Playing) {
	}
	virtual void on_ioerror(const char* file_Playing, int error_code) {
	}
};



class VideoState
    :public BaseThread //  stream reader thread 
{
public:
    VideoState();

    int open_input_stream(const char* filename, AVInputFormat* iformat, int paused = 0);
    
    void close_input_stream(); 

	void set_parser_cb(ParserCB * cb){
		parser_cb = cb;
	}

    // {{{ stream operation section
    void stream_seek( int64_t pos, int64_t rel, int seek_by_bytes);

    void toggle_pause();
    
    void step_to_next_frame();

    void seek_chapter( int incr);

    // }}} stream operation section

    // {{  some ffplay cmd line opt  
    int64_t streamopt_start_time;  // 命令行 -ss ，由 av_parse_time 解析为 microseconds
    int64_t streamopt_duration;    // 命令行 -t  ，由 av_parse_time 解析为 microseconds
    int     streamopt_autoexit;
    // }}
    
    SimpleAVDecoder av_decoder;

    AVFormatContext * format_context;
protected:    
	ParserCB  *parser_cb ;
    int eof;
    int abort_request;

    
    int infinite_buffer; 

    virtual  ThreadRetType thread_main();  //  stream reader thread 
    static int decode_interrupt_cb(void* ctx); // Chance for 'avformat' to 'break reading' 

    AVInputFormat* iformat;   // 命令行指定容器格式，ref only
    AString file_to_play; // 播放的文件 or url
  
    // format status section {{
    int paused;
    // }}  format status section
    int last_paused; // 之前一次reader loop的时候，是否是paused

    int open_stream_file();
    int last_video_stream, last_audio_stream ;
    void fill_packet_extra( AVPacketExtra* extra, const AVPacket* pkt) const;

    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;

    // 'reader thread' section {{{
    int  read_loop_check_pause(); // return: > 0 -- shoud 'continue', 0 -- go on current iteration, < 0 -- error exit loop
    int  read_loop_check_seek();  // return: > 0 -- shoud 'continue', 0 -- go on current iteration, < 0 -- error exit loop

    int is_pkt_in_play_range( AVPacket* pkt);
    double stream_ts_to_second(int64_t ts, int stream_index);

    void quit_main_loop(); // todo: still ref 'SDL'
    // }}} 'reader thread' section
};

#if _MSC_VER > 1916     // prevent xxxx already defined in some_source.cpp
template<> void AutoReleasePtr<VideoState>::release();
#endif

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
