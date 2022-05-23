#include "stdafx.h"
#include "SingleFilePlayer.h"


#define MAX_TY_ATTACHMENT_SIZE (1024 * 512 )

const char* decode_play_state(int state)
{
	if (PS_NOFILE == state)
	{
		return "no file";
	}
	else if ( PS_LOADED == state)
	{
		return "loaded";
	}
	else if ( PS_PLAYING == state)
	{
		return "playing";
	}
	else if ( PS_PAUSED == state)
	{
		return "paused";
	}
	else if (PS_STEPPING == state)
	{
		return "stepping";
	}
	else if ( PS_INVALID == state)
	{
		return "invalid state";
	}
	else
	{
		return "bad state";
	}

}

void BasePlayer::on_state_changed(int old_state, int new_state)
{
	LOG_DEBUG("'%s' => '%s'\n", decode_play_state(old_state), decode_play_state(new_state));
}

SingleFilePlayer::~SingleFilePlayer()
{
}

int SingleFilePlayer::is_loaded() const
{
	if (PS_LOADED == _state
		|| PS_PLAYING == _state
		|| PS_PAUSED  == _state
		|| PS_STEPPING == _state
		)
	{
		return 1;
	}

	return 0;
}
int SingleFilePlayer::open_file(const char* media_file)
{
	LOG_DEBUG("Let's open %s.\n", media_file);

	if (is_loaded())
	{
		if (_media_file == media_file)
		{
			return 0;
		}

		close_file();
	}
	
	_driver = create_ffmpeg_wrapper(this);
	int r = _driver->Init(this);
	if (r)
	{
		return 1;
	}

	r = _driver->Open(media_file);
	if (r)
	{
		return 2;
	}

	_media_file = media_file;
	_total_length = 0;
	switch_state(PS_LOADED);
	return 0;
}

void SingleFilePlayer::switch_state(int new_state)
{
	int old_state = get_state();
	_state = new_state;
	on_state_changed( old_state, new_state);

}

int SingleFilePlayer::close_file()
{
	if (!is_loaded())
	{
		LOG_WARN(" 'close_file' in state '%s'\n", decode_play_state( get_state()) );
		return 1;
	}

	if (PS_PLAYING == _state
		|| PS_PAUSED == _state
		|| PS_STEPPING == _state
		)
	{
		stop();
	}

	// todo: 
	LOG_DEBUG("Let's close '%s'.\n", _media_file.GetString());
	_driver->Close();
	_media_file = "";

	switch_state(PS_NOFILE);
	
	_driver->Release();
	

	return 0;
}

int SingleFilePlayer::play(HWND screen)
{
	if (!is_loaded() 
		|| PS_PLAYING == get_state())
	{
		LOG_WARN(" 'play' in state '%s'\n", decode_play_state(get_state()));
		return 1;
	}
	int r;
	
	if (PS_LOADED == get_state())
	{
		r = _driver->Play(screen);
		_canvas = screen;

		if (r)
		{
			return 2;
		}

		if (_sounding_ext)
		{
			play_sound();
		}
		else
		{
			mute();
		}
	}
	else if (PS_PAUSED == get_state())
	{
		r = _driver->Resume();
		if (r)
		{
			return 2;
		}
	}
	else
	{
		r = _driver->Play(screen);
		if (r)
		{
			return 2;
		}
		_canvas = screen;
	}

	switch_state(PS_PLAYING);
	return 0;
}

int SingleFilePlayer::pause()
{
	if ( PS_PLAYING != get_state())
	{
		LOG_WARN(" 'pause' in state '%s'\n", decode_play_state(get_state()));
		return 1;
	}

	int r = _driver->Pause();
	if (r)
	{
		return 2;
	}

	switch_state(PS_PAUSED);
	return 0;
}

int SingleFilePlayer::stop()
{
	if (!is_loaded()
		|| PS_LOADED == get_state())
	{
		LOG_WARN(" 'stop' in state '%s'\n", decode_play_state(get_state()));
		return 1;
	}

	int r = _driver->Stop();
	_canvas = NULL;
	if (r)
	{
		return 2;
	}
	
	mute_internal(); 

	switch_state(PS_LOADED);
	return 0;
}


int SingleFilePlayer::get_played_time(int* seconds)	//获取文件当前播放位置（秒）	
{
	if (!is_loaded())
	{
		return 1;
	}

	return _driver->GetPlayedTime(seconds);
}

int SingleFilePlayer::set_played_time(int seconds)	 //设置文件当前播放位置（秒）	
{
	if (!is_loaded())
	{
		return 1;
	}

	return _driver->SetPlayedTime(seconds);
}

int SingleFilePlayer::get_picture_size(int* width, int* height)     // 获得图像尺寸
{
	if (!is_loaded())
	{
		return 1;
	}

	return _driver->GetPictureSize(width,  height);
}

int SingleFilePlayer::get_file_total_time(int* seconds)	//获取文件总时长（秒）
{
	if (!is_loaded())
	{
		return 1;
	}

	if (_total_length)
	{
		*seconds = _total_length;
	}

	int len;

	int r = _driver->GetFileTotalTime(&len);
	if (0 == r)
	{
		_total_length = len;
		*seconds = len;
		return 0;
	}

	return r;
}

int SingleFilePlayer::play_sound()
{
	if (_sounding_ext)
	{
		// 对外已然在播放声音

		if (!is_loaded())
		{
			_sounding_internal = ISS_NA;
			return 0;
		}

		if (ISS_PLAYING_SOUND == _sounding_internal)
		{
			// 内部也在放，无须动作
			return 0;
		}

		int r = _driver->OpenSound();
		if (!r)
		{
			_sounding_internal = ISS_PLAYING_SOUND;
		}
		else
		{
			return 1;
		}
		
		return 0;
	}
	else
	{
		// 对外处于消音状态
		_sounding_ext = 1;

		if (!is_loaded())
		{
			_sounding_internal = ISS_NA;
			return 1;
		}

		if (ISS_PLAYING_SOUND == _sounding_internal)		
		{
			LOG_WARN("internally 'playing sound' while externally not.\n");
			return 0;
		}

		
		int r = _driver->OpenSound();
		if (!r)
		{
			_sounding_internal = ISS_PLAYING_SOUND;
			return 0;
		}
		else
		{
			return 1;
		}

	}
}

int SingleFilePlayer::mute_internal()
{
	if (ISS_PLAYING_SOUND != _sounding_internal)
	{
		LOG_WARN("already internally 'muted'.\n");
		_sounding_internal = ISS_MUTED;
		return 0;
	}

	int r = _driver->CloseSound();
	if (!r)
	{
		_sounding_internal = ISS_MUTED;
		return 0;
	}
	else
	{
		return 1;
	}

}

int SingleFilePlayer::mute()
{
	if (_sounding_ext)
	{
		// 对外已然在播放声音
		_sounding_ext = 0;

		if (!is_loaded())
		{
			_sounding_internal = ISS_NA;
			return 0;
		}

		return mute_internal();		
	}
	else
	{
		// 对外处于消音状态
		if (!is_loaded())
		{
			_sounding_internal = ISS_NA;
			return 0;
		}

		
		if (ISS_PLAYING_SOUND != _sounding_internal)
		{
			_sounding_internal = ISS_MUTED;
			return 0;
		}


		LOG_WARN("internally 'playing sound' while externally not.\n");
		
		return mute_internal();
		
	}
}


int  SingleFilePlayer::faster()  //加速一档
{
	if ( PS_PLAYING != _state)
	{
		return 1;
	}

	return  _driver->Faster();
}

int  SingleFilePlayer::slower()	 //减速一档
{
	if (PS_PLAYING != _state)
	{
		return 1;
	}

	return  _driver->Slower();
}

int  SingleFilePlayer::get_speed(int* speed)
{
	if (PS_PLAYING != _state)
	{
		*speed = 0;
		return 0;
	}

	return  _driver->GetSpeed(speed);
}

int SingleFilePlayer::step_forward()
{
	if (!(PS_PLAYING == _state
		|| PS_PAUSED == _state
		|| PS_STEPPING == _state)
		)
	{
		return 1;
	}
	int r = _driver->FrameForward();
	if (r)
	{
		return 1;
	}

	if (PS_STEPPING != _state)
	{
		switch_state(PS_STEPPING);
	}
	

	return 0;
}

int SingleFilePlayer::step_back()
{
	if (!(PS_PLAYING == _state
		|| PS_PAUSED == _state
		|| PS_STEPPING == _state)
		)
	{
		return 1;
	}

	int r = _driver->FrameBack();
	if (r)
	{
		return 1;
	}

	if (PS_STEPPING != _state)
	{
		switch_state(PS_STEPPING);
	}

	return 0;
}

int  SingleFilePlayer::get_volume(unsigned short* vol)  // 获得声卡输出的主音量。 range: 0 - VOLUME_MAX	
{
	if (!_driver)
	{
		*vol = 0;
		return 1;
	}


	return _driver->GetVolume(vol);
}

int  SingleFilePlayer::set_volume(unsigned short  vol)  // 设定声卡输出的主音量，会影响到其他的声音应用。   range: 0 - VOLUME_MAX
{
	if (!_driver)
	{
		return 1;
	}

	return _driver->SetVolume(vol);

}


void SingleFilePlayer::on_progress(int  seconds)
{
	//printf("progress : %d (s).\n", seconds);
}

void SingleFilePlayer::on_eof()
{
	LOG_DEBUG("Eof.\n");
	pause();
}

void SingleFilePlayer::on_picture_size_got(int w, int h)
{
	LOG_DEBUG("pic size: %d x %d.\n", w, h);
}


int SingleFilePlayer::is_custom_draw_present()
{
	return 0;
}

void SingleFilePlayer::on_custom_draw(HDC hDc)
{
	return;

}

