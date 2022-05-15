#pragma once

#include "utils/utils.h"

#include "BasePlayer.h"
#include "BaseDecoder.h"

class SingleFilePlayer
	: public BasePlayer
	, public DecoderEventCB
{
public:
	SingleFilePlayer()
	{
		_state = PS_NOFILE;
		_driver = NULL;
		_total_length = 0;

		_sounding_ext = 1;
		_sounding_internal = ISS_NA;

		_canvas = NULL;
	}

	// 返回 PLAY_STATE 型
	virtual int get_state()
	{
		return _state;
	}

	virtual ~SingleFilePlayer();

	virtual int open_file(const char * media_file);
	virtual int close_file();

	virtual int play(HWND screen);
	virtual int pause();
	virtual int stop();

	virtual int get_played_time(int* time_point);	//获取文件当前播放位置（秒）	
	virtual int set_played_time(int time_point);	 //设置文件当前播放位置（秒）	
	virtual int get_file_total_time(int* seconds);	//获取文件总时长（秒）

	virtual int  faster();	 //加速一档
	virtual int  slower();	 //减速一档
	virtual int  get_speed(int* speed); // [-4, +4]

	virtual int step_forward();
	virtual int step_back();

	virtual int  get_volume(unsigned short* vol);  // 获得音量。 range: 0 - VOLUME_MAX	
	virtual int  set_volume(unsigned short  vol);  // 设定音量。 range: 0 - VOLUME_MAX

	virtual int get_picture_size(int* width, int* heighte);      // 获得图像尺寸

	virtual void on_progress(int  seconds);  // overides DecoderEventCB
	virtual void on_eof(); // overides DecoderEventCB
	virtual void on_picture_size_got(int w, int h) ; //overides DecoderEventCB


	virtual void on_custom_draw(HDC hDc); 		//overides DecoderEventCB	
	virtual int is_custom_draw_present();		//overides DecoderEventCB	


	
	typedef enum {
		ISS_NA = 0
		, ISS_PLAYING_SOUND
		, ISS_MUTED
	}ISS;

	virtual int is_playing_sound(int* sounding)
	{
		*sounding = _sounding_ext;
		return 0;
	}

	virtual int play_sound();	
	virtual int mute();
	
protected:
	int _state;

	int is_loaded() const;
	
	CAtlStringA _media_file;
	int _total_length;
	int _sounding_ext;  // 对外两态 
	int _sounding_internal; // 内部三态 
	int mute_internal();

	HWND _canvas;

	BaseDecoder* _driver;

	virtual void switch_state(int new_state);

};
