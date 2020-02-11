// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_NELSON_TDM_OUTPUT_AUDIO_STREAM_OUT_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_NELSON_TDM_OUTPUT_AUDIO_STREAM_OUT_H_

#include <lib/device-protocol/pdev.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <memory>
#include <optional>

#include <audio-proto/audio-proto.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/codec.h>
#include <ddktl/protocol/platform/device.h>
#include <soc/aml-common/aml-tdm-audio.h>

#include "codec.h"

namespace audio {
namespace nelson {

class NelsonAudioStreamOut : public SimpleAudioStream {
 protected:
  zx_status_t Init() TA_REQ(domain_token()) override;
  zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req) TA_REQ(domain_token()) override;
  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) TA_REQ(domain_token()) override;
  zx_status_t Start(uint64_t* out_start_time) TA_REQ(domain_token()) override;
  zx_status_t Stop() TA_REQ(domain_token()) override;
  zx_status_t SetGain(const audio_proto::SetGainReq& req) TA_REQ(domain_token()) override;
  void ShutdownHook() TA_REQ(domain_token()) override;

 private:
  enum {
    kHifiPllClk,
    kClockCount,
  };

  friend class SimpleAudioStream;
  friend class fbl::RefPtr<NelsonAudioStreamOut>;

  NelsonAudioStreamOut(zx_device_t* parent);
  ~NelsonAudioStreamOut() {}

  zx_status_t AddFormats() TA_REQ(domain_token());
  zx_status_t InitBuffer(size_t size) TA_REQ(domain_token());
  zx_status_t InitPdev() TA_REQ(domain_token());
  void ProcessRingNotification() TA_REQ(domain_token());

  uint32_t us_per_notification_ = 0;
  async::TaskClosureMethod<NelsonAudioStreamOut, &NelsonAudioStreamOut::ProcessRingNotification>
      notify_timer_ TA_GUARDED(domain_token()){this};
  ddk::PDev pdev_ TA_GUARDED(domain_token());
  zx::vmo ring_buffer_vmo_ TA_GUARDED(domain_token());
  fzl::PinnedVmo pinned_ring_buffer_;
  std::unique_ptr<AmlTdmDevice> lib_;
  ddk::ClockProtocolClient clks_[kClockCount] TA_GUARDED(domain_token());
  Codec codec_ TA_GUARDED(domain_token());
  zx::bti bti_ TA_GUARDED(domain_token());
};

}  // namespace nelson
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_NELSON_TDM_OUTPUT_AUDIO_STREAM_OUT_H_
