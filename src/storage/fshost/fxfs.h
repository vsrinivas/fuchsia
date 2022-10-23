// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FXFS_H_
#define SRC_STORAGE_FSHOST_FXFS_H_

#include <fidl/fuchsia.hardware.block/cpp/wire_types.h>
#include <lib/zx/result.h>

#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/fshost/fshost_config.h"

namespace fshost {

// Reformats `block_device` as an Fxfs instance with the data volume initialized.  Returns the
// filesystem as well as a connection to its data volume.
zx::result<std::pair<fs_management::StartedMultiVolumeFilesystem, fs_management::MountedVolume*>>
FormatFxfsAndInitDataVolume(fidl::ClientEnd<fuchsia_hardware_block::Block> block_device,
                            const fshost_config::Config& config);

// Unwraps the data volume in `fs`.  Any failures should be treated as fatal and the filesystem
// should be reformatted and re-initialized.
zx::result<fs_management::MountedVolume*> UnwrapDataVolume(
    fs_management::StartedMultiVolumeFilesystem& fs, const fshost_config::Config& config);

// Initializes the data volume in `fs`, which should be freshly reformatted.
zx::result<fs_management::MountedVolume*> InitDataVolume(
    fs_management::StartedMultiVolumeFilesystem& fs, const fshost_config::Config& config);

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_FXFS_H_
