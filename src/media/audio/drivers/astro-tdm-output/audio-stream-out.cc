// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "audio-stream-out.h"

#include <lib/zx/clock.h>
#include <math.h>

#include <optional>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>

namespace audio {
namespace astro {

enum {
  COMPONENT_PDEV,
  COMPONENT_I2C,
  COMPONENT_FAULT_GPIO,
  COMPONENT_ENABLE_GPIO,
  COMPONENT_COUNT,
};

// Calculate ring buffer size for 1 second of 16-bit, 48kHz, stereo.
constexpr size_t RB_SIZE = fbl::round_up<size_t, size_t>(48000 * 2 * 2u, PAGE_SIZE);

AstroAudioStreamOut::AstroAudioStreamOut(zx_device_t* parent) : SimpleAudioStream(parent, false) {}

zx_status_t AstroAudioStreamOut::InitPDev() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get composite protocol\n");
    return status;
  }

  zx_device_t* components[COMPONENT_COUNT] = {};
  size_t actual;
  composite_get_components(&composite, components, countof(components), &actual);
  if (actual < countof(components)) {
    zxlogf(ERROR, "could not get components\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_ = components[COMPONENT_PDEV];
  if (!pdev_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  audio_fault_ = components[COMPONENT_FAULT_GPIO];
  audio_en_ = components[COMPONENT_ENABLE_GPIO];

  if (!audio_fault_.is_valid() || !audio_en_.is_valid()) {
    zxlogf(ERROR, "%s failed to allocate gpio\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::I2cChannel i2c = components[COMPONENT_I2C];
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "%s failed to allocate i2c\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }
  codec_ = Tas27xx::Create(std::move(i2c));
  if (!codec_) {
    zxlogf(ERROR, "%s could not get tas27xx\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti - %d\n", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev_.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    return status;
  }
  aml_audio_ = AmlTdmDevice::Create(*std::move(mmio), HIFI_PLL, TDM_OUT_B, FRDDR_B, MCLK_B);
  if (aml_audio_ == nullptr) {
    zxlogf(ERROR, "%s failed to create tdm device\n", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  // Enable Codec
  audio_en_.Write(1);

  codec_->Init();

  // Initialize the ring buffer
  InitBuffer(RB_SIZE);

  aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr,
                        pinned_ring_buffer_.region(0).size);

  // Setup TDM.

  // 3 bitoffset, 4 slots, 32 bits/slot, 16 bits/sample, no mixing.
  aml_audio_->ConfigTdmOutSlot(3, 3, 31, 15, 0);

  // Lane0 right channel.
  aml_audio_->ConfigTdmOutSwaps(0x00000010);

  // Lane 0, unmask first 2 slots (0x00000003),
  aml_audio_->ConfigTdmOutLane(0, 0x00000003);

  // Setup appropriate tdm clock signals. mclk = 1536MHz/125 = 12.288MHz.
  aml_audio_->SetMclkDiv(124);

  // No need to set mclk pad via SetMClkPad (TAS2770 features "MCLK Free Operation").

  // sclk = 12.288MHz/2 = 6.144MHz, 1 every 128 sclks is frame sync.
  aml_audio_->SetSclkDiv(1, 0, 127);

  aml_audio_->Sync();

  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::Init() {
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
  cur_gain_state_.cur_gain = codec_->GetGain();
  cur_gain_state_.cur_mute = false;
  cur_gain_state_.cur_agc = false;

  cur_gain_state_.min_gain = codec_->GetMinGain();
  cur_gain_state_.max_gain = codec_->GetMaxGain();
  cur_gain_state_.gain_step = codec_->GetGainStep();
  cur_gain_state_.can_mute = false;
  cur_gain_state_.can_agc = false;

  snprintf(device_name_, sizeof(device_name_), "astro-audio-out");
  snprintf(mfr_name_, sizeof(mfr_name_), "Spacely Sprockets");
  snprintf(prod_name_, sizeof(prod_name_), "astro");

  unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

  return ZX_OK;
}

// Timer handler for sending out position notifications
void AstroAudioStreamOut::ProcessRingNotification() {
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
  resp.ring_buffer_pos = aml_audio_->GetRingPosition();
  NotifyPosition(resp);
}

zx_status_t AstroAudioStreamOut::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = aml_audio_->fifo_depth();
  external_delay_nsec_ = 0;

  // At this time only one format is supported, and hardware is initialized
  //  during driver binding, so nothing to do at this time.
  return ZX_OK;
}

void AstroAudioStreamOut::ShutdownHook() {
  aml_audio_->Shutdown();
  audio_en_.Write(0);
}

zx_status_t AstroAudioStreamOut::SetGain(const audio_proto::SetGainReq& req) {
  zx_status_t status = codec_->SetGain(req.gain);
  if (status != ZX_OK) {
    return status;
  }
  cur_gain_state_.cur_gain = codec_->GetGain();
  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
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

  aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, rb_frames * frame_size_);

  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::Start(uint64_t* out_start_time) {
  *out_start_time = aml_audio_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    us_per_notification_ = static_cast<uint32_t>(1000 * pinned_ring_buffer_.region(0).size /
                                                 (frame_size_ * 48 * notifs));
    notify_timer_.PostDelayed(dispatcher(), zx::usec(us_per_notification_));
  } else {
    us_per_notification_ = 0;
  }
  codec_->Mute(false);
  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::Stop() {
  codec_->Mute(true);
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  aml_audio_->Stop();
  return ZX_OK;
}

zx_status_t AstroAudioStreamOut::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Add the range for basic audio support.
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

zx_status_t AstroAudioStreamOut::InitBuffer(size_t size) {
  zx_status_t status;
  status = zx_vmo_create_contiguous(bti_.get(), size, 0, ring_buffer_vmo_.reset_and_get_address());
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

static zx_status_t audio_bind(void* ctx, zx_device_t* device) {
  auto stream = audio::SimpleAudioStream::Create<audio::astro::AstroAudioStreamOut>(device);
  if (stream == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  __UNUSED auto dummy = fbl::ExportToRawPtr(&stream);

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = audio_bind;
  return ops;
}();

}  // namespace astro
}  // namespace audio

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_tdm, audio::astro::driver_ops, "aml-tdm-out", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_TDM),
ZIRCON_DRIVER_END(aml_tdm)
    // clang-format on
