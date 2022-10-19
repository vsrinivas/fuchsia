// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_STREAM_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_STREAM_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <lib/ddk/device.h>

#include <intel-hda/codec-utils/dai-base.h>

#include "debug-logging.h"
#include "intel-dsp-topology.h"

namespace audio {
namespace intel_hda {

class IntelDspStream : public codecs::IntelHDADaiBase,
                       public fidl::WireServer<fuchsia_hardware_audio::RingBuffer> {
 public:
  explicit IntelDspStream(const DspStream& stream);

  const char* log_prefix() const { return log_prefix_; }

 protected:
  virtual ~IntelDspStream() {}

  void OnResetLocked() __TA_REQUIRES(obj_lock()) final;
  zx_status_t OnActivateLocked() __TA_REQUIRES(obj_lock()) final;
  void OnDeactivateLocked() __TA_REQUIRES(obj_lock()) final;
  void OnChannelDeactivateLocked(const DaiChannel& channel) __TA_REQUIRES(obj_lock()) final;
  zx_status_t OnDMAAssignedLocked() __TA_REQUIRES(obj_lock()) final;
  zx_status_t OnSolicitedResponseLocked(const CodecResponse& resp) __TA_REQUIRES(obj_lock()) final;
  zx_status_t OnUnsolicitedResponseLocked(const CodecResponse& resp)
      __TA_REQUIRES(obj_lock()) final;
  zx_status_t BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt)
      __TA_REQUIRES(obj_lock()) final;
  zx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt) __TA_REQUIRES(obj_lock()) final;
  void OnGetStringLocked(const audio_proto::GetStringReq& req, audio_proto::GetStringResp* out_resp)
      __TA_REQUIRES(obj_lock()) final;

  void CreateRingBuffer(DaiChannel* channel, fuchsia_hardware_audio::wire::DaiFormat dai_format,
                        fuchsia_hardware_audio::wire::Format ring_buffer_format,
                        ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer,
                        DaiChannel::CreateRingBufferCompleter::Sync& completer) override
      __TA_REQUIRES(obj_lock());

  // fuchsia hardware audio RingBuffer Interface
  void GetProperties(GetPropertiesCompleter::Sync& completer) override;
  void GetVmo(GetVmoRequestView request, GetVmoCompleter::Sync& completer) override;
  void Start(StartCompleter::Sync& completer) override;
  void Stop(StopCompleter::Sync& completer) override;
  void WatchClockRecoveryPositionInfo(
      WatchClockRecoveryPositionInfoCompleter::Sync& completer) override;
  void SetActiveChannels(SetActiveChannelsRequestView request,
                         SetActiveChannelsCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
  void WatchDelayInfo(WatchDelayInfoCompleter::Sync& completer) override;

 private:
  friend class fbl::RefPtr<IntelDspStream>;

  // Device name, exposed to the user.
  const fbl::String name_;

  // Log prefix storage
  char log_prefix_[LOG_PREFIX_STORAGE] = {0};
  const DspStream stream_;
  fidl::ClientEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer_;
  bool delay_info_updated_ = false;
};

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_STREAM_H_
