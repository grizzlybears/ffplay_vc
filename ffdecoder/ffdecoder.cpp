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

#include "ffdecoder.h"
extern "C"
{
#include "src/cmdutils.h"
}

/* options specified by the user */
AVInputFormat * opt_file_iformat;
const char * opt_input_filename;
 const char * g_window_title;
 int g_default_width  = 640;
 int g_default_height = 480;
  
 int opt_audio_disable = 0;
 int subtitle_disable = 0;
 float seek_interval = 10;
 int opt_alwaysontop = 0;
 int opt_startup_volume = 100;
 int opt_show_status = -1;
 int opt_av_sync_type = AV_SYNC_AUDIO_MASTER;
 int64_t opt_start_time = AV_NOPTS_VALUE;
 int64_t opt_duration = AV_NOPTS_VALUE;
 int fast = 0;
 int genpts = 0;
 int lowres = 0;
 int decoder_reorder_pts = -1;
 int autoexit = 0;
  int opt_framedrop = -1;
 int opt_infinite_buffer = -1;
 
 const char *audio_codec_name;
 const char *subtitle_codec_name;
 const char *video_codec_name;
double rdftspeed = 0.02;
 int64_t cursor_last_shown;
 int cursor_hidden = 0;

 int autorotate = 1;
 int opt_full_screen = 0;

/* current context */
 
 int64_t audio_callback_time;

 Render g_render;
 
 const struct TextureFormatEntry sdl_texture_format_map[] = {
    { AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },
    { AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },
    { AV_PIX_FMT_RGB555,         SDL_PIXELFORMAT_RGB555 },
    { AV_PIX_FMT_BGR555,         SDL_PIXELFORMAT_BGR555 },
    { AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },
    { AV_PIX_FMT_BGR565,         SDL_PIXELFORMAT_BGR565 },
    { AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },
    { AV_PIX_FMT_BGR24,          SDL_PIXELFORMAT_BGR24 },
    { AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },
    { AV_PIX_FMT_0BGR32,         SDL_PIXELFORMAT_BGR888 },
    { AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
    { AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
    { AV_PIX_FMT_RGB32,          SDL_PIXELFORMAT_ARGB8888 },
    { AV_PIX_FMT_RGB32_1,        SDL_PIXELFORMAT_RGBA8888 },
    { AV_PIX_FMT_BGR32,          SDL_PIXELFORMAT_ABGR8888 },
    { AV_PIX_FMT_BGR32_1,        SDL_PIXELFORMAT_BGRA8888 },
    { AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },
    { AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },
    { AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },
    { AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN },
};


///////////// packet_queue section {{{

/* packet queue handling */
/*
int packet_queue_init()
{
    memset(q, 0, sizeof(PacketQueue));
    q->abort_request = 1;
    return 0;
}
*/

AVPacket PacketQueue::flush_pkt;

int PacketQueue::is_flush_pkt(const AVPacket& to_check)
{
    return to_check.data == flush_pkt.data;
}

int PacketQueue::packet_queue_put_private( AVPacket *pkt)
{
    MyAVPacketList *pkt1;

    if (this->abort_request)
       return -1;

    pkt1 = (MyAVPacketList*) av_malloc(sizeof(MyAVPacketList));
    if (!pkt1)
        return -1;

    pkt1->pkt = *pkt;   // 要点, ‘pkt’ 已经被 clone 进 MyAVPacketList， 因此无所谓‘pkt’是来自heap/stack/global
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        this->serial++;

    pkt1->serial = this->serial;

    if (!this->last_pkt)
        this->first_pkt = pkt1;
    else
        this->last_pkt->next = pkt1;

    this->last_pkt = pkt1;

    this->nb_packets++;
    this->size += pkt1->pkt.size + sizeof(*pkt1);

    this->duration += pkt1->pkt.duration;
    /* XXX: should duplicate packet data in DV case */

    this->cond.wake();
    
    return 0;
}

// 接管pkt生命周期，put失败也释放掉
int PacketQueue::packet_queue_put( AVPacket *pkt)
{
    int ret;

    AutoLocker _yes_locked(this->cond);
    ret = packet_queue_put_private(pkt);
        
    if (pkt != &flush_pkt && ret < 0)
        av_packet_unref(pkt);

    return ret;
}

int PacketQueue::packet_queue_put_nullpacket( int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;  
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(pkt);
}


void PacketQueue::packet_queue_flush()
{
    MyAVPacketList *pkt, *pkt1;

    AutoLocker _yes_locked(this->cond);
    
    for (pkt = this->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);   
        av_freep(&pkt);  
    }
    this->last_pkt = NULL;
    this->first_pkt = NULL;
    this->nb_packets = 0;
    this->size = 0;
    this->duration = 0;
}

void PacketQueue::packet_queue_destroy()
{
    packet_queue_flush();    
}

void PacketQueue::packet_queue_abort()
{
    AutoLocker _yes_locked(this->cond);
    this->abort_request = 1;
    this->cond.wake();
}

void PacketQueue::packet_queue_start()
{
    AutoLocker _yes_locked(this->cond);
    this->abort_request = 0;
    this->cond.wake();
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
int PacketQueue::packet_queue_get( AVPacket *pkt, int block, /*out*/ int *serial)
{
    MyAVPacketList *pkt1;
    int ret;

    AutoLocker _yes_locked(this->cond);

    for (;;) {
        if (this->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = this->first_pkt;
        if (pkt1) {
            
            // 推移 list_header
            this->first_pkt = pkt1->next;
            if (!this->first_pkt)
                this->last_pkt = NULL;

            // 调整 ‘queue统计’
            this->nb_packets--;
            this->size -= pkt1->pkt.size + sizeof(*pkt1);
            this->duration -= pkt1->pkt.duration;
            
            // output 节点
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
            
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            this->cond.wait();
        }
    }
    
    return ret;
}

///////////// }}} packet_queue section



//     decoder section {{{
void Decoder::decoder_init( AVCodecContext *avctx, PacketQueue *queue, SimpleConditionVar* empty_queue_cond)
{
    //memset(d, 0, sizeof(Decoder));
    this->avctx = avctx;
    this->queue = queue;
    this->empty_queue_cond = empty_queue_cond;
    this->start_pts = AV_NOPTS_VALUE;
    this->pkt_serial = -1;
}

int Decoder::decoder_decode_frame(AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    for (;;) {
        
        if (this->queue->serial == this->pkt_serial) {
            do {
                if (this->queue->abort_request)
                    return -1;

                // 在音视频流中顺序解码

                switch (this->avctx->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                        ret = avcodec_receive_frame(this->avctx, frame);
                        if (ret >= 0) {
                            // 成功解得frame
                            if (decoder_reorder_pts == -1) {
                                frame->pts = frame->best_effort_timestamp;
                            } else if (!decoder_reorder_pts) {
                                frame->pts = frame->pkt_dts;
                            }
                        }
                        break;
                    case AVMEDIA_TYPE_AUDIO:
                        ret = avcodec_receive_frame(this->avctx, frame);
                        if (ret >= 0) { // 成功解得frame
                            AVRational tb = { 1, frame->sample_rate };
                            if (frame->pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(frame->pts, this->avctx->pkt_timebase, tb);
                            else if (this->next_pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(this->next_pts, this->next_pts_tb, tb);
                            if (frame->pts != AV_NOPTS_VALUE) {
                                this->next_pts = frame->pts + frame->nb_samples;
                                this->next_pts_tb = tb;
                            }
                        }
                        break;
                }
                if (ret == AVERROR_EOF) {
                    this->finished = this->pkt_serial;
                    avcodec_flush_buffers(this->avctx);
                    return 0;
                }
                if (ret >= 0) // 成功解得frame
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        AVPacket pkt;
        do {
            if (this->queue->nb_packets == 0)
                this->empty_queue_cond->wake();
                
            if (this->is_packet_pending) {
                av_packet_move_ref(&pkt, &this->pending_pkt);
                this->is_packet_pending = 0;
            } else {
                if (this->queue->packet_queue_get( &pkt, 1 /*block until get*/, &this->pkt_serial) < 0)
                    return -1;
            }
            if (this->queue->serial == this->pkt_serial)
                break;
            av_packet_unref(&pkt);
        } while (1);

        // 现在 ‘pkt’ 的serial符合要求了

        if (PacketQueue::is_flush_pkt(pkt)) {
            avcodec_flush_buffers(this->avctx);
            this->finished = 0;
            this->next_pts = this->start_pts;
            this->next_pts_tb = this->start_pts_tb;

            continue;
        }
        
        if (this->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int got_frame = 0;
            ret = avcodec_decode_subtitle2(this->avctx, sub, &got_frame, &pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN);
            } else {
                if (got_frame && !pkt.data) {
                    this->is_packet_pending = 1;
                    av_packet_move_ref(&this->pending_pkt, &pkt);
                }
                ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
        } else {
            // serial 发生了变化，只能先喂packet进codec

            if (avcodec_send_packet(this->avctx, &pkt) == AVERROR(EAGAIN)) 
            {
                //AVERROR(EAGAIN) : input is not accepted in the current state - user
                //    must read output with avcodec_receive_frame() (once
                //    all output is read, the packet should be resent, and
                //    the call will not fail with EAGAIN).

                av_log(this->avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                this->is_packet_pending = 1;
                av_packet_move_ref(&this->pending_pkt, &pkt);
            }
        }
        av_packet_unref(&pkt);
    }
}

void Decoder::decoder_destroy() {
    av_packet_unref(&this->pending_pkt);
    avcodec_free_context(&this->avctx);
}

void Decoder::decoder_abort( FrameQueue* fq)
{
    this->queue->packet_queue_abort();
    fq->frame_queue_signal();

    this->decoder_thread.wait_thread_quit();
    this->decoder_thread.safe_cleanup();

    this->queue->packet_queue_flush();
}

//     }}} decoder section 


//     frame_queue section {{{

void FrameQueue::unref_item(Frame* vp)
{
    av_frame_unref(vp->frame);
    avsubtitle_free(&vp->sub);
}

int FrameQueue::frame_queue_init( PacketQueue *pktq, int max_size, int keep_last)
{
    int i;    

    //memset(f, 0, sizeof(FrameQueue));
    //if (!(f->mutex = SDL_CreateMutex())) {
    //    av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
    //    return AVERROR(ENOMEM);
    //}
    //if (!(f->cond = SDL_CreateCond())) {
    //    av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
    //    return AVERROR(ENOMEM);
    //}

    this->pktq = pktq;
    this->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    this->keep_last = !!keep_last;
    for (i = 0; i < this->max_size; i++)
        if (!(this->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

void FrameQueue::frame_queue_destory()
{
    int i;
    for (i = 0; i < this->max_size; i++) {
        Frame *vp = &this->queue[i];
        unref_item(vp);
        av_frame_free(&vp->frame);
    }
    //SDL_DestroyMutex(f->mutex);
    //SDL_DestroyCond(f->cond);
}

 void FrameQueue::frame_queue_signal()
{
     AutoLocker _yes_locked(this->cond);
     this->cond.wake();
}

Frame * FrameQueue::frame_queue_peek()
{
    return & this->queue[(this->rindex + this->rindex_shown) % this->max_size];
}

Frame * FrameQueue::frame_queue_peek_next()
{
    return &this->queue[(this->rindex + this->rindex_shown + 1) % this->max_size];
}

Frame * FrameQueue::frame_queue_peek_last()
{
    return & this->queue[this->rindex];
}

Frame * FrameQueue::frame_queue_peek_writable()
{
    /* wait until we have space to put a new frame */
    {
        AutoLocker _yes_locked(this->cond);
        while (this->size >= this->max_size && !this->pktq->abort_request)  // todo: 如果挂钩的packet queue 退了，怎么能 signal 本 FrameQueue的 cond?
        {
            this->cond.wait();
        }
    }

    if (this->pktq->abort_request)
        return NULL;

    return &this->queue[this->windex];
}

Frame * FrameQueue::frame_queue_peek_readable()
{
    /* wait until we have a readable a new frame */
    {
        AutoLocker _yes_locked(this->cond);
        while (this->size - this->rindex_shown <= 0 &&
            !this->pktq->abort_request) // todo: 如果挂钩的packet queue 退了，怎么能 signal 本 FrameQueue的 cond?
        {
            this->cond.wait();
        }
    }

    if (this->pktq->abort_request)
        return NULL;

    return &this->queue[(this->rindex + this->rindex_shown) % this->max_size];
}

void FrameQueue::frame_queue_push()
{
    if (++this->windex == this->max_size)
        this->windex = 0;

    AutoLocker _yes_locked(this->cond);
    this->size++;

    this->cond.wake();
}

void FrameQueue::frame_queue_next()
{
    if (this->keep_last && !this->rindex_shown) {
        this->rindex_shown = 1;
        return;
    }
    unref_item(&this->queue[this->rindex]);  // 出队列前，先释放 Frame内带的data
    if (++this->rindex == this->max_size)
        this->rindex = 0;

    AutoLocker _yes_locked(this->cond);
    this->size--;
    this->cond.wake();

}

/* return the number of undisplayed frames in the queue */
int FrameQueue::frame_queue_nb_remaining()
{
    return this->size - this->rindex_shown;
}

/* return last shown position */
int64_t FrameQueue::frame_queue_last_pos()
{
    Frame *fp = &this->queue[this->rindex];
    if (this->rindex_shown && fp->serial == this->pktq->serial)
        return fp->pos;
    else
        return -1;
}
//      }}} frame_queue section

// render sectio {{{

int Render::init(int audio_disable, int alwaysontop)
{
    int sdl_flags;
    sdl_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (audio_disable)
        sdl_flags &= ~SDL_INIT_AUDIO;
    else {
        /* Try to work around an occasional ALSA buffer underflow issue when the
         * period size is NPOT due to ALSA resampling by forcing the buffer size. */
        if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
            SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);
    }

    if (SDL_Init(sdl_flags)) {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        return 2;
    }

    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);


    int cw_flags = SDL_WINDOW_HIDDEN;
    if (alwaysontop)
#if SDL_VERSION_ATLEAST(2,0,5)
        cw_flags |= SDL_WINDOW_ALWAYS_ON_TOP;
#else
        av_log(NULL, AV_LOG_WARNING, "Your SDL version doesn't support SDL_WINDOW_ALWAYS_ON_TOP. Feature will be inactive.\n");
#endif
    cw_flags |= SDL_WINDOW_RESIZABLE;

    if (this->create_window(g_program_name
        , SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, g_default_width, g_default_height
        , cw_flags))
    {
        return 3;
    }

    return 0;
}

void Render::show_window(const char* window_title, int w, int h, int left, int top, int should_fullscreen)
{
    SDL_SetWindowTitle(this->window, window_title);

    SDL_SetWindowSize(this->window, w, h);
    SDL_SetWindowPosition(this->window, left, top);
    if (should_fullscreen)
    {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        this->fullscreen = 1;
    }
    SDL_ShowWindow(window);    
}

int Render::create_window(const char* title, int x, int y, int w, int h, Uint32 flags)
{
    window = SDL_CreateWindow(title, x, y, w, h, flags);    
    if (!window)
    {
        av_log(NULL, AV_LOG_FATAL, "Failed to create window : %s", SDL_GetError());
        return 1;
    }
    
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, 0);
    }

    if (!renderer) {
        av_log(NULL, AV_LOG_WARNING, "Failed to initialize renderer: %s\n", SDL_GetError());
        return 2;
    }
    
    if (SDL_GetRendererInfo(renderer, &renderer_info))
    {
        av_log(NULL, AV_LOG_WARNING, "Failed to get  renderer info: %s\n", SDL_GetError());
        return 3;
    }
        
    av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", renderer_info.name);
        
    return 0;
}

void Render::toggle_full_screen()
{
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void Render::clear_render()
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

void Render::draw_render()
{
    SDL_RenderPresent(renderer);
}

void Render::close_audio()
{
    if (audio_dev > 0)
    {
        SDL_CloseAudioDevice(this->audio_dev);
    }

    audio_dev = 0;
}

void Render::safe_release()
{
    close_audio();

    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window)
    {
        SDL_DestroyWindow(window);
        window = NULL;
    }

}
void Render::fill_rectangle(int x, int y, int w, int h)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    if (w && h)
        SDL_RenderFillRect(this->renderer, &rect);
}

void Render::set_default_window_size(int width, int height, AVRational sar)
{
    SDL_Rect rect;
    int max_width = this->screen_width ? this->screen_width : INT_MAX;
    int max_height = this->screen_height ? this->screen_height : INT_MAX;
    if (max_width == INT_MAX && max_height == INT_MAX)
        max_height = height;
    
    calculate_display_rect(&rect, 0, 0, max_width, max_height, width, height, sar);
    
    g_default_width = rect.w;
    g_default_height = rect.h;
}

int Render::realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
    Uint32 format;
    int access, w, h;
    if (!*texture || SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format) {
        void *pixels;
        int pitch;
        if (*texture)
            SDL_DestroyTexture(*texture);
        if (!(*texture = SDL_CreateTexture(this->renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
            return -1;
        if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
            return -1;
        if (init_texture) {
            if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
                return -1;
            memset(pixels, 0, pitch * new_height);
            SDL_UnlockTexture(*texture);
        }
        av_log(NULL, AV_LOG_VERBOSE, "Created %dx%d texture with %s.\n", new_width, new_height, SDL_GetPixelFormatName(new_format));
    }
    return 0;
}
// }}} render sectio 

void calculate_display_rect(SDL_Rect *rect,
                                   int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                   int pic_width, int pic_height, AVRational pic_sar)
{
    AVRational aspect_ratio = pic_sar;
    int64_t width, height, x, y;

    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0)
        aspect_ratio = av_make_q(1, 1);

    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(pic_width, pic_height));

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > scr_width) {
        width = scr_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    rect->x = (int)(scr_xleft + x);
    rect->y = (int)(scr_ytop  + y);
    rect->w = FFMAX((int)width,  1);
    rect->h = FFMAX((int)height, 1);
}

void Render::get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode)
{
    int i;
    *sdl_blendmode = SDL_BLENDMODE_NONE;
    *sdl_pix_fmt = SDL_PIXELFORMAT_UNKNOWN;
    if (format == AV_PIX_FMT_RGB32   ||
        format == AV_PIX_FMT_RGB32_1 ||
        format == AV_PIX_FMT_BGR32   ||
        format == AV_PIX_FMT_BGR32_1)
        *sdl_blendmode = SDL_BLENDMODE_BLEND;
    for (i = 0; i < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; i++) {
        if (format == sdl_texture_format_map[i].format) {
            *sdl_pix_fmt = sdl_texture_format_map[i].texture_fmt;
            return;
        }
    }
}

int Render::upload_texture(SDL_Texture **tex, AVFrame *frame, struct SwsContext **img_convert_ctx) {
    int ret = 0;
    Uint32 sdl_pix_fmt;
    SDL_BlendMode sdl_blendmode;
    Render::get_sdl_pix_fmt_and_blendmode(frame->format, &sdl_pix_fmt, &sdl_blendmode);
    if (g_render.realloc_texture(tex, sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt, frame->width, frame->height, sdl_blendmode, 0) < 0)
        return -1;
    switch (sdl_pix_fmt) {
        case SDL_PIXELFORMAT_UNKNOWN:
            /* This should only happen if we are not using avfilter... */
            *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
                frame->width, frame->height, (enum AVPixelFormat) frame->format, frame->width, frame->height,
                AV_PIX_FMT_BGRA, sws_flags, NULL, NULL, NULL);
            if (*img_convert_ctx != NULL) {
                uint8_t *pixels[4];
                int pitch[4];
                if (!SDL_LockTexture(*tex, NULL, (void **)pixels, pitch)) {
                    sws_scale(*img_convert_ctx, (const uint8_t * const *)frame->data, frame->linesize,
                              0, frame->height, pixels, pitch);
                    SDL_UnlockTexture(*tex);
                }
            } else {
                av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                ret = -1;
            }
            break;
        case SDL_PIXELFORMAT_IYUV:
            if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0) {
                ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0], frame->linesize[0],
                                                       frame->data[1], frame->linesize[1],
                                                       frame->data[2], frame->linesize[2]);
            } else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0) {
                ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height                    - 1), -frame->linesize[0],
                                                       frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
                                                       frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]);
            } else {
                av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
                return -1;
            }
            break;
        default:
            if (frame->linesize[0] < 0) {
                ret = SDL_UpdateTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
            } else {
                ret = SDL_UpdateTexture(*tex, NULL, frame->data[0], frame->linesize[0]);
            }
            break;
    }
    return ret;
}

void Render::set_sdl_yuv_conversion_mode(AVFrame *frame)
{
#if SDL_VERSION_ATLEAST(2,0,8)
    SDL_YUV_CONVERSION_MODE mode = SDL_YUV_CONVERSION_AUTOMATIC;
    if (frame && (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUYV422 || frame->format == AV_PIX_FMT_UYVY422)) {
        if (frame->color_range == AVCOL_RANGE_JPEG)
            mode = SDL_YUV_CONVERSION_JPEG;
        else if (frame->colorspace == AVCOL_SPC_BT709)
            mode = SDL_YUV_CONVERSION_BT709;
        else if (frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M || frame->colorspace == AVCOL_SPC_SMPTE240M)
            mode = SDL_YUV_CONVERSION_BT601;
    }
    SDL_SetYUVConversionMode(mode);
#endif
}

Frame* get_current_subtitle_frame(VideoState* is, Frame* current_video_frame)
{
    Frame* sp = NULL;

    if (!is->subtitle_st) 
    {
        return NULL;
    }

    if (is->subpq.frame_queue_nb_remaining() <= 0) 
    {
        return NULL;
    }
    
    sp = is->subpq.frame_queue_peek();
    if (current_video_frame->pts < sp->pts + ((float)sp->sub.start_display_time / 1000))
    {
        return NULL;
    }

    if (sp->uploaded)
    {
        return sp;
    }
    
    
    uint8_t* pixels[4];
    int pitch[4];

    if (!sp->width || !sp->height) {
        sp->width = current_video_frame->width;
        sp->height = current_video_frame->height;
    }

    if (g_render.realloc_texture(&is->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
    {
        LOG_WARN("Failed in realloc_texture for substitle start at %u\n" , sp->sub.start_display_time);
        return NULL ;
    }

    for (unsigned int i = 0; i < sp->sub.num_rects; i++) {
        AVSubtitleRect* sub_rect = sp->sub.rects[i];

        sub_rect->x = av_clip(sub_rect->x, 0, sp->width);
        sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
        sub_rect->w = av_clip(sub_rect->w, 0, sp->width - sub_rect->x);
        sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

        is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
            sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
            sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
            0, NULL, NULL, NULL);
        if (!is->sub_convert_ctx) {
            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
            return NULL;
        }

        if (!SDL_LockTexture(is->sub_texture, (SDL_Rect*)sub_rect, (void**)pixels, pitch)) {
            sws_scale(is->sub_convert_ctx, (const uint8_t* const*)sub_rect->data, sub_rect->linesize,
                0, sub_rect->h, pixels, pitch);
            SDL_UnlockTexture(is->sub_texture);
        }
    }
    sp->uploaded = 1;
        
    return sp;

}
void video_image_display(VideoState *is)
{
    Frame *vp;
    Frame *sp = NULL;
    SDL_Rect rect;

    vp = is->pictq.frame_queue_peek_last();

    sp = get_current_subtitle_frame(is, vp);

    calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);

    if (!vp->uploaded) {
        if (Render::upload_texture(&is->vid_texture, vp->frame, &is->img_convert_ctx) < 0)
            return;
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
    }

    Render::set_sdl_yuv_conversion_mode(vp->frame);
    SDL_RenderCopyEx(g_render.renderer, is->vid_texture, NULL, &rect, 0, NULL, (SDL_RendererFlip)(vp->flip_v ? SDL_FLIP_VERTICAL : 0));
    Render::set_sdl_yuv_conversion_mode(NULL);
    if (sp) {
#if USE_ONEPASS_SUBTITLE_RENDER
        SDL_RenderCopy(g_render.renderer, is->sub_texture, NULL, &rect);
#else
        int i;
        double xratio = (double)rect.w / (double)sp->width;
        double yratio = (double)rect.h / (double)sp->height;
        for (i = 0; i < sp->sub.num_rects; i++) {
            SDL_Rect *sub_rect = (SDL_Rect*)sp->sub.rects[i];
            SDL_Rect target = {.x = rect.x + sub_rect->x * xratio,
                               .y = rect.y + sub_rect->y * yratio,
                               .w = sub_rect->w * xratio,
                               .h = sub_rect->h * yratio};
            SDL_RenderCopy(renderer, is->sub_texture, sub_rect, &target);
        }
#endif
    }
}

//  }}} render section 


void stream_component_close(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= (int) ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->auddec.decoder_abort(&is->sampq);
        g_render.close_audio();        
        is->auddec.decoder_destroy();
        swr_free(&is->swr_ctx);
        av_freep(&is->audio_buf1);
        is->audio_buf1_size = 0;
        is->audio_buf = NULL;

        if (is->rdft) {
            av_rdft_end(is->rdft);
            av_freep(&is->rdft_data);
            is->rdft = NULL;
            is->rdft_bits = 0;
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->viddec.decoder_abort( &is->pictq);
        is->viddec.decoder_destroy();
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subdec.decoder_abort( &is->subpq);
        is->subdec.decoder_destroy();
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_st = NULL;
        is->subtitle_stream = -1;
        break;
    default:
        break;
    }
}

// 关闭并释放 'is'
void VideoState::close()
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    this->abort_request = 1;
    this->wait_thread_quit();

    /* close each stream */
    if (this->audio_stream >= 0)
        stream_component_close(this, this->audio_stream);
    if (this->video_stream >= 0)
        stream_component_close(this, this->video_stream);
    if (this->subtitle_stream >= 0)
        stream_component_close(this, this->subtitle_stream);

    avformat_close_input(&this->ic);

    this->videoq.packet_queue_destroy();
    this->audioq.packet_queue_destroy();
    this->subtitleq.packet_queue_destroy();

    /* free all pictures */    
    this->pictq.frame_queue_destory();
    this->sampq.frame_queue_destory();
    this->subpq.frame_queue_destory();

    //SDL_DestroyCond(is->continue_read_thread);
    if (this->img_convert_ctx)
    {
        sws_freeContext(this->img_convert_ctx);
        this->img_convert_ctx = NULL;
    }
    if (this->sub_convert_ctx)
    {
        sws_freeContext(this->sub_convert_ctx);
        this->sub_convert_ctx = NULL;
    }
    av_free(this->filename);
    this->filename = NULL;
        
    if (this->vid_texture)
    {
        SDL_DestroyTexture(this->vid_texture);
        this->vid_texture = NULL;
    }

    if (this->sub_texture)
    {
        SDL_DestroyTexture(this->sub_texture);
        this->sub_texture = NULL;
    } 
}



void do_exit(VideoState *is)
{
    if (is) {
        is->close();
        delete is;
    }

    g_render.safe_release();
    
    uninit_opts();

    avformat_network_deinit();
    if (opt_show_status)
        printf("\n");
    SDL_Quit();
    av_log(NULL, AV_LOG_QUIET, "%s", "");
    exit(0);
}

void sigterm_handler(int sig)
{
    exit(123);
}



int video_open(VideoState *is)
{
    int w,h;

    w = g_render.screen_width ? g_render.screen_width : g_default_width;
    h = g_render.screen_height ? g_render.screen_height : g_default_height;

    if (!g_window_title)
        g_window_title = opt_input_filename;

    g_render.show_window(g_window_title, w, h, g_render.screen_left, g_render.screen_top, opt_full_screen);
    
    is->width  = w;
    is->height = h;

    return 0;
}

/* display the current picture, if any */
void video_display(VideoState *is)
{
    if (!is->width)
        video_open(is);

    g_render.clear_render();
    
    if (is->video_st)
        video_image_display(is);

    g_render.draw_render();
    
}

double Clock::get_clock()
{
    if (*this->queue_serial != this->serial)
        return NAN;
    if (this->paused) {
        return this->pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return this->pts_drift + time - (time - this->last_updated) * (1.0 - this->speed);
    }
}

void Clock::set_clock_at( double pts, int serial, double time)
{
    this->pts = pts;
    this->last_updated = time;
    this->pts_drift = this->pts - time;
    this->serial = serial;
}

void Clock::set_clock( double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(pts, serial, time);
}

void Clock::set_clock_speed( double speed)
{
    set_clock( get_clock(), this->serial);
    this->speed = speed;
}

void Clock::init_clock( int *queue_serial)
{
    this->speed = 1.0;
    this->paused = 0;
    this->queue_serial = queue_serial;
    set_clock( NAN, -1);
}

void Clock::sync_clock_to_slave( Clock *slave)
{
    double clock = get_clock();
    double slave_clock = slave->get_clock();
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        this->set_clock(slave_clock, slave->serial);
}

int get_master_sync_type(VideoState *is) {
    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    } else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
double get_master_clock(VideoState *is)
{
    double val;

    switch (get_master_sync_type(is)) {
        case AV_SYNC_VIDEO_MASTER:
            val = is->vidclk.get_clock();
            break;
        case AV_SYNC_AUDIO_MASTER:
            val = is->audclk.get_clock();
            break;
        default:
            val = is->extclk.get_clock();
            break;
    }
    return val;
}

void check_external_clock_speed(VideoState *is) {
   if (is->video_stream >= 0 && is->videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
       is->audio_stream >= 0 && is->audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) {
       is->extclk.set_clock_speed( FFMAX(EXTERNAL_CLOCK_SPEED_MIN, is->extclk.get_clock_speed() - EXTERNAL_CLOCK_SPEED_STEP));
   } else if ((is->video_stream < 0 || is->videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
              (is->audio_stream < 0 || is->audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES)) {
       is->extclk.set_clock_speed( FFMIN(EXTERNAL_CLOCK_SPEED_MAX, is->extclk.get_clock_speed() + EXTERNAL_CLOCK_SPEED_STEP));
   } else {
       double speed = is->extclk.get_clock_speed();
       if (speed != 1.0)
           is->extclk.set_clock_speed( speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
   }
}

/* seek in the stream */
void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!is->seek_req) {
        is->seek_pos = pos;
        is->seek_rel = rel;
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        is->seek_req = 1;

        is->continue_read_thread.wake();        
    }
}

/* pause or resume the video */
void stream_toggle_pause(VideoState *is)
{
    if (is->paused) {
        is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.get_last_set_point();
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
        is->vidclk.set_clock(is->vidclk.get_clock(), is->vidclk.serial);
    }
    is->extclk.set_clock(is->extclk.get_clock(), is->extclk.serial);
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

void toggle_pause(VideoState *is)
{
    stream_toggle_pause(is);
    is->step = 0;
}

void toggle_mute(VideoState *is)
{
    is->muted = !is->muted;
}

void update_volume(VideoState *is, int sign, double step)
{
    double volume_level = is->audio_volume ? (20 * log(is->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    is->audio_volume = av_clip(is->audio_volume == new_volume ? (is->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

void step_to_next_frame(VideoState *is)
{
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        stream_toggle_pause(is);
    is->step = 1;
}

double compute_target_delay(double delay, VideoState *is)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = is->vidclk.get_clock() - get_master_clock(is);

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
            delay, -diff);

    return delay;
}

double vp_duration(VideoState *is, Frame *vp, Frame *nextvp) {
    if (vp->serial == nextvp->serial) {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
            return vp->duration;
        else
            return duration;
    } else {
        return 0.0;
    }
}

void update_video_pts(VideoState *is, double pts, int64_t pos, int serial) {
    /* update current video pts */
    is->vidclk.set_clock( pts, serial);
    is->extclk.sync_clock_to_slave( &is->vidclk);
}

/* called to display each frame */
void video_refresh(void *opaque, double *remaining_time)
{
    VideoState *is = (VideoState*)opaque;
    double time;

    Frame *sp, *sp2;

    if (!is->paused && get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        check_external_clock_speed(is);

    if (is->video_st) {
retry:
        if (is->pictq.frame_queue_nb_remaining() == 0) {
            // nothing to do, no picture to display in the queue
        } else {
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = is->pictq.frame_queue_peek_last();
            vp = is->pictq.frame_queue_peek();

            if (vp->serial != is->videoq.serial) {
                is->pictq.frame_queue_next();
                goto retry;
            }

            if (lastvp->serial != vp->serial)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            if (is->paused)
                goto display;

            /* compute nominal last_duration */
            last_duration = vp_duration(is, lastvp, vp);
            delay = compute_target_delay(last_duration, is);

            time= av_gettime_relative()/1000000.0;
            if (time < is->frame_timer + delay) {
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                goto display;
            }

            is->frame_timer += delay;
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            {
                AutoLocker yes_locked(is->pictq.cond);  // todo: 为何要锁
                if (!isnan(vp->pts))
                    update_video_pts(is, vp->pts, vp->pos, vp->serial);
            }
            
            if (is->pictq.frame_queue_nb_remaining() > 1) {
                Frame *nextvp = is->pictq.frame_queue_peek_next();
                duration = vp_duration(is, vp, nextvp);
                if(!is->step && (opt_framedrop >0 || (opt_framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration){
                    is->frame_drops_late++;
                    is->pictq.frame_queue_next();
                    goto retry;
                }
            }

            if (is->subtitle_st) {
                while (is->subpq.frame_queue_nb_remaining() > 0) {
                    sp = is->subpq.frame_queue_peek();

                    if (is->subpq.frame_queue_nb_remaining() > 1)
                        sp2 = is->subpq.frame_queue_peek_next();
                    else
                        sp2 = NULL;

                    if (sp->serial != is->subtitleq.serial
                            || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                            || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                    {
                        if (sp->uploaded) {
                            unsigned int i;
                            for (i = 0; i < sp->sub.num_rects; i++) {
                                AVSubtitleRect *sub_rect = sp->sub.rects[i];
                                uint8_t *pixels;
                                int pitch, j;

                                if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
                                    for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                        memset(pixels, 0, sub_rect->w << 2);
                                    SDL_UnlockTexture(is->sub_texture);
                                }
                            }
                        }
                        is->subpq.frame_queue_next();
                    } else {
                        break;
                    }
                }
            }

            is->pictq.frame_queue_next();
            is->force_refresh = 1;

            if (is->step && !is->paused)
                stream_toggle_pause(is);
        }
display:
        /* display picture */
        if ( is->force_refresh &&  is->pictq.is_last_frame_shown())
            video_display(is);
    }
    is->force_refresh = 0;
    if (opt_show_status) {
        print_stream_status(is);
    }
}

void print_stream_status(VideoState* is)
{
    AVBPrint buf;
    static int64_t last_time = 0;
    int64_t cur_time;
    int aqsize, vqsize, sqsize;
    double av_diff;

    cur_time = av_gettime_relative();
    if (last_time && (cur_time - last_time) < 30000) 
    {
        return;
    }
    
    
    aqsize = 0;
    vqsize = 0;
    sqsize = 0;
    if (is->audio_st)
        aqsize = is->audioq.size;
    if (is->video_st)
        vqsize = is->videoq.size;
    if (is->subtitle_st)
        sqsize = is->subtitleq.size;
    av_diff = 0;
    if (is->audio_st && is->video_st)
        av_diff = is->audclk.get_clock() - is->vidclk.get_clock();
    else if (is->video_st)
        av_diff = get_master_clock(is) - is->vidclk.get_clock();
    else if (is->audio_st)
        av_diff = get_master_clock(is) - is->audclk.get_clock();

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf,
        "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%" PRId64 "/%" PRId64 "   \r",
        get_master_clock(is),
        (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
        av_diff,
        is->frame_drops_early + is->frame_drops_late,
        aqsize / 1024,
        vqsize / 1024,
        sqsize,
        is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
        is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);

    if (opt_show_status == 1 && AV_LOG_INFO > av_log_get_level())
        fprintf(stderr, "%s", buf.str);
    else
        av_log(NULL, AV_LOG_INFO, "%s", buf.str);

    fflush(stderr);
    av_bprint_finalize(&buf, NULL);

    last_time = cur_time;    
}

int queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;

#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif

    if (!(vp = is->pictq.frame_queue_peek_writable()))
        return -1;

    vp->sar = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;

    g_render.set_default_window_size(vp->width, vp->height, vp->sar);

    av_frame_move_ref(vp->frame, src_frame);
    is->pictq.frame_queue_push();
    return 0;
}

int get_video_frame(VideoState *is, AVFrame *frame)
{
    int got_picture;

    if ((got_picture = is->viddec.decoder_decode_frame( frame, NULL)) < 0)
        return -1;

    if (!got_picture)
    {
        return 0;
    }
    
    
    double dpts = NAN;

    if (frame->pts != AV_NOPTS_VALUE)
        dpts = av_q2d(is->video_st->time_base) * frame->pts;

    frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

    if (opt_framedrop >0 || (opt_framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) {
        // 看看是否有必要抛弃帧
        if (frame->pts != AV_NOPTS_VALUE) {
            double diff = dpts - get_master_clock(is);
            if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                diff - is->frame_last_filter_delay < 0 &&
                is->viddec.pkt_serial == is->vidclk.serial &&
                is->videoq.nb_packets) 
            {
                // 抛弃帧
                is->frame_drops_early++;
                av_frame_unref(frame);
                got_picture = 0;
            }
        }
    }
    
    return got_picture;
}


int audio_thread(void *arg)
{
    VideoState *is = (VideoState*)arg;
    AVFrame *frame = av_frame_alloc();
    Frame *af;

    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        if ((got_frame = is->auddec.decoder_decode_frame( frame, NULL)) < 0)
            goto the_end;

        if (!got_frame)
        {
            continue;
        }
        
        
        tb.num = 1;
        tb.den = frame->sample_rate ;


            if (!(af = is->sampq.frame_queue_peek_writable()))
                goto the_end;

            af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            af->pos = frame->pkt_pos;
            af->serial = is->auddec.pkt_serial;

            AVRational szr_dur = { frame->nb_samples, frame->sample_rate };
            af->duration = av_q2d(szr_dur);

            av_frame_move_ref(af->frame, frame);
            is->sampq.frame_queue_push();

        
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

 the_end:
    av_frame_free(&frame);
    return ret;
}

int Decoder::decoder_start( int (*fn)(void *), const char *thread_name, void* arg)
{
    this->queue->packet_queue_start();
    int i = this->decoder_thread.create_thread_with_cb(fn, thread_name, arg);
    if (i) {
        return AVERROR(ENOMEM); // 底层已经log
    }
    return 0;
}

int video_thread(void *arg)
{
    VideoState *is = (VideoState*)arg;
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = is->video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);


    if (!frame)
        return AVERROR(ENOMEM);

    for (;;) {
        ret = get_video_frame(is, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;

            AVRational szr_dur = { frame_rate.den, frame_rate.num };
            duration = (frame_rate.num && frame_rate.den ? av_q2d(szr_dur) : 0);
            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            ret = queue_picture(is, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
            av_frame_unref(frame);

        if (ret < 0)
            goto the_end;
    }
 the_end:
    av_frame_free(&frame);
    return 0;
}

int subtitle_thread(void *arg)
{
    VideoState *is = (VideoState*)arg;
    Frame *sp;
    int got_subtitle;
    double pts;

    for (;;) {
        if (!(sp = is->subpq.frame_queue_peek_writable()))
            return 0;

        if ((got_subtitle = is->subdec.decoder_decode_frame( NULL, &sp->sub)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = is->subdec.pkt_serial;
            sp->width = is->subdec.avctx->width;
            sp->height = is->subdec.avctx->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            is->subpq.frame_queue_push();
        } else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}

/* copy samples for viewing in editor window */
void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
int synchronize_audio(VideoState *is, int nb_samples)
{
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER) {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = is->audclk.get_clock() - get_master_clock(is);

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                        diff, avg_diff, wanted_nb_samples - nb_samples,
                        is->audio_clock, is->audio_diff_threshold);
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}

/**
 * Decode one audio frame and return its uncompressed size.
 *
 * The processed audio frame is decoded, converted if required, and
 * stored in is->audio_buf, with size in bytes given by the return
 * value.
 */
int audio_decode_frame(VideoState *is)
{
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    if (is->paused)
        return -1;

    do {
#if defined(_WIN32)
        while (is->sampq.frame_queue_nb_remaining() == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        if (!(af = is->sampq.frame_queue_peek_readable()))
            return -1;
        is->sampq.frame_queue_next();
    } while (af->serial != is->audioq.serial);

    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
                                           af->frame->nb_samples,
                                           (AVSampleFormat)af->frame->format, 1);

    dec_channel_layout =
        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

    if (af->frame->format        != is->audio_src.fmt            ||
        dec_channel_layout       != is->audio_src.channel_layout ||
        af->frame->sample_rate   != is->audio_src.freq           ||
        (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx)) {
        swr_free(&is->swr_ctx);
        is->swr_ctx = swr_alloc_set_opts(NULL,
                                         is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
                                         dec_channel_layout,   (AVSampleFormat) af->frame->format, af->frame->sample_rate,
                                         0, NULL);
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
                    is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            return -1;
        }
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels       = af->frame->channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = (AVSampleFormat) af->frame->format;
    }

    if (is->swr_ctx) {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        uint8_t **out = &is->audio_buf1;
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
        int out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
                                        wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    } else {
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;
    is->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->audio_clock - last_clock,
               is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}

/* prepare a new audio buffer */
void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = (VideoState *)opaque;
    int audio_size, len1;

    audio_callback_time = av_gettime_relative();

    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
           audio_size = audio_decode_frame(is);
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf = NULL;
               is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           } else {
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        if (!is->muted && is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME)
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        else {
            memset(stream, 0, len1);
            if (!is->muted && is->audio_buf)
                SDL_MixAudioFormat(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, AUDIO_S16SYS, len1, is->audio_volume);
        }
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
    is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(is->audio_clock)) {
        is->audclk.set_clock_at( is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec
            , is->audio_clock_serial
            , audio_callback_time / 1000000.0);
        is->extclk.sync_clock_to_slave( &is->audclk);
    }
}

int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    SDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;
    while (!(g_render.audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    return spec.size;
}

/* open a given stream. Return 0 if OK */
int stream_component_open(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;
    AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts = NULL;
    AVDictionaryEntry *t = NULL;
    int sample_rate, nb_channels;
    int64_t channel_layout;
    int ret = 0;
    int stream_lowres = lowres;

    if (stream_index < 0 || stream_index >= (int)ic->nb_streams)
        return -1;

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    codec = avcodec_find_decoder(avctx->codec_id);

    switch(avctx->codec_type){
        case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; forced_codec_name =    audio_codec_name; break;
        case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; forced_codec_name = subtitle_codec_name; break;
        case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; forced_codec_name =    video_codec_name; break;
    }
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
                                      "No codec could be found with name '%s'\n", forced_codec_name);
        else                   av_log(NULL, AV_LOG_WARNING,
                                      "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
                codec->max_lowres);
        stream_lowres = codec->max_lowres;
    }
    avctx->lowres = stream_lowres;

    if (fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;

    opts = filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_dict_set(&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret =  AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    is->eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:

        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;

        /* prepare audio output */
        if ((ret = audio_open(is, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
            goto fail;
        is->audio_hw_buf_size = ret;
        is->audio_src = is->audio_tgt;
        is->audio_buf_size  = 0;
        is->audio_buf_index = 0;

        /* init averaging filter */
        is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
           we correct audio sync only if larger than this threshold */
        is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];

        is->auddec.decoder_init(avctx, &is->audioq, &is->continue_read_thread);
        if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
            is->auddec.start_pts = is->audio_st->start_time;
            is->auddec.start_pts_tb = is->audio_st->time_base;
        }
        if ((ret = is->auddec.decoder_start(audio_thread, "audio_decoder", is)) < 0)
            goto out;
        SDL_PauseAudioDevice(g_render.audio_dev, 0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];

        is->viddec.decoder_init( avctx, &is->videoq, &is->continue_read_thread);
        if ((ret = is->viddec.decoder_start(video_thread, "video_decoder", is)) < 0)
            goto out;
        is->queue_attachments_req = 1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_stream = stream_index;
        is->subtitle_st = ic->streams[stream_index];

        is->subdec.decoder_init( avctx, &is->subtitleq, &is->continue_read_thread);
        if ((ret = is->subdec.decoder_start(subtitle_thread, "subtitle_decoder", is)) < 0)
            goto out;
        break;
    default:
        break;
    }
    goto out;

fail:
    avcodec_free_context(&avctx);
out:
    av_dict_free(&opts);

    return ret;
}

int VideoState::decode_interrupt_cb(void *ctx)
{
    VideoState *is = (VideoState*)ctx;
    return is->abort_request;
}

int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue) {
    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}

int is_realtime(AVFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
       || !strcmp(s->iformat->name, "rtsp")
       || !strcmp(s->iformat->name, "sdp")
    )
        return 1;

    if(s->pb && (   !strncmp(s->url, "rtp:", 4)
                 || !strncmp(s->url, "udp:", 4)
                )
    )
        return 1;
    return 0;
}

/* this thread gets the stream from the disk or the network */

unsigned VideoState::run()
{
    AVFormatContext * format_context = NULL;
    int err;
    unsigned ret = 1;
    int st_index[AVMEDIA_TYPE_NB];
    AVPacket pkt1, *pkt = &pkt1;
    int64_t stream_start_time;
    int pkt_in_play_range = 0;
    AVDictionaryEntry *t;
    //SDL_mutex *wait_mutex = SDL_CreateMutex();
    int scan_all_pmts_set = 0;
    int64_t pkt_ts;

    memset(st_index, -1, sizeof(st_index));
    this->eof = 0;

    // 1. 打开文件
    format_context = avformat_alloc_context();
    if (!format_context) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    format_context->interrupt_callback.callback = decode_interrupt_cb;
    format_context->interrupt_callback.opaque = this;
    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {  // 命令行上，如果用户指定了格式，还可以配一些参数
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    err = avformat_open_input(&format_context, this->filename, this->iformat, &format_opts);
    if (err < 0) {
        print_error(this->filename, err);
        ret = -1;
        goto fail;
    }
    if (scan_all_pmts_set)
        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

    if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }
    this->ic = format_context;

    if (genpts)
        format_context->flags |= AVFMT_FLAG_GENPTS;

    av_format_inject_global_side_data(format_context);
    
    unsigned int i;    
    {
        AVDictionary **opts = setup_find_stream_info_opts(format_context, codec_opts); // 'codec_opts' 命令行中codec相关选项
        unsigned int orig_nb_streams = format_context->nb_streams;

        err = avformat_find_stream_info(format_context, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (err < 0) {
            av_log(NULL, AV_LOG_WARNING,
                   "%s: could not find codec parameters\n", this->filename);
            ret = -1;
            goto fail;
        }
    }

    if (format_context->pb)
        format_context->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end
    
    this->max_frame_duration = (format_context->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    if (!g_window_title && (t = av_dict_get(format_context->metadata, "title", NULL, 0)))
        g_window_title = av_asprintf("%s - %s", t->value, opt_input_filename);

    /* if seeking requested, we execute it */
    if (opt_start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = opt_start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += format_context->start_time;
        ret = avformat_seek_file(format_context, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                this->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    this->realtime = is_realtime(format_context);

    if (opt_show_status)
        av_dump_format(format_context, 0, this->filename, 0);

    // 2. 逐个打开流

    // 2.1 av_find_best_stream
    for (i = 0; i < format_context->nb_streams; i++) {
        st_index[i] = -1;        
    }
       
    st_index[AVMEDIA_TYPE_VIDEO] =
        av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO,
                            st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);

    if (!opt_audio_disable)
        st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
    if ( !subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =
            av_find_best_stream(format_context, AVMEDIA_TYPE_SUBTITLE,
                                st_index[AVMEDIA_TYPE_SUBTITLE],
                                (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                 st_index[AVMEDIA_TYPE_AUDIO] :
                                 st_index[AVMEDIA_TYPE_VIDEO]),
                                NULL, 0);

    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream *st = format_context->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecParameters *codec_para = st->codecpar;
        AVRational sar = av_guess_sample_aspect_ratio(format_context, st, NULL);
        if (codec_para->width)
            g_render.set_default_window_size(codec_para->width, codec_para->height, sar);
    }

    // 2.2 open the streams 
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        stream_component_open(this, st_index[AVMEDIA_TYPE_AUDIO]);
    }
        
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret = stream_component_open(this, st_index[AVMEDIA_TYPE_VIDEO]);
    }

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        stream_component_open(this, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    if (this->video_stream < 0 && this->audio_stream < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               this->filename);
        ret = -1;
        goto fail;
    }

    if (opt_infinite_buffer < 0 && this->realtime)
        opt_infinite_buffer = 1;

    // 3. real loop
    for (;;) {
        // 3.1 如果break
        if (this->abort_request)
            break;

        // 3.2 处理pause
        if (this->paused != this->last_paused) {
            this->last_paused = this->paused;
            if (this->paused)
                this->read_pause_return = av_read_pause(format_context);
            else
                av_read_play(format_context);
        }

        if (this->paused &&
                (!strcmp(this->iformat->name, "rtsp") ||
                 (format_context->pb && !strncmp(opt_input_filename, "mmsh:", 5)))) {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
            SDL_Delay(10);
            continue;
        }


        // 3.3 处理seek
        if (this->seek_req) {
            int64_t seek_target = this->seek_pos;
            int64_t seek_min    = this->seek_rel > 0 ? seek_target - this->seek_rel + 2: INT64_MIN;
            int64_t seek_max    = this->seek_rel < 0 ? seek_target - this->seek_rel - 2: INT64_MAX;
// FIXME the +-2 is due to rounding being not done in the correct direction in generation
//      of the seek_pos/seek_rel variables

            ret = avformat_seek_file(this->ic, -1, seek_min, seek_target, seek_max, this->seek_flags);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", this->ic->url);
            } else {
                if (this->audio_stream >= 0) {
                    this->audioq.packet_queue_flush();
                    this->audioq.packet_queue_put(&PacketQueue::flush_pkt);
                }
                if (this->subtitle_stream >= 0) {
                    this->subtitleq.packet_queue_flush();
                    this->subtitleq.packet_queue_put(&PacketQueue::flush_pkt);
                }
                if (this->video_stream >= 0) {
                    this->videoq.packet_queue_flush();
                    this->videoq.packet_queue_put(&PacketQueue::flush_pkt);
                }
                if (this->seek_flags & AVSEEK_FLAG_BYTE) {
                    this->extclk.set_clock( NAN, 0);
                } else {
                    this->extclk.set_clock( seek_target / (double)AV_TIME_BASE, 0);
                }
            }
            this->seek_req = 0;
            this->queue_attachments_req = 1;
            this->eof = 0;
            if (this->paused)
                step_to_next_frame(this);
        }
        if (this->queue_attachments_req) {
            if (this->video_st && this->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                AVPacket copy;
                if ((ret = av_packet_ref(&copy, &this->video_st->attached_pic)) < 0)
                    goto fail;
                this->videoq.packet_queue_put( &copy);
                this->videoq.packet_queue_put_nullpacket(this->video_stream);
            }
            this->queue_attachments_req = 0;
        }

        // 3.4 准备读入packet

        /* if the queue are full, no need to read more */
        if (opt_infinite_buffer <1 &&
              (this->audioq.size + this->videoq.size + this->subtitleq.size > MAX_QUEUE_SIZE
            || (stream_has_enough_packets(this->audio_st, this->audio_stream, &this->audioq) &&
                stream_has_enough_packets(this->video_st, this->video_stream, &this->videoq) &&
                stream_has_enough_packets(this->subtitle_st, this->subtitle_stream, &this->subtitleq))))
        {
            /* wait 10 ms */
            this->continue_read_thread.timed_wait(10);
            continue;
        }

        ret = av_read_frame(format_context, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(format_context->pb)) && !this->eof) {
                if (this->video_stream >= 0)
                    this->videoq.packet_queue_put_nullpacket( this->video_stream);
                if (this->audio_stream >= 0)
                    this->audioq.packet_queue_put_nullpacket( this->audio_stream);
                if (this->subtitle_stream >= 0)
                    this->subtitleq.packet_queue_put_nullpacket( this->subtitle_stream);
                this->eof = 1;
            }
            if (format_context->pb && format_context->pb->error) {
                if (autoexit)
                    goto fail;
                else
                    break;
            }

            this->continue_read_thread.timed_wait(10);
            continue;
        } else {
            this->eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = format_context->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        pkt_in_play_range = opt_duration == AV_NOPTS_VALUE ||
                (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                av_q2d(format_context->streams[pkt->stream_index]->time_base) -
                (double)(opt_start_time != AV_NOPTS_VALUE ? opt_start_time : 0) / 1000000 <= ((double)opt_duration / 1000000);
        if (pkt->stream_index == this->audio_stream && pkt_in_play_range) {
            this->audioq.packet_queue_put( pkt);
        } else if (pkt->stream_index == this->video_stream && pkt_in_play_range
                   && !(this->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            this->videoq.packet_queue_put( pkt);
        } else if (pkt->stream_index == this->subtitle_stream && pkt_in_play_range) {
            this->subtitleq.packet_queue_put( pkt);
        } else {
            av_packet_unref(pkt);
        }
    }

    ret = 0;
 fail:
    if (format_context && !this->ic)
        avformat_close_input(&format_context);

    if (ret != 0) {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = this;
        SDL_PushEvent(&event);
    }

    return 0;
}

void AutoReleasePtr<VideoState>::release()
{
    if (!me)
        return;
    me->close();
    //外界会delete
}

int VideoState::open(const char *filename, AVInputFormat *iformat)
{
    AutoReleasePtr<VideoState> close_if_failed(this);

    this->last_video_stream = this->video_stream = -1;
    this->last_audio_stream = this->audio_stream = -1;
    this->last_subtitle_stream = this->subtitle_stream = -1;
    this->filename = av_strdup(filename);
    if (!this->filename)
        return 1;

    this->iformat = iformat;
    this->ytop    = 0;
    this->xleft   = 0;

    // prepare packet queues and frame queues 
    if (this->pictq.frame_queue_init( &this->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        return 2;
    if (this->subpq.frame_queue_init( &this->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        return 3;
    if (this->sampq.frame_queue_init( &this->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        return 4;

    // init clocks 
    this->vidclk.init_clock(&this->videoq.serial);
    this->audclk.init_clock( &this->audioq.serial);
    this->extclk.init_clock( &this->extclk.serial);
    this->audio_clock_serial = -1;

    if (opt_startup_volume < 0)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", opt_startup_volume);
    if (opt_startup_volume > 100)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", opt_startup_volume);
    this->audio_volume = opt_startup_volume;

    this->audio_volume = av_clip(this->audio_volume, 0, 100);
    this->audio_volume = av_clip(SDL_MIX_MAXVOLUME * this->audio_volume / 100, 0, SDL_MIX_MAXVOLUME);
    
    this->muted = 0;
    this->av_sync_type = opt_av_sync_type;
    this->create_thread();
    
    close_if_failed.dismiss();
    return 0;
}

void stream_cycle_channel(VideoState *is, int codec_type)
{
    AVFormatContext *ic = is->ic;
    int start_index, stream_index;
    int old_index;
    AVStream *st;
    AVProgram *p = NULL;
    int nb_streams = is->ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = is->last_video_stream;
        old_index = is->video_stream;
    } else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = is->last_audio_stream;
        old_index = is->audio_stream;
    } else {
        start_index = is->last_subtitle_stream;
        old_index = is->subtitle_stream;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
        p = av_find_program_from_stream(ic, NULL, is->video_stream);
        if (p) {
            nb_streams = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index = -1;
            stream_index = start_index;
        }
    }

    for (;;) {
        if (++stream_index >= nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                is->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codecpar->codec_type == codec_type) {
            /* check that parameters are OK */
            switch (codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (st->codecpar->sample_rate != 0 &&
                    st->codecpar->channels != 0)
                    goto the_end;
                break;
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
 the_end:
    if (p && stream_index != -1)
        stream_index = p->stream_index[stream_index];
    av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
           av_get_media_type_string((AVMediaType)codec_type),
           old_index,
           stream_index);

    stream_component_close(is, old_index);
    stream_component_open(is, stream_index);
}


void seek_chapter(VideoState *is, int incr)
{
    int64_t pos = (int64_t) ( get_master_clock(is) * AV_TIME_BASE );
    int i;

    if (!is->ic->nb_chapters)
        return;

    AVRational time_base_q = { 1, AV_TIME_BASE };

    /* find the current chapter */
    for (i = 0; i < (int) is->ic->nb_chapters; i++) {
        AVChapter *ch = is->ic->chapters[i];
                
        if (av_compare_ts(pos, time_base_q, ch->start, ch->time_base) < 0) {
            i--;
            break;
        }
    }

    i += incr;
    i = FFMAX(i, 0);
    if (i >= (int) is->ic->nb_chapters)
        return;

    av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
    stream_seek(is, av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,
        time_base_q), 0, 0);
}

