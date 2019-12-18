// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_ASTRO_PDM_INPUT_AUDIO_STREAM_IN_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_ASTRO_PDM_INPUT_AUDIO_STREAM_IN_H_

#include <lib/device-protocol/pdev.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <optional>

#include <audio-proto/audio-proto.h>
#include <soc/aml-common/aml-pdm-audio.h>

namespace audio {
namespace astro {

class AstroAudioStreamIn : public SimpleAudioStream {
 public:
  AstroAudioStreamIn(zx_device_t* parent);

 protected:
  zx_status_t Init() __TA_REQUIRES(domain_token()) override;
  zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
      __TA_REQUIRES(domain_token()) override;
  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) __TA_REQUIRES(domain_token()) override;
  zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_token()) override;
  zx_status_t Stop() __TA_REQUIRES(domain_token()) override;
  void ShutdownHook() __TA_REQUIRES(domain_token()) override;

 private:
  friend class fbl::RefPtr<AstroAudioStreamIn>;

  zx_status_t AddFormats() __TA_REQUIRES(domain_token());
  zx_status_t InitBuffer(size_t size);
  zx_status_t InitPDev();
  void ProcessRingNotification() __TA_REQUIRES(domain_token());

  uint32_t us_per_notification_ = 0;
  uint32_t frames_per_second_ = 0;
  async::TaskClosureMethod<AstroAudioStreamIn, &AstroAudioStreamIn::ProcessRingNotification>
      notify_timer_ __TA_GUARDED(domain_token()){this};

  std::optional<ddk::PDev> pdev_;

  zx::vmo ring_buffer_vmo_;
  fzl::PinnedVmo pinned_ring_buffer_;

  std::unique_ptr<AmlPdmDevice> pdm_;

  zx::bti bti_;
};
}  // namespace astro
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_ASTRO_PDM_INPUT_AUDIO_STREAM_IN_H_
