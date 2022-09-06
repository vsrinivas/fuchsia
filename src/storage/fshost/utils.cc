// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/utils.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>

namespace fshost {

zx::status<zx::channel> CloneNode(fidl::UnownedClientEnd<fuchsia_io::Node> node) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (endpoints.is_error())
    return endpoints.take_error();

  if (zx_status_t status =
          fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::Node>(node))
              ->Clone(fuchsia_io::wire::OpenFlags::kCloneSameRights, std::move(endpoints->server))
              .status();
      status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(std::move(endpoints->client).TakeChannel());
}

zx::status<std::string> GetDevicePath(fidl::UnownedClientEnd<fuchsia_device::Controller> device) {
  std::string device_path;
  if (auto result = fidl::WireCall(device)->GetTopologicalPath(); result.status() != ZX_OK) {
    return zx::error(result.status());
  } else if (result->is_error()) {
    return zx::error(result->error_value());
  } else {
    device_path = result->value()->path.get();
  }
  return zx::ok(device_path);
}

}  // namespace fshost
