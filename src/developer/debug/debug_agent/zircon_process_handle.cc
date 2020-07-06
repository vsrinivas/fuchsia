// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/zircon_process_handle.h"

#include "src/developer/debug/debug_agent/process_info.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

namespace {}  // namespace

ZirconProcessHandle::ZirconProcessHandle(zx_koid_t process_koid, zx::process p)
    : process_koid_(process_koid), process_(std::move(p)) {}

zx_status_t ZirconProcessHandle::GetInfo(zx_info_process* info) const {
  return process_.get_info(ZX_INFO_PROCESS, info, sizeof(zx_info_process), nullptr, nullptr);
}

std::vector<debug_ipc::AddressRegion> ZirconProcessHandle::GetAddressSpace(uint64_t address) const {
  std::vector<debug_ipc::AddressRegion> regions;
  std::vector<zx_info_maps_t> map = GetMaps();

  if (address) {
    // Get a specific region.
    for (const auto& entry : map) {
      if (address < entry.base)
        continue;
      if (address <= (entry.base + entry.size))
        regions.push_back({entry.name, entry.base, entry.size, entry.depth});
    }
  } else {
    // Get all regions.
    size_t ix = 0;
    regions.resize(map.size());
    for (const auto& entry : map) {
      regions[ix].name = entry.name;
      regions[ix].base = entry.base;
      regions[ix].size = entry.size;
      regions[ix].depth = entry.depth;
      ++ix;
    }
  }

  return regions;
}

zx_status_t ZirconProcessHandle::ReadMemory(uintptr_t address, void* buffer, size_t len,
                                            size_t* actual) const {
  return process_.read_memory(address, buffer, len, actual);
}

zx_status_t ZirconProcessHandle::WriteMemory(uintptr_t address, const void* buffer, size_t len,
                                             size_t* actual) {
  return process_.write_memory(address, buffer, len, actual);
}

std::vector<debug_ipc::MemoryBlock> ZirconProcessHandle::ReadMemoryBlocks(uint64_t address,
                                                                          uint32_t size) const {
  // Optimistically assume the read will work which will be faster in the common case.
  if (debug_ipc::MemoryBlock block = ReadOneMemoryBlock(address, size); block.valid)
    return {std::move(block)};

  // Failure reading, this memory is either not mapped or it may cross mapping boundaries. To solve
  // the multiple boundary problem, get the memory mapping and compute all mapping boundaries in the
  // requested region. Then try to read each of the resulting blocks (which may be valid or
  // invalid).
  //
  // This computed boundaries array will contain all boundaries (including the end address and some
  // duplicates) except the begin address (this will be implicit in the later computation).
  std::vector<uint64_t> boundaries;
  for (const zx_info_maps_t& map : GetMaps()) {
    // The returned maps should be sorted so any mapping region starting past our region means all
    // relevant boundaries have been found.
    if (map.base > address + size)
      break;
    if (map.base > address)
      boundaries.push_back(map.base);
    uint64_t end = map.base + map.size;
    if (end > address && end < address + size)
      boundaries.push_back(end);
  }
  boundaries.push_back(address + size);
  std::sort(boundaries.begin(), boundaries.end());

  std::vector<debug_ipc::MemoryBlock> blocks;

  uint64_t begin = address;
  for (uint64_t end : boundaries) {
    // There will be some duplicates in the boundaries array so skip anything that's empty. These
    // duplicates are caused by a range which a child inside it that is coincident with one of the
    // parent boundaries, or two regions that abut each other.
    if (end == begin)
      continue;
    blocks.push_back(ReadOneMemoryBlock(begin, static_cast<uint32_t>(end - begin)));
    begin = end;
  }
  return blocks;
}

debug_ipc::MemoryBlock ZirconProcessHandle::ReadOneMemoryBlock(uint64_t address,
                                                               uint32_t size) const {
  debug_ipc::MemoryBlock block;

  block.address = address;
  block.size = size;
  block.data.resize(size);

  size_t bytes_read = 0;
  if (process_.read_memory(address, &block.data[0], block.size, &bytes_read) == ZX_OK &&
      bytes_read == size) {
    block.valid = true;
  } else {
    block.valid = false;
    block.data.resize(0);
  }
  return block;
}

std::vector<zx_info_maps_t> ZirconProcessHandle::GetMaps() const {
  const size_t kRegionsCountGuess = 64u;
  const size_t kNewRegionsCountGuess = 4u;

  size_t count_guess = kRegionsCountGuess;

  std::vector<zx_info_maps_t> map;
  size_t actual;
  size_t avail;

  while (true) {
    map.resize(count_guess);

    zx_status_t status = process_.get_info(ZX_INFO_PROCESS_MAPS, &map[0],
                                           sizeof(zx_info_maps) * map.size(), &actual, &avail);

    if (status != ZX_OK) {
      actual = 0;
      break;
    } else if (actual == avail) {
      break;
    }

    count_guess = avail + kNewRegionsCountGuess;
  }

  map.resize(actual);
  return map;
}

}  // namespace debug_agent
