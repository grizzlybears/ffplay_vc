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

	// ���� PLAY_STATE ��
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

	virtual int get_played_time(int* time_point);	//��ȡ�ļ���ǰ����λ�ã��룩	
	virtual int set_played_time(int time_point);	 //�����ļ���ǰ����λ�ã��룩	
	virtual int get_file_total_time(int* seconds);	//��ȡ�ļ���ʱ�����룩

	virtual int  faster();	 //����һ��
	virtual int  slower();	 //����һ��
	virtual int  get_speed(int* speed); // [-4, +4]

	virtual int step_forward();
	virtual int step_back();

	virtual int  get_volume(unsigned short* vol);  // ��������� range: 0 - VOLUME_MAX	
	virtual int  set_volume(unsigned short  vol);  // �趨������ range: 0 - VOLUME_MAX

	virtual int get_picture_size(int* width, int* heighte);      // ���ͼ��ߴ�

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
	int _sounding_ext;  // ������̬ 
	int _sounding_internal; // �ڲ���̬ 
	int mute_internal();

	HWND _canvas;

	BaseDecoder* _driver;

	virtual void switch_state(int new_state);

};
