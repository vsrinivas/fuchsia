// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/third_party/libunwindstack/fuchsia/MemoryFuchsia.h"

#include <errno.h>
#include <zircon/syscalls.h>

namespace unwindstack {

MemoryFuchsia::MemoryFuchsia(zx_handle_t process) : process_(process) {}

size_t MemoryFuchsia::Read(uint64_t addr, void* dst, size_t size) {
  size_t actual = 0;
  zx_status_t status =
      zx_process_read_memory(process_, addr, dst, size, &actual);
  if (status != ZX_OK) {
    // Calling code expects errno to be set on failure.
    errno = EFAULT;
    return 0;
  }
  return actual;
}

// Memory and MemoryRange ------------------------------------------------------
//
// This is the implementation of MemoryRange and the required parts of Memory
// from the Memory.cpp file. By duplicating this simple code, we can avoid
// forking that large Android file. It should be kept in sync with that
// version.
//
// TODO(brettw) factor MemoryRange out so we can have a different Memory
// implementation while using the shared MemoryRange implementation.

bool Memory::ReadFully(uint64_t addr, void* dst, size_t size) {
  size_t rc = Read(addr, dst, size);
  return rc == size;
}

bool Memory::ReadString(uint64_t addr, std::string* string, uint64_t max_read) {
  string->clear();
  uint64_t bytes_read = 0;
  while (bytes_read < max_read) {
    uint8_t value;
    if (!ReadFully(addr, &value, sizeof(value))) {
      return false;
    }
    if (value == '\0') {
      return true;
    }
    string->push_back(value);
    addr++;
    bytes_read++;
  }
  return false;
}

MemoryRange::MemoryRange(const std::shared_ptr<Memory>& memory, uint64_t begin,
                         uint64_t length, uint64_t offset)
    : memory_(memory), begin_(begin), length_(length), offset_(offset) {}

size_t MemoryRange::Read(uint64_t addr, void* dst, size_t size) {
  if (addr < offset_) {
    return 0;
  }

  uint64_t read_offset = addr - offset_;
  if (read_offset >= length_) {
    return 0;
  }

  uint64_t read_length =
      std::min(static_cast<uint64_t>(size), length_ - read_offset);
  uint64_t read_addr;
  if (__builtin_add_overflow(read_offset, begin_, &read_addr)) {
    return 0;
  }

  return memory_->Read(read_addr, dst, read_length);
}

void MemoryRanges::Insert(MemoryRange* memory) {
  maps_.emplace(memory->offset() + memory->length(), memory);
}

size_t MemoryRanges::Read(uint64_t addr, void* dst, size_t size) {
  auto entry = maps_.upper_bound(addr);
  if (entry != maps_.end()) {
    return entry->second->Read(addr, dst, size);
  }
  return 0;
}

}  // namespace unwindstack
