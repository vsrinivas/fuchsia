// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio-stream-in.h"

#include <lib/zx/clock.h>
#include <math.h>

#include <numeric>
#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>

#include "src/media/audio/drivers/aml-g12-pdm/aml_g12_pdm_bind.h"

namespace audio::aml_g12 {

constexpr size_t kMinSampleRate = 48000;
constexpr size_t kMaxSampleRate = 96000;

AudioStreamIn::AudioStreamIn(zx_device_t* parent) : SimpleAudioStream(parent, true /* is input */) {
  frames_per_second_ = kMinSampleRate;
}

zx_status_t AudioStreamIn::Create(void* ctx, zx_device_t* parent) {
  auto stream = audio::SimpleAudioStream::Create<AudioStreamIn>(parent);
  if (stream == nullptr) {
    zxlogf(ERROR, "%s Could not create aml-g12-pdm driver", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }
  __UNUSED auto dummy = fbl::ExportToRawPtr(&stream);
  return ZX_OK;
}

zx_status_t AudioStreamIn::SetGain(const audio_proto::SetGainReq& req) {
  return req.gain == 0.f ? ZX_OK : ZX_ERR_INVALID_ARGS;
}

zx_status_t AudioStreamIn::Init() {
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

  strncpy(mfr_name_, metadata_.manufacturer, sizeof(mfr_name_));
  strncpy(prod_name_, metadata_.product_name, sizeof(prod_name_));
  unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;
  snprintf(device_name_, sizeof(device_name_), "%s-audio-pdm-in", prod_name_);

  // TODO(mpuryear): change this to the domain of the clock received from the board driver
  clock_domain_ = 0;

  return ZX_OK;
}

zx_status_t AudioStreamIn::InitPDev() {
  size_t actual = 0;
  auto status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &metadata_,
                                    sizeof(metadata::AmlPdmConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlPdmConfig) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }

  pdev_protocol_t pdev;
  status = device_get_protocol(parent(), ZX_PROTOCOL_PDEV, &pdev);
  if (status) {
    zxlogf(ERROR, "%s get pdev protocol failed %d", __FILE__, status);
    return status;
  }

  ddk::PDev pdev2(&pdev);
  if (!pdev2.is_valid()) {
    zxlogf(ERROR, "%s could not get pdev", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  status = pdev2.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti %d", __FUNCTION__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> mmio0, mmio1;
  status = pdev2.MapMmio(0, &mmio0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not map mmio0 %d", __FUNCTION__, status);
    return status;
  }
  status = pdev2.MapMmio(1, &mmio1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not map mmio1 %d", __FUNCTION__, status);
    return status;
  }

  lib_ = AmlPdmDevice::Create(*std::move(mmio0), *std::move(mmio1), HIFI_PLL,
                              metadata_.sysClockDivFactor - 1, metadata_.dClockDivFactor - 1,
                              TODDR_B, metadata_.version);
  if (lib_ == nullptr) {
    zxlogf(ERROR, "%s failed to create audio device", __FUNCTION__);
    return ZX_ERR_NO_MEMORY;
  }

  // Initial setup of one page of buffer, just to be safe.
  status = InitBuffer(PAGE_SIZE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to init buffer %d", __FILE__, status);
    return status;
  }
  status =
      lib_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, pinned_ring_buffer_.region(0).size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to set buffer %d", __FILE__, status);
    return status;
  }

  InitHw();

  return ZX_OK;
}

void AudioStreamIn::InitHw() {
  // Enable first metadata_.number_of_channels channels.
  lib_->ConfigPdmIn(static_cast<uint8_t>((1 << metadata_.number_of_channels) - 1));
  uint8_t mute_slots = 0;
  // Set muted slots from channels_to_use_bitmask limited to channels in use.
  if (channels_to_use_bitmask_ != AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED)
    mute_slots =
        static_cast<uint8_t>(~channels_to_use_bitmask_ & ((1 << metadata_.number_of_channels) - 1));
  lib_->SetMute(mute_slots);
  lib_->SetRate(frames_per_second_);
  lib_->Sync();
}

zx_status_t AudioStreamIn::ChangeFormat(const audio_proto::StreamSetFmtReq& req) {
  fifo_depth_ = lib_->fifo_depth();
  external_delay_nsec_ = 0;

  if (req.channels != metadata_.number_of_channels) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (req.frames_per_second != 48'000 && req.frames_per_second != 96'000) {
    return ZX_ERR_INVALID_ARGS;
  }
  frames_per_second_ = req.frames_per_second;
  channels_to_use_bitmask_ = req.channels_to_use_bitmask;

  InitHw();

  return ZX_OK;
}

zx_status_t AudioStreamIn::GetBuffer(const audio_proto::RingBufGetBufferReq& req,
                                     uint32_t* out_num_rb_frames, zx::vmo* out_buffer) {
  size_t ring_buffer_size = fbl::round_up<size_t, size_t>(
      req.min_ring_buffer_frames * frame_size_, std::lcm(frame_size_, lib_->GetBufferAlignment()));
  size_t out_frames = ring_buffer_size / frame_size_;
  if (out_frames > std::numeric_limits<uint32_t>::max()) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t vmo_size = fbl::round_up<size_t, size_t>(ring_buffer_size, PAGE_SIZE);
  auto status = InitBuffer(vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to init buffer %d", __FILE__, status);
    return status;
  }

  constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
  status = ring_buffer_vmo_.duplicate(rights, out_buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to duplicate vmo", __FUNCTION__);
    return status;
  }
  status = lib_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, ring_buffer_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to set buffer %d", __FILE__, status);
    return status;
  }
  // This is safe because of the overflow check we made above.
  *out_num_rb_frames = static_cast<uint32_t>(out_frames);
  return status;
}

void AudioStreamIn::RingBufferShutdown() { lib_->Shutdown(); }

zx_status_t AudioStreamIn::Start(uint64_t* out_start_time) {
  *out_start_time = lib_->Start();

  uint32_t notifs = LoadNotificationsPerRing();
  if (notifs) {
    size_t size = 0;
    ring_buffer_vmo_.get_size(&size);
    notification_rate_ =
        zx::duration(zx_duration_from_usec(1'000 * pinned_ring_buffer_.region(0).size /
                                           (frame_size_ * frames_per_second_ / 1'000 * notifs)));
    notify_timer_.PostDelayed(dispatcher(), notification_rate_);
  } else {
    notification_rate_ = {};
  }
  return ZX_OK;
}

// Timer handler for sending out position notifications.
void AudioStreamIn::ProcessRingNotification() {
  ScopedToken t(domain_token());
  ZX_ASSERT(notification_rate_ != zx::duration());

  notify_timer_.PostDelayed(dispatcher(), notification_rate_);

  audio_proto::RingBufPositionNotify resp = {};
  resp.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

  resp.monotonic_time = zx::clock::get_monotonic().get();
  resp.ring_buffer_pos = lib_->GetRingPosition();
  NotifyPosition(resp);
}

void AudioStreamIn::ShutdownHook() { lib_->Shutdown(); }

zx_status_t AudioStreamIn::Stop() {
  notify_timer_.Cancel();
  notification_rate_ = {};
  lib_->Stop();
  return ZX_OK;
}

zx_status_t AudioStreamIn::AddFormats() {
  fbl::AllocChecker ac;
  supported_formats_.reserve(1, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory, can not create supported formats list");
    return ZX_ERR_NO_MEMORY;
  }

  audio_stream_format_range_t range = {};
  range.min_channels = metadata_.number_of_channels;
  range.max_channels = metadata_.number_of_channels;
  range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  range.min_frames_per_second = kMinSampleRate;
  range.max_frames_per_second = kMaxSampleRate;
  range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;

  supported_formats_.push_back(range);

  return ZX_OK;
}

zx_status_t AudioStreamIn::InitBuffer(size_t size) {
  pinned_ring_buffer_.Unpin();
  auto status =
      zx_vmo_create_contiguous(bti_.get(), size, 0, ring_buffer_vmo_.reset_and_get_address());
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
    if (!AllowNonContiguousRingBuffer()) {
      zxlogf(ERROR, "%s buffer is not contiguous", __func__);
      return ZX_ERR_NO_MEMORY;
    }
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AudioStreamIn::Create;
  return ops;
}();

}  // namespace audio::aml_g12

ZIRCON_DRIVER(aml_g12_pdm, audio::aml_g12::driver_ops, "zircon", "0.1");
