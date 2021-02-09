// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_CONFIGS_CHUNKED_COMPRESSION_PARAMS_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_CONFIGS_CHUNKED_COMPRESSION_PARAMS_H_

#include "src/lib/chunked-compression/chunked-compressor.h"
namespace blobfs {

// Returns the default chunked compression params based on |input_size| which
// is the original uncompressed on-disk size in bytes for a blob.
chunked_compression::CompressionParams GetDefaultChunkedCompressionParams(size_t input_size);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_CONFIGS_CHUNKED_COMPRESSION_PARAMS_H_
