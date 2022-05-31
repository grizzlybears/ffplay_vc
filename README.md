# ffplay_vc

Get your own video player in 30 minutes :)

# Brief

Sometimes, we hope we can embed an  'AV player' into our own application. It would have API like this:
```C++
	int  Open(const char* fileName) ;	// open media file
	void Close(void) ;                  //close media
	int  Play(HWND  screen) ;
	int  Stop() ;
```
Or, we may want a 'AV Decoder' to play some customized stream from spec protocol/device (i.e. NVR), like this
```C++
	int   open_stream(const AVCodecParameters * codec_para);  // spec which codec
	void  close_all_stream();  // close all codec
	void  feed_pkt(AVPacket* pkt ); // feed a 'pkt' to the decoder

	void  video_refresh(); // called from timer to refresh display
```

When in those moments, 'ffplay' would be a good start, em... until we see the code :) Yes 'ffplay' is a comprehensive sample of ffmpeg, it doesn't intend to be reusable.
We need a 'clean' ffplay providing  above API, and yes here it is.


# Build
```
on Windows:
    No extra dependencies is required, the working copy carries a set of ffmpeg+SDL2 in 'dep_on_windows'.
    Just open the .sln with VC2017 or higher and build/run. There are 3 'vcproj's.
    'ffplay' is the 'classic' command line style 'ffplay'.
    'ffplay_vc' is a windows GUI behaving as a simplest video player.
    'ffplay_hik' is variation of 'ffplay_vc' which fetches video stream by Hik(海康) API and play it by 'AV Decoder'.
```

```
on Linux:
    At this time, I use system installed SDL2-devel, and refer my local source built ffmpeg. You may want to tweak the 'Makefile' to set FFCFLAGS/FFLDFLAGS match your environment.
    Then just 'make', 'make test' or 'make valgrind'
```



# API

## class hierarchical structure

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
For now you can take a look at [${root}/src_vc/player/FFMpegWrapper.{h|cpp}](src_vc/player/FFMpegWrapper.h)
It  embeds  'AV Player' into existing  'player framework'  in less than 300 lines of code. ( em, maybe also in 30 minutes :- )

# TODO:
For linux version, I guess 'so be it', the purpose of linux version is simply 'to check my code by valgrind'. :)

On windows, the final object is to shape 'ffplay_vc' to  'native windows style',  that is 'drawing pictures by d3d and playing sound by XAudio'. 
Enhance the ‘ffplay_hik’  demo to play video record with ‘speed+-’ and ‘seek’.

