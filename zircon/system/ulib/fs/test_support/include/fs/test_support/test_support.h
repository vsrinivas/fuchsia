// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TEST_SUPPORT_TEST_SUPPORT_H_
#define FS_TEST_SUPPORT_TEST_SUPPORT_H_

#include <zircon/types.h>

#include <string>

namespace fs {

// Returns the full topological path from a device path or channel.
std::string GetTopologicalPath(const std::string& path);
std::string GetTopologicalPath(zx_handle_t channel);

}  // namespace fs

#endif  // FS_TEST_SUPPORT_TEST_SUPPORT_H_
