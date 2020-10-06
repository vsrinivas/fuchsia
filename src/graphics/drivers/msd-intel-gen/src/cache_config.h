// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CACHE_CONFIG_H
#define CACHE_CONFIG_H

#include <stdint.h>

#include <memory>

#include <magma_util/instruction_writer.h>

#include "types.h"

class CacheConfig {
 public:
  // Returns the number of bytes required to write into the instruction stream.
  static uint32_t InstructionBytesRequired();

  // Assumes there is sufficient space available to write into the instruction stream.
  static bool InitCacheConfig(magma::InstructionWriter* writer, EngineCommandStreamerId engine_id);

 private:
  static void GetLncfMemoryObjectControlState(std::vector<uint16_t>& mocs);
  static void GetMemoryObjectControlState(std::vector<uint32_t>& mocs);

  static constexpr uint32_t kMemoryObjectControlStateEntries = 62;
  static constexpr uint32_t kLncfMemoryObjectControlStateEntries =
      kMemoryObjectControlStateEntries / 2;

  static_assert(kMemoryObjectControlStateEntries % 2 == 0,
                "kMemoryObjectControlStateEntries not even");

  friend class TestCacheConfig;
};

#endif  // CACHE_CONFIG
