// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_BACKENDS_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_BACKENDS_H_

#include <lib/stdcompat/span.h>

#include "partition.h"

namespace gigaboot {

const cpp20::span<const uint8_t> GetPermanentAttributes();
const cpp20::span<const uint8_t> GetPermanentAttributesHash();

// Get factory default partition information
const cpp20::span<const PartitionMap::PartitionEntry> GetPartitionCustomizations();

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_BACKENDS_H_
