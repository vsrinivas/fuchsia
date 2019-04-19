// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <optional>

#include <audio-proto/audio-proto.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform/device.h>
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
#include <soc/mt8167/mt8167-audio-out.h>
#include <zircon/thread_annotations.h>

#include "codec.h"

namespace audio {
namespace mt8167 {

class Mt8167AudioStreamOut : public SimpleAudioStream {

protected:
    zx_status_t Init() TA_REQ(domain_->token()) override;
    zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
        TA_REQ(domain_->token()) override;
    zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                          uint32_t* out_num_rb_frames,
                          zx::vmo* out_buffer) TA_REQ(domain_->token()) override;
    zx_status_t Start(uint64_t* out_start_time) TA_REQ(domain_->token()) override;
    zx_status_t Stop() TA_REQ(domain_->token()) override;
    zx_status_t SetGain(const audio_proto::SetGainReq& req)
        TA_REQ(domain_->token()) override;
    void ShutdownHook() TA_REQ(domain_->token()) override;
    zx_status_t InitPost() override;

private:
    friend class SimpleAudioStream;
    friend class fbl::RefPtr<Mt8167AudioStreamOut>;

    Mt8167AudioStreamOut(zx_device_t* parent);
    ~Mt8167AudioStreamOut() {}

    zx_status_t AddFormats() TA_REQ(domain_->token());
    zx_status_t InitBuffer(size_t size) TA_REQ(domain_->token());
    zx_status_t InitPdev() TA_REQ(domain_->token());
    zx_status_t ProcessRingNotification();

    uint32_t us_per_notification_ = 0;
    fbl::RefPtr<dispatcher::Timer> notify_timer_;
    ddk::PDev pdev_ TA_GUARDED(domain_->token());
    std::unique_ptr<Codec> codec_;
    zx::vmo ring_buffer_vmo_ TA_GUARDED(domain_->token());
    fzl::PinnedVmo pinned_ring_buffer_ TA_GUARDED(domain_->token());
    std::unique_ptr<MtAudioOutDevice> mt_audio_;
    ddk::GpioProtocolClient codec_reset_ TA_GUARDED(domain_->token());
    ddk::GpioProtocolClient codec_mute_ TA_GUARDED(domain_->token());
    zx::bti bti_ TA_GUARDED(domain_->token());
};

} // namespace mt8167
} // namespace audio
