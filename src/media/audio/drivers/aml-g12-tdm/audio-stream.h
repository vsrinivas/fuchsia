// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_AUDIO_STREAM_H_
#define SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_AUDIO_STREAM_H_

#include <lib/device-protocol/pdev.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <audio-proto/audio-proto.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/metadata/audio.h>
#include <ddktl/protocol/gpio.h>
#include <fbl/mutex.h>
#include <soc/aml-common/aml-tdm-audio.h>

namespace audio {
namespace aml_g12 {

class AmlG12TdmStream : public SimpleAudioStream {
 public:
  AmlG12TdmStream(zx_device_t* parent, bool is_input, ddk::PDev pdev,
                  const ddk::GpioProtocolClient enable_gpio);

 protected:
  zx_status_t Init() __TA_REQUIRES(domain_token()) override;
  virtual zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
      __TA_REQUIRES(domain_token()) override;  // virtual for unit testing.
  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) __TA_REQUIRES(domain_token()) override;
  zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_token()) override;
  zx_status_t Stop() __TA_REQUIRES(domain_token()) override;
  zx_status_t SetGain(const audio_proto::SetGainReq& req) __TA_REQUIRES(domain_token()) override;
  void ShutdownHook() __TA_REQUIRES(domain_token()) override;

  // Protected for unit test.
  zx_status_t InitCodec();
  zx_status_t InitHW();
  zx_status_t InitPDev();
  void InitDaiFormats();
  zx_status_t InitCodecsGain() __TA_REQUIRES(domain_token());

  std::vector<SimpleCodecClient> codecs_;
  std::unique_ptr<AmlTdmDevice> aml_audio_;
  metadata::AmlConfig metadata_ = {};

 private:
  friend class fbl::RefPtr<AmlG12TdmStream>;

  static constexpr uint8_t kFifoDepth = 0x20;

  zx_status_t AddFormats() __TA_REQUIRES(domain_token());
  zx_status_t InitBuffer(size_t size);
  void ProcessRingNotification();
  void UpdateCodecsGainState(GainState state) __TA_REQUIRES(domain_token());
  void UpdateCodecsGainStateFromCurrent() __TA_REQUIRES(domain_token());

  uint32_t us_per_notification_ = 0;
  DaiFormat dai_formats_[metadata::kMaxNumberOfCodecs] = {};
  uint32_t frame_rate_ = 0;

  async::TaskClosureMethod<AmlG12TdmStream, &AmlG12TdmStream::ProcessRingNotification> notify_timer_
      __TA_GUARDED(domain_token()){this};

  ddk::PDev pdev_;

  zx::vmo ring_buffer_vmo_;
  fzl::PinnedVmo pinned_ring_buffer_;

  zx::bti bti_;
  const ddk::GpioProtocolClient enable_gpio_;
  uint64_t channels_to_use_;
  bool override_mute_ = true;
};

}  // namespace aml_g12
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_AML_G12_TDM_AUDIO_STREAM_H_
