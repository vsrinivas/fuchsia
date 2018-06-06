// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/limits.h>

#include <zircon/device/audio.h>

#include <audio-proto-utils/format-utils.h>

#include "intel-audio-dsp.h"
#include "intel-dsp-stream.h"

namespace audio {
namespace intel_hda {

IntelDspStream::IntelDspStream(uint32_t id,
                               bool is_input,
                               const DspPipeline& pipeline,
                               const audio_stream_unique_id_t* unique_id)
    : IntelHDAStreamBase(id, is_input), pipeline_(pipeline) {
    snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %cStream #%u", is_input ? 'I' : 'O', id);

    if (unique_id) {
        SetPersistentUniqueId(*unique_id);
    } else {
        const audio_stream_unique_id_t uid = {
            'I', 'D', 'S', 'P',
            static_cast<uint8_t>(id >> 24),
            static_cast<uint8_t>(id >> 16),
            static_cast<uint8_t>(id >> 8),
            static_cast<uint8_t>(id),
            static_cast<uint8_t>(is_input),
            0
        };
        SetPersistentUniqueId(uid);
    }
}

zx_status_t IntelDspStream::ProcessSetStreamFmt(const ihda_proto::SetStreamFmtResp& codec_resp,
                                                zx::channel&& ring_buffer_channel) {
    ZX_DEBUG_ASSERT(ring_buffer_channel.is_valid());

    fbl::AutoLock lock(obj_lock());
    audio_proto::StreamSetFmtResp resp = { };
    zx_status_t res = ZX_OK;

    // Are we shutting down?
    if (!is_active()) {
        return ZX_ERR_BAD_STATE;
    }

    // The DSP needs to coordinate with ring buffer commands. Set up an additional
    // channel to intercept messages on the ring buffer channel.
    zx::channel client_endpoint;
    res = CreateClientRingBufferChannelLocked(fbl::move(ring_buffer_channel), &client_endpoint);
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to set up client ring buffer channel (res %d)\n", res);
        goto finished;
    }

    // Let the implementation send the commands required to finish changing the
    // stream format.
    res = FinishChangeStreamFormatLocked(encoded_fmt());
    if (res != ZX_OK) {
        LOG(ERROR, "Failed to finish set format (enc fmt 0x%04hx res %d)\n", encoded_fmt(), res);
        goto finished;
    }

    ZX_DEBUG_ASSERT(client_endpoint.is_valid());

    // Respond to the caller, transferring the DMA handle back in the process.
    resp.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;
    resp.hdr.transaction_id = set_format_tid();
    resp.result = ZX_OK;
    resp.external_delay_nsec = 0;   // report his properly based on the codec path delay.
    res = stream_channel()->Write(&resp, sizeof(resp), fbl::move(client_endpoint));

    // If we don't have a set format operation in flight, or the stream channel
    // has been closed, this set format operation has been canceled.  Do not
    // return an error up the stack; we don't want to close the connection to
    // our codec device.
    if ((set_format_tid() == AUDIO_INVALID_TRANSACTION_ID) ||
        (stream_channel() == nullptr)) {
        goto finished;
    }

finished:
    // Something went fatally wrong when trying to send the result back to the
    // caller.  Close the stream channel.
    if ((res != ZX_OK) && (stream_channel() != nullptr)) {
        OnChannelDeactivateLocked(*stream_channel());
        stream_channel()->Deactivate();
        stream_channel() = nullptr;
    }

    // One way or the other, this set format operation is finished.  Clear out
    // the in-flight transaction ID
    SetFormatTidLocked(AUDIO_INVALID_TRANSACTION_ID);

    return ZX_OK;
}

zx_status_t IntelDspStream::CreateClientRingBufferChannelLocked(
        zx::channel&& ring_buffer_channel,
        zx::channel* out_client_channel) {
    // Attempt to allocate a new ring buffer channel and bind it to us.
    // This channel is connected to the upstream device.
    auto channel = dispatcher::Channel::Create();
    if (channel == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    dispatcher::Channel::ProcessHandler phandler(
    [stream = fbl::WrapRefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->domain());
        return stream->ProcessRbRequest(channel);
    });

    dispatcher::Channel::ChannelClosedHandler chandler(
    [stream = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel) -> void {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->domain());
        stream->ProcessRbDeactivate(channel);
    });

    zx_status_t res = channel->Activate(fbl::move(ring_buffer_channel),
                                        domain(),
                                        fbl::move(phandler),
                                        fbl::move(chandler));
    if (res != ZX_OK) {
        return res;
    }
    ZX_DEBUG_ASSERT(rb_channel_ == nullptr);
    rb_channel_ = channel;

    // Attempt to allocate a new ring buffer channel and bind it to us.
    // This channel is connected to the client.
    auto client_channel = dispatcher::Channel::Create();
    if (client_channel == nullptr) {
        rb_channel_->Deactivate();
        rb_channel_ = nullptr;
        return ZX_ERR_NO_MEMORY;
    }

    dispatcher::Channel::ProcessHandler client_phandler(
    [stream = fbl::WrapRefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->domain());
        return stream->ProcessClientRbRequest(channel);
    });

    dispatcher::Channel::ChannelClosedHandler client_chandler(
    [stream = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel) -> void {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->domain());
        stream->ProcessClientRbDeactivate(channel);
    });

    res = client_channel->Activate(out_client_channel,
                                   domain(),
                                   fbl::move(client_phandler),
                                   fbl::move(client_chandler));
    if (res == ZX_OK) {
        ZX_DEBUG_ASSERT(client_rb_channel_ == nullptr);
        client_rb_channel_ = client_channel;
    } else {
        rb_channel_->Deactivate();
        rb_channel_ = nullptr;
    }

    return res;
}

zx_status_t IntelDspStream::ProcessRbRequest(dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(obj_lock());

    // If we have lost our connection to the codec device, or are in the process
    // of shutting down, there is nothing further we can do.  Fail the request
    // and close the connection to the caller.
    if (!is_active() || (rb_channel_ == nullptr) || (client_rb_channel_ == nullptr)) {
        return ZX_ERR_BAD_STATE;
    }

    zx::handle rxed_handle;
    uint32_t req_size;
    union {
        audio_proto::CmdHdr                  hdr;
        audio_proto::RingBufGetFifoDepthResp get_fifo_depth;
        audio_proto::RingBufGetBufferResp    get_buffer;
        audio_proto::RingBufStartResp        start;
        audio_proto::RingBufStopResp         stop;
    } req;
    // TODO(johngro) : How large is too large?
    static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

    zx_status_t res = channel->Read(&req, sizeof(req), &req_size, &rxed_handle);
    if (res != ZX_OK) {
        return res;
    }

    switch (req.hdr.cmd) {
    case AUDIO_RB_CMD_START:
    {
        auto dsp = fbl::RefPtr<IntelAudioDsp>::Downcast(parent_codec());
        zx_status_t st = dsp->StartPipeline(pipeline_);
        if (st != ZX_OK) {
            audio_proto::RingBufStartResp resp = { };
            resp.hdr = req.hdr;
            resp.result = st;
            return client_rb_channel_->Write(&resp, sizeof(resp));
        }
        break;
    }
    default:
        break;
    }

    return client_rb_channel_->Write(&req, req_size, fbl::move(rxed_handle));
}

void IntelDspStream::ProcessRbDeactivate(const dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(obj_lock());

    LOG(TRACE, "ProcessClientRbDeactivate\n");

    ZX_DEBUG_ASSERT(channel == rb_channel_.get());
    rb_channel_->Deactivate();
    rb_channel_ = nullptr;

    // Deactivate the client channel.
    if (client_rb_channel_ != nullptr) {
        client_rb_channel_->Deactivate();
        client_rb_channel_ = nullptr;
    }
}

zx_status_t IntelDspStream::ProcessClientRbRequest(dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(obj_lock());

    // If we have lost our connection to the codec device, or are in the process
    // of shutting down, there is nothing further we can do.  Fail the request
    // and close the connection to the caller.
    if (!is_active() || (rb_channel_ == nullptr) || (client_rb_channel_ == nullptr)) {
        return ZX_ERR_BAD_STATE;
    }

    uint32_t req_size;
    union {
        audio_proto::CmdHdr                 hdr;
        audio_proto::RingBufGetFifoDepthReq get_fifo_depth;
        audio_proto::RingBufGetBufferReq    get_buffer;
        audio_proto::RingBufStartReq        start;
        audio_proto::RingBufStopReq         stop;
    } req;
    // TODO(johngro) : How large is too large?
    static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

    zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
    if (res != ZX_OK) {
        return res;
    }

    switch (req.hdr.cmd) {
    case AUDIO_RB_CMD_STOP:
    {
        auto dsp = fbl::RefPtr<IntelAudioDsp>::Downcast(parent_codec());
        zx_status_t st = dsp->PausePipeline(pipeline_);
        if (st != ZX_OK) {
            audio_proto::RingBufStopResp resp = { };
            resp.hdr = req.hdr;
            resp.result = st;
            return channel->Write(&resp, sizeof(resp));
        }
        break;
    }
    default:
        break;
    }

    return rb_channel_->Write(&req, req_size);
}

void IntelDspStream::ProcessClientRbDeactivate(const dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock lock(obj_lock());

    LOG(TRACE, "ProcessClientRbDeactivate\n");

    ZX_DEBUG_ASSERT(channel == client_rb_channel_.get());
    client_rb_channel_->Deactivate();
    client_rb_channel_ = nullptr;

    // Deactivate the upstream channel.
    if (rb_channel_ != nullptr) {
        rb_channel_->Deactivate();
        rb_channel_ = nullptr;
    }
}

zx_status_t IntelDspStream::OnActivateLocked() {
    // FIXME(yky) Hardcode supported formats.
    audio_stream_format_range_t fmt;
    fmt.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    fmt.min_frames_per_second =
    fmt.max_frames_per_second = 48000;
    fmt.min_channels =
    fmt.max_channels = 2;
    fmt.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

    fbl::Vector<audio_proto::FormatRange> supported_formats;
    fbl::AllocChecker ac;
    supported_formats.push_back(fmt, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    SetSupportedFormatsLocked(fbl::move(supported_formats));
    return ZX_OK;
}

void IntelDspStream::OnDeactivateLocked() {
    LOG(TRACE, "OnDeactivateLocked\n");
}

void IntelDspStream::OnChannelDeactivateLocked(const dispatcher::Channel& channel) {
    LOG(TRACE, "OnChannelDeactivateLocked\n");
}

zx_status_t IntelDspStream::OnDMAAssignedLocked() {
    LOG(TRACE, "OnDMAAssignedLocked\n");
    return PublishDeviceLocked();
}

zx_status_t IntelDspStream::OnSolicitedResponseLocked(const CodecResponse& resp) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelDspStream::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelDspStream::BeginChangeStreamFormatLocked(
        const audio_proto::StreamSetFmtReq& req) {
    LOG(TRACE, "BeginChangeStreamFormatLocked\n");
    return ZX_OK;
}

zx_status_t IntelDspStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
    LOG(TRACE, "FinishChangeStreamFormatLocked\n");
    return ZX_OK;
}

void IntelDspStream::OnGetGainLocked(audio_proto::GetGainResp* out_resp) {
    LOG(TRACE, "OnGetGainLocked\n");
    IntelHDAStreamBase::OnGetGainLocked(out_resp);
}

void IntelDspStream::OnSetGainLocked(const audio_proto::SetGainReq& req,
                                     audio_proto::SetGainResp* out_resp) {
    LOG(TRACE, "OnSetGainLocked\n");
    IntelHDAStreamBase::OnSetGainLocked(req, out_resp);
}

void IntelDspStream::OnPlugDetectLocked(dispatcher::Channel* response_channel,
                                        const audio_proto::PlugDetectReq& req,
                                        audio_proto::PlugDetectResp* out_resp) {
    LOG(TRACE, "OnPlugDetectLocked\n");
    IntelHDAStreamBase::OnPlugDetectLocked(response_channel, req, out_resp);
}

void IntelDspStream::OnGetStringLocked(const audio_proto::GetStringReq& req,
                                       audio_proto::GetStringResp* out_resp) {
    ZX_DEBUG_ASSERT(out_resp);
    const char* requested_string = nullptr;

    switch (req.id) {
        case AUDIO_STREAM_STR_ID_MANUFACTURER:
            requested_string = "Intel";
            break;

        case AUDIO_STREAM_STR_ID_PRODUCT:
            requested_string = is_input() ? "Builtin Microphone" : "Builtin Speakers";
            break;

        default:
            IntelHDAStreamBase::OnGetStringLocked(req, out_resp);
            return;
    }

    int res = snprintf(reinterpret_cast<char*>(out_resp->str), sizeof(out_resp->str), "%s",
                       requested_string ? requested_string : "<unassigned>");
    ZX_DEBUG_ASSERT(res >= 0);
    out_resp->result = ZX_OK;
    out_resp->strlen = fbl::min<uint32_t>(res, sizeof(out_resp->str) - 1);
    out_resp->id = req.id;
}

}  // namespace intel_hda
}  // namespace audio
