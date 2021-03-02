#include "SimpleAvCommon.h"

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

    pkt1->pkt = *pkt;   // Ҫ��, ��pkt�� �Ѿ��� clone �� MyAVPacketList�� �������ν��pkt��������heap/stack/global
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

// �ӹ�pkt�������ڣ�putʧ��Ҳ�ͷŵ�
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

            // ���� list_header
            this->first_pkt = pkt1->next;
            if (!this->first_pkt)
                this->last_pkt = NULL;

            // ���� ��queueͳ�ơ�
            this->nb_packets--;
            this->size -= pkt1->pkt.size + sizeof(*pkt1);
            this->total_duration -= pkt1->pkt.duration;

            // output �ڵ�
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
        while (this->size >= this->max_size && !this->pktq->abort_request)  // todo: ����ҹ���packet queue ���ˣ���ô�� signal �� FrameQueue�� cond?
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
            !this->pktq->abort_request) // todo: ����ҹ���packet queue ���ˣ���ô�� signal �� FrameQueue�� cond?
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
    unref_item(&this->queue[this->rindex]);  // ������ǰ�����ͷ� Frame�ڴ���data
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


void AutoReleasePtr<AVCodecContext>::release()
{
    if (!me)
        return;

    avcodec_free_context(&me);
    me = NULL;
}

void AutoReleasePtr<AVFormatContext>::release()
{
    if (!me)
        return;

    avformat_close_input(&me);
    me = NULL;
}