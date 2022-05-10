#pragma once
#include "common_def.h"

// usually comes from worker thread (other than UI thread)
class DecoderEventCB
{
public:	
	virtual void on_progress(int  seconds) = 0; 
	virtual void on_picture_size_got(int w, int h) = 0;
	virtual void on_eof() = 0;

	virtual void on_custom_draw(HDC hDc)		// 显示窗口叠加自绘，通常用于画水印
	{
	}

	virtual int is_custom_draw_present()		// 是否需要自绘
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
	virtual int  Init(DecoderEventCB* event_cb) = 0;	//初始化
	virtual void Release(void) = 0;	    //释放

	virtual int  Open(const char* fileName) = 0;	 //打开文件
	virtual void Close(void) = 0;              //关闭文件

	virtual int  Play(HWND  screen) = 0;	 //播放
	virtual int  Pause() = 0;      //暂停
	virtual int  Resume() = 0;     //恢复
	virtual int  Stop() = 0;	   //停止

	virtual int  Faster()	 //加速一档
	{
		return DEC_NOT_SUPPORTED;
	}
	virtual int  Slower()	 //降速一档
	{
		return DEC_NOT_SUPPORTED;
	}
	virtual int  GetSpeed(int * speed)	// [-SPEED_MAX, +SPEED_MAX]
	{
		return DEC_NOT_SUPPORTED;
	}
	
		
	//virtual int SetPlayPos(int  percentage);        //设置文件当前播放位置（百分比）
	//virtual int GetPlayPos(int *percentage);        //获取文件当前播放位置（百分比）

	virtual int GetPlayedTime(int* time_point)					//获取文件当前播放位置（秒）
	{
		return DEC_NOT_SUPPORTED;
	}
	virtual int SetPlayedTime(int  time_point)					//设置文件当前播放位置（秒）
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int GetFileTotalTime(int* seconds)				//获取文件总时长（秒）
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int GetPictureSize(int* width, int* heighte)      // 获得图像尺寸
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int CapturePic(const char* strPicName)       //抓图
	{
		return DEC_NOT_SUPPORTED;
	}
	
	virtual int  OpenSound() {
		return DEC_NOT_SUPPORTED;
	}
	virtual int  CloseSound() {
		return DEC_NOT_SUPPORTED;
	}

	// 获得音量。 range: 0 - MAX_VOLUME
	virtual int  GetVolume(unsigned short * vol) {
		return DEC_NOT_SUPPORTED;
	}

	// 设定音量。   range: 0 - MAX_VOLUME
	virtual int  SetVolume(unsigned short   vol) {
		return DEC_NOT_SUPPORTED;
	}

	virtual int  FrameBack(void)    //单帧向后
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int  FrameForward(void) 		 //单帧向前
	{
		return DEC_NOT_SUPPORTED;
	}

	//virtual void poll_while_playing();  //播放过程中，外界需要定期调用此函数

protected:
	
};

BaseDecoder* create_simple(DecoderEventCB* cb); 

