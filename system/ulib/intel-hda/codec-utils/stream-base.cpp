// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <audio-proto-utils/format-utils.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>

#include <audio-proto/audio-proto.h>
#include <intel-hda/codec-utils/codec-driver-base.h>
#include <intel-hda/codec-utils/stream-base.h>
#include <intel-hda/utils/intel-hda-proto.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {
namespace codecs {

zx_protocol_device_t IntelHDAStreamBase::STREAM_DEVICE_THUNKS = {
    .version      = DEVICE_OPS_VERSION,
    .get_protocol = nullptr,
    .open         = nullptr,
    .open_at      = nullptr,
    .close        = nullptr,
    .unbind       = nullptr,
    .release      = nullptr,
    .read         = nullptr,
    .write        = nullptr,
    .get_size     = nullptr,
    .ioctl        = [](void* ctx, uint32_t op,
                       const void* in_buf, size_t in_len,
                       void* out_buf, size_t out_len, size_t* out_actual) -> zx_status_t {
                        return reinterpret_cast<IntelHDAStreamBase*>(ctx)->
                            DeviceIoctl(op, in_buf, in_len, out_buf, out_len, out_actual);
                    },
    .suspend      = nullptr,
    .resume       = nullptr,
    .rxrpc        = nullptr,
};

IntelHDAStreamBase::IntelHDAStreamBase(uint32_t id, bool is_input)
    : id_(id),
      is_input_(is_input) {
    snprintf(dev_name_, sizeof(dev_name_), "%s-stream-%03u", is_input_ ? "input" : "output", id_);
    default_domain_ = dispatcher::ExecutionDomain::Create();
}

IntelHDAStreamBase::~IntelHDAStreamBase() {
}

void IntelHDAStreamBase::PrintDebugPrefix() const {
    printf("[%s] ", dev_name_);
}

void IntelHDAStreamBase::SetPersistentUniqueId(const audio_stream_unique_id_t& id) {
    fbl::AutoLock obj_lock(&obj_lock_);
    persistent_unique_id_ = id;
}

zx_status_t IntelHDAStreamBase::Activate(fbl::RefPtr<IntelHDACodecDriverBase>&& parent_codec,
                                         const fbl::RefPtr<dispatcher::Channel>& codec_channel) {
    ZX_DEBUG_ASSERT(codec_channel != nullptr);

    fbl::AutoLock obj_lock(&obj_lock_);
    if (is_active() || (codec_channel_ != nullptr) || (default_domain_ == nullptr))
        return ZX_ERR_BAD_STATE;

    ZX_DEBUG_ASSERT(parent_codec_ == nullptr);

    // Remember our parent codec and our codec channel.  If something goes wrong
    // during activation, make sure we let go of these references.
    //
    // Note; the cleanup lambda needs to have thread analysis turned off because
    // the compiler is not quite smart enough to figure out that the obj_lock
    // AutoLock will destruct (and release the lock) after the AutoCall runs,
    // and that the AutoCall will never leave this scope.
    auto cleanup = fbl::MakeAutoCall([this]() __TA_NO_THREAD_SAFETY_ANALYSIS {
        parent_codec_.reset();
        codec_channel_.reset();
    });
    parent_codec_  = fbl::move(parent_codec);
    codec_channel_ = codec_channel;

    // Allow our implementation to send its initial stream setup commands to the
    // codec.
    zx_status_t res = OnActivateLocked();
    if (res != ZX_OK)
        return res;

    // Request a DMA context
    ihda_proto::RequestStreamReq req;

    req.hdr.transaction_id = id();
    req.hdr.cmd = IHDA_CODEC_REQUEST_STREAM;
    req.input  = is_input();

    res = codec_channel_->Write(&req, sizeof(req));
    if (res != ZX_OK)
        return res;

    cleanup.cancel();
    return ZX_OK;
}

void IntelHDAStreamBase::Deactivate() {
    {
        fbl::AutoLock obj_lock(&obj_lock_);
        DEBUG_LOG("Deactivating stream\n");

        // Let go of any unsolicited stream tags we may be holding.
        if (unsol_tag_count_) {
            ZX_DEBUG_ASSERT(parent_codec_ != nullptr);
            parent_codec_->ReleaseAllUnsolTags(*this);
            unsol_tag_count_ = 0;
        }

        // Clear out our parent_codec_ pointer.  This will mark us as being
        // inactive and prevent any new connections from being made.
        parent_codec_.reset();

        // We should already have been removed from our codec's active stream list
        // at this point.
        ZX_DEBUG_ASSERT(!this->InContainer());
    }

    default_domain_->Deactivate();

    {
        fbl::AutoLock obj_lock(&obj_lock_);
        ZX_DEBUG_ASSERT(stream_channel_ == nullptr);

        // Allow our implementation to send the commands needed to tear down the
        // widgets which make up this stream.
        OnDeactivateLocked();

        // If we have been given a DMA stream by the IHDA controller, attempt to
        // return it now.
        if ((dma_stream_id_ != IHDA_INVALID_STREAM_ID) && (codec_channel_ != nullptr)) {
            ihda_proto::ReleaseStreamReq req;

            req.hdr.transaction_id = id();
            req.hdr.cmd = IHDA_CODEC_RELEASE_STREAM_NOACK,
            req.stream_id = dma_stream_id_;

            codec_channel_->Write(&req, sizeof(req));

            dma_stream_id_  = IHDA_INVALID_STREAM_ID;
            dma_stream_tag_ = IHDA_INVALID_STREAM_TAG;
        }

        // Let go of our reference to the codec device channel.
        codec_channel_ = nullptr;

        // If we had published a device node, remove it now.
        if (parent_device_ != nullptr) {
            device_remove(stream_device_);
            parent_device_ = nullptr;
        }
    }

    DEBUG_LOG("Deactivate complete\n");
}

zx_status_t IntelHDAStreamBase::PublishDeviceLocked() {
    if (!is_active() || (parent_device_ != nullptr)) return ZX_ERR_BAD_STATE;
    ZX_DEBUG_ASSERT(parent_codec_ != nullptr);

    // Initialize our device and fill out the protocol hooks
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = dev_name_;
    args.ctx = this;
    args.ops = &STREAM_DEVICE_THUNKS;
    args.proto_id = (is_input() ? ZX_PROTOCOL_AUDIO_INPUT : ZX_PROTOCOL_AUDIO_OUTPUT);

    // Publish the device.
    zx_status_t res = device_add(parent_codec_->codec_device(), &args, &stream_device_);
    if (res != ZX_OK) {
        LOG("Failed to add stream device for \"%s\" (res %d)\n", dev_name_, res);
        return res;
    }

    // Record our parent.
    parent_device_ = parent_codec_->codec_device();

    return ZX_OK;
}

zx_status_t IntelHDAStreamBase::ProcessResponse(const CodecResponse& resp) {
    fbl::AutoLock obj_lock(&obj_lock_);

    if (!is_active()) {
        DEBUG_LOG("Ignoring codec response (0x%08x, 0x%08x) for inactive stream id %u\n",
                resp.data, resp.data_ex, id());
        return ZX_OK;
    }

    return resp.unsolicited()
        ? OnUnsolicitedResponseLocked(resp)
        : OnSolicitedResponseLocked(resp);
}

zx_status_t IntelHDAStreamBase::ProcessRequestStream(const ihda_proto::RequestStreamResp& resp) {
    fbl::AutoLock obj_lock(&obj_lock_);
    zx_status_t res;

    if (!is_active()) return ZX_ERR_BAD_STATE;

    res = SetDMAStreamLocked(resp.stream_id, resp.stream_tag);
    if (res != ZX_OK) {
        // TODO(johngro) : If we failed to set the DMA info because this stream
        // is in the process of shutting down, we really should return the
        // stream to the controller.
        //
        // Right now, we are going to return an error which will cause the lower
        // level infrastructure to close the codec device channel.  This will
        // prevent a leak (the core controller driver will re-claim the stream),
        // but it will also ruin all of the other streams in this codec are
        // going to end up being destroyed.  For simple codec driver who never
        // change stream topology, this is probably fine, but for more
        // complicated ones it probably is not.
        return res;
    }

    return OnDMAAssignedLocked();
}

zx_status_t IntelHDAStreamBase::ProcessSetStreamFmt(const ihda_proto::SetStreamFmtResp& codec_resp,
                                                    zx::channel&& ring_buffer_channel) {
    ZX_DEBUG_ASSERT(ring_buffer_channel.is_valid());

    fbl::AutoLock obj_lock(&obj_lock_);
    audio_proto::StreamSetFmtResp resp = { };
    zx_status_t res = ZX_OK;

    // Are we shutting down?
    if (!is_active()) return ZX_ERR_BAD_STATE;

    // If we don't have a set format operation in flight, or the stream channel
    // has been closed, this set format operation has been canceled.  Do not
    // return an error up the stack; we don't want to close the connection to
    // our codec device.
    if ((set_format_tid_ == AUDIO_INVALID_TRANSACTION_ID) ||
        (stream_channel_ == nullptr))
        goto finished;

    // Let the implementation send the commands required to finish changing the
    // stream format.
    res = FinishChangeStreamFormatLocked(encoded_fmt_);
    if (res != ZX_OK) {
        DEBUG_LOG("Failed to finish set format (enc fmt 0x%04hx res %d)\n", encoded_fmt_, res);
        goto finished;
    }

    // Respond to the caller, transferring the DMA handle back in the process.
    resp.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;
    resp.hdr.transaction_id = set_format_tid_;
    resp.result = ZX_OK;
    resp.external_delay_nsec = 0;   // report his properly based on the codec path delay.
    res = stream_channel_->Write(&resp, sizeof(resp), fbl::move(ring_buffer_channel));

finished:
    // Something went fatally wrong when trying to send the result back to the
    // caller.  Close the stream channel.
    if ((res != ZX_OK) && (stream_channel_ != nullptr)) {
        OnChannelDeactivateLocked(*stream_channel_);
        stream_channel_->Deactivate();
        stream_channel_ = nullptr;
    }

    // One way or the other, this set format operation is finished.  Clear out
    // the in-flight transaction ID
    set_format_tid_ = AUDIO_INVALID_TRANSACTION_ID;

    return ZX_OK;
}

// TODO(johngro) : Refactor this; this sample_format of parameters is 95% the same
// between both the codec and stream base classes.
zx_status_t IntelHDAStreamBase::SendCodecCommandLocked(uint16_t  nid,
                                                       CodecVerb verb,
                                                       Ack       do_ack) {
    if (codec_channel_ == nullptr) return ZX_ERR_BAD_STATE;

    ihda_codec_send_corb_cmd_req_t cmd;

    cmd.hdr.cmd = (do_ack == Ack::NO) ? IHDA_CODEC_SEND_CORB_CMD_NOACK : IHDA_CODEC_SEND_CORB_CMD;
    cmd.hdr.transaction_id = id();
    cmd.nid = nid;
    cmd.verb = verb.val;

    return codec_channel_->Write(&cmd, sizeof(cmd));
}

zx_status_t IntelHDAStreamBase::SetDMAStreamLocked(uint16_t id, uint8_t tag) {
    if ((id == IHDA_INVALID_STREAM_ID) || (tag == IHDA_INVALID_STREAM_TAG))
        return ZX_ERR_INVALID_ARGS;

    ZX_DEBUG_ASSERT((dma_stream_id_  == IHDA_INVALID_STREAM_ID) ==
                 (dma_stream_tag_ == IHDA_INVALID_STREAM_TAG));

    if (dma_stream_id_ != IHDA_INVALID_STREAM_ID)
        return ZX_ERR_BAD_STATE;

    dma_stream_id_  = id;
    dma_stream_tag_ = tag;

    return ZX_OK;
}

zx_status_t IntelHDAStreamBase::DeviceIoctl(uint32_t op,
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

    fbl::AutoLock obj_lock(&obj_lock_);

    // Do not allow any new connections if we are in the process of shutting down
    if (!is_active())
        return ZX_ERR_BAD_STATE;

    // For now, block new connections if we currently have no privileged
    // connection, but there is a SetFormat request in flight to the codec
    // driver.  We are trying to avoid the following sequence...
    //
    // 1) A privileged connection starts a set format.
    // 2) After we ask the controller to set the format, our privileged channel
    //    is closed.
    // 3) A new user connects.
    // 4) The response to the first client's request arrives and gets sent
    //    to the second client.
    // 5) Confusion ensues.
    //
    // Denying new connections while the old request is in flight avoids this,
    // but is generally a terrible solution.  What we should really do is tag
    // the requests to the codec driver with a unique ID which we can use to
    // filter responses.  One option might be to split the transaction ID so
    // that a portion of the TID is used for stream routing, while another
    // portion is used for requests like this.
    bool privileged = (stream_channel_ == nullptr);
    if (privileged && (set_format_tid_ != AUDIO_INVALID_TRANSACTION_ID))
        return ZX_ERR_SHOULD_WAIT;

    // Attempt to allocate a new driver channel and bind it to us.  If we don't
    // already have a stream_channel_, flag this channel is the privileged
    // connection (The connection which is allowed to do things like change
    // formats).
    auto channel = dispatcher::Channel::Create();
    if (channel == nullptr)
        return ZX_ERR_NO_MEMORY;

    dispatcher::Channel::ProcessHandler phandler(
    [stream = fbl::WrapRefPtr(this), privileged](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
        return stream->ProcessClientRequest(channel, privileged);
    });

    dispatcher::Channel::ChannelClosedHandler chandler(
    [stream = fbl::WrapRefPtr(this), privileged](const dispatcher::Channel* channel) -> void {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, stream->default_domain_);
        stream->ProcessClientDeactivate(channel, privileged);
    });

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

zx_status_t IntelHDAStreamBase::DoGetStreamFormatsLocked(dispatcher::Channel* channel,
                                                         bool privileged,
                                                         const audio_proto::StreamGetFmtsReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    size_t formats_sent = 0;
    audio_proto::StreamGetFmtsResp resp = { };

    if (supported_formats_.size() > fbl::numeric_limits<uint16_t>::max()) {
        LOG("Too many formats (%zu) to send during AUDIO_STREAM_CMD_GET_FORMATS request!\n",
            supported_formats_.size());
        return ZX_ERR_INTERNAL;
    }

    resp.hdr = req.hdr;
    resp.format_range_count = static_cast<uint16_t>(supported_formats_.size());

    do {
        size_t todo, payload_sz, __UNUSED to_send;
        zx_status_t res;

        todo = fbl::min<size_t>(supported_formats_.size() - formats_sent,
                                   AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
        payload_sz = sizeof(resp.format_ranges[0]) * todo;
        to_send = offsetof(audio_proto::StreamGetFmtsResp, format_ranges) + payload_sz;

        resp.first_format_range_ndx = static_cast<uint16_t>(formats_sent);
        ::memcpy(resp.format_ranges, supported_formats_.get() + formats_sent, payload_sz);

        res = channel->Write(&resp, sizeof(resp));
        if (res != ZX_OK) {
            DEBUG_LOG("Failed to send get stream formats response (res %d)\n", res);
            return res;
        }

        formats_sent += todo;
    } while (formats_sent < supported_formats_.size());

    return ZX_OK;
}

zx_status_t IntelHDAStreamBase::DoSetStreamFormatLocked(dispatcher::Channel* channel,
                                                        bool privileged,
                                                        const audio_proto::StreamSetFmtReq& fmt) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    ihda_proto::SetStreamFmtReq req;
    uint16_t encoded_fmt;
    bool found_supported_format = false;
    zx_status_t res;

    // Check to make sure that this channel is permitted to change formats.
    if (!privileged) {
        res = ZX_ERR_ACCESS_DENIED;
        goto send_fail_response;
    }

    // If we don't have a DMA stream assigned to us, or there is already a set
    // format operation in flight, we cannot proceed.
    if ((dma_stream_id_  == IHDA_INVALID_STREAM_ID) ||
        (set_format_tid_ != AUDIO_INVALID_TRANSACTION_ID)) {
        res = ZX_ERR_BAD_STATE;
        goto send_fail_response;
    }

    // Is the requested format compatible with this stream?
    for (const auto& format_range : supported_formats_) {
        found_supported_format = utils::FormatIsCompatible(fmt.frames_per_second,
                                                           fmt.channels,
                                                           fmt.sample_format,
                                                           format_range);
        if (found_supported_format)
            break;
    }

    if (!found_supported_format) {
        res = ZX_ERR_NOT_SUPPORTED;
        goto send_fail_response;
    }

    // The upper level stream told us that they support this format, we had
    // better be able to encode it into an IHDA format specifier.
    res = EncodeStreamFormat(fmt, &encoded_fmt);
    if (res != ZX_OK) {
        DEBUG_LOG("Failed to encode stream format %u:%hu:%s (res %d)\n",
                  fmt.frames_per_second,
                  fmt.channels,
                  audio_proto::SampleFormatToString(fmt.sample_format),
                  res);
        goto send_fail_response;
    }

    // Let our implementation start the process of a format change.  This gives
    // it a chance to check the format for compatibility, and send commands to
    // quiesce the converters and amplifiers if it approves of the format.
    res = BeginChangeStreamFormatLocked(fmt);
    if (res != ZX_OK) {
        DEBUG_LOG("Stream impl rejected stream format %u:%hu:%s (res %d)\n",
                  fmt.frames_per_second,
                  fmt.channels,
                  audio_proto::SampleFormatToString(fmt.sample_format),
                  res);
        goto send_fail_response;
    }

    // Set the format of DMA stream.  This will stop any stream in progress and
    // close any connection to its clients.  At this point, all of our checks
    // are done and we expect success.  If anything goes wrong, consider it to
    // be a fatal internal error and close the connection to our client by
    // returning an error.
    ZX_DEBUG_ASSERT(codec_channel_ != nullptr);
    req.hdr.cmd = IHDA_CODEC_SET_STREAM_FORMAT;
    req.hdr.transaction_id = id();
    req.stream_id = dma_stream_id_;
    req.format = encoded_fmt;
    res = codec_channel_->Write(&req, sizeof(req));
    if (res != ZX_OK) {
        DEBUG_LOG("Failed to write set stream format %u:%hu:%s to codec channel (res %d)\n",
                  fmt.frames_per_second,
                  fmt.channels,
                  audio_proto::SampleFormatToString(fmt.sample_format),
                  res);
        return res;
    }

    // Success!  Record the transaction ID of the request.  It indicates that the
    // format change is in progress, and will be needed to send the final
    // response back to the caller.
    set_format_tid_ = fmt.hdr.transaction_id;
    encoded_fmt_    = encoded_fmt;
    return ZX_OK;

send_fail_response:
    audio_proto::StreamSetFmtResp resp = { };
    resp.hdr = fmt.hdr;
    resp.result = res;

    ZX_DEBUG_ASSERT(channel != nullptr);
    res = channel->Write(&resp, sizeof(resp));
    if (res != ZX_OK)
        DEBUG_LOG("Failing to write %zu bytes in response (res %d)\n", sizeof(resp), res);
    return res;
}

zx_status_t IntelHDAStreamBase::DoGetGainLocked(dispatcher::Channel* channel,
                                                bool privileged,
                                                const audio_proto::GetGainReq& req) {
    // Fill out the response header, then let the stream implementation fill out
    // the payload.
    audio_proto::GetGainResp resp = { };
    resp.hdr = req.hdr;
    OnGetGainLocked(&resp);

    ZX_DEBUG_ASSERT(channel != nullptr);
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDAStreamBase::DoSetGainLocked(dispatcher::Channel* channel,
                                                bool privileged,
                                                const audio_proto::SetGainReq& req) {
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK) {
        OnSetGainLocked(req, nullptr);
        return ZX_OK;
    }

    // Fill out the response header, then let the stream implementation fill out
    // the payload.
    audio_proto::SetGainResp resp = { };
    resp.hdr = req.hdr;
    OnSetGainLocked(req, &resp);

    ZX_DEBUG_ASSERT(channel != nullptr);
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDAStreamBase::DoPlugDetectLocked(dispatcher::Channel* channel,
                                                   bool privileged,
                                                   const audio_proto::PlugDetectReq& req) {
    if (req.hdr.cmd & AUDIO_FLAG_NO_ACK) {
        OnPlugDetectLocked(channel, req, nullptr);
        return ZX_OK;
    }

    // Fill out the response header, then let the stream implementation fill out
    // the payload.
    audio_proto::PlugDetectResp resp = { };
    resp.hdr = req.hdr;
    OnPlugDetectLocked(channel, req, &resp);

    ZX_DEBUG_ASSERT(channel != nullptr);
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDAStreamBase::DoGetUniqueIdLocked(dispatcher::Channel* channel,
                                                    bool privileged,
                                                    const audio_proto::GetUniqueIdReq& req) {
    audio_proto::GetUniqueIdResp resp = { };

    resp.hdr = req.hdr;
    resp.unique_id = persistent_unique_id_;

    ZX_DEBUG_ASSERT(channel != nullptr);
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDAStreamBase::DoGetStringLocked(dispatcher::Channel* channel,
                                                  bool privileged,
                                                  const audio_proto::GetStringReq& req) {
    // Fill out the response header, then let the stream implementation fill out
    // the payload.
    audio_proto::GetStringResp resp = { };
    resp.hdr = req.hdr;
    resp.id = req.id;
    OnGetStringLocked(req, &resp);

    ZX_DEBUG_ASSERT(channel != nullptr);
    return channel->Write(&resp, sizeof(resp));
}

#define HANDLE_REQ(_ioctl, _payload, _handler, _allow_noack)    \
case _ioctl:                                                    \
    if (req_size != sizeof(req._payload)) {                     \
        DEBUG_LOG("Bad " #_ioctl                                \
                  " response length (%u != %zu)\n",             \
                  req_size, sizeof(req._payload));              \
        return ZX_ERR_INVALID_ARGS;                                \
    }                                                           \
    if (!_allow_noack && (req.hdr.cmd & AUDIO_FLAG_NO_ACK)) {  \
        DEBUG_LOG("NO_ACK flag not allowed for " #_ioctl "\n"); \
        return ZX_ERR_INVALID_ARGS;                                \
    }                                                           \
    return _handler(channel, privileged, req._payload);
zx_status_t IntelHDAStreamBase::ProcessClientRequest(dispatcher::Channel* channel,
                                                     bool privileged) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock obj_lock(&obj_lock_);

    // If we have lost our connection to the codec device, or are in the process
    // of shutting down, there is nothing further we can do.  Fail the request
    // and close the connection to the caller.
    if (!is_active() || (codec_channel_ == nullptr))
        return ZX_ERR_BAD_STATE;

    union {
        audio_proto::CmdHdr           hdr;
        audio_proto::StreamGetFmtsReq get_formats;
        audio_proto::StreamSetFmtReq  set_format;
        audio_proto::GetGainReq       get_gain;
        audio_proto::SetGainReq       set_gain;
        audio_proto::PlugDetectReq    plug_detect;
        audio_proto::GetUniqueIdReq   get_unique_id;
        audio_proto::GetStringReq     get_string;
        // TODO(johngro) : add more commands here
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
        HANDLE_REQ(AUDIO_STREAM_CMD_GET_FORMATS,   get_formats,   DoGetStreamFormatsLocked, false);
        HANDLE_REQ(AUDIO_STREAM_CMD_SET_FORMAT,    set_format,    DoSetStreamFormatLocked,  false);
        HANDLE_REQ(AUDIO_STREAM_CMD_GET_GAIN,      get_gain,      DoGetGainLocked,          false);
        HANDLE_REQ(AUDIO_STREAM_CMD_SET_GAIN,      set_gain,      DoSetGainLocked,          true);
        HANDLE_REQ(AUDIO_STREAM_CMD_PLUG_DETECT,   plug_detect,   DoPlugDetectLocked,       true);
        HANDLE_REQ(AUDIO_STREAM_CMD_GET_UNIQUE_ID, get_unique_id, DoGetUniqueIdLocked,      false);
        HANDLE_REQ(AUDIO_STREAM_CMD_GET_STRING,    get_string,    DoGetStringLocked,        false);
        default:
            DEBUG_LOG("Unrecognized stream command 0x%04x\n", req.hdr.cmd);
            return ZX_ERR_NOT_SUPPORTED;
    }
}
#undef HANDLE_REQ

void IntelHDAStreamBase::ProcessClientDeactivate(const dispatcher::Channel* channel,
                                                 bool privileged) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    fbl::AutoLock obj_lock(&obj_lock_);

    // Let our subclass know that this channel is going away.
    OnChannelDeactivateLocked(*channel);

    // Is this the privileged stream channel?
    if (privileged) {
        ZX_DEBUG_ASSERT(channel == stream_channel_.get());
        stream_channel_.reset();
    }
}

zx_status_t IntelHDAStreamBase::AllocateUnsolTagLocked(uint8_t* out_tag) {
    if (!parent_codec_)
        return ZX_ERR_BAD_STATE;

    zx_status_t res = parent_codec_->AllocateUnsolTag(*this, out_tag);
    if (res == ZX_OK)
        unsol_tag_count_++;

    return res;
}

void IntelHDAStreamBase::ReleaseUnsolTagLocked(uint8_t tag) {
    ZX_DEBUG_ASSERT(unsol_tag_count_ > 0);
    ZX_DEBUG_ASSERT(parent_codec_ != nullptr);
    parent_codec_->ReleaseUnsolTag(*this, tag);
    unsol_tag_count_--;
}

// TODO(johngro) : Move this out to a utils library?
#define MAKE_RATE(_rate, _base, _mult, _div) \
    { .rate = _rate, .encoded = (_base << 14) | ((_mult - 1) << 11) | ((_div - 1) << 8) }
zx_status_t IntelHDAStreamBase::EncodeStreamFormat(const audio_proto::StreamSetFmtReq& fmt,
                                                   uint16_t* encoded_fmt_out) {
    ZX_DEBUG_ASSERT(encoded_fmt_out != nullptr);

    // See section 3.7.1
    // Start with the channel count.  Intel HDA DMA streams support between 1
    // and 16 channels.
    uint32_t channels = fmt.channels - 1;
    if ((fmt.channels < 1) || (fmt.channels > 16))
        return ZX_ERR_NOT_SUPPORTED;

    // Next determine the bit sample_format format
    uint32_t bits;
    switch (fmt.sample_format) {
    case AUDIO_SAMPLE_FORMAT_8BIT:        bits = 0; break;
    case AUDIO_SAMPLE_FORMAT_16BIT:       bits = 1; break;
    case AUDIO_SAMPLE_FORMAT_20BIT_IN32:  bits = 2; break;
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:  bits = 3; break;
    case AUDIO_SAMPLE_FORMAT_32BIT:
    case AUDIO_SAMPLE_FORMAT_32BIT_FLOAT: bits = 4; break;
    default: return ZX_ERR_NOT_SUPPORTED;
    }

    // Finally, determine the base frame rate, as well as the multiplier and
    // divisor.
    static const struct {
        uint32_t rate;
        uint32_t encoded;
    } RATE_ENCODINGS[] = {
        // 48 KHz family
        MAKE_RATE(  6000, 0, 1, 8),
        MAKE_RATE(  8000, 0, 1, 6),
        MAKE_RATE(  9600, 0, 1, 5),
        MAKE_RATE( 16000, 0, 1, 3),
        MAKE_RATE( 24000, 0, 1, 2),
        MAKE_RATE( 32000, 0, 2, 3),
        MAKE_RATE( 48000, 0, 1, 1),
        MAKE_RATE( 96000, 0, 2, 1),
        MAKE_RATE(144000, 0, 3, 1),
        MAKE_RATE(192000, 0, 4, 1),
        // 44.1 KHz family
        MAKE_RATE( 11025, 1, 1, 4),
        MAKE_RATE( 22050, 1, 1, 2),
        MAKE_RATE( 44100, 1, 1, 1),
        MAKE_RATE( 88200, 1, 2, 1),
        MAKE_RATE(176400, 1, 4, 1),
    };

    for (const auto& r : RATE_ENCODINGS) {
        if (r.rate == fmt.frames_per_second) {
            *encoded_fmt_out = static_cast<uint16_t>(r.encoded | channels | (bits << 4));
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_SUPPORTED;
}
#undef MAKE_RATE

/////////////////////////////////////////////////////////////////////
//
// Default handlers
//
/////////////////////////////////////////////////////////////////////
zx_status_t IntelHDAStreamBase::OnActivateLocked() {
    return ZX_OK;
}

void IntelHDAStreamBase::OnDeactivateLocked() { }
void IntelHDAStreamBase::OnChannelDeactivateLocked(const dispatcher::Channel& channel) { }

zx_status_t IntelHDAStreamBase::OnDMAAssignedLocked() {
    return PublishDeviceLocked();
}

zx_status_t IntelHDAStreamBase::OnSolicitedResponseLocked(const CodecResponse& resp) {
    return ZX_OK;
}

zx_status_t IntelHDAStreamBase::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
    return ZX_OK;
}

zx_status_t IntelHDAStreamBase::BeginChangeStreamFormatLocked(
        const audio_proto::StreamSetFmtReq& fmt) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelHDAStreamBase::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
    return ZX_ERR_INTERNAL;
}

void IntelHDAStreamBase::OnGetGainLocked(audio_proto::GetGainResp* out_resp) {
    ZX_DEBUG_ASSERT(out_resp != nullptr);

    // By default we claim to have a fixed, un-mute-able gain stage.
    out_resp->cur_mute  = false;
    out_resp->cur_agc   = false;
    out_resp->cur_gain  = 0.0;

    out_resp->can_mute  = false;
    out_resp->can_agc   = false;
    out_resp->min_gain  = 0.0;
    out_resp->max_gain  = 0.0;
    out_resp->gain_step = 0.0;
}

void IntelHDAStreamBase::OnSetGainLocked(const audio_proto::SetGainReq& req,
                                         audio_proto::SetGainResp* out_resp) {
    // Nothing to do if no response is expected.
    if (out_resp == nullptr) {
        ZX_DEBUG_ASSERT(req.hdr.cmd & AUDIO_FLAG_NO_ACK);
        return;
    }

    bool illegal_mute = (req.flags & AUDIO_SGF_MUTE_VALID) && (req.flags & AUDIO_SGF_MUTE);
    bool illegal_agc  = (req.flags & AUDIO_SGF_AGC_VALID)  && (req.flags & AUDIO_SGF_AGC);
    bool illegal_gain = (req.flags & AUDIO_SGF_GAIN_VALID) && (req.gain != 0.0f);

    out_resp->cur_mute = false;
    out_resp->cur_gain = 0.0;
    out_resp->result   = (illegal_mute || illegal_agc || illegal_gain)
                       ? ZX_ERR_INVALID_ARGS
                       : ZX_OK;
}

void IntelHDAStreamBase::OnPlugDetectLocked(dispatcher::Channel* response_channel,
                                            const audio_proto::PlugDetectReq& req,
                                            audio_proto::PlugDetectResp* out_resp) {
    // Nothing to do if no response is expected.
    if (out_resp == nullptr) {
        ZX_DEBUG_ASSERT(req.hdr.cmd & AUDIO_FLAG_NO_ACK);
        return;
    }

    ZX_DEBUG_ASSERT(parent_codec_ != nullptr);
    out_resp->flags = static_cast<audio_pd_notify_flags_t>(AUDIO_PDNF_HARDWIRED |
                                                            AUDIO_PDNF_PLUGGED);
    out_resp->plug_state_time = parent_codec_->create_time();
}

void IntelHDAStreamBase::OnGetStringLocked(const audio_proto::GetStringReq& req,
                                           audio_proto::GetStringResp* out_resp) {
    ZX_DEBUG_ASSERT(out_resp);

    switch (req.id) {
        case AUDIO_STREAM_STR_ID_MANUFACTURER:
        case AUDIO_STREAM_STR_ID_PRODUCT: {
            int res = snprintf(reinterpret_cast<char*>(out_resp->str),
                               sizeof(out_resp->str), "<unknown>");
            ZX_DEBUG_ASSERT(res >= 0);  // there should be no way for snprintf to fail here.
            out_resp->strlen = fbl::min<uint32_t>(res, sizeof(out_resp->str) - 1);
            out_resp->result = ZX_OK;
            break;
        }

        default:
            out_resp->strlen = 0;
            out_resp->result = ZX_ERR_NOT_FOUND;
            break;
    }
}

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
