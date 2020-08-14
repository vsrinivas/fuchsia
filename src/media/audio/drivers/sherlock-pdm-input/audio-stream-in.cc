// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream-in.h"

#include <lib/zx/clock.h>
#include <math.h>

#include <optional>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>

namespace audio {
namespace sherlock {

constexpr size_t kMaxNumberOfChannels = 3;
constexpr size_t kMinSampleRate = 48000;
constexpr size_t kMaxSampleRate = 96000;
constexpr size_t kBytesPerSample = 2;
// Calculate ring buffer size for 1 second of 16-bit, 48kHz.
constexpr size_t kRingBufferSize = fbl::round_up<size_t, size_t>(
    kMaxSampleRate * kBytesPerSample * kMaxNumberOfChannels, ZX_PAGE_SIZE);

SherlockAudioStreamIn::SherlockAudioStreamIn(zx_device_t* parent)
    : SimpleAudioStream(parent, true /* is input */) {
  frames_per_second_ = kMinSampleRate;
}

zx_status_t SherlockAudioStreamIn::Create(void* ctx, zx_device_t* parent) {
  auto stream = audio::SimpleAudioStream::Create<SherlockAudioStreamIn>(parent);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t SherlockAudioStreamIn::Init() {
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

  snprintf(device_name_, sizeof(device_name_), "sherlock-audio-in");
  snprintf(mfr_name_, sizeof(mfr_name_), "unknown");
  snprintf(prod_name_, sizeof(prod_name_), "sherlock");

  unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

zx_status_t SherlockAudioStreamIn::InitPDev() {
  size_t actual = 0;
  auto status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &number_of_channels_,
                                    sizeof(number_of_channels_), &actual);
  if (status != ZX_OK || sizeof(number_of_channels_) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }

  pdev_protocol_t pdev;
  status = device_get_protocol(parent(), ZX_PROTOCOL_PDEV, &pdev);
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

  InitBuffer(kRingBufferSize);

  pdm_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, pinned_ring_buffer_.region(0).size);

  InitHw();

  return ZX_OK;
}

void SherlockAudioStreamIn::InitHw() {
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

zx_status_t SherlockAudioStreamIn::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = pdm_->fifo_depth();
  external_delay_nsec_ = 0;

  if (req.channels != number_of_channels_) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (req.frames_per_second != 48000 && req.frames_per_second != 96000) {
    return ZX_ERR_INVALID_ARGS;
  }
  frames_per_second_ = req.frames_per_second;
  channels_to_use_bitmask_ = req.channels_to_use_bitmask;

  InitHw();

  return ZX_OK;
}

zx_status_t SherlockAudioStreamIn::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
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

zx_status_t SherlockAudioStreamIn::Start(uint64_t* out_start_time) {
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

// Timer handler for sending out position notifications.
void SherlockAudioStreamIn::ProcessRingNotification() {
  ScopedToken t(domain_token());
  ZX_ASSERT(us_per_notification_ != 0);

  notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.monotonic_time = zx::clock::get_monotonic().get();
  resp.ring_buffer_pos = pdm_->GetRingPosition();
  NotifyPosition(resp);
}

void SherlockAudioStreamIn::ShutdownHook() {
  Stop();
  pinned_ring_buffer_.Unpin();
}

zx_status_t SherlockAudioStreamIn::Stop() {
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  pdm_->Stop();
  return ZX_OK;
}

zx_status_t SherlockAudioStreamIn::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list");
    return ZX_ERR_NO_MEMORY;
  }

  audio_stream_format_range_t range;
  range.min_channels = number_of_channels_;
  range.max_channels = number_of_channels_;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  range.min_frames_per_second = kMinSampleRate;
  range.max_frames_per_second = kMaxSampleRate;
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  supported_formats_.push_back(range);

  return ZX_OK;
}

zx_status_t SherlockAudioStreamIn::InitBuffer(size_t size) {
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

  // TODO(ZX-3149): Per johngro's suggestion preallocate contiguous memory (say in
  // platform bus) since we are likely to fail after running for a while and we need to
  // init again (say the devhost is restarted).
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

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = SherlockAudioStreamIn::Create;
  return ops;
}();

}  // namespace sherlock
}  // namespace audio

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_pdm, audio::sherlock::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_SHERLOCK_PDM),
ZIRCON_DRIVER_END(aml_pdm)
    // clang-format on
