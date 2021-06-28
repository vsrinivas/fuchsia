// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/minidump_memory.h"

#include <memory>

#include "src/lib/elflib/elflib.h"

namespace zxdb {

namespace {

// Helper to make crashpad::MemorySnapshot::Read easier to use.
bool ReadMinidumpMemorySnapshot(const crashpad::MemorySnapshot& memory,
                                std::function<bool(void*, size_t)> callback) {
  class Delegate : public crashpad::MemorySnapshot::Delegate {
   public:
    explicit Delegate(std::function<bool(void*, size_t)> cb) : cb_(std::move(cb)) {}
    bool MemorySnapshotDelegateRead(void* data, size_t size) override { return cb_(data, size); }
    std::function<bool(void*, size_t)> cb_;
  };
  Delegate delegate(std::move(callback));
  return memory.Read(&delegate);
}

}  // namespace

MinidumpMemory::MinidumpMemory(const crashpad::ProcessSnapshotMinidump& minidump,
                               BuildIDIndex& build_id_index) {
  for (const auto& thread : minidump.Threads()) {
    auto stack = thread->Stack();

    if (!stack) {
      continue;
    }

    regions_.emplace_back(stack->Address(), stack->Address() + stack->Size(),
                          std::make_shared<SnapshotMemoryRegion>(stack));
  }

  for (const auto& minidump_mod : minidump.Modules()) {
    uint64_t base = minidump_mod->Address();
    auto entry = build_id_index.EntryForBuildID(MinidumpGetBuildId(*minidump_mod));
    if (!entry.debug_info.empty()) {
      debug_modules_.emplace(base, FileMemoryRegion(base, entry.debug_info));
    }

    if (entry.binary.empty()) {
      continue;
    }

    auto elf = elflib::ElfLib::Create(entry.binary);
    if (!elf) {
      continue;
    }
    auto module = std::make_shared<FileMemoryRegion>(base, entry.binary);
    for (const auto& segment : elf->GetSegmentHeaders()) {
      // Only PT_LOAD segments are actually mapped. The rest are informational.
      if (segment.p_type != elflib::PT_LOAD) {
        continue;
      }
      if (segment.p_flags & elflib::PF_W) {
        // Writable segment. Data in the ELF file might not match what was present at the time of
        // the crash.
        continue;
      }
      regions_.emplace_back(base + segment.p_vaddr, base + segment.p_vaddr + segment.p_memsz,
                            module);
    }
  }
  std::sort(regions_.begin(), regions_.end());

  // Sanity check.
  uint64_t last_end = 0;
  for (auto& [start, end, mem] : regions_) {
    FX_CHECK(start >= last_end);
    last_end = end;
  }
}

std::vector<debug_ipc::MemoryBlock> MinidumpMemory::ReadMemoryBlocks(uint64_t address,
                                                                     uint64_t size) {
  uint64_t end = address + size;
  std::vector<debug_ipc::MemoryBlock> res;
  if (address == end) {
    return res;
  }
  for (auto& [region_start, region_end, region_memory] : regions_) {
    // Space before the first region and between any two regions.
    if (address < region_start) {
      auto& block = res.emplace_back();
      block.address = address;
      block.size = std::min(end, region_start) - address;
      block.valid = false;
      if (end <= region_start) {
        address = end;
        break;
      }
      address = region_start;
    }
    // Now we have address >= region_start.
    if (address < region_end) {
      auto& block = res.emplace_back();
      block.address = address;
      block.size = std::min(end, region_end) - address;
      block.data.resize(block.size);
      block.valid = region_memory->ReadBytes(address, block.size, block.data.data()).ok();
      if (!block.valid) {
        block.data.clear();
      }
      if (end <= region_end) {
        address = end;
        break;
      }
      address = region_end;
    }
  }
  // Space after the last region.
  if (address < end) {
    auto& block = res.emplace_back();
    block.address = address;
    block.size = end - address;
    block.valid = false;
  }
  return res;
}

unwinder::Memory* MinidumpMemory::GetMemoryRegion(uint64_t address) {
  for (auto& [start, end, memory] : regions_) {
    if (address >= start && address < end) {
      return memory.get();
    }
  }
  return nullptr;
}

std::map<uint64_t, unwinder::Memory*> MinidumpMemory::GetDebugModuleMap() {
  std::map<uint64_t, unwinder::Memory*> res;
  for (auto& [addr, memory] : debug_modules_) {
    res.emplace(addr, &memory);
  }
  return res;
}

unwinder::Error MinidumpMemory::SnapshotMemoryRegion::ReadBytes(uint64_t addr, uint64_t size,
                                                                void* dst) {
  if (addr < snapshot_->Address() || addr + size > snapshot_->Address() + snapshot_->Size()) {
    return unwinder::Error("out of boundary");
  }
  uint64_t offset = addr - snapshot_->Address();
  bool ok = ReadMinidumpMemorySnapshot(*snapshot_, [&](void* data, uint64_t actual_size) {
    if (offset + size > actual_size) {
      return false;
    }
    memcpy(dst, reinterpret_cast<uint8_t*>(data) + offset, size);
    return true;
  });
  if (ok) {
    return unwinder::Success();
  }
  return unwinder::Error("error reading from the memory snapshot");
}

unwinder::Error MinidumpMemory::FileMemoryRegion::ReadBytes(uint64_t addr, uint64_t size,
                                                            void* dst) {
  if (addr < load_address_) {
    return unwinder::Error("out of boundary");
  }
  fseek(file_.get(), static_cast<int64_t>(addr - load_address_), SEEK_SET);
  if (fread(dst, 1, size, file_.get()) != size) {
    return unwinder::Error("short read");
  }
  return unwinder::Success();
}

std::string MinidumpGetBuildId(const crashpad::ModuleSnapshot& mod) {
  auto build_id = mod.BuildID();

  if (build_id.empty()) {
    return "";
  }

  // 2 hex characters per 1 byte, so the string size is twice the data size. Hopefully we'll be
  // overwriting the zeros we're filling with.
  std::string ret(build_id.size() * 2, '\0');
  char* pos = ret.data();

  for (const auto& byte : build_id) {
    sprintf(pos, "%02hhx", byte);
    pos += 2;
  }

  return ret;
}

}  // namespace zxdb
