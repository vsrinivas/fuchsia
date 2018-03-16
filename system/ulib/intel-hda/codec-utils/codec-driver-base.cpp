// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <lib/zx/channel.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <string.h>

#include <dispatcher-pool/dispatcher-thread-pool.h>

#include <intel-hda/codec-utils/codec-driver-base.h>
#include <intel-hda/codec-utils/stream-base.h>
#include <intel-hda/utils/intel-hda-proto.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {
namespace codecs {

void IntelHDACodecDriverBase::PrintDebugPrefix() const {
    printf("HDACodec : ");
}

IntelHDACodecDriverBase::IntelHDACodecDriverBase() {
    default_domain_ = dispatcher::ExecutionDomain::Create();
    ZX_ASSERT(default_domain_ != nullptr);
}

#define DEV(_ctx)  static_cast<IntelHDACodecDriverBase*>(_ctx)
zx_protocol_device_t IntelHDACodecDriverBase::CODEC_DEVICE_THUNKS = {
    .version      = DEVICE_OPS_VERSION,
    .get_protocol = nullptr,
    .open         = nullptr,
    .open_at      = nullptr,
    .close        = nullptr,
    .unbind       = nullptr,
    .release      = [](void* ctx) { DEV(ctx)->DeviceRelease(); },
    .read         = nullptr,
    .write        = nullptr,
    .get_size     = nullptr,
    .ioctl        = nullptr,
    .suspend      = nullptr,
    .resume       = nullptr,
    .rxrpc        = nullptr,
};
#undef DEV

zx_status_t IntelHDACodecDriverBase::Bind(zx_device_t* codec_dev, const char* name) {
    zx_status_t res;

    if (codec_dev == nullptr)
        return ZX_ERR_INVALID_ARGS;

    if (codec_device_ != nullptr)
        return ZX_ERR_BAD_STATE;

    ihda_codec_protocol_t proto;
    res = device_get_protocol(codec_dev, ZX_PROTOCOL_IHDA_CODEC, &proto);
    if (res != ZX_OK)
        return res;

    if ((proto.ops == nullptr) ||
        (proto.ops->get_driver_channel == nullptr))
        return ZX_ERR_NOT_SUPPORTED;

    // Allocate a dispatcher::Channel object which we will use to talk to the codec device
    fbl::RefPtr<dispatcher::Channel> device_channel = dispatcher::Channel::Create();
    if (device_channel == nullptr)
        return ZX_ERR_NO_MEMORY;

    // Obtain a channel handle from the device
    zx::channel channel;
    res = proto.ops->get_driver_channel(proto.ctx, channel.reset_and_get_address());
    if (res != ZX_OK)
        return res;

    // Stash our reference to our device channel.  If activate succeeds, we
    // could start to receive messages from the codec device immediately.
    {
        fbl::AutoLock device_channel_lock(&device_channel_lock_);
        device_channel_ = device_channel;
    }

    // Activate our device channel.  If something goes wrong, clear out the
    // internal device_channel_ reference.
    dispatcher::Channel::ProcessHandler phandler(
    [codec = fbl::WrapRefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
        return codec->ProcessClientRequest(channel);
    });

    dispatcher::Channel::ChannelClosedHandler chandler(
    [codec = fbl::WrapRefPtr(this)](const dispatcher::Channel* channel) -> void {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
        codec->ProcessClientDeactivate(channel);
    });

    res = device_channel->Activate(fbl::move(channel),
                                   default_domain_,
                                   fbl::move(phandler),
                                   fbl::move(chandler));
    if (res != ZX_OK) {
        fbl::AutoLock device_channel_lock(&device_channel_lock_);
        device_channel_.reset();
        return res;
    }

    auto codec = fbl::RefPtr<IntelHDACodecDriverBase>(this);

    // Initialize our device and fill out the protocol hooks
    device_add_args_t args;
    memset(&args, 0, sizeof(args));
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = name;
    {
        // use a different refptr to avoid problems in error path
        auto ddk_ref = codec;
        args.ctx = ddk_ref.leak_ref();
    }
    args.ops =  &CODEC_DEVICE_THUNKS;
    args.flags = DEVICE_ADD_NON_BINDABLE;

    // Publish the device.
    res = device_add(codec_dev, &args, nullptr);
    if (res != ZX_OK) {
        LOG("Failed to add codec device for \"%s\" (res %d)\n", name, res);

        fbl::AutoLock device_channel_lock(&device_channel_lock_);
        device_channel_.reset();
        codec->Shutdown();
        codec.reset();
        return res;
    }

    // Success!  Now that we are started, stash a pointer to the codec device
    // that we are the driver for.
    codec_device_ = codec_dev;
    return ZX_OK;
}

void IntelHDACodecDriverBase::Shutdown() {
    // Flag the fact that we are shutting down.  This will prevent any new
    // streams from becoming activated.
    {
        fbl::AutoLock shutdown_lock(&shutdown_lock_);
        shutting_down_ = true;
    }

    DEBUG_LOG("Shutting down codec\n");

    active_streams_lock_.Acquire();
    while (!active_streams_.is_empty()) {
        auto delete_me = active_streams_.pop_front();

        active_streams_lock_.Release();
        delete_me->Deactivate();
        delete_me = nullptr;
        active_streams_lock_.Acquire();
    }
    active_streams_lock_.Release();

    // Close the connection to our codec.
    DEBUG_LOG("Unlinking from controller\n");
    UnlinkFromController();

    DEBUG_LOG("Shutdown complete\n");
}

void IntelHDACodecDriverBase::DeviceRelease() {
    auto thiz = fbl::internal::MakeRefPtrNoAdopt(this);
    // Shut the codec down.
    thiz->Shutdown();
    // Let go of the reference.
    thiz.reset();
}

#define CHECK_RESP_ALLOW_HANDLE(_ioctl, _payload)           \
    do {                                                    \
        if (resp_size != sizeof(resp._payload)) {           \
            DEBUG_LOG("Bad " #_ioctl                        \
                      " response length (%u != %zu)\n",     \
                      resp_size, sizeof(resp._payload));    \
            return ZX_ERR_INVALID_ARGS;                        \
        }                                                   \
    } while (0)
#define CHECK_RESP(_ioctl, _payload)                \
    do {                                            \
        if (rxed_handle.is_valid()) {               \
            DEBUG_LOG("Unexpected handle in "       \
                       #_ioctl " response\n");      \
            return ZX_ERR_INVALID_ARGS;                \
        }                                           \
        CHECK_RESP_ALLOW_HANDLE(_ioctl, _payload);  \
    } while (0)

zx_status_t IntelHDACodecDriverBase::ProcessClientRequest(dispatcher::Channel* channel) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    uint32_t resp_size;
    CodecChannelResponses resp;
    zx::handle rxed_handle;

    zx_status_t res = channel->Read(&resp, sizeof(resp), &resp_size, &rxed_handle);
    if (res != ZX_OK) {
        DEBUG_LOG("Error reading from device channel (res %d)!\n", res);
        return res;
    }

    if (resp_size < sizeof(resp.hdr)) {
        DEBUG_LOG("Bad length (%u) reading from device channel (expectd at least %zu)!\n",
                  resp_size, sizeof(resp.hdr));
        return ZX_ERR_INVALID_ARGS;
    }

    // Does this response belong to one of our streams?
    if ((resp.hdr.transaction_id != IHDA_INVALID_TRANSACTION_ID) &&
        (resp.hdr.transaction_id != CODEC_TID)) {
        auto stream = GetActiveStream(resp.hdr.transaction_id);

        if (stream == nullptr) {
            DEBUG_LOG("Received codec device response for inactive stream (id %u)\n",
                      resp.hdr.transaction_id);
            return ZX_ERR_BAD_STATE;
        } else {
            return ProcessStreamResponse(stream, resp, resp_size, fbl::move(rxed_handle));
        }
    } else {
        switch(resp.hdr.cmd) {
        case IHDA_CODEC_SEND_CORB_CMD: {
            CHECK_RESP(IHDA_CODEC_SEND_CORB_CMD, send_corb);

            CodecResponse payload(resp.send_corb.data, resp.send_corb.data_ex);
            if (!payload.unsolicited())
                return ProcessSolicitedResponse(payload);

            // If this is an unsolicited reponse, check to see if the tag is
            // owned by a stream or not.  If it is, dispatch the payload to the
            // stream, otherwise give it to the codec.
            uint32_t stream_id;
            zx_status_t res = MapUnsolTagToStreamId(payload.unsol_tag(), &stream_id);
            if (res != ZX_OK) {
                DEBUG_LOG("Received unexpected unsolicited reponse (tag %u)\n",
                          payload.unsol_tag());
                return ZX_OK;
            }

            if (stream_id == CODEC_TID)
                return ProcessUnsolicitedResponse(payload);

            auto stream = GetActiveStream(stream_id);
            if (stream == nullptr) {
                DEBUG_LOG("Received unsolicited reponse (tag %u) for inactive stream (id %u)\n",
                          payload.unsol_tag(), stream_id);
                return ZX_OK;
            } else {
                return stream->ProcessResponse(payload);
            }
        }

        default:
            DEBUG_LOG("Received unexpected response type (%u) for codec device!\n",
                      resp.hdr.cmd);
            return ZX_ERR_INVALID_ARGS;
        }
    }
}

zx_status_t IntelHDACodecDriverBase::ProcessStreamResponse(
        const fbl::RefPtr<IntelHDAStreamBase>& stream,
        const CodecChannelResponses& resp,
        uint32_t resp_size,
        zx::handle&& rxed_handle) {
    zx_status_t res;
    ZX_DEBUG_ASSERT(stream != nullptr);

    switch(resp.hdr.cmd) {
    case IHDA_CODEC_SEND_CORB_CMD: {
        CHECK_RESP(IHDA_CODEC_SEND_CORB_CMD, send_corb);
        CodecResponse payload(resp.send_corb.data, resp.send_corb.data_ex);

        if (payload.unsolicited()) {
            DEBUG_LOG("Unsolicited response sent directly to stream ID %u! (0x%08x, 0x%08x)\n",
                      stream->id(), payload.data, payload.data_ex);
            return ZX_ERR_INVALID_ARGS;
        }

        return stream->ProcessResponse(payload);
    }

    case IHDA_CODEC_REQUEST_STREAM:
        CHECK_RESP(IHDA_CODEC_REQUEST_STREAM, request_stream);
        return stream->ProcessRequestStream(resp.request_stream);

    case IHDA_CODEC_SET_STREAM_FORMAT: {
        CHECK_RESP_ALLOW_HANDLE(IHDA_CODEC_SET_STREAM_FORMAT, set_stream_fmt);

        zx::channel channel;
        res = ConvertHandle(&rxed_handle, &channel);
        if (res != ZX_OK) {
            DEBUG_LOG("Invalid or non-Channel handle in IHDA_CODEC_SET_STREAM_FORMAT "
                      "response (res %d)\n", res);
            return res;
        }

        return stream->ProcessSetStreamFmt(resp.set_stream_fmt, fbl::move(channel));
    }

    default:
        DEBUG_LOG("Received unexpected response type (%u) for codec stream device!\n",
                  resp.hdr.cmd);
        return ZX_ERR_INVALID_ARGS;
    }
}

#undef CHECK_RESP
#undef CHECK_RESP_ALLOW_HANDLE

void IntelHDACodecDriverBase::ProcessClientDeactivate(const dispatcher::Channel* channel) {
    bool do_shutdown = false;

    {
        fbl::AutoLock device_channel_lock(&device_channel_lock_);

        // If the channel we use to talk to our device is closing, clear out our
        // internal bookkeeping.
        //
        // TODO(johngro) : We should probably tell our implementation about this.
        if (channel == device_channel_.get()) {
            do_shutdown = true;
            device_channel_.reset();
        }
    }

    if (do_shutdown)
        Shutdown();
}

void IntelHDACodecDriverBase::UnlinkFromController() {
    fbl::AutoLock device_channel_lock(&device_channel_lock_);
    if (device_channel_ != nullptr) {
        device_channel_->Deactivate();
        device_channel_ = nullptr;
    }
}

zx_status_t IntelHDACodecDriverBase::SendCodecCommand(uint16_t  nid,
                                                      CodecVerb verb,
                                                      bool      no_ack) {
    fbl::RefPtr<dispatcher::Channel> device_channel;
    {
        fbl::AutoLock device_channel_lock(&device_channel_lock_);
        device_channel = device_channel_;
    }

    if (device_channel == nullptr)
        return ZX_ERR_BAD_STATE;

    ihda_codec_send_corb_cmd_req_t cmd;

    cmd.hdr.cmd = no_ack ? IHDA_CODEC_SEND_CORB_CMD_NOACK : IHDA_CODEC_SEND_CORB_CMD;
    cmd.hdr.transaction_id = CODEC_TID;
    cmd.nid = nid;
    cmd.verb = verb.val;

    return device_channel->Write(&cmd, sizeof(cmd));
}

fbl::RefPtr<IntelHDAStreamBase> IntelHDACodecDriverBase::GetActiveStream(uint32_t stream_id) {
    fbl::AutoLock active_streams_lock(&active_streams_lock_);
    auto iter = active_streams_.find(stream_id);
    return iter.IsValid() ? iter.CopyPointer() : nullptr;
}

zx_status_t IntelHDACodecDriverBase::ActivateStream(
        const fbl::RefPtr<IntelHDAStreamBase>& stream) {
    if ((stream == nullptr) ||
        (stream->id() == IHDA_INVALID_TRANSACTION_ID) ||
        (stream->id() == CODEC_TID))
        return ZX_ERR_INVALID_ARGS;

    fbl::AutoLock shutdown_lock(&shutdown_lock_);
    if (shutting_down_)
        return ZX_ERR_BAD_STATE;

    // Grab a reference to the channel we use to talk to the codec device.  If
    // the channel has already been closed, we cannot activate this stream.
    fbl::RefPtr<dispatcher::Channel> device_channel;
    {
        fbl::AutoLock device_channel_lock(&device_channel_lock_);
        if (device_channel_ == nullptr)
            return ZX_ERR_BAD_STATE;
        device_channel = device_channel_;
    }

    // Add this channel to the set of active channels.  If we encounte a key
    // collision, then something is wrong with our codec driver implementation.
    // Fail the activation.
    {
        fbl::AutoLock active_streams_lock(&active_streams_lock_);
        if (!active_streams_.insert_or_find(stream))
            return ZX_ERR_BAD_STATE;
    }

    // Go ahead and activate the stream.
    return stream->Activate(fbl::WrapRefPtr(this), device_channel);
}

zx_status_t IntelHDACodecDriverBase::AllocateUnsolTag(const IntelHDAStreamBase& stream,
                                                      uint8_t* out_tag) {
    return AllocateUnsolTag(stream.id(), out_tag);
}

void IntelHDACodecDriverBase::ReleaseUnsolTag(const IntelHDAStreamBase& stream, uint8_t tag) {
    return ReleaseUnsolTag(stream.id(), tag);
}

void IntelHDACodecDriverBase::ReleaseAllUnsolTags(const IntelHDAStreamBase& stream) {
    return ReleaseAllUnsolTags(stream.id());
}

zx_status_t IntelHDACodecDriverBase::DeactivateStream(uint32_t stream_id) {
    fbl::RefPtr<IntelHDAStreamBase> stream;
    {
        fbl::AutoLock active_streams_lock(&active_streams_lock_);
        stream = active_streams_.erase(stream_id);
    }

    if (stream == nullptr)
        return ZX_ERR_NOT_FOUND;

    stream->Deactivate();

    return ZX_OK;
}

zx_status_t IntelHDACodecDriverBase::AllocateUnsolTag(uint32_t stream_id, uint8_t* out_tag) {
    ZX_DEBUG_ASSERT(out_tag != nullptr);
    fbl::AutoLock unsol_tag_lock(&unsol_tag_lock_);

    static_assert(sizeof(free_unsol_tags_) == sizeof(long long int),
                  "free_unsol_tags_ is not the same size as a long long int.  "
                  "Cannot use ffsll to find the first set bit!");

    uint32_t first_set = ffsll(free_unsol_tags_);
    if (!first_set)
      return ZX_ERR_NO_MEMORY;

    --first_set;

    *out_tag = static_cast<uint8_t>(first_set);
    free_unsol_tags_ &= ~(1ull << first_set);
    unsol_tag_to_stream_id_map_[first_set] = stream_id;
    return ZX_OK;

}

void IntelHDACodecDriverBase::ReleaseUnsolTag(uint32_t stream_id, uint8_t tag) {
    fbl::AutoLock unsol_tag_lock(&unsol_tag_lock_);
    uint64_t mask = uint64_t(1u) << tag;

    ZX_DEBUG_ASSERT(mask != 0);
    ZX_DEBUG_ASSERT(!(free_unsol_tags_ & mask));
    ZX_DEBUG_ASSERT(tag < countof(unsol_tag_to_stream_id_map_));
    ZX_DEBUG_ASSERT(unsol_tag_to_stream_id_map_[tag] == stream_id);

    free_unsol_tags_ |= mask;
}

void IntelHDACodecDriverBase::ReleaseAllUnsolTags(uint32_t stream_id) {
    fbl::AutoLock unsol_tag_lock(&unsol_tag_lock_);

    for (uint32_t tmp = 0u; tmp < countof(unsol_tag_to_stream_id_map_); ++tmp) {
        uint64_t mask = uint64_t(1u) << tmp;
        if (!(free_unsol_tags_ & mask) && (unsol_tag_to_stream_id_map_[tmp] == stream_id)) {
            free_unsol_tags_ |= mask;
        }
    }
}

zx_status_t IntelHDACodecDriverBase::MapUnsolTagToStreamId(uint8_t tag, uint32_t* out_stream_id) {
    ZX_DEBUG_ASSERT(out_stream_id != nullptr);

    fbl::AutoLock unsol_tag_lock(&unsol_tag_lock_);
    uint64_t mask = uint64_t(1u) << tag;

    if ((!mask) || (free_unsol_tags_ & mask))
        return ZX_ERR_NOT_FOUND;

    ZX_DEBUG_ASSERT(tag < countof(unsol_tag_to_stream_id_map_));
    *out_stream_id = unsol_tag_to_stream_id_map_[tag];
    return ZX_OK;
}

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
