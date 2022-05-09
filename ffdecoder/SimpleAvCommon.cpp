#include "SimpleAvCommon.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avutil.h"
///////////// packet_queue section {{{

/* packet queue handling */

AVPacket PacketQueue::flush_pkt;

int PacketQueue::is_flush_pkt(const AVPacket& to_check)
{
    return to_check.data == flush_pkt.data;
}

int PacketQueue::packet_queue_put_private(AVPacket* pkt)
{
    MyAVPacketListNode* pkt1;

    if (this->abort_request)
        return -1;

    pkt1 = (MyAVPacketListNode*)av_malloc(sizeof(MyAVPacketListNode));
    if (!pkt1)
        return -1;

    pkt1->pkt = *pkt;   // 要点, ‘pkt’ 已经被 clone 进 MyAVPacketList， 因此无所谓‘pkt’是来自heap/stack/global
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        this->serial++;

    pkt1->serial = this->serial;

    if (!this->last_pkt)
        this->first_pkt = pkt1;
    else
        this->last_pkt->next = pkt1;

    this->last_pkt = pkt1;

    this->nb_packets++;
    this->size += pkt1->pkt.size + sizeof(*pkt1);

    this->total_duration += pkt1->pkt.duration;
    /* XXX: should duplicate packet data in DV case */

    this->cond.wake();

    return 0;
}

// 接管pkt生命周期，put失败也释放掉
int PacketQueue::packet_queue_put(AVPacket* pkt)
{
    int ret;

    AutoLocker _yes_locked(this->cond);
    ret = packet_queue_put_private(pkt);

    if (pkt != &flush_pkt && ret < 0)
        av_packet_unref(pkt);

    return ret;
}

int PacketQueue::packet_queue_put_nullpacket(int stream_index)
{
    AVPacket pkt1, * pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(pkt);
}


void PacketQueue::packet_queue_flush()
{
    MyAVPacketListNode* pkt, * pkt1;

    AutoLocker _yes_locked(this->cond);

    for (pkt = this->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    this->last_pkt = NULL;
    this->first_pkt = NULL;
    this->nb_packets = 0;
    this->size = 0;
    this->total_duration = 0;
}

void PacketQueue::packet_queue_destroy()
{
    packet_queue_flush();
}

void PacketQueue::packet_queue_abort()
{
    AutoLocker _yes_locked(this->cond);
    this->abort_request = 1;
    this->cond.wake();
}

void PacketQueue::packet_queue_start()
{
    AutoLocker _yes_locked(this->cond);
    this->abort_request = 0;
    this->cond.wake();
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
int PacketQueue::packet_queue_get(AVPacket* pkt, int block, /*out*/ int* serial)
{
    MyAVPacketListNode* pkt1;
    int ret;

    AutoLocker _yes_locked(this->cond);

    for (;;) {
        if (this->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = this->first_pkt;
        if (pkt1) {

            // 推移 list_header
            this->first_pkt = pkt1->next;
            if (!this->first_pkt)
                this->last_pkt = NULL;

            // 调整 ‘queue统计’
            this->nb_packets--;
            this->size -= pkt1->pkt.size + sizeof(*pkt1);
            this->total_duration -= pkt1->pkt.duration;

            // output 节点
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;

            av_free(pkt1);
            ret = 1;
            break;
        }
        else if (!block) {
            ret = 0;
            break;
        }
        else {
            this->cond.wait();
        }
    }

    return ret;
}

///////////// }}} packet_queue section


//     frame_queue section {{{

void FrameQueue::unref_item(Frame* vp)
{
    av_frame_unref(vp->frame);
}

int FrameQueue::frame_queue_init(PacketQueue* pktq, int max_size, int keep_last)
{
    int i;

    rindex = 0 ;         // 最初的‘读头’
    rindex_shown = 0;   // rindex指向的位置，是否曾经被 shown过
    windex = 0;
    size = 0;

    this->pktq = pktq;
    this->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    this->keep_last = !!keep_last;
    for (i = 0; i < this->max_size; i++)
        if (!(this->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

void FrameQueue::frame_queue_destory()
{
    int i;
    for (i = 0; i < this->max_size; i++) {
        Frame* vp = &this->queue[i];
        unref_item(vp);
        av_frame_free(&vp->frame);
    }

}

void FrameQueue::frame_queue_signal()
{
    AutoLocker _yes_locked(this->fq_signal);
    this->fq_signal.wake();
}

Frame* FrameQueue::frame_queue_peek()
{
    return &this->queue[(this->rindex + this->rindex_shown) % this->max_size];
}

Frame* FrameQueue::frame_queue_peek_next()
{
    return &this->queue[(this->rindex + this->rindex_shown + 1) % this->max_size];
}

Frame* FrameQueue::frame_queue_peek_last()
{
    return &this->queue[this->rindex];
}

Frame* FrameQueue::frame_queue_peek_writable()
{
    /* wait until we have space to put a new frame */
    {
        AutoLocker _yes_locked(this->fq_signal);
        while (this->size >= this->max_size 
                && !this->pktq->abort_request)  // todo: 如果挂钩的packet queue 退了，怎么能 signal 本 FrameQueue的 cond?
        {
            this->fq_signal.wait();
        }
    }

    if (this->pktq->abort_request)
        return NULL;

    return &this->queue[this->windex];
}

Frame* FrameQueue::frame_queue_peek_readable()
{
    /* wait until we have a readable a new frame */
    {
        AutoLocker _yes_locked(this->fq_signal);
        while (this->size - this->rindex_shown <= 0 &&
            !this->pktq->abort_request) // todo: 如果挂钩的packet queue 退了，怎么能 signal 本 FrameQueue的 cond?
        {
            this->fq_signal.wait();
        }
    }

    if (this->pktq->abort_request)
        return NULL;

    return &this->queue[(this->rindex + this->rindex_shown) % this->max_size];
}

void FrameQueue::frame_queue_push()
{
    if (++this->windex == this->max_size)
        this->windex = 0;

    AutoLocker _yes_locked(this->fq_signal);
    this->size++;

    this->fq_signal.wake();
}

void FrameQueue::frame_queue_next()
{
    if (this->keep_last && !this->rindex_shown) {
        this->rindex_shown = 1;
        return;
    }
    unref_item(&this->queue[this->rindex]);  // 出队列前，先释放 Frame内带的data
    if (++this->rindex == this->max_size)
        this->rindex = 0;

    AutoLocker _yes_locked(this->fq_signal);
    this->size--;
    this->fq_signal.wake();

}

/* return the number of undisplayed frames in the queue */
int FrameQueue::frame_queue_nb_remaining()
{
    return this->size - this->rindex_shown;
}

/* return last shown position */
int64_t FrameQueue::frame_queue_last_pos()
{
    Frame* fp = &this->queue[this->rindex];
    if (this->rindex_shown && fp->serial == this->pktq->serial)
        return fp->pos;
    else
        return -1;
}
//      }}} frame_queue section

// {{{ Clock section 
double Clock::get_clock()
{
    if (*this->queue_serial != this->serial)
        return NAN;
    if (this->paused) {
        return this->pts;
    }
    else {
        double time = av_gettime_relative() / 1000000.0;
        return this->pts_drift + time - (time - this->last_updated) * (1.0 - this->speed);
    }
}

void Clock::set_clock_at(double pts, int serial, double time)
{
    this->pts = pts;
    this->last_updated = time;
    this->pts_drift = this->pts - time;
    this->serial = serial;
}

void Clock::set_clock(double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(pts, serial, time);
}

void Clock::set_clock_speed(double speed)
{
    set_clock(get_clock(), this->serial);
    this->speed = speed;
}

void Clock::init_clock(int* queue_serial)
{
    this->speed = 1.0;
    this->paused = 0;
    this->queue_serial = queue_serial;
    set_clock(NAN, -1);
}

void Clock::sync_clock_to_slave(Clock* slave)
{
    double clock = get_clock();
    double slave_clock = slave->get_clock();
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        this->set_clock(slave_clock, slave->serial);
}

// }}} Clock section 


CString av_strerror2(int err)
{
    char errbuf[128];
    const char* errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));

    return errbuf_ptr;
}

int is_realtime(AVFormatContext* s)
{
    if (!strcmp(s->iformat->name, "rtp")
        || !strcmp(s->iformat->name, "rtsp")
        || !strcmp(s->iformat->name, "sdp")
        )
        return 1;

    if (s->pb && (!strncmp(s->url, "rtp:", 4)
        || !strncmp(s->url, "udp:", 4)
        )
        )
        return 1;
    return 0;
}

template<>
void AutoReleasePtr<AVCodecContext>::release()
{
    if (!me)
        return;

    avcodec_free_context(&me);
    me = NULL;
}

template<>
void AutoReleasePtr<AVFormatContext>::release()
{
    if (!me)
        return;

    avformat_close_input(&me);
    me = NULL;
}

CString decode_codec_tag(uint32_t  codec_tag);
CString codec_para_2_str (const AVCodecParameters * codec_para)
{
    //ref: 
    //  libavformat/dump.c  dump_stream_format()
    //  libavcodec/utils.c  avcodec_string()

    CString vcodec_para_2_str (const AVCodecParameters * codec_para);
    CString acodec_para_2_str (const AVCodecParameters * codec_para);

    if ( AVMEDIA_TYPE_VIDEO == codec_para->codec_type)
    {
        return vcodec_para_2_str ( codec_para);
    }
    else if ( AVMEDIA_TYPE_AUDIO == codec_para->codec_type)
    {
        return acodec_para_2_str ( codec_para);
    }
    else if ( AVMEDIA_TYPE_UNKNOWN == codec_para->codec_type)
    {
        return "'unknown' codec";
    }
    else if ( AVMEDIA_TYPE_DATA == codec_para->codec_type)
    {
        return "'data'codec";
    }
    else if ( AVMEDIA_TYPE_SUBTITLE == codec_para->codec_type)
    {
        return "'subtitle' codec";
    }
    else if ( AVMEDIA_TYPE_ATTACHMENT == codec_para->codec_type)
    {
        return "'attachment' codec";
    }
    else
    {
        return "bad codec";
    }
}


CString vcodec_para_2_str (const AVCodecParameters * codec_para)
{
    CString codec_desc;
    const AVCodecDescriptor * descriptor = avcodec_descriptor_get(codec_para->codec_id);
    if (descriptor)
    { 
        codec_desc.Format("Viddo codec '%s %s'  id %d, extra_size: %d"
                , descriptor->name, decode_codec_tag(codec_para->codec_tag).c_str() 
                , codec_para->codec_id
                , codec_para->extradata_size 
                );
    }
    else
    {
        codec_desc.Format("unsupoorted video codec id: %d, tag: %u, extra_size: %d"
                ,  codec_para->codec_id, codec_para->codec_tag
                , codec_para->extradata_size 
                );
    }

    CString s;
    s.Format(" %s, [%dx%d] sar %d:%d, field_order: %d, %d kb/s"
            , av_get_pix_fmt_name((AVPixelFormat)codec_para->format) 
            , codec_para-> width, codec_para->height
            , codec_para->sample_aspect_ratio.num, codec_para->sample_aspect_ratio.den
            , codec_para->field_order
            , (int)(codec_para->bit_rate /1000)
            );

    return CString("%s\n  %s", codec_desc.c_str(), s.c_str());
}

CString acodec_para_2_str (const AVCodecParameters * codec_para)
{
    CString codec_desc;
    const AVCodecDescriptor * descriptor = avcodec_descriptor_get(codec_para->codec_id);
    if (descriptor)
    {
        codec_desc.Format("Audio codec '%s %s'  id %d, extra_size: %d"
                , descriptor->name, decode_codec_tag(codec_para->codec_tag).c_str() 
                , codec_para->codec_id
                , codec_para->extradata_size 
                );
    }
    else
    {
        codec_desc.Format("unsupoorted audio codec id: %d, tag: %u, extra_size: %d"
                ,  codec_para->codec_id, codec_para->codec_tag
                , codec_para->extradata_size 
                );
    }

    char layout[100];
    av_get_channel_layout_string( layout, sizeof(layout), codec_para->channels, codec_para->channel_layout);

    CString s;
    s.Format(" %s (%d bits), layout: %s, %d Hz, %d kb/s"
            , av_get_sample_fmt_name((AVSampleFormat)codec_para->format)
            , av_get_bytes_per_sample((AVSampleFormat)codec_para->format) * 8 
            , layout
            , codec_para->sample_rate
            , (int) (codec_para->bit_rate / 1000)
            );

    return CString("%s\n  %s", codec_desc.c_str(), s.c_str());
}

CString decode_codec_tag(uint32_t  codec_tag)
{
    if (!codec_tag)
    {
        return "";
    }
   
    char buf[AV_FOURCC_MAX_STRING_SIZE] = {0};
    av_fourcc_make_string(buf, codec_tag );

	return CString ("/%s", (const char*)buf);
}

