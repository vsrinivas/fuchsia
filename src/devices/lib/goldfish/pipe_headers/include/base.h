// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_LIB_GOLDFISH_INCLUDE_BASE_H_
#define SRC_GRAPHICS_DISPLAY_LIB_GOLDFISH_INCLUDE_BASE_H_

#include <zircon/compiler.h>

#include <cstdint>

namespace goldfish {

// Maximum number of buffers that can be used for read/write commands.
const uint32_t MAX_BUFFERS_PER_COMMAND = 336;

// Additional command parameters used for read/write commands.
// Note: This structure is known to both the virtual device and driver.
struct PipeCmdReadWriteParams {
  uint32_t buffers_count;
  int32_t consumed_size;
  uint64_t ptrs[MAX_BUFFERS_PER_COMMAND];
  uint32_t sizes[MAX_BUFFERS_PER_COMMAND];
  uint32_t read_index;
} __PACKED;

// Pipe command structure used for all commands.
// Note: This structure is known to both the virtual device and driver.
struct PipeCmdBuffer {
  int32_t cmd;
  int32_t id;
  int32_t status;
  int32_t reserved;
  PipeCmdReadWriteParams rw_params;
} __PACKED;

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DISPLAY_LIB_GOLDFISH_INCLUDE_BASE_H_
