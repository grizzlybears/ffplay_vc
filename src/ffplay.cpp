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


#include "ffdecoder/ffdecoder.h"

extern "C"
{
#include "cmdutils.h"
}

#include <assert.h>

#include "utils/utils.h"

const char g_program_name[] = "ffplay";
const int program_birth_year = 2003;


void refresh_loop_wait_event(VideoState *is, SDL_Event *event) {
    double remaining_time = 0.0;
    SDL_PumpEvents();
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (!cursor_hidden && av_gettime_relative() - cursor_last_shown > CURSOR_HIDE_DELAY) {
            SDL_ShowCursor(0);
            cursor_hidden = 1;
        }
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if ( !is->paused || is->force_refresh)
            is->video_refresh( &remaining_time);
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
        refresh_loop_wait_event(cur_stream, &event);
        switch (event.type) {
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                do_exit(cur_stream);
                break;
            }
            // If we don't yet have a window, skip all key events, because read_thread might still be initializing...
            if (!cur_stream->width)
                continue;
            switch (event.key.keysym.sym) {
            case SDLK_f:
                g_render.toggle_full_screen();
                cur_stream->force_refresh = 1;
                break;
            case SDLK_p:
            case SDLK_SPACE:
                toggle_pause(cur_stream);
                break;
            case SDLK_m:
                toggle_mute(cur_stream);
                break;
            case SDLK_KP_MULTIPLY:
            case SDLK_0:
                update_volume(cur_stream, 1, SDL_VOLUME_STEP);
                break;
            case SDLK_KP_DIVIDE:
            case SDLK_9:
                update_volume(cur_stream, -1, SDL_VOLUME_STEP);
                break;
            case SDLK_s: // S: Step to next frame
                step_to_next_frame(cur_stream);
                break;
            case SDLK_a:
                cur_stream->stream_cycle_channel( AVMEDIA_TYPE_AUDIO);
                break;
            case SDLK_v:
                cur_stream->stream_cycle_channel( AVMEDIA_TYPE_VIDEO);
                break;
            case SDLK_c:
                cur_stream->stream_cycle_channel(AVMEDIA_TYPE_VIDEO);
                cur_stream->stream_cycle_channel(AVMEDIA_TYPE_AUDIO);
                cur_stream->stream_cycle_channel(AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_t:
                cur_stream->stream_cycle_channel( AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_PAGEUP:
                if (cur_stream->format_context->nb_chapters <= 1) {
                    incr = 600.0;
                    goto do_seek;
                }
                seek_chapter(cur_stream, 1);
                break;
            case SDLK_PAGEDOWN:
                if (cur_stream->format_context->nb_chapters <= 1) {
                    incr = -600.0;
                    goto do_seek;
                }
                seek_chapter(cur_stream, -1);
                break;
            case SDLK_LEFT:
                incr = seek_interval ? -seek_interval : -10.0;
                goto do_seek;
            case SDLK_RIGHT:
                incr = seek_interval ? seek_interval : 10.0;
                goto do_seek;
            case SDLK_UP:
                incr = 60.0;
                goto do_seek;
            case SDLK_DOWN:
                incr = -60.0;
            do_seek:
                    {
                        pos = get_master_clock(cur_stream);
                        if (isnan(pos))
                            pos = (double)cur_stream->seek_pos / AV_TIME_BASE;
                        pos += incr;
                        if (cur_stream->format_context->start_time != AV_NOPTS_VALUE && pos < cur_stream->format_context->start_time / (double)AV_TIME_BASE)
                            pos = cur_stream->format_context->start_time / (double)AV_TIME_BASE;
                        stream_seek(cur_stream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
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
                    g_render.toggle_full_screen();
                    cur_stream->force_refresh = 1;
                    last_mouse_left_click = 0;
                } else {
                    last_mouse_left_click = av_gettime_relative();
                }
            }
        case SDL_MOUSEMOTION:
            if (cursor_hidden) {
                SDL_ShowCursor(1);
                cursor_hidden = 0;
            }
            cursor_last_shown = av_gettime_relative();
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
                    stream_seek(cur_stream, size*x/cur_stream->width, 0, 1);
                } else {
                    int64_t ts;
                    int ns, hh, mm, ss;
                    int tns, thh, tmm, tss;
                    tns  = cur_stream->format_context->duration / 1000000LL;
                    thh  = tns / 3600;
                    tmm  = (tns % 3600) / 60;
                    tss  = (tns % 60);
                    frac = x / cur_stream->width;
                    ns   = frac * tns;
                    hh   = ns / 3600;
                    mm   = (ns % 3600) / 60;
                    ss   = (ns % 60);
                    av_log(NULL, AV_LOG_INFO,
                           "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
                            hh, mm, ss, thh, tmm, tss);
                    ts = frac * cur_stream->format_context->duration;
                    if (cur_stream->format_context->start_time != AV_NOPTS_VALUE)
                        ts += cur_stream->format_context->start_time;
                    stream_seek(cur_stream, ts, 0, 0);
                }
            break;
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    g_render.screen_width  = cur_stream->width  = event.window.data1;
                    g_render.screen_height = cur_stream->height = event.window.data2;
                    
                case SDL_WINDOWEVENT_EXPOSED:
                    cur_stream->force_refresh = 1;
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            do_exit(cur_stream);
            break;
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

int opt_width(void *optctx, const char *opt, const char *arg)
{
    g_render.screen_width = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

int opt_height(void *optctx, const char *opt, const char *arg)
{
    g_render.screen_height = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

int opt_format(void *optctx, const char *opt, const char *arg)
{
    opt_file_iformat = av_find_input_format(arg);
    if (!opt_file_iformat) {
        av_log(NULL, AV_LOG_FATAL, "Unknown input format: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

int opt_frame_pix_fmt(void *optctx, const char *opt, const char *arg)
{
    av_log(NULL, AV_LOG_WARNING, "Option -pix_fmt is deprecated, use -pixel_format.\n");
    return opt_default(NULL, "pixel_format", arg);
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

int opt_codec(void *optctx, const char *opt, const char *arg)
{
   const char *spec = strchr(opt, ':');
   if (!spec) {
       av_log(NULL, AV_LOG_ERROR,
              "No media specifier was specified in '%s' in option '%s'\n",
               arg, opt);
       return AVERROR(EINVAL);
   }
   spec++;
   switch (spec[0]) {
   case 'a' :    opt_audio_codec_name = arg; break;
   case 's' : opt_subtitle_codec_name = arg; break;
   case 'v' :    opt_video_codec_name = arg; break;
   default:
       av_log(NULL, AV_LOG_ERROR,
              "Invalid media specifier '%s' in option '%s'\n", spec, opt);
       return AVERROR(EINVAL);
   }
   return 0;
}

static int dummy;

static const OptionDef options[] = {
    CMDUTILS_COMMON_OPTIONS
    { "x", HAS_ARG, { .func_arg = opt_width }, "force displayed width", "width" },
    { "y", HAS_ARG, { .func_arg = opt_height }, "force displayed height", "height" },
    { "s", HAS_ARG | OPT_VIDEO, { .func_arg = opt_frame_size }, "set frame size (WxH or abbreviation)", "size" },
    { "fs", OPT_BOOL, { &opt_full_screen }, "force full screen" },
    { "an", OPT_BOOL, { &opt_audio_disable }, "disable audio" },
    { "sn", OPT_BOOL, { &opt_subtitle_disable }, "disable subtitling" },
    { "ss", HAS_ARG, { .func_arg = opt_seek }, "seek to a given position in seconds", "pos" },
    { "t", HAS_ARG, { .func_arg = parse_opt_duration }, "play  \"duration\" seconds of audio/video", "duration" },
    { "seek_interval", OPT_FLOAT | HAS_ARG, { &seek_interval }, "set seek interval for left/right keys, in seconds", "seconds" },
    { "alwaysontop", OPT_BOOL, { &opt_alwaysontop }, "window always on top" },
    { "volume", OPT_INT | HAS_ARG, { &opt_startup_volume}, "set startup volume 0=min 100=max", "volume" },
    { "f", HAS_ARG, { .func_arg = opt_format }, "force format", "fmt" },
    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, { .func_arg = opt_frame_pix_fmt }, "set pixel format", "format" },
    { "stats", OPT_BOOL | OPT_EXPERT, { &opt_show_status }, "show status", "" },
    { "genpts", OPT_BOOL | OPT_EXPERT, { &genpts }, "generate pts", "" },
    { "drp", OPT_INT | HAS_ARG | OPT_EXPERT, { &decoder_reorder_pts }, "let decoder reorder pts 0=off 1=on -1=auto", ""},
    { "lowres", OPT_INT | HAS_ARG | OPT_EXPERT, { &lowres }, "", "" },
    { "sync", HAS_ARG | OPT_EXPERT, { .func_arg = opt_sync }, "set audio-video sync. type (type=audio/video/ext)", "type" },
    { "autoexit", OPT_BOOL | OPT_EXPERT, { &autoexit }, "exit at the end", "" },
    { "framedrop", OPT_BOOL | OPT_EXPERT, { &opt_framedrop }, "drop frames when cpu is too slow", "" },
    { "infbuf", OPT_BOOL | OPT_EXPERT, { &opt_infinite_buffer }, "don't limit the input buffer size (useful with realtime streams)", "" },
    { "window_title", OPT_STRING | HAS_ARG, { &g_window_title }, "set window title", "window title" },
    { "left", OPT_INT | HAS_ARG | OPT_EXPERT, { &g_render.screen_left }, "set the x position for the left of the window", "x pos" },
    { "top", OPT_INT | HAS_ARG | OPT_EXPERT, { &g_render.screen_top }, "set the y position for the top of the window", "y pos" },
    { "rdftspeed", OPT_INT | HAS_ARG| OPT_AUDIO | OPT_EXPERT, { &rdftspeed }, "rdft speed", "msecs" },
    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, { .func_arg = opt_default }, "generic catch all option", "" },
    { "i", OPT_BOOL, { &dummy}, "read specified file", "input_file"},
    { "codec", HAS_ARG, { .func_arg = opt_codec}, "force decoder", "decoder_name" },
    { "acodec", HAS_ARG | OPT_STRING | OPT_EXPERT, {    &opt_audio_codec_name }, "force audio decoder",    "decoder_name" },
    { "scodec", HAS_ARG | OPT_STRING | OPT_EXPERT, { &opt_subtitle_codec_name }, "force subtitle decoder", "decoder_name" },
    { "vcodec", HAS_ARG | OPT_STRING | OPT_EXPERT, {    &opt_video_codec_name }, "force video decoder",    "decoder_name" },
    { "autorotate", OPT_BOOL, { &autorotate }, "automatically rotate video", "" },
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
           "a                   cycle audio channel in the current program\n"
           "v                   cycle video channel\n"
           "t                   cycle subtitle channel in the current program\n"
           "c                   cycle program\n"
           "w                   cycle video filters or show modes\n"
           "s                   activate frame-step mode\n"
           "left/right          seek backward/forward 10 seconds or to custom interval if -seek_interval is set\n"
           "down/up             seek backward/forward 1 minute\n"
           "page down/page up   seek backward/forward 10 minutes\n"
           "right mouse click   seek to percentage in file corresponding to fraction of width\n"
           "left double-click   toggle full screen\n"
           );
}

/* Called from the main */
int main(int argc, char **argv)
{
    //printf_as_default_logger();
    init_default_logger(GetModuleHandle(NULL), "ffplay.log");
    
    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    parse_loglevel(argc, argv, options);

    /* register all codecs, demux and protocols */
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();

    init_opts();

    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

    show_banner(argc, argv, options);

    parse_options(NULL, argc, argv, options, opt_input_file);

    if (!opt_input_filename) {
        show_usage();
        av_log(NULL, AV_LOG_FATAL, "An input file must be specified\n");
        av_log(NULL, AV_LOG_FATAL,
               "Use -h to get full help or, even better, run 'man %s'\n", g_program_name);
        return (1);
    }

    if (g_render.init(opt_audio_disable, opt_alwaysontop))
    {
        do_exit(NULL);
    }

    av_init_packet(&PacketQueue::flush_pkt);
    PacketQueue::flush_pkt.data = (uint8_t*)&PacketQueue::flush_pkt;

    VideoState* is = new VideoState();
    if (!is)
    {
        av_log(NULL, AV_LOG_FATAL, "Failed to create VideoState.\n");
        do_exit(NULL);
    }
    
    if (is->open(opt_input_filename, opt_file_iformat)) {
        av_log(NULL, AV_LOG_FATAL, "Failed to initialize VideoState!\n");
        do_exit(is);
    }

    event_loop(is);

    /* never returns */

    return 0;
}
