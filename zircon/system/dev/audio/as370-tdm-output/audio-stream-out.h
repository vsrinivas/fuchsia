// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <optional>

#include <audio-proto/audio-proto.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device-internal.h>
#include <ddktl/device.h>
#include <ddktl/protocol/codec.h>
#include <ddktl/protocol/platform/device.h>
#include <fbl/mutex.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/pdev.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/sync/completion.h>
#include <zircon/thread_annotations.h>

#include "codec.h"

namespace audio {
namespace as370 {

class As370AudioStreamOut : public SimpleAudioStream {

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
    zx_status_t InitPost() TA_REQ(domain_->token()) override;

private:
    enum {
        kAvpll0Clk,
        kAvpll1Clk,
        kClockCount,
    };

    friend class SimpleAudioStream;
    friend class fbl::RefPtr<As370AudioStreamOut>;

    As370AudioStreamOut(zx_device_t* parent);
    ~As370AudioStreamOut() {}

    zx_status_t InitPdev() TA_REQ(domain_->token());

    ddk::PDev pdev_ TA_GUARDED(domain_->token());
    Codec codec_ TA_GUARDED(domain_->token());
    ddk::ClockProtocolClient clks_[kClockCount] TA_GUARDED(domain_->token());
};

} // namespace as370
} // namespace audio
