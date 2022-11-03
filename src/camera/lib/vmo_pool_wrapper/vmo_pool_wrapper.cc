// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/vmo_pool_wrapper/vmo_pool_wrapper.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>
#include <utility>

namespace camera {

constexpr auto kTag = "VmoPoolWrapper";

zx_status_t VmoPoolWrapper::Init(cpp20::span<zx::unowned_vmo> vmos,
                                 std::optional<std::string> name) {
  if (zx_status_t status = pool_.Init(vmos); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to initialize VmoPool";
    return status;
  }
  min_free_buffers_ = static_cast<uint32_t>(pool_.free_buffers());
  if (name.has_value()) {
    name_ = std::move(name.value());
  }
  return ZX_OK;
}

std::optional<fzl::VmoPool::Buffer> VmoPoolWrapper::LockBufferForWrite() {
  auto result = pool_.LockBufferForWrite();
  if (!result->valid()) {
    FX_LOGST(INFO, kTag) << CreateLogString("Lock buffer for write failed - no buffers available");
    return result;
  }

  // Check whether the free buffer count has reached a new minimum. If it has,
  // log the current state of the pool.
  uint32_t free_buffers = static_cast<uint32_t>(pool_.free_buffers());
  if (free_buffers < min_free_buffers_) {
    min_free_buffers_ = free_buffers;
    std::ostringstream oss;
    oss << "Updated min free buffers=" << free_buffers;
    FX_LOGST(INFO, kTag) << CreateLogString(oss.str());
  }

  return result;
}

std::string VmoPoolWrapper::CreateLogString(const std::string& message) {
  std::ostringstream oss;
  oss << "(";
  if (!name_.empty()) {
    oss << name_ << ", ";
  }
  oss << this << ", buffer size=" << pool_.buffer_size() << ", buffers=" << pool_.total_buffers()
      << "): " << message;
  return oss.str();
}
}  // namespace camera
