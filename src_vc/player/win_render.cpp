#include "stdafx.h"

#include "win_render.h"

#if defined(_WIN32) && defined(_DEBUG) 
#define new DEBUG_NEW
#endif

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)
#define RESET_REFRESH_COUNT_EVENT    (SDL_USEREVENT + 3)

WinRender::WinRender(SimpleAVDecoder* decoder, DecoderEventCB* e)
{
	associated_decoder = decoder;
	_event_cb = e;
	canvas = NULL;
    img_convert_ctx = NULL; 
	need_pic_size = 0;
	
	pFrameRGB = NULL;
	rgb_buffer = NULL;

	audio_dev = 0;
}

int WinRender::init(int audio_disable, int alwaysontop)
{
	create_thread(); 
    return 0;
}

void WinRender::attach_to_window(HWND  hWnd)
{
	need_pic_size = 1;
	canvas = hWnd;

	SDL_Event event;
	event.type = RESET_REFRESH_COUNT_EVENT;
	SDL_PushEvent(&event);

}

void WinRender::dettach_from_window()
{
	need_pic_size = 0;
	canvas = NULL;
}

void WinRender::pause_audio(int pause_on )
{
	if (!audio_dev)
	{
		LOG_WARN("Audio not opened yet.\n");
		return;
	}

	SDL_PauseAudioDevice(audio_dev, pause_on);
}

void WinRender::close_audio()
{
	if (audio_dev > 0)
	{
		SDL_CloseAudioDevice(this->audio_dev);
	}

	audio_dev = 0;
}

void WinRender::safe_release()
{
    close_audio(); 

	quit_main_loop();
	this->wait_thread_quit();
    
    if (this->img_convert_ctx)
    {
        sws_freeContext(this->img_convert_ctx);
        this->img_convert_ctx = NULL;
    }

	if (pFrameRGB)
	{
		av_frame_free(&pFrameRGB);
	}

	if (rgb_buffer)
	{
		av_free(rgb_buffer);
		rgb_buffer = NULL;
	}
}


void WinRender::upload_and_draw_frame(Frame* vp)
{
#ifdef _DEBUG
	static int frame_num = 0;

	if (0 == (frame_num % 25))
	{
		OutputDebugStringA("\n");
	}
	OutputDebugStringA(".");
	frame_num++;
#endif

	if (!canvas)
	{
		return;
	}

	AVFrame* frame = vp->frame;

	if (need_pic_size)
	{
		if (_event_cb)
		{
			_event_cb->on_picture_size_got(frame->width, frame->height);
		}

		need_pic_size = 0;
	}
	img_convert_ctx = sws_getCachedContext(img_convert_ctx,
		frame->width, frame->height, (enum AVPixelFormat)frame->format
		, frame->width, frame->height, AV_PIX_FMT_RGB24
		, SWS_BICUBIC, NULL, NULL, NULL);
	if (!img_convert_ctx)
	{
		LOG_ERROR("Cannot initialize the conversion context\n");
		return ;
	}

	// todo:  pFrameRGB & rgb_buffer could be reused, as long as playing same video (same pic size).
	
	if (!pFrameRGB)
	{
		pFrameRGB = av_frame_alloc();
		if (!pFrameRGB)
		{
			LOG_ERROR("Alloc frame failed!\n");
			return;
		}
	}
	

	int rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame->width, frame->height, 1);
	rgb_buffer = (uint8_t *)av_realloc(rgb_buffer, rgb_buffer_size);

	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize
		, rgb_buffer, AV_PIX_FMT_RGB24
		, frame->width, frame->height, 1);

	sws_scale(img_convert_ctx
		, (const unsigned char* const*)frame->data, frame->linesize
		, 0, frame->height
		, pFrameRGB->data, pFrameRGB->linesize);

	draw_frame_gdi(rgb_buffer, frame->width, frame->height, canvas);

}

void WinRender::draw_frame_gdi(uint8_t * rgb_buffer, int width, int height, HWND  hWnd) // called from 'render thread'
{
	HDC hdc = GetDC(hWnd);

	RECT rect;
	GetWindowRect(hWnd, &rect);
	LONG screen_w = rect.right - rect.left;
	LONG screen_h = rect.bottom - rect.top;

	//BMP Header
	BITMAPINFO m_bmphdr = { 0 };
	DWORD dwBmpHdr = sizeof(BITMAPINFO);
	//24bit
	m_bmphdr.bmiHeader.biBitCount = 24;
	m_bmphdr.bmiHeader.biClrImportant = 0;
	m_bmphdr.bmiHeader.biSize = dwBmpHdr;
	m_bmphdr.bmiHeader.biSizeImage = 0;
	m_bmphdr.bmiHeader.biWidth = width;
	m_bmphdr.bmiHeader.biHeight = - height; //from bottom to top
	m_bmphdr.bmiHeader.biXPelsPerMeter = 0;
	m_bmphdr.bmiHeader.biYPelsPerMeter = 0;
	m_bmphdr.bmiHeader.biClrUsed = 0;
	m_bmphdr.bmiHeader.biPlanes = 1;
	m_bmphdr.bmiHeader.biCompression = BI_RGB;

	int iRet = StretchDIBits(hdc,
		0, 0,
		screen_w , screen_h,
		0, 0,
		width, height,
		rgb_buffer,
		&m_bmphdr,
		DIB_RGB_COLORS,
		SRCCOPY);
#ifdef _DEBUG
	if (iRet == GDI_ERROR)
	{
		OutputDebugStringA("#");
	}
	else if ( 0 == iRet)
	{
		OutputDebugStringA("@");
	}
#endif		
}

void WinRender::mix_audio( uint8_t * dst, const uint8_t * src, uint8_t len, int volume /* [0 - 100]*/ )
{
	int sdl_volume = SDL_MIX_MAXVOLUME * volume / 100;
	SDL_MixAudioFormat(dst, src, AUDIO_S16SYS, len, sdl_volume);
}

int WinRender::open_audio(AudioDecoder* decoder, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
	SDL_AudioSpec wanted_spec, spec;
	const char *env;
	static const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };
	static const int next_sample_rates[] = { 0, 44100, 48000, 96000, 192000 };
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
	wanted_spec.userdata = (void*)decoder;
	while (!(this->audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
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
	audio_hw_params->channels = spec.channels;
	audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
	audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
	if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
		av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
		return -1;
	}
	return spec.size;
}

ThreadRetType WinRender::thread_main()
{
	int sdl_flags =  SDL_INIT_AUDIO | SDL_INIT_TIMER;
	if (SDL_Init(sdl_flags)) {
		LOG_ERROR("Failed to initialize SDL.\n");

		// todo: we should let 'Render' know this.
		return 1;
	}

	sql_event_loop();

	LOG_DEBUG("SDL thread quit.\n");
	SDL_Quit();
	return 0;
}

void WinRender::sql_event_loop()
{
	SDL_Event event;
	int refresh_count = 0;

	for (;;) {
		refresh_loop_wait_event(associated_decoder , &event, refresh_count);

		switch (event.type) 
		{
		case SDL_QUIT:
		case FF_QUIT_EVENT:
			return;

		case RESET_REFRESH_COUNT_EVENT:
			refresh_count = 0;
			break;

		default:
			break;
		}
	}
}

void WinRender::refresh_loop_wait_event(SimpleAVDecoder * av_decoder, /*SDL_Event*/ void *e, int& refresh_count)
{
	SDL_Event *event = (SDL_Event *)e;

	double remaining_time = 0.0;
	SDL_PumpEvents();
	while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
		if (remaining_time > 0.0)
			av_usleep((unsigned int)(remaining_time * 1000000.0));
		
		remaining_time = REFRESH_RATE;
		if (!av_decoder->is_paused() || av_decoder->is_drawing_needed())
		{ 
			av_decoder->video_refresh(&remaining_time);

			if (!_event_cb)
			{
				continue;
			}

			refresh_count++;

			if ( 20 == refresh_count )
			{
				refresh_count = 0;
				double ts = this->associated_decoder->get_master_clock();
				if (isnan(ts))
				{
					_event_cb->on_progress(0);
				}
				else
				{
					_event_cb->on_progress((int)ts);
				}
			}

		}

		SDL_PumpEvents();
	}
}

void WinRender::quit_main_loop()
{
	SDL_Event event;
	event.type = FF_QUIT_EVENT;
	event.user.data1 = this;
	SDL_PushEvent(&event);
}
