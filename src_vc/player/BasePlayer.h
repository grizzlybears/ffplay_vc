#pragma once
#include "common_def.h"

typedef enum
{
	PS_NOFILE = 0
	,PS_LOADED
	,PS_PLAYING
	,PS_PAUSED
	,PS_STEPPING  // 有的SDK从单步恢复要调 play，从暂停恢复调 resume，需要分一下。
	,PS_INVALID
} PLAY_STATE;

const char* decode_play_state(int state);

class BasePlayer
{
public:
	
	virtual ~BasePlayer()
	{
	}

	// 返回 PLAY_STATE 型
	virtual int get_state() = 0;

	virtual int open_file(const char * media_file) = 0;
	virtual int close_file() = 0;

	virtual int play(HWND screen) = 0;
	virtual int pause() = 0;
	virtual int stop() = 0;
	
	virtual int get_played_time(int* seconds)	//获取文件当前播放位置（秒）
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int set_played_time(int seconds)	 //设置文件当前播放位置（秒）
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int get_file_total_time(int* seconds)	//获取文件总时长（秒）
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int step_forward()
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int step_back()
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int is_playing_sound(int * sounding )
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int play_sound()
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int mute()
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int  get_volume(unsigned short* vol)  // 获得音量。 range: 0 - VOLUME_MAX
	{
		return DEC_NOT_SUPPORTED;
	}
	virtual int  set_volume(unsigned short  vol)  // 设定音量。   range: 0 - VOLUME_MAX
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual int get_picture_size(int* width, int* heighte)      // 获得图像尺寸
	{
		return DEC_NOT_SUPPORTED;
	}

	virtual void on_state_changed( int old_state, int new_state );

	

protected:
	virtual void switch_state(int new_state) = 0;
};
