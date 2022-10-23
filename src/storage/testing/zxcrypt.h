// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_TESTING_ZXCRYPT_H_
#define SRC_STORAGE_TESTING_ZXCRYPT_H_

#include <lib/zx/result.h>

#include <string>

namespace storage {

// Formats the given block device with a new zxcrypt volume and then unseals the newly created
// volume, waiting for the block device to appear before returning.
//
// Returns the path to the newly created zxcrypt volume.
zx::result<std::string> CreateZxcryptVolume(const std::string& device_path);

}  // namespace storage

#endif  // SRC_STORAGE_TESTING_ZXCRYPT_H_
