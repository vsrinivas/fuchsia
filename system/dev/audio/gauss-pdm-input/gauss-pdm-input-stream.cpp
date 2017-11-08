// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/limits.h>
#include <zircon/device/audio.h>
#include <zx/vmar.h>

#include "a113-pdm.h"
#include "dispatcher-pool/dispatcher-thread-pool.h"
#include "gauss-pdm-input-stream.h"

namespace audio {
namespace gauss {

constexpr size_t BUFFER_SIZE = 0x8000;

GaussPdmInputStream::~GaussPdmInputStream() {}

// static
zx_status_t GaussPdmInputStream::Create(zx_device_t* parent) {
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
        // TODO(almasrymina): outright leaking this reference feels wrong.  We
        // should bind this to the devmgr cookie somehow instead.
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

    return GaussPdmInputStreamBase::DdkAdd(devname);
}

zx_status_t GaussPdmInputStream::AddFormats() {
    return ZX_OK;
}

void GaussPdmInputStream::DdkUnbind() {
    // Close all of our client event sources if we have not already.
    default_domain_->Deactivate();

    // Unpublish our device node.
    DdkRemove();
}

void GaussPdmInputStream::DdkRelease() {
    // Reclaim our reference from the driver framework and let it go out of
    // scope.  If this is our last reference (it should be), we will destruct
    // immediately afterwards.
    auto thiz = fbl::internal::MakeRefPtrNoAdopt(this);
}

zx_status_t GaussPdmInputStream::DdkIoctl(uint32_t op, const void* in_buf,
                                          size_t in_len, void* out_buf,
                                          size_t out_len, size_t* out_actual) {
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

#define HREQ(_cmd, _payload, _handler, _allow_noack, ...)                \
    case _cmd:                                                           \
        if (req_size != sizeof(req._payload)) {                          \
            zxlogf(ERROR, "Bad " #_cmd " response length (%u != %zu)\n", \
                   req_size, sizeof(req._payload));                      \
            return ZX_ERR_INVALID_ARGS;                                  \
        }                                                                \
        if (!_allow_noack && (req.hdr.cmd & AUDIO_FLAG_NO_ACK)) {        \
            zxlogf(ERROR, "NO_ACK flag not allowed for " #_cmd "\n");    \
            return ZX_ERR_INVALID_ARGS;                                  \
        }                                                                \
        return _handler(channel, req._payload, ##__VA_ARGS__);
zx_status_t
GaussPdmInputStream::ProcessStreamChannel(dispatcher::Channel* channel,
                                          bool privileged) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(&lock_);

    // TODO(almasrymina): Factor all of this behavior around accepting channels
    // and
    // dispatching audio driver requests into some form of utility class so it
    // can be shared with the IntelHDA codec implementations as well.
    union {
        audio_proto::CmdHdr hdr;
        audio_proto::StreamGetFmtsReq get_formats;
        audio_proto::StreamSetFmtReq set_format;
        audio_proto::GetGainReq get_gain;
        audio_proto::SetGainReq set_gain;
        audio_proto::PlugDetectReq plug_detect;
        // TODO(almasrymina): add more commands here
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
        HREQ(AUDIO_STREAM_CMD_GET_FORMATS, get_formats,
             OnGetStreamFormatsLocked, false);
        HREQ(AUDIO_STREAM_CMD_SET_FORMAT, set_format, OnSetStreamFormatLocked,
             false, privileged);
        HREQ(AUDIO_STREAM_CMD_GET_GAIN, get_gain, OnGetGainLocked, false);
        HREQ(AUDIO_STREAM_CMD_SET_GAIN, set_gain, OnSetGainLocked, true);
        HREQ(AUDIO_STREAM_CMD_PLUG_DETECT, plug_detect, OnPlugDetectLocked,
             true);
    default:
        zxlogf(ERROR, "Unrecognized stream command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t
GaussPdmInputStream::ProcessRingBufferChannel(dispatcher::Channel* channel) {
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
        HREQ(AUDIO_RB_CMD_GET_FIFO_DEPTH, get_fifo_depth, OnGetFifoDepthLocked,
             false);
        HREQ(AUDIO_RB_CMD_GET_BUFFER, get_buffer, OnGetBufferLocked, false);
        HREQ(AUDIO_RB_CMD_START, rb_start, OnStartLocked, false);
        HREQ(AUDIO_RB_CMD_STOP, rb_stop, OnStopLocked, false);
    default:
        zxlogf(ERROR, "Unrecognized ring buffer command 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}
#undef HANDLE_REQ

zx_status_t GaussPdmInputStream::OnGetStreamFormatsLocked(
    dispatcher::Channel* channel, const audio_proto::StreamGetFmtsReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    uint16_t formats_sent = 0;
    audio_proto::StreamGetFmtsResp resp;

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

zx_status_t GaussPdmInputStream::OnSetStreamFormatLocked(
    dispatcher::Channel* channel, const audio_proto::StreamSetFmtReq& req,
    bool privileged) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    zx::channel client_rb_channel;
    audio_proto::StreamSetFmtResp resp;

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
    if (rb_channel_ != nullptr) {
        rb_channel_->Deactivate();
        rb_channel_.reset();
    }

    // Create a new ring buffer channel which can be used to move bulk data and
    // bind it to us.
    rb_channel_ = dispatcher::Channel::Create();
    if (rb_channel_ == nullptr) {
        resp.result = ZX_ERR_NO_MEMORY;
    } else {
        dispatcher::Channel::ProcessHandler
            phandler([stream =
                          fbl::WrapRefPtr(this)](dispatcher::Channel * channel)
                         ->zx_status_t {
                             OBTAIN_EXECUTION_DOMAIN_TOKEN(
                                 t, stream->default_domain_);
                             return stream->ProcessRingBufferChannel(channel);
                         });

        dispatcher::Channel::ChannelClosedHandler
        chandler([stream =
                      fbl::WrapRefPtr(this)](const dispatcher::Channel* channel)
                     ->void {
                         OBTAIN_EXECUTION_DOMAIN_TOKEN(t,
                                                       stream->default_domain_);
                         stream->DeactivateRingBufferChannel(channel);
                     });

        resp.result =
            rb_channel_->Activate(&client_rb_channel, default_domain_,
                                  fbl::move(phandler), fbl::move(chandler));
        if (resp.result != ZX_OK) {
            rb_channel_.reset();
        }
    }

    a113_audio_register_toddr(&audio_device_);

    resp.result = pdev_map_interrupt(&audio_device_.pdev, 0 /* PDM IRQ */,
                                     &audio_device_.pdm_irq);

    if (resp.result != ZX_OK) {
        goto finished;
    }

    thrd_t irqthrd;
    thrd_create_with_name(&irqthrd, GaussPdmInputStream::IrqThread, this,
                          "pdm_irq_thread");

finished:
    if (resp.result == ZX_OK) {
        return channel->Write(&resp, sizeof(resp),
                              fbl::move(client_rb_channel));
    } else {
        return channel->Write(&resp, sizeof(resp));
    }
}

int GaussPdmInputStream::IrqThread(void* arg) {
    GaussPdmInputStream* dev = (GaussPdmInputStream*)arg;
    zx_status_t status;
    zxlogf(ERROR, "starting running interrupt handler.\n");

    for (;;) {
        status = zx_interrupt_wait(dev->audio_device_.pdm_irq);
        if (status != ZX_OK) {
            zxlogf(ERROR, "gauss pdm input: interrupt error\n");
            break;
        }

        a113_toddr_clear_interrupt(&dev->audio_device_, 0x4);

        zx_interrupt_complete(dev->audio_device_.pdm_irq);

        uint32_t offset = a113_toddr_get_position(&dev->audio_device_) -
                          a113_ee_audio_read(&dev->audio_device_,
                                             EE_AUDIO_TODDR_B_START_ADDR);

        dev->vmo_helper_.printoffsetinvmo(offset);

        audio_proto::RingBufPositionNotify resp;

        resp.ring_buffer_pos = offset;

        resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;
        resp.hdr.transaction_id = AUDIO_INVALID_TRANSACTION_ID;

        // TODO(almasrymina): need to properly stop the hardware from
        // recording, so we don't have to null check here.
        {
            fbl::AutoLock lock(&dev->lock_);
            if (dev->rb_channel_) {
                dev->rb_channel_->Write(&resp, sizeof(resp));
            }
        }
    }

    zxlogf(DEBUG1, "Leaving irq thread.\n");

    return ZX_OK;
}

zx_status_t
GaussPdmInputStream::OnGetGainLocked(dispatcher::Channel* channel,
                                     const audio_proto::GetGainReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    audio_proto::GetGainResp resp;

    resp.hdr = req.hdr;
    resp.cur_mute = false;
    resp.cur_gain = 0.0;
    resp.can_mute = false;
    resp.min_gain = 0.0;
    resp.max_gain = 0.0;
    resp.gain_step = 0.0;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t
GaussPdmInputStream::OnSetGainLocked(dispatcher::Channel* channel,
                                     const audio_proto::SetGainReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return ZX_OK;

    audio_proto::SetGainResp resp;
    resp.hdr = req.hdr;

    bool illegal_mute =
        (req.flags & AUDIO_SGF_MUTE_VALID) && (req.flags & AUDIO_SGF_MUTE);
    bool illegal_gain =
        (req.flags & AUDIO_SGF_GAIN_VALID) && (req.gain != 0.0f);

    resp.cur_mute = false;
    resp.cur_gain = 0.0;
    resp.result = (illegal_mute || illegal_gain) ? ZX_ERR_INVALID_ARGS : ZX_OK;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t
GaussPdmInputStream::OnPlugDetectLocked(dispatcher::Channel* channel,
                                        const audio_proto::PlugDetectReq& req) {
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK)
        return ZX_OK;

    audio_proto::PlugDetectResp resp;
    resp.hdr = req.hdr;
    resp.flags = static_cast<audio_pd_notify_flags_t>(AUDIO_PDNF_HARDWIRED |
                                                      AUDIO_PDNF_PLUGGED);
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t GaussPdmInputStream::OnGetFifoDepthLocked(
    dispatcher::Channel* channel,
    const audio_proto::RingBufGetFifoDepthReq& req) {
    audio_proto::RingBufGetFifoDepthResp resp;

    resp.hdr = req.hdr;
    resp.result = ZX_OK;
    resp.fifo_depth = GetFifoBytes();

    return channel->Write(&resp, sizeof(resp));
}

uint32_t GaussPdmInputStream::GetFifoBytes() {
    // TODO(almasrymina): ok assumption...?
    return 0x40 * 8;
}

zx_status_t GaussPdmInputStream::OnGetBufferLocked(
    dispatcher::Channel* channel, const audio_proto::RingBufGetBufferReq& req) {
    audio_proto::RingBufGetBufferResp resp;
    zx::vmo client_rb_handle;
    uint32_t client_rights;

    resp.hdr = req.hdr;
    resp.result = ZX_ERR_INTERNAL;

    // Compute the ring buffer size.  It needs to be at least as big
    // as the virtual fifo depth.
    // TODO(almasrymina): need to revist these calculations.
    ZX_DEBUG_ASSERT(frame_size_ && ((GetFifoBytes() % frame_size_) == 0));

    vmo_helper_.DestroyVmo();

    // Create the ring buffer vmo we will use to share memory with the client.
    resp.result = vmo_helper_.AllocateVmo(BUFFER_SIZE);
    if (resp.result != ZX_OK) {
        zxlogf(ERROR, "Failed to create ring buffer (size %lu)\n",
               BUFFER_SIZE);
        goto finished;
    }

    zx_paddr_t start_address;
    zx_paddr_t end_address;

    resp.result = vmo_helper_.GetVmoRange(&start_address);
    if (resp.result != ZX_OK) {
        zxlogf(ERROR, "Failed to get range.\n");
        goto finished;
    }

    // -1 because the addresses are indexed 0 -> size-1.
    end_address = start_address + BUFFER_SIZE - 1;

    a113_toddr_set_buf(&audio_device_, (uint32_t)start_address,
                       (uint32_t)end_address);
    a113_toddr_set_intrpt(&audio_device_, 1024);

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
GaussPdmInputStream::OnStartLocked(dispatcher::Channel* channel,
                                   const audio_proto::RingBufStartReq& req) {
    audio_proto::RingBufStartResp resp;
    resp.hdr = req.hdr;
    resp.start_ticks = 0;

    a113_pdm_fifo_reset(&audio_device_);

    a113_toddr_enable(&audio_device_, 1);
    a113_pdm_enable(&audio_device_, 1);

    resp.start_ticks = zx_ticks_get();

    a113_pdm_dump_registers(&audio_device_);

    resp.result = ZX_OK;
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t
GaussPdmInputStream::OnStopLocked(dispatcher::Channel* channel,
                                  const audio_proto::RingBufStopReq& req) {
    audio_proto::RingBufStopResp resp;

    a113_pdm_enable(&audio_device_, 0);
    a113_toddr_enable(&audio_device_, 0);

    a113_audio_unregister_toddr(&audio_device_);
    // TODO(almasrymina): need to also release the interrupt.

    resp.hdr = req.hdr;

    return channel->Write(&resp, sizeof(resp));
}

void GaussPdmInputStream::DeactivateStreamChannel(
    const dispatcher::Channel* channel) {
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() == channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() != channel);

    a113_audio_unregister_toddr(&audio_device_);
    stream_channel_.reset();
}

void GaussPdmInputStream::DeactivateRingBufferChannel(
    const dispatcher::Channel* channel) {
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() != channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() == channel);

    a113_pdm_enable(&audio_device_, 0);
    a113_toddr_enable(&audio_device_, 0);

    rb_channel_.reset();
}

} // namespace gauss
} // namespace audio

extern "C" zx_status_t gauss_pdm_input_bind(void* ctx, zx_device_t* device,
                                            void** cookie) {
    zxlogf(INFO, "gauss_pdm_input_bind\n");
    audio::gauss::GaussPdmInputStream::Create(device);
    return ZX_OK;
}

extern "C" void gauss_pdm_input_release(void*) {
    zxlogf(INFO, "gauss_pdm_input_release\n");
    audio::dispatcher::ThreadPool::ShutdownAll();
}
