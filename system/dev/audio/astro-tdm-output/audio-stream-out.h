// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/gpio.h>
#include <dispatcher-pool/dispatcher-timer.h>
#include <fbl/mutex.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <audio-proto/audio-proto.h>

#include <soc/aml-common/aml-tdm-audio.h>

#include "tas27xx.h"

namespace audio {
namespace astro {

class AstroAudioStreamOut : public SimpleAudioStream {

public:
    AstroAudioStreamOut(zx_device_t* parent);

protected:
    zx_status_t Init() __TA_REQUIRES(domain_->token()) override;
    zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
        __TA_REQUIRES(domain_->token()) override;
    zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                          uint32_t* out_num_rb_frames,
                          zx::vmo* out_buffer) __TA_REQUIRES(domain_->token()) override;
    zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_->token()) override;
    zx_status_t Stop() __TA_REQUIRES(domain_->token()) override;
    zx_status_t SetGain(const audio_proto::SetGainReq& req)
        __TA_REQUIRES(domain_->token()) override;
    void ShutdownHook() __TA_REQUIRES(domain_->token()) override;
    zx_status_t InitPost() override;

private:
    friend class fbl::RefPtr<AstroAudioStreamOut>;

    static constexpr uint8_t kFifoDepth = 0x20;

    zx_status_t AddFormats() __TA_REQUIRES(domain_->token());;
    zx_status_t InitBuffer(size_t size);
    zx_status_t InitPDev();
    zx_status_t ProcessRingNotification();

    uint32_t us_per_notification_ = 0;

    fbl::RefPtr<dispatcher::Timer> notify_timer_;

    fbl::optional<ddk::PDev> pdev_;

    fbl::unique_ptr<Tas27xx> codec_;

    zx::vmo ring_buffer_vmo_;
    fzl::PinnedVmo pinned_ring_buffer_;

    fbl::unique_ptr<AmlTdmDevice> aml_audio_;
    fbl::optional<ddk::GpioProtocolProxy> audio_en_;
    fbl::optional<ddk::GpioProtocolProxy> audio_fault_;

    zx::bti bti_;
};

} // namespace astro
} // namespace audio
