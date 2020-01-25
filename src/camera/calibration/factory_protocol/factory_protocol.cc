// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/calibration/factory_protocol/factory_protocol.h"

#include <fcntl.h>
#include <lib/fdio/fdio.h>

#include <fbl/unique_fd.h>
#include <src/lib/files/file.h>
#include <src/lib/syslog/cpp/logger.h>

namespace camera {

constexpr auto kTag = "camera_calibration";

static constexpr const char* kIspDevicePath = "/dev/class/isp-device-test/000";
static constexpr const char* kControllerDevicePath =
    "/dev/camera-controller/camera-controller-device";
static constexpr const char* kDirPath = "/calibration";

std::unique_ptr<FactoryProtocol> FactoryProtocol::Create(zx::channel channel,
                                                         async_dispatcher_t* dispatcher) {
  auto factory_impl = std::make_unique<FactoryProtocol>(dispatcher);
  factory_impl->binding_.set_error_handler(
      [&factory_impl](zx_status_t status) { factory_impl->Shutdown(status); });

  zx_status_t status = factory_impl->binding_.Bind(std::move(channel), factory_impl->dispatcher_);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status);
    return nullptr;
  }

  // Connect to the isp-tester device.
  int result = open(kIspDevicePath, O_RDONLY);
  if (result < 0) {
    FX_LOGST(ERROR, kTag) << "Error opening " << kIspDevicePath;
    return nullptr;
  }
  fbl::unique_fd isp_fd_(result);

  zx::channel isp_tester_channel;
  status = fdio_get_service_handle(isp_fd_.get(), isp_tester_channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to get service handle";
    return nullptr;
  }

  factory_impl->isp_tester_.Bind(std::move(isp_tester_channel));

  // Connect to the controller device.
  result = open(kControllerDevicePath, O_RDONLY);
  if (result < 0) {
    FX_LOGST(ERROR, kTag) << "Error opening " << kControllerDevicePath;
    return nullptr;
  }
  fbl::unique_fd controller_fd(result);

  zx::channel controller_channel;
  status = fdio_get_service_handle(controller_fd.get(), controller_channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to get service handle";
    return nullptr;
  }

  // TODO(nzo): Is there any harm to reusing the same dispatcher?
  fuchsia::hardware::camera::DevicePtr device;
  device.Bind(std::move(controller_channel), factory_impl->dispatcher_);

  device->GetChannel2(factory_impl->controller_.NewRequest().TakeChannel());
  if (!factory_impl->controller_) {
    FX_LOGST(ERROR, kTag) << "Failed to get controller interface from device";
    return nullptr;
  }

  return factory_impl;
}

zx_status_t FactoryProtocol::ConnectToStream() {
  fuchsia::sysmem::ImageFormat_2 format;
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;

  // TODO(nzo): Is there any harm to reusing the same dispatcher?
  auto request = stream_.NewRequest(dispatcher_);
  zx_status_t status = isp_tester_->CreateStream(std::move(request), &buffers, &format);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to create stream";
    return status;
  }
  image_io_util_ = ImageIOUtil::Create(&buffers, kDirPath);

  stream_.set_error_handler([&](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Stream disconnected";
    Shutdown(status);
  });
  stream_.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
    OnFrameAvailable(std::move(info));
    frames_received_ = true;
  };
  stream_->Start();
  streaming_ = true;

  return ZX_OK;
};

void FactoryProtocol::Shutdown(zx_status_t status) {
  // Close the connection if it's open.
  if (binding_.is_bound()) {
    binding_.Close(status);
  }

  // Stop streaming if it's started.
  if (streaming_) {
    stream_->Stop();
    streaming_ = false;
  }
}

// |fuchsia::camera2::Stream|

void FactoryProtocol::OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) {
  if (info.frame_status != fuchsia::camera2::FrameStatus::OK) {
    FX_LOGST(ERROR, kTag) << "Received OnFrameAvailable with error event";
    return;
  }

  // Only allow a single write.
  if (write_allowed_) {
    zx_status_t status = image_io_util_->WriteImageData(info.buffer_id);
    if (status != ZX_OK) {
      FX_PLOGST(ERROR, kTag, status) << "Failed to write to disk";
      return;
    }
    write_allowed_ = false;
  }

  stream_->ReleaseFrame(info.buffer_id);
}

// |fuchsia::factory::camera::CameraFactory|

void FactoryProtocol::DetectCamera(DetectCameraCallback callback) {
  fuchsia::camera2::DeviceInfo device_info;
  zx_status_t status = controller_->GetDeviceInfo(&device_info);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to call GetDeviceInfo";
    Shutdown(status);
  }

  auto response = fuchsia::factory::camera::CameraFactory_DetectCamera_Response();
  // TODO(41671): account for multiple cameras on a single device.
  response.camera_id = 0;
  response.camera_info = std::move(device_info);

  auto result = fuchsia::factory::camera::CameraFactory_DetectCamera_Result::WithResponse(
      std::move(response));
  callback(std::move(result));
}

void FactoryProtocol::Start() { FX_NOTIMPLEMENTED(); }

void FactoryProtocol::Stop() { FX_NOTIMPLEMENTED(); }

void FactoryProtocol::SetConfig(uint32_t mode, int32_t integration_time, int32_t analog_gain,
                                int32_t digital_gain, SetConfigCallback callback) {
  FX_NOTIMPLEMENTED();
}

void FactoryProtocol::CaptureImage(CaptureImageCallback callback) { FX_NOTIMPLEMENTED(); }

void FactoryProtocol::WriteCalibrationData(fuchsia::mem::Buffer calibration_data,
                                           std::string file_path,
                                           WriteCalibrationDataCallback callback) {
  FX_NOTIMPLEMENTED();
}

}  // namespace camera
