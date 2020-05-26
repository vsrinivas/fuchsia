// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_FVM_H_
#define SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_FVM_H_

#include <lib/zx/status.h>

#include <string>

namespace isolated_devmgr {

// Creates an Fvm partition with the given slice size on the given device.
zx::status<std::string> CreateFvmPartition(const std::string& device_path, int slice_size);

}  // namespace isolated_devmgr

#endif  // SRC_LIB_ISOLATED_DEVMGR_V2_COMPONENT_FVM_H_
