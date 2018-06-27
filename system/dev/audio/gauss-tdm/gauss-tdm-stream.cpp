// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/limits.h>
#include <string.h>
#include <zircon/device/audio.h>
#include <lib/zx/vmar.h>

#include "dispatcher-pool/dispatcher-thread-pool.h"
#include "tas57xx.h"
#include "tdm-audio-stream.h"

namespace audio {
namespace gauss {

TdmOutputStream::~TdmOutputStream() {}

// static
zx_status_t TdmOutputStream::Create(zx_device_t* parent) {
    auto domain = dispatcher::ExecutionDomain::Create();
    if (domain == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    auto stream = fbl::AdoptRef(
        new TdmOutputStream(parent, fbl::move(domain)));

    zx_status_t res = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &stream->pdev_);
    if (res != ZX_OK) {
        return res;
    }

    res = pdev_get_bti(&stream->pdev_, 0, stream->bti_.reset_and_get_address());
    if (res != ZX_OK) {
        return res;
    }

    size_t mmio_size;
    void *regs;
    res = pdev_map_mmio(&stream->pdev_, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                        &regs, &mmio_size,
                        stream->regs_vmo_.reset_and_get_address());

    if (res != ZX_OK) {
        zxlogf(ERROR, "tdm-output-driver: failed to map mmio.\n");
        return res;
    }
    stream->regs_ = static_cast<aml_tdm_regs_t*>(regs);

    stream->SetModuleClocks();

    //Sleep to let clocks stabilize in amps.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

    res = device_get_protocol(parent, ZX_PROTOCOL_I2C, &stream->i2c_);
    if ( res != ZX_OK) {
        zxlogf(ERROR,"tdm-output-driver: failed to acquire i2c\n");
        return res;
    }

    stream->left_sub_ = Tas57xx::Create(&stream->i2c_, 0);
    if (!stream->left_sub_) {
        return ZX_ERR_NO_RESOURCES;
    }

    stream->right_sub_ = Tas57xx::Create(&stream->i2c_, 1);
    if (!stream->right_sub_) {
        return ZX_ERR_NO_RESOURCES;
    }
    stream->tweeters_ = Tas57xx::Create(&stream->i2c_, 2);
    if (!stream->tweeters_) {
        return ZX_ERR_NO_RESOURCES;
    }

    /*TODO(hollande) - right now we are getting the irq via pdev, but would also like
                       a way to push down which tdm block and frddr blocks to use. will hard
                       code to TDMC and FRDDRC for now.
    */

    stream->notify_timer_ = dispatcher::Timer::Create();
    dispatcher::Timer::ProcessHandler thandler(
            [tdm = stream](dispatcher::Timer * timer)->zx_status_t {
                OBTAIN_EXECUTION_DOMAIN_TOKEN(t, tdm->default_domain_);
                return tdm->ProcessRingNotification();
            });

    stream->notify_timer_->Activate(stream->default_domain_, fbl::move(thandler));

    res = stream->Bind("tdm-output-driver");
    // if successful, we need to leak the stream reference since it holds this object
    //  and would otherwise go away once leaving scope of Create.
    //  Note: clang and gcc feel differently about different ways of doing this
    //        operation, below is a compromisse to appease both simultaneously.
    if (res == ZX_OK) {
        __UNUSED auto dummy = stream.leak_ref();
    }

    return ZX_OK;
}

zx_status_t TdmOutputStream::Bind(const char* devname) {
    ZX_DEBUG_ASSERT(!supported_formats_.size());

    zx_status_t res = AddFormats(&supported_formats_);
    if (res != ZX_OK) {
        zxlogf(ERROR,"Failed to add formats\n");
        return res;
    }

    left_sub_->Standby();
    left_sub_->Reset();
    left_sub_->Init(0);
    left_sub_->SetGain(current_gain_);
    left_sub_->ExitStandby();

    right_sub_->Standby();
    right_sub_->Reset();
    right_sub_->Init(1);
    right_sub_->SetGain(current_gain_);
    right_sub_->ExitStandby();

    tweeters_->Standby();
    tweeters_->Reset();
    tweeters_->Init(0);
    tweeters_->SetGain(current_gain_);
    tweeters_->ExitStandby();

    return TdmAudioStreamBase::DdkAdd(devname);
}

void TdmOutputStream::ReleaseRingBufferLocked() {
    io_buffer_release(&ring_buffer_);
}

zx_status_t TdmOutputStream::AddFormats(
        fbl::Vector<audio_stream_format_range_t>* supported_formats) {
    if (!supported_formats)
        return ZX_ERR_INVALID_ARGS;

    // Record the min/max number of channels.
    audio_stream_format_range_t range;
    range.min_channels = 2;
    range.max_channels = 2;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_frames_per_second = 48000;
    range.max_frames_per_second = 48000;

    fbl::AllocChecker ac;
    supported_formats->reserve(1, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    range.flags = ASF_RANGE_FLAG_FPS_CONTINUOUS;

    supported_formats->push_back(range);

    return ZX_OK;
}

void TdmOutputStream::DdkUnbind() {
    // Close all of our client event sources if we have not already.
    default_domain_->Deactivate();
    // Quiet the data being output on tdm
    regs_->tdmout[TDM_OUT_C].ctl0 &= ~(1 << 31);

    // TODO(hollande) - implement more thorough teardown/reset of the hw state.

    // Unpublish our device node.
    DdkRemove();
}

void TdmOutputStream::DdkRelease() {
    // Ensure execution domain has successfully deactivated.
    ZX_DEBUG_ASSERT(!default_domain_ || default_domain_->deactivated());
    // Reclaim our reference from the driver framework and let it go out of
    // scope.  If this is our last reference (it should be), we will destruct
    // immediately afterwards.
    auto thiz = fbl::internal::MakeRefPtrNoAdopt(this);
}

zx_status_t TdmOutputStream::DdkIoctl(uint32_t op,
                                      const void* in_buf,
                                      size_t in_len,
                                      void* out_buf,
                                      size_t out_len,
                                      size_t* out_actual) {
    // The only IOCTL we support is get channel.
    if (op != AUDIO_IOCTL_GET_CHANNEL) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if ((out_buf == nullptr) ||
        (out_actual == nullptr) ||
        (out_len != sizeof(zx_handle_t))) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AutoLock lock(&lock_);

    // Attempt to allocate a new driver channel and bind it to us.  If we don't
    // already have an stream_channel_, flag this channel is the privileged
    // connection (The connection which is allowed to do things like change
    // formats).
    bool privileged = (stream_channel_ == nullptr);
    auto channel = dispatcher::Channel::Create();
    if (channel == nullptr)
        return ZX_ERR_NO_MEMORY;

    dispatcher::Channel::ProcessHandler phandler(
        [ stream = fbl::WrapRefPtr(this), privileged ](dispatcher::Channel * channel)->zx_status_t {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
            return stream->ProcessStreamChannel(channel, privileged);
        });

    dispatcher::Channel::ChannelClosedHandler chandler;
    if (privileged) {
        chandler = dispatcher::Channel::ChannelClosedHandler(
            [stream = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel)->void {
                OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
                stream->DeactivateStreamChannel(channel);
            });
    }

    zx::channel client_endpoint;
    zx_status_t res = channel->Activate(&client_endpoint,
                                        default_domain_,
                                        fbl::move(phandler),
                                        fbl::move(chandler));
    if (res == ZX_OK) {
        if (privileged) {
            ZX_DEBUG_ASSERT(stream_channel_ == nullptr);
            stream_channel_ = channel;
        }

        *(reinterpret_cast<zx_handle_t*>(out_buf)) = client_endpoint.release();
        *out_actual = sizeof(zx_handle_t);
    }

    return res;
}

#define HREQ(_cmd, _payload, _handler, _allow_noack, ...)             \
    case _cmd:                                                        \
        if (req_size != sizeof(req._payload)) {                       \
            zxlogf(ERROR, "Bad " #_cmd                                \
                          " response length (%u != %zu)\n",           \
                   req_size, sizeof(req._payload));                   \
            return ZX_ERR_INVALID_ARGS;                               \
        }                                                             \
        if (!_allow_noack && (req.hdr.cmd & AUDIO_FLAG_NO_ACK)) {     \
            zxlogf(ERROR, "NO_ACK flag not allowed for " #_cmd "\n"); \
            return ZX_ERR_INVALID_ARGS;                               \
        }                                                             \
        return _handler(channel, req._payload, ##__VA_ARGS__);
zx_status_t TdmOutputStream::ProcessStreamChannel(dispatcher::Channel* channel, bool privileged) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(&lock_);

    union {
        audio_proto::CmdHdr hdr;
        audio_proto::StreamGetFmtsReq get_formats;
        audio_proto::StreamSetFmtReq set_format;
        audio_proto::GetGainReq get_gain;
        audio_proto::SetGainReq set_gain;
        audio_proto::PlugDetectReq plug_detect;
        audio_proto::GetUniqueIdReq get_unique_id;
        audio_proto::GetStringReq get_string;
        // TODO(hollande): add more commands here
    } req;

    static_assert(sizeof(req) <= 256,
                  "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK)
        return res;

    if ((req_size < sizeof(req.hdr) ||
         (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID)))
        return ZX_ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before selecting the dispatch target.
    auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
    switch (cmd) {
        HREQ(AUDIO_STREAM_CMD_GET_FORMATS, get_formats, OnGetStreamFormatsLocked, false);
        HREQ(AUDIO_STREAM_CMD_SET_FORMAT, set_format, OnSetStreamFormatLocked, false, privileged);
        HREQ(AUDIO_STREAM_CMD_GET_GAIN, get_gain, OnGetGainLocked, false);
        HREQ(AUDIO_STREAM_CMD_SET_GAIN, set_gain, OnSetGainLocked, true);
        HREQ(AUDIO_STREAM_CMD_PLUG_DETECT, plug_detect, OnPlugDetectLocked, true);
        HREQ(AUDIO_STREAM_CMD_GET_UNIQUE_ID, get_unique_id, OnGetUniqueIdLocked, false);
        HREQ(AUDIO_STREAM_CMD_GET_STRING, get_string, OnGetStringLocked, false);
    default:
        zxlogf(ERROR, "Unrecognized stream command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t TdmOutputStream::ProcessRingBufferChannel(dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(&lock_);

    union {
        audio_proto::CmdHdr hdr;
        audio_proto::RingBufGetFifoDepthReq get_fifo_depth;
        audio_proto::RingBufGetBufferReq get_buffer;
        audio_proto::RingBufStartReq rb_start;
        audio_proto::RingBufStopReq rb_stop;
        // TODO(almasrymina): add more commands here
    } req;

    static_assert(sizeof(req) <= 256,
                  "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK)
        return res;

    if ((req_size < sizeof(req.hdr) ||
         (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID)))
        return ZX_ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before selecting the dispatch target.
    auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
    switch (cmd) {
        HREQ(AUDIO_RB_CMD_GET_FIFO_DEPTH, get_fifo_depth, OnGetFifoDepthLocked, false);
        HREQ(AUDIO_RB_CMD_GET_BUFFER, get_buffer, OnGetBufferLocked, false);
        HREQ(AUDIO_RB_CMD_START, rb_start, OnStartLocked, false);
        HREQ(AUDIO_RB_CMD_STOP, rb_stop, OnStopLocked, false);
    default:
        zxlogf(ERROR, "Unrecognized ring buffer command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}
#undef HREQ

zx_status_t TdmOutputStream::OnGetStreamFormatsLocked(
        dispatcher::Channel* channel,
        const audio_proto::StreamGetFmtsReq& req) const {
    ZX_DEBUG_ASSERT(channel != nullptr);
    uint16_t formats_sent = 0;
    audio_proto::StreamGetFmtsResp resp = { };

    if (supported_formats_.size() > fbl::numeric_limits<uint16_t>::max()) {
        zxlogf(ERROR,
                "Too many formats (%zu) to send during AUDIO_STREAM_CMD_GET_FORMATS request!\n",
               supported_formats_.size());
        return ZX_ERR_INTERNAL;
    }

    resp.hdr = req.hdr;
    resp.format_range_count = static_cast<uint16_t>(supported_formats_.size());

    do {
        uint16_t todo, payload_sz;
        zx_status_t res;

        todo = fbl::min<uint16_t>(static_cast<uint16_t>(supported_formats_.size() - formats_sent),
                                  AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
        payload_sz = static_cast<uint16_t>(sizeof(resp.format_ranges[0]) * todo);

        resp.first_format_range_ndx = formats_sent;
        ::memcpy(resp.format_ranges, supported_formats_.get() + formats_sent, payload_sz);

        res = channel->Write(&resp, sizeof(resp));
        if (res != ZX_OK) {
            zxlogf(ERROR, "Failed to send get stream formats response (res %d)\n", res);
            return res;
        }

        formats_sent = (uint16_t)(formats_sent + todo);
    } while (formats_sent < supported_formats_.size());

    return ZX_OK;
}

zx_status_t TdmOutputStream::OnSetStreamFormatLocked(dispatcher::Channel* channel,
                                                     const audio_proto::StreamSetFmtReq& req,
                                                     bool privileged) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    zx::channel client_rb_channel;
    audio_proto::StreamSetFmtResp resp = { };
    bool found_one = false;

    resp.hdr = req.hdr;

    // Only the privileged stream channel is allowed to change the format.
    if (!privileged) {
        ZX_DEBUG_ASSERT(channel == stream_channel_.get());
        resp.result = ZX_ERR_ACCESS_DENIED;
        goto finished;
    }

    // Check the format for compatibility
    for (const auto& fmt : supported_formats_) {
        if (audio::utils::FormatIsCompatible(req.frames_per_second,
                                             req.channels,
                                             req.sample_format,
                                             fmt)) {
            found_one = true;
            break;
        }
    }

    if (!found_one) {
        resp.result = ZX_ERR_INVALID_ARGS;
        goto finished;
    }

    // Determine the frame size.
    frame_size_ = audio::utils::ComputeFrameSize(req.channels, req.sample_format);
    if (!frame_size_) {
        zxlogf(ERROR, "Failed to compute frame size (ch %hu fmt 0x%08x)\n", req.channels,
               req.sample_format);
        resp.result = ZX_ERR_INTERNAL;
        goto finished;
    }

    // Looks like we are going ahead with this format change.  Tear down any
    // exiting ring buffer interface before proceeding.
    if (rb_channel_ != nullptr) {
        rb_channel_->Deactivate();
        rb_channel_.reset();
    }

    //A fifo is 256x64bit, B/C fifos are 128x64bit
    //  We are using the C fifo.
    fifo_bytes_ = kFifoDepth * 8;    // each fifo entry is 64bits wide

    // Create a new ring buffer channel which can be used to move bulk data and
    // bind it to us.
    rb_channel_ = dispatcher::Channel::Create();
    if (rb_channel_ == nullptr) {
        resp.result = ZX_ERR_NO_MEMORY;
    } else {
        dispatcher::Channel::ProcessHandler phandler(
            [stream = fbl::WrapRefPtr(this)](dispatcher::Channel * channel)->zx_status_t {
                OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
                return stream->ProcessRingBufferChannel(channel);
            });

        dispatcher::Channel::ChannelClosedHandler chandler(
            [stream = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel)->void {
                OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
                stream->DeactivateRingBufferChannel(channel);
            });

        resp.result = rb_channel_->Activate(&client_rb_channel,
                                            default_domain_,
                                            fbl::move(phandler),
                                            fbl::move(chandler));
        if (resp.result != ZX_OK) {
            rb_channel_.reset();
        }
    }

finished:
    if (resp.result == ZX_OK) {
        // TODO(johngro): Report the actual external delay.
        resp.external_delay_nsec = 0;
        return channel->Write(&resp, sizeof(resp), fbl::move(client_rb_channel));
    } else {
        return channel->Write(&resp, sizeof(resp));
    }
}

zx_status_t TdmOutputStream::OnGetGainLocked(dispatcher::Channel* channel,
                                             const audio_proto::GetGainReq& req) const {
    ZX_DEBUG_ASSERT(channel != nullptr);
    audio_proto::GetGainResp resp = { };

    resp.hdr = req.hdr;
    resp.cur_mute = false;
    resp.cur_gain = current_gain_;
    resp.can_mute = false;
    resp.min_gain = -103.0;
    resp.max_gain = 20.0;
    resp.gain_step = 0.5;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t TdmOutputStream::OnSetGainLocked(dispatcher::Channel* channel,
                                             const audio_proto::SetGainReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return ZX_OK;

    audio_proto::SetGainResp resp = { };
    resp.hdr = req.hdr;


    bool illegal_mute = (req.flags & AUDIO_SGF_MUTE_VALID) && (req.flags & AUDIO_SGF_MUTE);
    bool illegal_gain = (req.flags & AUDIO_SGF_GAIN_VALID) && (!left_sub_->ValidGain(req.gain));

    if (!illegal_gain) {
        left_sub_->SetGain(req.gain);
        right_sub_->SetGain(req.gain);
        tweeters_->SetGain(req.gain);
        left_sub_->GetGain(&current_gain_);
    }

    resp.cur_mute = false;
    resp.cur_gain = current_gain_;
    resp.result = (illegal_mute || illegal_gain)
                      ? ZX_ERR_INVALID_ARGS
                      : ZX_OK;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t TdmOutputStream::OnPlugDetectLocked(dispatcher::Channel* channel,
                                                const audio_proto::PlugDetectReq& req) {
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return ZX_OK;

    audio_proto::PlugDetectResp resp = { };
    resp.hdr = req.hdr;
    resp.flags = static_cast<audio_pd_notify_flags_t>(AUDIO_PDNF_HARDWIRED |
                                                      AUDIO_PDNF_PLUGGED);
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t TdmOutputStream::OnGetUniqueIdLocked(dispatcher::Channel* channel,
                                                 const audio_proto::GetUniqueIdReq& req) const {
    audio_proto::GetUniqueIdResp resp;

    static const audio_stream_unique_id_t spkr_id = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;
    resp.hdr = req.hdr;
    resp.unique_id = spkr_id;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t TdmOutputStream::OnGetStringLocked(dispatcher::Channel* channel,
                                               const audio_proto::GetStringReq& req) const {
    audio_proto::GetStringResp resp;

    resp.hdr = req.hdr;
    resp.id = req.id;

    const char* str;
    switch (req.id) {
        case AUDIO_STREAM_STR_ID_MANUFACTURER: str = "Gauss"; break;
        case AUDIO_STREAM_STR_ID_PRODUCT:      str = "Builtin Speakers"; break;
        default:                               str = nullptr; break;
    }

    if (str == nullptr) {
        resp.result = ZX_ERR_NOT_FOUND;
        resp.strlen = 0;
    } else {
        int res = snprintf(reinterpret_cast<char*>(resp.str), sizeof(resp.str), "%s", str);
        ZX_DEBUG_ASSERT(res >= 0);
        resp.result = ZX_OK;
        resp.strlen = fbl::min<uint32_t>(res, sizeof(resp.str) - 1);
    }

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t TdmOutputStream::OnGetFifoDepthLocked(
        dispatcher::Channel* channel,
        const audio_proto::RingBufGetFifoDepthReq& req) const {
    audio_proto::RingBufGetFifoDepthResp resp = { };

    resp.hdr = req.hdr;
    resp.result = ZX_OK;
    resp.fifo_depth = fifo_bytes_;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t TdmOutputStream::SetModuleClocks() {

    /* enable mclk c
       select mpll2 as source - 245758771.206Hz
       divide clock source by 2 to get tdm mclk
       divide mclk by 10 to get 12287938.5603 Hz SCLK
       SCLK is 256 x fs => 47999.7600012 frames per sec
    */
    regs_->mclk_ctl[MCLK_C] = (1 << 31) | (2 << 24) | (9);

    // configure mst_sclk_gen
    regs_->sclk_ctl[MCLK_C].ctl0 = (0x03 << 30) | (1 << 20) | (0 << 10) | 255;
    regs_->sclk_ctl[MCLK_C].ctl1 = 0x00000001;

    regs_->clk_tdmout_ctl[TDM_OUT_C] = (0x03 << 30) | (2 << 24) | (2 << 20);

    // Enable clock gates for PDM and TDM blocks
    regs_->clk_gate_en |= (1 << 8) | (1 << 11);
    return ZX_OK;
}

zx_status_t TdmOutputStream::OnGetBufferLocked(dispatcher::Channel* channel,
                                               const audio_proto::RingBufGetBufferReq& req) {
    audio_proto::RingBufGetBufferResp resp = { };
    zx::vmo client_rb_handle;
    uint32_t client_rights;

    resp.hdr = req.hdr;
    resp.result = ZX_ERR_INTERNAL;

    // Unmap and release any previous ring buffer.
    ReleaseRingBufferLocked();

    // Compute the ring buffer size.  It needs to be at least as big
    // as the virtual fifo depth.
    ZX_DEBUG_ASSERT(frame_size_ && ((fifo_bytes_ % frame_size_) == 0));
    ZX_DEBUG_ASSERT(fifo_bytes_ && ((fifo_bytes_ % fifo_bytes_) == 0));
    ring_buffer_size_ = req.min_ring_buffer_frames;
    ring_buffer_size_ *= frame_size_;
    if (ring_buffer_size_ < fifo_bytes_)
        ring_buffer_size_ = fbl::round_up(fifo_bytes_, frame_size_);

    // TODO - (hollande) Make this work with non contig vmo
    resp.result = io_buffer_init(&ring_buffer_, bti_.get(), ring_buffer_size_,
                                 IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (resp.result != ZX_OK) {
        zxlogf(ERROR, "Failed to create ring buffer (size %u, res %d)\n", ring_buffer_size_,
               resp.result);
        goto finished;
    }
    ring_buffer_phys_ = (uint32_t)io_buffer_phys(&ring_buffer_);
    ring_buffer_virt_ = io_buffer_virt(&ring_buffer_);

    uint32_t bytes_per_notification;
    if (req.notifications_per_ring) {
        bytes_per_notification = ring_buffer_size_ / req.notifications_per_ring;
    } else {
        bytes_per_notification = 0;
    }
    //TODO - (hollande) calculate this with current rate;
    us_per_notification_ = (1000 * bytes_per_notification) /(48 * frame_size_);

    // Create the client's handle to the ring buffer vmo and set it back to them.
    client_rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE;

    zx_handle_t vmo_copy;
    resp.result = zx_handle_duplicate(ring_buffer_.vmo_handle, client_rights, &vmo_copy);
    if (resp.result != ZX_OK) {
        zxlogf(ERROR, "Failed to duplicate ring buffer handle (res %d)\n", resp.result);
        goto finished;
    }
    client_rb_handle.reset(vmo_copy);
    resp.num_ring_buffer_frames = ring_buffer_size_ / frame_size_;

finished:
    zx_status_t res;
    if (resp.result == ZX_OK) {
        ZX_DEBUG_ASSERT(client_rb_handle.is_valid());
        res = channel->Write(&resp, sizeof(resp), fbl::move(client_rb_handle));
    } else {
        res = channel->Write(&resp, sizeof(resp));
    }

    if (res != ZX_OK) {
        zxlogf(ERROR,"Error in ring buffer creation\n");
        ReleaseRingBufferLocked();
    }

    return res;
}

zx_status_t TdmOutputStream::ProcessRingNotification() {

    if (running_) {
        notify_timer_->Arm(zx_deadline_after(ZX_USEC(us_per_notification_)));
    } else {
        notify_timer_->Cancel();
    }

    audio_proto::RingBufPositionNotify resp = { };
    resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

    resp.ring_buffer_pos = regs_->frddr[2].status2 - ring_buffer_phys_;

    fbl::AutoLock lock(&lock_);
    if (rb_channel_) {
        return rb_channel_->Write(&resp, sizeof(resp));
    } else {
        zxlogf(ERROR,"RingBufferNotification Failed - rb channel closed\n");
        //return ok so the Timer can live on for later use.
        return ZX_OK;
    }
}

zx_status_t TdmOutputStream::OnStartLocked(dispatcher::Channel* channel,
                                           const audio_proto::RingBufStartReq& req) {

    running_ = true;
    if (us_per_notification_ > 0) {
        notify_timer_->Arm(zx_deadline_after(ZX_USEC(us_per_notification_)));
    }

    audio_proto::RingBufStartResp resp = { };

    resp.hdr = req.hdr;
    resp.result = ZX_OK;

    regs_->arb_ctl |= (1 << 31) | (1 << 6);

    regs_->frddr[2].ctl0 = (2 << 0);
    // Set fifo depth and threshold to half the depth
    regs_->frddr[2].ctl1 = (kFifoDepth << 24) | ((kFifoDepth / 2) << 16) | (0 << 8);

    regs_->frddr[2].start_addr = (uint32_t)ring_buffer_phys_;
    regs_->frddr[2].finish_addr = (uint32_t)(ring_buffer_phys_ + ring_buffer_size_ - 8);

    regs_->tdmout[TDM_OUT_C].ctl0 =  (1 << 15) | (7 << 5 ) | (31 << 0);

    regs_->tdmout[TDM_OUT_C].ctl1 =  (15 << 8) | (2 << 24) | (2  << 4);

    regs_->tdmout[TDM_OUT_C].mask[0]=0x00000003;
    regs_->tdmout[TDM_OUT_C].swap = 0x00000010;
    regs_->tdmout[TDM_OUT_C].mask_val=0x00000000;
    regs_->tdmout[TDM_OUT_C].mute_val=0x00000000;

    //reset the module
    regs_->tdmout[TDM_OUT_C].ctl0 &= ~(3 << 28);
    regs_->tdmout[TDM_OUT_C].ctl0 |=  (1 << 29);
    regs_->tdmout[TDM_OUT_C].ctl0 |=  (1 << 28);

    //enable frddr
    regs_->frddr[TDM_OUT_C].ctl0 |= (1 << 31);

    //enable tdmout
    regs_->tdmout[TDM_OUT_C].ctl0 |= (1 << 31);

    resp.start_time = zx_clock_get_monotonic();
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t TdmOutputStream::OnStopLocked(dispatcher::Channel* channel,
                                          const audio_proto::RingBufStopReq& req) {
    notify_timer_->Cancel();
    regs_->tdmout[TDM_OUT_C].ctl0 &= ~(1 << 31);
    running_ = false;
    audio_proto::RingBufStopResp resp = { };
    resp.hdr = req.hdr;
    resp.result = ZX_OK;
    return channel->Write(&resp, sizeof(resp));
}

void TdmOutputStream::DeactivateStreamChannel(const dispatcher::Channel* channel) {
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() == channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() != channel);
    stream_channel_.reset();
}

void TdmOutputStream::DeactivateRingBufferChannel(const dispatcher::Channel* channel) {
    notify_timer_->Cancel();
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() != channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() == channel);

    rb_channel_.reset();
}

} // namespace gauss
} // namespace audio

extern "C" zx_status_t gauss_tdm_bind(void* ctx, zx_device_t* device, void** cookie) {
    audio::gauss::TdmOutputStream::Create(device);
    return ZX_OK;
}

extern "C" void gauss_tdm_release(void*) {
    dispatcher::ThreadPool::ShutdownAll();
}
