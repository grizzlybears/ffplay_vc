#pragma once
#include "common_def.h"

// usually comes from worker thread (other than UI thread)
class DecoderEventCB
{
public:	
	virtual void on_progress(int  seconds) = 0; 
	virtual void on_picture_size_got(int w, int h) = 0;
	virtual void on_eof() = 0;

	virtual void on_custom_draw(HDC hDc)		// ��ʾ���ڵ����Ի棬ͨ�����ڻ�ˮӡ
	{
	}

	virtual int is_custom_draw_present()		// �Ƿ���Ҫ�Ի�
	{
		return 0;	
	}
};

class BaseDecoder
{
public:
	
	virtual ~BaseDecoder()
	{
	}
	
	// don't 'LOG_XXX' in ctor. We may statically create instance. call 'Init' instead.
	virtual int  Init(DecoderEventCB* event_cb) = 0;	//��ʼ��
	virtual void Release(void) = 0;	    //�ͷ�

	virtual int  Open(const char* fileName) = 0;	 //���ļ�
	virtual void Close(void) = 0;              //�ر��ļ�

	virtual int  Play(HWND  screen) = 0;	 //����
	virtual int  Pause() = 0;      //��ͣ
	virtual int  Resume() = 0;     //�ָ�
	virtual int  Stop() = 0;	   //ֹͣ

	virtual int  Faster()	 //����һ��
	{
		return DEC_NOT_SUPPORTED;
	}
	virtual int  Slower()	 //����һ��
	{
		return DEC_NOT_SUPPORTED;
	}
	virtual int  GetSpeed(int * speed)	// [-SPEED_MAX, +SPEED_MAX]
	{
		return DEC_NOT_SUPPORTED;
	}
	
		
	//virtual int SetPlayPos(int  percentage);        //�����ļ���ǰ����λ�ã��ٷֱȣ�
	//virtual int GetPlayPos(int *percentage);        //��ȡ�ļ���ǰ����λ�ã��ٷֱȣ�

	virtual int GetPlayedTime(int* time_point)					//��ȡ�ļ���ǰ����λ�ã��룩
	{
		return DEC_NOT_SUPPORTED;
	}
	virtual int SetPlayedTime(int  time_point)					//�����ļ���ǰ����λ�ã��룩
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int GetFileTotalTime(int* seconds)				//��ȡ�ļ���ʱ�����룩
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int GetPictureSize(int* width, int* heighte)      // ���ͼ��ߴ�
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int CapturePic(const char* strPicName)       //ץͼ
	{
		return DEC_NOT_SUPPORTED;
	}
	
	virtual int  OpenSound() {
		return DEC_NOT_SUPPORTED;
	}
	virtual int  CloseSound() {
		return DEC_NOT_SUPPORTED;
	}

	// ��������� range: 0 - MAX_VOLUME
	virtual int  GetVolume(unsigned short * vol) {
		return DEC_NOT_SUPPORTED;
	}

	// �趨������   range: 0 - MAX_VOLUME
	virtual int  SetVolume(unsigned short   vol) {
		return DEC_NOT_SUPPORTED;
	}

	virtual int  FrameBack(void)    //��֡���
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int  FrameForward(void) 		 //��֡��ǰ
	{
		return DEC_NOT_SUPPORTED;
	}

	//virtual void poll_while_playing();  //���Ź����У������Ҫ���ڵ��ô˺���

protected:
	
};

BaseDecoder* create_simple(DecoderEventCB* cb); 

