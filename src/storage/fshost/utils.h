// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_UTILS_H_
#define SRC_STORAGE_FSHOST_UTILS_H_

#include <fidl/fuchsia.device/cpp/wire_types.h>
#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

namespace fshost {

// Clones the given node, returning a raw channel to it.
zx::status<zx::channel> CloneNode(fidl::UnownedClientEnd<fuchsia_io::Node> node);

// Returns the topological path of the given device.
zx::status<std::string> GetDevicePath(fidl::UnownedClientEnd<fuchsia_device::Controller> device);

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_UTILS_H_
