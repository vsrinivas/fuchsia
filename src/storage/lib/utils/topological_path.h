// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_UTILS_TOPOLOGICAL_PATH_H_
#define SRC_STORAGE_LIB_UTILS_TOPOLOGICAL_PATH_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>

#include <string>
#include <string_view>

namespace storage {

zx::result<std::string> GetTopologicalPath(
    fidl::UnownedClientEnd<fuchsia_device::Controller> channel);
zx::result<std::string> GetTopologicalPath(const std::string& path);

}  // namespace storage

#endif  // SRC_STORAGE_LIB_UTILS_TOPOLOGICAL_PATH_H_
