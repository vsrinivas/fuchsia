// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_NELSON_PDM_INPUT_AUDIO_STREAM_IN_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_NELSON_PDM_INPUT_AUDIO_STREAM_IN_H_

#include <lib/device-protocol/pdev.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <optional>

#include <audio-proto/audio-proto.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/platform/device.h>
#include <soc/aml-common/aml-pdm-audio.h>

namespace audio {
namespace nelson {

class NelsonAudioStreamIn : public SimpleAudioStream {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

 protected:
  zx_status_t Init() __TA_REQUIRES(domain_token()) override;
  zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
      __TA_REQUIRES(domain_token()) override;
  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) __TA_REQUIRES(domain_token()) override;
  zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_token()) override;
  zx_status_t Stop() __TA_REQUIRES(domain_token()) override;
  void RingBufferShutdown() TA_REQ(domain_token()) override;
  void ShutdownHook() __TA_REQUIRES(domain_token()) override;

  enum {
    kHifiPllClk,
    kClockCount,
  };

 private:
  friend class fbl::RefPtr<NelsonAudioStreamIn>;
  friend class SimpleAudioStream;

  explicit NelsonAudioStreamIn(zx_device_t* parent);

  zx_status_t AddFormats() TA_REQ(domain_token());
  zx_status_t InitBuffer(size_t size) TA_REQ(domain_token());
  zx_status_t InitPDev() TA_REQ(domain_token());
  void ProcessRingNotification() TA_REQ(domain_token());

  zx::duration notification_rate_ = {};
  uint32_t frames_per_second_ = 0;
  async::TaskClosureMethod<NelsonAudioStreamIn, &NelsonAudioStreamIn::ProcessRingNotification>
      notify_timer_ __TA_GUARDED(domain_token()){this};

  ddk::PDev pdev_ TA_GUARDED(domain_token());

  zx::vmo ring_buffer_vmo_;
  fzl::PinnedVmo pinned_ring_buffer_;
  std::unique_ptr<AmlPdmDevice> lib_;
  ddk::ClockProtocolClient clks_[kClockCount] TA_GUARDED(domain_token());
  zx::bti bti_;
};
}  // namespace nelson
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_NELSON_PDM_INPUT_AUDIO_STREAM_IN_H_
