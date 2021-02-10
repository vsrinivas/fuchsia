// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-stream.h"

#include <zircon/device/audio.h>

#include <algorithm>
#include <utility>

#include <audio-proto-utils/format-utils.h>
#include <fbl/auto_call.h>

#include "intel-dsp.h"

namespace audio {
namespace intel_hda {

IntelDspStream::IntelDspStream(uint32_t id, bool is_input, const DspPipeline& pipeline,
                               fbl::String name, const audio_stream_unique_id_t* unique_id)
    : IntelHDAStreamBase(id, is_input), name_(std::move(name)), pipeline_(pipeline) {
  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %cStream #%u", is_input ? 'I' : 'O', id);

  if (unique_id) {
    SetPersistentUniqueId(*unique_id);
  } else {
    const audio_stream_unique_id_t uid = {'I',
                                          'D',
                                          'S',
                                          'P',
                                          static_cast<uint8_t>(id >> 24),
                                          static_cast<uint8_t>(id >> 16),
                                          static_cast<uint8_t>(id >> 8),
                                          static_cast<uint8_t>(id),
                                          static_cast<uint8_t>(is_input),
                                          0};
    SetPersistentUniqueId(uid);
  }
}

zx_status_t IntelDspStream::ProcessSetStreamFmt(const ihda_proto::SetStreamFmtResp& codec_resp,
                                                zx::channel&& ring_buffer_channel) {
  ZX_DEBUG_ASSERT(ring_buffer_channel.is_valid());

  fbl::AutoLock lock(obj_lock());
  zx_status_t res = ZX_OK;

  // Are we shutting down?
  if (!is_active()) {
    return ZX_ERR_BAD_STATE;
  }

  // The DSP needs to coordinate with ring buffer commands. Set up an additional
  // channel to intercept messages on the ring buffer channel.

  zx::channel channel_local;
  zx::channel channel_remote;
  res = zx::channel::create(0, &channel_local, &channel_remote);
  if (res != ZX_OK) {
    return res;
  }

  client_rb_channel_ = Channel::Create(std::move(channel_local));
  if (client_rb_channel_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  client_rb_channel_->SetHandler(
      [stream = fbl::RefPtr(this)](async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
        stream->ClientRingBufferChannelSignalled(dispatcher, wait, status, signal);
      });

  ZX_DEBUG_ASSERT(channel_remote.is_valid());

  audio_proto::StreamSetFmtResp resp = {};
  // Respond to the caller, transferring the DMA handle back in the process.
  resp.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;
  resp.hdr.transaction_id = set_format_tid();
  resp.result = ZX_OK;
  resp.external_delay_nsec = 0;  // report his properly based on the codec path delay.
  res = stream_channel()->Write(&resp, sizeof(resp), std::move(channel_remote));
  if (res != ZX_OK) {
    return res;
  }

  rb_channel_ = Channel::Create(std::move(ring_buffer_channel));
  if (rb_channel_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  rb_channel_->SetHandler([stream = fbl::RefPtr(this), channel = rb_channel_.get()](
                              async_dispatcher_t* dispatcher, async::WaitBase* wait,
                              zx_status_t status, const zx_packet_signal_t* signal) {
    stream->RingBufferChannelSignalled(dispatcher, wait, status, signal);
  });

  // We do not start the wait until we have rb_channel_ and client_rb_channel_ set.
  res = client_rb_channel_->BeginWait(dispatcher());
  if (res != ZX_OK) {
    client_rb_channel_.reset();
    rb_channel_.reset();
    return res;
  }
  res = rb_channel_->BeginWait(dispatcher());
  if (res != ZX_OK) {
    rb_channel_.reset();
    client_rb_channel_.reset();
    return res;
  }

  // Let the implementation send the commands required to finish changing the
  // stream format.
  res = FinishChangeStreamFormatLocked(encoded_fmt());
  if (res != ZX_OK) {
    LOG(ERROR, "Failed to finish set format (enc fmt 0x%04hx res %d)\n", encoded_fmt(), res);
    goto finished;
  }

  // If we don't have a set format operation in flight, or the stream channel
  // has been closed, this set format operation has been canceled.  Do not
  // return an error up the stack; we don't want to close the connection to
  // our codec device.
  if ((set_format_tid() == AUDIO_INVALID_TRANSACTION_ID) || (stream_channel() == nullptr)) {
    goto finished;
  }

finished:
  // Something went fatally wrong when trying to send the result back to the
  // caller.  Close the stream channel.
  if ((res != ZX_OK) && (stream_channel() != nullptr)) {
    OnChannelDeactivateLocked(*stream_channel());
    stream_channel() = nullptr;
  }

  // One way or the other, this set format operation is finished.  Clear out
  // the in-flight transaction ID
  SetFormatTidLocked(AUDIO_INVALID_TRANSACTION_ID);

  return ZX_OK;
}

void IntelDspStream::RingBufferChannelSignalled(async_dispatcher_t* dispatcher,
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
    fbl::AutoLock lock(obj_lock());
    zx_status_t status = ProcessRbRequestLocked(rb_channel_.get());
    if (status != ZX_OK) {
      peer_closed_asserted = true;
    }
  }
  if (peer_closed_asserted) {
    ProcessRbDeactivate();
  } else if (readable_asserted) {
    wait->Begin(dispatcher);
  }
}

void IntelDspStream::ClientRingBufferChannelSignalled(async_dispatcher_t* dispatcher,
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
    fbl::AutoLock lock(obj_lock());
    zx_status_t status = ProcessClientRbRequestLocked(client_rb_channel_.get());
    if (status != ZX_OK) {
      peer_closed_asserted = true;
    }
  }
  if (peer_closed_asserted) {
    ProcessClientRbDeactivate();
  } else if (readable_asserted) {
    wait->Begin(dispatcher);
  }
}

zx_status_t IntelDspStream::ProcessRbRequestLocked(Channel* channel) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  // If we have lost our connection to the codec device, or are in the process
  // of shutting down, there is nothing further we can do.  Fail the request
  // and close the connection to the caller.
  if (!is_active() || (rb_channel_ == nullptr) || (client_rb_channel_ == nullptr)) {
    return ZX_ERR_BAD_STATE;
  }

  zx::handle rxed_handle;
  uint32_t req_size;
  union {
    audio_proto::CmdHdr hdr;
    audio_proto::RingBufGetFifoDepthResp get_fifo_depth;
    audio_proto::RingBufGetBufferResp get_buffer;
    audio_proto::RingBufStartResp start;
    audio_proto::RingBufStopResp stop;
  } req;
  // TODO(johngro) : How large is too large?
  static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

  zx_status_t res = channel->Read(&req, sizeof(req), &req_size, rxed_handle);
  if (res != ZX_OK) {
    return res;
  }

  switch (req.hdr.cmd) {
    case AUDIO_RB_CMD_START: {
      auto dsp = fbl::RefPtr<IntelDsp>::Downcast(parent_codec());
      Status status = dsp->StartPipeline(pipeline_);
      if (!status.ok()) {
        LOG(ERROR, "Failed to start ring buffer: %s\n", status.ToString().c_str());
        audio_proto::RingBufStartResp resp = {};
        resp.hdr = req.hdr;
        resp.result = status.code();
        return client_rb_channel_->Write(&resp, sizeof(resp));
      }
      break;
    }
    default:
      break;
  }

  return client_rb_channel_->Write(&req, req_size, std::move(rxed_handle));
}

void IntelDspStream::ProcessRbDeactivate() {
  fbl::AutoLock lock(obj_lock());

  LOG(DEBUG, "ProcessClientRbDeactivate\n");

  rb_channel_ = nullptr;

  // Deactivate the client channel.
  if (client_rb_channel_ != nullptr) {
    client_rb_channel_ = nullptr;
  }
}

zx_status_t IntelDspStream::ProcessClientRbRequestLocked(Channel* channel) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  // If we have lost our connection to the codec device, or are in the process
  // of shutting down, there is nothing further we can do.  Fail the request
  // and close the connection to the caller.
  if (!is_active() || (rb_channel_ == nullptr) || (client_rb_channel_ == nullptr)) {
    return ZX_ERR_BAD_STATE;
  }

  uint32_t req_size;
  union {
    audio_proto::CmdHdr hdr;
    audio_proto::RingBufGetFifoDepthReq get_fifo_depth;
    audio_proto::RingBufGetBufferReq get_buffer;
    audio_proto::RingBufStartReq start;
    audio_proto::RingBufStopReq stop;
  } req;
  // TODO(johngro) : How large is too large?
  static_assert(sizeof(req) <= 256, "Request buffer is too large to hold on the stack!");

  zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
  if (res != ZX_OK) {
    return res;
  }

  switch (req.hdr.cmd) {
    case AUDIO_RB_CMD_STOP: {
      auto dsp = fbl::RefPtr<IntelDsp>::Downcast(parent_codec());
      Status status = dsp->PausePipeline(pipeline_);
      if (!status.ok()) {
        LOG(ERROR, "Failed to stop ring buffer: %s\n", status.ToString().c_str());
        audio_proto::RingBufStopResp resp = {};
        resp.hdr = req.hdr;
        resp.result = status.code();
        return channel->Write(&resp, sizeof(resp));
      }
      break;
    }
    default:
      break;
  }

  return rb_channel_->Write(&req, req_size);
}

void IntelDspStream::ProcessClientRbDeactivate() {
  fbl::AutoLock lock(obj_lock());

  LOG(DEBUG, "ProcessClientRbDeactivate\n");

  client_rb_channel_ = nullptr;

  // Deactivate the upstream channel.
  if (rb_channel_ != nullptr) {
    rb_channel_ = nullptr;
  }
}

zx_status_t IntelDspStream::OnActivateLocked() {
  // FIXME(yky) Hardcode supported formats.
  audio_stream_format_range_t fmt;
  fmt.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  fmt.min_frames_per_second = fmt.max_frames_per_second = 48000;
  fmt.min_channels = fmt.max_channels = 2;
  fmt.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  fbl::Vector<audio_proto::FormatRange> supported_formats;
  fbl::AllocChecker ac;
  supported_formats.push_back(fmt, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  SetSupportedFormatsLocked(std::move(supported_formats));
  return ZX_OK;
}

void IntelDspStream::OnDeactivateLocked() { LOG(DEBUG, "OnDeactivateLocked\n"); }

void IntelDspStream::OnChannelDeactivateLocked(const Channel& channel) {
  LOG(DEBUG, "OnChannelDeactivateLocked\n");
}

zx_status_t IntelDspStream::OnDMAAssignedLocked() {
  LOG(DEBUG, "OnDMAAssignedLocked\n");
  return PublishDeviceLocked();
}

zx_status_t IntelDspStream::OnSolicitedResponseLocked(const CodecResponse& resp) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelDspStream::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelDspStream::BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& req) {
  LOG(DEBUG, "BeginChangeStreamFormatLocked\n");
  return ZX_OK;
}

zx_status_t IntelDspStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
  LOG(DEBUG, "FinishChangeStreamFormatLocked\n");
  return ZX_OK;
}

void IntelDspStream::OnGetGainLocked(audio_proto::GetGainResp* out_resp) {
  LOG(DEBUG, "OnGetGainLocked\n");
  IntelHDAStreamBase::OnGetGainLocked(out_resp);
}

void IntelDspStream::OnSetGainLocked(const audio_proto::SetGainReq& req,
                                     audio_proto::SetGainResp* out_resp) {
  LOG(DEBUG, "OnSetGainLocked\n");
  IntelHDAStreamBase::OnSetGainLocked(req, out_resp);
}

void IntelDspStream::OnPlugDetectLocked(Channel* response_channel,
                                        const audio_proto::PlugDetectReq& req,
                                        audio_proto::PlugDetectResp* out_resp) {
  LOG(DEBUG, "OnPlugDetectLocked\n");
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
      requested_string = name_.c_str();
      break;

    default:
      IntelHDAStreamBase::OnGetStringLocked(req, out_resp);
      return;
  }

  int res = snprintf(reinterpret_cast<char*>(out_resp->str), sizeof(out_resp->str), "%s",
                     requested_string ? requested_string : "<unassigned>");
  ZX_DEBUG_ASSERT(res >= 0);
  out_resp->result = ZX_OK;
  out_resp->strlen = std::min<uint32_t>(res, sizeof(out_resp->str) - 1);
  out_resp->id = req.id;
}

}  // namespace intel_hda
}  // namespace audio
