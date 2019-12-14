// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtual_camera2_control.h"

#include <lib/async/default.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto TAG = "virtual_camera";

static constexpr uint32_t kBufferCountForCamping = 5;
static constexpr uint32_t kFakeImageCodedWidth = 640;
static constexpr uint32_t kFakeImageMinWidth = 640;
static constexpr uint32_t kFakeImageMaxWidth = 2048;
static constexpr uint32_t kFakeImageCodedHeight = 480;
static constexpr uint32_t kFakeImageMinHeight = 480;
static constexpr uint32_t kFakeImageMaxHeight = 1280;
static constexpr uint32_t kFakeImageMinBytesPerRow = 480;
static constexpr uint32_t kFakeImageMaxBytesPerRow = 0xfffffff;
static constexpr uint32_t kFakeImageBytesPerRowDivisor = 128;
static constexpr uint32_t kFakeImageFps = 30;
static constexpr uint32_t kNumberOfLayers = 1;

static constexpr uint64_t kNanosecondsPerSecond = 1e9;

void VirtualCamera2ControllerImpl::OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo frame) {
  // TODO(36747): don't send multiple error frames unless the client calls
  // AcknowledgeErrorFrame().
  stream_->OnFrameAvailable(std::move(frame));
}

fuchsia::sysmem::BufferCollectionConstraints GetFakeConstraints() {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = kBufferCountForCamping;
  // TODO(36757): Add tests for allocating contiguous memory, with the following:
  // constraints.has_buffer_memory_constraints = true;
  // constraints.buffer_memory_constraints.physically_contiguous_required = true;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  image_constraints.min_coded_width = kFakeImageMinWidth;
  image_constraints.max_coded_width = kFakeImageMaxWidth;
  image_constraints.min_coded_height = kFakeImageMinHeight;
  image_constraints.max_coded_height = kFakeImageMaxHeight;
  image_constraints.min_bytes_per_row = kFakeImageMinBytesPerRow;
  image_constraints.max_bytes_per_row = kFakeImageMaxBytesPerRow;
  image_constraints.layers = kNumberOfLayers;
  image_constraints.bytes_per_row_divisor = kFakeImageBytesPerRowDivisor;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC601_PAL;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  return constraints;
}

std::vector<fuchsia::sysmem::ImageFormat_2> GetImageFormats() {
  fuchsia::sysmem::ImageFormat_2 ret;
  ret.coded_width = kFakeImageCodedWidth;
  ret.coded_height = kFakeImageCodedHeight;
  ret.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  std::vector<fuchsia::sysmem::ImageFormat_2> ret_vec;
  ret_vec.push_back(ret);
  return ret_vec;
}

fuchsia::camera2::StreamProperties GetStreamProperties(fuchsia::camera2::CameraStreamType type) {
  fuchsia::camera2::StreamProperties ret{};
  ret.set_stream_type(type);
  return ret;
}

VirtualCamera2ControllerImpl::VirtualCamera2ControllerImpl(
    fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control,
    async_dispatcher_t* dispatcher, fit::closure on_connection_closed)
    : binding_(this) {
  // Make up some configs:
  fuchsia::camera2::hal::Config config1;
  fuchsia::camera2::hal::StreamConfig sconfig = {
      .frame_rate =
          {
              .frames_per_sec_numerator = kFakeImageFps,
              .frames_per_sec_denominator = 1,
          },
      .constraints = GetFakeConstraints(),
      .properties = GetStreamProperties(fuchsia::camera2::CameraStreamType::MACHINE_LEARNING),
      .image_formats = GetImageFormats(),
  };
  config1.stream_configs.push_back(std::move(sconfig));
  configs_.push_back(std::move(config1));

  binding_.set_error_handler([on_connection_closed = std::move(on_connection_closed)](
                                 zx_status_t status) { on_connection_closed(); });
  binding_.Bind(std::move(control), dispatcher);
}

void VirtualCamera2ControllerImpl::PostNextCaptureTask() {
  // Set the next frame time to be start + frame_count / frames per second.
  int64_t next_frame_time = frame_to_timestamp_.Apply(frame_count_++);
  FX_DCHECK(next_frame_time > 0) << "TimelineFunction gave negative result!";
  FX_DCHECK(next_frame_time != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";
  task_.PostForTime(async_get_default_dispatcher(), zx::time(next_frame_time));
  FX_VLOGS(4) << "VirtualCameraSource: setting next frame to: " << next_frame_time << "   "
              << next_frame_time - static_cast<int64_t>(zx_clock_get_monotonic())
              << " nsec from now";
}

// Checks which buffer can be written to,
// writes it, then signals it ready.
// Then sleeps until next cycle.
void VirtualCamera2ControllerImpl::ProduceFrame() {
  fuchsia::camera2::FrameAvailableInfo event = {};
  // For realism, give the frame a timestamp that is kFramesOfDelay frames
  // in the past:
  event.metadata.set_timestamp(frame_to_timestamp_.Apply(frame_count_ - kFramesOfDelay));
  FX_DCHECK(event.metadata.timestamp()) << "TimelineFunction gave negative result!";
  FX_DCHECK(event.metadata.timestamp() != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";

  // As per the camera driver spec, we always send an OnFrameAvailable message,
  // even if there is an error.
  auto buffer = buffers_.LockBufferForWrite();
  if (!buffer) {
    FX_LOGST(ERROR, TAG) << "no available frames, dropping frame #" << frame_count_;
    event.frame_status = fuchsia::camera2::FrameStatus::ERROR_BUFFER_FULL;
  } else {  // Got a buffer.  Fill it with color:
    color_source_.FillARGB(buffer->virtual_address(), buffer->size());
    event.buffer_id = buffer->ReleaseWriteLockAndGetIndex();
  }

  OnFrameAvailable(std::move(event));
  // Schedule next frame:
  PostNextCaptureTask();
}

void VirtualCamera2ControllerImpl::GetConfigs(GetConfigsCallback callback) {
  callback(fidl::Clone(configs_), ZX_OK);
}

void VirtualCamera2ControllerImpl::GetDeviceInfo(GetDeviceInfoCallback callback) {
  fuchsia::camera2::DeviceInfo camera_device_info;
  camera_device_info.set_vendor_name(kVirtualCameraVendorName);
  camera_device_info.set_product_name(kVirtualCameraProductName);
  camera_device_info.set_type(fuchsia::camera2::DeviceType::VIRTUAL);
  callback(std::move(camera_device_info));
}

void VirtualCamera2ControllerImpl::CreateStream(
    uint32_t config_index, uint32_t stream_type, uint32_t /*image_format_index*/,
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) {
  auto& stream_config = configs_[config_index].stream_configs[stream_type];
  rate_ = stream_config.frame_rate;

  // Pull all the vmos out of the structs that BufferCollection_2 stores them in:
  std::array<zx::vmo, buffer_collection.buffers.size()> vmos;
  for (uint32_t i = 0; i < buffer_collection.buffer_count; ++i) {
    vmos[i] = std::move(buffer_collection.buffers[i].vmo);
  }

  // If we fail here we return, which drops the stream request, closing the channel.
  zx_status_t status = buffers_.Init(vmos.data(), buffer_collection.buffer_count);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Init buffers failed!";
    return;
  }
  status = buffers_.MapVmos();
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Map buffers failed!";
    return;
  }

  stream_ = std::make_unique<VirtualCamera2StreamImpl>(*this, std::move(stream));
}

void VirtualCamera2ControllerImpl::VirtualCamera2StreamImpl::OnFrameAvailable(
    fuchsia::camera2::FrameAvailableInfo frame) {
  binding_.events().OnFrameAvailable(std::move(frame));
}

void VirtualCamera2ControllerImpl::VirtualCamera2StreamImpl::Start() {
  // Set a timeline function to convert from framecount to monotonic time.
  // The start time is now, the start frame number is 0, and the
  // conversion function from frame to time is:
  // frames_per_sec_denominator * 1e9 * num_frames) / frames_per_sec_numerator
  owner_.frame_to_timestamp_ = media::TimelineFunction(
      zx_clock_get_monotonic(), 0, owner_.rate_.frames_per_sec_denominator * kNanosecondsPerSecond,
      owner_.rate_.frames_per_sec_numerator);

  owner_.frame_count_ = 0;

  // Set the first time at which we will generate a frame:
  owner_.PostNextCaptureTask();
}

void VirtualCamera2ControllerImpl::VirtualCamera2StreamImpl::Stop() { owner_.task_.Cancel(); }

void VirtualCamera2ControllerImpl::VirtualCamera2StreamImpl::ReleaseFrame(uint32_t buffer_index) {
  owner_.buffers_.ReleaseBuffer(buffer_index);
}

VirtualCamera2ControllerImpl::VirtualCamera2StreamImpl::VirtualCamera2StreamImpl(
    VirtualCamera2ControllerImpl& owner, fidl::InterfaceRequest<fuchsia::camera2::Stream> stream)
    : owner_(owner), binding_(this, std::move(stream)) {
  binding_.set_error_handler([](zx_status_t status) {
    // Anything to do here?
  });
}

}  // namespace camera
