// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <audio-proto/audio-proto.h>
#include <ddk/io-buffer.h>
#include <ddk/metadata.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/metadata/audio.h>
#include <lib/device-protocol/pdev.h>
#include <ddktl/protocol/gpio.h>
#include <dispatcher-pool/dispatcher-timer.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <soc/aml-common/aml-tdm-audio.h>
#include <lib/zircon-internal/thread_annotations.h>

#include "tas5720.h"

namespace audio {
namespace sherlock {

class SherlockAudioStreamOut : public SimpleAudioStream {

public:
    SherlockAudioStreamOut(zx_device_t* parent);

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
    friend class fbl::RefPtr<SherlockAudioStreamOut>;

    zx_status_t AddFormats() TA_REQ(domain_->token());
    zx_status_t InitBuffer(size_t size) TA_REQ(domain_->token());
    zx_status_t InitPdev() TA_REQ(domain_->token());
    zx_status_t ProcessRingNotification();

    uint32_t us_per_notification_ = 0;
    fbl::RefPtr<dispatcher::Timer> notify_timer_;
    ddk::PDev pdev_ TA_GUARDED(domain_->token());
    metadata::Codec codecs_types_ TA_GUARDED(domain_->token());
    fbl::Array<fbl::unique_ptr<Codec>> codecs_ TA_GUARDED(domain_->token());
    zx::vmo ring_buffer_vmo_ TA_GUARDED(domain_->token());
    fzl::PinnedVmo pinned_ring_buffer_ TA_GUARDED(domain_->token());
    fbl::unique_ptr<AmlTdmDevice> aml_audio_;
    ddk::GpioProtocolClient audio_en_ TA_GUARDED(domain_->token());
    ddk::GpioProtocolClient audio_fault_ TA_GUARDED(domain_->token());
    zx::bti bti_ TA_GUARDED(domain_->token());
};

} // namespace sherlock
} // namespace audio
