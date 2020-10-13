// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream-in.h"

#include <lib/zx/clock.h>
#include <math.h>

#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>

#include "src/media/audio/drivers/astro-pdm-input/aml_pdm-bind.h"

namespace audio {
namespace astro {

// Expects 2 mics.
constexpr size_t kMinNumberOfChannels = 2;
constexpr size_t kMaxNumberOfChannels = 2;
constexpr size_t kMinSampleRate = 48000;
constexpr size_t kMaxSampleRate = 96000;
constexpr size_t kBytesPerSample = 2;
// Calculate ring buffer size for 1 second of 16-bit, 48kHz.
constexpr size_t kRingBufferSize = fbl::round_up<size_t, size_t>(
    kMaxSampleRate * kBytesPerSample * kMaxNumberOfChannels, ZX_PAGE_SIZE);

AstroAudioStreamIn::AstroAudioStreamIn(zx_device_t* parent)
    : SimpleAudioStream(parent, true /* is input */) {
  frames_per_second_ = kMinSampleRate;
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

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

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
    zxlogf(ERROR, "%s could not obtain bti - %d", __func__, status);
    return status;
  }
  std::optional<ddk::MmioBuffer> mmio0, mmio1;
  status = pdev_->MapMmio(0, &mmio0);
  if (status != ZX_OK) {
    return status;
  }
  status = pdev_->MapMmio(1, &mmio1);
  if (status != ZX_OK) {
    return status;
  }
  // HIFI_PLL should be configurd to provide 768MHz to audio clock tree
  // sysclk tarket is 192MHz, achieved by a divider value of 4 (write 3 to register)
  // dclk target is 3.072MHz, achieved by a divider value of 250 (write 249 to register)
  pdm_ = AmlPdmDevice::Create(*std::move(mmio0), *std::move(mmio1), HIFI_PLL, 3, 249, TODDR_B);
  if (pdm_ == nullptr) {
    zxlogf(ERROR, "%s failed to create pdm device", __func__);
    return ZX_ERR_NO_MEMORY;
  }
  // Initialize the ring buffer
  InitBuffer(kRingBufferSize);

  pdm_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, pinned_ring_buffer_.region(0).size);

  InitHw();

  return ZX_OK;
}

void AstroAudioStreamIn::InitHw() {
  // First kNumberOfChannels channels.
  pdm_->ConfigPdmIn(static_cast<uint8_t>((1 << number_of_channels_) - 1));
  uint8_t mute_slots = 0;
  // Set muted slots from channels_to_use_bitmask limited to channels in use.
  if (channels_to_use_bitmask_ != AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED)
    mute_slots = static_cast<uint8_t>(~channels_to_use_bitmask_ & ((1 << number_of_channels_) - 1));
  pdm_->SetMute(mute_slots);
  pdm_->SetRate(frames_per_second_);
  pdm_->Sync();
}

zx_status_t AstroAudioStreamIn::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = pdm_->fifo_depth();
  external_delay_nsec_ = 0;

  if (req.channels < kMinNumberOfChannels || req.channels > kMaxNumberOfChannels) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (req.frames_per_second != 48000 && req.frames_per_second != 96000) {
    return ZX_ERR_INVALID_ARGS;
  }
  frames_per_second_ = req.frames_per_second;
  number_of_channels_ = static_cast<uint8_t>(req.channels);
  channels_to_use_bitmask_ = req.channels_to_use_bitmask;

  InitHw();

  return ZX_OK;
}

zx_status_t AstroAudioStreamIn::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                          uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  uint32_t rb_frames = static_cast<uint32_t>(pinned_ring_buffer_.region(0).size) / frame_size_;

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

  pdm_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, rb_frames * frame_size_);
  return ZX_OK;
}

zx_status_t AstroAudioStreamIn::Start(uint64_t* out_start_time) {
  *out_start_time = pdm_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    us_per_notification_ =
        static_cast<uint32_t>(1000 * pinned_ring_buffer_.region(0).size /
                              (frame_size_ * frames_per_second_ / 1000 * notifs));
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  } else {
    us_per_notification_ = 0;
  }
  return ZX_OK;
}

// Timer handler for sending out position notifications
void AstroAudioStreamIn::ProcessRingNotification() {
  ScopedToken t(domain_token());
  if (us_per_notification_) {
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  } else {
    notify_timer_.Cancel();
    return;
  }

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.monotonic_time = zx::clock::get_monotonic().get();
  resp.ring_buffer_pos = pdm_->GetRingPosition();
  NotifyPosition(resp);
}

void AstroAudioStreamIn::ShutdownHook() {
  Stop();
  pinned_ring_buffer_.Unpin();
}

zx_status_t AstroAudioStreamIn::Stop() {
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  pdm_->Stop();
  return ZX_OK;
}

zx_status_t AstroAudioStreamIn::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list");
    return ZX_ERR_NO_MEMORY;
  }
  // Astro only supports stereo, 16-bit, 48k audio in
  audio_stream_format_range_t range;

  range.min_channels = kMinNumberOfChannels;
  range.max_channels = kMaxNumberOfChannels;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  range.min_frames_per_second = kMinSampleRate;
  range.max_frames_per_second = kMaxSampleRate;
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  supported_formats_.push_back(range);

  return ZX_OK;
}

zx_status_t AstroAudioStreamIn::InitBuffer(size_t size) {
  // Make sure the DMA is stopped before releasing quarantine.
  pdm_->Stop();
  // Make sure that all reads/writes have gone through.
#if defined(__aarch64__)
  asm __volatile__("dsb sy");
#endif
  auto status = bti_.release_quarantine();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not release quarantine bti - %d", __func__, status);
    return status;
  }
  status = zx_vmo_create_contiguous(bti_.get(), size, 0, ring_buffer_vmo_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to allocate ring buffer vmo - %d", __func__, status);
    return status;
  }

  status = pinned_ring_buffer_.Pin(ring_buffer_vmo_, bti_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to pin ring buffer vmo - %d", __func__, status);
    return status;
  }
  if (pinned_ring_buffer_.region_count() != 1) {
    zxlogf(ERROR, "%s buffer is not contiguous", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

}  // namespace astro
}  // namespace audio

__BEGIN_CDECLS

zx_status_t pdm_audio_bind(void* ctx, zx_device_t* device) {
  auto stream = audio::SimpleAudioStream::Create<audio::astro::AstroAudioStreamIn>(device);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  __UNUSED auto dummy = fbl::ExportToRawPtr(&stream);

  return ZX_OK;
}

static constexpr zx_driver_ops_t aml_pdm_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = pdm_audio_bind;
  return ops;
}();
__END_CDECLS

ZIRCON_DRIVER(aml_pdm, aml_pdm_driver_ops, "aml-pdm-in", "0.1")
