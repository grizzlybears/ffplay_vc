# ffplay_vc

三十分で自作のビデオプレーヤが作れます :)

# Brief

こういう時はありませんか。自分のアプリケーションに 'AV player'機能を載せくて、下記のようなAPIがほしい:
```C++
	int  Open(const char* fileName) ;	// open media file
	void Close(void) ;                  // close media
	int  Play(HWND  screen) ;
	int  Stop() ;
```
又は, 特定のprotocolやデバイス（例えばvideo recorder） より、特別 のストリームを再生したい。その時はlibvlcのような汎用のdecoderがきかないため、下記のようなAPIをもつ 'AV Decoder'がほしい： 
```C++
	int   open_stream(const AVCodecParameters * codec_para);  // codec種類を指定する。
	void  close_all_stream();                                 // close all codec
	void  feed_pkt(AVPacket* pkt ); // feed a 'pkt' to the decoder

	void  video_refresh();   // 何らかのtimerに呼び出され、画面をrefreshする。
```
そういう時には、もちろん'ffplay' を追っていきますね。ただ実際にソースを見たら、「ん。。。」という感じ。そう、 'ffplay'はそもそもffmpegのサンプル且つ’test bed’であり、再利用ためのものではありません。 
それでは’クリーン’で、上記のAPIを提供してくれるffplay はありませんか。
あります、ここに。


# Build
```
on Windows:
    ほかの依頼は不要、working copyに「ffmpeg+SDL2」セットを持ちます、'dep_on_windows'に御参照できます。
    直ちに .slnをVC2017もしくはそれ以上のVSで開キ、そして build/runできます。二つ'vcproj'はありますs。'ffplay' は従来の'command line style' ffplayであり, そして 'ffplay_vc' は windows GUIの簡単なビデオプレーヤです。 
```

```
on Linux:
    今現在は、 system installed 「SDL2-devel」と私ローカルのソースビルドffmpegを参照しています。「Makefile」 に 「FFCFLAGS/FFLDFLAGS」を、実際の環境にあるように調整する必要はあります。
    そして、 'make', 'make test' or 'make valgrind'
```


# API

## クラスの階層

```C++
class VideoState  // high level wrapper,  function like 'AV Player' mentioned in 'Brief'
			// the naming is to salute 'ffplay' :)
{

	// VideoState is a wrapper of 'AVFormatContext',
	// and carries a 'SimpleAVDecoder'

	class SimpleAVDecoder
	{
		// SimpleAVDecoder is the 'AV Decoder' mentioned in 'Brief'
		// it wrappers AVCodecContext*, does the 'AV Sync' stuff, and carries a
		RenderBase*  render;

		class RenderBase  // draw pircures and play sound
		{
			// there is a 'sdl_render' impementation used in 'linux ver', which is just extracted from original 'ffplay'.
			// And in 'ffplay_vc', the render is substituded by 'win_render',
			// which draws picture by windows GDI but still plays sound via SDL at this moment.
		}
 	}
	...
};

```


## class details

(under construction...) 
For now you can take a look at ${working_copy_root}/src_vc/player/FFMpegWrapper.{h|cpp}
It  embeds  'AV Player' into existing  'player framework'  in less than 300 lines of code. ( em, maybe also in 30 minutes :- )

# TODO:
For linux version, I guess 'so be it', the purpose of linux version is simply 'to check my code by valgrind'. :)

On windows, the final object is to shape 'ffplay_vc' to  'native windows style',  that is 'drawing pictures by d3d and playing sound by XAudio'. 
And also a demo to play video from Hik(海康) NVR without Hik playlib is planned.

