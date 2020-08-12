// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio-stream-in.h"

#include <math.h>

#include <optional>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>

// TODO(andresoportus): Refactor astro, sherlock and nelson into an AMLogic drivers.

namespace {
enum {
  FRAGMENT_PDEV,
  FRAGMENT_APLL_CLOCK,
  FRAGMENT_COUNT,
};
}  // namespace
namespace audio {
namespace nelson {

// Expects 2 mics.
constexpr size_t kNumberOfChannels = 2;
constexpr size_t kMinSampleRate = 48000;
constexpr size_t kMaxSampleRate = 96000;

NelsonAudioStreamIn::NelsonAudioStreamIn(zx_device_t* parent)
    : SimpleAudioStream(parent, true /* is input */) {}

zx_status_t NelsonAudioStreamIn::Create(void* ctx, zx_device_t* parent) {
  auto stream = audio::SimpleAudioStream::Create<NelsonAudioStreamIn>(parent);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t NelsonAudioStreamIn::Init() {
  auto status = InitPDev();
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

  snprintf(device_name_, sizeof(device_name_), "nelson-audio-in");
  snprintf(mfr_name_, sizeof(mfr_name_), "unknown");
  snprintf(prod_name_, sizeof(prod_name_), "nelson");

  unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

zx_status_t NelsonAudioStreamIn::InitPDev() {
  composite_protocol_t composite = {};
  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT] = {};
  size_t actual = 0;
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  if (actual != FRAGMENT_COUNT) {
    zxlogf(ERROR, "%s could not get fragments", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_ = fragments[FRAGMENT_PDEV];
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "%s could not get pdev", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti %d", __FUNCTION__, status);
    return status;
  }

  clks_[kHifiPllClk] = fragments[FRAGMENT_APLL_CLOCK];
  if (!clks_[kHifiPllClk].is_valid()) {
    zxlogf(ERROR, "%s could not get clk", __FILE__);
    return status;
  }

  // HIFI_PLL = 1.536GHz = 125 * 4 * 64 * 48000.
  clks_[kHifiPllClk].SetRate(125 * 4 * 64 * 48'000);
  clks_[kHifiPllClk].Enable();

  std::optional<ddk::MmioBuffer> mmio0, mmio1;
  status = pdev_.MapMmio(0, &mmio0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not map mmio %d", __FUNCTION__, status);
    return status;
  }
  status = pdev_.MapMmio(1, &mmio1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not map mmio %d", __FUNCTION__, status);
    return status;
  }

  lib_ = AmlPdmDevice::Create(*std::move(mmio0), *std::move(mmio1), HIFI_PLL, 7, 499, TODDR_B,
                              metadata::AmlVersion::kS905D3G);
  if (lib_ == nullptr) {
    zxlogf(ERROR, "%s failed to create audio device", __FUNCTION__);
    return ZX_ERR_NO_MEMORY;
  }

  // Calculate ring buffer size for 1 second of 16-bit, 48kHz.
  constexpr size_t kRingBufferSize =
      fbl::round_up<size_t, size_t>(kMaxSampleRate * 2 * kNumberOfChannels, ZX_PAGE_SIZE);
  // Initialize the ring buffer
  InitBuffer(kRingBufferSize);

  lib_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, pinned_ring_buffer_.region(0).size);

  lib_->ConfigPdmIn((1 << kNumberOfChannels) - 1);  // First kNumberOfChannels channels.

  lib_->Sync();

  return ZX_OK;
}

zx_status_t NelsonAudioStreamIn::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = lib_->fifo_depth();
  external_delay_nsec_ = 0;

  lib_->SetRate(req.frames_per_second);
  frames_per_second_ = req.frames_per_second;

  return ZX_OK;
}

zx_status_t NelsonAudioStreamIn::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
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

  lib_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, rb_frames * frame_size_);
  return status;
}

void NelsonAudioStreamIn::RingBufferShutdown() { lib_->Shutdown(); }

zx_status_t NelsonAudioStreamIn::Start(uint64_t* out_start_time) {
  *out_start_time = lib_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    size_t size = 0;
    ring_buffer_vmo_.get_size(&size);
    notification_rate_ = zx::duration(zx_duration_from_msec(
        pinned_ring_buffer_.region(0).size / (frame_size_ * frames_per_second_ / 1000 * notifs)));
    notify_timer_.PostDelayed(dispatcher(), notification_rate_);
  } else {
    notification_rate_ = {};
  }
  return ZX_OK;
}

// Timer handler for sending out position notifications.
void NelsonAudioStreamIn::ProcessRingNotification() {
  ScopedToken t(domain_token());
  ZX_ASSERT(notification_rate_ != zx::duration());

  notify_timer_.PostDelayed(dispatcher(), notification_rate_);

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.ring_buffer_pos = lib_->GetRingPosition();
  NotifyPosition(resp);
}

void NelsonAudioStreamIn::ShutdownHook() { lib_->Shutdown(); }

zx_status_t NelsonAudioStreamIn::Stop() {
  notify_timer_.Cancel();
  notification_rate_ = {};
  lib_->Stop();
  return ZX_OK;
}

zx_status_t NelsonAudioStreamIn::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list");
    return ZX_ERR_NO_MEMORY;
  }

  audio_stream_format_range_t range = {};
  range.min_channels = kNumberOfChannels;
  range.max_channels = kNumberOfChannels;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  range.min_frames_per_second = kMinSampleRate;
  range.max_frames_per_second = kMaxSampleRate;
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  supported_formats_.push_back(range);

  return ZX_OK;
}

zx_status_t NelsonAudioStreamIn::InitBuffer(size_t size) {
  zx_status_t status;
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
  ops.bind = NelsonAudioStreamIn::Create;
  return ops;
}();

}  // namespace nelson
}  // namespace audio

// clang-format off
ZIRCON_DRIVER_BEGIN(nelson_audio_in, audio::nelson::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D3),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_PDM),
ZIRCON_DRIVER_END(nelson_audio_in)
    // clang-format on
