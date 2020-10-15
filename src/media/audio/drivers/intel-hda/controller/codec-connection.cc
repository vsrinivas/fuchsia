// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec-connection.h"

#include <stdio.h>
#include <zircon/assert.h>

#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <intel-hda/utils/codec-commands.h>

#include "debug-logging.h"
#include "intel-hda-controller.h"
#include "intel-hda-stream.h"
#include "utils.h"

namespace audio {
namespace intel_hda {

#define SET_DEVICE_PROP(_prop, _value)                                               \
  do {                                                                               \
    static_assert(PROP_##_prop < countof(dev_props_), "Invalid Device Property ID"); \
    dev_props_[PROP_##_prop].id = BIND_IHDA_CODEC_##_prop;                           \
    dev_props_[PROP_##_prop].value = (_value);                                       \
  } while (false)

CodecConnection::ProbeCommandListEntry CodecConnection::PROBE_COMMANDS[] = {
    {.verb = GET_PARAM(CodecParam::VENDOR_ID), .parse = &CodecConnection::ParseVidDid},
    {.verb = GET_PARAM(CodecParam::REVISION_ID), .parse = &CodecConnection::ParseRevisionId},
};

#define DEV (static_cast<CodecConnection*>(ctx))

// Device FIDL thunks
fuchsia_hardware_intel_hda_CodecDevice_ops_t CodecConnection::CODEC_FIDL_THUNKS = {
    .GetChannel = [](void* ctx, fidl_txn_t* txn) -> zx_status_t { return DEV->GetChannel(txn); },
};

zx_protocol_device_t CodecConnection::CODEC_DEVICE_THUNKS = []() {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.message = [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
    return fuchsia_hardware_intel_hda_CodecDevice_dispatch(ctx, txn, msg,
                                                           &CodecConnection::CODEC_FIDL_THUNKS);
  };
  return ops;
}();

ihda_codec_protocol_ops_t CodecConnection::CODEC_PROTO_THUNKS = {
    .get_driver_channel = [](void* ctx, zx_handle_t* channel_out) -> zx_status_t {
      ZX_DEBUG_ASSERT(ctx);
      return DEV->CodecGetDispatcherChannel(channel_out);
    },
};
#undef DEV

CodecConnection::CodecConnection(IntelHDAController& controller, uint8_t codec_id)
    : controller_(controller), codec_id_(codec_id) {
  ::memset(&dev_props_, 0, sizeof(dev_props_));
  dev_props_[PROP_PROTOCOL].id = BIND_PROTOCOL;
  dev_props_[PROP_PROTOCOL].value = ZX_PROTOCOL_IHDA_CODEC;
  default_domain_ = dispatcher::ExecutionDomain::Create();

  const auto& info = controller_.dev_info();
  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA Codec %02x:%02x.%01x/%02x", info.bus_id,
           info.dev_id, info.func_id, codec_id_);
}

fbl::RefPtr<CodecConnection> CodecConnection::Create(IntelHDAController& controller,
                                                     uint8_t codec_id) {
  ZX_DEBUG_ASSERT(codec_id < HDA_MAX_CODECS);

  fbl::AllocChecker ac;
  auto ret = fbl::AdoptRef(new (&ac) CodecConnection(controller, codec_id));
  if (!ac.check()) {
    GLOBAL_LOG(ERROR, "Out of memory attempting to allocate codec\n");
    return nullptr;
  }

  if (ret->default_domain_ == nullptr) {
    LOG_EX(ERROR, *ret, "Out of memory attempting to allocate execution domain\n");
    return nullptr;
  }

  return ret;
}

zx_status_t CodecConnection::Startup() {
  ZX_DEBUG_ASSERT(state_ == State::PROBING);

  for (size_t i = 0; i < countof(PROBE_COMMANDS); ++i) {
    CodecCommand cmd(id(), 0u, PROBE_COMMANDS[i].verb);
    auto job = CodecCmdJobAllocator::New(cmd);

    if (job == nullptr) {
      LOG(ERROR, "Failed to allocate job during initial codec probe!\n");
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t res = controller_.QueueCodecCmd(std::move(job));
    if (res != ZX_OK) {
      LOG(ERROR, "Failed to queue job (res = %d) during initial codec probe!\n", res);
      return res;
    }
  }

  return ZX_OK;
}

void CodecConnection::SendCORBResponse(const fbl::RefPtr<dispatcher::Channel>& channel,
                                       const CodecResponse& resp, uint32_t transaction_id) {
  ZX_DEBUG_ASSERT(channel != nullptr);
  ihda_codec_send_corb_cmd_resp_t payload;

  payload.hdr.transaction_id = transaction_id;
  payload.hdr.cmd = IHDA_CODEC_SEND_CORB_CMD;
  payload.data = resp.data;
  payload.data_ex = resp.data_ex;

  zx_status_t res = channel->Write(&payload, sizeof(payload));
  if (res != ZX_OK) {
    LOG(DEBUG, "Error writing CORB response (%08x, %08x) res = %d\n", resp.data, resp.data_ex, res);
  }
}

void CodecConnection::ProcessSolicitedResponse(const CodecResponse& resp,
                                               std::unique_ptr<CodecCmdJob>&& job) {
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
      LOG(ERROR, "Error parsing solicited response during codec probe! (data %08x)\n", resp.data);

      // TODO(johngro) : shutdown and cleanup somehow.
      state_ = State::FATAL_ERROR;
    }
  } else if (job->response_channel() != nullptr) {
    LOG(TRACE, "Sending solicited response [%08x, %08x] to channel %p\n", resp.data, resp.data_ex,
        job->response_channel().get());

    // Does this job have a response channel?  If so, attempt to send the
    // response back on the channel (assuming that it is still open).
    SendCORBResponse(job->response_channel(), resp, job->transaction_id());
  }
}

void CodecConnection::ProcessUnsolicitedResponse(const CodecResponse& resp) {
  // If we still have a channel to our codec driver, grab a reference to it
  // and send the unsolicited response to it.
  fbl::RefPtr<dispatcher::Channel> codec_driver_channel;
  {
    fbl::AutoLock codec_driver_channel_lock(&codec_driver_channel_lock_);
    codec_driver_channel = codec_driver_channel_;
  }

  if (codec_driver_channel)
    SendCORBResponse(codec_driver_channel, resp);
}

void CodecConnection::ProcessWakeupEvt() {
  // TODO(johngro) : handle wakeup events.  Wakeup events are delivered for
  // two reasons.
  //
  // 1) The codec had brought the controller out of a low power state for some
  //    reason.
  // 2) The codec has been hot-unplugged.
  //
  // Currently, we support neither power management, nor hot-unplug.  Just log
  // the fact that we have been woken up and do nothing.
  LOG(WARNING, "Wakeup event received - Don't know how to handle this yet!\n");
}

void CodecConnection::Shutdown() {
  // Close all existing connections and synchronize with any client threads
  // who are currently processing requests.
  state_ = State::SHUTTING_DOWN;
  default_domain_->Deactivate();

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

zx_status_t CodecConnection::PublishDevice() {
  // Generate our name.
  char name[ZX_DEVICE_NAME_MAX];
  snprintf(name, sizeof(name), "intel-hda-codec-%03u", codec_id_);

  // Initialize our device and fill out the protocol hooks
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = name;
  args.ctx = this;
  args.ops = &CODEC_DEVICE_THUNKS;
  args.proto_id = ZX_PROTOCOL_IHDA_CODEC;
  args.proto_ops = &CODEC_PROTO_THUNKS;
  args.props = dev_props_;
  args.prop_count = countof(dev_props_);

  // Publish the device.
  zx_status_t res = device_add(controller_.dev_node(), &args, &dev_node_);
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to add codec device for \"%s\" (res %d)\n", name, res);
    return res;
  }

  return ZX_OK;
}

zx_status_t CodecConnection::ParseVidDid(const CodecResponse& resp) {
  props_.vid = static_cast<uint16_t>((resp.data >> 16) & 0xFFFF);
  props_.did = static_cast<uint16_t>(resp.data & 0xFFFF);

  SET_DEVICE_PROP(VID, props_.vid);
  SET_DEVICE_PROP(DID, props_.did);

  return (props_.vid != 0) ? ZX_OK : ZX_ERR_INTERNAL;
}

zx_status_t CodecConnection::ParseRevisionId(const CodecResponse& resp) {
  props_.ihda_vmaj = static_cast<uint8_t>((resp.data >> 20) & 0xF);
  props_.ihda_vmin = static_cast<uint8_t>((resp.data >> 16) & 0xF);
  props_.rev_id = static_cast<uint8_t>((resp.data >> 8) & 0xFF);
  props_.step_id = static_cast<uint8_t>(resp.data & 0xFF);

  SET_DEVICE_PROP(MAJOR_REV, props_.ihda_vmaj);
  SET_DEVICE_PROP(MINOR_REV, props_.ihda_vmin);
  SET_DEVICE_PROP(VENDOR_REV, props_.rev_id);
  SET_DEVICE_PROP(VENDOR_STEP, props_.step_id);

  state_ = State::FINDING_DRIVER;
  return PublishDevice();
}

zx_status_t CodecConnection::GetChannel(fidl_txn_t* txn) {
  dispatcher::Channel::ProcessHandler phandler(
      [codec = fbl::RefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
        return codec->ProcessUserRequest(channel);
      });

  zx::channel remote_endpoint_out;
  zx_status_t res = CreateAndActivateChannel(default_domain_, std::move(phandler), nullptr, nullptr,
                                             &remote_endpoint_out);

  if (res != ZX_OK) {
    return res;
  }

  return fuchsia_hardware_intel_hda_CodecDeviceGetChannel_reply(txn, remote_endpoint_out.release());
}

#define PROCESS_CMD(_req_ack, _ioctl, _payload, _handler)                    \
  case _ioctl:                                                               \
    if (req_size != sizeof(req._payload)) {                                  \
      LOG(DEBUG, "Bad " #_payload " request length (%u != %zu)\n", req_size, \
          sizeof(req._payload));                                             \
      return ZX_ERR_INVALID_ARGS;                                            \
    }                                                                        \
    if ((_req_ack) && (req.hdr.cmd & IHDA_NOACK_FLAG)) {                     \
      LOG(DEBUG, "Cmd " #_payload                                            \
                 " requires acknowledgement, but the "                       \
                 "NOACK flag was set!\n");                                   \
      return ZX_ERR_INVALID_ARGS;                                            \
    }                                                                        \
    return _handler(channel, req._payload)

zx_status_t CodecConnection::ProcessCodecRequest(dispatcher::Channel* channel) {
  zx_status_t res;
  uint32_t req_size;
  union {
    ihda_proto::CmdHdr hdr;
    ihda_proto::SendCORBCmdReq corb_cmd;
    ihda_proto::RequestStreamReq request_stream;
    ihda_proto::ReleaseStreamReq release_stream;
    ihda_proto::SetStreamFmtReq set_stream_fmt;
  } req;
  // TODO(johngro) : How large is too large?
  static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

  // Read the user request.
  ZX_DEBUG_ASSERT(channel != nullptr);
  res = channel->Read(&req, sizeof(req), &req_size);
  if (res != ZX_OK) {
    LOG(DEBUG, "Failed to read user request (res %d)\n", res);
    return res;
  }

  // Sanity checks.
  if (req_size < sizeof(req.hdr)) {
    LOG(DEBUG, "Client request too small to contain header (%u < %zu)\n", req_size,
        sizeof(req.hdr));
    return ZX_ERR_INVALID_ARGS;
  }

  auto cmd_id = static_cast<ihda_cmd_t>(req.hdr.cmd & ~IHDA_NOACK_FLAG);
  if (req.hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID) {
    LOG(DEBUG, "Invalid transaction ID in client request 0x%04x\n", cmd_id);
    return ZX_ERR_INVALID_ARGS;
  }

  // Dispatch
  LOG(TRACE, "Codec Request (cmd 0x%04x tid %u) len %u\n", req.hdr.cmd, req.hdr.transaction_id,
      req_size);

  switch (cmd_id) {
    PROCESS_CMD(true, IHDA_CODEC_REQUEST_STREAM, request_stream, ProcessRequestStream);
    PROCESS_CMD(false, IHDA_CODEC_RELEASE_STREAM, release_stream, ProcessReleaseStream);
    PROCESS_CMD(false, IHDA_CODEC_SET_STREAM_FORMAT, set_stream_fmt, ProcessSetStreamFmt);
    PROCESS_CMD(false, IHDA_CODEC_SEND_CORB_CMD, corb_cmd, ProcessSendCORBCmd);
    default:
      LOG(DEBUG, "Unrecognized command ID 0x%04x\n", req.hdr.cmd);
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t CodecConnection::ProcessUserRequest(dispatcher::Channel* channel) {
  zx_status_t res;
  uint32_t req_size;
  union {
    ihda_proto::CmdHdr hdr;
    ihda_proto::GetIDsReq get_ids;
    ihda_proto::SendCORBCmdReq corb_cmd;
  } req;
  // TODO(johngro) : How large is too large?
  static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

  // Read the client request.
  ZX_DEBUG_ASSERT(channel != nullptr);
  res = channel->Read(&req, sizeof(req), &req_size);
  if (res != ZX_OK) {
    LOG(DEBUG, "Failed to read client request (res %d)\n", res);
    return res;
  }

  // Sanity checks.
  if (req_size < sizeof(req.hdr)) {
    LOG(DEBUG, "Client request too small to contain header (%u < %zu)\n", req_size,
        sizeof(req.hdr));
    return ZX_ERR_INVALID_ARGS;
  }

  auto cmd_id = static_cast<ihda_cmd_t>(req.hdr.cmd & ~IHDA_NOACK_FLAG);
  if (req.hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID) {
    LOG(DEBUG, "Invalid transaction ID in client request 0x%04x\n", cmd_id);
    return ZX_ERR_INVALID_ARGS;
  }

  // Dispatch
  LOG(TRACE, "User Request (cmd 0x%04x tid %u) len %u\n", req.hdr.cmd, req.hdr.transaction_id,
      req_size);

  // We only allow CORB "get" requests.
  if (cmd_id == IHDA_CODEC_SEND_CORB_CMD && CodecVerb(req.corb_cmd.verb).is_set()) {
    LOG(DEBUG, "User attempted to perform privileged command.\n");
    return ZX_ERR_ACCESS_DENIED;
  }

  switch (cmd_id) {
    PROCESS_CMD(true, IHDA_CMD_GET_IDS, get_ids, ProcessGetIDs);
    PROCESS_CMD(false, IHDA_CODEC_SEND_CORB_CMD, corb_cmd, ProcessSendCORBCmd);
    default:
      LOG(DEBUG, "Unrecognized command ID 0x%04x\n", req.hdr.cmd);
      return ZX_ERR_INVALID_ARGS;
  }
}

#undef PROCESS_CMD

void CodecConnection::ProcessCodecDeactivate(const dispatcher::Channel* channel) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  // This should be the driver channel (client channels created with IOCTL do
  // not register a deactivate handler).  Start by releasing the internal
  // channel reference from within the codec_driver_channel_lock.
  {
    fbl::AutoLock lock(&codec_driver_channel_lock_);
    ZX_DEBUG_ASSERT(channel == codec_driver_channel_.get());
    codec_driver_channel_.reset();
  }

  // Return any DMA streams the codec driver had owned back to the controller.
  IntelHDAStream::Tree tmp;
  {
    fbl::AutoLock lock(&active_streams_lock_);
    tmp = std::move(active_streams_);
  }

  while (!tmp.is_empty()) {
    auto stream = tmp.pop_front();
    stream->Deactivate();
    controller_.ReturnStream(std::move(stream));
  }
}

zx_status_t CodecConnection::ProcessGetIDs(dispatcher::Channel* channel,
                                           const ihda_proto::GetIDsReq& req) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  ihda_proto::GetIDsResp resp;
  resp.hdr = req.hdr;
  resp.vid = props_.vid;
  resp.did = props_.did;
  resp.ihda_vmaj = props_.ihda_vmaj;
  resp.ihda_vmin = props_.ihda_vmin;
  resp.rev_id = props_.rev_id;
  resp.step_id = props_.step_id;

  return channel->Write(&resp, sizeof(resp));
}

zx_status_t CodecConnection::ProcessSendCORBCmd(dispatcher::Channel* channel,
                                                const ihda_proto::SendCORBCmdReq& req) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  CodecVerb verb(req.verb);

  // Make sure that the command is well formed.
  if (!CodecCommand::SanityCheck(id(), req.nid, verb)) {
    LOG(DEBUG, "Bad SEND_CORB_CMD request values [%u, %hu, 0x%05x]\n", id(), req.nid, verb.val);
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<dispatcher::Channel> chan_ref =
      (req.hdr.cmd & IHDA_NOACK_FLAG) ? nullptr : fbl::RefPtr(channel);

  auto job = CodecCmdJobAllocator::New(std::move(chan_ref), req.hdr.transaction_id,
                                       CodecCommand(id(), req.nid, verb));

  if (job == nullptr)
    return ZX_ERR_NO_MEMORY;

  zx_status_t res = controller_.QueueCodecCmd(std::move(job));
  if (res != ZX_OK) {
    LOG(DEBUG, "Failed to queue CORB command [%u, %hu, 0x%05x] (res %d)\n", id(), req.nid, verb.val,
        res);
  }

  return res;
}

zx_status_t CodecConnection::ProcessRequestStream(dispatcher::Channel* channel,
                                                  const ihda_proto::RequestStreamReq& req) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  ihda_proto::RequestStreamResp resp;
  resp.hdr = req.hdr;

  // Attempt to get a stream of the proper type.
  auto type = req.input ? IntelHDAStream::Type::INPUT : IntelHDAStream::Type::OUTPUT;
  auto stream = controller_.AllocateStream(type);

  if (stream != nullptr) {
    // Success, send its ID and its tag back to the codec and add it to the
    // set of active streams owned by this codec.
    resp.result = ZX_OK;
    resp.stream_id = stream->id();
    resp.stream_tag = stream->tag();

    fbl::AutoLock lock(&active_streams_lock_);
    active_streams_.insert(std::move(stream));
  } else {
    // Failure; tell the codec that we are out of streams.
    resp.result = ZX_ERR_NO_MEMORY;
    resp.stream_id = 0;
    resp.stream_tag = 0;
  }

  return channel->Write(&resp, sizeof(resp));
}

zx_status_t CodecConnection::ProcessReleaseStream(dispatcher::Channel* channel,
                                                  const ihda_proto::ReleaseStreamReq& req) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  // Remove the stream from the active set.
  fbl::RefPtr<IntelHDAStream> stream;
  {
    fbl::AutoLock lock(&active_streams_lock_);
    stream = active_streams_.erase(req.stream_id);
  }

  // If the stream was not active, our codec driver has some sort of internal
  // inconsistency.  Hang up the phone on it.
  if (stream == nullptr)
    return ZX_ERR_BAD_STATE;

  // Give the stream back to the controller and (if an ack was requested) tell
  // our codec driver that things went well.
  stream->Deactivate();
  controller_.ReturnStream(std::move(stream));

  if (req.hdr.cmd & IHDA_NOACK_FLAG)
    return ZX_OK;

  ihda_proto::RequestStreamResp resp;
  resp.hdr = req.hdr;
  return channel->Write(&resp, sizeof(resp));
}

zx_status_t CodecConnection::ProcessSetStreamFmt(dispatcher::Channel* channel,
                                                 const ihda_proto::SetStreamFmtReq& req) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  // Sanity check the requested format.
  if (!StreamFormat(req.format).SanityCheck()) {
    LOG(DEBUG, "Invalid encoded stream format 0x%04hx!\n", req.format);
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

  // If the stream was not active, our codec driver has some sort of internal
  // inconsistency.  Hang up the phone on it.
  if (stream == nullptr)
    return ZX_ERR_BAD_STATE;

  // Set the stream format and assign the client channel to the stream.  If
  // this stream is already bound to a client, this will cause that connection
  // to be closed.
  zx::channel client_channel;
  zx_status_t res = stream->SetStreamFormat(default_domain_, req.format, &client_channel);
  if (res != ZX_OK) {
    LOG(DEBUG, "Failed to set stream format 0x%04hx for stream %hu (res %d)\n", req.format,
        req.stream_id, res);
    return res;
  }

  // Send the channel back to the codec driver.
  ZX_DEBUG_ASSERT(client_channel.is_valid());
  ihda_proto::SetStreamFmtResp resp;
  resp.hdr = req.hdr;
  res = channel->Write(&resp, sizeof(resp), std::move(client_channel));

  if (res != ZX_OK)
    LOG(DEBUG, "Failed to send stream channel back to codec driver (res %d)\n", res);

  return res;
}

zx_status_t CodecConnection::CodecGetDispatcherChannel(zx_handle_t* remote_endpoint_out) {
  if (!remote_endpoint_out)
    return ZX_ERR_INVALID_ARGS;

  dispatcher::Channel::ProcessHandler phandler(
      [codec = fbl::RefPtr(this)](dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
        return codec->ProcessCodecRequest(channel);
      });

  dispatcher::Channel::ChannelClosedHandler chandler(
      [codec = fbl::RefPtr(this)](const dispatcher::Channel* channel) -> void {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(t, codec->default_domain_);
        codec->ProcessCodecDeactivate(channel);
      });

  // Enter the driver channel lock.  If we have already connected to a codec
  // driver, simply fail the request.  Otherwise, attempt to build a driver channel
  // and activate it.
  fbl::AutoLock lock(&codec_driver_channel_lock_);

  if (codec_driver_channel_ != nullptr)
    return ZX_ERR_BAD_STATE;

  zx::channel client_channel;
  zx_status_t res;
  res = CreateAndActivateChannel(default_domain_, std::move(phandler), std::move(chandler),
                                 &codec_driver_channel_, &client_channel);
  if (res == ZX_OK) {
    // If things went well, release the reference to the remote endpoint
    // from the zx::channel instance into the unmanaged world of DDK
    // protocols.
    *remote_endpoint_out = client_channel.release();
  }

  return res;
}

}  // namespace intel_hda
}  // namespace audio
