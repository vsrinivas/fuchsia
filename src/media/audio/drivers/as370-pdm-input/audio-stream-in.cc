// Copyright 2019 The Fuchsia Authors. All rights reserved.
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

namespace {
enum {
  COMPONENT_PDEV,
  COMPONENT_SHARED_DMA,
  COMPONENT_APLL_CLOCK,
  COMPONENT_COUNT,
};
}  // namespace
namespace audio {
namespace as370 {

As370AudioStreamIn::As370AudioStreamIn(zx_device_t* parent)
    : SimpleAudioStream(parent, true /* is input */) {}

zx_status_t As370AudioStreamIn::Create(void* ctx, zx_device_t* parent) {
  auto stream = audio::SimpleAudioStream::Create<As370AudioStreamIn>(parent);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t As370AudioStreamIn::Init() {
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

  snprintf(device_name_, sizeof(device_name_), "as370-audio-in");
  snprintf(mfr_name_, sizeof(mfr_name_), "unknown");
  snprintf(prod_name_, sizeof(prod_name_), "as370");

  unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

zx_status_t As370AudioStreamIn::InitPDev() {
  composite_protocol_t composite = {};
  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not get composite protocol\n", __FILE__);
    return status;
  }

  zx_device_t* components[COMPONENT_COUNT] = {};
  size_t actual = 0;
  composite_get_components(&composite, components, countof(components), &actual);
  if (actual != COMPONENT_COUNT) {
    zxlogf(ERROR, "%s could not get components\n", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_ = components[COMPONENT_PDEV];
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "%s could not get pdev\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }
  clks_[kAvpll0Clk] = components[COMPONENT_APLL_CLOCK];
  if (!clks_[kAvpll0Clk].is_valid()) {
    zxlogf(ERROR, "%s could not get clk\n", __FILE__);
    return status;
  }
  // PLL0 = 196.608MHz = e.g. 48K (FSYNC) * 64 (BCLK) * 8 (MCLK) * 8.
  clks_[kAvpll0Clk].SetRate(kMaxRate * 64 * 8 * 8);
  clks_[kAvpll0Clk].Enable();

  ddk::SharedDmaProtocolClient dma;
  dma = components[COMPONENT_SHARED_DMA];
  if (!dma.is_valid()) {
    zxlogf(ERROR, "%s could not get DMA\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> mmio_global, mmio_avio_global, mmio_i2s;
  status = pdev_.MapMmio(0, &mmio_global);
  if (status != ZX_OK) {
    return status;
  }
  status = pdev_.MapMmio(1, &mmio_avio_global);
  if (status != ZX_OK) {
    return status;
  }
  status = pdev_.MapMmio(2, &mmio_i2s);
  if (status != ZX_OK) {
    return status;
  }

  lib_ = SynAudioInDevice::Create(*std::move(mmio_global), *std::move(mmio_avio_global),
                                  *std::move(mmio_i2s), dma);
  if (lib_ == nullptr) {
    zxlogf(ERROR, "%s failed to create Syn audio device\n", __FUNCTION__);
    return ZX_ERR_NO_MEMORY;
  }

  // Calculate ring buffer size for 1 second of 16-bit at kMaxRate.
  const size_t kRingBufferSize = fbl::round_up<size_t, size_t>(
      kMaxRate * sizeof(uint16_t) * SynAudioInDevice::kNumberOfChannels, ZX_PAGE_SIZE);
  status = lib_->GetBuffer(kRingBufferSize, &ring_buffer_vmo_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to Init buffer %d\n", __FILE__, status);
    return status;
  }

  size_t size;
  status = ring_buffer_vmo_.get_size(&size);
  zxlogf(INFO, "audio: as370 audio input initialized %lX\n", size);
  return ZX_OK;
}

zx_status_t As370AudioStreamIn::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = lib_->fifo_depth();
  external_delay_nsec_ = 0;

  // At this time only one format is supported, and hardware is initialized
  //  during driver binding, so nothing to do at this time.
  return ZX_OK;
}

zx_status_t As370AudioStreamIn::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                          uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
  auto status = ring_buffer_vmo_.duplicate(rights, out_buffer);
  if (status != ZX_OK) {
    return status;
  }

  size_t size;
  status = ring_buffer_vmo_.get_size(&size);
  *out_num_rb_frames = static_cast<uint32_t>(size) / frame_size_;
  return status;
}

void As370AudioStreamIn::RingBufferShutdown() { lib_->Shutdown(); }

zx_status_t As370AudioStreamIn::Start(uint64_t* out_start_time) {
  *out_start_time = lib_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    size_t size = 0;
    ring_buffer_vmo_.get_size(&size);
    notification_rate_ =
        zx::duration(zx_duration_from_msec(size / (frame_size_ * kMaxRate / 1000 * notifs)));
    notify_timer_.PostDelayed(dispatcher(), notification_rate_);
  } else {
    notification_rate_ = {};
  }
  return ZX_OK;
}

// Timer handler for sending out position notifications.
void As370AudioStreamIn::ProcessRingNotification() {
  ScopedToken t(domain_token());
  ZX_ASSERT(notification_rate_ != zx::duration());

  notify_timer_.PostDelayed(dispatcher(), notification_rate_);

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.ring_buffer_pos = lib_->GetRingPosition();
  NotifyPosition(resp);
}

void As370AudioStreamIn::ShutdownHook() { lib_->Shutdown(); }

zx_status_t As370AudioStreamIn::Stop() {
  notify_timer_.Cancel();
  notification_rate_ = {};
  lib_->Stop();
  return ZX_OK;
}

zx_status_t As370AudioStreamIn::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list\n");
    return ZX_ERR_NO_MEMORY;
  }

  audio_stream_format_range_t range;
  range.min_channels = SynAudioInDevice::kNumberOfChannels;
  range.max_channels = SynAudioInDevice::kNumberOfChannels;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  range.min_frames_per_second = kMaxRate;
  range.max_frames_per_second = kMaxRate;
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  supported_formats_.push_back(range);

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = As370AudioStreamIn::Create;
  return ops;
}();

}  // namespace as370
}  // namespace audio

// clang-format off
ZIRCON_DRIVER_BEGIN(as370_audio_in, audio::as370::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_SYNAPTICS_AS370),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AS370_AUDIO_IN),
ZIRCON_DRIVER_END(as370_audio_in)
    // clang-format on
