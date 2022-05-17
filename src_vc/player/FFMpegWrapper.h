#pragma once

#include "utils/utils.h"

#include "BaseDecoder.h"

class VideoState;
class DecoderFFMpegWrapper
	: public BaseDecoder
	, public BaseThread
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


	virtual int  Faster();	 //����һ��	
	virtual int  Slower();	 //����һ��	
	virtual int  GetSpeed(int* speed);	// [-4, +4]
	int _speed;

	virtual int  FrameBack(void);    //��֡���	
	virtual int  FrameForward(void);  //��֡��ǰ

	virtual int  OpenSound();
	virtual int  CloseSound();
		
	virtual int  GetVolume(unsigned short* vol);  // ��������� range: 0 - VOLUME_MAX	
	virtual int  SetVolume(unsigned short  vol);  // �趨������ range: 0 - VOLUME_MAX

	virtual int GetPictureSize(int* width, int* height);      // ���ͼ��ߴ�
	int _width;
	int _height;

	
	/// }}} BaseDecoder section
	void handle_eof(DWORD nPort);

	VideoState* vs;
	DecoderEventCB* _event_cb;

};
