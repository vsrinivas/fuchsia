// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_BENCHMARK_MINIDUMP_MEMORY_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_BENCHMARK_MINIDUMP_MEMORY_H_

#include <cstdint>
#include <memory>

#include "src/developer/debug/third_party/libunwindstack/include/unwindstack/Memory.h"
#include "src/developer/debug/unwinder/memory.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"

namespace benchmark {

std::string MinidumpGetBuildID(const crashpad::ModuleSnapshot& mod);

class MinidumpMemory : public unwindstack::Memory, public unwinder::Memory {
 public:
  class MemoryRegion {
   public:
    MemoryRegion(uint64_t start_in, size_t size_in) : start(start_in), size(size_in) {}
    virtual ~MemoryRegion() = default;

    virtual size_t Read(uint64_t offset, size_t size, void* dst) const = 0;

    const uint64_t start;
    const size_t size;
  };

  explicit MinidumpMemory(const crashpad::ProcessSnapshotMinidump& minidump);

  size_t Read(uint64_t addr, void* dst, size_t size) override;
  unwinder::Error ReadBytes(uint64_t addr, uint64_t size, void* dst) override;

  struct Statistics {
    uint64_t read_count = 0;
    uint64_t total_read_size = 0;
  };

  const Statistics& GetStatistics() const { return statistics_; }
  void ResetStatistics() { statistics_ = Statistics(); }

 private:
  std::vector<std::unique_ptr<MemoryRegion>> regions_;
  Statistics statistics_;
};

}  // namespace benchmark

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_BENCHMARK_MINIDUMP_MEMORY_H_
