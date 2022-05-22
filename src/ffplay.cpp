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

extern "C" {
#include "cmdutils.h"
}
#include <assert.h>
#include <signal.h>

#include <SDL.h>
#include "cmdutils.h"
#include "ffdecoder/ffdecoder.h"

const char g_program_name[] = "ffplay";
const int program_birth_year = 2003;

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

void sigterm_handler(int sig);

// keep refreshing video until any SDL_Event occurs
void refresh_loop_wait_event(SimpleAVDecoder * av_decoder, SDL_Event *event) {
    double remaining_time = 0.0;
    SDL_PumpEvents();
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (! av_decoder->render->cursor_hidden && av_gettime_relative() - av_decoder->render->cursor_last_shown > CURSOR_HIDE_DELAY) {
            SDL_ShowCursor(0);
            av_decoder->render->cursor_hidden = 1;
        }
        if (remaining_time > 0.0)
            av_usleep((unsigned int)(remaining_time * 1000000.0));
        
        double speed = av_decoder->get_decoder_clock()->get_clock_speed();
        remaining_time = speed > 1 ?  0.001 * FFMAX(1,   10 -  speed ) :   REFRESH_RATE ;
        if ( !av_decoder->is_paused() || av_decoder->is_drawing_needed())
            av_decoder->video_refresh( &remaining_time);

        SDL_PumpEvents();
    }
}

/* handle an event sent by the GUI */
void event_loop(VideoState *cur_stream)
{
    SDL_Event event;
    double incr, pos, frac;

    for (;;) {
        double x;
        refresh_loop_wait_event(&cur_stream->av_decoder, &event);
        switch (event.type) {
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                return;
            }

            // If we don't yet have a window, skip all key events, because read_thread might still be initializing...
            if (!cur_stream->av_decoder.render->is_initialized())
                continue;
            switch (event.key.keysym.sym) {
            case SDLK_f:
                cur_stream->av_decoder.render->toggle_full_screen();
                cur_stream->av_decoder.toggle_need_drawing (1);
                break;
            case SDLK_p:
            case SDLK_SPACE:
                cur_stream->toggle_pause();
                break;
            case SDLK_m:
                cur_stream->av_decoder.toggle_mute();
                break;
            case SDLK_PLUS:
            case SDLK_KP_PLUS:
                {
                    double cur_speed = cur_stream->av_decoder.get_decoder_clock()->get_clock_speed();
                    double speed = 2 * cur_speed;
                    debug_printf(" speed: %.3f -> %.3f\n", cur_speed,  speed );
                    cur_stream->av_decoder.get_decoder_clock()->set_clock_speed( speed );

                    if ( ! float_equal(speed, 1.0) )
                    {
	                    cur_stream->av_decoder.set_master_sync_type(AV_SYNC_EXTERNAL_CLOCK ); 
                    }
                    else
                    { 
	                    cur_stream->av_decoder.set_master_sync_type(AV_SYNC_AUDIO_MASTER); 
                    }

                    cur_stream->av_decoder.get_decoder_clock()->set_clock_speed( speed );
                }
                break; 
            case SDLK_MINUS:
            case SDLK_KP_MINUS:
                {
                    double cur_speed = cur_stream->av_decoder.get_decoder_clock()->get_clock_speed();
                    double speed =  cur_speed / 2;
                    debug_printf(" speed: %.3f -> %.3f\n", cur_speed,  speed );

                    if ( ! float_equal(speed, 1.0) )
                    {
	                    cur_stream->av_decoder.set_master_sync_type(AV_SYNC_EXTERNAL_CLOCK); 
                    }
                    else
                    { 
	                    cur_stream->av_decoder.set_master_sync_type(AV_SYNC_AUDIO_MASTER); 
                    }
                    cur_stream->av_decoder.get_decoder_clock()->set_clock_speed( speed );
                }
                break;

            case SDLK_KP_MULTIPLY:
            case SDLK_0:
                cur_stream->av_decoder.update_volume( 10);
                break;
            case SDLK_KP_DIVIDE:
            case SDLK_9:
                cur_stream->av_decoder.update_volume( -10);
                break;
            case SDLK_s: // S: Step to next frame
                cur_stream->step_to_next_frame();
                break;
           
            case SDLK_PAGEUP:
                if (cur_stream->format_context->nb_chapters <= 1) {
                    incr = 600.0;  // seek  + 600(s) 
                    goto do_seek;
                }
                cur_stream->seek_chapter( 1);  // to next  chapter
                break;
            case SDLK_PAGEDOWN:
                if (cur_stream->format_context->nb_chapters <= 1) {
                    incr = -600.0;
                    goto do_seek;
                }
                cur_stream->seek_chapter( -1);
                break;
            case SDLK_LEFT:
                incr =  -10.0; // seek -10(s)
                goto do_seek;
            case SDLK_RIGHT:
                incr =  10.0; // seek +10(s) 
                goto do_seek;
            case SDLK_UP:
                incr = 60.0;  
                goto do_seek;
            case SDLK_DOWN:
                incr = -60.0; 
            do_seek:
                    {
                        pos = cur_stream->av_decoder.get_master_clock();
                        if (isnan(pos))
                            pos = (double) 0;
                        pos += incr;
                        if (cur_stream->format_context->start_time != AV_NOPTS_VALUE && pos < cur_stream->format_context->start_time / (double)AV_TIME_BASE)
                            pos = cur_stream->format_context->start_time / (double)AV_TIME_BASE;
                        cur_stream->stream_seek((int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
                    }
                break;
            default:
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                static int64_t last_mouse_left_click = 0;
                if (av_gettime_relative() - last_mouse_left_click <= 500000) {
                    cur_stream->av_decoder.render->toggle_full_screen();
                    cur_stream->av_decoder.toggle_need_drawing (1);
                    last_mouse_left_click = 0;
                } else {
                    last_mouse_left_click = av_gettime_relative();
                }
            }
        case SDL_MOUSEMOTION:
            if (cur_stream->av_decoder.render->cursor_hidden) {
                SDL_ShowCursor(1);
                cur_stream->av_decoder.render->cursor_hidden = 0;
            }
            cur_stream->av_decoder.render->cursor_last_shown = av_gettime_relative();
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button != SDL_BUTTON_RIGHT)
                    break;
                x = event.button.x;
            } else {
                if (!(event.motion.state & SDL_BUTTON_RMASK))
                    break;
                x = event.motion.x;
            }
                if (cur_stream->format_context->duration <= 0) {
                    uint64_t size =  avio_size(cur_stream->format_context->pb);
                    cur_stream->stream_seek( (int64_t)( size * x / cur_stream->av_decoder.render->screen_width), 0, 1);
                } else {
                    int64_t ts;
                    int ns, hh, mm, ss;
                    int tns, thh, tmm, tss;
                    tns  = (int)( cur_stream->format_context->duration / AV_TIME_BASE);
                    thh  = tns / 3600;
                    tmm  = (tns % 3600) / 60;
                    tss  = (tns % 60);
                    frac = x / cur_stream->av_decoder.render->screen_width ;
                    ns   = (int)(frac * tns);
                    hh   = ns / 3600;
                    mm   = (ns % 3600) / 60;
                    ss   = (ns % 60);
                    av_log(NULL, AV_LOG_INFO,
                           "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
                            hh, mm, ss, thh, tmm, tss);
                    ts = (int64_t)( frac * cur_stream->format_context->duration );
                    if (cur_stream->format_context->start_time != AV_NOPTS_VALUE)
                        ts += cur_stream->format_context->start_time;
                    cur_stream->stream_seek( ts, 0, 0);
                }
            break;
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    cur_stream->av_decoder.render->screen_width  = event.window.data1;
                    cur_stream->av_decoder.render->screen_height = event.window.data2;
                    
                case SDL_WINDOWEVENT_EXPOSED:
                    cur_stream->av_decoder.toggle_need_drawing (1);
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            return;

        default:
            break;
        }
    }
}

int opt_frame_size(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "Option -s is deprecated, use -video_size.\n");
    return opt_default(NULL, "video_size", arg);
}


int opt_sync(void *optctx, const char *opt, const char *arg)
{
    if (!strcmp(arg, "audio"))
        opt_av_sync_type = AV_SYNC_AUDIO_MASTER;
    else if (!strcmp(arg, "video"))
        opt_av_sync_type = AV_SYNC_VIDEO_MASTER;
    else if (!strcmp(arg, "ext"))
        opt_av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
    else {
        av_log(NULL, AV_LOG_ERROR, "Unknown value for %s: %s\n", opt, arg);
        exit(1);
    }
    return 0;
}

int opt_seek(void *optctx, const char *opt, const char *arg)
{
    opt_start_time = parse_time_or_die(opt, arg, 1);
    return 0;
}

int parse_opt_duration(void *optctx, const char *opt, const char *arg)
{
    opt_duration = parse_time_or_die(opt, arg, 1);
    return 0;
}


void opt_input_file(void *optctx, const char *filename)
{
    if (opt_input_filename) {
        av_log(NULL, AV_LOG_FATAL,
               "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                filename, opt_input_filename);
        exit(1);
    }
    if (!strcmp(filename, "-"))
        filename = "pipe:";
    opt_input_filename = filename;
}

static int dummy;

static const OptionDef options[] = {
#if defined(__GNUC__) || ( _MSC_VER >= 1920)
    CMDUTILS_COMMON_OPTIONS
    { "s", HAS_ARG | OPT_VIDEO, { .func_arg = opt_frame_size }, "set frame size (WxH or abbreviation)", "size" },
    { "ss", HAS_ARG, { .func_arg = opt_seek }, "seek to a given position in seconds", "pos" },
    { "t", HAS_ARG, { .func_arg = parse_opt_duration }, "play  \"duration\" seconds of audio/video", "duration" },
    { "stats", OPT_BOOL | OPT_EXPERT, { &opt_show_status }, "show status", "" },
    { "drp", OPT_INT | HAS_ARG | OPT_EXPERT, { &opt_decoder_reorder_pts }, "let decoder reorder pts 0=off 1=on -1=auto", ""},
    { "sync", HAS_ARG | OPT_EXPERT, { .func_arg = opt_sync }, "set audio-video sync. type (type=audio/video/ext)", "type" },
    { "autoexit", OPT_BOOL | OPT_EXPERT, { &opt_autoexit }, "exit at the end", "" },
    { "i", OPT_BOOL, { &dummy}, "read specified file", "input_file"},    
#endif
    { NULL, },
};

static void show_usage(void)
{
    av_log(NULL, AV_LOG_INFO, "Simple media player\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [options] input_file\n", g_program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}

void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, OPT_EXPERT, 0);
    show_help_options(options, "Advanced options:", OPT_EXPERT, 0, 0);
    printf("\n");
    show_help_children(avcodec_get_class(), AV_OPT_FLAG_DECODING_PARAM);
    show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
#if !CONFIG_AVFILTER
    show_help_children(sws_get_class(), AV_OPT_FLAG_ENCODING_PARAM);
#else
    show_help_children(avfilter_get_class(), AV_OPT_FLAG_FILTERING_PARAM);
#endif
    printf("\nWhile playing:\n"
           "q, ESC              quit\n"
           "f                   toggle full screen\n"
           "p, SPC              pause\n"
           "m                   toggle mute\n"
           "9, 0                decrease and increase volume respectively\n"
           "/, *                decrease and increase volume respectively\n"
           "s                   activate frame-step mode\n"
           "left/right          seek backward/forward 10 seconds or to custom interval if -seek_interval is set\n"
           "down/up             seek backward/forward 1 minute\n"
           "page down/page up   seek backward/forward 10 minutes\n"
           "right mouse click   seek to percentage in file corresponding to fraction of width\n"
           "left double-click   toggle full screen\n"
           );
}

#ifdef _MSC_VER
SharedFile  g_main_logger;  // on Windows, 'FILE*' is not thread-safe.
#endif

/* Called from the main */
int main(int argc, char **argv)
{
    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    parse_loglevel(argc, argv, options);

    /* register all codecs, demux and protocols */
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();

    init_opts();


    show_banner(argc, argv, options);

    parse_options(NULL, argc, argv, options, opt_input_file);

    if (!opt_input_filename) {
        show_usage();
        av_log(NULL, AV_LOG_FATAL, "An input file must be specified\n");
        av_log(NULL, AV_LOG_FATAL,
               "Use -h to get full help or, even better, run 'man %s'\n", g_program_name);
        return (1);
    }

    Decoder::onetime_global_init();

    // init format
    VideoState* is = new VideoState();
    if (!is)
    {
        av_log(NULL, AV_LOG_FATAL, "Failed to create VideoState.\n");
        goto EXIT;
    }

    is->streamopt_start_time = opt_start_time;
    is->streamopt_duration   = opt_duration;
    is->streamopt_autoexit = opt_autoexit;
    
    // init decoder
    is->av_decoder.render = RenderBase::create_sdl_render() ;
    if (!is->av_decoder.render)
    {
        LOG_ERROR("Failed in creating render.\n");
        goto EXIT;
    }

    if (is->av_decoder.render->init(0 /*audio disable*/, 0 /*alwaysontop*/))
    {
        goto EXIT;
    }
    is->av_decoder.render->window_title = opt_input_filename;
    is->av_decoder.show_status = opt_show_status;
    is->av_decoder.set_master_sync_type(opt_av_sync_type);
    is->av_decoder.decoder_reorder_pts = opt_decoder_reorder_pts;
    
    // open media
    if (is->open_input_stream(opt_input_filename, NULL)) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
        goto EXIT;
    }

    signal(SIGINT, sigterm_handler); /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

    event_loop(is);

EXIT:
    if (is)
    {
        is->close_input_stream();
        delete is;
    }

    avformat_network_deinit();
    if (opt_show_status)
        printf("\n");
    SDL_Quit();
    av_log(NULL, AV_LOG_QUIET, "%s", "Bye.\n");

    return 0;
}

void sigterm_handler(int sig)
{
    SDL_Event event;
    event.type = SDL_QUIT;

    SDL_PushEvent(&event);

}

void VideoState::quit_main_loop() 
{
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    event.user.data1 = this;
    SDL_PushEvent(&event);
}
