// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cache_config.h"

#include <vector>

#include "instructions.h"
#include "msd_intel_buffer.h"
#include "registers.h"

using namespace registers;

uint32_t CacheConfig::InstructionBytesRequired() {
  uint32_t num_dwords = MiLoadDataImmediate::dword_count(kMemoryObjectControlStateEntries) +
                        MiLoadDataImmediate::dword_count(kLncfMemoryObjectControlStateEntries) +
                        MiNoop::kDwordCount * 2 + MiBatchBufferEnd::kDwordCount;
  return num_dwords * sizeof(uint32_t);
}

bool CacheConfig::InitCacheConfig(magma::InstructionWriter* writer,
                                  EngineCommandStreamerId engine_id) {
  DASSERT(engine_id == RENDER_COMMAND_STREAMER);

  std::vector<uint32_t> graphics_mocs;
  GetMemoryObjectControlState(graphics_mocs);

  MiLoadDataImmediate::write(writer, MemoryObjectControlState::kGraphicsOffset,
                             magma::to_uint32(graphics_mocs.size()), graphics_mocs.data());
  MiNoop::write(writer);

  std::vector<uint16_t> lncf_mocs;
  GetLncfMemoryObjectControlState(lncf_mocs);

  std::vector<uint32_t> lncf_mocs_32;
  for (uint32_t i = 0; i < lncf_mocs.size(); i += 2) {
    uint32_t entry = lncf_mocs[i + 1];
    entry = (entry << 16) | lncf_mocs[i];
    lncf_mocs_32.push_back(entry);
  }

  MiLoadDataImmediate::write(writer, LncfMemoryObjectControlState::kOffset,
                             magma::to_uint32(lncf_mocs_32.size()), lncf_mocs_32.data());
  MiNoop::write(writer);

  return true;
}

void CacheConfig::GetMemoryObjectControlState(std::vector<uint32_t>& mocs) {
  mocs.resize(kMemoryObjectControlStateEntries);

  // Mesa assumes index 0 = uncached, 1 = use pagetable settings, 2 = cached
  uint32_t index = 0;
  mocs[index++] = MemoryObjectControlState::format(MemoryObjectControlState::UNCACHED,
                                                   MemoryObjectControlState::LLC_ELLC,
                                                   MemoryObjectControlState::LRU_0);
  mocs[index++] = MemoryObjectControlState::format(MemoryObjectControlState::PAGETABLE,
                                                   MemoryObjectControlState::LLC_ELLC,
                                                   MemoryObjectControlState::LRU_3);
  mocs[index++] = MemoryObjectControlState::format(MemoryObjectControlState::WRITEBACK,
                                                   MemoryObjectControlState::LLC_ELLC,
                                                   MemoryObjectControlState::LRU_3);

  while (index < kMemoryObjectControlStateEntries) {
    mocs[index++] = MemoryObjectControlState::format(MemoryObjectControlState::UNCACHED,
                                                     MemoryObjectControlState::LLC_ELLC,
                                                     MemoryObjectControlState::LRU_0);
  }
}

void CacheConfig::GetLncfMemoryObjectControlState(std::vector<uint16_t>& mocs) {
  mocs.resize(kMemoryObjectControlStateEntries);

  // Mesa assumes index 0 = uncached, 1 = use pagetable settings, 2 = cached
  uint32_t index = 0;
  mocs[index++] = LncfMemoryObjectControlState::format(LncfMemoryObjectControlState::UNCACHED);
  mocs[index++] = LncfMemoryObjectControlState::format(LncfMemoryObjectControlState::WRITEBACK);
  mocs[index++] = LncfMemoryObjectControlState::format(LncfMemoryObjectControlState::WRITEBACK);

  while (index < kMemoryObjectControlStateEntries) {
    mocs[index++] = LncfMemoryObjectControlState::format(LncfMemoryObjectControlState::UNCACHED);
  }
}
