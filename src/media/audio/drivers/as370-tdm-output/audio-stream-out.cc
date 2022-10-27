// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio-stream-out.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/clock.h>

#include <optional>
#include <utility>

#include <ddktl/metadata/audio.h>
#include <fbl/array.h>
#include <soc/as370/as370-audio-regs.h>

#include "src/media/audio/drivers/as370-tdm-output/as370_audio_out_bind.h"

// TODO(andresoportus): Add handling for the other formats supported by this controller.

namespace {

constexpr uint32_t kWantedFrameRate = 48'000;

}  // namespace

namespace audio {
namespace as370 {

// Expects L+R.
constexpr size_t kNumberOfChannels = 2;

As370AudioStreamOut::As370AudioStreamOut(zx_device_t* parent)
    : SimpleAudioStream(parent, false), pdev_(parent) {}

zx_status_t As370AudioStreamOut::InitPdev() {
  pdev_ = ddk::PDev::FromFragment(parent());
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "could not get pdev");
    return ZX_ERR_NO_RESOURCES;
  }
  clks_[kAvpll0Clk] = ddk::ClockProtocolClient(parent(), "clock");
  if (!clks_[kAvpll0Clk].is_valid()) {
    zxlogf(ERROR, "GetClk failed");
    return ZX_ERR_NO_RESOURCES;
  }
  // PLL0 = 196.608MHz = e.g. 48K (FSYNC) * 64 (BCLK) * 8 (MCLK) * 8.
  clks_[kAvpll0Clk].SetRate(kWantedFrameRate * 64 * 8 * 8);
  clks_[kAvpll0Clk].Enable();

  ddk::SharedDmaProtocolClient dma(parent(), "dma");
  if (!dma.is_valid()) {
    zxlogf(ERROR, "could not get DMA");
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> mmio_avio_global, mmio_i2s;
  zx_status_t status = pdev_.MapMmio(0, &mmio_avio_global);
  if (status != ZX_OK) {
    return status;
  }
  status = pdev_.MapMmio(1, &mmio_i2s);
  if (status != ZX_OK) {
    return status;
  }

  lib_ = SynAudioOutDevice::Create(*std::move(mmio_avio_global), *std::move(mmio_i2s), dma);
  if (lib_ == nullptr) {
    zxlogf(ERROR, "failed to create Syn audio device");
    return ZX_ERR_NO_MEMORY;
  }

  // Calculate ring buffer size for 1 second of 16-bit at kMaxRate.
  const size_t kRingBufferSize = fbl::round_up<size_t, size_t>(
      kWantedFrameRate * sizeof(uint16_t) * kNumberOfChannels, zx_system_get_page_size());
  status = InitBuffer(kRingBufferSize);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to Init buffer %d", status);
    return status;
  }

  // TODO(113005): Remove all codec controlling from this driver by converting it into a DAI driver.
  status = codec_.SetProtocol(ddk::CodecProtocolClient(parent(), "codec"));
  if (status != ZX_OK) {
    zxlogf(ERROR, "could set codec protocol %d", status);
    return ZX_ERR_NO_RESOURCES;
  }

  // Reset and initialize codec after we have configured I2S.
  status = codec_.Reset();
  if (status != ZX_OK) {
    return status;
  }

  status = codec_.Start();
  if (status != ZX_OK) {
    return status;
  }
  GainState state = {};
  state.gain = 0.0f;
  state.muted = false;
  codec_.SetGainState(state);

  status = codec_.SetBridgedMode(false);
  if (status != ZX_OK) {
    return status;
  }

  DaiFormat format = {};
  format.number_of_channels = 2;
  format.channels_to_use_bitmask = 3;
  format.sample_format = SampleFormat::PCM_SIGNED;
  format.frame_format = FrameFormat::I2S;
  format.frame_rate = kWantedFrameRate;
  format.bits_per_sample = 32;
  format.bits_per_slot = 32;
  zx::result<CodecFormatInfo> info = codec_.SetDaiFormat(format);
  if (info.is_ok()) {
    zxlogf(INFO, "audio: as370 audio output initialized");
  }
  return status;
}

zx_status_t As370AudioStreamOut::Init() {
  auto status = InitPdev();
  if (status != ZX_OK) {
    return status;
  }

  status = AddFormats();
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not add formats");
    return status;
  }

  // Get our gain capabilities.
  auto state = codec_.GetGainState();
  if (state.is_error()) {
    zxlogf(ERROR, "failed to get gain state");
    return state.error_value();
  }
  cur_gain_state_.cur_gain = state->gain;
  cur_gain_state_.cur_mute = state->muted;
  cur_gain_state_.cur_agc = state->agc_enabled;

  auto format = codec_.GetGainFormat();
  if (format.is_error()) {
    zxlogf(ERROR, "failed to get gain format");
    return format.error_value();
  }

  cur_gain_state_.min_gain = format->min_gain;
  cur_gain_state_.max_gain = format->max_gain;
  cur_gain_state_.gain_step = format->gain_step;
  cur_gain_state_.can_mute = format->can_mute;
  cur_gain_state_.can_agc = format->can_agc;

  snprintf(device_name_, sizeof(device_name_), "as370-audio-out");
  snprintf(mfr_name_, sizeof(mfr_name_), "unknown");
  snprintf(prod_name_, sizeof(prod_name_), "as370");

  unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

// Timer handler for sending out position notifications.
void As370AudioStreamOut::ProcessRingNotification() {
  ScopedToken t(domain_token());
  ZX_ASSERT(us_per_notification_ != 0);

  notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.monotonic_time = zx::clock::get_monotonic().get();
  resp.ring_buffer_pos = lib_->GetRingPosition();
  NotifyPosition(resp);
}

zx_status_t As370AudioStreamOut::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = lib_->fifo_depth();
  external_delay_nsec_ = 0;

  // At this time only one format is supported, and hardware is initialized
  // during driver binding, so nothing to do at this time.
  return ZX_OK;
}

void As370AudioStreamOut::ShutdownHook() { lib_->Shutdown(); }

zx_status_t As370AudioStreamOut::SetGain(const audio_proto::SetGainReq& req) {
  GainState state;
  state.gain = req.gain;
  state.muted = cur_gain_state_.cur_mute;
  state.agc_enabled = cur_gain_state_.cur_agc;
  codec_.SetGainState(std::move(state));
  cur_gain_state_.cur_gain = state.gain;
  return ZX_OK;
}

zx_status_t As370AudioStreamOut::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
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

zx_status_t As370AudioStreamOut::Start(uint64_t* out_start_time) {
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

zx_status_t As370AudioStreamOut::Stop() {
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  lib_->Stop();
  return ZX_OK;
}

zx_status_t As370AudioStreamOut::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Add the range for basic audio support.
  SimpleAudioStream::SupportedFormat format = {};

  format.range.min_channels = kNumberOfChannels;
  format.range.max_channels = kNumberOfChannels;
  format.range.sample_formats = AUDIO_SAMPLE_FORMAT_32BIT;
  assert(kWantedFrameRate == 48000);
  format.range.min_frames_per_second = kWantedFrameRate;
  format.range.max_frames_per_second = kWantedFrameRate;
  format.range.flags = ASF_RANGE_FLAG_FPS_CONTINUOUS;  // No need to specify family when min == max.

  supported_formats_.push_back(format);

  return ZX_OK;
}

zx_status_t As370AudioStreamOut::InitBuffer(size_t size) {
  auto status = lib_->GetBuffer(size, &ring_buffer_vmo_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not get ring buffer");
  }
  return status;
}

}  // namespace as370
}  // namespace audio

static zx_status_t syn_audio_out_bind(void* ctx, zx_device_t* device) {
  auto stream = audio::SimpleAudioStream::Create<audio::as370::As370AudioStreamOut>(device);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t syn_audio_out_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = syn_audio_out_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER(as370_audio_out, syn_audio_out_driver_ops, "zircon", "0.1");

// clang-format on
