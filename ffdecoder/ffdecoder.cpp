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



//     decoder section {{{
void Decoder::onetime_global_init()
{
    av_init_packet(&PacketQueue::flush_pkt);
    PacketQueue::flush_pkt.data = (uint8_t*)&PacketQueue::flush_pkt;
}

AVCodecContext* Decoder::create_codec_directly( const AVCodecParameters * codec_para, const StreamParam* extra_para )
{ 
    AVCodecContext* codec_context = avcodec_alloc_context3(NULL);
    if (!codec_context)
        return NULL;

    AutoReleasePtr<AVCodecContext> guard(codec_context);
 
    int ret = avcodec_parameters_to_context(codec_context,  codec_para);
    if (ret < 0)
        return NULL;   
    
    codec_context->pkt_timebase = extra_para->time_base ;
 
    AVCodec* codec = avcodec_find_decoder(codec_context->codec_id);
    if (!codec) {
        av_log(NULL, AV_LOG_WARNING,
            "No decoder could be found for codec %s\n", avcodec_get_name(codec_context->codec_id));
        return NULL;
    }
    codec_context->codec_id = codec->id;  

    if ((ret = avcodec_open2(codec_context, codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_WARNING, "Failed to open  codec %d(%s), LE = %d\n", codec->id, avcodec_get_name(codec->id), ret);
        return NULL;
    }

    guard.dismiss();
    return codec_context;
}

int Decoder::decoder_init( AVCodecContext *avctx, const StreamParam* extra_para, SimpleConditionVar* empty_queue_cond)
{
    this->avctx = avctx;

    stream_param = *extra_para;
    
    this->empty_pkt_queue_cond = empty_queue_cond;
    this->start_pts = AV_NOPTS_VALUE;
    this->pkt_serial = -1; 
    
    av_init_packet(&pending_pkt);
    is_packet_pending = 0;
    
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
                ret = avcodec_receive_frame(this->avctx, frame);
                if (ret >= 0) {
                    // 成功解得frame
                    on_got_new_frame(frame);
                    return 1;
                }                
                else if (ret == AVERROR_EOF) {
                    this->finished = this->pkt_serial;
                    avcodec_flush_buffers(this->avctx);
                    return 0;
                }                
                    
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

        // 现在 ‘pkt’ 的serial符合要求

        if (PacketQueue::is_flush_pkt(pkt)) {
            avcodec_flush_buffers(this->avctx);
            this->finished = 0;
            this->next_pts          = this->start_pts;
            this->next_pts_timebase = this->start_pts_timebase;

            continue;
        }
        
        
        // 喂packet进codec

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
        
        av_packet_unref(&pkt);
    }
}

Render* Decoder::get_render()
{
    return & this->_av_decoder->render;
}


void Decoder::decoder_destroy() {    
    decoder_abort();

    av_packet_unref(&this->pending_pkt);
    avcodec_free_context(&this->avctx);

    this->packet_q.packet_queue_destroy();
    this->frame_q.frame_queue_destory();

    inited = 0;
}

void Decoder::decoder_abort()
{
    this->packet_q.packet_queue_abort();
    this->frame_q.frame_queue_signal();

    this->BaseThread::wait_thread_quit();  

    this->packet_q.packet_queue_flush();
}

int VideoDecoder::decoder_init(AVCodecContext* avctx, const StreamParam* extra_para,   SimpleConditionVar* empty_queue_cond)
{
    if (MyBase::decoder_init(avctx, extra_para, empty_queue_cond))
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
    inited = 1;
    return 0;
}

void VideoDecoder::on_got_new_frame(AVFrame* frame)
{
    if (_av_decoder->decoder_reorder_pts == -1) { // let decoder reorder pts 0=off 1=on -1=auto
        frame->pts = frame->best_effort_timestamp;
    }
    else if (!_av_decoder->decoder_reorder_pts) {
        frame->pts = frame->pkt_dts;
    }
}

void VideoDecoder::decoder_destroy() {
    MyBase::decoder_destroy();

    if (this->img_convert_ctx)
    {
        sws_freeContext(this->img_convert_ctx);
        this->img_convert_ctx = NULL;
    }
}

int AudioDecoder::decoder_init(AVCodecContext* avctx, const StreamParam* extra_para,  SimpleConditionVar* empty_queue_cond)
{
    if (MyBase::decoder_init(avctx,  extra_para, empty_queue_cond))
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

    //if ((this->_av_decoder->vs->format_context->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK))
    //    && !this->_av_decoder->vs->format_context->iformat->read_seek)
    //{   // 如果iformat不支持seek，那么起始位置就以AVStream里说的为准，否则还有机会根据命令行-ss来seek
    //    this->start_pts = stream_param.start_time ;
    //    this->start_pts_timebase = stream_param.time_base ;         
    //}

    SDL_PauseAudioDevice(this->_av_decoder->render.audio_dev, 0);
    
    if (decoder_start())
    {
        return 2;
    }
    inited = 1;

    return 0;

}

void AudioDecoder::decoder_destroy() {
    MyBase::decoder_destroy();
    this->_av_decoder->render.close_audio();
    
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

void AudioDecoder::on_got_new_frame(AVFrame* frame)
{
    AVRational tb = { 1, frame->sample_rate };
    if (frame->pts != AV_NOPTS_VALUE)
        frame->pts = av_rescale_q(frame->pts, this->avctx->pkt_timebase, tb);
    else if (this->next_pts != AV_NOPTS_VALUE)
        frame->pts = av_rescale_q(this->next_pts, this->next_pts_timebase, tb);
    if (frame->pts != AV_NOPTS_VALUE) {
        this->next_pts = frame->pts + frame->nb_samples;
        this->start_pts_timebase = tb;
    }
}
//     }}} decoder section 

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

    if (this->create_window(window_title.GetString()
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

void Render::calculate_display_rect(SDL_Rect* rect,
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
    rect->y = (int)(scr_ytop + y);
    rect->w = FFMAX((int)width, 1);
    rect->h = FFMAX((int)height, 1);
}

void Render::get_sdl_pix_fmt_and_blendmode(int format, Uint32* sdl_pix_fmt, SDL_BlendMode* sdl_blendmode)
{
    unsigned int i;
    *sdl_blendmode = SDL_BLENDMODE_NONE;
    *sdl_pix_fmt = SDL_PIXELFORMAT_UNKNOWN;
    if (format == AV_PIX_FMT_RGB32 ||
        format == AV_PIX_FMT_RGB32_1 ||
        format == AV_PIX_FMT_BGR32 ||
        format == AV_PIX_FMT_BGR32_1)
        *sdl_blendmode = SDL_BLENDMODE_BLEND;
    for (i = 0; i < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; i++) {
        if (format == sdl_texture_format_map[i].format) {
            *sdl_pix_fmt = sdl_texture_format_map[i].texture_fmt;
            return;
        }
    }
}

int Render::upload_texture(SDL_Texture** tex, AVFrame* frame, struct SwsContext** img_convert_ctx) {
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
            frame->width, frame->height, (enum AVPixelFormat)frame->format, frame->width, frame->height,
            AV_PIX_FMT_BGRA, SWS_FLAG_4_PIXELFORMAT_UNKNOWN, NULL, NULL, NULL);
        if (*img_convert_ctx != NULL) {
            uint8_t* pixels[4];
            int pitch[4];
            if (!SDL_LockTexture(*tex, NULL, (void**)pixels, pitch)) {
                sws_scale(*img_convert_ctx, (const uint8_t* const*)frame->data, frame->linesize,
                    0, frame->height, pixels, pitch);
                SDL_UnlockTexture(*tex);
            }
        }
        else {
            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
            ret = -1;
        }
        break;
    case SDL_PIXELFORMAT_IYUV:
        if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0) {
            ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0], frame->linesize[0],
                frame->data[1], frame->linesize[1],
                frame->data[2], frame->linesize[2]);
        }
        else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0) {
            ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0],
                frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
                frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]);
        }
        else {
            av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
            return -1;
        }
        break;
    default:
        if (frame->linesize[0] < 0) {
            ret = SDL_UpdateTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
        }
        else {
            ret = SDL_UpdateTexture(*tex, NULL, frame->data[0], frame->linesize[0]);
        }
        break;
    }
    return ret;
}

void Render::set_sdl_yuv_conversion_mode(AVFrame* frame)
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



void VideoDecoder::video_image_display()
{
    Frame *vp;

    SDL_Rect rect;

    vp = this->frame_q.frame_queue_peek_last();

    Render::calculate_display_rect(&rect, this->xleft, this->ytop, this->width, this->height, vp->width, vp->height, vp->sample_aspect_ratio);

    if (!vp->uploaded) {
        if (this->_av_decoder->render.upload_texture(&this->get_render()->vid_texture, vp->frame, &this->img_convert_ctx) < 0)
            return;
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
    }

    this->get_render()->show_texture(vp, rect,  0);
}


int  SimpleAVDecoder::get_opened_streams_mask()   // 返回 bit0 代表V， bit1 代表A
{
    int r = 0;
    if (this->viddec.is_inited())
    {
        r |= 1;
    }
    if (this->auddec.is_inited())
    {
        r |= 2;
    }
    return r;
}

void SimpleAVDecoder::close_all_stream()
{
    if (this->auddec.is_inited())
    {
        this->auddec.decoder_destroy();
    }

    if (this->viddec.is_inited())
    {
        this->viddec.decoder_destroy();
    }
}

void VideoState::close_input_stream()
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    this->abort_request = 1;
    this->wait_thread_quit();

    /* close each stream */
    this->av_decoder.close_all_stream();
    
    // this->format_context->streams[stream_index]->discard = AVDISCARD_ALL;  // 相比原来ffplay，这个步骤没做

    avformat_close_input(&this->format_context);
}


int VideoDecoder::video_open()
{
    Render* render = get_render();

    render->show_window(render->window_title.GetString()
        , render->screen_width, render->screen_height, render->screen_left, render->screen_top
        , 0);
    
    this->width  = render->screen_width;
    this->height = render->screen_height;

    return 0;
}

/* display the current picture, if any */
void VideoDecoder::video_display()
{
    if (!this->width)
        this->video_open();

    this->get_render()->clear_render();
    
    if (this->is_inited())
        this->video_image_display();

    this->get_render()->draw_render();
    
}


int SimpleAVDecoder::get_master_sync_type() {
    if (this->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (this->viddec.is_inited())
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    } else if (this->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (this->auddec.is_inited())
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    } else {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

/* get the current master clock value */
double SimpleAVDecoder::get_master_clock()
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

void SimpleAVDecoder::check_external_clock_speed() {
   if ( (this->viddec.is_inited() && this->viddec.packet_q.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) ||
        (this->auddec.is_inited() && this->auddec.packet_q.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES) )
   {
       // 降速一档
       this->extclk.set_clock_speed( FFMAX(EXTERNAL_CLOCK_SPEED_MIN, this->extclk.get_clock_speed() - EXTERNAL_CLOCK_SPEED_STEP));  
   }
   else if (( !this->viddec.is_inited()  || this->viddec.packet_q.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
              ( !this->auddec.is_inited() || this->auddec.packet_q.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES))
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
int SimpleAVDecoder::internal_toggle_pause()
{
    if (this->paused) { // let's resume
        this->viddec.frame_timer += av_gettime_relative() / 1000000.0 - this->viddec.stream_clock.get_last_set_point();        
        this->viddec.stream_clock.set_clock(this->viddec.stream_clock.get_clock(), this->viddec.stream_clock.serial);
    }

    this->extclk.set_clock(this->extclk.get_clock(), this->extclk.serial);

    this->paused = this->auddec.stream_clock.paused = this->viddec.stream_clock.paused = this->extclk.paused = !this->paused;
    return this->paused;
}

void VideoState::toggle_pause()
{
    paused = this->av_decoder.internal_toggle_pause();
    this->av_decoder.step = 0;
}

void SimpleAVDecoder::toggle_mute( )
{
    this->auddec.muted = !this->auddec.muted;
}

void SimpleAVDecoder::update_volume( int sign, double step)
{
    double volume_level = this->auddec.audio_volume ? (20 * log(this->auddec.audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    this->auddec.audio_volume = av_clip(this->auddec.audio_volume == new_volume ? (this->auddec.audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

void VideoState::step_to_next_frame()
{
    /* if the stream is paused unpause it, then step */
    if (this->paused)
        this->av_decoder.internal_toggle_pause();
    this->av_decoder.step = 1;
}

double SimpleAVDecoder::compute_target_delay(double frame_duration)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (AV_SYNC_VIDEO_MASTER != this->get_master_sync_type() ) {
        /* if video is slave, we try to correct big delays by duplicating or deleting a frame */
        diff = this->viddec.stream_clock.get_clock() - this->get_master_clock();

        /* skip or repeat frame. We take into account the delay to compute the threshold. 
        I still don't know if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, frame_duration));  // sync_threshold is in range [AV_SYNC_THRESHOLD_MIN, AV_SYNC_THRESHOLD_MAX]
        if (!isnan(diff) && fabs(diff) < this->max_frame_duration) { // diff 如果在 [-sync_threshold, +sync_threshold] 范围内，就不调整了
            if (diff <= -sync_threshold)
                frame_duration = FFMAX(0, frame_duration + diff);   // V钟慢了(-diff) ，因此从‘应显示时间’里扣掉 (-diff))，即 +diff
            else if (diff >= sync_threshold && frame_duration > AV_SYNC_FRAMEDUP_THRESHOLD)
                frame_duration = frame_duration + diff; // V钟快得很多，超过了AV_SYNC_FRAMEDUP_THRESHOLD，则一次性补足，即 +diff
            else if (diff >= sync_threshold) 
                frame_duration = 2 * frame_duration; // V钟快了(diff)，但未超过AV_SYNC_FRAMEDUP_THRESHOLD，则原时长翻倍
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",  frame_duration, -diff);

    return frame_duration;
}

double SimpleAVDecoder::vp_duration(Frame *vp, Frame *nextvp) {
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

void SimpleAVDecoder::update_video_clock(double pts, int64_t pos, int serial) {
    /* update current video pts */
    viddec.stream_clock.set_clock( pts, serial);
    extclk.sync_clock_to_slave( &viddec.stream_clock);
}

/* called to display each frame */
void SimpleAVDecoder::video_refresh(double *remaining_time)
{
    if (!this->paused && this->get_master_sync_type() == AV_SYNC_EXTERNAL_CLOCK && this->realtime)
        this->check_external_clock_speed();

    if (this->viddec.is_inited()) {
        prepare_picture_for_display(remaining_time);

        /* display picture */
        if (this->force_refresh && this->viddec.frame_q.is_last_frame_shown())
            this->viddec.video_display();
    }
    this->force_refresh = 0;
    if (this->show_status) {
        this->print_stream_status();
    }
}

void SimpleAVDecoder::prepare_picture_for_display(double* remaining_time)
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

    this->viddec.frame_q.frame_queue_next();
    this->force_refresh = 1;

    if (this->step && !this->paused)
        internal_toggle_pause();
}

void SimpleAVDecoder::print_stream_status()
{
    AVBPrint buf;
    static int64_t last_time = 0;
    int64_t cur_time;
    int aqsize, vqsize;
    double av_diff;

    cur_time = av_gettime_relative();
    if (last_time && (cur_time - last_time) < 30000) 
    {
        return;
    }
    
    
    aqsize = 0;
    vqsize = 0;
    
    if (this->auddec.is_inited())
        aqsize = this->auddec.packet_q.size;
    if (this->viddec.is_inited())
        vqsize = this->viddec.packet_q.size;

    av_diff = 0;
    if (this->auddec.is_inited() && this->viddec.is_inited())
        av_diff = this->auddec.stream_clock.get_clock() - this->viddec.stream_clock.get_clock();
    else if (this->viddec.is_inited())
        av_diff = this->get_master_clock() - this->viddec.stream_clock.get_clock();
    else if (this->auddec.is_inited())
        av_diff = this->get_master_clock() - this->auddec.stream_clock.get_clock();

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf,
        "clock:%7.2f %s:%7.3f framedrop=%4d aq=%5dKB vq=%5dKB f=%" PRId64 "/%" PRId64 "   \r",
        this->get_master_clock(),
        (this->auddec.is_inited() && this->viddec.is_inited()) ? "A-V" : (this->viddec.is_inited() ? "M-V" : (this->auddec.is_inited() ? "M-A" : "   ")),
        av_diff,
        this->frame_drops_early + this->frame_drops_late,
        aqsize / 1024,
        vqsize / 1024,        
        this->viddec.is_inited() ? this->viddec.avctx->pts_correction_num_faulty_dts : 0,
        this->viddec.is_inited() ? this->viddec.avctx->pts_correction_num_faulty_pts : 0);

    if (this->show_status == 1 && AV_LOG_INFO > av_log_get_level())
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

    this->get_render()->set_default_window_size(vp->width, vp->height, vp->sample_aspect_ratio);

    av_frame_move_ref(vp->frame, src_frame);
    frame_q.frame_queue_push();
    return 0;
}

// 返回 <0 表示退出解码线程
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
        dpts = av_q2d(this->stream_param.time_base ) * frame->pts;

    //frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(this->_vs->format_context, stream, frame); // 有点过于奥义，试着删掉看效果

    if (this->_av_decoder->get_master_sync_type() != AV_SYNC_VIDEO_MASTER) {
        // 看看是否有必要抛弃帧
        if (frame->pts != AV_NOPTS_VALUE) {
            double diff = dpts - this->_av_decoder->get_master_clock();
            if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                diff < 0 &&
                pkt_serial == stream_clock.serial &&
                packet_q.nb_packets) 
            {
                // 抛弃帧
                this->_av_decoder->frame_drops_early++;
                av_frame_unref(frame);
                got_picture = 0;
            }
        }
    }
    
    return got_picture;
}


ThreadRetType  AudioDecoder::thread_main()
{
    AVFrame *frame = av_frame_alloc();
    Frame *af;

    int got_frame = 0;
    AVRational time_base;
    int ret = 0;

    if (!frame)
        return (ThreadRetType) AVERROR(ENOMEM);

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
    return (ThreadRetType)0;
}

int Decoder::decoder_start()
{
    packet_q.packet_queue_start();
    create_thread(); // todo: 统一 error report 机制
    return 0;
}

int Decoder::stream_has_enough_packets() {
    if (!is_inited() || packet_q.abort_request)
        return 1;
    return       //(stream->disposition & AV_DISPOSITION_ATTACHED_PIC) ||   // 不考虑“show静态图片”的情况
        packet_q.nb_packets > QUEUE_ENOUGH_PKG && (!packet_q.total_duration    // 如果 total_duration 是0，说明pkt.duration无效，那就是只看包数量
                                            || av_q2d(this->stream_param.time_base) * packet_q.total_duration > QUEUE_ENOUGH_TIME);
            
}

ThreadRetType  VideoDecoder::thread_main()
{
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = this->stream_param.time_base;

    if (!frame)
        return (ThreadRetType)AVERROR(ENOMEM);

    for (;;) {
        ret = get_video_frame( frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;

        AVRational guessed_frame_rate = stream_param.guessed_vframe_rate;
        AVRational szr_dur = { guessed_frame_rate.den, guessed_frame_rate.num };
        duration = (guessed_frame_rate.num && guessed_frame_rate.den ? av_q2d(szr_dur) : 0);
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        ret = queue_picture( frame, pts, duration, frame->pkt_pos, pkt_serial);
        av_frame_unref(frame);

        if (ret < 0)
            goto the_end;
    }
 the_end:
    av_frame_free(&frame);
    return (ThreadRetType) 0;
}


/* return the wanted number of samples to get better sync if sync_type is video
 * or external master clock */
int SimpleAVDecoder::synchronize_audio( int nb_samples)  
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

    if (this->_av_decoder->paused)
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
    wanted_nb_samples = this->_av_decoder->synchronize_audio(af->frame->nb_samples);

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
        this->_av_decoder->extclk.sync_clock_to_slave( &this->stream_clock);
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
    while (!(this->get_render()->audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
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


int VideoState::decode_interrupt_cb(void *ctx)
{
    VideoState *is = (VideoState*)ctx;
    return is->abort_request;
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

    err = avformat_open_input(&format_context, this->file_to_play, this->iformat, NULL);
    if (err < 0) {
        print_error(this->file_to_play, err);
        return -1;
    }

    this->format_context = format_context;
    guard.dismiss();

    av_format_inject_global_side_data(format_context);

    err = avformat_find_stream_info(format_context, NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_WARNING,
            "%s: could not find codec parameters\n", this->file_to_play.GetString());
        return  -1;
    }

    if (format_context->pb)
        format_context->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    this->eof = 0; 

    /* if seeking requested, we execute it */
    if (this->streamopt_start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = this->streamopt_start_time;
        /* add the stream start time */
        if (format_context->start_time != AV_NOPTS_VALUE)
            timestamp += format_context->start_time;
        ret = avformat_seek_file(format_context, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                this->file_to_play.GetString(), (double)timestamp / AV_TIME_BASE);
        }
    }

    if (this->infinite_buffer < 0 && is_realtime(format_context))
        this->infinite_buffer = 1;

    av_dump_format(format_context, 0, this->file_to_play, 0);

    return 0;
}
 
// Return:  0 -- success, non-zero -- error.
int SimpleAVDecoder::open_stream(const AVCodecParameters * codec_para, const StreamParam* extra_para, SimpleConditionVar*  notify_reader)
{ 
    Decoder * decoder = NULL;
    if (AVMEDIA_TYPE_AUDIO == codec_para->codec_type  )  {
        decoder = &this->auddec;
    } 
    else if (AVMEDIA_TYPE_VIDEO == codec_para->codec_type   ) {
        decoder = &this->viddec;
    }

    if (!decoder)
    {
        LOG_WARN("Only support audio/video stream.\n");
        return 1;
    }
    
    if (decoder->is_inited() ) {
        LOG_WARN("%s stream alread opened.\n", AVMEDIA_TYPE_AUDIO == codec_para->codec_type ? "audio" : "video" );
        return 2;
    }

    AVCodecContext* codec_context  = Decoder::create_codec_directly (codec_para , extra_para);
    if(!codec_context )
    {
        return 3;
    }

    if ( decoder->decoder_init( codec_context, extra_para, notify_reader))
    {
        return 4;
    }

    return 0 ;
}

// 返回 bit0 代表V opened ， bit1 代表A opened 
int SimpleAVDecoder::open_stream_from_avformat(AVFormatContext* format_context, /*in,hold*/SimpleConditionVar* notify_reader, int* vstream_id, int* astream_id)
{
    // 1. some preparation
    this->max_frame_duration = (format_context->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
    this->realtime = is_realtime(format_context);

    // 2. open vstream if present
    int vs,as; 

    vs = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO,-1, -1, NULL, 0);
    if (vs >= 0)
    {
        AVStream* stream = format_context->streams[vs];
        StreamParam  extra_para;  
        extra_para.time_base  = stream->time_base;
        extra_para.start_time = stream->start_time;
        extra_para.guessed_vframe_rate = av_guess_frame_rate(format_context, stream, NULL);

        if (0 == open_stream(stream->codecpar , &extra_para, notify_reader))
        {
            *vstream_id = vs;   

            if (stream->codecpar->width)   // todo: maybe moving to VideoDecoer::decoder_init would be better
            {
                AVRational sar = av_guess_sample_aspect_ratio(format_context, stream, NULL);
                this->render.set_default_window_size(stream->codecpar->width, stream->codecpar->height, sar);
            }
        }
    }
    else
    {
        LOG_WARN("No video stream found.\n");
        vs = -1;
    }

    // 3. open astream if present
    as = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, vs, NULL, 0);
    if (as >= 0)
    {
        AVStream* stream = format_context->streams[as];
        StreamParam  extra_para;  
        extra_para.time_base  = stream->time_base;
        extra_para.start_time = stream->start_time;

        if (0 == open_stream(stream->codecpar , &extra_para, notify_reader))
        {
            *astream_id = as;
        }
    }
    
    return get_opened_streams_mask();
}


int VideoState::read_loop_check_pause() // return: nonzero -- shoud 'continue', 0 -- go on current iteration
{
    if (this->paused != this->last_paused) { // we should take some action
        this->last_paused = this->paused;
        if (this->paused)
        {
            int r = av_read_pause(format_context);            
            LOG_ERROR( "av_read_pause got %d, %s\n", r, av_strerror2(r).GetString());
        }
        else
        {   
            int r = av_read_play(format_context);
            LOG_ERROR("av_read_play got %d, %s\n", r, av_strerror2(r).GetString());
        }
    }

    if (this->paused &&
        (!strcmp(this->format_context->iformat->name, "rtsp") ||
            (format_context->pb && !strncmp(this->file_to_play.GetString(), "mmsh:", 5)))) {
        /* wait 10 ms to avoid trying to get another packet */
        /* XXX: horrible */
        SDL_Delay(10);
        return 1;
    }
    return 0;
}
void SimpleAVDecoder::discard_buffer(double seek_target ) // 用于在seek后清cache。如果是按时间seek，则应顺手给出 seek_target (以秒为单位)
{
    if (this->auddec.is_inited()) {
        this->auddec.packet_q.packet_queue_flush();
        this->auddec.packet_q.packet_queue_put(&PacketQueue::flush_pkt); // packet queue 的 serial ++
    }
    if (this->viddec.is_inited()) {
        this->viddec.packet_q.packet_queue_flush();
        this->viddec.packet_q.packet_queue_put(&PacketQueue::flush_pkt);
    }
    this->extclk.set_clock(seek_target, 0);    
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
        this->av_decoder.discard_buffer( (this->seek_flags & AVSEEK_FLAG_BYTE) ?  NAN : seek_target / (double)AV_TIME_BASE);
    }

    this->seek_req = 0;
    this->eof = 0;
    if (this->paused)
        this->step_to_next_frame();

    return 0;
}

int SimpleAVDecoder::is_buffer_full()
{
    if (this->auddec.packet_q.size + this->viddec.packet_q.size > MAX_QUEUE_SIZE)
    {
        //OutputDebugString("#\n"); 
        return 1;
    }

    if (this->auddec.stream_has_enough_packets() && this->viddec.stream_has_enough_packets())
    {
        //OutputDebugString("$\n");
        return 2;
    }
    return 0;
}
void SimpleAVDecoder::feed_null_pkt() // todo: 作用不明，待研究
{
    if (this->viddec.is_inited())
        this->viddec.packet_q.packet_queue_put_nullpacket(0);
    if (this->auddec.is_inited())
        this->auddec.packet_q.packet_queue_put_nullpacket(0);
}

void SimpleAVDecoder::feed_pkt(AVPacket* pkt, const AVPacketExtra* extra) // 向解码器喂数据包
{
    if (PSI_VIDEO == extra->v_or_a  ) {
        this->viddec.packet_q.packet_queue_put(pkt);
    }
    else if (PSI_AUDIO == extra->v_or_a  ) {
        this->auddec.packet_q.packet_queue_put(pkt);
    }
    else {
        av_packet_unref(pkt);
    }
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
ThreadRetType  VideoState::thread_main()
{
    int ret = 1;
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
        if  (infinite_buffer <1 && this->av_decoder.is_buffer_full())
        {
            /* wait 10 ms */
            AutoLocker _yes_locked(continue_read_thread);
            this->continue_read_thread.timed_wait_ms(10);
            continue;
        }

        ret = av_read_frame(format_context, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(format_context->pb)) && !this->eof) {  
                this->av_decoder.feed_null_pkt(); // todo: null pkt 作用不明
                this->eof = 1;
                // todo: 这里可以回调一下通知上层 EOF
                if (streamopt_autoexit)
                    goto fail;

            }
            if (format_context->pb && format_context->pb->error) {
                // todo: 这里可以回调一下通知上层 I/O Error
                if (streamopt_autoexit)
                    goto fail;
            }

            {
                AutoLocker _yes_locked(continue_read_thread);
                this->continue_read_thread.timed_wait_ms(10);
            }
            continue;
        } else {
            this->eof = 0;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        if (!is_pkt_in_play_range(pkt)) { 
            av_packet_unref(pkt); // todo: 不到 start_point，还是超过duration，应该有不同处理
            continue;
        }
        
        AVPacketExtra extra;
        fill_packet_extra( & extra, pkt);

        this->av_decoder.feed_pkt(pkt, &extra);
    }
    
    ret = 0;
 fail:

    if (ret != 0) {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = this;
        SDL_PushEvent(&event);
    }

    return (ThreadRetType) 0;
}

void VideoState::fill_packet_extra( AVPacketExtra* extra, const AVPacket* pkt) const
{ 
    if ( this->last_video_stream == pkt->stream_index )
    {
        extra->v_or_a = PSI_VIDEO;
    }
    else if ( this->last_audio_stream == pkt->stream_index  )
    {
        extra->v_or_a = PSI_AUDIO;
    }
}

int VideoState::is_pkt_in_play_range( AVPacket* pkt)
{
    if (this->streamopt_duration == AV_NOPTS_VALUE)
        return 1;
    //todo: 如果是‘实况转播’，来包就放，也应该无条件返回1。

    int64_t stream_start_time = format_context->streams[pkt->stream_index]->start_time;
    if (AV_NOPTS_VALUE == stream_start_time)
        stream_start_time = 0;

    int64_t pkt_ts = ( pkt->pts == AV_NOPTS_VALUE) ? pkt->dts : pkt->pts;
    double ts_relative_to_stream = stream_ts_to_second( pkt_ts - stream_start_time, pkt->stream_index);
    //printf("stream %d, ts_relative_to_stream = %f\n", pkt->stream_index, ts_relative_to_stream);
    double start_point = (double)((streamopt_start_time != AV_NOPTS_VALUE ? streamopt_start_time : 0) / 1000000); // opt_start_time is in unit of microsecond
    double duration = (double) (streamopt_duration / 1000000);  // opt_duration  is in unit of microsecond

    return (ts_relative_to_stream - start_point) <= duration;
}

double VideoState::stream_ts_to_second(int64_t ts,  int stream_index)
{
    return ts * av_q2d(format_context->streams[stream_index]->time_base);
}

template <>
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

    this->last_video_stream = this->last_audio_stream =  -1;

    this->file_to_play = filename;
    
    this->iformat = iformat;


    // 打开文件，酌情seek   
    if (open_stream_file())
    {
        return 5;
    }

    // 逐流打开解码器
    if (0 == this->av_decoder.open_stream_from_avformat(this->format_context, &this->continue_read_thread,&last_video_stream, &last_audio_stream))
    {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s'.\n",  this->file_to_play.GetString());
        return 6;
    }

    this->create_thread(); // 启动读流线程
    
    close_if_failed.dismiss();
    return 0;
}

void VideoState::seek_chapter( int incr)
{
    int64_t pos = (int64_t) ( this->av_decoder.get_master_clock() * AV_TIME_BASE );
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


void print_error(const char* filename, int err)
{
    char errbuf[128];
    const char* errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));
    av_log(NULL, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
}
