#include "stdafx.h"
#include <SDL.h>

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
    sws_ctx_for_rgb = NULL;
	need_pic_size = 0;
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
}

void WinRender::close_audio()
{
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
    
    
    if (this-> sws_ctx_for_rgb)
    {
        sws_freeContext(this->sws_ctx_for_rgb);
        this-> sws_ctx_for_rgb = NULL;
    }
	
}



int save_frame_to_rgb24(AVFrame* frame, struct SwsContext** sws_ctx)
{
    static int frame_num = 0;
    if (! ( 0 == (frame_num % 10) && frame_num < 100 ) )
    {
        frame_num ++;
        return 0;
    }
    frame_num ++;
    LOG_DEBUG("save frame #%02d.\n", frame_num );

    *sws_ctx = sws_getCachedContext(*sws_ctx ,
            frame->width, frame->height, (enum AVPixelFormat)frame->format
            , frame->width, frame->height , AV_PIX_FMT_RGB24
            , SWS_BICUBIC , NULL, NULL, NULL);
    if (! *sws_ctx)
    {
        LOG_ERROR("Cannot initialize the conversion context\n");
        return 1;
    }

    AVFrame *pFrameRGB = av_frame_alloc(); 
    if (!pFrameRGB )
    {
        LOG_ERROR("Alloc frame failed!\n");
        return -1;
    } 
    
    int rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame->width,  frame->height, 1);
    uint8_t * rgb_buffer = (uint8_t *)av_malloc(rgb_buffer_size );

    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize
            , rgb_buffer, AV_PIX_FMT_RGB24
            , frame->width, frame->height , 1);

    sws_scale(*sws_ctx
            , (const unsigned char* const*) frame->data, frame->linesize
            , 0, frame->height
            , pFrameRGB->data, pFrameRGB->linesize);

    AString filename("frame.%02d.ppm" , frame_num );
    save_rgb_frame_to_file( filename, pFrameRGB , frame->width,  frame->height);

    av_frame_free(&pFrameRGB);
    av_free(rgb_buffer);

    return 0;
}

void save_rgb_frame_to_file(const char* filename, AVFrame *pFrame, int width, int height)
{
    FILE *pFile = NULL;
    int y = 0;
    
    pFile = fopen( filename, "wb");
    if (NULL == pFile)
    {
        LOG_ERROR("failed to open %s for wr.\n" ,  filename);
        return;
    }
    
    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);
    
    // Write pixel data
    for (y = 0; y < height; y++)
        fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);
    
    fclose(pFile);
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

	sws_ctx_for_rgb = sws_getCachedContext(sws_ctx_for_rgb,
		frame->width, frame->height, (enum AVPixelFormat)frame->format
		, frame->width, frame->height, AV_PIX_FMT_RGB24
		, SWS_BICUBIC, NULL, NULL, NULL);
	if (!sws_ctx_for_rgb)
	{
		LOG_ERROR("Cannot initialize the conversion context\n");
		return ;
	}

	// todo:  pFrameRGB & rgb_buffer could be reused, as long as playing same video (same pic size).
	AVFrame *pFrameRGB = av_frame_alloc();
	if (!pFrameRGB)
	{
		LOG_ERROR("Alloc frame failed!\n");
		return;
	}

	int rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame->width, frame->height, 1);
	uint8_t * rgb_buffer = (uint8_t *)av_malloc(rgb_buffer_size);

	av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize
		, rgb_buffer, AV_PIX_FMT_RGB24
		, frame->width, frame->height, 1);

	sws_scale(sws_ctx_for_rgb
		, (const unsigned char* const*)frame->data, frame->linesize
		, 0, frame->height
		, pFrameRGB->data, pFrameRGB->linesize);

	draw_frame_gdi(rgb_buffer, frame->width, frame->height, canvas);

	// AString filename("frame.%02d.ppm", frame_num);
	// save_rgb_frame_to_file(filename, pFrameRGB, frame->width, frame->height);

	av_frame_free(&pFrameRGB);
	av_free(rgb_buffer);

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
}

int WinRender::open_audio(AudioDecoder* decoder, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
	return 2048;
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

	LOG_DEBUG("SQL thread quit.\n");
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
