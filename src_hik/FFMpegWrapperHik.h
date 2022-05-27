#pragma once

#include "utils/utils.h"

#include "player/BaseDecoder.h"
#include "ffdecoder/ffdecoder.h"

#include "HCNetSDK.h" 

class DecoderFFMpegWrapper
	: public BaseDecoder
	, public BaseThread
	, public ParserCB
{
public:

	DecoderFFMpegWrapper()
	{
		// don't 'LOG_XXX' in ctor. We may statically create instance. call 'Init' instead.
		_event_cb = NULL;
		_inited = 0;
		_speed = 0;

		_width = 0;
		_height = 0;

		vs = NULL;
	}
	virtual ~DecoderFFMpegWrapper();

	void set_event_cb(DecoderEventCB* cb)
	{
		_event_cb = cb;
	}
	
	
	//
	// BaseDecoder section {{{
	virtual int  Init(DecoderEventCB* event_cb);	
	virtual void Release(void);	   
	int _inited;

	virtual int  Open(const char* fileName);	
	virtual void Close(void);             

	virtual int  Play(HWND  screen);
	virtual int  Pause();     
	virtual int  Resume();    
	virtual int  Stop();	  

	virtual int GetPlayedTime(int* time_point);		// unit: second 
	virtual int SetPlayedTime(int  time_point);		//  unit: second 
	virtual int GetFileTotalTime(int* seconds);	


	virtual int  Faster();	 //加速一档	
	virtual int  Slower();	 //降速一档	
	virtual int  GetSpeed(int* speed);	// [-4, +4]
	int _speed;

	virtual int  FrameBack(void);    //单帧向后	
	virtual int  FrameForward(void);  //单帧向前

	virtual int  OpenSound();
	virtual int  CloseSound();
		
	virtual int  GetVolume(unsigned short* vol);  // 获得音量。 range: 0 - VOLUME_MAX	
	virtual int  SetVolume(unsigned short  vol);  // 设定音量。 range: 0 - VOLUME_MAX

	virtual int GetPictureSize(int* width, int* height);      // 获得图像尺寸
	int _width;
	int _height;
	// }}} BaseDecoder section

	// {{{ ParserCB section
	virtual void on_eof(const char* file_Playing);
	// }}} ParserCB section

	VideoState* vs;
	DecoderEventCB* _event_cb;

};
