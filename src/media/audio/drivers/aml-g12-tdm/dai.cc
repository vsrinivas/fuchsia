// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "dai.h"

#include <lib/device-protocol/pdev.h>
#include <lib/fit/result.h>
#include <lib/zx/clock.h>
#include <math.h>
#include <string.h>

#include <numeric>
#include <optional>
#include <utility>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>

#include "src/media/audio/drivers/aml-g12-tdm/aml_tdm_dai_bind.h"

namespace audio::aml_g12 {

enum {
  FRAGMENT_PDEV,
  FRAGMENT_COUNT,
};

AmlG12TdmDai::AmlG12TdmDai(zx_device_t* parent)
    : AmlG12TdmDaiDeviceType(parent), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  loop_.StartThread();
}

void AmlG12TdmDai::InitDaiFormats() {
  // Only the PCM signed sample format is supported.
  dai_format_.sample_format = ::fuchsia::hardware::audio::DaiSampleFormat::PCM_SIGNED;
  dai_format_.frame_rate = AmlTdmConfigDevice::kSupportedFrameRates[0];
  dai_format_.bits_per_sample = metadata_.dai.bits_per_sample;
  dai_format_.bits_per_slot = metadata_.dai.bits_per_slot;
  dai_format_.number_of_channels = metadata_.dai.number_of_channels;
  dai_format_.channels_to_use_bitmask = std::numeric_limits<uint64_t>::max();  // Enable all.
  switch (metadata_.dai.type) {
    case metadata::DaiType::I2s:
      dai_format_.frame_format.set_frame_format_standard(
          ::fuchsia::hardware::audio::DaiFrameFormatStandard::I2S);
      break;
    case metadata::DaiType::StereoLeftJustified:
      dai_format_.frame_format.set_frame_format_standard(
          ::fuchsia::hardware::audio::DaiFrameFormatStandard::STEREO_LEFT);
      break;
    case metadata::DaiType::Tdm1:
      dai_format_.frame_format.set_frame_format_standard(
          ::fuchsia::hardware::audio::DaiFrameFormatStandard::TDM1);
      break;
    default:
      ZX_ASSERT(0);  // Not supported.
  }
}

zx_status_t AmlG12TdmDai::DaiConnect(zx::channel channel) {
  dai_binding_.emplace(this, std::move(channel), loop_.dispatcher());
  return ZX_OK;
}

void AmlG12TdmDai::Reset(ResetCallback callback) {
  auto status =
      aml_audio_->InitHW(metadata_, dai_format_.channels_to_use_bitmask, dai_format_.frame_rate);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to init tdm hardware %d\n", __FILE__, status);
  }
  callback();
}

zx_status_t AmlG12TdmDai::InitPDev() {
  size_t actual = 0;
  auto status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &metadata_,
                                    sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }
  status = AmlTdmConfigDevice::Normalize(metadata_);
  if (status != ZX_OK) {
    return status;
  }
  InitDaiFormats();

  pdev_protocol_t pdev;
  status = device_get_protocol(parent(), ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get pdev %d", __FILE__, status);
    return status;
  }
  auto pdev2 = ddk::PDev(&pdev);
  status = pdev2.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not obtain bti %d", __FILE__, status);
    return status;
  }
  std::optional<ddk::MmioBuffer> mmio;
  status = pdev2.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get mmio %d", __FILE__, status);
    return status;
  }
  aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, *std::move(mmio));
  if (aml_audio_ == nullptr) {
    zxlogf(ERROR, "%s failed to create TDM device with config", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  Reset([]() {});

  return ZX_OK;
}

void AmlG12TdmDai::DdkRelease() {
  loop_.Shutdown();
  Shutdown();
  delete this;
}

void AmlG12TdmDai::Shutdown() {
  if (rb_started_) {
    Stop([]() {});
  }
  aml_audio_->Shutdown();
  pinned_ring_buffer_.Unpin();
}

void AmlG12TdmDai::GetVmo(uint32_t min_frames, uint32_t clock_recovery_notifications_per_ring,
                          GetVmoCallback callback) {
  if (rb_started_) {
    zxlogf(ERROR, "%s GetVmo failed, ring buffer started", __FILE__);
    ringbuffer_binding_->Unbind();
    return;
  }
  frame_size_ = metadata_.ring_buffer.number_of_channels * metadata_.ring_buffer.bytes_per_sample;
  size_t ring_buffer_size = fbl::round_up<size_t, size_t>(
      min_frames * frame_size_, std::lcm(frame_size_, aml_audio_->GetBufferAlignment()));
  size_t out_frames = ring_buffer_size / frame_size_;
  if (out_frames > std::numeric_limits<uint32_t>::max()) {
    zxlogf(ERROR, "%s out frames too big %zu", __FILE__, out_frames);
    ringbuffer_binding_->Unbind();
    return;
  }
  auto status = InitBuffer(ring_buffer_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to init buffer %d", __FILE__, status);
    ringbuffer_binding_->Unbind();
    return;
  }

  constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
  zx::vmo buffer;
  status = ring_buffer_vmo_.duplicate(rights, &buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s GetVmo failed, could not duplicate VMO", __FILE__);
    ringbuffer_binding_->Unbind();
    return;
  }

  status = aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, ring_buffer_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to set buffer %d", __FILE__, status);
    ringbuffer_binding_->Unbind();
    return;
  }

  expected_notifications_per_ring_.store(clock_recovery_notifications_per_ring);
  rb_fetched_ = true;
  // This is safe because of the overflow check we made above.
  auto out_num_rb_frames = static_cast<uint32_t>(out_frames);
  callback(fit::ok(std::make_tuple(out_num_rb_frames, std::move(buffer))));
}

void AmlG12TdmDai::Start(StartCallback callback) {
  uint64_t start_time = 0;
  if (rb_started_ || !rb_fetched_) {
    zxlogf(ERROR, "Could not start %s\n", __FILE__);
    callback(start_time);
    return;
  }

  start_time = aml_audio_->Start();
  rb_started_ = true;

  uint32_t notifs = expected_notifications_per_ring_.load();
  if (notifs) {
    us_per_notification_ =
        static_cast<uint32_t>(1000 * pinned_ring_buffer_.region(0).size /
                              (frame_size_ * dai_format_.frame_rate / 1000 * notifs));
    notify_timer_.PostDelayed(loop_.dispatcher(), zx::usec(us_per_notification_));
  } else {
    us_per_notification_ = 0;
  }

  callback(start_time);
}

void AmlG12TdmDai::Stop(StopCallback callback) {
  if (!rb_started_) {
    zxlogf(ERROR, "Could not stop %s\n", __FILE__);
    callback();
    return;
  }
  notify_timer_.Cancel();
  us_per_notification_ = 0;
  aml_audio_->Stop();
  rb_started_ = false;
  callback();
}

zx_status_t AmlG12TdmDai::InitBuffer(size_t size) {
  // Make sure the DMA is stopped before releasing quarantine.
  aml_audio_->Stop();
  // Make sure that all reads/writes have gone through.
#if defined(__aarch64__)
  asm __volatile__("dsb sy");
#endif
  auto status = bti_.release_quarantine();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not release quarantine bti - %d", __FILE__, status);
    return status;
  }
  pinned_ring_buffer_.Unpin();
  status = zx_vmo_create_contiguous(bti_.get(), size, 0, ring_buffer_vmo_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to allocate ring buffer vmo - %d", __FILE__, status);
    return status;
  }

  status = pinned_ring_buffer_.Pin(ring_buffer_vmo_, bti_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to pin ring buffer vmo - %d", __FILE__, status);
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

void AmlG12TdmDai::GetInfo(GetInfoCallback callback) {
  ::fuchsia::hardware::audio::DaiInfo info;
  info.manufacturer = metadata_.manufacturer;
  info.product_name = metadata_.product_name;
  callback(std::move(info));
}

void AmlG12TdmDai::GetRingBufferFormats(GetRingBufferFormatsCallback callback) {
  ::fuchsia::hardware::audio::Dai_GetRingBufferFormats_Result result;
  ::fuchsia::hardware::audio::Dai_GetRingBufferFormats_Response response;
  ::fuchsia::hardware::audio::PcmSupportedFormats pcm_formats;
  pcm_formats.number_of_channels.push_back(metadata_.ring_buffer.number_of_channels);
  pcm_formats.sample_formats.push_back(::fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  pcm_formats.bytes_per_sample.push_back(metadata_.ring_buffer.bytes_per_sample);
  pcm_formats.valid_bits_per_sample.push_back(metadata_.ring_buffer.bytes_per_sample * 8);
  for (size_t i = 0; i < countof(AmlTdmConfigDevice::kSupportedFrameRates); ++i) {
    pcm_formats.frame_rates.push_back(AmlTdmConfigDevice::kSupportedFrameRates[i]);
  }
  ::fuchsia::hardware::audio::SupportedFormats formats;
  formats.set_pcm_supported_formats(std::move(pcm_formats));
  response.ring_buffer_formats.push_back(std::move(formats));
  result.set_response(std::move(response));
  callback(std::move(result));
}

void AmlG12TdmDai::GetDaiFormats(GetDaiFormatsCallback callback) {
  ::fuchsia::hardware::audio::Dai_GetDaiFormats_Result result;
  ::fuchsia::hardware::audio::Dai_GetDaiFormats_Response response;
  ::fuchsia::hardware::audio::DaiSupportedFormats formats;
  formats.number_of_channels.push_back(metadata_.dai.number_of_channels);
  formats.sample_formats.push_back(::fuchsia::hardware::audio::DaiSampleFormat::PCM_SIGNED);
  ::fuchsia::hardware::audio::DaiFrameFormat frame_format;
  switch (metadata_.dai.type) {
    case metadata::DaiType::I2s:
      frame_format.set_frame_format_standard(
          ::fuchsia::hardware::audio::DaiFrameFormatStandard::I2S);
      break;
    case metadata::DaiType::StereoLeftJustified:
      frame_format.set_frame_format_standard(
          ::fuchsia::hardware::audio::DaiFrameFormatStandard::STEREO_LEFT);
      break;
    case metadata::DaiType::Tdm1:
      frame_format.set_frame_format_standard(
          ::fuchsia::hardware::audio::DaiFrameFormatStandard::TDM1);
      break;
    default:
      ZX_ASSERT(0);  // Not supported.
  }
  formats.frame_formats.push_back(std::move(frame_format));
  for (size_t i = 0; i < countof(AmlTdmConfigDevice::kSupportedFrameRates); ++i) {
    formats.frame_rates.push_back(AmlTdmConfigDevice::kSupportedFrameRates[i]);
  }
  formats.bits_per_slot.push_back(metadata_.dai.bits_per_slot);
  formats.bits_per_sample.push_back(metadata_.dai.bits_per_sample);
  response.dai_formats.push_back(std::move(formats));
  result.set_response(std::move(response));
  callback(std::move(result));
}

void AmlG12TdmDai::CreateRingBuffer(
    ::fuchsia::hardware::audio::DaiFormat dai_format,
    ::fuchsia::hardware::audio::Format ring_buffer_format,
    ::fidl::InterfaceRequest<::fuchsia::hardware::audio::RingBuffer> ring_buffer) {
  // Stop and terminate a previous ring buffer.
  if (rb_started_) {
    Stop([]() {});
    ringbuffer_binding_->Unbind();
  }

  ringbuffer_binding_.emplace(this, std::move(ring_buffer), loop_.dispatcher());
  dai_format_ = std::move(dai_format);
  Reset([]() {});
}

void AmlG12TdmDai::GetProperties(GetPropertiesCallback callback) {
  ::fuchsia::hardware::audio::RingBufferProperties prop;
  prop.set_external_delay(0);
  prop.set_fifo_depth(aml_audio_->fifo_depth());
  prop.set_needs_cache_flush_or_invalidate(false);
  callback(std::move(prop));
}

void AmlG12TdmDai::ProcessRingNotification() {
  if (us_per_notification_) {
    notify_timer_.PostDelayed(loop_.dispatcher(), zx::usec(us_per_notification_));
  } else {
    notify_timer_.Cancel();
    return;
  }
  ::fuchsia::hardware::audio::RingBufferPositionInfo info;
  info.position = aml_audio_->GetRingPosition();
  info.timestamp = zx::clock::get_monotonic().get();
  if (position_callback_) {
    (*position_callback_)(std::move(info));
    position_callback_.reset();
  }
}

void AmlG12TdmDai::WatchClockRecoveryPositionInfo(WatchClockRecoveryPositionInfoCallback callback) {
  if (!expected_notifications_per_ring_.load()) {
    zxlogf(ERROR, "%s no notifications per ring", __FILE__);
  }
  position_callback_ = std::move(callback);
}

static zx_status_t dai_bind(void* ctx, zx_device_t* device) {
  size_t actual = 0;
  metadata::AmlConfig metadata = {};
  auto status = device_get_metadata(device, DEVICE_METADATA_PRIVATE, &metadata,
                                    sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }
  auto dai = std::make_unique<audio::aml_g12::AmlG12TdmDai>(device);
  if (dai == nullptr) {
    zxlogf(ERROR, "%s Could not create DAI driver", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  status = dai->InitPDev();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not init device", __FILE__);
    return status;
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMLOGIC_S905D2},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMLOGIC_DAI_OUT},
  };
  const char* name = "aml-g12-tdm-dai-out";
  if (metadata.is_input) {
    props[2].value = PDEV_DID_AMLOGIC_DAI_IN;
    name = "aml-g12-tdm-dai-in";
  }
  status = dai->DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not add DAI driver to the DDK", __FILE__);
    return status;
  }
  __UNUSED auto dummy = dai.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = dai_bind;
  return ops;
}();

}  // namespace audio::aml_g12

// clang-format off
ZIRCON_DRIVER(aml_g12_tdm_dai, audio::aml_g12::driver_ops, "aml-g12-tdm-dai", "0.1")
