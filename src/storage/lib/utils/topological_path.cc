// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/utils/topological_path.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>

namespace storage {

zx::result<std::string> GetTopologicalPath(
    fidl::UnownedClientEnd<fuchsia_device::Controller> channel) {
  auto result = fidl::WireCall(channel)->GetTopologicalPath();
  if (!result.ok())
    return zx::error(result.status());
  if (result->is_error())
    return zx::error(result->error_value());
  return zx::ok(std::string(result->value()->path.data(), result->value()->path.size()));
}

zx::result<std::string> GetTopologicalPath(const std::string& path) {
  auto client_end_or = component::Connect<fuchsia_device::Controller>(path.c_str());
  if (client_end_or.is_error())
    return client_end_or.take_error();
  return GetTopologicalPath(*client_end_or);
}

}  // namespace storage
