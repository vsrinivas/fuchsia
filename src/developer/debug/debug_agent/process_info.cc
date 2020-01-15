// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/process_info.h"

// clang-format off
// Included early because of conflicts.
#include "src/lib/elflib/elflib.h"
// clang-format on

#include <inttypes.h>
#include <lib/zx/thread.h>
#include <link.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <algorithm>
#include <iterator>
#include <map>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/lib/fxl/logging.h"

namespace debug_agent {

namespace {

zx_status_t WalkModules(const zx::process& process, uint64_t dl_debug_addr,
                        std::function<bool(const zx::process&, uint64_t, uint64_t)> cb) {
  size_t num_read = 0;
  uint64_t lmap = 0;
  zx_status_t status =
      process.read_memory(dl_debug_addr + offsetof(r_debug, r_map), &lmap, sizeof(lmap), &num_read);
  if (status != ZX_OK)
    return status;

  size_t module_count = 0;

  // Walk the linked list.
  constexpr size_t kMaxObjects = 512;  // Sanity threshold.
  while (lmap != 0) {
    if (module_count++ >= kMaxObjects)
      return ZX_ERR_BAD_STATE;

    uint64_t base;
    if (process.read_memory(lmap + offsetof(link_map, l_addr), &base, sizeof(base), &num_read) !=
        ZX_OK)
      break;

    uint64_t next;
    if (process.read_memory(lmap + offsetof(link_map, l_next), &next, sizeof(next), &num_read) !=
        ZX_OK)
      break;

    if (!cb(process, base, lmap))
      break;

    lmap = next;
  }

  return ZX_OK;
}

debug_ipc::ThreadRecord::BlockedReason ThreadStateBlockedReasonToEnum(uint32_t state) {
  FXL_DCHECK(ZX_THREAD_STATE_BASIC(state) == ZX_THREAD_STATE_BLOCKED);

  switch (state) {
    case ZX_THREAD_STATE_BLOCKED_EXCEPTION:
      return debug_ipc::ThreadRecord::BlockedReason::kException;
    case ZX_THREAD_STATE_BLOCKED_SLEEPING:
      return debug_ipc::ThreadRecord::BlockedReason::kSleeping;
    case ZX_THREAD_STATE_BLOCKED_FUTEX:
      return debug_ipc::ThreadRecord::BlockedReason::kFutex;
    case ZX_THREAD_STATE_BLOCKED_PORT:
      return debug_ipc::ThreadRecord::BlockedReason::kPort;
    case ZX_THREAD_STATE_BLOCKED_CHANNEL:
      return debug_ipc::ThreadRecord::BlockedReason::kChannel;
    case ZX_THREAD_STATE_BLOCKED_WAIT_ONE:
      return debug_ipc::ThreadRecord::BlockedReason::kWaitOne;
    case ZX_THREAD_STATE_BLOCKED_WAIT_MANY:
      return debug_ipc::ThreadRecord::BlockedReason::kWaitMany;
    case ZX_THREAD_STATE_BLOCKED_INTERRUPT:
      return debug_ipc::ThreadRecord::BlockedReason::kInterrupt;
    default:
      FXL_NOTREACHED();
      return debug_ipc::ThreadRecord::BlockedReason::kNotBlocked;
  }
}

// Reads a null-terminated string from the given address of the given process.
zx_status_t ReadNullTerminatedString(const zx::process& process, zx_vaddr_t vaddr,
                                     std::string* dest) {
  // Max size of string we'll load as a sanity check.
  constexpr size_t kMaxString = 32768;

  dest->clear();

  constexpr size_t kBlockSize = 256;
  char block[kBlockSize];
  while (dest->size() < kMaxString) {
    size_t num_read = 0;
    zx_status_t status = process.read_memory(vaddr, block, kBlockSize, &num_read);
    if (status != ZX_OK)
      return status;

    for (size_t i = 0; i < num_read; i++) {
      if (block[i] == 0)
        return ZX_OK;
      dest->push_back(block[i]);
    }

    if (num_read < kBlockSize)
      return ZX_OK;  // Partial read: hit the mapped memory boundary.
    vaddr += kBlockSize;
  }
  return ZX_OK;
}

}  // namespace

zx_status_t GetProcessInfo(zx_handle_t process, zx_info_process* info) {
  return zx_object_get_info(process, ZX_INFO_PROCESS, info, sizeof(zx_info_process), nullptr,
                            nullptr);
}

zx_status_t GetModulesForProcess(const zx::process& process, uint64_t dl_debug_addr,
                                 std::vector<debug_ipc::Module>* modules) {
  return WalkModules(
      process, dl_debug_addr, [modules](const zx::process& process, uint64_t base, uint64_t lmap) {
        debug_ipc::Module module;
        module.base = base;
        module.debug_address = lmap;

        uint64_t str_addr;
        size_t num_read;
        if (process.read_memory(lmap + offsetof(link_map, l_name), &str_addr, sizeof(str_addr),
                                &num_read) != ZX_OK)
          return false;

        if (ReadNullTerminatedString(process, str_addr, &module.name) != ZX_OK)
          return false;

        auto elf = elflib::ElfLib::Create([&process, base = module.base](
                                              uint64_t offset, std::vector<uint8_t>* buf) {
          size_t num_read = 0;

          if (process.read_memory(base + offset, buf->data(), buf->size(), &num_read) != ZX_OK) {
            return false;
          }

          return num_read == buf->size();
        });

        if (elf) {
          module.build_id = elf->GetGNUBuildID();
        }

        modules->push_back(std::move(module));
        return true;
      });
}

std::vector<zx_info_maps_t> GetProcessMaps(const zx::process& process) {
  const size_t kRegionsCountGuess = 64u;
  const size_t kNewRegionsCountGuess = 4u;

  size_t count_guess = kRegionsCountGuess;

  std::vector<zx_info_maps_t> map;
  size_t actual;
  size_t avail;

  while (true) {
    map.resize(count_guess);

    zx_status_t status = process.get_info(ZX_INFO_PROCESS_MAPS, &map[0],
                                          sizeof(zx_info_maps) * map.size(), &actual, &avail);

    if (status != ZX_OK) {
      fprintf(stderr, "error %d for zx_object_get_info\n", status);
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

bool ReadProcessMemoryBlock(const zx::process& process, uint64_t address, uint32_t size,
                            debug_ipc::MemoryBlock* block) {
  block->address = address;
  block->size = size;
  block->data.resize(size);

  size_t bytes_read = 0;
  if (process.read_memory(address, &block->data[0], block->size, &bytes_read) == ZX_OK &&
      bytes_read == size) {
    block->valid = true;
    return true;
  }
  block->valid = false;
  block->data.resize(0);
  return false;
}

void ReadProcessMemoryBlocks(const zx::process& process, uint64_t address, uint32_t size,
                             std::vector<debug_ipc::MemoryBlock>* blocks) {
  // Optimistically assume the read will work which will be faster in the
  // common case.
  blocks->resize(1);
  if (ReadProcessMemoryBlock(process, address, size, &(*blocks)[0]))
    return;

  // Failure reading, this memory is either not mapped or it may cross mapping
  // boundaries. To solve the multiple boundary problem, get the memory mapping
  // and compute all mapping boundaries in the requested region. Then try to
  // read each of the resulting blocks (which may be valid or invalid).
  //
  // This computed boundaries array will contain all boundaries (including the
  // end address and some duplicates) except the begin address (this will be
  // implicit in the later computation).
  std::vector<uint64_t> boundaries;
  for (const zx_info_maps_t& map : GetProcessMaps(process)) {
    // The returned maps should be sorted so any mapping region starting past
    // our region means all relevant boundaries have been found.
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

  blocks->clear();
  uint64_t begin = address;
  for (uint64_t end : boundaries) {
    // There will be some duplicates in the boundaries array so skip anything
    // that's empty. These duplicates are caused by a range which a child
    // inside it that is coincident with one of the parent boundaries, or
    // two regions that abut each other.
    if (end == begin)
      continue;
    blocks->emplace_back();
    ReadProcessMemoryBlock(process, begin, static_cast<uint32_t>(end - begin), &blocks->back());
    begin = end;
  }
}

debug_ipc::ThreadRecord::State ThreadStateToEnums(
    uint32_t state, debug_ipc::ThreadRecord::BlockedReason* blocked_reason) {
  struct Mapping {
    uint32_t int_state;
    debug_ipc::ThreadRecord::State enum_state;
  };
  static const Mapping mappings[] = {
      {ZX_THREAD_STATE_NEW, debug_ipc::ThreadRecord::State::kNew},
      {ZX_THREAD_STATE_RUNNING, debug_ipc::ThreadRecord::State::kRunning},
      {ZX_THREAD_STATE_SUSPENDED, debug_ipc::ThreadRecord::State::kSuspended},
      {ZX_THREAD_STATE_BLOCKED, debug_ipc::ThreadRecord::State::kBlocked},
      {ZX_THREAD_STATE_DYING, debug_ipc::ThreadRecord::State::kDying},
      {ZX_THREAD_STATE_DEAD, debug_ipc::ThreadRecord::State::kDead}};

  const uint32_t basic_state = ZX_THREAD_STATE_BASIC(state);
  *blocked_reason = debug_ipc::ThreadRecord::BlockedReason::kNotBlocked;

  for (const Mapping& mapping : mappings) {
    if (mapping.int_state == basic_state) {
      if (mapping.enum_state == debug_ipc::ThreadRecord::State::kBlocked) {
        *blocked_reason = ThreadStateBlockedReasonToEnum(state);
      }
      return mapping.enum_state;
    }
  }
  FXL_NOTREACHED();
  return debug_ipc::ThreadRecord::State::kDead;
}

}  // namespace debug_agent
