#pragma  once

#include <atlbase.h>
#include <atlwin.h>

#include "ffdecoder/ffdecoder.h"

class WinRender: public RenderBase
{
public:
   
	WinRender();
    virtual ~WinRender()
    {
        safe_release();
    }

    virtual int init(int audio_disable, int alwaysontop);

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
    virtual void upload_and_draw_frame(Frame* video_frame);

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


	// todo: we may need to subclass the windows, handle WM_PAINT/WM_SIZE ...
	void attach_to_window(HWND  hWnd); 
	void dettach_from_window();

protected:
    struct SwsContext* img_convert_ctx; 
    struct SwsContext* sws_ctx_for_rgb; 

	HWND  canvas;

};

int save_frame_to_rgb24(const AVFrame* frame, struct SwsContext** sws_ctx);
void save_rgb_frame_to_file(const char* filename, AVFrame *pFrame, int width, int height);

