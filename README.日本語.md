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
又は, 特定のprotocolやデバイス（例えばvideo recorder） より、特別のストリームを再生したい。その時はlibvlcのような汎用decoderが効かないため、下記のようなAPIをもつ 'AV Decoder'がほしい： 
```C++
	int   open_stream(const AVCodecParameters * codec_para);  // codec種類を指定する。
	void  close_all_stream();                                 // close all codec
	void  feed_pkt(AVPacket* pkt ); // feed a 'pkt' to the decoder

	void  video_refresh();   // 何らかのtimerに呼び出され、画面をrefreshする。
```
そういう時には、もちろん'ffplay' を追っていきますね。ただ実際にソースを見たら、「ん。。。」という感じ。そう、 'ffplay'はそもそもffmpegのサンプル且つ’test bed’であり、再利用ための物ではありません。 
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
class VideoState  //上位ラッパー,  「Brief」の載っている 'AV Player'です
			// the naming is to salute 'ffplay' :)
{

	// VideoState is a wrapper of 'AVFormatContext',
	// そして「SimpleAVDecoder」を付けています。

	class SimpleAVDecoder
	{
		// SimpleAVDecoder は「Brief」に載っている'AV Decoder'です。'
		// AVCodecContextの機能をラッパーし、'AV同期'も担当し、そして「render」を付けています。

		RenderBase*  render;
		class RenderBase  // draw pircures and play sound
		{
			// 'sdl_render'という実装は'linux ver'にあります、やっていることは original 'ffplay'そのままです。
			// そして'ffplay_vc'には, render は 'win_render'という実装に置き換えされ、
			// windows GDIでイメージを表示しますが、音声の再生は今段階まだ SDLを利用しています。
		}
 	}
	...
};

```


## class details

(under construction...) 
For now you can take a look at ${working_copy_root}/src_vc/player/FFMpegWrapper.{h|cpp}
It embeds  'AV Player' into existing  'player framework'  in less than 300 lines of code. ( em, maybe also in 30 minutes :- )

# TODO:
Linux versionなら恐らく「このまま置いていく」となります。そもそもlinux versionの目的は、「valgrind」でのコードチェックでした：） 

On windows, 最終目標は'native windows style'のプレーヤになること、それは「D3Dでイメージを描画し、XAudioで音声を放送ます、SDLにさようなら」。また、Hik(海康) NVR よりのストリームを再生するサンプルはこれから加ます（Hik PlayLibを使わずに）。

