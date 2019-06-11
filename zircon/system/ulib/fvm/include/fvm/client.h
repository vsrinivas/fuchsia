// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header includes FVM utilities which may be used by clients of
// the volume manager.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <block-client/cpp/block-device.h>
#include <lib/zx/channel.h>
#include <zircon/device/block.h>

namespace fvm {

// Walks through all slices on the partition backed by |channel|, attempting to free
// everything except for the first slice.
//
// This method is deprecated: Prefer ResetAllSlices2.
zx_status_t ResetAllSlices(const zx::unowned_channel& channel);

// Identical to ResetAllSlices, but operating on the |BlockDevice| interface.
//
// TODO(smklein): Deprecate all uses of |ResetAllSlices|, rename this function
// to |ResetAllSlices|. This should be possible as soon as blobfs and minfs both
// use the BlockDevice interface.
zx_status_t ResetAllSlices2(block_client::BlockDevice* device);

}
