// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONSTANTS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONSTANTS_H_

#include <zircon/types.h>

namespace block_verity {

constexpr size_t kBlockSize = 4096;
constexpr size_t kHashOutputSize = 32;

// "block-verity-v1\0"
constexpr uint8_t kBlockVerityMagic[16] = {0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x2d, 0x76, 0x65,
                                           0x72, 0x69, 0x74, 0x79, 0x2d, 0x76, 0x31, 0x00};

// little-endian 1 as uint32_t
constexpr uint8_t kSHA256HashTag[4] = {0x01, 0x00, 0x00, 0x00};

// little-endian 4096 as uint32_t
constexpr uint8_t kBlockSizeTag[4] = {0x00, 0x10, 0x00, 0x00};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONSTANTS_H_
