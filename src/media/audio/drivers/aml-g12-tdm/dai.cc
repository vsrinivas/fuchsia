// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "dai.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fpromise/result.h>
#include <lib/zx/clock.h>
#include <math.h>
#include <string.h>

#include <numeric>
#include <optional>
#include <utility>

#include <fbl/algorithm.h>

#include "src/media/audio/drivers/aml-g12-tdm/aml_tdm_dai_bind.h"

namespace audio::aml_g12 {

enum {
  FRAGMENT_PDEV,
  FRAGMENT_COUNT,
};

AmlG12TdmDai::AmlG12TdmDai(zx_device_t* parent, ddk::PDev pdev)
    : AmlG12TdmDaiDeviceType(parent),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      pdev_(std::move(pdev)) {
  ddk_proto_id_ = ZX_PROTOCOL_DAI;
  loop_.StartThread("aml-g12-tdm-dai");
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
    case metadata::DaiType::Tdm2:
      dai_format_.frame_format.set_frame_format_standard(
          ::fuchsia::hardware::audio::DaiFrameFormatStandard::TDM2);
      break;
    case metadata::DaiType::Tdm3:
      dai_format_.frame_format.set_frame_format_standard(
          ::fuchsia::hardware::audio::DaiFrameFormatStandard::TDM3);
      break;
  }
}

void AmlG12TdmDai::Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) {
  ::fidl::InterfaceRequest<::fuchsia::hardware::audio::Dai> dai;
  dai.set_channel(request->dai_protocol.TakeChannel());
  dai_binding_.emplace(this, std::move(dai), loop_.dispatcher());
  dai_binding_->set_error_handler([this](zx_status_t status) -> void {
    zxlogf(INFO, "DAI protocol %s", zx_status_get_string(status));
    Stop([]() {});
    delay_info_sent_ = false;
  });
}

zx_status_t AmlG12TdmDai::DaiConnect(zx::channel channel) {
  dai_binding_.emplace(this, std::move(channel), loop_.dispatcher());
  return ZX_OK;
}

void AmlG12TdmDai::Reset(ResetCallback callback) {
  auto status =
      aml_audio_->InitHW(metadata_, dai_format_.channels_to_use_bitmask, dai_format_.frame_rate);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to init tdm hardware %d", status);
  }
  callback();
}

zx_status_t AmlG12TdmDai::InitPDev() {
  size_t actual = 0;
  auto status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &metadata_,
                                    sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(ERROR, "device_get_metadata failed %d", status);
    return status;
  }
  status = AmlTdmConfigDevice::Normalize(metadata_);
  if (status != ZX_OK) {
    return status;
  }
  InitDaiFormats();

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not obtain bti %d", status);
    return status;
  }
  std::optional<fdf::MmioBuffer> mmio;
  status = pdev_.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not get mmio %d", status);
    return status;
  }
  aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, *std::move(mmio));
  if (aml_audio_ == nullptr) {
    zxlogf(ERROR, "failed to create TDM device with config");
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
    zxlogf(ERROR, "GetVmo failed, ring buffer started");
    ringbuffer_binding_->Unbind();
    return;
  }
  frame_size_ = metadata_.ring_buffer.number_of_channels * metadata_.ring_buffer.bytes_per_sample;
  size_t ring_buffer_size = fbl::round_up<size_t, size_t>(
      min_frames * frame_size_, std::lcm(frame_size_, aml_audio_->GetBufferAlignment()));
  size_t out_frames = ring_buffer_size / frame_size_;
  if (out_frames > std::numeric_limits<uint32_t>::max()) {
    zxlogf(ERROR, "out frames too big %zu", out_frames);
    ringbuffer_binding_->Unbind();
    return;
  }
  auto status = InitBuffer(ring_buffer_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to init buffer %d", status);
    ringbuffer_binding_->Unbind();
    return;
  }

  constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
  zx::vmo buffer;
  status = ring_buffer_vmo_.duplicate(rights, &buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "GetVmo failed, could not duplicate VMO");
    ringbuffer_binding_->Unbind();
    return;
  }

  status = aml_audio_->SetBuffer(pinned_ring_buffer_.region(0).phys_addr, ring_buffer_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to set buffer %d", status);
    ringbuffer_binding_->Unbind();
    return;
  }

  expected_notifications_per_ring_.store(clock_recovery_notifications_per_ring);
  rb_fetched_ = true;
  // This is safe because of the overflow check we made above.
  auto out_num_rb_frames = static_cast<uint32_t>(out_frames);
  callback(fpromise::ok(std::make_tuple(out_num_rb_frames, std::move(buffer))));
}

void AmlG12TdmDai::Start(StartCallback callback) {
  uint64_t start_time = 0;
  if (rb_started_ || !rb_fetched_) {
    zxlogf(ERROR, "Could not start");
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
    zxlogf(ERROR, "Could not stop");
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
  __asm__ volatile("dsb sy" : : : "memory");
#endif
  auto status = bti_.release_quarantine();
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not release quarantine bti - %d", status);
    return status;
  }
  pinned_ring_buffer_.Unpin();
  status = zx_vmo_create_contiguous(bti_.get(), size, 0, ring_buffer_vmo_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to allocate ring buffer vmo - %d", status);
    return status;
  }

  status = pinned_ring_buffer_.Pin(ring_buffer_vmo_, bti_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to pin ring buffer vmo - %d", status);
    return status;
  }
  if (pinned_ring_buffer_.region_count() != 1) {
    if (!AllowNonContiguousRingBuffer()) {
      zxlogf(ERROR, "buffer is not contiguous");
      return ZX_ERR_NO_MEMORY;
    }
  }
  return ZX_OK;
}

void AmlG12TdmDai::GetProperties(::fuchsia::hardware::audio::Dai::GetPropertiesCallback callback) {
  ::fuchsia::hardware::audio::DaiProperties props;
  props.set_is_input(metadata_.is_input);
  props.set_manufacturer(metadata_.manufacturer);
  props.set_product_name(metadata_.product_name);
  callback(std::move(props));
}

void AmlG12TdmDai::GetRingBufferFormats(GetRingBufferFormatsCallback callback) {
  ::fuchsia::hardware::audio::Dai_GetRingBufferFormats_Result result;
  ::fuchsia::hardware::audio::Dai_GetRingBufferFormats_Response response;
  ::fuchsia::hardware::audio::PcmSupportedFormats pcm_formats;
  ::fuchsia::hardware::audio::ChannelSet channel_set;
  std::vector<::fuchsia::hardware::audio::ChannelAttributes> attributes(
      metadata_.ring_buffer.number_of_channels);
  channel_set.set_attributes(std::move(attributes));
  pcm_formats.mutable_channel_sets()->push_back(std::move(channel_set));
  pcm_formats.mutable_sample_formats()->push_back(
      ::fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  pcm_formats.mutable_bytes_per_sample()->push_back(metadata_.ring_buffer.bytes_per_sample);
  pcm_formats.mutable_valid_bits_per_sample()->push_back(metadata_.ring_buffer.bytes_per_sample *
                                                         8);
  for (size_t i = 0; i < std::size(AmlTdmConfigDevice::kSupportedFrameRates); ++i) {
    pcm_formats.mutable_frame_rates()->push_back(AmlTdmConfigDevice::kSupportedFrameRates[i]);
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
  for (size_t i = 0; i < std::size(AmlTdmConfigDevice::kSupportedFrameRates); ++i) {
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
  if (ring_buffer_format.pcm_format().frame_rate == 0) {
    zxlogf(ERROR, "Bad (zero) frame rate");
    ring_buffer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint32_t bytes_per_frame = ring_buffer_format.pcm_format().bytes_per_sample *
                             ring_buffer_format.pcm_format().number_of_channels;
  if (bytes_per_frame == 0) {
    zxlogf(ERROR, "Bad (zero) bytes per frame");
    ring_buffer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Stop and terminate a previous ring buffer.
  if (rb_started_) {
    Stop([]() {});
    ringbuffer_binding_->Unbind();
  }

  ringbuffer_binding_.emplace(this, std::move(ring_buffer), loop_.dispatcher());
  ringbuffer_binding_->set_error_handler([this](zx_status_t status) -> void {
    zxlogf(INFO, "RingBuffer protocol %s", zx_status_get_string(status));
    Stop([]() {});
  });
  dai_format_ = std::move(dai_format);

  uint32_t fifo_depth_frames = (aml_audio_->fifo_depth() + bytes_per_frame - 1) / bytes_per_frame;
  internal_delay_nsec_ = static_cast<uint64_t>(fifo_depth_frames) * 1'000'000'000 /
                         static_cast<uint64_t>(ring_buffer_format.pcm_format().frame_rate);

  Reset([]() {});
}

void AmlG12TdmDai::GetProperties(
    ::fuchsia::hardware::audio::RingBuffer::GetPropertiesCallback callback) {
  ::fuchsia::hardware::audio::RingBufferProperties prop;
  prop.set_external_delay(0);
  prop.set_fifo_depth(aml_audio_->fifo_depth());
  prop.set_needs_cache_flush_or_invalidate(true);
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
    zxlogf(ERROR, "no notifications per ring");
  }
  position_callback_ = std::move(callback);
}

void AmlG12TdmDai::WatchDelayInfo(WatchDelayInfoCallback callback) {
  if (delay_info_sent_) {
    return;  // Only send delay state once, as if it never changed.
  }
  delay_info_sent_ = true;
  fuchsia::hardware::audio::DelayInfo delay_info = {};
  // No external delay information is provided by this driver.
  delay_info.set_internal_delay(internal_delay_nsec_);
  callback(std::move(delay_info));
}

static zx_status_t dai_bind(void* ctx, zx_device_t* device) {
  size_t actual = 0;
  metadata::AmlConfig metadata = {};
  auto status = device_get_metadata(device, DEVICE_METADATA_PRIVATE, &metadata,
                                    sizeof(metadata::AmlConfig), &actual);
  if (status != ZX_OK || sizeof(metadata::AmlConfig) != actual) {
    zxlogf(ERROR, "device_get_metadata failed %d", status);
    return status;
  }
  pdev_protocol_t pdev;
  status = device_get_protocol(device, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not get pdev %d", status);
    return status;
  }
  auto dai = std::make_unique<audio::aml_g12::AmlG12TdmDai>(device, ddk::PDev(&pdev));
  if (dai == nullptr) {
    zxlogf(ERROR, "Could not create DAI driver");
    return ZX_ERR_NO_MEMORY;
  }

  status = dai->InitPDev();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not init device");
    return status;
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMLOGIC_DAI_OUT},
  };
  const char* name = "aml-g12-tdm-dai-out";
  if (metadata.is_input) {
    props[1].value = PDEV_DID_AMLOGIC_DAI_IN;
    name = "aml-g12-tdm-dai-in";
  }
  status = dai->DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not add DAI driver to the DDK");
    return status;
  }
  [[maybe_unused]] auto unused = dai.release();
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
ZIRCON_DRIVER(aml_g12_tdm_dai, audio::aml_g12::driver_ops, "aml-g12-tdm-dai", "0.1");
