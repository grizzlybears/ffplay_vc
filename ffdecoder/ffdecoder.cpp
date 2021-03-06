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

extern "C" {
#include "src/cmdutils.h"
}

/* options specified by the user */
AVInputFormat * opt_file_iformat;
const char * opt_input_filename;
  
 int opt_subtitle_disable = 0;
 int opt_show_status = -1; //原来是 -1;
 int opt_av_sync_type = AV_SYNC_AUDIO_MASTER;
 int64_t opt_start_time = AV_NOPTS_VALUE;
 int64_t opt_duration = AV_NOPTS_VALUE;
 int opt_decoder_reorder_pts = -1;
 int opt_autoexit = 0;
 int opt_infinite_buffer = -1;


  int opt_full_screen = 0;


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

AVPacket PacketQueue::flush_pkt;

int PacketQueue::is_flush_pkt(const AVPacket& to_check)
{
    return to_check.data == flush_pkt.data;
}

int PacketQueue::packet_queue_put_private( AVPacket *pkt)
{
    MyAVPacketListNode* pkt1;

    if (this->abort_request)
       return -1;

    pkt1 = (MyAVPacketListNode*) av_malloc(sizeof(MyAVPacketListNode));
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

    this->total_duration += pkt1->pkt.duration;
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
    MyAVPacketListNode *pkt, *pkt1;

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
    this->total_duration = 0;
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
    MyAVPacketListNode *pkt1;
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
            this->total_duration -= pkt1->pkt.duration;
            
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

void AutoReleasePtr<AVCodecContext>::release()
{
    if (!me)
        return;

    avcodec_free_context(&me);
    me = NULL;
}

//     decoder section {{{
AVCodecContext* Decoder::create_codec(AVFormatContext* format_context, int stream_id)
{
    AVCodecContext* avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return NULL;

    AutoReleasePtr<AVCodecContext> guard(avctx);

    int ret = avcodec_parameters_to_context(avctx, format_context->streams[stream_id]->codecpar);
    if (ret < 0)
        return NULL;
    avctx->pkt_timebase = format_context->streams[stream_id]->time_base;

    AVCodec* codec = avcodec_find_decoder(avctx->codec_id);
    if (!codec) {
        av_log(NULL, AV_LOG_WARNING,
            "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        return NULL;
    }
    avctx->codec_id = codec->id;
    if ((ret = avcodec_open2(avctx, codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_WARNING, "Failed to open  codec %d(%s), LE = %d\n", codec->id, avcodec_get_name(codec->id), ret);
        return NULL;
    }

    guard.dismiss();
    return avctx;
}

int Decoder::decoder_init( AVCodecContext *avctx, int stream_id, AVStream* stream, SimpleConditionVar* empty_queue_cond)
{
    this->avctx = avctx;
    this->stream_id = stream_id;
    this->stream = stream;
    
    this->empty_pkt_queue_cond = empty_queue_cond;
    this->start_pts = AV_NOPTS_VALUE;
    this->pkt_serial = -1;

    return 0;
}

int Decoder::decoder_decode_frame(AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    for (;;) {
        
        if (this->packet_q.serial == this->pkt_serial) {
            do {
                if (this->packet_q.abort_request)
                    return -1;

                // 在音视频流中顺序解码

                switch (this->avctx->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:
                        ret = avcodec_receive_frame(this->avctx, frame);
                        if (ret >= 0) {
                            // 成功解得frame
                            if (opt_decoder_reorder_pts == -1) { // let decoder reorder pts 0=off 1=on -1=auto
                                frame->pts = frame->best_effort_timestamp;
                            } else if (!opt_decoder_reorder_pts) {
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
            if (this->packet_q.nb_packets == 0) // todo: 为何要q里没包才wake()? reader thread是判断 decoder->stream_has_enough_packets() 决定是否继续读的
                this->empty_pkt_queue_cond->wake();
                
            if (this->is_packet_pending) {
                av_packet_move_ref(&pkt, &this->pending_pkt);
                this->is_packet_pending = 0;
            } else {
                if (this->packet_q.packet_queue_get( &pkt, 1 /*block until get*/, &this->pkt_serial) < 0)
                    return -1;
            }
            if (this->packet_q.serial == this->pkt_serial)
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

    this->packet_q.packet_queue_destroy();
    this->frame_q.frame_queue_destory();
}

void Decoder::decoder_abort()
{
    this->packet_q.packet_queue_abort();
    this->frame_q.frame_queue_signal();

    this->BaseThread::wait_thread_quit();  
    this->BaseThread::safe_cleanup();

    this->packet_q.packet_queue_flush();
}

int VideoDecoder::decoder_init(AVCodecContext* avctx, int stream_id, AVStream* stream, SimpleConditionVar* empty_queue_cond)
{
    if (MyBase::decoder_init(avctx, stream_id, stream, empty_queue_cond))
    {
        return 1;
    }

    if (this->frame_q.frame_queue_init(&this->packet_q, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        return 2;

    this->stream_clock.init_clock(&this->packet_q.serial);
    
    if (decoder_start())
    {
        return 3;
    }

    return 0;
}

void VideoDecoder::decoder_destroy() {
    MyBase::decoder_destroy();

    if (this->img_convert_ctx)
    {
        sws_freeContext(this->img_convert_ctx);
        this->img_convert_ctx = NULL;
    }
}

int AudioDecoder::decoder_init(AVCodecContext* avctx, int stream_id, AVStream* stream, SimpleConditionVar* empty_queue_cond)
{
    if (MyBase::decoder_init(avctx, stream_id, stream, empty_queue_cond))
    {
        return 1;
    }

    if (this->frame_q.frame_queue_init(&this->packet_q, SAMPLE_QUEUE_SIZE, 1) < 0)
        return 2;

    this->stream_clock.init_clock(&this->packet_q.serial);
    this->audio_clock_serial = -1;

    //////////////////////////////////////////////////////////////////////////
    
    this->audio_volume = av_clip(this->audio_volume, 0, 100);
    this->audio_volume = av_clip(SDL_MIX_MAXVOLUME * this->audio_volume / 100, 0, SDL_MIX_MAXVOLUME);

    this->muted = 0;


    /////////////////////////////////////////////////////////////////////////
    int sample_rate, nb_channels;
    int64_t channel_layout;

    sample_rate = avctx->sample_rate;
    nb_channels = avctx->channels;
    channel_layout = avctx->channel_layout;
    int ret;
    /* prepare audio output */
    if ((ret = AudioDecoder::audio_open(this, channel_layout, nb_channels, sample_rate, &this->audio_tgt)) < 0)
        return 3;  

    this->audio_hw_buf_size = ret;
    this->audio_src = this->audio_tgt;
    this->audio_buf_size = 0;
    this->audio_buf_index = 0;

    /* init averaging filter */
    this->audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
    this->audio_diff_avg_count = 0;
    /* since we do not have a precise anough audio FIFO fullness,
       we correct audio sync only if larger than this threshold */
    this->audio_diff_threshold = (double)(this->audio_hw_buf_size) / this->audio_tgt.bytes_per_sec;

    if ((this->_vs->format_context->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK))
        && !this->_vs->format_context->iformat->read_seek)
    {
        this->start_pts = this->stream->start_time;
        this->start_pts_tb = this->stream->time_base;
    }

    SDL_PauseAudioDevice(this->_vs->render.audio_dev, 0);
    
    if (decoder_start())
    {
        return 2;
    }

    return 0;

}

void AudioDecoder::decoder_destroy() {
    MyBase::decoder_destroy();
    this->_vs->render.close_audio();
    
    if (this->swr_ctx)
    {
        swr_free(&this->swr_ctx);
        this->swr_ctx = NULL;
    }

    if (this->audio_buf1)
    {
        av_freep(&this->audio_buf1);
        this->audio_buf1 = NULL;
        this->audio_buf1_size = 0;
    }
        
    this->audio_buf = NULL;
}
int SubtitleDecoder::decoder_init(AVCodecContext* avctx, int stream_id, AVStream* stream, SimpleConditionVar* empty_queue_cond)
{
    if (MyBase::decoder_init(avctx, stream_id, stream, empty_queue_cond))
    {
        return 1;
    }

    if (this->frame_q.frame_queue_init(&this->packet_q, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        return 2;

    if (decoder_start())
    {
        return 3;
    }
    return 0;
}

void SubtitleDecoder::decoder_destroy() {
    MyBase::decoder_destroy();

    if (this->sub_convert_ctx)
    {
        sws_freeContext(this->sub_convert_ctx);
        this->sub_convert_ctx = NULL;
    }
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

}

 void FrameQueue::frame_queue_signal()
{
     AutoLocker _yes_locked(this->fq_signal);
     this->fq_signal.wake();
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
        AutoLocker _yes_locked(this->fq_signal);
        while (this->size >= this->max_size && !this->pktq->abort_request)  // todo: 如果挂钩的packet queue 退了，怎么能 signal 本 FrameQueue的 cond?
        {
            this->fq_signal.wait();
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
        AutoLocker _yes_locked(this->fq_signal);
        while (this->size - this->rindex_shown <= 0 &&
            !this->pktq->abort_request) // todo: 如果挂钩的packet queue 退了，怎么能 signal 本 FrameQueue的 cond?
        {
            this->fq_signal.wait();
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

    AutoLocker _yes_locked(this->fq_signal);
    this->size++;

    this->fq_signal.wake();
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

    AutoLocker _yes_locked(this->fq_signal);
    this->size--;
    this->fq_signal.wake();

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
        , SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, this->screen_width, this->screen_height
        , cw_flags))
    {
        return 3;
    }

    int default_display = 0;
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(default_display, &dm))
    {
        av_log(NULL, AV_LOG_WARNING, "Failed in SDL_GetDesktopDisplayMode . LE = %s\n", SDL_GetError());
    }
    else
    {
        this->screen_width = dm.w;
        this->screen_height =dm.h;

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
    
    screen_width  = rect.w;
    screen_height = rect.h;
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

void Render::calculate_display_rect(SDL_Rect *rect,
                                   int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                   int pic_width, int pic_height, AVRational pic_sar)
{
    AVRational aspect_ratio = pic_sar;
    int64_t width, height, x, y;

    if (scr_width >= pic_width && scr_height >= pic_height)
    {
        // don't enlarge
        width = pic_width;
        height = pic_height;
    }
    else
    {
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
    if (realloc_texture(tex, sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt, frame->width, frame->height, sdl_blendmode, 0) < 0)
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

Frame* VideoState::get_current_subtitle_frame( Frame* current_video_frame)
{
    if (!this->subdec.stream) 
    {
        return NULL;
    }

    if (this->subdec.frame_q.frame_queue_nb_remaining() <= 0)
    {
        return NULL;
    }
    
    Frame* sp =  this->subdec.frame_q.frame_queue_peek();
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

    if (this->render.realloc_texture(&this->render.sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
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

        this->subdec.sub_convert_ctx = sws_getCachedContext(this->subdec.sub_convert_ctx,
            sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
            sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
            0, NULL, NULL, NULL);
        if (!this->subdec.sub_convert_ctx) {
            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
            return NULL;
        }

        if (!SDL_LockTexture(this->render.sub_texture, (SDL_Rect*)sub_rect, (void**)pixels, pitch)) {
            sws_scale(this->subdec.sub_convert_ctx, (const uint8_t* const*)sub_rect->data, sub_rect->linesize,
                0, sub_rect->h, pixels, pitch);
            SDL_UnlockTexture(this->render.sub_texture);
        }
    }
    sp->uploaded = 1;
        
    return sp;
}

void VideoDecoder::video_image_display()
{
    Frame *vp;
    Frame *sp = NULL;
    SDL_Rect rect;

    vp = this->frame_q.frame_queue_peek_last();

    sp = this->_vs->get_current_subtitle_frame( vp);

    Render::calculate_display_rect(&rect, this->xleft, this->ytop, this->width, this->height, vp->width, vp->height, vp->sample_aspect_ratio);

    if (!vp->uploaded) {
        if (this->_vs->render.upload_texture(&this->_vs->render.vid_texture, vp->frame, &this->img_convert_ctx) < 0)
            return;
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
    }

    this->_vs->render.show_texture(vp, rect, sp ? 1 : 0);
}

void Render::show_texture(const Frame* video_frame, const SDL_Rect& rect, int show_subtitle)
{
    Render::set_sdl_yuv_conversion_mode(video_frame->frame);
    SDL_RenderCopyEx(this->renderer, this->vid_texture, NULL, &rect, 0, NULL, (SDL_RendererFlip)(video_frame->flip_v ? SDL_FLIP_VERTICAL : 0));
    Render::set_sdl_yuv_conversion_mode(NULL);

    if (show_subtitle) {
        SDL_RenderCopy(this->renderer, this->sub_texture, NULL, &rect);
    }
}

//  }}} render section 


void VideoState::stream_component_close( int stream_index)
{
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= (int)this->format_context->nb_streams)
        return;
    codecpar = format_context->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        this->auddec.decoder_abort();
        this->auddec.decoder_destroy();

        break;
    case AVMEDIA_TYPE_VIDEO:
        this->viddec.decoder_abort();
        this->viddec.decoder_destroy();
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        this->subdec.decoder_abort();
        this->subdec.decoder_destroy();
        break;
    default:
        break;
    }

    this->format_context->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        this->auddec.stream = NULL;
        this->auddec.stream_id = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        this->viddec.stream = NULL;
        this->viddec.stream_id = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        this->subdec.stream = NULL;
        this->subdec.stream_id = -1;
        break;
    default:
        break;
    }
}

void VideoState::close_input_stream()
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    this->abort_request = 1;
    this->wait_thread_quit();

    /* close each stream */
    if (this->auddec.stream_id >= 0)
        stream_component_close(this->auddec.stream_id);
    if (this->viddec.stream_id >= 0)
        stream_component_close(this->viddec.stream_id);
    if (this->subdec.stream_id >= 0)
        stream_component_close( this->subdec.stream_id);

    avformat_close_input(&this->format_context);

    av_free(this->filename);
    this->filename = NULL;
        

}


int VideoDecoder::video_open()
{
    if ("" == this->_vs->render.window_title)
    {
        this->_vs->render.window_title = opt_input_filename;
    }

    this->_vs->render.show_window(this->_vs->render.window_title.GetString()
        , this->_vs->render.screen_width, this->_vs->render.screen_height, this->_vs->render.screen_left, this->_vs->render.screen_top
        , opt_full_screen);
    
    this->width  = this->_vs->render.screen_width;
    this->height = this->_vs->render.screen_height;

    return 0;
}

/* display the current picture, if any */
void VideoDecoder::video_display()
{
    if (!this->width)
        this->video_open();

    this->_vs->render.clear_render();
    
    if (this->stream)
        this->video_image_display();

    this->_vs->render.draw_render();
    
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

int VideoState::get_master_sync_type() {
    if (this->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (this->viddec.stream)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    } else if (this->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (this->auddec.stream)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    } else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
double VideoState::get_master_clock()
{
    double val;

    switch (this->get_master_sync_type()) {
        case AV_SYNC_VIDEO_MASTER:
            val = this->viddec.stream_clock.get_clock();
            break;
        case AV_SYNC_AUDIO_MASTER:
            val = this->auddec.stream_clock.get_clock();
            break;
        default:
            val = this->extclk.get_clock();
            break;
    }
    return val;
}

void VideoState::check_external_clock_speed() {
   if (this->viddec.stream_id >= 0 && this->viddec.packet_q.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
       this->auddec.stream_id >= 0 && this->auddec.packet_q.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES)
   {
       // 降速一档
       this->extclk.set_clock_speed( FFMAX(EXTERNAL_CLOCK_SPEED_MIN, this->extclk.get_clock_speed() - EXTERNAL_CLOCK_SPEED_STEP));  
   }
   else if ((this->viddec.stream_id < 0 || this->viddec.packet_q.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
              (this->auddec.stream_id < 0 || this->auddec.packet_q.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES))
   {
       // 加速一档
       this->extclk.set_clock_speed( FFMIN(EXTERNAL_CLOCK_SPEED_MAX, this->extclk.get_clock_speed() + EXTERNAL_CLOCK_SPEED_STEP));
   } 
   else 
   {
       double speed = this->extclk.get_clock_speed();
       if (speed != 1.0)  // 向‘原速’靠近一档
           this->extclk.set_clock_speed( speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
   }
}

/* seek in the stream */
void VideoState::stream_seek(int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!this->seek_req) {
        this->seek_pos = pos;
        this->seek_rel = rel;
        this->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
            this->seek_flags |= AVSEEK_FLAG_BYTE;
        this->seek_req = 1;

        this->continue_read_thread.wake();
    }
}

/* pause or resume the video */
void VideoState::internal_toggle_pause()
{
    if (this->paused) {
        this->viddec.frame_timer += av_gettime_relative() / 1000000.0 - this->viddec.stream_clock.get_last_set_point();
        if (this->read_pause_return != AVERROR(ENOSYS)) {
            this->viddec.stream_clock.paused = 0;
        }
        this->viddec.stream_clock.set_clock(this->viddec.stream_clock.get_clock(), this->viddec.stream_clock.serial);
    }

    this->extclk.set_clock(this->extclk.get_clock(), this->extclk.serial);

    this->paused = this->auddec.stream_clock.paused = this->viddec.stream_clock.paused = this->extclk.paused = !this->paused;
}

void VideoState::toggle_pause()
{
    internal_toggle_pause();
    this->step = 0;
}

void VideoState::toggle_mute( )
{
    this->auddec.muted = !this->auddec.muted;
}

void VideoState::update_volume( int sign, double step)
{
    double volume_level = this->auddec.audio_volume ? (20 * log(this->auddec.audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    this->auddec.audio_volume = av_clip(this->auddec.audio_volume == new_volume ? (this->auddec.audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

void VideoState::step_to_next_frame()
{
    /* if the stream is paused unpause it, then step */
    if (this->paused)
        this->internal_toggle_pause();
    this->step = 1;
}

double VideoState::compute_target_delay(double delay)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (AV_SYNC_VIDEO_MASTER != this->get_master_sync_type() ) {
        /* if video is slave, we try to correct big delays by duplicating or deleting a frame */
        diff = this->viddec.stream_clock.get_clock() - this->get_master_clock();

        /* skip or repeat frame. We take into account the delay to compute the threshold. 
        I still don't know if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));  // sync_threshold is in range [AV_SYNC_THRESHOLD_MIN, AV_SYNC_THRESHOLD_MAX]
        if (!isnan(diff) && fabs(diff) < this->max_frame_duration) { // diff 如果在 [-sync_threshold, +sync_threshold] 范围内，就不调整了
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);   // V钟慢了(-diff) ，因此从‘应显示时间’里扣掉 (-diff))，即 +diff
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff; // V钟快得很多，超过了AV_SYNC_FRAMEDUP_THRESHOLD，则一次性补足，即 +diff
            else if (diff >= sync_threshold) 
                delay = 2 * delay; // V钟快了(diff)，但超过AV_SYNC_FRAMEDUP_THRESHOLD，则原时长翻倍
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
            delay, -diff);

    return delay;
}

double VideoState::vp_duration(Frame *vp, Frame *nextvp) {
    if (vp->serial == nextvp->serial) {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0 || duration > this->max_frame_duration)
            return vp->duration; // 前後兩幀pts的距离不合理
        else
            return duration;
    } else {
        return 0.0;
    }
}

void VideoState::update_video_clock(double pts, int64_t pos, int serial) {
    /* update current video pts */
    viddec.stream_clock.set_clock( pts, serial);
    extclk.sync_clock_to_slave( &viddec.stream_clock);
}

/* called to display each frame */
void VideoState::video_refresh(double *remaining_time)
{
    if (!this->paused && this->get_master_sync_type() == AV_SYNC_EXTERNAL_CLOCK && this->realtime)
        this->check_external_clock_speed();

    if (this->viddec.stream) {
        prepare_picture_for_display(remaining_time);

        /* display picture */
        if (this->force_refresh && this->viddec.frame_q.is_last_frame_shown())
            this->viddec.video_display();
    }
    this->force_refresh = 0;
    if (opt_show_status) {
        this->print_stream_status();
    }
}

void VideoState::prepare_picture_for_display(double* remaining_time)
{
    Frame* vp, * lastvp;
retry:    
    if (this->viddec.frame_q.frame_queue_nb_remaining() == 0) {
        // nothing to do, no picture to display in the frame queue
        return;
    }
    /* dequeue the picture */
    lastvp = this->viddec.frame_q.frame_queue_peek_last(); //‘已经上屏的帧’
    vp = this->viddec.frame_q.frame_queue_peek(); //‘接下来要上屏的帧’

    if (vp->serial != this->viddec.packet_q.serial) { // 说明seek过，vp 是seek前cache的，已经不合时宜
        this->viddec.frame_q.frame_queue_next();
        goto retry;
    }
    
    if (lastvp->serial != vp->serial)
        this->viddec.frame_timer = av_gettime_relative() / 1000000.0;

    if (this->paused)
        return;

    double time_now, last_duration, duration, delay;
    Frame* sp, * sp2;

    /* compute nominal last_duration */
    last_duration = this->vp_duration(lastvp, vp);      // 根据pts计算出名义上lastvp应该显示多久
    delay = this->compute_target_delay(last_duration);   // 根据‘时钟同步’的要求，再调整‘last_duration’,得到delay = ‘lastvp应该显示多久’

    time_now = av_gettime_relative() / 1000000.0;
    if (time_now < this->viddec.frame_timer + delay) {
        *remaining_time = FFMIN(this->viddec.frame_timer + delay - time_now, *remaining_time);  //‘当前显示帧’还可以再坚持‘*remaining_time’之久
        return;
    }

    // 要显示下一帧了
    this->viddec.frame_timer += delay;
    if (delay > 0 && time_now - this->viddec.frame_timer > AV_SYNC_THRESHOLD_MAX)
        this->viddec.frame_timer = time_now;

    {
        AutoLocker yes_locked(this->viddec.frame_q.fq_signal);
        if (!isnan(vp->pts))
            this->update_video_clock(vp->pts, vp->pos, vp->serial);  // 其实是更新 vstream的clock，以及‘外部时钟’
    }

    if (this->viddec.frame_q.frame_queue_nb_remaining() > 1) { // 考察一下是否需要跳帧
        Frame* nextvp = this->viddec.frame_q.frame_queue_peek_next();
        duration = this->vp_duration(vp, nextvp);
        if (!this->step &&
            (get_master_sync_type() != AV_SYNC_VIDEO_MASTER) &&
            time_now > this->viddec.frame_timer + duration) // 没有时间留给'nextvp'显示，所以要跳过'nextvp'。 
        {
            this->frame_drops_late++;
            this->viddec.frame_q.frame_queue_next();   // todo: 如果我们可以动态调节‘显示刷新’的间隔，那么不跳帧，而把‘间隔’调到最小，应该提高体验
            goto retry;
        }
    }

    if (this->subdec.stream) {  // 酌情叠加字幕
        while (this->subdec.frame_q.frame_queue_nb_remaining() > 0) {
            sp = this->subdec.frame_q.frame_queue_peek();

            if (this->subdec.frame_q.frame_queue_nb_remaining() > 1)
                sp2 = this->subdec.frame_q.frame_queue_peek_next();
            else
                sp2 = NULL;

            if (sp->serial != this->subdec.packet_q.serial
                || (this->viddec.stream_clock.pts > (sp->pts + ((float)sp->sub.end_display_time / 1000)))
                || (sp2 && this->viddec.stream_clock.pts > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
            {
                if (sp->uploaded) {
                    unsigned int i;
                    for (i = 0; i < sp->sub.num_rects; i++) {
                        AVSubtitleRect* sub_rect = sp->sub.rects[i];
                        uint8_t* pixels;
                        int pitch, j;

                        if (!SDL_LockTexture(this->render.sub_texture, (SDL_Rect*)sub_rect, (void**)&pixels, &pitch)) {
                            for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                memset(pixels, 0, sub_rect->w << 2);
                            SDL_UnlockTexture(this->render.sub_texture);
                        }
                    }
                }
                this->subdec.frame_q.frame_queue_next();
            }
            else {
                break;
            }
        }
    }

    this->viddec.frame_q.frame_queue_next();
    this->force_refresh = 1;

    if (this->step && !this->paused)
        internal_toggle_pause();
}

void VideoState::print_stream_status()
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
    if (this->auddec.stream)
        aqsize = this->auddec.packet_q.size;
    if (this->viddec.stream)
        vqsize = this->viddec.packet_q.size;
    if (this->subdec.stream)
        sqsize = this->subdec.packet_q.size;
    av_diff = 0;
    if (this->auddec.stream && this->viddec.stream)
        av_diff = this->auddec.stream_clock.get_clock() - this->viddec.stream_clock.get_clock();
    else if (this->viddec.stream)
        av_diff = this->get_master_clock() - this->viddec.stream_clock.get_clock();
    else if (this->auddec.stream)
        av_diff = this->get_master_clock() - this->auddec.stream_clock.get_clock();

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf,
        "clock:%7.2f %s:%7.3f framedrop=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%" PRId64 "/%" PRId64 "   \r",
        this->get_master_clock(),
        (this->auddec.stream && this->viddec.stream) ? "A-V" : (this->viddec.stream ? "M-V" : (this->auddec.stream ? "M-A" : "   ")),
        av_diff,
        this->frame_drops_early + this->frame_drops_late,
        aqsize / 1024,
        vqsize / 1024,
        sqsize,
        this->viddec.stream ? this->viddec.avctx->pts_correction_num_faulty_dts : 0,
        this->viddec.stream ? this->viddec.avctx->pts_correction_num_faulty_pts : 0);

    if (opt_show_status == 1 && AV_LOG_INFO > av_log_get_level())
        fprintf(stderr, "%s", buf.str);
    else
        av_log(NULL, AV_LOG_INFO, "%s", buf.str);

    fflush(stderr);
    av_bprint_finalize(&buf, NULL);

    last_time = cur_time;    
}

int VideoDecoder::queue_picture( AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;

#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif

    if (!(vp = frame_q.frame_queue_peek_writable()))
        return -1;

    vp->sample_aspect_ratio = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;

    this->_vs->render.set_default_window_size(vp->width, vp->height, vp->sample_aspect_ratio);

    av_frame_move_ref(vp->frame, src_frame);
    frame_q.frame_queue_push();
    return 0;
}

int VideoDecoder::get_video_frame( AVFrame *frame)
{
    int got_picture;

    if ((got_picture = decoder_decode_frame( frame, NULL)) < 0)
        return -1;

    if (!got_picture)
    {
        return 0;
    }
    
    double dpts = NAN;

    if (frame->pts != AV_NOPTS_VALUE)
        dpts = av_q2d(stream->time_base) * frame->pts;

    frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(this->_vs->format_context, stream, frame);

    if (this->_vs->get_master_sync_type() != AV_SYNC_VIDEO_MASTER) {
        // 看看是否有必要抛弃帧
        if (frame->pts != AV_NOPTS_VALUE) {
            double diff = dpts - this->_vs->get_master_clock();
            if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                diff < 0 &&
                pkt_serial == stream_clock.serial &&
                packet_q.nb_packets) 
            {
                // 抛弃帧
                this->_vs->frame_drops_early++;
                av_frame_unref(frame);
                got_picture = 0;
            }
        }
    }
    
    return got_picture;
}


unsigned int AudioDecoder::run()
{
    AVFrame *frame = av_frame_alloc();
    Frame *af;

    int got_frame = 0;
    AVRational time_base;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do {
        if ((got_frame = decoder_decode_frame( frame, NULL)) < 0)
            goto the_end;

        if (!got_frame)
        {
            continue;
        }
        
        time_base.num = 1;
        time_base.den = frame->sample_rate ;

        if (!(af = frame_q.frame_queue_peek_writable()))
            goto the_end;

        af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(time_base);
        af->pos = frame->pkt_pos;
        af->serial = this->pkt_serial;

        AVRational szr_dur = { frame->nb_samples, frame->sample_rate };
        af->duration = av_q2d(szr_dur);

        av_frame_move_ref(af->frame, frame);
        frame_q.frame_queue_push();

        
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

 the_end:
    av_frame_free(&frame);
    return ret;
}

int Decoder::decoder_start()
{
    packet_q.packet_queue_start();
    create_thread(); // todo: 统一 error report 机制
    return 0;
}

int Decoder::stream_has_enough_packets() {
    return stream_id < 0 ||
        packet_q.abort_request ||
        (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        packet_q.nb_packets > MIN_FRAMES && (!packet_q.total_duration    // 如果 total_duration 是0，说明pkt.duration无效，那就是只看包数量
                                            || av_q2d(stream->time_base) * packet_q.total_duration > 1.0
            );
}

unsigned int VideoDecoder::run()
{
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = this->stream->time_base;
    AVRational frame_rate = av_guess_frame_rate(this->_vs->format_context, this->stream, NULL);

    if (!frame)
        return AVERROR(ENOMEM);

    for (;;) {
        ret = get_video_frame( frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;

            AVRational szr_dur = { frame_rate.den, frame_rate.num };
            duration = (frame_rate.num && frame_rate.den ? av_q2d(szr_dur) : 0);
            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            ret = queue_picture( frame, pts, duration, frame->pkt_pos, pkt_serial);
            av_frame_unref(frame);

        if (ret < 0)
            goto the_end;
    }
 the_end:
    av_frame_free(&frame);
    return 0;
}

unsigned int SubtitleDecoder::run()
{
    Frame *sp;
    int got_subtitle;
    double pts;

    for (;;) {
        if (!(sp = frame_q.frame_queue_peek_writable()))
            return 0;

        if ((got_subtitle = decoder_decode_frame( NULL, &sp->sub)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0) {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = pkt_serial;
            sp->width = avctx->width;
            sp->height = avctx->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            frame_q.frame_queue_push();
        } else if (got_subtitle) {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}


/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
int VideoState::synchronize_audio( int nb_samples)  // todo: move to AudioDecoder
{
    int wanted_nb_samples = nb_samples;

    if (AV_SYNC_AUDIO_MASTER == get_master_sync_type() ) 
        return wanted_nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    double diff, avg_diff;
    int min_nb_samples, max_nb_samples;

    diff = auddec.stream_clock.get_clock() - get_master_clock();

    if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD) {
        auddec.audio_diff_cum = diff + auddec.audio_diff_avg_coef * auddec.audio_diff_cum;
        if (auddec.audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
            /* not enough measures to have a correct estimate */
            auddec.audio_diff_avg_count++;
        } else {
            /* estimate the A-V difference */
            avg_diff = auddec.audio_diff_cum * (1.0 - auddec.audio_diff_avg_coef);

            if (fabs(avg_diff) >= auddec.audio_diff_threshold) {
                wanted_nb_samples = nb_samples + (int)(diff * auddec.audio_src.freq);
                min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
            }
            av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                    diff, avg_diff, wanted_nb_samples - nb_samples,
                    auddec.audio_clock, auddec.audio_diff_threshold);
        }
    } else {
        /* too big difference : may be initial PTS errors, so
            reset A-V filter */
        auddec.audio_diff_avg_count = 0;
        auddec.audio_diff_cum       = 0;
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
int AudioDecoder::audio_decode_frame()
{
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    if (this->_vs->paused)
        return -1;

    do {
#if defined(_WIN32)
        while (this->frame_q.frame_queue_nb_remaining() == 0) {
            if ((av_gettime_relative() - this->audio_callback_time) > 1000000LL * this->audio_hw_buf_size / this->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        if (!(af = this->frame_q.frame_queue_peek_readable()))
            return -1;
        this->frame_q.frame_queue_next();
    } while (af->serial != this->packet_q.serial);

    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
                                           af->frame->nb_samples,
                                           (AVSampleFormat)af->frame->format, 1);

    dec_channel_layout =
        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
    wanted_nb_samples = this->_vs->synchronize_audio(af->frame->nb_samples);

    if (af->frame->format        != this->audio_src.fmt            ||
        dec_channel_layout       != this->audio_src.channel_layout ||
        af->frame->sample_rate   != this->audio_src.freq           ||
        (wanted_nb_samples       != af->frame->nb_samples && !this->swr_ctx))
    {
        swr_free(&this->swr_ctx);
        this->swr_ctx = swr_alloc_set_opts(NULL,
            this->audio_tgt.channel_layout, this->audio_tgt.fmt, this->audio_tgt.freq,
            dec_channel_layout,   (AVSampleFormat) af->frame->format, af->frame->sample_rate,
            0, NULL);
        if (!this->swr_ctx || swr_init(this->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
                this->audio_tgt.freq, av_get_sample_fmt_name(this->audio_tgt.fmt), this->audio_tgt.channels);
            swr_free(&this->swr_ctx);
            return -1;
        }
        this->audio_src.channel_layout = dec_channel_layout;
        this->audio_src.channels       = af->frame->channels;
        this->audio_src.freq = af->frame->sample_rate;
        this->audio_src.fmt = (AVSampleFormat) af->frame->format;
    }

    if (this->swr_ctx) {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        uint8_t **out = &this->audio_buf1;
        int out_count = (int64_t)wanted_nb_samples * this->audio_tgt.freq / af->frame->sample_rate + 256;
        int out_size  = av_samples_get_buffer_size(NULL, this->audio_tgt.channels, out_count, this->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples) {
            if (swr_set_compensation(this->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * this->audio_tgt.freq / af->frame->sample_rate,
                wanted_nb_samples * this->audio_tgt.freq / af->frame->sample_rate) < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&this->audio_buf1, &this->audio_buf1_size, out_size);
        if (!this->audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(this->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(this->swr_ctx) < 0)
                swr_free(&this->swr_ctx);
        }
        this->audio_buf = this->audio_buf1;
        resampled_data_size = len2 * this->audio_tgt.channels * av_get_bytes_per_sample(this->audio_tgt.fmt);
    } else {
        this->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = this->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        this->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        this->audio_clock = NAN;
    this->audio_clock_serial = af->serial;
#ifdef DEBUG_SYNC
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->auddec.audio_clock - last_clock,
               is->auddec.audio_clock, auddec.audio_clock0);
        last_clock = is->auddec.audio_clock;
    }
#endif
    return resampled_data_size;
}

/* prepare a new audio buffer */
void AudioDecoder::sdl_audio_callback(void* opaque, Uint8* stream, int len)
{
    AudioDecoder* audio_decoder = (AudioDecoder*)opaque;
    audio_decoder->handle_sdl_audio_cb(stream, len);
}

void AudioDecoder::handle_sdl_audio_cb(Uint8* stream, int len)
{
    int audio_size, len1;

    this->audio_callback_time = av_gettime_relative();

    while (len > 0) {
        if (this->audio_buf_index >= this->audio_buf_size) {
           audio_size = this->audio_decode_frame();
           if (audio_size < 0) {
                /* if error, just output silence */
               this->audio_buf = NULL;
               this->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / this->audio_tgt.frame_size * this->audio_tgt.frame_size;
           } else {
               this->audio_buf_size = audio_size;
           }
           this->audio_buf_index = 0;
        }
        len1 = this->audio_buf_size - this->audio_buf_index;
        if (len1 > len)
            len1 = len;
        if (!this->muted && this->audio_buf && this->audio_volume == SDL_MIX_MAXVOLUME)
            memcpy(stream, (uint8_t *)this->audio_buf + this->audio_buf_index, len1);
        else {
            memset(stream, 0, len1);
            if (!this->muted && this->audio_buf)
                SDL_MixAudioFormat(stream, (uint8_t *)this->audio_buf + this->audio_buf_index, AUDIO_S16SYS, len1, this->audio_volume);
        }
        len -= len1;
        stream += len1;
        this->audio_buf_index += len1;
    }
    this->audio_write_buf_size = this->audio_buf_size - this->audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(this->audio_clock)) {
        this->stream_clock.set_clock_at(this->audio_clock - (double)(2 * this->audio_hw_buf_size + this->audio_write_buf_size) / this->audio_tgt.bytes_per_sec
            , this->audio_clock_serial
            , this->audio_callback_time / 1000000.0);
        this->_vs->extclk.sync_clock_to_slave( &this->stream_clock);
    }
}

int AudioDecoder::audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
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
    while (!(this->_vs->render.audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
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

/* open a given stream, create decoder for it. Return 0 if OK */
int VideoState::stream_component_open(int stream_index)
{
    if (stream_index < 0 || stream_index >= (int)format_context->nb_streams)
        return -1;

    AVCodecContext* avctx = Decoder::create_codec(this->format_context, stream_index);
    
    this->eof = 0; // 如果是‘切换流 stream_cycle_channel’来的，这里再清一下eof
    this->format_context->streams[stream_index]->discard = AVDISCARD_DEFAULT;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:                  
        this->auddec.decoder_init(avctx, stream_index , format_context->streams[stream_index], &this->continue_read_thread);
        this->last_audio_stream = stream_index;
        break;
    case AVMEDIA_TYPE_VIDEO:  
        this->viddec.decoder_init( avctx, stream_index, format_context->streams[stream_index], &this->continue_read_thread); 
        this->last_video_stream = stream_index;
        break;
    case AVMEDIA_TYPE_SUBTITLE: 
        this->subdec.decoder_init( avctx, stream_index, format_context->streams[stream_index], &this->continue_read_thread);
        this->last_subtitle_stream = stream_index;
        break;
    default:
        break;
    }
    
    return 0;
}

int VideoState::decode_interrupt_cb(void *ctx)
{
    VideoState *is = (VideoState*)ctx;
    return is->abort_request;
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

void AutoReleasePtr<AVFormatContext>::release()
{
    if (!me)
        return;

    avformat_close_input(&me);
    me = NULL;
}

int VideoState::open_stream_file()
{
    int err, ret = 1;

    AVFormatContext* format_context = NULL;
    format_context = avformat_alloc_context();
    if (!format_context) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        return  AVERROR(ENOMEM);        
    }
    AutoReleasePtr<AVFormatContext> guard(format_context);

    format_context->interrupt_callback.callback = decode_interrupt_cb;
    format_context->interrupt_callback.opaque = this;

    err = avformat_open_input(&format_context, this->filename, this->iformat, NULL);
    if (err < 0) {
        print_error(this->filename, err);
        return -1;
    }

    this->format_context = format_context;
    guard.dismiss();

    av_format_inject_global_side_data(format_context);

    unsigned int orig_nb_streams = format_context->nb_streams;
    err = avformat_find_stream_info(format_context, NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_WARNING,
            "%s: could not find codec parameters\n", this->filename);
        return  -1;
    }

    if (format_context->pb)
        format_context->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    this->max_frame_duration = (format_context->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    /* if seeking requested, we execute it */
    if (opt_start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = opt_start_time;
        /* add the stream start time */
        if (format_context->start_time != AV_NOPTS_VALUE)
            timestamp += format_context->start_time;
        ret = avformat_seek_file(format_context, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                this->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    this->realtime = is_realtime(format_context);

    if (opt_infinite_buffer < 0 && this->realtime)
        opt_infinite_buffer = 1;

    if (opt_show_status)
        av_dump_format(format_context, 0, this->filename, 0);

    return 0;
}

int VideoState::open_streams()
{
    int st_index[AVMEDIA_TYPE_NB];
    memset(st_index, -1, sizeof(st_index));
    
    // 2.1 av_find_best_stream
    unsigned int i;
    for (i = 0; i < this->format_context->nb_streams; i++) {
        st_index[i] = -1;
    }

    st_index[AVMEDIA_TYPE_VIDEO] =
        av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO,
            st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);

    
    st_index[AVMEDIA_TYPE_AUDIO] =
    av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO,
        st_index[AVMEDIA_TYPE_AUDIO],
        st_index[AVMEDIA_TYPE_VIDEO],
        NULL, 0);

    if (!opt_subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =
        av_find_best_stream(format_context, AVMEDIA_TYPE_SUBTITLE,
            st_index[AVMEDIA_TYPE_SUBTITLE],
            (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                st_index[AVMEDIA_TYPE_AUDIO] :
                st_index[AVMEDIA_TYPE_VIDEO]),
            NULL, 0);

    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        AVStream* st = format_context->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecParameters* codec_para = st->codecpar;
        AVRational sar = av_guess_sample_aspect_ratio(format_context, st, NULL);
        if (codec_para->width)
            this->render.set_default_window_size(codec_para->width, codec_para->height, sar);
    }

    // 2.2 open the streams 
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
    }

    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        stream_component_open(st_index[AVMEDIA_TYPE_VIDEO]);
    }

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        stream_component_open(st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    if (this->viddec.stream_id < 0 && this->auddec.stream < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
            this->filename);
        return -1;
    }

    return 0;
}

int VideoState::read_loop_check_pause() // return: nonzero -- shoud 'continue', 0 -- go on current iteration
{
    if (this->paused != this->last_paused) {
        this->last_paused = this->paused;
        if (this->paused)
            this->read_pause_return = av_read_pause(format_context);
        else
            av_read_play(format_context);
    }

    if (this->paused &&
        (!strcmp(this->format_context->iformat->name, "rtsp") ||
            (format_context->pb && !strncmp(opt_input_filename, "mmsh:", 5)))) {
        /* wait 10 ms to avoid trying to get another packet */
        /* XXX: horrible */
        SDL_Delay(10);
        return 1;
    }
    return 0;
}

int  VideoState::read_loop_check_seek()   // return: > 0 -- shoud 'continue', 0 -- go on current iteration, < 0 -- error exit loop
{
    if (!this->seek_req)
    {
        return 0;
    }

    int64_t seek_target = this->seek_pos;
    int64_t seek_min = this->seek_rel > 0 ? seek_target - this->seek_rel + 2 : INT64_MIN;
    int64_t seek_max = this->seek_rel < 0 ? seek_target - this->seek_rel - 2 : INT64_MAX;
    // FIXME the +-2 is due to rounding being not done in the correct direction in generation
    //      of the seek_pos/seek_rel variables

    int ret = avformat_seek_file(this->format_context, -1, seek_min, seek_target, seek_max, this->seek_flags);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
            "%s: error while seeking\n", this->format_context->url);
    }
    else {
        // seek成功，清现有的缓存
        if (this->auddec.stream_id >= 0) {
            this->auddec.packet_q.packet_queue_flush();
            this->auddec.packet_q.packet_queue_put(&PacketQueue::flush_pkt); // packet queue 的 serial ++
        }
        if (this->subdec.stream_id >= 0) {
            this->subdec.packet_q.packet_queue_flush();
            this->subdec.packet_q.packet_queue_put(&PacketQueue::flush_pkt);
        }
        if (this->viddec.stream_id >= 0) {
            this->viddec.packet_q.packet_queue_flush();
            this->viddec.packet_q.packet_queue_put(&PacketQueue::flush_pkt);
        }
        if (this->seek_flags & AVSEEK_FLAG_BYTE) {
            this->extclk.set_clock(NAN, 0);
        }
        else {
            this->extclk.set_clock(seek_target / (double)AV_TIME_BASE, 0);
        }
    }

    this->seek_req = 0;
    this->eof = 0;
    if (this->paused)
        this->step_to_next_frame();

    return 0;
}

#define LOOP_CHECK(func) \
{\
    int ret = func;\
    if (ret < 0)\
        goto fail;\
    else if (ret > 0)\
        continue;\
}
/* this thread gets the stream from the disk or the network */
unsigned VideoState::run()
{
    unsigned ret = 1;
    AVPacket pkt1, *pkt = &pkt1;
    this->eof = 0;

    // 3. real loop
    for (;;) {
        // 3.1  break 条件
        if (this->abort_request)
            break;
       
        // 3.2 处理pause
        LOOP_CHECK( read_loop_check_pause());        

        // 3.3 处理seek
        LOOP_CHECK(read_loop_check_seek());

        // 3.4 准备读入packet

        /* if the queue are full, no need to read more */
        if  (opt_infinite_buffer <1 &&
              (this->auddec.packet_q.size + this->viddec.packet_q.size + this->subdec.packet_q.size > MAX_QUEUE_SIZE
              || (this->auddec.stream_has_enough_packets() &&
                  this->viddec.stream_has_enough_packets() &&
                  this->subdec.stream_has_enough_packets()))
            )
        {
            /* wait 10 ms */
            this->continue_read_thread.timed_wait(10);
            continue;
        }

        ret = av_read_frame(format_context, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(format_context->pb)) && !this->eof) {  //todo: 走不进来这里，视频放完之后show_status显示vq是乱数
                if (this->viddec.stream_id >= 0)
                    this->viddec.packet_q.packet_queue_put_nullpacket( this->viddec.stream_id);
                if (this->auddec.stream_id >= 0)
                    this->auddec.packet_q.packet_queue_put_nullpacket( this->auddec.stream_id);
                if (this->subdec.stream_id >= 0)
                    this->subdec.packet_q.packet_queue_put_nullpacket( this->subdec.stream_id);
                this->eof = 1;
            }
            if (format_context->pb && format_context->pb->error) {
                if (opt_autoexit)
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
        if (!is_pkt_in_play_range(pkt)) { 
            av_packet_unref(pkt); // todo: 不到 start_point，还是超过duration，应该有不同处理
            continue;
        }

        if (pkt->stream_index == this->auddec.stream_id ) {
            this->auddec.packet_q.packet_queue_put( pkt);
        }
        else if (pkt->stream_index == this->viddec.stream_id && !(this->viddec.stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            this->viddec.packet_q.packet_queue_put( pkt);
        }
        else if (pkt->stream_index == this->subdec.stream_id ) {
            this->subdec.packet_q.packet_queue_put( pkt);
        }
        else {
            av_packet_unref(pkt);
        }
    }

    ret = 0;
 fail:

    if (ret != 0) {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = this;
        SDL_PushEvent(&event);
    }

    return 0;
}

int VideoState::is_pkt_in_play_range( AVPacket* pkt)
{
    if (opt_duration == AV_NOPTS_VALUE)
        return 1;
    //todo: 如果是‘实况转播’，来包就放，也应该无条件返回1。

    int64_t stream_start_time = format_context->streams[pkt->stream_index]->start_time;
    if (AV_NOPTS_VALUE == stream_start_time)
        stream_start_time = 0;

    int64_t pkt_ts = ( pkt->pts == AV_NOPTS_VALUE) ? pkt->dts : pkt->pts;
    double ts_relative_to_stream = stream_ts_to_second( pkt_ts - stream_start_time, pkt->stream_index);
    //printf("stream %d, ts_relative_to_stream = %f\n", pkt->stream_index, ts_relative_to_stream);
    double start_point = (opt_start_time != AV_NOPTS_VALUE ? opt_start_time : 0) / 1000000; // opt_start_time is in unit of microsecond
    double duration = opt_duration / 1000000;

    return (ts_relative_to_stream - start_point) <= duration;
}

double VideoState::stream_ts_to_second(int64_t ts,  int stream_index)
{
    return ts * av_q2d(format_context->streams[stream_index]->time_base);
}

void AutoReleasePtr<VideoState>::release()
{
    if (!me)
        return;
    me->close_input_stream();
    //外界会delete
}

int VideoState::open_input_stream(const char *filename, AVInputFormat *iformat)
{
    AutoReleasePtr<VideoState> close_if_failed(this);

    this->last_video_stream = this->viddec.stream_id = -1;
    this->last_audio_stream = this->auddec.stream_id = -1;
    this->last_subtitle_stream = this->subdec.stream_id = -1;
    this->filename = av_strdup(filename);
    if (!this->filename)
        return 1;

    this->iformat = iformat;

    //todo: move to 'simple av decoder'        
    this->extclk.init_clock( &this->extclk.serial);  
    this->av_sync_type = opt_av_sync_type;


    // 打开文件，酌情seek   
    if (open_stream_file())
    {
        return 5;
    }

    // 逐个打开流
    if (open_streams())
    {
        return 6;
    }

    this->create_thread(); // 启动读流线程
    
    close_if_failed.dismiss();
    return 0;
}

void VideoState::stream_cycle_channel( int codec_type)
{
    int start_index, stream_index;
    int old_index;
    AVStream *st;
    AVProgram *p = NULL;
    int nb_streams = format_context->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = this->last_video_stream;
        old_index = this->viddec.stream_id;
    } else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = this->last_audio_stream;
        old_index = this->auddec.stream_id;
    } else {
        start_index = this->last_subtitle_stream;
        old_index = this->subdec.stream_id;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && this->viddec.stream_id != -1) {
        p = av_find_program_from_stream(this->format_context, NULL, this->viddec.stream_id);
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
                this->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = this->format_context->streams[p ? p->stream_index[stream_index] : stream_index];
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

    this->stream_component_close( old_index);
    this->stream_component_open(stream_index);
}


void VideoState::seek_chapter( int incr)
{
    int64_t pos = (int64_t) ( this->get_master_clock() * AV_TIME_BASE );
    int i;

    if (!this->format_context->nb_chapters)
        return;

    AVRational time_base_q = { 1, AV_TIME_BASE };

    /* find the current chapter */
    for (i = 0; i < (int) this->format_context->nb_chapters; i++) {
        AVChapter *ch = this->format_context->chapters[i];
                
        if (av_compare_ts(pos, time_base_q, ch->start, ch->time_base) < 0) {
            i--;
            break;
        }
    }

    i += incr;
    i = FFMAX(i, 0);
    if (i >= (int) this->format_context->nb_chapters)
        return;

    av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
    this->stream_seek( av_rescale_q(this->format_context->chapters[i]->start, this->format_context->chapters[i]->time_base,time_base_q)
        , 0, 0);
}
