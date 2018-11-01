// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/pdev.h>
#include <dispatcher-pool/dispatcher-timer.h>
#include <fbl/optional.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <audio-proto/audio-proto.h>
#include <soc/aml-common/aml-pdm-audio.h>

namespace audio {
namespace astro {

class AstroAudioStreamIn : public SimpleAudioStream {

public:
    AstroAudioStreamIn(zx_device_t* parent);

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
    friend class fbl::RefPtr<AstroAudioStreamIn>;

    zx_status_t AddFormats() __TA_REQUIRES(domain_->token());
    zx_status_t InitBuffer(size_t size);
    zx_status_t InitPDev();
    zx_status_t ProcessRingNotification();

    uint32_t us_per_notification_ = 0;

    fbl::RefPtr<dispatcher::Timer> notify_timer_;

    fbl::optional<ddk::PDev> pdev_;

    zx::vmo ring_buffer_vmo_;
    fzl::PinnedVmo pinned_ring_buffer_;

    fbl::unique_ptr<AmlPdmDevice> pdm_;

    zx::bti bti_;
};
} //namespace astro
} //namespace audio
