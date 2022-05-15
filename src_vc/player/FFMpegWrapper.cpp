#include "stdafx.h"

#include "FFMpegWrapper.h"
#include "ffdecoder/ffdecoder.h"

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

	this->vs = new VideoState();
	if (!vs)
	{
		LOG_ERROR( "Failed to create VideoState.\n");
		return 1;
	}

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

int  DecoderFFMpegWrapper::Open(const char* fileName)	
{
	_width = 0;
	_height = 0;

	_speed = 0;
	LOG_ERROR("Not implemented\n");
	return 1;
}

void DecoderFFMpegWrapper::Close(void)
{
	
	_speed = 0;
	_width = 0;
	_height = 0;

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
	LOG_ERROR("Not implemented\n");
	return 1;
}

int  DecoderFFMpegWrapper::Pause()  
{
	LOG_ERROR("Not implemented\n");
	return 1;
}
int  DecoderFFMpegWrapper::Resume()   
{
	return 1;
}
int  DecoderFFMpegWrapper::Stop()	
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

int  DecoderFFMpegWrapper::Faster()	 //����һ��	
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

int  DecoderFFMpegWrapper::Slower()	 //����һ��	
{
	LOG_ERROR("Not implemented\n");

	return 1;
}

int  DecoderFFMpegWrapper::GetSpeed(int* speed)	// [-4, +4]
{
	*speed = _speed;
	return 0;
}

int  DecoderFFMpegWrapper::FrameForward(void)  //��֡��ǰ
{
	LOG_ERROR("Not implemented\n");
	_speed = 0;
	return 0;
}


int  DecoderFFMpegWrapper::FrameBack(void)    //��֡���	
{
	LOG_ERROR("Not implemented\n");
	_speed = 0;
	return 0;
	
}

int DecoderFFMpegWrapper::GetPlayedTime(int* time_point)		//��ȡ�ļ���ǰ����λ�ã��룩
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

int DecoderFFMpegWrapper::SetPlayedTime(int  time_point)		//�����ļ���ǰ����λ�ã��룩
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

int DecoderFFMpegWrapper::GetFileTotalTime(int* seconds)			//��ȡ�ļ���ʱ�����룩
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

int DecoderFFMpegWrapper::GetPictureSize(int* width, int* height)      // ���ͼ��ߴ�
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
	LOG_ERROR("Not implemented\n");
	return 1;

}

int  DecoderFFMpegWrapper::CloseSound()
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

int DecoderFFMpegWrapper::GetVolume(unsigned short* vol)
{
	LOG_ERROR("Not implemented\n");
	return 1;
	
}

int DecoderFFMpegWrapper::SetVolume(unsigned short  vol)
{
	LOG_ERROR("Not implemented\n");
	return 1;
}

