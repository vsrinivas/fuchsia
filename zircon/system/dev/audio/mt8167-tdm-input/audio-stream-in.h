// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>

#include <audio-proto/audio-proto.h>
#include <ddktl/pdev.h>
#include <dispatcher-pool/dispatcher-timer.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <soc/mt8167/mt8167-audio-in.h>
#include <zircon/thread_annotations.h>

#include "tlv320adc.h"

namespace audio {
namespace mt8167 {

class Mt8167AudioStreamIn : public SimpleAudioStream {

protected:
    zx_status_t Init() __TA_REQUIRES(domain_->token()) override;
    zx_status_t ChangeFormat(const audio_proto::StreamSetFmtReq& req)
        __TA_REQUIRES(domain_->token()) override;
    zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                          uint32_t* out_num_rb_frames,
                          zx::vmo* out_buffer) __TA_REQUIRES(domain_->token()) override;
    zx_status_t Start(uint64_t* out_start_time) __TA_REQUIRES(domain_->token()) override;
    zx_status_t Stop() __TA_REQUIRES(domain_->token()) override;
    zx_status_t InitPost() override;

private:
    friend class SimpleAudioStream;
    friend class fbl::RefPtr<Mt8167AudioStreamIn>;

    Mt8167AudioStreamIn(zx_device_t* parent);
    ~Mt8167AudioStreamIn() {}

    zx_status_t AddFormats() __TA_REQUIRES(domain_->token());
    zx_status_t InitBuffer(size_t size) TA_REQ(domain_->token());
    zx_status_t InitPdev() TA_REQ(domain_->token());
    zx_status_t ProcessRingNotification();

    uint32_t us_per_notification_ = 0;
    fbl::RefPtr<dispatcher::Timer> notify_timer_;
    ddk::PDev pdev_ TA_GUARDED(domain_->token());
    std::unique_ptr<Tlv320adc> codec_;
    zx::vmo ring_buffer_vmo_ TA_GUARDED(domain_->token());
    fzl::PinnedVmo pinned_ring_buffer_ TA_GUARDED(domain_->token());
    std::unique_ptr<MtAudioInDevice> mt_audio_;
    ddk::GpioProtocolClient codec_reset_ TA_GUARDED(domain_->token());
    zx::bti bti_ TA_GUARDED(domain_->token());
};
} // namespace mt8167
} // namespace audio
