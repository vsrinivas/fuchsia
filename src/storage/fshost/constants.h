// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_CONSTANTS_H_
#define SRC_STORAGE_FSHOST_CONSTANTS_H_

#include <string_view>

namespace fshost {

// These need to match whatever our imaging tools do.
inline constexpr std::string_view kBlobfsPartitionLabel = "blobfs";
inline constexpr std::string_view kDataPartitionLabel = "data";
inline constexpr std::string_view kLegacyDataPartitionLabel = "minfs";

// This is the path the to fshost admin service that shell tools should see.
inline constexpr std::string_view kHubAdminServicePath =
    "/hub-v2/children/bootstrap/children/fshost/exec/out/svc/fuchsia.fshost.Admin";

// Binaries for data partition filesystems are expected to be at well known locations.
inline const char* kMinfsPath = "/pkg/bin/minfs";
inline const char* kFxfsPath = "/pkg/bin/fxfs";
inline const char* kF2fsPath = "/pkg/bin/f2fs";
inline const char* kFactoryfsPath = "/pkg/bin/factoryfs";

// These are default sizes of data partition.
inline const uint64_t kDefaultMinfsMaxBytes = 24 * 1024 * 1024;
inline const uint64_t kDefaultF2fsMinBytes = 100 * 1024 * 1024;

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_CONSTANTS_H_
