// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "isp_stream_provider.h"

#include <fcntl.h>
#include <fuchsia/camera/common/cpp/fidl.h>
#include <fuchsia/camera/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fdio.h>

#include <src/lib/fxl/logging.h>

#include "camera_common_stream_shim.h"

static constexpr const char* kDevicePath = "/dev/class/isp-device-test/000";

// Create a provider using the known IspDeviceTest device.
std::unique_ptr<StreamProvider> IspStreamProvider::Create() {
  auto provider = std::make_unique<IspStreamProvider>();

  int result = open(kDevicePath, O_RDONLY);
  if (result < 0) {
    FXL_LOG(ERROR) << "Error opening " << kDevicePath;
    return nullptr;
  }
  provider->isp_fd_.reset(result);

  return std::move(provider);
}

// Offer a stream as served through the tester interface.
std::unique_ptr<fuchsia::camera2::Stream> IspStreamProvider::ConnectToStream(
    fuchsia::camera2::Stream_EventSender* event_handler, fuchsia::sysmem::ImageFormat_2* format_out,
    fuchsia::sysmem::BufferCollectionInfo_2* buffers_out) {
  if (!format_out || !buffers_out) {
    return nullptr;
  }

  // Get a channel to the tester device.
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(isp_fd_.get(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to get service handle";
    return nullptr;
  }

  // Bind the tester interface and create a stream.
  fuchsia::camera::test::IspTesterSyncPtr tester;
  tester.Bind(std::move(channel));
  fuchsia::camera::common::StreamPtr stream;
  fuchsia::sysmem::BufferCollectionInfo buffers;
  status = tester->CreateStream(stream.NewRequest(), &buffers);
  if (status != ZX_OK) {
    FXL_PLOG(ERROR, status) << "Failed to create stream";
    return nullptr;
  }

  // Populate output parameters with ISP-provided values and known parameters.
  format_out->pixel_format.type = buffers.format.image().pixel_format.type;
  format_out->display_width = buffers.format.image().width;
  format_out->display_height = buffers.format.image().height;
  format_out->bytes_per_row = buffers.format.image().planes[0].bytes_per_row;
  buffers_out->buffer_count = buffers.buffer_count;
  buffers_out->settings.buffer_settings.size_bytes = buffers.vmo_size;
  for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
    buffers_out->buffers[i].vmo = std::move(buffers.vmos[i]);
    buffers_out->buffers[i].vmo_usable_start = 0;
  }

  return std::make_unique<camera::CameraCommonStreamShim>(std::move(stream), event_handler);
}
