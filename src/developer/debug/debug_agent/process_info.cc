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
#include <lib/syslog/cpp/macros.h>

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
  FX_DCHECK(ZX_THREAD_STATE_BASIC(state) == ZX_THREAD_STATE_BLOCKED);

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
    case ZX_THREAD_STATE_BLOCKED_PAGER:
      return debug_ipc::ThreadRecord::BlockedReason::kPager;
    default:
      FX_NOTREACHED();
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
  FX_NOTREACHED();
  return debug_ipc::ThreadRecord::State::kDead;
}

}  // namespace debug_agent
