#include "stdafx.h"

#include "FFMpegWrapperHik.h"

#include "player/win_render.h"

#define HIK_NVR_ADDR "192.168.2.106" 
#define HIK_NVR_PORT 8000
#define HIK_NVR_UID  "admin"
#define HIK_NVR_PASS "12345"
#define HIK_NVR_CHAN_TO_PLAY 34

//#define TRACE_FRAMES (1)

#if defined(_WIN32) && defined(_DEBUG) 
#define new DEBUG_NEW
#endif

#define LOG_HIK_ERROR( msg_prefix ) \
{ \
		CAtlStringA error_msg; \
        LONG code = NET_DVR_GetLastError();\
        char * err_str = NET_DVR_GetErrorMsg(&code);\
        error_msg.Format("%s code = %d, %s",msg_prefix, code,err_str); \
        LOG_ERROR("%s\n",error_msg.GetString() ); \
}

static void CALLBACK PlayESCallBack(LONG lPreviewHandle, NET_DVR_PACKET_INFO_EX* pstruPackInfo, void* pUser);

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
	BOOL b = NET_DVR_Init();
	if (!b)
	{
		LOG_ERROR("Failed to init Hik.\n");
		return 1;
	}

	

	RenderBase* render = new WinRender(&av_decoder, event_cb);
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

	av_decoder.render = render;
	av_decoder.set_master_sync_type(AV_SYNC_AUDIO_MASTER); 
	//vs->av_decoder.set_master_sync_type(AV_SYNC_VIDEO_MASTER);

	guard2.dismiss();
	
	_inited = 1;
	return 0;
}


void DecoderFFMpegWrapper::Release(void)	 
{
	if (!_inited)
	{
		return;
	}

	av_decoder.render->safe_release();
	NET_DVR_Cleanup();

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
	if (play_handle <0 ) \
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

	// connect to Hik NVR
	NET_DVR_DEVICEINFO_V30 devInfo;
	login_ssesion = NET_DVR_Login_V30((char*)HIK_NVR_ADDR, HIK_NVR_PORT
		, (char*)HIK_NVR_UID, (char*)HIK_NVR_PASS, &devInfo);
	if (login_ssesion < 0)
	{
		LOG_HIK_ERROR("NET_DVR_Login_V40 failed,");
		return -1;
	}

	

	return 0;
}

void DecoderFFMpegWrapper::Close(void)
{
	CHECK_IF_INITED();
	_speed = 0;
	_width = 0;
	_height = 0;

	if (login_ssesion > 0)
	{
		NET_DVR_Logout_V30(login_ssesion);
		login_ssesion = -1;
	}
	
	av_decoder.close_all_stream();
}


void DecoderFFMpegWrapper::on_eof(const char* file_Playing)
{
	if (this->_event_cb)
	{
		_event_cb->on_eof();
	}
}

int  DecoderFFMpegWrapper::Play(HWND  screen)
{
	CHECK_IF_INITED(1);

	WinRender* render = (WinRender*)av_decoder.render;
	BOOL b;

	NET_DVR_PREVIEWINFO preview_info = { 0 };
	preview_info.lChannel = HIK_NVR_CHAN_TO_PLAY;
	preview_info.dwStreamType = 1; //0-主码流，1-子码流，2-三码流，3-虚拟码流，以此类推 
	preview_info.dwLinkMode = 0; //连接方式：0- TCP方式，1- UDP方式，2- 多播方式，3- RTP方式，4- RTP/RTSP，5- RTP/HTTP，6- HRUDP（可靠传输） ，7- RTSP/HTTPS，8- NPQ 
	preview_info.hPlayWnd = NULL;
	preview_info.bBlocked = 1;
	preview_info.byProtoType = 0; //应用层取流协议：0- 私有协议，1- RTSP协议。

	
	//start 'real play' 
	play_handle = NET_DVR_RealPlay_V40(login_ssesion, &preview_info, NULL, NULL);

	if (play_handle < 0)
	{
		LOG_HIK_ERROR("NET_DVR_RealPlay_V40 failed, ");
		goto FAILED;
	}

	b = NET_DVR_SetESRealPlayCallBack(play_handle, PlayESCallBack, this);
	if (!b)
	{
		LOG_HIK_ERROR("NET_DVR_SetESRealPlayCallBack failed, ");
		goto FAILED;
	}

	{
		AVCodecParameters codec_para;
		memset((void*)&codec_para, 0, sizeof codec_para);
		codec_para.codec_id = AV_CODEC_ID_H264;  // yes, must be figured out manually.
		codec_para.codec_type = AVMEDIA_TYPE_VIDEO;

		StreamParam  extra_para = { 0 };
		extra_para.start_time = av_gettime_relative(); // we are live-casting
		extra_para.time_base = av_make_q(1, AV_TIME_BASE);
		extra_para.guessed_vframe_rate = av_make_q(1, 25);

		Decoder * decoder = &av_decoder.viddec;

		AVCodecContext* codec_context = Decoder::create_codec_directly(&codec_para, &extra_para);
		if (!codec_context)
		{
			goto FAILED;
		}

		if (decoder->decoder_init(codec_context, &extra_para))
		{
			goto FAILED;
		}

	}
	
	
	render-> attach_to_window(screen);

	if (av_decoder.is_paused())
	{
		av_decoder.internal_toggle_pause();
	}

	return 0;

FAILED:
	LOG_ERROR("Oops, some thing not right.\n");

	if (play_handle >= 0)
	{
		NET_DVR_StopRealPlay(play_handle);
		play_handle = -1;
	}

	return 1;
}
void CALLBACK PlayESCallBack(LONG lPreviewHandle, NET_DVR_PACKET_INFO_EX* pstruPackInfo, void* pUser)
{
	DecoderFFMpegWrapper* ff = (DecoderFFMpegWrapper*)pUser;
	ff->handle_hik_ES_cb(lPreviewHandle, pstruPackInfo);
}

void DecoderFFMpegWrapper::handle_hik_ES_cb(LONG lPreviewHandle, NET_DVR_PACKET_INFO_EX* pstruPackInfo)
{
	AVPacketExtra extra;

	const char * s;
	switch (pstruPackInfo->dwPacketType)
	{
	case 0: //"Header";
	case 11: //"Private"
		return;

	case 10: // "Audio"
		extra.v_or_a = PSI_AUDIO;
		s = "A";
		break;
	case 1: //"I";
		s = "I";
		extra.v_or_a = PSI_VIDEO;
		break;
	case 2: // "B";
		s = "B";
		extra.v_or_a = PSI_VIDEO;
		break;
	case 3: // "P";
		s = "P";
		extra.v_or_a = PSI_VIDEO;
		break;

	default:
		LOG_WARN("Warn! Unknown packet type %d\n", pstruPackInfo->dwPacketType);
		return;
	}


#if	TRACE_FRAMES
	static int _dot_count = 0;
	OutputDebugStringA(s);
	_dot_count++;

	if (100 == _dot_count)
	{
		OutputDebugStringA("\n");
		_dot_count = 0;
	}
#endif
	unsigned char * pkt_data = (unsigned char*)av_malloc(pstruPackInfo->dwPacketSize + AV_INPUT_BUFFER_PADDING_SIZE);
	if (!pkt_data)
	{
		LOG_ERROR("not enough memory for new AvPacket\n");
		return;
	}

	memcpy(pkt_data, pstruPackInfo->pPacketBuffer, pstruPackInfo->dwPacketSize);


	AVPacket packet;
	av_init_packet(&packet);

	int r = av_packet_from_data(&packet, pkt_data, pstruPackInfo->dwPacketSize);
	if (r)
	{
		LOG_WARN("failed in av_packet_from_data \n");
		return ;
	}

	packet.pts = av_gettime_relative(); // we are live-casting
	av_decoder.feed_pkt(&packet, &extra);

}

int  DecoderFFMpegWrapper::Pause()  
{
	LOG_DEBUG("replay == live cast\n");
	return DEC_NOT_SUPPORTED;
}
int  DecoderFFMpegWrapper::Resume()   
{
	LOG_DEBUG("replay == live cast\n");
	return DEC_NOT_SUPPORTED;
}
int  DecoderFFMpegWrapper::Stop()	
{
	CHECK_IF_MEDIA_PRESENT(1);

	NET_DVR_SetESRealPlayCallBack(play_handle, NULL, NULL);
	NET_DVR_StopRealPlay(play_handle);
	{
		play_handle = -1;
	}

	if (!av_decoder.is_paused())
	{
		av_decoder.internal_toggle_pause();
	}
	av_decoder.discard_buffer(0);
	av_decoder.close_all_stream();

	WinRender* render = (WinRender*)av_decoder.render;
	render->dettach_from_window();
	
	return 0;
}

int  DecoderFFMpegWrapper::Faster()	 
{
	LOG_DEBUG("replay == live cast\n");
	return DEC_NOT_SUPPORTED;
	
}

int  DecoderFFMpegWrapper::Slower()	 
{
	LOG_DEBUG("replay == live cast\n");
	return DEC_NOT_SUPPORTED;
}

int  DecoderFFMpegWrapper::GetSpeed(int* speed)	// [-4, +4]
{
	*speed = _speed;
	return 0;
}

int  DecoderFFMpegWrapper::FrameForward(void)  //单帧向前
{
	LOG_DEBUG("replay == live cast\n");
	return DEC_NOT_SUPPORTED;
}


int  DecoderFFMpegWrapper::FrameBack(void)    //单帧向后	
{
	LOG_DEBUG("replay == live cast\n");
	return DEC_NOT_SUPPORTED;
}

int DecoderFFMpegWrapper::GetPlayedTime(int* time_point)		//获取文件当前播放位置（秒）
{
	CHECK_IF_MEDIA_PRESENT(1);

	double ts = av_decoder.get_master_clock();
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
	LOG_DEBUG("replay == live cast\n");
	return DEC_NOT_SUPPORTED;
}

int DecoderFFMpegWrapper::GetFileTotalTime(int* seconds)			//获取文件总时长（秒）
{
	CHECK_IF_MEDIA_PRESENT(1);

	// LOG_DEBUG("replay == live cast\n");
	*seconds = (int)99999999;

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
	return DEC_NOT_SUPPORTED;
}


