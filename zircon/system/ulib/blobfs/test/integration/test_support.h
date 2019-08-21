// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_TEST_INTEGRATION_TEST_SUPPORT_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_TEST_INTEGRATION_TEST_SUPPORT_H_

#include <zircon/types.h>

#include <string>

// Returns the full topological path from a device path or channel.
std::string GetTopologicalPath(const std::string& path);
std::string GetTopologicalPath(zx_handle_t channel);

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_TEST_INTEGRATION_TEST_SUPPORT_H_
