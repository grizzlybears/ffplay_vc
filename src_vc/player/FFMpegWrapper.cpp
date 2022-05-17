#include "stdafx.h"

#include "FFMpegWrapper.h"
#include "ffdecoder/ffdecoder.h"
#include "win_render.h"

#if defined(_WIN32) && defined(_DEBUG) 
#define new DEBUG_NEW
#endif


BaseDecoder* create_ffmpeg_wrapper(DecoderEventCB* cb)
{
	static DecoderFFMpegWrapper ff;

	ff.set_event_cb(cb);
	return &ff;
}



DecoderFFMpegWrapper::~DecoderFFMpegWrapper()
{
}


int  DecoderFFMpegWrapper::Init(DecoderEventCB* event_cb)
{
	if (_inited)
	{
		return 0;
	}

	VideoState* vs = new VideoState();
	if (!vs)
	{
		LOG_ERROR( "Failed to create VideoState.\n");
		return 1;
	}
	AutoReleasePtr<VideoState> guard1;

	RenderBase* render = new WinRender(&vs->av_decoder, event_cb);
	if (!render)
	{
		LOG_ERROR("Failed to create WinRender.\n");
		return 2;
	}
	AutoReleasePtr<RenderBase> guard2;
	
	if (render->init(0, 0))
	{
		LOG_ERROR("Failed to init WinRender.\n");
		return 3;
	}

	vs->av_decoder.render = render;
	// vs->av_decoder.set_master_sync_type(AV_SYNC_AUDIO_MASTER);  wait for our audio implementation
	vs->av_decoder.set_master_sync_type(AV_SYNC_VIDEO_MASTER);

	guard2.dismiss();
	guard1.dismiss();
	this->vs = vs;
	_inited = 1;
	return 0;
}


void DecoderFFMpegWrapper::Release(void)	 
{
	if (!_inited)
	{
		return;
	}

	delete this->vs;
	_inited = 0;
}

#define CHECK_IF_INITED(r) \
	if (!_inited) \
	{ \
		LOG_ERROR("Not initialzied.\n"); \
		return r; \
	}

#define CHECK_IF_MEDIA_PRESENT(r) \
	if (!_inited) \
	{ \
		LOG_ERROR("Not initialzied.\n"); \
		return r; \
	} \
	if (!vs->format_context) \
	{ \
		LOG_ERROR("No media present.\n"); \
		return r; \
	}




int  DecoderFFMpegWrapper::Open(const char* fileName)	
{
	CHECK_IF_INITED(1);

	_width = 0;
	_height = 0;

	_speed = 0;

	// open media
	if (vs->open_input_stream(fileName, NULL, 1)) 
	{
		LOG_ERROR( "Failed to open '%s'.\n", fileName);
		return 1;
	}
	return 0;
}

void DecoderFFMpegWrapper::Close(void)
{
	CHECK_IF_INITED();
	_speed = 0;
	_width = 0;
	_height = 0;

	vs->close_input_stream();
}


void DecoderFFMpegWrapper::handle_eof(DWORD nPort)
{
	if (this->_event_cb)
	{
		_event_cb->on_eof();
	}
}


int  DecoderFFMpegWrapper::Play(HWND  screen)
{
	CHECK_IF_MEDIA_PRESENT(1);
	WinRender* render = (WinRender*)vs->av_decoder.render;
	render-> attach_to_window(screen);

	vs->toggle_pause();
	return 0;
}

int  DecoderFFMpegWrapper::Pause()  
{
	CHECK_IF_MEDIA_PRESENT(1);
	vs->toggle_pause();
	return 0;
}
int  DecoderFFMpegWrapper::Resume()   
{
	CHECK_IF_MEDIA_PRESENT(1);
	vs->toggle_pause();
	return 0;
}
int  DecoderFFMpegWrapper::Stop()	
{
	CHECK_IF_MEDIA_PRESENT(1);
	if (!vs->av_decoder.is_paused())
	{
		vs->toggle_pause();
	}
	vs->stream_seek(0, 0, 0);

	WinRender* render = (WinRender*)vs->av_decoder.render;
	render->dettach_from_window();
	
	return 0;
}

int  DecoderFFMpegWrapper::Faster()	 //加速一档	
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

int  DecoderFFMpegWrapper::Slower()	 //降速一档	
{
	LOG_ERROR("Not implemented\n");

	return 1;
}

int  DecoderFFMpegWrapper::GetSpeed(int* speed)	// [-4, +4]
{
	*speed = _speed;
	return 0;
}

int  DecoderFFMpegWrapper::FrameForward(void)  //单帧向前
{
	LOG_ERROR("Not implemented\n");
	_speed = 0;
	return 0;
}


int  DecoderFFMpegWrapper::FrameBack(void)    //单帧向后	
{
	LOG_ERROR("Not implemented\n");
	_speed = 0;
	return 0;
	
}

int DecoderFFMpegWrapper::GetPlayedTime(int* time_point)		//获取文件当前播放位置（秒）
{
	CHECK_IF_MEDIA_PRESENT(1);

	double ts = vs->av_decoder.get_master_clock();
	if (isnan(ts))
	{
		*time_point = 0;
	}
	else
	{ 
		*time_point = (int)ts;
	}
	return 0;
}

int DecoderFFMpegWrapper::SetPlayedTime(int  time_point)		//设置文件当前播放位置（秒）
{
	CHECK_IF_MEDIA_PRESENT(1);

	double pos = vs->av_decoder.get_master_clock();
	if (isnan(pos))
		pos = (double)0;

	int64_t  cur = (int64_t)(pos * AV_TIME_BASE);

	int64_t delta = (int64_t)time_point * AV_TIME_BASE - cur;

	vs->stream_seek(cur, delta, 0);
	
	
	return 0;
}

int DecoderFFMpegWrapper::GetFileTotalTime(int* seconds)			//获取文件总时长（秒）
{
	CHECK_IF_MEDIA_PRESENT(1);

	int64_t duration = vs->format_context->duration / AV_TIME_BASE;
	*seconds = (int)duration;
	
	return 0;
}

int DecoderFFMpegWrapper::GetPictureSize(int* width, int* height)      // 获得图像尺寸
{
	if (!_width)
	{
		return 1;
	}
	
	*width  = _width;
	*height = _height;
	return 0;
}

int  DecoderFFMpegWrapper::OpenSound()
{
	return 0;
}

int  DecoderFFMpegWrapper::CloseSound()
{
	return 0;
}

int DecoderFFMpegWrapper::GetVolume(unsigned short* vol)
{
	*vol = 100;
	return 0;
}

int DecoderFFMpegWrapper::SetVolume(unsigned short  vol)
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

