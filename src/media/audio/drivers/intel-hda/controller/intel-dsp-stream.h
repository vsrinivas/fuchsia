// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_STREAM_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_STREAM_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <lib/ddk/device.h>

#include <intel-hda/codec-utils/stream-base.h>

#include "debug-logging.h"
#include "intel-dsp-topology.h"

namespace audio {
namespace intel_hda {

class IntelDspStream : public codecs::IntelHDAStreamBase,
                       public fidl::WireServer<fuchsia_hardware_audio::RingBuffer> {
 public:
  IntelDspStream(uint32_t id, bool is_input, const DspPipeline& pipeline, fbl::String name,
                 const audio_stream_unique_id_t* unique_id = nullptr);

  const char* log_prefix() const { return log_prefix_; }

 protected:
  virtual ~IntelDspStream() {}

  zx_status_t OnActivateLocked() __TA_REQUIRES(obj_lock()) final;
  void OnDeactivateLocked() __TA_REQUIRES(obj_lock()) final;
  void OnChannelDeactivateLocked(const StreamChannel& channel) __TA_REQUIRES(obj_lock()) final;
  zx_status_t OnDMAAssignedLocked() __TA_REQUIRES(obj_lock()) final;
  zx_status_t OnSolicitedResponseLocked(const CodecResponse& resp) __TA_REQUIRES(obj_lock()) final;
  zx_status_t OnUnsolicitedResponseLocked(const CodecResponse& resp)
      __TA_REQUIRES(obj_lock()) final;
  zx_status_t BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt)
      __TA_REQUIRES(obj_lock()) final;
  zx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt) __TA_REQUIRES(obj_lock()) final;
  void OnGetGainLocked(audio_proto::GainState* out_resp) __TA_REQUIRES(obj_lock()) final;
  void OnSetGainLocked(const audio_proto::SetGainReq& req, audio_proto::SetGainResp* out_resp)
      __TA_REQUIRES(obj_lock()) final;
  void OnPlugDetectLocked(StreamChannel* response_channel, audio_proto::PlugDetectResp* out_resp)
      __TA_REQUIRES(obj_lock()) final;
  void OnGetStringLocked(const audio_proto::GetStringReq& req, audio_proto::GetStringResp* out_resp)
      __TA_REQUIRES(obj_lock()) final;

  void CreateRingBuffer(StreamChannel* channel, fuchsia_hardware_audio::wire::Format format,
                        ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer,
                        StreamChannel::CreateRingBufferCompleter::Sync& completer) override;

  // fuchsia hardware audio RingBuffer Interface
  void GetProperties(GetPropertiesRequestView request,
                     GetPropertiesCompleter::Sync& completer) override;
  void GetVmo(GetVmoRequestView request, GetVmoCompleter::Sync& completer) override;
  void Start(StartRequestView request, StartCompleter::Sync& completer) override;
  void Stop(StopRequestView request, StopCompleter::Sync& completer) override;
  void WatchClockRecoveryPositionInfo(
      WatchClockRecoveryPositionInfoRequestView request,
      WatchClockRecoveryPositionInfoCompleter::Sync& completer) override;
  void SetActiveChannels(SetActiveChannelsRequestView request,
                         SetActiveChannelsCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  friend class fbl::RefPtr<IntelDspStream>;

  // Device name, exposed to the user.
  const fbl::String name_;

  // Log prefix storage
  char log_prefix_[LOG_PREFIX_STORAGE] = {0};
  const DspPipeline pipeline_;
  fidl::ClientEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer_;
};

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_STREAM_H_
