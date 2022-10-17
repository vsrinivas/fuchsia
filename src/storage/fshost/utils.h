// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_UTILS_H_
#define SRC_STORAGE_FSHOST_UTILS_H_

#include <fidl/fuchsia.device/cpp/wire_types.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire_types.h>
#include <fidl/fuchsia.io/cpp/wire_types.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

namespace fshost {

// Resizes `volume` to a contiguous range of up to `target_bytes` (rounded up to the nearest slice
// size), freeing all other slices allocated to the volume.  This is destructive of any data stored
// in the volume.
// Returns the resulting size of the volume.
// If `target_bytes` is 0, then the volume will be sized to the larger of 24MiB and 10% of the
// available space.
// If `inside_zxcrypt` is set, one less FVM slice is allocated, since the zxcrypt header occupies
// one slice.
zx::result<uint64_t> ResizeVolume(
    fidl::UnownedClientEnd<fuchsia_hardware_block_volume::Volume> volume, uint64_t target_bytes,
    bool inside_zxcrypt);

// Clones the given node, returning a raw channel to it.
zx::result<zx::channel> CloneNode(fidl::UnownedClientEnd<fuchsia_io::Node> node);

// Returns the topological path of the given device.
zx::result<std::string> GetDevicePath(fidl::UnownedClientEnd<fuchsia_device::Controller> device);

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_UTILS_H_
