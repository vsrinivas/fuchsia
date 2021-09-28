// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/driver-inspector.h"

#include <lib/inspect/cpp/reader.h>
#include <zircon/errors.h>

#include <algorithm>

namespace wlan::iwlwifi {

DriverInspector::DriverInspector(DriverInspectorOptions options)
    : inspector_(std::make_unique<::inspect::Inspector>(
          ::inspect::InspectSettings{.maximum_size = options.vmo_size})),
      core_dump_capacity_(options.core_dump_capacity) {}

DriverInspector::~DriverInspector() = default;

zx_status_t DriverInspector::PublishCoreDump(const char* core_dump_name,
                                             cpp20::span<const char> core_dump) {
  zx_status_t status = ZX_OK;
  if (core_dump.size() > core_dump_capacity_) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard lock(core_dump_mutex_);

  // Drop the oldest core dumps until enough space is available.
  size_t total_size = 0;
  for (auto riter = core_dumps_.rbegin(); riter != core_dumps_.rend(); ++riter) {
    total_size += riter->dump_size_;
    if (core_dump.size() + total_size > core_dump_capacity_) {
      core_dumps_.erase(core_dumps_.begin(), riter.base());
      break;
    }
  }

  ::inspect::ByteVectorProperty property = GetRoot().CreateByteVector(
      core_dump_name, {reinterpret_cast<const uint8_t*>(core_dump.data()), core_dump.size()});
  if (!property) {
    return ZX_ERR_NO_SPACE;
  }

  core_dumps_.emplace_back(CoreDumpEntry{std::move(property), core_dump.size()});
  return status;
}

::inspect::Node& DriverInspector::GetRoot() const {
  // ::inspect::Node is thread-safe.
  return inspector_->GetRoot();
}

::zx::vmo DriverInspector::DuplicateVmo() const { return inspector_->DuplicateVmo(); }

}  // namespace wlan::iwlwifi
