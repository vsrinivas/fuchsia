// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/examples/camera/camera_client/camera_client.h"

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/fzl/fdio.h>

namespace camera {

using namespace fuchsia::camera;

Client::Client() : Client(sys::ComponentContext::Create()) {}

Client::Client(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {}

ControlSyncPtr &Client::camera() { return camera_control_; }

ManagerSyncPtr &Client::manager() { return manager_; }

zx_status_t Client::LoadVideoFormats(
    std::function<zx_status_t(
        uint32_t index, std::vector<fuchsia::camera::VideoFormat> *formats,
        uint32_t *total_format_count)>
        get_formats) {
  uint32_t total_format_count;
  uint32_t format_index = 0;
  do {
    std::vector<VideoFormat> call_formats;

    zx_status_t status =
        get_formats(format_index, &call_formats, &total_format_count);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Couldn't get camera formats (status " << status << ")";
      return status;
    }

    for (auto &&f : call_formats) {
      formats_.push_back(f);
    }
    format_index += call_formats.size();
  } while (formats_.size() < total_format_count);

  printf("Available formats: %d\n", (int)formats_.size());
  for (int i = 0; i < (int)formats_.size(); i++) {
    printf("format[%d] - width: %d, height: %d, stride: %u\n", i,
           formats_[i].format.width, formats_[i].format.height,
           static_cast<uint32_t>(formats_[i].format.planes[0].bytes_per_row));
  }

  return ZX_OK;
}

static void dump_device_info(const DeviceInfo &device_info) {
  std::cout << "Device Info - camera_id: " << device_info.camera_id
            << ", vendor_id: " << device_info.vendor_id
            << ", vendor_name: " << device_info.vendor_name << "\n"
            << "  product_id: " << device_info.product_id
            << ", product_name: " << device_info.product_name
            << ", serial_number: " << device_info.serial_number << "\n"
            << "  max_stream_count: " << device_info.max_stream_count
            << ", output_capabilities: " << device_info.output_capabilities
            << "\n";
}

zx_status_t Client::StartManager(int device_id) {
  // Connect to Camera Manager:
  context_->svc()->Connect(manager().NewRequest());

  std::vector<DeviceInfo> devices;
  zx_status_t status = manager()->GetDevices(&devices);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get devices. error: " << status;
    return status;
  }

  std::cout << "Obtained " << devices.size() << " devices\n";
  for (size_t i = 0; i < devices.size(); i++) {
    dump_device_info(devices[i]);
  }

  return LoadVideoFormats(
      [device_id, &devices, this](uint32_t format_index,
                       std::vector<fuchsia::camera::VideoFormat> *formats,
                       uint32_t *total_format_count) {
        return manager()->GetFormats(devices[device_id].camera_id, format_index,
                                     formats, total_format_count);
      });
}

zx_status_t Client::StartDriver(const char *device) {
  zx_status_t status = Open(device);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't open camera client (status " << status << ")";
    return status;
  }

  DeviceInfo device_info;
  status = camera()->GetDeviceInfo(&device_info);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't get device info (status " << status << ")";
    return status;
  }

  dump_device_info(device_info);

  return LoadVideoFormats(
      [this](uint32_t format_index,
             std::vector<fuchsia::camera::VideoFormat> *formats,
             uint32_t *total_format_count) {
        zx_status_t driver_status;
        zx_status_t status = camera()->GetFormats(
            format_index, formats, total_format_count, &driver_status);
        return driver_status == ZX_OK ? status : driver_status;
      });
}

zx_status_t Client::Open(const char *device) {
  std::string dev_path = device;
  fbl::unique_fd dev_node{::open(dev_path.c_str(), O_RDONLY)};
  if (!dev_node.is_valid()) {
    FXL_LOG(ERROR) << "Client::Open failed to open device node at \""
                   << dev_path << "\". (" << strerror(errno) << " : " << errno
                   << ")";
    return ZX_ERR_IO;
  }
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  FXL_CHECK(status == ZX_OK) << "Failed to create channel. status " << status;

  fzl::FdioCaller dev(std::move(dev_node));
  zx_status_t res = fuchsia_hardware_camera_DeviceGetChannel(
      dev.borrow_channel(), remote.release());
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to obtain channel (res " << res << ")";
    return res;
  }

  camera().Bind(std::move(local));

  return ZX_OK;
}

}  // namespace camera
