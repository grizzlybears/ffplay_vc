﻿#pragma  once

#include <atlbase.h>
#include <atlwin.h>

#include "ffdecoder/ffdecoder.h"
#include "BaseDecoder.h"
#include <SDL.h>

class WinRender : public RenderBase
	, public BaseThread
{
public:
	WinRender(SimpleAVDecoder* decoder, DecoderEventCB* e);
    virtual ~WinRender()
    {
        safe_release();
    }

	// {{  RenderBase section

    virtual int init(int audio_disable, int alwaysontop);
	SimpleAVDecoder* associated_decoder;  //just ref, doesn't take ownership
	DecoderEventCB* _event_cb;            //just ref, doesn't take ownership

	virtual void toggle_full_screen()
	{
	}

	virtual void safe_release();
	    
    virtual int open_audio( AudioDecoder* decoder, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams* audio_hw_params);
    virtual void mix_audio( uint8_t * dst, const uint8_t * src, uint8_t len, int volume /* [0 - 100]*/ ) ;
    virtual void pause_audio(int pause_on );
    virtual void close_audio();

	virtual void clear_render()
	{
	}
	virtual void draw_render()
	{
	}
    virtual void upload_and_draw_frame(Frame* video_frame);// called from 'render thread'

	virtual int create_window(const char* title, int x, int y, int w, int h, uint32_t flags)
	{
		return 0;
	}
	
	virtual void show_window(int fullscreen)
	{
	}
    virtual void set_default_window_size(int width, int height, AVRational sar)
	{
	}
	// }}  RenderBase section
	
	// {{ SDL section
	virtual  ThreadRetType thread_main();  //  we are 'BaseThread'
	void sql_event_loop(); 
	void refresh_loop_wait_event(SimpleAVDecoder * av_decoder, /*SDL_Event*/ void *event, int& refresh_count);
	void quit_main_loop();
	// }} SDL section

	// todo: we may need to subclass the windows, handle WM_PAINT/WM_SIZE ...
	void attach_to_window(HWND  hWnd); 
	void dettach_from_window();
	void draw_frame_gdi(uint8_t * rgb_buffer, int width, int height, HWND  hWnd); // called from 'render thread'

protected:
    struct SwsContext* img_convert_ctx; 
	AVFrame *pFrameRGB;
	uint8_t * rgb_buffer;

	HWND  canvas;
	int   need_pic_size;

	SDL_AudioDeviceID audio_dev;

};

