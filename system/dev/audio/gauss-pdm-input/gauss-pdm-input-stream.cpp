// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-proto-utils/format-utils.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/limits.h>
#include <string.h>
#include <zircon/device/audio.h>
#include <zx/vmar.h>

#include "dispatcher-pool/dispatcher-thread-pool.h"
#include "gauss-pdm-input-stream.h"
#include "gauss-pdm-input.h"

namespace audio {
namespace gauss {

GaussPdmInputStream::~GaussPdmInputStream() {}

// static
zx_status_t GaussPdmInputStream::Create(zx_device_t* parent) {
    auto domain = dispatcher::ExecutionDomain::Create();
    if (domain == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    auto stream =
        fbl::AdoptRef(new GaussPdmInputStream(parent, fbl::move(domain)));

    zx_status_t res = stream->Bind("pdm-audio-driver");
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

zx_status_t GaussPdmInputStream::Bind(const char* devname) {
    ZX_DEBUG_ASSERT(!supported_formats_.size());
    return GaussPdmInputStreamBase::DdkAdd(devname);
}

void GaussPdmInputStream::ReleaseRingBufferLocked() {
    if (ring_buffer_virt_ != nullptr) {
        ZX_DEBUG_ASSERT(ring_buffer_size_ != 0);
        zx::vmar::root_self().unmap(
            reinterpret_cast<uintptr_t>(ring_buffer_virt_), ring_buffer_size_);
        ring_buffer_virt_ = nullptr;
        ring_buffer_size_ = 0;
    }
    ring_buffer_vmo_.reset();
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
            stream_channel_ = channel;
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
        if (audio::utils::FormatIsCompatible(
                req.frames_per_second, req.channels, req.sample_format, fmt)) {
            found_one = true;
            break;
        }
    }

    if (!found_one) {
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

finished:
    if (resp.result == ZX_OK) {
        return channel->Write(&resp, sizeof(resp),
                              fbl::move(client_rb_channel));
    } else {
        return channel->Write(&resp, sizeof(resp));
    }
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
    resp.fifo_depth = fifo_bytes_;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t GaussPdmInputStream::OnGetBufferLocked(
    dispatcher::Channel* channel, const audio_proto::RingBufGetBufferReq& req) {
    audio_proto::RingBufGetBufferResp resp;
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
        ring_buffer_size_ = fifo_bytes_;

    // Set up our state for generating notifications.
    if (req.notifications_per_ring) {
        bytes_per_notification_ =
            ring_buffer_size_ / req.notifications_per_ring;
    } else {
        bytes_per_notification_ = 0;
    }

    // Create the ring buffer vmo we will use to share memory with the client.
    resp.result = zx::vmo::create(ring_buffer_size_, 0, &ring_buffer_vmo_);
    if (resp.result != ZX_OK) {
        zxlogf(ERROR, "Failed to create ring buffer (size %u, res %d)\n",
               ring_buffer_size_, resp.result);
        goto finished;
    }

    // TODO(almasrymina): fetch physical mappings and set up DMA hardware here!

    // Create the client's handle to the ring buffer vmo and set it back to
    // them.
    client_rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP | ZX_RIGHT_READ;

    resp.result = ring_buffer_vmo_.duplicate(client_rights, &client_rb_handle);
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

    if (res != ZX_OK)
        ReleaseRingBufferLocked();

    return res;
}

zx_status_t
GaussPdmInputStream::OnStartLocked(dispatcher::Channel* channel,
                                   const audio_proto::RingBufStartReq& req) {
    return ZX_OK;
}

zx_status_t
GaussPdmInputStream::OnStopLocked(dispatcher::Channel* channel,
                                  const audio_proto::RingBufStopReq& req) {
    return ZX_OK;
}

void GaussPdmInputStream::DeactivateStreamChannel(
    const dispatcher::Channel* channel) {
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() == channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() != channel);
    stream_channel_.reset();
}

void GaussPdmInputStream::DeactivateRingBufferChannel(
    const dispatcher::Channel* channel) {
    fbl::AutoLock lock(&lock_);

    ZX_DEBUG_ASSERT(stream_channel_.get() != channel);
    ZX_DEBUG_ASSERT(rb_channel_.get() == channel);

    rb_channel_.reset();
}

} // namespace gauss
} // namespace audio

extern "C" zx_status_t gauss_pdm_input_bind(void* ctx, zx_device_t* device,
                                            void** cookie) {
    printf("gauss_audio_bind\n");
    audio::gauss::GaussPdmInputStream::Create(device);
    return ZX_OK;
}

extern "C" void gauss_pdm_input_release(void*) {
    printf("gauss_audio_release\n");
    audio::dispatcher::ThreadPool::ShutdownAll();
}
