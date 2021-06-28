// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MINIDUMP_MEMORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MINIDUMP_MEMORY_H_

#include <cstdint>
#include <memory>
#include <utility>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/unwinder/memory.h"
#include "src/developer/debug/zxdb/symbols/build_id_index.h"
#include "third_party/crashpad/snapshot/memory_snapshot.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"

namespace zxdb {

// The memory of a process from minidump. It consists of multiple regions, some are backed by the
// memory snapshot in the minidump, some are backed by files on disk.
class MinidumpMemory {
 public:
  // Use unwinder::Memory as our abstract region interface, so that we can directly feed those
  // memory regions to the unwinder.
  //
  // The boundary of a memory region is not saved in the region itself but rather saved in us.
  // The callers of |ReadBytes| must ensure that they do not go beyond the boundary.
  using Region = unwinder::Memory;

  // Memory region backed by minidump memory snapshot, e.g., a stack.
  class SnapshotMemoryRegion : public Region {
   public:
    // snapshot should outlive us.
    explicit SnapshotMemoryRegion(const crashpad::MemorySnapshot* snapshot) : snapshot_(snapshot) {}
    unwinder::Error ReadBytes(uint64_t addr, uint64_t size, void* dst) override;

   private:
    const crashpad::MemorySnapshot* snapshot_;
  };

  // Memory region backed by a file, e.g., .text and .rodata of a module.
  class FileMemoryRegion : public Region {
   public:
    FileMemoryRegion(uint64_t load_address, const std::string& path)
        : load_address_(load_address), file_(fopen(path.c_str(), "rb"), fclose) {}
    unwinder::Error ReadBytes(uint64_t addr, uint64_t size, void* dst) override;

   private:
    uint64_t load_address_;
    std::unique_ptr<FILE, decltype(&fclose)> file_;
  };

  MinidumpMemory(const crashpad::ProcessSnapshotMinidump& minidump, BuildIDIndex& build_id_index);

  // For testing.
  explicit MinidumpMemory(
      std::vector<std::tuple<uint64_t, uint64_t, std::shared_ptr<Region>>> regions)
      : regions_(std::move(regions)) {}

  // Similar to |debug_agent::ProcessHandle::ReadMemoryBlocks|.
  // Used by |MinidumpRemoteAPI::ReadMemory|.
  std::vector<debug_ipc::MemoryBlock> ReadMemoryBlocks(uint64_t address, uint64_t size);

  // Used by the unwinder.
  Region* GetMemoryRegion(uint64_t address);
  std::map<uint64_t, Region*> GetDebugModuleMap();

 private:
  // regions_ include the stacks and the modules. Using shared_ptr here because multiple regions
  // could be provided with the same module.
  std::vector<std::tuple<uint64_t, uint64_t, std::shared_ptr<Region>>> regions_;

  // debug_modules_ are used to provide the module_map for the unwinder.
  std::map<uint64_t, FileMemoryRegion> debug_modules_;
};

// Helper to get BuildID from a minidump module.
std::string MinidumpGetBuildId(const crashpad::ModuleSnapshot& mod);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MINIDUMP_MEMORY_H_
