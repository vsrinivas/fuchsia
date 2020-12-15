// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header includes FVM utilities which may be used by clients of
// the volume manager.

#ifndef SRC_STORAGE_FVM_CLIENT_H_
#define SRC_STORAGE_FVM_CLIENT_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/zx/channel.h>
#include <zircon/device/block.h>

#include <block-client/cpp/block-device.h>

namespace fvm {

// Walks through all slices on the partition backed by |device|, attempting to
// free everything except for the first slice.
zx_status_t ResetAllSlices(block_client::BlockDevice* device);

}  // namespace fvm

#endif  // SRC_STORAGE_FVM_CLIENT_H_
