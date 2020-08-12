// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_SHERLOCK_PDM_INPUT_AUDIO_STREAM_IN_H_
#define SRC_MEDIA_AUDIO_DRIVERS_SHERLOCK_PDM_INPUT_AUDIO_STREAM_IN_H_

#include <lib/device-protocol/pdev.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <optional>

#include <audio-proto/audio-proto.h>
#include <soc/aml-common/aml-pdm-audio.h>

namespace audio {
namespace sherlock {

class SherlockAudioStreamIn : public SimpleAudioStream {
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
  void ShutdownHook() __TA_REQUIRES(domain_token()) override;

 protected:
  SherlockAudioStreamIn(zx_device_t* parent);
  ~SherlockAudioStreamIn() = default;
  std::unique_ptr<AmlPdmDevice> pdm_;

 private:
  friend class SimpleAudioStream;
  friend class fbl::RefPtr<SherlockAudioStreamIn>;

  zx_status_t AddFormats() __TA_REQUIRES(domain_token());
  zx_status_t InitBuffer(size_t size) TA_REQ(domain_token());
  zx_status_t InitPDev() TA_REQ(domain_token());
  void InitHw();
  void ProcessRingNotification();

  uint32_t us_per_notification_ = 0;
  uint32_t frames_per_second_ = 0;
  uint64_t channels_to_use_bitmask_ = AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED;
  uint8_t number_of_channels_ = 2;
  async::TaskClosureMethod<SherlockAudioStreamIn, &SherlockAudioStreamIn::ProcessRingNotification>
      notify_timer_ TA_GUARDED(domain_token()){this};
  std::optional<ddk::PDev> pdev_ TA_GUARDED(domain_token());
  zx::vmo ring_buffer_vmo_ TA_GUARDED(domain_token());
  fzl::PinnedVmo pinned_ring_buffer_ TA_GUARDED(domain_token());
  zx::bti bti_ TA_GUARDED(domain_token());
};
}  // namespace sherlock
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_SHERLOCK_PDM_INPUT_AUDIO_STREAM_IN_H_
