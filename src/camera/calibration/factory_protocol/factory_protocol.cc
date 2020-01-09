// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/calibration/factory_protocol/factory_protocol.h"

#include <fcntl.h>
#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/fdio/fdio.h>

#include "src/lib/files/file.h"
#include "src/lib/syslog/cpp/logger.h"

namespace camera {

static constexpr const char* kDevicePath = "/dev/class/isp-device-test/000";
static constexpr const char* kDirPath = "/calibration";

std::unique_ptr<FactoryProtocol> FactoryProtocol::Create() {
  auto factory_impl = std::make_unique<FactoryProtocol>();

  int result = open(kDevicePath, O_RDONLY);
  if (result < 0) {
    FX_LOGS(ERROR) << "Error opening " << kDevicePath;
    return nullptr;
  }
  factory_impl->isp_fd_.reset(result);

  zx_status_t status = factory_impl->loop_.RunUntilIdle();
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failure to start loop.";
    return nullptr;
  }

  return factory_impl;
}

zx_status_t FactoryProtocol::ConnectToStream() {
  // Get a channel to the tester device.
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(isp_fd_.get(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get service handle";
    return status;
  }

  // Bind the tester interface and create a stream.
  fuchsia::camera::test::IspTesterSyncPtr tester;
  tester.Bind(std::move(channel));
  fuchsia::sysmem::ImageFormat_2 format;
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  auto request = stream_.NewRequest(loop_.dispatcher());
  status = tester->CreateStream(std::move(request), &buffers, &format);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create stream";
    return status;
  }
  image_io_util_ = ImageIOUtil::Create(&buffers, kDirPath);

  stream_.set_error_handler([this](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Stream disconnected";
    loop_.Quit();
  });
  stream_.events().OnFrameAvailable = fit::bind_member(this, &FactoryProtocol::OnFrameAvailable);
  stream_->Start();
  streaming_ = true;

  return ZX_OK;
};

void FactoryProtocol::StopStream() {
  if (streaming_) {
    stream_->Stop();
    streaming_ = false;
  }
}

void FactoryProtocol::OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) {
  if (info.frame_status != fuchsia::camera2::FrameStatus::OK) {
    FX_LOGS(ERROR) << "Received OnFrameAvailable with error event";
    return;
  }
  zx_status_t status = image_io_util_->WriteImageData(info.buffer_id);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to write to disk";
    return;
  }
  stream_->ReleaseFrame(info.buffer_id);
}

void FactoryProtocol::DetectCamera(DetectCameraCallback callback) {
  FX_NOTIMPLEMENTED();
}

void FactoryProtocol::Start() {
  FX_NOTIMPLEMENTED();
}

void FactoryProtocol::Stop() {
  FX_NOTIMPLEMENTED();
}

void FactoryProtocol::SetConfig(uint32_t mode, uint32_t exposure, int32_t analog_gain,
                                int32_t digital_gain, SetConfigCallback callback) {
  FX_NOTIMPLEMENTED();
}

void FactoryProtocol::CaptureImage(CaptureImageCallback callback) {
  FX_NOTIMPLEMENTED();
}

void FactoryProtocol::WriteCalibrationData(fuchsia::mem::Buffer calibration_data,
                                           std::string file_path,
                                           WriteCalibrationDataCallback callback) {
  FX_NOTIMPLEMENTED();
}

}  // namespace camera
