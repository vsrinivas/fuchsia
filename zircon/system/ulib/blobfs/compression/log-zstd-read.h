// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_LOG_ZSTD_READ_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_LOG_ZSTD_READ_H_

#include <stdint.h>
#include <stddef.h>

#include <string>

namespace blobfs {

void EnableZSTDReadLogging();

void DisableZSTDReadLogging();

void LogZSTDRead(std::string name, uint8_t* buf, size_t byte_offset, size_t num_bytes);

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_LOG_ZSTD_READ_H_
