// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/intelhda/codec/cpp/banjo.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <iterator>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/string_printf.h>
#include <intel-hda/codec-utils/codec-driver-base.h>
#include <intel-hda/codec-utils/stream-base.h>
#include <intel-hda/utils/intel-hda-proto.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {
namespace codecs {

void IntelHDACodecDriverBase::PrintDebugPrefix() const { printf("HDACodec : "); }

IntelHDACodecDriverBase::IntelHDACodecDriverBase() : loop_(&kAsyncLoopConfigNeverAttachToThread) {
  loop_.StartThread("intel-hda-codec-driver-loop");
}

#define DEV(_ctx) static_cast<IntelHDACodecDriverBase*>(_ctx)
zx_protocol_device_t IntelHDACodecDriverBase::CODEC_DEVICE_THUNKS = []() {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.release = [](void* ctx) { DEV(ctx)->DeviceRelease(); };
  ops.suspend = [](void* ctx, uint8_t requested_state, bool enable_wake, uint8_t suspend_reason) {
    uint8_t out_state;
    zx_status_t status =
        DEV(ctx)->Suspend(requested_state, enable_wake, suspend_reason, &out_state);
    device_suspend_reply(DEV(ctx)->zxdev(), status, out_state);
  };
  return ops;
}();
#undef DEV

zx::result<> IntelHDACodecDriverBase::Bind(zx_device_t* codec_dev, const char* name) {
  ZX_ASSERT(codec_dev != nullptr);

  if (codec_device_ != nullptr) {
    LOG("Codec already bound.");
    return zx::error(ZX_ERR_BAD_STATE);
  }

  ddk::IhdaCodecProtocolClient client;
  zx_status_t result = ddk::IhdaCodecProtocolClient::CreateFromDevice(codec_dev, &client);
  if (result != ZX_OK) {
    LOG("Failure while attempting to fetch DDK protocol.");
    return zx::error(result);
  }

  zx::channel channel;

  // Obtain a channel handle from the device
  result = client.GetDriverChannel(&channel);
  if (result != ZX_OK) {
    LOG("Error fetching driver channel.");
    return zx::error(result);
  }

  auto device_channel = Channel::Create(std::move(channel));
  if (device_channel == nullptr) {
    LOG("Error creating device channel.");
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  // Stash our reference to our device channel.  If activate succeeds, we
  // could start to receive messages from the codec device immediately.
  {
    fbl::AutoLock device_channel_lock(&device_channel_lock_);
    device_channel_ = device_channel;
    device_channel_->SetHandler(
        [codec = fbl::RefPtr(this)](async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                    zx_status_t status, const zx_packet_signal_t* signal) {
          codec->ChannelSignalled(dispatcher, wait, status, signal);
        });
    result = device_channel_->BeginWait(loop_.dispatcher());
    if (result != ZX_OK) {
      device_channel_.reset();
      LOG("Error on begin wait.");
      return zx::error(result);
    }
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
    args.ctx = fbl::ExportToRawPtr(&ddk_ref);
  }
  args.ops = &CODEC_DEVICE_THUNKS;
  args.flags = DEVICE_ADD_NON_BINDABLE;

  // Publish the device.
  result = device_add(codec_dev, &args, &zxdev_);
  if (result != ZX_OK) {
    LOG("Failed to add codec device for \"%s\" (result %d)\n", name, result);

    fbl::AutoLock device_channel_lock(&device_channel_lock_);
    device_channel_.reset();
    codec->Shutdown();
    codec.reset();
    LOG("Failed to add codec device for \"%s\".", name);
    return zx::error(result);
  }

  // Success!  Now that we are started, stash a pointer to the codec device
  // that we are the driver for.
  codec_device_ = codec_dev;
  return zx::ok();
}

void IntelHDACodecDriverBase::ChannelSignalled(async_dispatcher_t* dispatcher,
                                               async::WaitBase* wait, zx_status_t status,
                                               const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    if (status != ZX_ERR_CANCELED) {  // Cancel is expected.
      return;
    }
  }
  bool readable_asserted = signal->observed & ZX_CHANNEL_READABLE;
  bool peer_closed_asserted = signal->observed & ZX_CHANNEL_PEER_CLOSED;
  if (readable_asserted) {
    // Grab a reference to the device channel, the processing may grab the lock.
    fbl::RefPtr<Channel> device_channel;
    {
      fbl::AutoLock device_channel_lock(&device_channel_lock_);
      device_channel = device_channel_;
    }
    zx_status_t status = ProcessClientRequest(device_channel.get());
    if (status != ZX_OK) {
      peer_closed_asserted = true;
    }
  }
  if (peer_closed_asserted) {
    ProcessClientDeactivate();
  } else if (readable_asserted) {
    wait->Begin(dispatcher);
  }
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

  loop_.Shutdown();
  DEBUG_LOG("Shutdown complete\n");
}

zx_status_t IntelHDACodecDriverBase::Suspend(uint8_t requested_state, bool enable_wake,
                                             uint8_t suspend_reason, uint8_t* out_state) {
  *out_state = DEV_POWER_STATE_D0;
  return ZX_ERR_NOT_SUPPORTED;
}

void IntelHDACodecDriverBase::DeviceRelease() {
  auto thiz = fbl::ImportFromRawPtr(this);
  // Shut the codec down.
  thiz->Shutdown();
  // Let go of the reference.
  thiz.reset();
}

#define CHECK_RESP_ALLOW_HANDLE(_ioctl, _payload)                           \
  do {                                                                      \
    if (resp_size != sizeof(resp._payload)) {                               \
      DEBUG_LOG("Bad " #_ioctl " response length (%u != %zu)\n", resp_size, \
                sizeof(resp._payload));                                     \
      return ZX_ERR_INVALID_ARGS;                                           \
    }                                                                       \
  } while (0)
#define CHECK_RESP(_ioctl, _payload)                            \
  do {                                                          \
    if (rxed_handle.is_valid()) {                               \
      DEBUG_LOG("Unexpected handle in " #_ioctl " response\n"); \
      return ZX_ERR_INVALID_ARGS;                               \
    }                                                           \
    CHECK_RESP_ALLOW_HANDLE(_ioctl, _payload);                  \
  } while (0)

zx_status_t IntelHDACodecDriverBase::ProcessClientRequest(Channel* channel) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  uint32_t resp_size;
  CodecChannelResponses resp;
  zx::handle rxed_handle;

  zx_status_t res = channel->Read(&resp, sizeof(resp), &resp_size, rxed_handle);
  if (res != ZX_OK) {
    DEBUG_LOG("Error reading from device channel (res %d)!\n", res);
    return res;
  }

  if (resp_size < sizeof(resp.hdr)) {
    DEBUG_LOG("Bad length (%u) reading from device channel (expected at least %zu)!\n", resp_size,
              sizeof(resp.hdr));
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
      return ProcessStreamResponse(stream, resp, resp_size, std::move(rxed_handle));
    }
  } else {
    switch (resp.hdr.cmd) {
      case IHDA_CODEC_SEND_CORB_CMD: {
        CHECK_RESP(IHDA_CODEC_SEND_CORB_CMD, send_corb);

        CodecResponse payload(resp.send_corb.data, resp.send_corb.data_ex);
        if (!payload.unsolicited())
          return ProcessSolicitedResponse(payload);

        // If this is an unsolicited response, check to see if the tag is
        // owned by a stream or not.  If it is, dispatch the payload to the
        // stream, otherwise give it to the codec.
        uint32_t stream_id;
        zx_status_t res = MapUnsolTagToStreamId(payload.unsol_tag(), &stream_id);
        if (res != ZX_OK) {
          DEBUG_LOG("Received unexpected unsolicited response (tag %u)\n", payload.unsol_tag());
          return ZX_OK;
        }

        if (stream_id == CODEC_TID)
          return ProcessUnsolicitedResponse(payload);

        auto stream = GetActiveStream(stream_id);
        if (stream == nullptr) {
          DEBUG_LOG("Received unsolicited response (tag %u) for inactive stream (id %u)\n",
                    payload.unsol_tag(), stream_id);
          return ZX_OK;
        } else {
          return stream->ProcessResponse(payload);
        }
      }

      default:
        DEBUG_LOG("Received unexpected response type (%u) for codec device!\n", resp.hdr.cmd);
        return ZX_ERR_INVALID_ARGS;
    }
  }
}

zx_status_t IntelHDACodecDriverBase::ProcessStreamResponse(
    const fbl::RefPtr<IntelHDAStreamBase>& stream, const CodecChannelResponses& resp,
    uint32_t resp_size, zx::handle&& rxed_handle) {
  ZX_DEBUG_ASSERT(stream != nullptr);

  switch (resp.hdr.cmd) {
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

      return stream->ProcessSetStreamFmtLocked(resp.set_stream_fmt);
    }

    default:
      DEBUG_LOG("Received unexpected response type (%u) for codec stream device!\n", resp.hdr.cmd);
      return ZX_ERR_INVALID_ARGS;
  }
}

#undef CHECK_RESP
#undef CHECK_RESP_ALLOW_HANDLE

void IntelHDACodecDriverBase::ProcessClientDeactivate() {
  bool do_shutdown = false;

  {
    fbl::AutoLock device_channel_lock(&device_channel_lock_);

    // If the channel we use to talk to our device is closing, clear out our
    // internal bookkeeping.
    //
    // TODO(johngro) : We should probably tell our implementation about this.
    do_shutdown = true;
    device_channel_.reset();
  }

  if (do_shutdown)
    Shutdown();
}

void IntelHDACodecDriverBase::UnlinkFromController() {
  fbl::AutoLock device_channel_lock(&device_channel_lock_);
  if (device_channel_ != nullptr) {
    device_channel_ = nullptr;
  }
}

zx_status_t IntelHDACodecDriverBase::SendCodecCommand(uint16_t nid, CodecVerb verb, bool no_ack) {
  fbl::RefPtr<Channel> device_channel;
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

zx_status_t IntelHDACodecDriverBase::ActivateStream(const fbl::RefPtr<IntelHDAStreamBase>& stream) {
  if ((stream == nullptr) || (stream->id() == IHDA_INVALID_TRANSACTION_ID) ||
      (stream->id() == CODEC_TID))
    return ZX_ERR_INVALID_ARGS;

  fbl::AutoLock shutdown_lock(&shutdown_lock_);
  if (shutting_down_)
    return ZX_ERR_BAD_STATE;

  // Grab a reference to the channel we use to talk to the codec device.  If
  // the channel has already been closed, we cannot activate this stream.
  fbl::RefPtr<Channel> device_channel;
  {
    fbl::AutoLock device_channel_lock(&device_channel_lock_);
    if (device_channel_ == nullptr)
      return ZX_ERR_BAD_STATE;
    device_channel = device_channel_;
  }

  // Add this channel to the set of active channels.  If we encounter a key
  // collision, then something is wrong with our codec driver implementation.
  // Fail the activation.
  {
    fbl::AutoLock active_streams_lock(&active_streams_lock_);
    if (!active_streams_.insert_or_find(stream))
      return ZX_ERR_BAD_STATE;
  }

  // Go ahead and activate the stream.
  return stream->Activate(fbl::RefPtr(this), device_channel);
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
  ZX_DEBUG_ASSERT(tag < std::size(unsol_tag_to_stream_id_map_));
  ZX_DEBUG_ASSERT(unsol_tag_to_stream_id_map_[tag] == stream_id);

  free_unsol_tags_ |= mask;
}

void IntelHDACodecDriverBase::ReleaseAllUnsolTags(uint32_t stream_id) {
  fbl::AutoLock unsol_tag_lock(&unsol_tag_lock_);

  for (uint32_t tmp = 0u; tmp < std::size(unsol_tag_to_stream_id_map_); ++tmp) {
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

  ZX_DEBUG_ASSERT(tag < std::size(unsol_tag_to_stream_id_map_));
  *out_stream_id = unsol_tag_to_stream_id_map_[tag];
  return ZX_OK;
}

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
