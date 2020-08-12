// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_SHERLOCK_TDM_OUTPUT_AUDIO_STREAM_OUT_H_
#define SRC_MEDIA_AUDIO_DRIVERS_SHERLOCK_TDM_OUTPUT_AUDIO_STREAM_OUT_H_

#include <lib/device-protocol/pdev.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <audio-proto/audio-proto.h>
#include <ddk/io-buffer.h>
#include <ddk/metadata.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/metadata/audio.h>
#include <ddktl/protocol/gpio.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <soc/aml-common/aml-tdm-audio.h>

#include "../codecs/tas5720/tas5720.h"

namespace audio {
namespace sherlock {

class SherlockAudioStreamOut : public SimpleAudioStream {
 public:
  SherlockAudioStreamOut(zx_device_t* parent);

 protected:
  zx_status_t Init() TA_REQ(domain_token()) override;
  zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req) TA_REQ(domain_token()) override;
  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) TA_REQ(domain_token()) override;
  zx_status_t Start(uint64_t* out_start_time) TA_REQ(domain_token()) override;
  zx_status_t Stop() TA_REQ(domain_token()) override;
  zx_status_t SetGain(const audio_proto::SetGainReq& req) TA_REQ(domain_token()) override;
  void ShutdownHook() TA_REQ(domain_token()) override;

  virtual zx_status_t InitPdev() TA_REQ(domain_token());  // virtual and protected for unit test.
  zx_status_t InitCodecs() TA_REQ(domain_token());        // protected for unit test.
  zx_status_t InitHW() TA_REQ(domain_token());            // protected for unit test.

  fbl::Array<std::unique_ptr<Tas5720>> codecs_
      TA_GUARDED(domain_token());                                // protected for unit test.
  ddk::GpioProtocolClient audio_en_ TA_GUARDED(domain_token());  // protected for unit test.
  std::unique_ptr<AmlTdmDevice> aml_audio_;                      // protected for unit test.

 private:
  friend class fbl::RefPtr<SherlockAudioStreamOut>;

  zx_status_t AddFormats() TA_REQ(domain_token());
  zx_status_t InitBuffer(size_t size) TA_REQ(domain_token());
  zx_status_t SetCodecsGain(float gain) TA_REQ(domain_token());
  void ProcessRingNotification();

  uint32_t us_per_notification_ = 0;
  uint32_t frames_per_second_ = 0;
  uint32_t rb_size_ = 0;
  uint64_t channels_to_use_bitmask_ = AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED;

  async::TaskClosureMethod<SherlockAudioStreamOut, &SherlockAudioStreamOut::ProcessRingNotification>
      notify_timer_ TA_GUARDED(domain_token()){this};
  ddk::PDev pdev_ TA_GUARDED(domain_token());
  metadata::Codec codecs_types_ TA_GUARDED(domain_token());
  zx::vmo ring_buffer_vmo_ TA_GUARDED(domain_token());
  fzl::PinnedVmo pinned_ring_buffer_ TA_GUARDED(domain_token());
  ddk::GpioProtocolClient audio_fault_ TA_GUARDED(domain_token());
  zx::bti bti_ TA_GUARDED(domain_token());
};

}  // namespace sherlock
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_SHERLOCK_TDM_OUTPUT_AUDIO_STREAM_OUT_H_
