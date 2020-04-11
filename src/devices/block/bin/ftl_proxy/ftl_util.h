// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_BIN_FTL_PROXY_FTL_UTIL_H_
#define SRC_DEVICES_BLOCK_BIN_FTL_PROXY_FTL_UTIL_H_

#include <lib/zx/object.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ftl_proxy {

// Returns the topological path of the first FTL that shows up in |device_path|.
//
// This method will block until the FTL shows up in |device_class_path| or |max_wait| time has
// passed.
std::string GetFtlTopologicalPath(std::string_view device_class_path, zx::duration max_wait);

// Given a |device_class_path| where the FTL will eventually appear, waits until the FTL shows up,
// and obtain the topological path.
//
// This method will block until the FTL shows up in |device_class_path| or 10 minutes have
// passed.
inline std::string GetFtlTopologicalPath(std::string_view device_class_path) {
  return GetFtlTopologicalPath(device_class_path, zx::min(10));
}

// Given a |ftl_path| obtain the inspect vmo from the device.
zx::vmo GetFtlInspectVmo(std::string_view ftl_path);

// Returns a the current wear count of the device. This is the maximum wear count over all blocks.
std::optional<uint64_t> GetDeviceWearCount(const zx::vmo& inspect_vmo);

}  // namespace ftl_proxy

#endif  // SRC_DEVICES_BLOCK_BIN_FTL_PROXY_FTL_UTIL_H_
