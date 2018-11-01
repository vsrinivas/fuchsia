// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream-in.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <math.h>

namespace audio {
namespace astro {

// Calculate ring buffer size for 1 second of 16-bit, 48kHz, stereo.
constexpr size_t RB_SIZE = fbl::round_up<size_t, size_t>(48000 * 2 * 2u, ZX_PAGE_SIZE);

AstroAudioStreamIn::AstroAudioStreamIn(zx_device_t* parent)
    : SimpleAudioStream(parent, true /* is input */) {
}

zx_status_t AstroAudioStreamIn::Init() {
    zx_status_t status;

    status = InitPDev();
    if (status != ZX_OK) {
        return status;
    }

    status = AddFormats();
    if (status != ZX_OK) {
        return status;
    }
    // Set our gain capabilities.
    cur_gain_state_.cur_gain = 0;
    cur_gain_state_.cur_mute = false;
    cur_gain_state_.cur_agc = false;
    cur_gain_state_.min_gain = 0;
    cur_gain_state_.max_gain = 0;
    cur_gain_state_.gain_step = 0;
    cur_gain_state_.can_mute = false;
    cur_gain_state_.can_agc = false;

    snprintf(device_name_, sizeof(device_name_), "astro-audio-in");
    snprintf(mfr_name_, sizeof(mfr_name_), "Bike Sheds, Inc.");
    snprintf(prod_name_, sizeof(prod_name_), "astro");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;

    return ZX_OK;
}

zx_status_t AstroAudioStreamIn::InitPDev() {
    pdev_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_PDEV, &pdev);
    if (status) {
        return status;
    }
    pdev_ = ddk::PDev(&pdev);

    status = pdev_->GetBti(0, &bti_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not obtain bti - %d\n", __func__, status);
        return status;
    }
    fbl::optional<ddk::MmioBuffer> mmio0, mmio1;
    status = pdev_->MapMmio(0, &mmio0);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_->MapMmio(1, &mmio1);
    if (status != ZX_OK) {
        return status;
    }

    pdm_ = AmlPdmDevice::Create(fbl::move(*mmio0),
                                fbl::move(*mmio1),
                                HIFI_PLL, 7, 499, TODDR_B);
    if (pdm_ == nullptr) {
        zxlogf(ERROR, "%s failed to create pdm device\n", __func__);
        return ZX_ERR_NO_MEMORY;
    }
    //Initialize the ring buffer
    InitBuffer(RB_SIZE);

    pdm_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr,
                    pinned_ring_buffer_.region(0).size);

    pdm_->Sync();

    return ZX_OK;
}

zx_status_t AstroAudioStreamIn::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
    fifo_depth_ = pdm_->fifo_depth();
    external_delay_nsec_ = 0;

    // At this time only one format is supported, and hardware is initialized
    //  during driver binding, so nothing to do at this time.
    return ZX_OK;
}

zx_status_t AstroAudioStreamIn::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                          uint32_t* out_num_rb_frames,
                                          zx::vmo* out_buffer) {
    uint32_t rb_frames =
        static_cast<uint32_t>(pinned_ring_buffer_.region(0).size) / frame_size_;

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

    pdm_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr,
                    rb_frames * frame_size_);
    return ZX_OK;
}

zx_status_t AstroAudioStreamIn::Start(uint64_t* out_start_time) {
    *out_start_time = pdm_->Start();

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

//Timer handler for sending out position notifications
zx_status_t AstroAudioStreamIn::ProcessRingNotification() {

    if (us_per_notification_) {
        notify_timer_->Arm(zx_deadline_after(ZX_USEC(us_per_notification_)));
    } else {
        notify_timer_->Cancel();
        return ZX_OK;
    }

    audio_proto::RingBufPositionNotify resp = {};
    resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

    resp.ring_buffer_pos = pdm_->GetRingPosition();
    return NotifyPosition(resp);
}

zx_status_t AstroAudioStreamIn::InitPost() {

    notify_timer_ = dispatcher::Timer::Create();
    if (notify_timer_ == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    dispatcher::Timer::ProcessHandler thandler(
        [pdm = this](dispatcher::Timer * timer)->zx_status_t {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(t, pdm->domain_);
            return pdm->ProcessRingNotification();
        });

    return notify_timer_->Activate(domain_, fbl::move(thandler));
}

zx_status_t AstroAudioStreamIn::Stop() {
    notify_timer_->Cancel();
    us_per_notification_ = 0;
    pdm_->Stop();
    return ZX_OK;
}

zx_status_t AstroAudioStreamIn::AddFormats() {
    fbl::AllocChecker ac;
    supported_formats_.reserve(1, &ac);
    if (!ac.check()) {
        zxlogf(ERROR, "Out of memory, can not create supported formats list\n");
        return ZX_ERR_NO_MEMORY;
    }
    // Astro only supports stereo, 16-bit, 48k audio in
    audio_stream_format_range_t range;

    range.min_channels = 2;
    range.max_channels = 2;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_frames_per_second = 48000;
    range.max_frames_per_second = 48000;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

    supported_formats_.push_back(range);

    return ZX_OK;
}

zx_status_t AstroAudioStreamIn::InitBuffer(size_t size) {
    zx_status_t status;
    status = zx_vmo_create_contiguous(bti_.get(), size, 0,
                                      ring_buffer_vmo_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to allocate ring buffer vmo - %d\n", __func__, status);
        return status;
    }

    status = pinned_ring_buffer_.Pin(ring_buffer_vmo_, bti_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to pin ring buffer vmo - %d\n", __func__, status);
        return status;
    }
    if (pinned_ring_buffer_.region_count() != 1) {
        zxlogf(ERROR, "%s buffer is not contiguous", __func__);
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

} //namespace astro
} //namespace audio

__BEGIN_CDECLS

zx_status_t pdm_audio_bind(void* ctx, zx_device_t* device) {

    auto stream =
        audio::SimpleAudioStream::Create<audio::astro::AstroAudioStreamIn>(device);
    if (stream == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    __UNUSED auto dummy = stream.leak_ref();

    return ZX_OK;
}

static zx_driver_ops_t aml_pdm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = pdm_audio_bind,
    .create = nullptr,
    .release = nullptr,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_pdm, aml_pdm_driver_ops, "aml-pdm-in", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ASTRO_PDM),
ZIRCON_DRIVER_END(aml_pdm)
// clang-format on
__END_CDECLS
