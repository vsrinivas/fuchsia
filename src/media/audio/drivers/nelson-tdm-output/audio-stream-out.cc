// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio-stream-out.h"

#include <lib/mmio/mmio.h>
#include <lib/zx/clock.h>

#include <optional>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddktl/metadata/audio.h>
#include <fbl/array.h>

// TODO(andresoportus): Add handling for the other formats supported by this controller.

namespace {

enum {
  FRAGMENT_PDEV,
  FRAGMENT_CODEC,
  FRAGMENT_CLOCK,
  FRAGMENT_COUNT,
};

}  // namespace

namespace audio {
namespace nelson {

// Expects L+R.
constexpr size_t kNumberOfChannels = 2;

NelsonAudioStreamOut::NelsonAudioStreamOut(zx_device_t* parent)
    : SimpleAudioStream(parent, false), pdev_(parent) {}

zx_status_t NelsonAudioStreamOut::InitCodec() {
  auto status = codec_.GetInfo();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get codec info %d", __FUNCTION__, status);
    return status;
  }

  // Reset and initialize codec after we have configured I2S.
  status = codec_.Reset();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not reset codec %d", __FUNCTION__, status);
    return status;
  }

  status = codec_.SetNotBridged();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not set not bridged mode %d", __FUNCTION__, status);
    return status;
  }

  status = codec_.CheckExpectedDaiFormat();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get expected DAI format %d", __FUNCTION__, status);
    return status;
  }

  dai_format_t format = {
      .number_of_channels = 2,
      .channels_to_use_bitmask = 3,
      .sample_format = kWantedSampleFormat,
      .frame_format = kWantedFrameFormat,
      .frame_rate = kWantedFrameRate,
      .bits_per_slot = kWantedBitsPerSlot,
      .bits_per_sample = kWantedBitsPerSample,
  };
  status = codec_.SetDaiFormat(format);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not set DAI format %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t NelsonAudioStreamOut::InitHW() {
  lib_->Shutdown();

  lib_->Initialize();

  // Setup TDM.

  // 3 bitoffset, 2 slots, 32 bits/slot, 16 bits/sample (works for 32 in codec), no mixing.
  lib_->ConfigTdmSlot(3, 1, 31, 15, 0, true);

  // Lane0 right channel.
  lib_->ConfigTdmSwaps(0x00000010);

  // Lane 0, unmask first 2 slots (0x00000003),
  auto status = lib_->ConfigTdmLane(0, 0x00000003, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure TDM out lane %d", __FILE__, status);
    return status;
  }

  // Setup appropriate tdm clock signals. mclk = 1.536GHz/125 = 12.288MHz.
  status = lib_->SetMclkDiv(124);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure MCLK %d", __FILE__, status);
    return status;
  }

  // sclk = 12.288MHz/4 = 3.072MHz, 32 every 64 sclks is frame sync (I2S).
  status = lib_->SetSclkDiv(3, 31, 63, true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not configure SCLK %d", __FILE__, status);
    return status;
  }

  lib_->Sync();

  status = InitCodec();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not initialize codec - %d", __FILE__, status);
    return status;
  }

  zxlogf(INFO, "audio: Nelson audio output initialized");
  return ZX_OK;
}

zx_status_t NelsonAudioStreamOut::InitPdev() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT] = {};
  size_t actual;
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

  codec_.proto_client_ = fragments[FRAGMENT_CODEC];
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "%s Could not get pdev", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti %d", __FUNCTION__, status);
    return status;
  }

  clks_[kHifiPllClk] = fragments[FRAGMENT_CLOCK];
  if (!clks_[kHifiPllClk].is_valid()) {
    zxlogf(ERROR, "%s GetClk failed", __FILE__);
    return status;
  }

  // HIFI_PLL = 1.536GHz = 125 * 4 * 64 * 48000 (kWantedFrameRate)
  clks_[kHifiPllClk].SetRate(125 * 4 * 64 * kWantedFrameRate);
  clks_[kHifiPllClk].Enable();

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev_.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not map mmio %d", __FUNCTION__, status);
    return status;
  }

  lib_ = AmlTdmOutDevice::Create(*std::move(mmio), HIFI_PLL, TDM_OUT_B, FRDDR_B, MCLK_B,
                                 metadata::AmlVersion::kS905D3G);
  if (lib_ == nullptr) {
    zxlogf(ERROR, "%s failed to create audio device", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  // Calculate ring buffer size for 1 second of 16-bit at max rate.
  const size_t kRingBufferSize = fbl::round_up<size_t, size_t>(
      kWantedFrameRate * sizeof(uint16_t) * kNumberOfChannels, ZX_PAGE_SIZE);
  status = InitBuffer(kRingBufferSize);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to Init buffer %d", __FILE__, status);
    return status;
  }

  lib_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, pinned_ring_buffer_.region(0).size);

  codec_.proto_client_ = fragments[FRAGMENT_CODEC];
  if (!codec_.proto_client_.is_valid()) {
    zxlogf(ERROR, "%s Could not get codec", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  return InitHW();
}

zx_status_t NelsonAudioStreamOut::Init() {
  auto status = InitPdev();
  if (status != ZX_OK) {
    return status;
  }

  status = AddFormats();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not add formats %d", __FILE__, status);
    return status;
  }

  // Get our gain capabilities.
  gain_state_t state = {};
  status = codec_.GetGainState(&state);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get gain state %d", __FILE__, status);
    return status;
  }
  cur_gain_state_.cur_gain = state.gain;
  cur_gain_state_.cur_mute = state.muted;
  cur_gain_state_.cur_agc = state.agc_enable;

  gain_format_t format = {};
  status = codec_.GetGainFormat(&format);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get gain format %d", __FILE__, status);
    return status;
  }

  cur_gain_state_.min_gain = format.min_gain;
  cur_gain_state_.max_gain = format.max_gain;
  cur_gain_state_.gain_step = format.gain_step;
  cur_gain_state_.can_mute = format.can_mute;
  cur_gain_state_.can_agc = format.can_agc;

  snprintf(device_name_, sizeof(device_name_), "nelson-audio-out");
  snprintf(mfr_name_, sizeof(mfr_name_), "unknown");
  snprintf(prod_name_, sizeof(prod_name_), "nelson");

  unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

// Timer handler for sending out position notifications.
void NelsonAudioStreamOut::ProcessRingNotification() {
  ScopedToken t(domain_token());
  ZX_ASSERT(us_per_notification_ != 0);

  ZX_ASSERT(notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_)) == ZX_OK);

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.monotonic_time = zx::clock::get_monotonic().get();
  resp.ring_buffer_pos = lib_->GetRingPosition();
  NotifyPosition(resp);
}

zx_status_t NelsonAudioStreamOut::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = lib_->fifo_depth();
  external_delay_nsec_ = 0;

  // At this time only one format is supported, and hardware is initialized
  // during driver binding, so nothing to do at this time.
  return ZX_OK;
}

void NelsonAudioStreamOut::ShutdownHook() { lib_->Shutdown(); }

zx_status_t NelsonAudioStreamOut::SetGain(const audio_proto::SetGainReq& req) {
  gain_state_t state;
  state.gain = req.gain;
  state.muted = cur_gain_state_.cur_mute;
  state.agc_enable = cur_gain_state_.cur_agc;
  auto status = codec_.SetGainState(&state);
  if (status != ZX_OK) {
    return status;
  }
  cur_gain_state_.cur_gain = state.gain;
  return ZX_OK;
}

zx_status_t NelsonAudioStreamOut::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                            uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  size_t size = 0;
  ring_buffer_vmo_.get_size(&size);
  uint32_t rb_frames = static_cast<uint32_t>(size / frame_size_);

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

  return ZX_OK;
}

zx_status_t NelsonAudioStreamOut::Start(uint64_t* out_start_time) {
  *out_start_time = lib_->Start();
  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    size_t size = 0;
    ring_buffer_vmo_.get_size(&size);
    us_per_notification_ = static_cast<uint32_t>(1000 * size / (frame_size_ * 48 * notifs));
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  } else {
    us_per_notification_ = 0;
  }
  return ZX_OK;
}

zx_status_t NelsonAudioStreamOut::Stop() {
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  lib_->Stop();
  return ZX_OK;
}

zx_status_t NelsonAudioStreamOut::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Add the range for basic audio support.
  audio_stream_format_range_t range;

  range.min_channels = kNumberOfChannels;
  range.max_channels = kNumberOfChannels;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  assert(kWantedFrameRate == 48000);
  range.min_frames_per_second = kWantedFrameRate;
  range.max_frames_per_second = kWantedFrameRate;
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  supported_formats_.push_back(range);

  return ZX_OK;
}

zx_status_t NelsonAudioStreamOut::InitBuffer(size_t size) {
  auto status = zx::vmo::create_contiguous(bti_, size, 0, &ring_buffer_vmo_);
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

}  // namespace nelson
}  // namespace audio

static zx_status_t nelson_audio_out_bind(void* ctx, zx_device_t* device) {
  auto stream = audio::SimpleAudioStream::Create<audio::nelson::NelsonAudioStreamOut>(device);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t nelson_audio_out_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = nelson_audio_out_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(nelson_audio_out, nelson_audio_out_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D3),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_TDM),
ZIRCON_DRIVER_END(nelson_audio_out)
    // clang-format on
