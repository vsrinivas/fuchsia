// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_EXTERNAL_DECOMPRESSOR_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_EXTERNAL_DECOMPRESSOR_H_

#include <blobfs/compression-settings.h>
#include <fuchsia/blobfs/internal/llcpp/fidl.h>

namespace blobfs {

// Convert from fidl compatible enum to local.
CompressionAlgorithm CompressionAlgorithmFidlToLocal(
    const llcpp::fuchsia::blobfs::internal::CompressionAlgorithm algorithm);

// Convert to fidl compatible enum from local.
llcpp::fuchsia::blobfs::internal::CompressionAlgorithm CompressionAlgorithmLocalToFidl(
    CompressionAlgorithm algorithm);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_EXTERNAL_DECOMPRESSOR_H_
