#pragma  once
#include <SDL.h>

#include "ffdecoder/ffdecoder.h"

class Render: public RenderBase
{
public:
   
    Render();
    virtual ~Render()
    {
        safe_release();
    }

    virtual int init(int audio_disable, int alwaysontop);

    virtual void toggle_full_screen();
    virtual void safe_release();

    static void get_sdl_pix_fmt_and_blendmode(int format, Uint32* sdl_pix_fmt, SDL_BlendMode* sdl_blendmode);
    static void set_sdl_yuv_conversion_mode(AVFrame* frame);
    
    static void calculate_display_rect(SDL_Rect* rect,
        int scr_xleft, int scr_ytop, int scr_width, int scr_height,
        int pic_width, int pic_height, AVRational pic_sar);
    
    virtual int open_audio( AudioDecoder* decoder, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams* audio_hw_params);
    virtual void mix_audio( uint8_t * dst, const uint8_t * src, uint8_t len, int volume /* [0 - 100]*/ ) ;
    virtual void pause_audio(int pause_on );
    virtual void close_audio();

    virtual void clear_render();
    virtual void draw_render();
    virtual void upload_and_draw_frame(Frame* video_frame);

    virtual int create_window(const char* title, int x, int y, int w, int h, Uint32 flags);
    virtual void show_window( int fullscreen);
    virtual void set_default_window_size(int width, int height, AVRational sar);

    SDL_AudioDeviceID audio_dev; // todo: play audio in render not 'AudioDecoder'
protected:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_RendererInfo renderer_info;

    SDL_Texture* sub_texture;   // 字幕画布
    SDL_Texture* vid_texture;   // 视频画布
    
    struct SwsContext* img_convert_ctx; 

    
    int upload_texture(SDL_Texture** tex, AVFrame* frame, struct SwsContext** img_convert_ctx);
    void show_texture(const Frame* video_frame, const SDL_Rect& rect, int show_subtitle);
    int realloc_texture(SDL_Texture** texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);
    
};

