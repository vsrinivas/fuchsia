// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio-stream-out.h"

#include <optional>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/metadata/audio.h>
#include <ddk/protocol/composite.h>
#include <soc/mt8167/mt8167-clk-regs.h>

#include "tas5782.h"
#include "tas5805.h"

namespace audio {
namespace mt8167 {

enum {
    COMPONENT_PDEV,
    COMPONENT_I2C,
    COMPONENT_RESET_GPIO, // This is optional
    COMPONENT_MUTE_GPIO, // This is optional
    COMPONENT_COUNT,
};


// Expects L+R.
constexpr size_t kNumberOfChannels = 2;
// Calculate ring buffer size for 1 second of 16-bit, 48kHz.
constexpr size_t kRingBufferSize = fbl::round_up<size_t, size_t>(48000 * 2 * kNumberOfChannels,
                                                                 PAGE_SIZE);

Mt8167AudioStreamOut::Mt8167AudioStreamOut(zx_device_t* parent)
    : SimpleAudioStream(parent, false), pdev_(parent) {
}

zx_status_t Mt8167AudioStreamOut::InitPdev() {
    composite_protocol_t composite;

    auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Could not get composite protocol\n");
        return status;
    }

    zx_device_t* components[COMPONENT_COUNT] = {};
    size_t actual;
    composite_get_components(&composite, components, countof(components), &actual);
    // Only PDEV and I2C components are required.
    if (actual < 2) {
        zxlogf(ERROR, "could not get components\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    pdev_ = components[COMPONENT_PDEV];
    if (!pdev_.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    metadata::Codec codec;
    status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &codec, sizeof(metadata::Codec),
                                 &actual);
    if (status != ZX_OK || sizeof(metadata::Codec) != actual) {
        zxlogf(ERROR, "%s device_get_metadata failed %d\n", __FILE__, status);
        return status;
    }

    // TODO(andresoportus): Move GPIO control to codecs?
    // Not all codecs have these GPIOs.
    if (components[COMPONENT_RESET_GPIO]) {
        codec_reset_ = components[COMPONENT_RESET_GPIO];
    }
    if (components[COMPONENT_MUTE_GPIO]) {
        codec_mute_ = components[COMPONENT_MUTE_GPIO];
    }

    if (codec == metadata::Codec::Tas5782) {
        zxlogf(INFO, "audio: using TAS5782 codec\n");
        codec_ = Tas5782::Create(components[COMPONENT_I2C], 0);
    } else if (codec == metadata::Codec::Tas5805) {
        zxlogf(INFO, "audio: using TAS5805 codec\n");
        codec_ = Tas5805::Create(components[COMPONENT_I2C], 0);
    } else {
        zxlogf(ERROR, "%s could not get codec\n", __FUNCTION__);
        return ZX_ERR_NO_RESOURCES;
    }

    status = pdev_.GetBti(0, &bti_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not obtain bti %d\n", __FUNCTION__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> mmio_audio, mmio_clk, mmio_pll;
    status = pdev_.MapMmio(0, &mmio_audio);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_.MapMmio(1, &mmio_clk);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_.MapMmio(2, &mmio_pll);
    if (status != ZX_OK) {
        return status;
    }

    // I2S2 corresponds to I2S_8CH.
    mt_audio_ = MtAudioOutDevice::Create(*std::move(mmio_audio), MtAudioOutDevice::I2S2);
    if (mt_audio_ == nullptr) {
        zxlogf(ERROR, "%s failed to create device\n", __FUNCTION__);
        return ZX_ERR_NO_MEMORY;
    }

    if (codec_reset_.is_valid()) {
        codec_reset_.Write(0); // Reset.
        // Delay to be safe.
        zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
        codec_reset_.Write(1); // Set to "not reset".
        // Delay to be safe.
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    }
    codec_->Init();

    // Initialize the ring buffer
    InitBuffer(kRingBufferSize);

    mt_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr,
                         pinned_ring_buffer_.region(0).size);

    // Configure XO and PLLs for interface aud1.

    // Power up playback for I2S2 by clearing the power down bit for div1.
    CLK_SEL_9::Get().ReadFrom(&*mmio_clk).set_apll12_div1_pdn(0).WriteTo(&*mmio_clk);

    // Enable aud1 PLL.
    APLL1_CON0::Get().ReadFrom(&*mmio_pll).set_APLL1_EN(1).WriteTo(&*mmio_pll);

    return ZX_OK;
}

zx_status_t Mt8167AudioStreamOut::Init() {
    zx_status_t status;

    status = InitPdev();
    if (status != ZX_OK) {
        return status;
    }

    status = AddFormats();
    if (status != ZX_OK) {
        return status;
    }

    // Set our gain capabilities.
    cur_gain_state_.cur_gain = codec_->GetGain();
    cur_gain_state_.cur_mute = false;
    cur_gain_state_.cur_agc = false;

    cur_gain_state_.min_gain = codec_->GetMinGain();
    cur_gain_state_.max_gain = codec_->GetMaxGain();
    cur_gain_state_.gain_step = codec_->GetGainStep();
    cur_gain_state_.can_mute = false;
    cur_gain_state_.can_agc = false;

    snprintf(device_name_, sizeof(device_name_), "mt8167-audio-out");
    snprintf(mfr_name_, sizeof(mfr_name_), "unknown");
    snprintf(prod_name_, sizeof(prod_name_), "mt8167");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

    return ZX_OK;
}

zx_status_t Mt8167AudioStreamOut::InitPost() {

    notify_timer_ = dispatcher::Timer::Create();
    if (notify_timer_ == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    dispatcher::Timer::ProcessHandler thandler(
        [thiz = this](dispatcher::Timer * timer)->zx_status_t {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(t, thiz->domain_);
            return thiz->ProcessRingNotification();
        });

    return notify_timer_->Activate(domain_, std::move(thandler));
}

// Timer handler for sending out position notifications.
zx_status_t Mt8167AudioStreamOut::ProcessRingNotification() {

    ZX_ASSERT(us_per_notification_ != 0);

    notify_timer_->Arm(zx_deadline_after(ZX_USEC(us_per_notification_)));

    audio_proto::RingBufPositionNotify resp = {};
    resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

    resp.ring_buffer_pos = mt_audio_->GetRingPosition();
    return NotifyPosition(resp);
}

zx_status_t Mt8167AudioStreamOut::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
    fifo_depth_ = mt_audio_->fifo_depth();
    external_delay_nsec_ = 0;

    // At this time only one format is supported, and hardware is initialized
    // during driver binding, so nothing to do at this time.
    return ZX_OK;
}

void Mt8167AudioStreamOut::ShutdownHook() {
    if (codec_mute_.is_valid()) {
        codec_mute_.Write(0); // Set to "mute".
    }
    if (codec_reset_.is_valid()) {
        codec_reset_.Write(0); // Keep the codec in reset.
    }
    mt_audio_->Shutdown();
}

zx_status_t Mt8167AudioStreamOut::SetGain(const audio_proto::SetGainReq& req) {
    zx_status_t status = codec_->SetGain(req.gain);
    if (status != ZX_OK) {
        return status;
    }
    cur_gain_state_.cur_gain = codec_->GetGain();
    return ZX_OK;
}

zx_status_t Mt8167AudioStreamOut::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                            uint32_t* out_num_rb_frames,
                                            zx::vmo* out_buffer) {
    uint32_t rb_frames =
        static_cast<uint32_t>(pinned_ring_buffer_.region(0).size / frame_size_);

    if (req.min_ring_buffer_frames > rb_frames) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    zx_status_t status;
    constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
    status = ring_buffer_vmo_.duplicate(rights, out_buffer);
    if (status != ZX_OK) {
        return status;
    }

    *out_num_rb_frames = rb_frames;

    mt_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr,
                         rb_frames * frame_size_);

    return ZX_OK;
}

zx_status_t Mt8167AudioStreamOut::Start(uint64_t* out_start_time) {

    *out_start_time = mt_audio_->Start();
    uint32_t notifs = LoadNotificationsPerRing();
    if (notifs) {
        us_per_notification_ = static_cast<uint32_t>(
            1000 * pinned_ring_buffer_.region(0).size / (frame_size_ * 48 * notifs));
        notify_timer_->Arm(zx_deadline_after(ZX_USEC(us_per_notification_)));
    } else {
        us_per_notification_ = 0;
    }
    return ZX_OK;
}

zx_status_t Mt8167AudioStreamOut::Stop() {
    notify_timer_->Cancel();
    us_per_notification_ = 0;
    mt_audio_->Stop();
    return ZX_OK;
}

zx_status_t Mt8167AudioStreamOut::AddFormats() {
    fbl::AllocChecker ac;
    supported_formats_.reserve(1, &ac);
    if (!ac.check()) {
        zxlogf(ERROR, "Out of memory, can not create supported formats list\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Add the range for basic audio support.
    audio_stream_format_range_t range;

    range.min_channels = kNumberOfChannels;
    range.max_channels = kNumberOfChannels;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_frames_per_second = 48000;
    range.max_frames_per_second = 48000;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

    supported_formats_.push_back(range);

    return ZX_OK;
}

zx_status_t Mt8167AudioStreamOut::InitBuffer(size_t size) {
    zx_status_t status;
    status = zx_vmo_create_contiguous(bti_.get(), size, 0,
                                      ring_buffer_vmo_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to allocate ring buffer vmo - %d\n", __FUNCTION__, status);
        return status;
    }

    status = pinned_ring_buffer_.Pin(ring_buffer_vmo_, bti_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to pin ring buffer vmo - %d\n", __FUNCTION__, status);
        return status;
    }
    if (pinned_ring_buffer_.region_count() != 1) {
        zxlogf(ERROR, "%s buffer is not contiguous", __FUNCTION__);
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

} // mt8167
} // audio

__BEGIN_CDECLS

zx_status_t mt_audio_out_bind(void* ctx, zx_device_t* device) {

    auto stream =
        audio::SimpleAudioStream::Create<audio::mt8167::Mt8167AudioStreamOut>(device);
    if (stream == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

static zx_driver_ops_t mt_audio_out_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = mt_audio_out_bind,
    .create = nullptr,
    .release = nullptr,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(mt8167_audio_out, mt_audio_out_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MEDIATEK_8167S_REF),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_AUDIO_OUT),
ZIRCON_DRIVER_END(mt8167_audio_out)
// clang-format on

__END_CDECLS
