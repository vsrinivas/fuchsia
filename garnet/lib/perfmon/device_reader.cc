// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/perfmon/device_reader.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace perfmon {
namespace internal {

bool DeviceReader::Create(fxl::WeakPtr<Controller> controller, uint32_t buffer_size_in_pages,
                          std::unique_ptr<Reader>* out_reader) {
  zx::vmar vmar;
  uintptr_t addr;
  // The controller records the buffer size in pages, but internally the
  // size in bytes is what we use.
  size_t buffer_size = buffer_size_in_pages * Controller::kPageSize;
  auto status = zx::vmar::root_self()->allocate2(ZX_VM_CAN_MAP_READ, 0u, buffer_size, &vmar, &addr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to obtain vmar for reading trace data: " << status;
    return false;
  }

  out_reader->reset(new DeviceReader(std::move(controller), buffer_size, std::move(vmar)));
  return true;
}

DeviceReader::DeviceReader(fxl::WeakPtr<Controller> controller, uint32_t buffer_size, zx::vmar vmar)
    : Reader(zx_system_get_num_cpus()),
      controller_(std::move(controller)),
      buffer_size_(buffer_size),
      vmar_(std::move(vmar)) {
  FX_DCHECK(controller_);
}

DeviceReader::~DeviceReader() { UnmapBuffer(); }

bool DeviceReader::MapBuffer(const std::string& name, uint32_t trace_num) {
  if (!controller_) {
    FX_LOGS(ERROR) << name << ": unable to map buffer, controller is gone";
    return false;
  }

  if (!UnmapBuffer()) {
    return false;
  }

  zx::vmo vmo;
  if (!controller_->GetBufferHandle(name, trace_num, &vmo)) {
    return false;
  }

  uintptr_t addr;
  zx_status_t status = vmar_.map(ZX_VM_PERM_READ, 0, vmo, 0, buffer_size_, &addr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << name << ": Unable to map buffer vmo: " << status;
    return false;
  }
  buffer_contents_ = reinterpret_cast<const void*>(addr);

  ReaderStatus rstatus =
      BufferReader::Create(name, buffer_contents_, buffer_size_, &buffer_reader_);
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
      FX_LOGS(ERROR) << "Unable to unmap buffer vmo: " << status;
      return false;
    }
    buffer_contents_ = nullptr;
  }
  return true;
}

}  // namespace internal
}  // namespace perfmon
