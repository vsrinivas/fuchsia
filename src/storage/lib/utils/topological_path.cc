// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/utils/topological_path.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/service/llcpp/service.h>

namespace storage {

zx::status<std::string> GetTopologicalPath(
    fidl::UnownedClientEnd<fuchsia_device::Controller> channel) {
  auto result = fidl::WireCall(channel).GetTopologicalPath();
  if (!result.ok())
    return zx::error(result.status());
  if (result->result.is_err())
    return zx::error(result->result.err());
  return zx::ok(
      std::string(result->result.response().path.data(), result->result.response().path.size()));
}

zx::status<std::string> GetTopologicalPath(const std::string& path) {
  auto client_end_or = service::Connect<fuchsia_device::Controller>(path.c_str());
  if (client_end_or.is_error())
    return client_end_or.take_error();
  return GetTopologicalPath(*client_end_or);
}

}  // namespace storage
