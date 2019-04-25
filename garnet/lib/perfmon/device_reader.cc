// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/perfmon/device_reader.h"

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>

#include "garnet/lib/perfmon/controller.h"
#include "garnet/lib/perfmon/properties_impl.h"

namespace perfmon {

bool DeviceReader::Create(int fd, uint32_t buffer_size_in_pages,
                          std::unique_ptr<DeviceReader>* out_reader) {
  zx::vmar vmar;
  uintptr_t addr;
  // The controller records the buffer size in pages, but internally the
  // size in bytes is what we use.
  size_t buffer_size = buffer_size_in_pages * Controller::kPageSize;
  auto status = zx::vmar::root_self()->allocate(
      0u, buffer_size, ZX_VM_CAN_MAP_READ, &vmar, &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to obtain vmar for reading trace data: "
                   << status;
    return false;
  }

  out_reader->reset(new DeviceReader(fd, buffer_size, std::move(vmar)));
  return true;
}

DeviceReader::DeviceReader(int fd, uint32_t buffer_size, zx::vmar vmar)
    : Reader(zx_system_get_num_cpus()),
      fd_(fd), buffer_size_(buffer_size), vmar_(std::move(vmar)) {
  FXL_DCHECK(fd_ >= 0);
}

DeviceReader::~DeviceReader() {
  UnmapBuffer();
}

bool DeviceReader::GetProperties(Properties* props) {
  perfmon_ioctl_properties_t properties;
  auto status = ioctl_perfmon_get_properties(fd_, &properties);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to get properties: " << status;
    return false;
  }

  internal::IoctlToPerfmonProperties(properties, props);
  return true;
}

bool DeviceReader::GetConfig(perfmon_ioctl_config_t* config) {
  auto status = ioctl_perfmon_get_config(fd_, config);
  if (status < 0)
    FXL_LOG(ERROR) << "ioctl_perfmon_get_config failed: " << status;
  return status >= 0;
}

bool DeviceReader::MapBuffer(const std::string& name, uint32_t trace_num) {
  if (!UnmapBuffer()) {
    return false;
  }

  ioctl_perfmon_buffer_handle_req_t req;
  req.descriptor = trace_num;
  zx_handle_t raw_vmo;
  auto ioctl_status = ioctl_perfmon_get_buffer_handle(fd_, &req, &raw_vmo);
  if (ioctl_status < 0) {
    FXL_LOG(ERROR) << name << ": ioctl_perfmon_get_buffer_handle failed: "
                   << ioctl_status;
    return false;
  }
  zx::vmo vmo(raw_vmo);

  uintptr_t addr;
  zx_status_t status = vmar_.map(0, vmo, 0, buffer_size_, ZX_VM_PERM_READ,
                                 &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << name << ": Unable to map buffer vmo: " << status;
    return false;
  }
  buffer_contents_ = reinterpret_cast<const void*>(addr);

  ReaderStatus rstatus = BufferReader::Create(name, buffer_contents_,
                                              buffer_size_, &buffer_reader_);
  if (rstatus != ReaderStatus::kOk) {
    return false;
  }

  current_vmo_ = std::move(vmo);
  return true;
}

bool DeviceReader::UnmapBuffer() {
  if (buffer_contents_) {
    current_vmo_.reset();
    buffer_reader_.reset();
    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer_contents_);
    auto status = vmar_.unmap(addr, buffer_size_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Unable to unmap buffer vmo: " << status;
      return false;
    }
    buffer_contents_ = nullptr;
  }
  return true;
}

}  // namespace perfmon
