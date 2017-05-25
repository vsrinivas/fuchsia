// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <mx/channel.h>
#include <mxtl/auto_lock.h>
#include <mxtl/limits.h>
#include <string.h>

#include "drivers/audio/intel-hda/codecs/utils/codec-driver-base.h"
#include "drivers/audio/intel-hda/codecs/utils/stream-base.h"
#include "drivers/audio/intel-hda/utils/intel-hda-proto.h"
#include "drivers/audio/intel-hda/utils/utils.h"

#include "debug-logging.h"

namespace audio {
namespace intel_hda {
namespace codecs {

void IntelHDACodecDriverBase::PrintDebugPrefix() const {
    printf("HDACodec : ");
}

mx_status_t IntelHDACodecDriverBase::Bind(mx_device_t* codec_dev) {
    mx_status_t res;
    void* proto_void;

    if (codec_dev == nullptr)
        return ERR_INVALID_ARGS;

    if (codec_device_ != nullptr)
        return ERR_BAD_STATE;

    res = device_op_get_protocol(codec_dev, MX_PROTOCOL_IHDA_CODEC, &proto_void);
    if (res != NO_ERROR)
        return res;

    auto codec_interface = static_cast<ihda_codec_protocol_t*>(proto_void);
    if ((codec_interface == nullptr) ||
        (codec_interface->get_driver_channel == nullptr))
        return ERR_NOT_SUPPORTED;

    // Allocate a DispatcherChannel object which we will use to talk to the codec device
    mxtl::RefPtr<DispatcherChannel> device_channel = DispatcherChannelAllocator::New(1);
    if (device_channel == nullptr)
        return ERR_NO_MEMORY;

    // Obtain a channel handle from the device
    mx::channel channel;
    res = codec_interface->get_driver_channel(codec_dev, channel.get_address());
    if (res != NO_ERROR)
        return res;

    // Stash our reference to our device channel.  If activate succeeds, we
    // could start to receive messages from the codec device immediately.
    {
        mxtl::AutoLock device_channel_lock(&device_channel_lock_);
        device_channel_ = device_channel;
    }

    // Activate our device channel.  If something goes wrong, clear out the
    // internal device_channel_ reference.
    res = device_channel->Activate(mxtl::WrapRefPtr(this), mxtl::move(channel));
    if (res != NO_ERROR) {
        mxtl::AutoLock device_channel_lock(&device_channel_lock_);
        device_channel_.reset();
        return res;
    }

    // Success!  Now that we are started, stash a pointer to the codec device
    // that we are the driver for.
    codec_device_ = codec_dev;
    return NO_ERROR;
}

void IntelHDACodecDriverBase::Shutdown() {
    // Flag the fact that we are shutting down.  This will prevent any new
    // streams from becoming activated.
    {
        mxtl::AutoLock shutdown_lock(&shutdown_lock_);
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

#define CHECK_RESP_ALLOW_HANDLE(_ioctl, _payload)           \
    do {                                                    \
        if (resp_size != sizeof(resp._payload)) {           \
            DEBUG_LOG("Bad " #_ioctl                        \
                      " response length (%u != %zu)\n",     \
                      resp_size, sizeof(resp._payload));    \
            return ERR_INVALID_ARGS;                        \
        }                                                   \
    } while (0)
#define CHECK_RESP(_ioctl, _payload)                \
    do {                                            \
        if (rxed_handle.is_valid()) {               \
            DEBUG_LOG("Unexpected handle in "       \
                       #_ioctl " response\n");      \
            return ERR_INVALID_ARGS;                \
        }                                           \
        CHECK_RESP_ALLOW_HANDLE(_ioctl, _payload);  \
    } while (0)

mx_status_t IntelHDACodecDriverBase::ProcessChannel(DispatcherChannel* channel) {
    MX_DEBUG_ASSERT(channel != nullptr);

    uint32_t resp_size;
    CodecChannelResponses resp;
    mx::handle rxed_handle;

    mx_status_t res = channel->Read(&resp, sizeof(resp), &resp_size, &rxed_handle);
    if (res != NO_ERROR) {
        DEBUG_LOG("Error reading from device channel (res %d)!\n", res);
        return res;
    }

    if (resp_size < sizeof(resp.hdr)) {
        DEBUG_LOG("Bad length (%u) reading from device channel (expectd at least %zu)!\n",
                  resp_size, sizeof(resp.hdr));
        return ERR_INVALID_ARGS;
    }

    // Does this response belong to one of our streams?
    if ((resp.hdr.transaction_id != IHDA_INVALID_TRANSACTION_ID) &&
        (resp.hdr.transaction_id != CODEC_TID)) {
        auto stream = GetActiveStream(resp.hdr.transaction_id);

        if (stream == nullptr) {
            DEBUG_LOG("Received codec device response for inactive stream (id %u)\n",
                      resp.hdr.transaction_id);
            return ERR_BAD_STATE;
        } else {
            return ProcessStreamResponse(stream, resp, resp_size, mxtl::move(rxed_handle));
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
            mx_status_t res = MapUnsolTagToStreamId(payload.unsol_tag(), &stream_id);
            if (res != NO_ERROR) {
                DEBUG_LOG("Received unexpected unsolicited reponse (tag %u)\n",
                          payload.unsol_tag());
                return NO_ERROR;
            }

            if (stream_id == CODEC_TID)
                return ProcessUnsolicitedResponse(payload);

            auto stream = GetActiveStream(stream_id);
            if (stream == nullptr) {
                DEBUG_LOG("Received unsolicited reponse (tag %u) for inactive stream (id %u)\n",
                          payload.unsol_tag(), stream_id);
                return NO_ERROR;
            } else {
                return stream->ProcessResponse(payload);
            }
        }

        default:
            DEBUG_LOG("Received unexpected response type (%u) for codec device!\n",
                      resp.hdr.cmd);
            return ERR_INVALID_ARGS;
        }
    }
}

mx_status_t IntelHDACodecDriverBase::ProcessStreamResponse(
        const mxtl::RefPtr<IntelHDAStreamBase>& stream,
        const CodecChannelResponses& resp,
        uint32_t resp_size,
        mx::handle&& rxed_handle) {
    mx_status_t res;
    MX_DEBUG_ASSERT(stream != nullptr);

    switch(resp.hdr.cmd) {
    case IHDA_CODEC_SEND_CORB_CMD: {
        CHECK_RESP(IHDA_CODEC_SEND_CORB_CMD, send_corb);
        CodecResponse payload(resp.send_corb.data, resp.send_corb.data_ex);

        if (payload.unsolicited()) {
            DEBUG_LOG("Unsolicited response sent directly to stream ID %u! (0x%08x, 0x%08x)\n",
                      stream->id(), payload.data, payload.data_ex);
            return ERR_INVALID_ARGS;
        }

        return stream->ProcessResponse(payload);
    }

    case IHDA_CODEC_REQUEST_STREAM:
        CHECK_RESP(IHDA_CODEC_REQUEST_STREAM, request_stream);
        return stream->ProcessRequestStream(resp.request_stream);

    case IHDA_CODEC_SET_STREAM_FORMAT: {
        CHECK_RESP_ALLOW_HANDLE(IHDA_CODEC_SET_STREAM_FORMAT, set_stream_fmt);

        mx::channel channel;
        res = ConvertHandle(&rxed_handle, &channel);
        if (res != NO_ERROR) {
            DEBUG_LOG("Invalid or non-Channel handle in IHDA_CODEC_SET_STREAM_FORMAT "
                      "response (res %d)\n", res);
            return res;
        }

        return stream->ProcessSetStreamFmt(resp.set_stream_fmt, mxtl::move(channel));
    }

    default:
        DEBUG_LOG("Received unexpected response type (%u) for codec stream device!\n",
                  resp.hdr.cmd);
        return ERR_INVALID_ARGS;
    }
}

#undef CHECK_RESP
#undef CHECK_RESP_ALLOW_HANDLE

void IntelHDACodecDriverBase::NotifyChannelDeactivated(const DispatcherChannel& channel) {
    bool do_shutdown = false;

    {
        mxtl::AutoLock device_channel_lock(&device_channel_lock_);

        // If the channel we use to talk to our device is closing, clear out our
        // internal bookkeeping.
        //
        // TODO(johngro) : We should probably tell our implementation about this.
        if (&channel == device_channel_.get()) {
            do_shutdown = true;
            device_channel_.reset();
        }
    }

    if (do_shutdown)
        Shutdown();
}

void IntelHDACodecDriverBase::UnlinkFromController() {
    mxtl::AutoLock device_channel_lock(&device_channel_lock_);
    if (device_channel_ != nullptr) {
        device_channel_->Deactivate(false);
        device_channel_ = nullptr;
    }
}

mx_status_t IntelHDACodecDriverBase::SendCodecCommand(uint16_t  nid,
                                                      CodecVerb verb,
                                                      bool      no_ack) {
    mxtl::RefPtr<DispatcherChannel> device_channel;
    {
        mxtl::AutoLock device_channel_lock(&device_channel_lock_);
        device_channel = device_channel_;
    }

    if (device_channel == nullptr)
        return ERR_BAD_STATE;

    ihda_codec_send_corb_cmd_req_t cmd;

    cmd.hdr.cmd = no_ack ? IHDA_CODEC_SEND_CORB_CMD_NOACK : IHDA_CODEC_SEND_CORB_CMD;
    cmd.hdr.transaction_id = CODEC_TID;
    cmd.nid = nid;
    cmd.verb = verb.val;

    return device_channel->Write(&cmd, sizeof(cmd));
}

mxtl::RefPtr<IntelHDAStreamBase> IntelHDACodecDriverBase::GetActiveStream(uint32_t stream_id) {
    mxtl::AutoLock active_streams_lock(&active_streams_lock_);
    auto iter = active_streams_.find(stream_id);
    return iter.IsValid() ? iter.CopyPointer() : nullptr;
}

mx_status_t IntelHDACodecDriverBase::ActivateStream(
        const mxtl::RefPtr<IntelHDAStreamBase>& stream) {
    if ((stream == nullptr) ||
        (stream->id() == IHDA_INVALID_TRANSACTION_ID) ||
        (stream->id() == CODEC_TID))
        return ERR_INVALID_ARGS;

    mxtl::AutoLock shutdown_lock(&shutdown_lock_);
    if (shutting_down_)
        return ERR_BAD_STATE;

    // Grab a reference to the channel we use to talk to the codec device.  If
    // the channel has already been closed, we cannot activate this stream.
    mxtl::RefPtr<DispatcherChannel> device_channel;
    {
        mxtl::AutoLock device_channel_lock(&device_channel_lock_);
        if (device_channel_ == nullptr)
            return ERR_BAD_STATE;
        device_channel = device_channel_;
    }

    // Add this channel to the set of active channels.  If we encounte a key
    // collision, then something is wrong with our codec driver implementation.
    // Fail the activation.
    {
        mxtl::AutoLock active_streams_lock(&active_streams_lock_);
        if (!active_streams_.insert_or_find(stream))
            return ERR_BAD_STATE;
    }

    // Go ahead and activate the stream.
    return stream->Activate(mxtl::WrapRefPtr(this), device_channel);
}

mx_status_t IntelHDACodecDriverBase::AllocateUnsolTag(const IntelHDAStreamBase& stream,
                                                      uint8_t* out_tag) {
    return AllocateUnsolTag(stream.id(), out_tag);
}

void IntelHDACodecDriverBase::ReleaseUnsolTag(const IntelHDAStreamBase& stream, uint8_t tag) {
    return ReleaseUnsolTag(stream.id(), tag);
}

void IntelHDACodecDriverBase::ReleaseAllUnsolTags(const IntelHDAStreamBase& stream) {
    return ReleaseAllUnsolTags(stream.id());
}

mx_status_t IntelHDACodecDriverBase::DeactivateStream(uint32_t stream_id) {
    mxtl::RefPtr<IntelHDAStreamBase> stream;
    {
        mxtl::AutoLock active_streams_lock(&active_streams_lock_);
        stream = active_streams_.erase(stream_id);
    }

    if (stream == nullptr)
        return ERR_NOT_FOUND;

    stream->Deactivate();

    return NO_ERROR;
}

mx_status_t IntelHDACodecDriverBase::AllocateUnsolTag(uint32_t stream_id, uint8_t* out_tag) {
    MX_DEBUG_ASSERT(out_tag != nullptr);
    mxtl::AutoLock unsol_tag_lock(&unsol_tag_lock_);

    static_assert(sizeof(free_unsol_tags_) == sizeof(long long int),
                  "free_unsol_tags_ is not the same size as a long long int.  "
                  "Cannot use ffsll to find the first set bit!");

    uint32_t first_set = ffsll(free_unsol_tags_);
    if (!first_set)
      return ERR_NO_MEMORY;

    --first_set;

    *out_tag = first_set;
    free_unsol_tags_ &= ~(1ull << first_set);
    unsol_tag_to_stream_id_map_[first_set] = stream_id;
    return NO_ERROR;

}

void IntelHDACodecDriverBase::ReleaseUnsolTag(uint32_t stream_id, uint8_t tag) {
    mxtl::AutoLock unsol_tag_lock(&unsol_tag_lock_);
    uint64_t mask = uint64_t(1u) << tag;

    MX_DEBUG_ASSERT(mask != 0);
    MX_DEBUG_ASSERT(!(free_unsol_tags_ & mask));
    MX_DEBUG_ASSERT(tag < countof(unsol_tag_to_stream_id_map_));
    MX_DEBUG_ASSERT(unsol_tag_to_stream_id_map_[tag] == stream_id);

    free_unsol_tags_ |= mask;
}

void IntelHDACodecDriverBase::ReleaseAllUnsolTags(uint32_t stream_id) {
    mxtl::AutoLock unsol_tag_lock(&unsol_tag_lock_);

    for (uint32_t tmp = 0u; tmp < countof(unsol_tag_to_stream_id_map_); ++tmp) {
        uint64_t mask = uint64_t(1u) << tmp;
        if (!(free_unsol_tags_ & mask) && (unsol_tag_to_stream_id_map_[tmp] == stream_id)) {
            free_unsol_tags_ |= mask;
        }
    }
}

mx_status_t IntelHDACodecDriverBase::MapUnsolTagToStreamId(uint8_t tag, uint32_t* out_stream_id) {
    MX_DEBUG_ASSERT(out_stream_id != nullptr);

    mxtl::AutoLock unsol_tag_lock(&unsol_tag_lock_);
    uint64_t mask = uint64_t(1u) << tag;

    if ((!mask) || (free_unsol_tags_ & mask))
        return ERR_NOT_FOUND;

    MX_DEBUG_ASSERT(tag < countof(unsol_tag_to_stream_id_map_));
    *out_stream_id = unsol_tag_to_stream_id_map_[tag];
    return NO_ERROR;
}

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
