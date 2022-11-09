/*
 * Copyright 2022 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SRC_SECURITY_LIB_SCRUTINY_UTILS_FFI_BLOBFS_EXPORT_H_
#define SRC_SECURITY_LIB_SCRUTINY_UTILS_FFI_BLOBFS_EXPORT_H_

#include <cstddef>

// FFI for ExportBlobs
// Acts as a C bridge to the C++ ExportBlobs function to enable it to
// be used by the rust FFI.
extern "C" int blobfs_export_blobs(const char* source_path, const char* output_path);

#endif  // SRC_SECURITY_LIB_SCRUTINY_UTILS_FFI_BLOBFS_EXPORT_H_
