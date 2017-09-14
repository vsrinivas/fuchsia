// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <stdio.h>

#include "drivers/audio/intel-hda/utils/codec-commands.h"

#include "debug-logging.h"
#include "intel-hda-codec.h"
#include "intel-hda-controller.h"
#include "intel-hda-stream.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

constexpr size_t IntelHDACodec::PROP_PROTOCOL;
constexpr size_t IntelHDACodec::PROP_VID;
constexpr size_t IntelHDACodec::PROP_DID;
constexpr size_t IntelHDACodec::PROP_MAJOR_REV;
constexpr size_t IntelHDACodec::PROP_MINOR_REV;
constexpr size_t IntelHDACodec::PROP_VENDOR_REV;
constexpr size_t IntelHDACodec::PROP_VENDOR_STEP;
constexpr size_t IntelHDACodec::PROP_COUNT;

// Special ID we use to distinguish the codec driver channel from all of the
// other channels we manage.
constexpr uintptr_t DRIVER_CHANNEL_ID = fbl::numeric_limits<uintptr_t>::max();

#define SET_DEVICE_PROP(_prop, _value) do { \
    static_assert(PROP_##_prop < countof(dev_props_), "Invalid Device Property ID"); \
    dev_props_[PROP_##_prop].id = BIND_IHDA_CODEC_##_prop; \
    dev_props_[PROP_##_prop].value = (_value); \
} while (false)

IntelHDACodec::ProbeCommandListEntry IntelHDACodec::PROBE_COMMANDS[] = {
    { .verb = GET_PARAM(CodecParam::VENDOR_ID),   .parse = &IntelHDACodec::ParseVidDid },
    { .verb = GET_PARAM(CodecParam::REVISION_ID), .parse = &IntelHDACodec::ParseRevisionId },
};

void IntelHDACodec::PrintDebugPrefix() const {
    printf("[%s:%u] ", controller_.dev_name(), codec_id_);
}

#define DEV (static_cast<IntelHDACodec*>(ctx))
zx_protocol_device_t IntelHDACodec::CODEC_DEVICE_THUNKS = {
    .version      = DEVICE_OPS_VERSION,
    .get_protocol = nullptr,
    .open         = nullptr,
    .open_at      = nullptr,
    .close        = nullptr,
    .unbind       = nullptr,
    .release      = nullptr,
    .read         = nullptr,
    .write        = nullptr,
    .iotxn_queue  = nullptr,
    .get_size     = nullptr,
    .ioctl        = [](void* ctx,
                      uint32_t op,
                      const void* in_buf,
                      size_t in_len,
                      void* out_buf,
                      size_t out_len,
                      size_t* out_actual) -> zx_status_t {
                        return reinterpret_cast<IntelHDACodec*>(ctx)->
                            DeviceIoctl(op, in_buf, in_len, out_buf, out_len, out_actual);
                   },
    .suspend      = nullptr,
    .resume       = nullptr,
};

ihda_codec_protocol_ops_t IntelHDACodec::CODEC_PROTO_THUNKS = {
    .get_driver_channel = [](void* ctx, zx_handle_t* channel_out) -> zx_status_t
    {
        ZX_DEBUG_ASSERT(ctx);
        return DEV->CodecGetDispatcherChannel(channel_out);
    },
};
#undef DEV

IntelHDACodec::IntelHDACodec(IntelHDAController& controller, uint8_t codec_id)
    : controller_(controller),
      codec_id_(codec_id) {
    ::memset(&dev_props_, 0, sizeof(dev_props_));
    dev_props_[PROP_PROTOCOL].id    = BIND_PROTOCOL;
    dev_props_[PROP_PROTOCOL].value = ZX_PROTOCOL_IHDA_CODEC;
}

fbl::RefPtr<IntelHDACodec> IntelHDACodec::Create(IntelHDAController& controller,
                                                      uint8_t codec_id) {
    ZX_DEBUG_ASSERT(codec_id < HDA_MAX_CODECS);
    return fbl::AdoptRef(new IntelHDACodec(controller, codec_id));
}

zx_status_t IntelHDACodec::Startup() {
    ZX_DEBUG_ASSERT(state_ == State::PROBING);

    for (size_t i = 0; i < countof(PROBE_COMMANDS); ++i) {
        CodecCommand cmd(id(), 0u, PROBE_COMMANDS[i].verb);
        auto job = CodecCmdJobAllocator::New(cmd);

        if (job == nullptr) {
            LOG("Failed to allocate job during initial codec probe!\n");
            return ZX_ERR_NO_MEMORY;
        }

        zx_status_t res = controller_.QueueCodecCmd(fbl::move(job));
        if (res != ZX_OK) {
            LOG("Failed to queue job (res = %d) during initial codec probe!\n", res);
            return res;
        }
    }

    return ZX_OK;
}

void IntelHDACodec::SendCORBResponse(const fbl::RefPtr<DispatcherChannel>& channel,
                                     const CodecResponse& resp,
                                     uint32_t transaction_id) {
    ZX_DEBUG_ASSERT(channel != nullptr);
    ihda_codec_send_corb_cmd_resp_t payload;

    payload.hdr.transaction_id = transaction_id;
    payload.hdr.cmd = IHDA_CODEC_SEND_CORB_CMD;
    payload.data    = resp.data;
    payload.data_ex = resp.data_ex;

    zx_status_t res = channel->Write(&payload, sizeof(payload));
    if (res != ZX_OK) {
        DEBUG_LOG("Error writing CORB response (%08x, %08x) res = %d\n",
                  resp.data, resp.data_ex, res);
        channel->Deactivate(true);
    }
}

void IntelHDACodec::ProcessSolicitedResponse(const CodecResponse& resp,
                                             fbl::unique_ptr<CodecCmdJob>&& job) {
    if (state_ == State::PROBING) {
        // Are we still in the PROBING stage of things?  If so, this job should
        // have no response channel assigned to it, and we should still be
        // waiting for responses from the codec to complete the initial probe.
        ZX_DEBUG_ASSERT(probe_rx_ndx_ < countof(PROBE_COMMANDS));

        const auto& cmd = PROBE_COMMANDS[probe_rx_ndx_];

        zx_status_t res = (this->*cmd.parse)(resp);
        if (res == ZX_OK) {
            ++probe_rx_ndx_;
        } else {
            LOG("Error parsing solicited response during codec probe! (data %08x)\n",
                    resp.data);

            // TODO(johngro) : shutdown and cleanup somehow.
            state_ = State::FATAL_ERROR;
        }
    } else if (job->response_channel() != nullptr) {
        VERBOSE_LOG("Sending solicited response [%08x, %08x] to channel %p\n",
                    resp.data, resp.data_ex, job->response_channel().get());

        // Does this job have a response channel?  If so, attempt to send the
        // response back on the channel (assuming that it is still open).
        SendCORBResponse(job->response_channel(), resp, job->transaction_id());
    }
}

void IntelHDACodec::ProcessUnsolicitedResponse(const CodecResponse& resp) {
    // If we still have a channel to our codec driver, grab a reference to it
    // and send the unsolicited response to it.
    fbl::RefPtr<DispatcherChannel> codec_driver_channel;
    {
        fbl::AutoLock codec_driver_channel_lock(&codec_driver_channel_lock_);
        codec_driver_channel = codec_driver_channel_;
    }

    if (codec_driver_channel)
        SendCORBResponse(codec_driver_channel, resp);
}

void IntelHDACodec::ProcessWakeupEvt() {
    // TODO(johngro) : handle wakeup events.  Wakeup events are delivered for
    // two reasons.
    //
    // 1) The codec had brought the controller out of a low power state for some
    //    reason.
    // 2) The codec has been hot-unplugged.
    //
    // Currently, we support neither power management, nor hot-unplug.  Just log
    // the fact that we have been woken up and do nothing.
    LOG("Wakeup event received!\n");
}

void IntelHDACodec::BeginShutdown() {
    // Close all existing connections and synchronize with any client threads
    // who are currently processing requests.
    state_ = State::SHUTTING_DOWN;
    IntelHDADevice::Shutdown();

    // Give any active streams we had back to our controller.
    IntelHDAStream::Tree streams;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        streams.swap(active_streams_);
    }

    while (!streams.is_empty())
        controller_.ReturnStream(streams.pop_front());

    state_ = State::SHUT_DOWN;
}

void IntelHDACodec::FinishShutdown() {
    ZX_DEBUG_ASSERT(state_ == State::SHUTTING_DOWN);
    state_ = State::SHUT_DOWN;
}

zx_status_t IntelHDACodec::PublishDevice() {
    // Generate our name.
    char name[ZX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "intel-hda-codec-%03u", codec_id_);

    // Initialize our device and fill out the protocol hooks
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = name;
    args.ctx = this;
    args.ops =  &CODEC_DEVICE_THUNKS;
    args.proto_id = ZX_PROTOCOL_IHDA_CODEC;
    args.proto_ops = &CODEC_PROTO_THUNKS;
    args.props = dev_props_;
    args.prop_count = countof(dev_props_);

    // Publish the device.
    zx_status_t res = device_add(controller_.dev_node(), &args, &dev_node_);
    if (res != ZX_OK) {
        LOG("Failed to add codec device for \"%s\" (res %d)\n", name, res);
        return res;
    }

    return ZX_OK;
}

zx_status_t IntelHDACodec::ParseVidDid(const CodecResponse& resp) {
    props_.vid = static_cast<uint16_t>((resp.data >> 16) & 0xFFFF);
    props_.did = static_cast<uint16_t>(resp.data & 0xFFFF);

    SET_DEVICE_PROP(VID, props_.vid);
    SET_DEVICE_PROP(DID, props_.did);

    return (props_.vid != 0) ? ZX_OK : ZX_ERR_INTERNAL;
}

zx_status_t IntelHDACodec::ParseRevisionId(const CodecResponse& resp) {
    props_.ihda_vmaj = static_cast<uint8_t>((resp.data >> 20) & 0xF);
    props_.ihda_vmin = static_cast<uint8_t>((resp.data >> 16) & 0xF);
    props_.rev_id    = static_cast<uint8_t>((resp.data >>  8) & 0xFF);
    props_.step_id   = static_cast<uint8_t>(resp.data & 0xFF);

    SET_DEVICE_PROP(MAJOR_REV,   props_.ihda_vmaj);
    SET_DEVICE_PROP(MINOR_REV,   props_.ihda_vmin);
    SET_DEVICE_PROP(VENDOR_REV,  props_.rev_id);
    SET_DEVICE_PROP(VENDOR_STEP, props_.step_id);

    state_ = State::FINDING_DRIVER;
    return PublishDevice();
}

#define PROCESS_CMD(_req_ack, _ioctl, _payload, _handler)       \
case _ioctl:                                                    \
    if (req_size != sizeof(req._payload)) {               \
        DEBUG_LOG("Bad " #_payload                              \
                  " request length (%u != %zu)\n",              \
                  req_size, sizeof(req._payload));        \
        return ZX_ERR_INVALID_ARGS;                                \
    }                                                           \
    if (_req_ack && (req.hdr.cmd & IHDA_NOACK_FLAG))  {   \
        DEBUG_LOG("Cmd " #_payload                              \
                  " requires acknowledgement, but the "         \
                  "NOACK flag was set!\n");                     \
        return ZX_ERR_INVALID_ARGS;                                \
    }                                                           \
    return _handler(channel, req._payload)

zx_status_t IntelHDACodec::ProcessClientRequest(DispatcherChannel* channel,
                                                const RequestBufferType& full_req,
                                                uint32_t req_size,
                                                zx::handle&& rxed_handle) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // Is this a request from a Stream channel?  If so, send it off to the
    // stream for processing (assuming that the stream still exists)
    bool is_stream_channel;
    auto stream = GetStreamForChannel(*channel, &is_stream_channel);
    if (is_stream_channel) {
        if (stream != nullptr) {
            return stream->ProcessClientRequest(channel,
                                                full_req.stream_requests,
                                                req_size,
                                                fbl::move(rxed_handle));
        } else {
            return ZX_ERR_BAD_STATE;
        }
    }

    // This must be a request for the codec.  Sanity check that portion of the
    // request payload.
    const auto& req = full_req.codec;
    if (req_size < sizeof(req.hdr)) {
        DEBUG_LOG("Client request too small to contain header (%u < %zu)\n",
                req_size, sizeof(req.hdr));
        return ZX_ERR_INVALID_ARGS;
    }

    VERBOSE_LOG("Client Request (cmd 0x%04x tid %u) len %u\n",
            req.hdr.cmd,
            req.hdr.transaction_id,
            req_size);

    auto cmd_id = static_cast<ihda_cmd_t>(req.hdr.cmd & ~IHDA_NOACK_FLAG);

    if (req.hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID) {
        DEBUG_LOG("Invalid transaction ID in client request 0x%04x\n", cmd_id);
        return ZX_ERR_INVALID_ARGS;
    }

    if (rxed_handle.is_valid()) {
        DEBUG_LOG("Received unexpected handle in client request 0x%04x\n", cmd_id);
        return ZX_ERR_INVALID_ARGS;
    }

    switch (cmd_id) {
    PROCESS_CMD(true,  IHDA_CMD_GET_IDS,             get_ids,        ProcessGetIDs);
    PROCESS_CMD(false, IHDA_CODEC_SEND_CORB_CMD,     corb_cmd,       ProcessSendCORBCmd);
    PROCESS_CMD(true,  IHDA_CODEC_REQUEST_STREAM,    request_stream, ProcessRequestStream);
    PROCESS_CMD(false, IHDA_CODEC_RELEASE_STREAM,    release_stream, ProcessReleaseStream);
    PROCESS_CMD(false, IHDA_CODEC_SET_STREAM_FORMAT, set_stream_fmt, ProcessSetStreamFmt);
    default:
        DEBUG_LOG("Unrecognized command ID 0x%04x\n", req.hdr.cmd);
        return ZX_ERR_INVALID_ARGS;
    }
}

#undef PROCESS_CMD

zx_status_t IntelHDACodec::ProcessGetIDs(DispatcherChannel* channel,
                                         const ihda_proto::GetIDsReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    ihda_proto::GetIDsResp resp;
    resp.hdr       = req.hdr;
    resp.vid       = props_.vid;
    resp.did       = props_.did;
    resp.ihda_vmaj = props_.ihda_vmaj;
    resp.ihda_vmin = props_.ihda_vmin;
    resp.rev_id    = props_.rev_id;
    resp.step_id   = props_.step_id;

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDACodec::ProcessSendCORBCmd(DispatcherChannel* channel,
                                              const ihda_proto::SendCORBCmdReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    CodecVerb verb(req.verb);

    // Make sure that the command is well formed.
    if (!CodecCommand::SanityCheck(id(), req.nid, verb)) {
        DEBUG_LOG("Bad SEND_CORB_CMD request values [%u, %hu, 0x%05x]\n",
                id(), req.nid, verb.val);
        return ZX_ERR_INVALID_ARGS;
    }

    // Only the dedicated driver channel is permitted to execute set verbs.
    if (verb.is_set() && (channel->owner_ctx() != DRIVER_CHANNEL_ID)) {
        DEBUG_LOG("SET verbs not allowed from unprivledged connections! [%u, %hu, 0x%05x]\n",
                id(), req.nid, verb.val);
        return ZX_ERR_ACCESS_DENIED;
    }

    fbl::RefPtr<DispatcherChannel> chan_ref = (req.hdr.cmd & IHDA_NOACK_FLAG)
                                             ? nullptr
                                             : fbl::WrapRefPtr(channel);

    auto job = CodecCmdJobAllocator::New(fbl::move(chan_ref),
                                         req.hdr.transaction_id,
                                         CodecCommand(id(), req.nid, verb));

    if (job == nullptr)
        return ZX_ERR_NO_MEMORY;

    zx_status_t res = controller_.QueueCodecCmd(fbl::move(job));
    if (res != ZX_OK) {
        DEBUG_LOG("Failed to queue CORB command [%u, %hu, 0x%05x] (res %d)\n",
                id(), req.nid, verb.val, res);
    }

    return res;
}

zx_status_t IntelHDACodec::ProcessRequestStream(DispatcherChannel* channel,
                                                const ihda_proto::RequestStreamReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // Only the dedicated driver channel is permitted to request DMA streams.
    if (channel->owner_ctx() != DRIVER_CHANNEL_ID) {
        DEBUG_LOG("RequestStream not allowed from unprivledged connections!\n");
        return ZX_ERR_ACCESS_DENIED;
    }

    ihda_proto::RequestStreamResp resp;
    resp.hdr = req.hdr;

    // Attempt to get a stream of the proper type.
    auto type = req.input
              ? IntelHDAStream::Type::INPUT
              : IntelHDAStream::Type::OUTPUT;
    auto stream = controller_.AllocateStream(type);

    if (stream != nullptr) {
        // Success, send its ID and its tag back to the codec and add it to the
        // set of active streams owned by this codec.
        resp.result     = ZX_OK;
        resp.stream_id  = stream->id();
        resp.stream_tag = stream->tag();

        fbl::AutoLock lock(&active_streams_lock_);
        active_streams_.insert(fbl::move(stream));
    } else {
        // Failure; tell the codec that we are out of streams.
        resp.result     = ZX_ERR_NO_MEMORY;
        resp.stream_id  = 0;
        resp.stream_tag = 0;
    }

    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDACodec::ProcessReleaseStream(DispatcherChannel* channel,
                                                const ihda_proto::ReleaseStreamReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // Only the dedicated driver channel is permitted to release DMA streams.
    if (channel->owner_ctx() != DRIVER_CHANNEL_ID) {
        DEBUG_LOG("RequestStream not allowed from unprivledged connections!\n");
        return ZX_ERR_ACCESS_DENIED;
    }

    // Remove the stream from the active set.
    fbl::RefPtr<IntelHDAStream> stream;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        stream = active_streams_.erase(req.stream_id);
    }

    // If the stream was not active, our codec driver is crazy.  Hang up the
    // phone on it.
    if (stream == nullptr)
        return ZX_ERR_BAD_STATE;

    // Give the stream back to the controller and (if an ack was requested) tell
    // our codec driver that things went well.
    stream->Deactivate();
    controller_.ReturnStream(fbl::move(stream));

    if (req.hdr.cmd & IHDA_NOACK_FLAG)
        return ZX_OK;

    ihda_proto::RequestStreamResp resp;
    resp.hdr = req.hdr;
    return channel->Write(&resp, sizeof(resp));
}

zx_status_t IntelHDACodec::ProcessSetStreamFmt(DispatcherChannel* channel,
                                               const ihda_proto::SetStreamFmtReq& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // Only the dedicated driver channel is permitted to release DMA streams.
    if (channel->owner_ctx() != DRIVER_CHANNEL_ID) {
        DEBUG_LOG("RequestStream not allowed from unprivledged connections!\n");
        return ZX_ERR_ACCESS_DENIED;
    }

    // Sanity check the requested format.
    if (!StreamFormat(req.format).SanityCheck()) {
        DEBUG_LOG("Invalid encoded stream format 0x%04hx!\n", req.format);
        return ZX_ERR_INVALID_ARGS;
    }

    // Grab a reference to the stream from the active set.
    fbl::RefPtr<IntelHDAStream> stream;
    {
        fbl::AutoLock lock(&active_streams_lock_);
        auto iter = active_streams_.find(req.stream_id);
        if (iter.IsValid())
            stream = iter.CopyPointer();
    }

    // If the stream was not active, our codec driver is crazy.  Hang up the
    // phone on it.
    if (stream == nullptr)
        return ZX_ERR_BAD_STATE;

    // Create a channel which will be used to configure the stream DMA buffers,
    // start/stop the channel, and send status reports.  Set the owner_ctx of
    // the stream to the stream's ID so we can look up requests which come in.
    ZX_DEBUG_ASSERT(req.stream_id == stream->id());
    auto stream_channel = DispatcherChannelAllocator::New(stream->id());
    if (stream_channel == nullptr)
        return ZX_ERR_NO_MEMORY;

    // Set the stream format and assign the client channel to the stream.  If
    // this stream is already bound to a client, this will cause that connection
    // to be closed.
    zx_status_t res = stream->SetStreamFormat(req.format, stream_channel);
    if (res != ZX_OK) {
        DEBUG_LOG("Failed to set stream format 0x%04hx for stream %hu (res %d)\n",
                  req.format, req.stream_id, res);
        return res;
    }

    // Activate the channel, binding it to (the codec) in the process.  This
    // has the effect of serializing all of the requests targeted at this codec,
    // or any of the DMA streams it is managing.
    zx::channel client_channel;
    res = stream_channel->Activate(fbl::WrapRefPtr(this), &client_channel);
    if (res != ZX_OK) {
        DEBUG_LOG("Failed to activate stream channel (res %d)\n", res);
        stream->Deactivate();
        return res;
    }

    // Send the channel back to the codec driver.
    ZX_DEBUG_ASSERT(client_channel.is_valid());
    ihda_proto::SetStreamFmtResp resp;
    resp.hdr = req.hdr;
    res = channel->Write(&resp, sizeof(resp), fbl::move(client_channel));

    if (res != ZX_OK)
        DEBUG_LOG("Failed to send stream channel back to codec driver (res %d)\n", res);

    return res;
}

void IntelHDACodec::NotifyChannelDeactivated(const DispatcherChannel& channel) {
    // If this was the driver's channel to us, release the internal channel
    // reference from within the codec_driver_channel_lock.
    if (channel.owner_ctx() == DRIVER_CHANNEL_ID) {
        {
            fbl::AutoLock lock(&codec_driver_channel_lock_);
            ZX_DEBUG_ASSERT(&channel == codec_driver_channel_.get());
            codec_driver_channel_.reset();
        }

        // Return any DMA streams the codec driver had owned back to the controller.
        IntelHDAStream::Tree tmp;
        {
            fbl::AutoLock lock(&active_streams_lock_);
            tmp = fbl::move(active_streams_);
        }

        while (!tmp.is_empty()) {
            auto stream = tmp.pop_front();
            stream->Deactivate();
            controller_.ReturnStream(fbl::move(stream));
        }

        return;
    }

    // If this was the currently active channel for one of our active streams,
    // deactivate the stream.
    auto stream = GetStreamForChannel(channel);
    if (stream != nullptr) {
        stream->OnChannelClosed(channel);
        return;
    }
}

zx_status_t IntelHDACodec::CodecGetDispatcherChannel(zx_handle_t* channel_out) {
    if (!channel_out)
        return ZX_ERR_INVALID_ARGS;

    *channel_out = ZX_HANDLE_INVALID;

    // Enter the driver channel lock.  If we have already connected to a codec
    // driver, simply fail the request.  Otherwise, attempt to build a driver channel
    // and activate it.
    fbl::RefPtr<DispatcherChannel> activate_me;
    {
        fbl::AutoLock lock(&codec_driver_channel_lock_);

        if (codec_driver_channel_ != nullptr)
            return ZX_ERR_BAD_STATE;

        // Allocate a new channel.  Use the owner_ctx() to indicate that this is
        // the singleton driver channel, and therefor allowed to perform
        // privledged operations (such as allocate stream dma contexts)
        codec_driver_channel_ = DispatcherChannelAllocator::New(DRIVER_CHANNEL_ID);
        if (codec_driver_channel_ == nullptr)
            return ZX_ERR_NO_MEMORY;

        // Now that we have successfully allocated a channel, we can take a
        // local reference to it, leave the codec driver channel lock, and
        // attempt to activate the channel.  Any attempts to create a new driver
        // channel on another thread while we activate this channel will fail
        // with ZX_ERR_BAD_STATE because codec_driver_channel_ is non-null.
        activate_me = codec_driver_channel_;
    }

    ZX_DEBUG_ASSERT(activate_me != nullptr);
    zx::channel client_channel;
    zx_status_t activate_result = activate_me->Activate(fbl::WrapRefPtr(this), &client_channel);

    // If we failed to activate the channel, release the internal reference we
    // were holding from inside of the codec_driver_channel_lock.
    if (activate_result != ZX_OK) {
        fbl::AutoLock lock(&codec_driver_channel_lock_);
        codec_driver_channel_ = nullptr;
    } else {
        *channel_out = client_channel.release();
    }

    return activate_result;
}

// Get a reference to the active stream (if any) associated with the specified channel.
fbl::RefPtr<IntelHDAStream> IntelHDACodec::GetStreamForChannel(const DispatcherChannel& channel,
                                                                bool* is_stream_channel_out) {
    bool stream_chan = ((channel.owner_ctx() > 0) &&
                        (channel.owner_ctx() <= fbl::numeric_limits<uint16_t>::max()));

    if (is_stream_channel_out != nullptr)
        *is_stream_channel_out = stream_chan;

    if (stream_chan) {
        auto stream_id = static_cast<uint16_t>(channel.owner_ctx());

        {
            fbl::AutoLock lock(&active_streams_lock_);
            auto iter = active_streams_.find(stream_id);
            if (iter.IsValid())
                return iter.CopyPointer();
        }
    }

    return nullptr;
}

}  // namespace intel_hda
}  // namespace audio
