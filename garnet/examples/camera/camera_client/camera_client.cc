// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/examples/camera/camera_client/camera_client.h"

#include <fcntl.h>
#include <lib/fxl/files/unique_fd.h>
#include "lib/fxl/strings/string_printf.h"

namespace camera {

using namespace fuchsia::camera;

Client::Client() : Client(component::StartupContext::CreateFromStartupInfo()) {}

Client::Client(std::unique_ptr<component::StartupContext> context)
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
        get_formats(format_index, &formats_, &total_format_count);
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

zx_status_t Client::StartManager() {
  // Connect to Camera Manager:
  context_->ConnectToEnvironmentService(manager().NewRequest());

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
      [&devices, this](uint32_t format_index,
                       std::vector<fuchsia::camera::VideoFormat> *formats,
                       uint32_t *total_format_count) {
        return manager()->GetFormats(devices[0].camera_id, format_index,
                                     formats, total_format_count);
      });
}

zx_status_t Client::StartDriver() {
  zx_status_t status = Open(0);
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

zx_status_t Client::Open(int dev_id) {
  std::string dev_path = fxl::StringPrintf("/dev/class/camera/%03u", dev_id);
  fxl::UniqueFD dev_node(::open(dev_path.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FXL_LOG(ERROR) << "Client::Open failed to open device node at \""
                   << dev_path << "\". (" << strerror(errno) << " : " << errno
                   << ")";
    return ZX_ERR_IO;
  }

  zx::channel channel;
  ssize_t res =
      ioctl_camera_get_channel(dev_node.get(), channel.reset_and_get_address());
  if (res < 0) {
    FXL_LOG(ERROR) << "Failed to obtain channel (res " << res << ")";
    return static_cast<zx_status_t>(res);
  }

  camera().Bind(std::move(channel));

  return ZX_OK;
}

}  // namespace camera
