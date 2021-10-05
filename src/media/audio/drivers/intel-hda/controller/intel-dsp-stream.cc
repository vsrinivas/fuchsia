// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-stream.h"

#include <zircon/device/audio.h>

#include <algorithm>
#include <utility>

#include <audio-proto-utils/format-utils.h>

#include "intel-dsp.h"

namespace {
namespace audio_fidl = fuchsia_hardware_audio;
}  // namespace

namespace audio {
namespace intel_hda {

IntelDspStream::IntelDspStream(const DspStream& stream)
    : IntelHDAStreamBase(stream.stream_id, stream.is_input), stream_(stream) {
  snprintf(log_prefix_, sizeof(log_prefix_), "IHDA DSP %cStream #%u", stream.is_input ? 'I' : 'O',
           stream.stream_id);

  SetPersistentUniqueId(stream.uid);
}

void IntelDspStream::CreateRingBuffer(StreamChannel* channel, audio_fidl::wire::Format format,
                                      ::fidl::ServerEnd<audio_fidl::RingBuffer> ring_buffer,
                                      StreamChannel::CreateRingBufferCompleter::Sync& completer) {
  // The DSP needs to coordinate with ring buffer commands. Set up an additional
  // LLCPP server to intercept messages on the ring buffer channel.

  auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  if (!endpoints.is_ok()) {
    LOG(ERROR, "Could not create end points");
    completer.Close(ZX_ERR_NO_MEMORY);
    return;
  }
  auto [client, server] = *std::move(endpoints);
  fidl::OnUnboundFn<fidl::WireServer<audio_fidl::RingBuffer>> on_unbound =
      [this](fidl::WireServer<audio_fidl::RingBuffer>*, fidl::UnbindInfo,
             fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer>) { ring_buffer_.reset(); };

  fidl::BindServer<fidl::WireServer<audio_fidl::RingBuffer>>(dispatcher(), std::move(ring_buffer),
                                                             this, std::move(on_unbound));

  ring_buffer_ = std::move(client);
  IntelHDAStreamBase::CreateRingBuffer(channel, std::move(format), std::move(server), completer);
}

// Pass-through.
void IntelDspStream::GetProperties(GetPropertiesRequestView request,
                                   GetPropertiesCompleter::Sync& completer) {
  auto result = fidl::WireCall(ring_buffer_)->GetProperties();
  if (result.status() != ZX_OK) {
    LOG(ERROR, "Error on GetProperties res = %d", result.status());
    completer.Close(result.status());
  } else {
    completer.Reply(std::move(result->properties));
  }
}

// Pass-through.
void IntelDspStream::GetVmo(GetVmoRequestView request, GetVmoCompleter::Sync& completer) {
  auto result = fidl::WireCall(ring_buffer_)
                    ->GetVmo(request->min_frames, request->clock_recovery_notifications_per_ring);
  if (result.status() != ZX_OK) {
    LOG(ERROR, "Error on GetVmo res = %d", result.status());
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
  } else {
    auto& response = result->result.mutable_response();
    completer.ReplySuccess(response.num_frames, std::move(response.ring_buffer));
  }
}

// Not just pass-through, we also start the DSP pipeline.
void IntelDspStream::Start(StartRequestView request, StartCompleter::Sync& completer) {
  fbl::AutoLock lock(obj_lock());
  auto result = fidl::WireCall(ring_buffer_)->Start();
  if (result.status() != ZX_OK) {
    LOG(ERROR, "Error on Start res = %d", result.status());
    completer.Close(result.status());
    return;
  }

  auto dsp = fbl::RefPtr<IntelDsp>::Downcast(parent_codec());
  Status status = dsp->StartPipeline(stream_.id);
  if (!status.ok()) {
    LOG(ERROR, "Error on pipeline start res = %s", status.ToString().c_str());
    completer.Close(status.code());
    return;
  }
  completer.Reply(result->start_time);
}

// Not just pass-through, we also pause the DSP pipeline.
void IntelDspStream::Stop(StopRequestView request, StopCompleter::Sync& completer) {
  fbl::AutoLock lock(obj_lock());
  auto dsp = fbl::RefPtr<IntelDsp>::Downcast(parent_codec());
  Status status = dsp->PausePipeline(stream_.id);
  if (!status.ok()) {
    LOG(ERROR, "Error on pipeline pause res = %s", status.ToString().c_str());
    completer.Close(status.code());
    return;
  }

  auto result = fidl::WireCall(ring_buffer_)->Stop();
  if (result.status() != ZX_OK) {
    LOG(ERROR, "Error on Stop res = %d", result.status());
    completer.Close(result.status());
    return;
  }
  completer.Reply();
}

// Pass-through.
void IntelDspStream::WatchClockRecoveryPositionInfo(
    WatchClockRecoveryPositionInfoRequestView request,
    WatchClockRecoveryPositionInfoCompleter::Sync& completer) {
  auto result = fidl::WireCall(ring_buffer_)->WatchClockRecoveryPositionInfo();
  if (result.status() != ZX_OK) {
    LOG(ERROR, "Error on Watch clock recovery position res = %d", result.status());
  }
  completer.Reply(result->position_info);
}

zx_status_t IntelDspStream::OnActivateLocked() {
  // FIXME(yky) Hardcode supported formats.
  audio_stream_format_range_t fmt;
  fmt.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  fmt.min_frames_per_second = fmt.max_frames_per_second = 48000;
  fmt.min_channels = fmt.max_channels = stream_.host_format.number_of_channels;
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

void IntelDspStream::OnDeactivateLocked() { LOG(DEBUG, "OnDeactivateLocked"); }

void IntelDspStream::OnChannelDeactivateLocked(const StreamChannel& channel) {
  LOG(DEBUG, "OnChannelDeactivateLocked");
}

zx_status_t IntelDspStream::OnDMAAssignedLocked() {
  LOG(DEBUG, "OnDMAAssignedLocked");
  return PublishDeviceLocked();
}

zx_status_t IntelDspStream::OnSolicitedResponseLocked(const CodecResponse& resp) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelDspStream::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelDspStream::BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& req) {
  LOG(DEBUG, "BeginChangeStreamFormatLocked");
  return ZX_OK;
}

zx_status_t IntelDspStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
  LOG(DEBUG, "FinishChangeStreamFormatLocked");
  return ZX_OK;
}

void IntelDspStream::OnGetGainLocked(audio_proto::GainState* out_resp) {
  LOG(DEBUG, "OnGetGainLocked");
  IntelHDAStreamBase::OnGetGainLocked(out_resp);
}

void IntelDspStream::OnSetGainLocked(const audio_proto::SetGainReq& req,
                                     audio_proto::SetGainResp* out_resp) {
  LOG(DEBUG, "OnSetGainLocked");
  IntelHDAStreamBase::OnSetGainLocked(req, out_resp);
}

void IntelDspStream::OnPlugDetectLocked(StreamChannel* response_channel,
                                        audio_proto::PlugDetectResp* out_resp) {
  LOG(DEBUG, "OnPlugDetectLocked");
  IntelHDAStreamBase::OnPlugDetectLocked(response_channel, out_resp);
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
