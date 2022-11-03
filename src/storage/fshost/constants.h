// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_CONSTANTS_H_
#define SRC_STORAGE_FSHOST_CONSTANTS_H_

#include <string_view>

namespace fshost {

// These need to match whatever our imaging tools do.
constexpr std::string_view kBlobfsPartitionLabel = "blobfs";
constexpr std::string_view kDataPartitionLabel = "data";
constexpr std::string_view kLegacyDataPartitionLabel = "minfs";

// This is the path the to fshost admin service that shell tools should see.
constexpr std::string_view kHubAdminServicePath =
    "/hub-v2/children/bootstrap/children/fshost/exec/out/svc/fuchsia.fshost.Admin";

// Binaries for data partition filesystems are expected to be at well known locations.
constexpr char kMinfsPath[] = "/pkg/bin/minfs";
constexpr char kFxfsPath[] = "/pkg/bin/fxfs";
constexpr char kF2fsPath[] = "/pkg/bin/f2fs";
constexpr char kFactoryfsPath[] = "/pkg/bin/factoryfs";

// These are default sizes of data partition.
constexpr uint64_t kDefaultMinfsMaxBytes = 24ull * 1024ull * 1024ull;
constexpr uint64_t kDefaultF2fsMinBytes = 100ull * 1024ull * 1024ull;

constexpr std::string_view kBlockDeviceClassPrefix = "/dev/class/block";
constexpr std::string_view kNandDeviceClassPrefix = "/dev/class/nand";

constexpr char kFVMDriverPath[] = "fvm.so";
constexpr char kGPTDriverPath[] = "gpt.so";
constexpr char kMBRDriverPath[] = "mbr.so";
constexpr char kZxcryptDriverPath[] = "zxcrypt.so";
constexpr char kBootpartDriverPath[] = "bootpart.so";
constexpr char kBlockVerityDriverPath[] = "block-verity.so";
constexpr char kNandBrokerDriverPath[] = "nand-broker.so";

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_CONSTANTS_H_
