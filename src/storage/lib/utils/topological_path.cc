// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/utils/topological_path.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>

#include <fbl/unique_fd.h>

namespace storage {

zx::status<std::string> GetTopologicalPath(const zx::channel& channel) {
  auto result =
      llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(zx::unowned_channel(channel));
  if (!result.ok())
    return zx::error(result.status());
  if (result->result.is_err())
    return zx::error(result->result.err());
  return zx::ok(
      std::string(result->result.response().path.data(), result->result.response().path.size()));
}

zx::status<std::string> GetTopologicalPath(const std::string& path) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (!fd)
    return zx::error(ZX_ERR_NOT_FOUND);
  fdio_cpp::FdioCaller caller(std::move(fd));
  return GetTopologicalPath(*caller.channel());
}

}  // namespace storage
