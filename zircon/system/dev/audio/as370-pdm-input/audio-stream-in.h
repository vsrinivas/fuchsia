// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_AS370_PDM_INPUT_AUDIO_STREAM_IN_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_AS370_PDM_INPUT_AUDIO_STREAM_IN_H_

#include <lib/device-protocol/pdev.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <optional>

#include <audio-proto/audio-proto.h>
#include <ddktl/protocol/clock.h>
#include <soc/as370/syn-audio-in.h>

namespace audio {
namespace as370 {

class As370AudioStreamIn : public SimpleAudioStream {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

 protected:
  zx_status_t Init() TA_REQ(domain_token()) override;
  zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req) TA_REQ(domain_token()) override;
  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) TA_REQ(domain_token()) override;
  zx_status_t Start(uint64_t* out_start_time) TA_REQ(domain_token()) override;
  zx_status_t Stop() TA_REQ(domain_token()) override;
  void RingBufferShutdown() TA_REQ(domain_token()) override;
  void ShutdownHook() TA_REQ(domain_token()) override;

 private:
  static constexpr size_t kMaxRate = 48000;

  enum {
    kAvpll0Clk,
    kClockCount,
  };
  friend class SimpleAudioStream;
  friend class fbl::RefPtr<As370AudioStreamIn>;

  explicit As370AudioStreamIn(zx_device_t* parent);
  ~As370AudioStreamIn() = default;

  zx_status_t AddFormats() TA_REQ(domain_token());
  zx_status_t InitPDev() TA_REQ(domain_token());
  void ProcessRingNotification() TA_REQ(domain_token());

  zx::duration notification_rate_ = {};
  async::TaskClosureMethod<As370AudioStreamIn, &As370AudioStreamIn::ProcessRingNotification>
      notify_timer_ TA_GUARDED(domain_token()){this};
  ddk::PDev pdev_ TA_GUARDED(domain_token());
  zx::vmo ring_buffer_vmo_ TA_GUARDED(domain_token());
  std::unique_ptr<SynAudioInDevice> lib_;
  ddk::ClockProtocolClient clks_[kClockCount] TA_GUARDED(domain_token());
  zx::vmo dma_buffer_ TA_GUARDED(domain_token());
};
}  // namespace as370
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_AS370_PDM_INPUT_AUDIO_STREAM_IN_H_
