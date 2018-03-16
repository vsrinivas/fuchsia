// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/algorithm.h>
#include <fbl/limits.h>
#include <zircon/device/audio.h>
#include <lib/zx/vmar.h>

#include "a113-pdm.h"
#include "dispatcher-pool/dispatcher-thread-pool.h"
#include "gauss-pdm-input-stream.h"

namespace audio {
namespace gauss {

GaussPdmInputStream::~GaussPdmInputStream() {}

// static
zx_status_t GaussPdmInputStream::Create(zx_device_t* parent) {
    zxlogf(DEBUG1, "%s\n", __func__);

    auto domain = dispatcher::ExecutionDomain::Create();
    if (domain == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    auto stream =
        fbl::AdoptRef(new GaussPdmInputStream(parent, fbl::move(domain)));

    zx_status_t res = stream->Bind("pdm-audio-driver", parent);
    if (res == ZX_OK) {
        // If bind/setup has succeeded, then the devmgr now controls our
        // lifecycle and will release us when finished with us.  Let go of our
        // local reference.
        //
        __UNUSED auto dummy = stream.leak_ref();
    }

    return ZX_OK;
}

zx_status_t GaussPdmInputStream::Bind(const char* devname,
                                      zx_device_t* parent) {
    ZX_DEBUG_ASSERT(!supported_formats_.size());
    a113_audio_device_init(&audio_device_, parent);

    audio_stream_format_range_t range;
    range.min_channels = 8;
    range.max_channels = 8;
    range.min_frames_per_second = 48000;
    range.max_frames_per_second = 48000;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    supported_formats_.push_back(range);

    a113_pdm_arb_config(&audio_device_);

    // Register interrupt and start irq handling thread.
    zx_status_t status = pdev_map_interrupt(
        &audio_device_.pdev, 0 /* PDM IRQ */, &audio_device_.pdm_irq);

    if (status != ZX_OK) {
        zxlogf(ERROR, "Colud not map interrupt.\n");
        goto finished;
    }

    status = pdev_get_bti(&audio_device_.pdev, 0, &audio_device_.bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Colud not get bti.\n");
        goto finished;
    }

    status = thrd_create_with_name(
        &irqthrd_,
        [](void* thiz) -> int {
            return reinterpret_cast<GaussPdmInputStream*>(thiz)->IrqThread();
        },
        this, "pdm_irq_thread");

    if (status != ZX_OK) {
        zxlogf(ERROR, "Could not start irq thread.\n");
    }

finished:
    if (status != ZX_OK) {
        zx_handle_close(audio_device_.pdm_irq);
        zx_handle_close(audio_device_.bti);
        return status;
    }

    return GaussPdmInputStreamBase::DdkAdd(devname);
}

void GaussPdmInputStream::DdkUnbind() {
    zxlogf(DEBUG1, "%s\n", __func__);
    // Close all of our client event sources if we have not already.
    default_domain_->Deactivate();

    // Unpublish our device node.
    DdkRemove();
}

void GaussPdmInputStream::DdkRelease() {
    zxlogf(DEBUG1, "%s\n", __func__);

    // Shutdown irq thread.
    zx_interrupt_signal(audio_device_.pdm_irq, ZX_INTERRUPT_SLOT_USER, 0);
    thrd_join(irqthrd_, nullptr);

    zx_handle_close(audio_device_.pdm_irq);
    zx_handle_close(audio_device_.bti);

    // Reclaim our reference from the driver framework and let it go out of
    // scope.  If this is our last reference (it should be), we will destruct
    // immediately afterwards.
    auto thiz = fbl::internal::MakeRefPtrNoAdopt(this);
}

zx_status_t GaussPdmInputStream::DdkIoctl(uint32_t op, const void* in_buf,
                                          size_t in_len, void* out_buf,
                                          size_t out_len, size_t* out_actual) {
    zxlogf(DEBUG1, "%s\n", __func__);
    // The only IOCTL we support is get channel.
    if (op != AUDIO_IOCTL_GET_CHANNEL) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if ((out_buf == nullptr) || (out_actual == nullptr) ||
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

    dispatcher::Channel::ProcessHandler phandler([
        stream = fbl::WrapRefPtr(this), privileged
    ](dispatcher::Channel * channel)->zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
        return stream->ProcessStreamChannel(channel, privileged);
    });

    dispatcher::Channel::ChannelClosedHandler chandler;
    if (privileged) {
        chandler = dispatcher::Channel::ChannelClosedHandler(
            [stream = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel)
                ->void {
                    OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
                    stream->DeactivateStreamChannel(channel);
                });
    }

    zx::channel client_endpoint;
    zx_status_t res =
        channel->Activate(&client_endpoint, default_domain_,
                          fbl::move(phandler), fbl::move(chandler));
    if (res == ZX_OK) {
        if (privileged) {
            ZX_DEBUG_ASSERT(stream_channel_ == nullptr);
            stream_channel_ = fbl::move(channel);
        }

        *(reinterpret_cast<zx_handle_t*>(out_buf)) = client_endpoint.release();
        *out_actual = sizeof(zx_handle_t);
    }

    return res;
}

#define HREQ(_cmd, _payload, _handler, _allow_noack, ...)                      \
    case _cmd:                                                                 \
        if (req_size != sizeof(req._payload)) {                                \
            zxlogf(ERROR, "Bad " #_cmd " response length (%u != %zu)\n",       \
                   req_size, sizeof(req._payload));                            \
            return ZX_ERR_INVALID_ARGS;                                        \
        }                                                                      \
        if (!_allow_noack && (req.hdr.cmd & AUDIO_FLAG_NO_ACK)) {              \
            zxlogf(ERROR, "NO_ACK flag not allowed for " #_cmd "\n");          \
            return ZX_ERR_INVALID_ARGS;                                        \
        }                                                                      \
        return _handler(channel, req._payload, ##__VA_ARGS__);
zx_status_t
GaussPdmInputStream::ProcessStreamChannel(dispatcher::Channel* channel,
                                          bool privileged) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    union {
        audio_proto::CmdHdr hdr;
        audio_proto::StreamGetFmtsReq get_formats;
        audio_proto::StreamSetFmtReq set_format;
        audio_proto::GetGainReq get_gain;
        audio_proto::SetGainReq set_gain;
        audio_proto::PlugDetectReq plug_detect;
    } req;

    static_assert(
        sizeof(req) <= 256,
        "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK)
        return res;

    if ((req_size < sizeof(req.hdr) ||
         (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID)))
        return ZX_ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before selecting the dispatch
    // target.
    auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
    switch (cmd) {
        HREQ(AUDIO_STREAM_CMD_GET_FORMATS, get_formats, OnGetStreamFormats,
             false);
        HREQ(AUDIO_STREAM_CMD_SET_FORMAT, set_format, OnSetStreamFormat, false,
             privileged);
        HREQ(AUDIO_STREAM_CMD_GET_GAIN, get_gain, OnGetGain, false);
        HREQ(AUDIO_STREAM_CMD_SET_GAIN, set_gain, OnSetGain, true);
        HREQ(AUDIO_STREAM_CMD_PLUG_DETECT, plug_detect, OnPlugDetect, true);
    default:
        zxlogf(ERROR, "Unrecognized stream command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t
GaussPdmInputStream::ProcessRingBufferChannel(dispatcher::Channel* channel) {
    zxlogf(DEBUG1, "%s\n", __func__);
    ZX_DEBUG_ASSERT(channel != nullptr);

    fbl::AutoLock lock(&lock_);

    union {
        audio_proto::CmdHdr hdr;
        audio_proto::RingBufGetFifoDepthReq get_fifo_depth;
        audio_proto::RingBufGetBufferReq get_buffer;
        audio_proto::RingBufStartReq rb_start;
        audio_proto::RingBufStopReq rb_stop;
    } req;

    static_assert(
        sizeof(req) <= 256,
        "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK)
        return res;

    if ((req_size < sizeof(req.hdr) ||
         (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID)))
        return ZX_ERR_INVALID_ARGS;

    // Strip the NO_ACK flag from the request before selecting the dispatch
    // target.
    auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
    switch (cmd) {
        HREQ(AUDIO_RB_CMD_GET_FIFO_DEPTH, get_fifo_depth, OnGetFifoDepth,
             false);
        HREQ(AUDIO_RB_CMD_GET_BUFFER, get_buffer, OnGetBuffer, false);
        HREQ(AUDIO_RB_CMD_START, rb_start, OnStart, false);
        HREQ(AUDIO_RB_CMD_STOP, rb_stop, OnStop, false);
    default:
        zxlogf(ERROR, "Unrecognized ring buffer command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}
#undef HREQ

zx_status_t GaussPdmInputStream::OnGetStreamFormats(
    dispatcher::Channel* channel, const audio_proto::StreamGetFmtsReq& req) {
    zxlogf(DEBUG1, "%s\n", __func__);
    ZX_DEBUG_ASSERT(channel != nullptr);
    uint16_t formats_sent = 0;
    audio_proto::StreamGetFmtsResp resp = { };

    if (supported_formats_.size() > fbl::numeric_limits<uint16_t>::max()) {
        zxlogf(ERROR, "Too many formats (%zu) to send during "
                      "AUDIO_STREAM_CMD_GET_FORMATS request!\n",
               supported_formats_.size());
        return ZX_ERR_INTERNAL;
    }

    resp.hdr = req.hdr;
    resp.format_range_count = static_cast<uint16_t>(supported_formats_.size());

    do {
        uint16_t todo, payload_sz;
        zx_status_t res;

        todo = fbl::min<uint16_t>(
            static_cast<uint16_t>(supported_formats_.size() - formats_sent),
            AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
        payload_sz =
            static_cast<uint16_t>(sizeof(resp.format_ranges[0]) * todo);

        resp.first_format_range_ndx = formats_sent;
        ::memcpy(resp.format_ranges, supported_formats_.get() + formats_sent,
                 payload_sz);

        res = channel->Write(&resp, sizeof(resp));
        if (res != ZX_OK) {
            zxlogf(ERROR,
                   "Failed to send get stream formats response (res %d)\n",
                   res);
            return res;
        }

        formats_sent = (uint16_t)(formats_sent + todo);
    } while (formats_sent < supported_formats_.size());

    return ZX_OK;
}

zx_status_t
GaussPdmInputStream::OnSetStreamFormat(dispatcher::Channel* channel,
                                       const audio_proto::StreamSetFmtReq& req,
                                       bool privileged) {
    zxlogf(DEBUG1, "%s\n", __func__);
    ZX_DEBUG_ASSERT(channel != nullptr);

    zx::channel client_rb_channel;
    audio_proto::StreamSetFmtResp resp = { };

    resp.hdr = req.hdr;

    // Only the privileged stream channel is allowed to change the format.
    if (!privileged) {
        ZX_DEBUG_ASSERT(channel == stream_channel_.get());
        resp.result = ZX_ERR_ACCESS_DENIED;
        goto finished;
    }

    // TODO(almasrymina): for now, we only support this one frame rate.
    if (req.frames_per_second != frame_rate_) {
        resp.result = ZX_ERR_INVALID_ARGS;
        goto finished;
    }

    // Determine the frame size.
    frame_size_ =
        audio::utils::ComputeFrameSize(req.channels, req.sample_format);
    if (!frame_size_) {
        zxlogf(ERROR, "Failed to compute frame size (ch %hu fmt 0x%08x)\n",
               req.channels, req.sample_format);
        resp.result = ZX_ERR_INTERNAL;
        goto finished;
    }

    // Looks like we are going ahead with this format change.  Tear down any
    // exiting ring buffer interface before proceeding.
    {
        fbl::AutoLock lock(&lock_);
        if (rb_channel_ != nullptr) {
            rb_channel_->Deactivate();
            rb_channel_.reset();
        }

        // Create a new ring buffer channel which can be used to move bulk data
        // and bind it to us.
        rb_channel_ = dispatcher::Channel::Create();
        if (rb_channel_ == nullptr) {
            resp.result = ZX_ERR_NO_MEMORY;
        } else {
            dispatcher::Channel::ProcessHandler
                phandler([stream = fbl::WrapRefPtr(this)](dispatcher::Channel *
                                                          channel)
                             ->zx_status_t {
                                 OBTAIN_EXECUTION_DOMAIN_TOKEN(
                                     t, stream->default_domain_);
                                 return stream->ProcessRingBufferChannel(
                                     channel);
                             });

            dispatcher::Channel::ChannelClosedHandler
            chandler([stream = fbl::WrapRefPtr(this)](
                         const dispatcher::Channel* channel)
                         ->void {
                             OBTAIN_EXECUTION_DOMAIN_TOKEN(
                                 t, stream->default_domain_);
                             stream->DeactivateRingBufferChannel(channel);
                         });

            resp.result =
                rb_channel_->Activate(&client_rb_channel, default_domain_,
                                      fbl::move(phandler), fbl::move(chandler));
            if (resp.result != ZX_OK) {
                rb_channel_.reset();
            }
        }
    }

    a113_audio_register_toddr(&audio_device_);

finished:
    if (resp.result == ZX_OK) {
        // TODO(johngro): Report the actual external delay.
        resp.external_delay_nsec = 0;
    } else {
        fbl::AutoLock lock(&lock_);

        if (rb_channel_) {
            rb_channel_->Deactivate();
            rb_channel_.reset();
        }
    }
    return channel->Write(&resp, sizeof(resp), fbl::move(client_rb_channel));
}

int GaussPdmInputStream::IrqThread() {
    zxlogf(DEBUG1, "Starting irq thread.\n");

    zx_status_t status;

    uint32_t last_notification_offset = 0;

    for (;;) {
        uint64_t slots;
        status = zx_interrupt_wait(audio_device_.pdm_irq, &slots);
        if (status != ZX_OK) {
            zxlogf(DEBUG1, "audio_pdm_input: interrupt error: %d.\n", status);
            break;
        }

        a113_toddr_clear_interrupt(&audio_device_, 0x4);

        uint32_t offset =
            a113_toddr_get_position(&audio_device_) -
            a113_ee_audio_read(&audio_device_, EE_AUDIO_TODDR_B_START_ADDR);

        vmo_helper_.printoffsetinvmo(offset);

        audio_proto::RingBufPositionNotify resp;
        resp.ring_buffer_pos = offset;
        resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;
        resp.hdr.transaction_id = AUDIO_INVALID_TRANSACTION_ID;

        size_t data_available =
            offset >= last_notification_offset
                ? offset - last_notification_offset
                : offset + ring_buffer_size_.load() - last_notification_offset;

        if (notifications_per_ring_.load() &&
            data_available >=
                ring_buffer_size_.load() / notifications_per_ring_.load()) {
            fbl::AutoLock lock(&lock_);
            if (!rb_channel_) {
                zxlogf(DEBUG1, "No rb_channel. Ignoring spurious interrupt.\n");
                continue;
            }
            rb_channel_->Write(&resp, sizeof(resp));
        }
    }

    zxlogf(DEBUG1, "Leaving irq thread.\n");

    return ZX_OK;
}

zx_status_t GaussPdmInputStream::OnGetGain(dispatcher::Channel* channel,
                                           const audio_proto::GetGainReq& req) {
    zxlogf(DEBUG1, "%s\n", __func__);

    ZX_DEBUG_ASSERT(channel != nullptr);
    audio_proto::GetGainResp resp = { };

    resp.hdr = req.hdr;
    resp.cur_mute = false;
    resp.cur_gain = 0.0;
    resp.can_mute = false;
    resp.min_gain = 0.0;
    resp.max_gain = 0.0;
    resp.gain_step = 0.0;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t GaussPdmInputStream::OnSetGain(dispatcher::Channel* channel,
                                           const audio_proto::SetGainReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return ZX_OK;

    audio_proto::SetGainResp resp = { };
    resp.hdr = req.hdr;

    // We don't support setting gain for now.
    resp.result = ZX_ERR_INVALID_ARGS;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t
GaussPdmInputStream::OnPlugDetect(dispatcher::Channel* channel,
                                  const audio_proto::PlugDetectReq& req) {
    zxlogf(DEBUG1, "%s\n", __func__);

    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return ZX_OK;

    audio_proto::PlugDetectResp resp = { };
    resp.hdr = req.hdr;
    resp.flags = static_cast<audio_pd_notify_flags_t>(AUDIO_PDNF_HARDWIRED |
                                                      AUDIO_PDNF_PLUGGED);
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t GaussPdmInputStream::OnGetFifoDepth(
    dispatcher::Channel* channel,
    const audio_proto::RingBufGetFifoDepthReq& req) {
    zxlogf(DEBUG1, "%s\n", __func__);

    audio_proto::RingBufGetFifoDepthResp resp = { };

    resp.hdr = req.hdr;
    resp.result = ZX_OK;
    resp.fifo_depth = static_cast<uint32_t>(fifo_depth_);

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t
GaussPdmInputStream::OnGetBuffer(dispatcher::Channel* channel,
                                 const audio_proto::RingBufGetBufferReq& req) {
    zxlogf(DEBUG1, "%s\n", __func__);

    audio_proto::RingBufGetBufferResp resp = { };
    zx::vmo client_rb_handle;
    uint32_t client_rights;

    resp.hdr = req.hdr;
    resp.result = ZX_ERR_INTERNAL;

    vmo_helper_.DestroyVmo();

    uint32_t notifications_per_ring =
        req.notifications_per_ring ? req.notifications_per_ring : 1;

    uint32_t requested_period_size =
        req.min_ring_buffer_frames * frame_size_ / notifications_per_ring;

    uint32_t period_size = fbl::round_up(requested_period_size,
                                         static_cast<uint32_t>(fifo_depth_));

    ring_buffer_size_.store(fbl::round_up(period_size * notifications_per_ring,
                                          static_cast<uint32_t>(PAGE_SIZE)));

    // TODO(johngro) : Come back here and fix this.  Right now, we know that our
    // frame size is always going to be 16 bytes (8 channels, 2 bytes per
    // channel), and that our ring buffer size is always going to be a multiple
    // of pages (4k, hence divisible by 16), so this should always be the case.
    //
    // Moving forward, if we ever want to support other frame sizes (in
    // particular, things which may not be a power of two), it would be good to
    // make this code more generic.  We have a few requirements to obey,
    // however.  Not only must the ring buffer size be a multiple of frame size,
    // it must also be a multiple of 8; hence a multiple of LCM(frame_size, 8).
    // It would be really handy to have a fbl:: version of fbl::gcd and fbl::lcm
    // to handle these calulations.  Perhaps, by the time that I come back and
    // address this, we will.
    if (ring_buffer_size_.load() % frame_size_) {
        zxlogf(ERROR, "Frame size (%u) does not divide ring buffer size (%zu)\n",
                frame_size_, ring_buffer_size_.load());
        goto finished;
    }

    notifications_per_ring_.store(req.notifications_per_ring);

    zxlogf(DEBUG1, "ring_buffer_size=%lu\n", ring_buffer_size_.load());
    zxlogf(DEBUG1, "req.notifications_per_ring=%u\n",
           req.notifications_per_ring);

    // Create the ring buffer vmo we will use to share memory with the client.
    resp.result = vmo_helper_.AllocateVmo(audio_device_.bti,
                                          ring_buffer_size_.load());
    if (resp.result != ZX_OK) {
        zxlogf(ERROR, "Failed to create ring buffer (size %lu)\n",
               ring_buffer_size_.load());
        goto finished;
    }

    zx_paddr_t start_address;
    zx_paddr_t end_address;

    resp.result = vmo_helper_.GetVmoRange(&start_address);
    if (resp.result != ZX_OK) {
        zxlogf(ERROR, "Failed to get range.\n");
        goto finished;
    }

    // -8 because the addresses are indexed 0 -> size-8. The TODDR processes
    // data in chunks of 8 bytes.
    end_address = start_address + ring_buffer_size_.load() - 8;

    a113_toddr_set_buf(&audio_device_, (uint32_t)start_address,
                       (uint32_t)end_address);
    a113_toddr_set_intrpt(&audio_device_,
                          static_cast<uint32_t>(period_size / 8));

    // TODO(almasrymina): TODDR and pdm configuration is hardcoded for now,
    // since we only support the one format. Need to revisit this when we
    // support more.
    a113_toddr_select_src(&audio_device_, PDMIN);
    a113_toddr_set_format(&audio_device_, RJ_16BITS, 31, 16);

    a113_toddr_set_fifos(&audio_device_, 0x40);
    a113_pdm_ctrl(&audio_device_, 16);
    a113_pdm_filter_ctrl(&audio_device_);

    // Create the client's handle to the ring buffer vmo and set it back to
    // them.
    client_rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP | ZX_RIGHT_READ;

    resp.result = vmo_helper_.Duplicate(client_rights, &client_rb_handle);
    if (resp.result != ZX_OK) {
        zxlogf(ERROR, "Failed to duplicate ring buffer handle (res %d)\n",
               resp.result);
        goto finished;
    }

    ZX_DEBUG_ASSERT((ring_buffer_size_.load() / frame_size_) <=
                    fbl::numeric_limits<decltype(resp.num_ring_buffer_frames)>::max());
    resp.num_ring_buffer_frames =
        static_cast<decltype(resp.num_ring_buffer_frames)>(ring_buffer_size_.load() / frame_size_);

finished:
    zx_status_t res;
    if (resp.result == ZX_OK) {
        ZX_DEBUG_ASSERT(client_rb_handle.is_valid());
        res = channel->Write(&resp, sizeof(resp), fbl::move(client_rb_handle));
    } else {
        res = channel->Write(&resp, sizeof(resp));
    }

    return res;
}

zx_status_t
GaussPdmInputStream::OnStart(dispatcher::Channel* channel,
                             const audio_proto::RingBufStartReq& req) {
    zxlogf(DEBUG1, "%s\n", __func__);

    audio_proto::RingBufStartResp resp = { };
    resp.hdr = req.hdr;

    a113_pdm_fifo_reset(&audio_device_);
    a113_toddr_enable(&audio_device_, 1);
    a113_pdm_enable(&audio_device_, 1);
    resp.start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

    resp.result = ZX_OK;
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t
GaussPdmInputStream::OnStop(dispatcher::Channel* channel,
                            const audio_proto::RingBufStopReq& req) {
    zxlogf(DEBUG1, "%s\n", __func__);

    audio_proto::RingBufStopResp resp = { };

    a113_toddr_enable(&audio_device_, 0);
    a113_pdm_enable(&audio_device_, 0);

    resp.hdr = req.hdr;

    return channel->Write(&resp, sizeof(resp));
}

void GaussPdmInputStream::DeactivateStreamChannel(
    const dispatcher::Channel* channel) {
    zxlogf(DEBUG1, "%s\n", __func__);

    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() == channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() != channel);

    stream_channel_.reset();
}

void GaussPdmInputStream::DeactivateRingBufferChannel(
    const dispatcher::Channel* channel) {
    zxlogf(DEBUG1, "%s\n", __func__);

    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() != channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() == channel);

    a113_audio_unregister_toddr(&audio_device_);

    rb_channel_->Deactivate();
    rb_channel_.reset();
}

} // namespace gauss
} // namespace audio

extern "C" zx_status_t gauss_pdm_input_bind(void* ctx, zx_device_t* device,
                                            void** cookie) {
    zxlogf(DEBUG1, "gauss_pdm_input_bind\n");
    audio::gauss::GaussPdmInputStream::Create(device);
    return ZX_OK;
}

extern "C" void gauss_pdm_input_release(void*) {
    zxlogf(DEBUG1, "gauss_pdm_input_release\n");
    dispatcher::ThreadPool::ShutdownAll();
}
