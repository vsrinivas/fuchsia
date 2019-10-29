// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "isp_stream_provider.h"

#include <fcntl.h>
#include <fuchsia/camera/test/cpp/fidl.h>
#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/fdio/fdio.h>

#include "src/lib/syslog/cpp/logger.h"

static constexpr const char* kDevicePath = "/dev/class/isp-device-test/000";

// Create a provider using the known IspDeviceTest device.
std::unique_ptr<StreamProvider> IspStreamProvider::Create() {
  auto provider = std::make_unique<IspStreamProvider>();

  int result = open(kDevicePath, O_RDONLY);
  if (result < 0) {
    FX_LOGS(ERROR) << "Error opening " << kDevicePath;
    return nullptr;
  }
  provider->isp_fd_.reset(result);

  return std::move(provider);
}

// Offer a stream as served through the tester interface.
std::tuple<zx_status_t, fuchsia::sysmem::ImageFormat_2, fuchsia::sysmem::BufferCollectionInfo_2,
           bool>
IspStreamProvider::ConnectToStream(fidl::InterfaceRequest<fuchsia::camera2::Stream> request) {
  // Get a channel to the tester device.
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(isp_fd_.get(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get service handle";
    return MakeErrorReturn(status);
  }

  // Bind the tester interface and create a stream.
  fuchsia::camera::test::IspTesterSyncPtr tester;
  tester.Bind(std::move(channel));
  fuchsia::sysmem::ImageFormat_2 format;
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  status = tester->CreateStream(std::move(request), &buffers, &format);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create stream";
    return MakeErrorReturn(status);
  }

  // The stream coming directly from the ISP is not oriented properly.
  return {ZX_OK, std::move(format), std::move(buffers), true};
}
