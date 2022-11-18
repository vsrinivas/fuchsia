
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf-search.h>
#include <lib/trace/event.h>
#include <zircon/sanitizer.h>

#include <iostream>
#include <strstream>

#include "src/performance/memory/profile/stack_compression.h"
#include "src/performance/memory/profile/trace_constants.h"

// Category used to publish trace records. This can be overridden for testing purpose.
extern const char* trace_category;
__attribute__((visibility("default"))) const char* trace_category = "memory_profile";

namespace {

// Limit to the stack size collected.
constexpr size_t kStackTraceMaximumDepth = 48;
constexpr size_t kStackTraceDiscardTop = 4;
constexpr size_t kStackTraceDiscardBottom = 4;

// Incremented fo each allocation and deallocation send. This is used as a
// unique trace record identifier. To be removed when fxb/111062 is fixed.
static std::atomic<uint64_t> trace_id{0};
// True when the the layout was sent and the trace is active, False otherwise.
static std::atomic<bool> memory_layout_sent{false};
// True when either the allocation or the deallocation hook is running.
static thread_local bool executing = false;

// Serializes the value to the output stream.
template <class T>
void WriteValue(std::ostream* os, T value) {
  os->write(reinterpret_cast<char*>(&value), sizeof(T));
}

// Serializes the value to the output stream.
void WriteValue(std::ostream* os, bool value) {
  const uint8_t v = value;
  WriteValue(os, v);
}

// Record the memory layout to the trace system.
// It is made of a set of record prefixed by an identifier char.
//
// Object:
//   uint8_t id: constant 'o' identifier byte for objects.
//   uint64_t size: length of the build id.
//   uint8_t[size] build_id: bytes composing the build id.
//
// Memory mapping:
//  uint8_t id: constant 'm' identifier byte for objects.
//  uint64_t starting_address: address of the fist byte of the region.
//  uint64_t size: size in bytes of the region of memory.
//  uint16_t module_index: based index of the module mapped in this region.
//  uint8_t readable: 1 when the range is readable, 0 otherwise.
//  uint8_t writable: 1 when the range is writable, 0 otherwise.
//  uint8_t executable: 1 when the range contains executable code, 0 otherwise.
//  uint64_t relative_addr: Module relative address. For ELF files the module
//      relative address will be the p_vaddr of the associated program header.
//      For example if your module's executable segment has p_vaddr=0x1000,
//      p_memsz=0x1234, and was loaded at 0x7acba69d5000 then you need to subtract
//      0x7acba69d4000 from any address between 0x7acba69d5000 and 0x7acba69d6234
//      to get the module relative address. The starting address will usually have
//      been rounded down to the active page size, and the size rounded up.
void send_memory_map_trace() {
  std::strstream blob;
  zx_handle_t process = zx_process_self();
  elf_search::ForEachModule(*zx::unowned_process{process},
                            [count = 0u, &blob](const elf_search::ModuleInfo& info) mutable {
                              const size_t kPageSize = zx_system_get_page_size();
                              unsigned int module_id = count++;
                              blob.put('o');
                              uint64_t size = info.build_id.size();
                              WriteValue(&blob, size);
                              for (auto c : info.build_id) {
                                blob.put(c);
                              }

                              // Now collect the various segments.
                              for (const auto& phdr : info.phdrs) {
                                if (phdr.p_type != PT_LOAD) {
                                  continue;
                                }
                                uintptr_t start = phdr.p_vaddr & -kPageSize;
                                uintptr_t end =
                                    (phdr.p_vaddr + phdr.p_memsz + kPageSize - 1) & -kPageSize;
                                uint64_t starting_address = info.vaddr + start;
                                uint64_t size = end - start;
                                uint16_t module_index = (uint16_t)module_id;
                                bool readable = !!(phdr.p_flags & PF_R);
                                bool writable = !!(phdr.p_flags & PF_W);
                                bool executable = !!(phdr.p_flags & PF_X);
                                uint64_t relative_addr = start;
                                blob.put('m');
                                WriteValue(&blob, starting_address);
                                WriteValue(&blob, size);
                                WriteValue(&blob, module_index);
                                WriteValue(&blob, readable);
                                WriteValue(&blob, writable);
                                WriteValue(&blob, executable);
                                WriteValue(&blob, relative_addr);
                              }
                            });

  TRACE_BLOB_EVENT(trace_category, LAYOUT, blob.str(), blob.pcount(), TRACE_ID,
                   TA_UINT64(trace_id++));
}

}  // namespace

extern "C" {

// Symbol is used by the scudo allocator to provide an optional hook for a
// callback that gets called on every allocation.
__attribute__((visibility("default"))) void __scudo_allocate_hook(void* ptr, unsigned int size) {
  if (executing) {
    return;
  }
  executing = true;
  if (TRACE_ENABLED() && TRACE_CATEGORY_ENABLED(trace_category)) {
    // The first time a trace is enabled, send the memory layout.
    // This is brittle as the trace can be enabled/disabled between two allocations.
    if (!memory_layout_sent.exchange(true)) {
      send_memory_map_trace();
    }
    uintptr_t pc[kStackTraceMaximumDepth];
    const size_t pc_size = __sanitizer_fast_backtrace(pc, sizeof(pc) / sizeof(pc[0]));
    uint8_t blob_buffer[kStackTraceMaximumDepth * 9];
    auto blob = (pc_size > kStackTraceDiscardBottom + kStackTraceDiscardTop)
                    ? compress({pc + kStackTraceDiscardTop,
                                pc_size - kStackTraceDiscardBottom - kStackTraceDiscardTop},
                               blob_buffer)
                    : cpp20::span<uint8_t>(blob_buffer, 0);

    TRACE_BLOB_EVENT(trace_category, ALLOC, blob.data(), blob.size(), ADDR,
                     TA_UINT64(reinterpret_cast<uint64_t>(ptr)), SIZE, TA_UINT64(size), TRACE_ID,
                     TA_UINT64(trace_id++));
  } else {
    // Next time a session is started, the layout has to be sent.
    memory_layout_sent.store(false);
  }
  executing = false;
}

// Symbol is used by the scudo allocator to provide an optional hook for a
// callback that gets called on every de-allocation.
__attribute__((visibility("default"))) void __scudo_deallocate_hook(void* ptr) {
  if (executing) {
    return;
  }
  executing = true;
  if (TRACE_ENABLED() && TRACE_CATEGORY_ENABLED(trace_category)) {
    TRACE_INSTANT(trace_category, DEALLOC, TRACE_SCOPE_THREAD, ADDR,
                  TA_UINT64(reinterpret_cast<uint64_t>(ptr)), TRACE_ID, TA_UINT64(trace_id++));
  } else {
    memory_layout_sent.store(false);
  }
  executing = false;
}

}  // extern c
