// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_BLOCK_DEVICES_H_
#define SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_BLOCK_DEVICES_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fitx/result.h>

#include <vector>

fitx::result<std::string, std::vector<fuchsia::virtualization::BlockSpec>> GetBlockDevices(
    size_t stateful_image_size);

#endif  // SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_BLOCK_DEVICES_H_
