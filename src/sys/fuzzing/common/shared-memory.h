// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_SHARED_MEMORY_H_
#define SRC_SYS_FUZZING_COMMON_SHARED_MEMORY_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include "src/lib/fxl/macros.h"

namespace fuzzing {
namespace {

using ::fuchsia::mem::Buffer;

}  // namespace

// This class can be used to share VMOs mapped into multiple processes. For example, one process
// may create a |fuchsia.mem.Buffer| with a certain capacity using:
//   SharedMemory shmem;
//   fuchsia.mem.Buffer buffer;
//   shmem.Create(capacity, &buffer);
//
// It can then send it to another process via FIDL, which can link it:
//   SharedMemory shmem;
//   shmem.Link(buffer);
//
// This buffer can be used to share fixed-length data, e.g. coverage data.
//
// For variable-length data, both callers should set the optional |inline_size| parameter to true,
// e.g.
//   shmem.Create(capacity, &buffer, /* inline_size= */ true);
// and
//   shmem.Link(buffer, /* inline_size= */ true);
//
// This will allocate |sizeof(uint64_t)| additional bytes to store the size of valid data in the
// VMO. This size can be updated using |write| or |Clear| and retrieved with |size|, allowing
// callers to send or receive variable-length data. Reading and writing this size is not guaranteed
// to be atomic, so callers should use some other method to coordinate when the size changes, e.g.
// with a SignalCoordinator.
//
class SharedMemory final {
 public:
  SharedMemory() = default;
  SharedMemory(SharedMemory&& other) { *this = std::move(other); }
  SharedMemory& operator=(SharedMemory&& other) noexcept;
  ~SharedMemory();

  const zx::vmo& vmo() const { return vmo_; }
  zx_vaddr_t addr() const { return mapped_addr_; }
  size_t capacity() const { return mapped_size_ - (header_ ? sizeof(InlineHeader) : 0); }
  bool is_mapped() const { return mapped_addr_ != 0; }
  uint8_t* data() { return data_; }
  size_t size();

  // Resets this object, then creates a VMO of at least |capacity| bytes, maps it. The size of the
  // shared memory is recorded in the buffer itself, making it compatible with |Resize| and |Write|.
  void Reserve(size_t capacity);

  // Resets and configures the object so subsequent calls to |Update| copy the region of memory
  // described by |data| and |size|. This region of memory MUST remain valid until this object is
  // destroyed or reset. The primary use of this method is to share compiler-provided
  // instrumentation across processes.
  void Mirror(void* data, size_t size);

  // Returns a buffer containing a duplicate of the VMO backing this memory region, suitable for
  // sending to another process.
  Buffer Share();

  // Resets this object, then takes ownership of the VMO handle in |buffer| and maps it. The buffer
  // must have been |Share|d from an object that was |Reserve|d.
  void LinkReserved(Buffer&& buffer);

  // Resets this object, then takes ownership of the VMO handle in |buffer| and maps it. The buffer
  // must have been |Share|d from an object that was |Mirror|ed.
  void LinkMirrored(Buffer&& buffer);

  // If |enable|d and AddressSanitizer is available, the object will poison the mapped memory beyond
  // |end()| whenever |size()| changes. If AddressSanitizer is not available, this method has no
  // effect. See also https://github.com/google/sanitizers/wiki/AddressSanitizerManualPoisoning.
  void SetPoisoning(bool enable);

  // Modifies the amount of the buffer considered valid. Must only be called on objects that have
  // been |Reserve|d or |LinkReserved|.
  void Resize(size_t size);

  // Appends data to the VMO.
  void Write(uint8_t one_byte);
  void Write(const void* data, size_t size);

  // Copies data from the mirrored memory region into this object. Must only be called on an object
  // that was |Mirror|ed.
  void Update();

  // Resizes |Reserve|d objects to 0, and zeros |Mirror|ed objects' mirrored memory.
  void Clear();

 private:
  // If |Create| is called with |inline_size = true|, buffer starts with an inline header.
  struct InlineHeader {
    char magic[8];
    uint64_t size;
  };

  // Unmaps the VMO (if mapped) and closes the VMO handle.
  void Reset();

  // If AddressSanitizer is available and the caller chose to |SetPoisoning|, ensures that the first
  // |size| bytes are not poisoned. Bytes following the next ASAN alignment boundary (typically 8
  // bytes) after |size| will be poisoned. If |size| is greater than or equal to the capacity, the
  // entire buffer will be unpoisoned.
  void PoisonAfter(size_t size);

  // Creates a new VMO with at least the given capacity.
  void Create(size_t capacity);

  // Maps the current VMO into this process' address space. Must not be currently mapped.
  void Map();

  // Takes ownership of the buffer's VMO and maps it.
  void Map(Buffer&& buffer);

  // The mapped VMO backing this object.
  zx::vmo vmo_;
  zx_vaddr_t mapped_addr_ = 0;
  size_t mapped_size_ = 0;

  // Describes the accessible shared memory.
  uint8_t* data_ = 0;
  size_t size_ = 0;

  // Inline header for |Reserve|d objects.
  InlineHeader* header_ = nullptr;

  // Memory region published by |Mirror|ed objects.
  void* mirror_ = nullptr;

  // Tracks poisoned memory. See |SetPoisoning|.
  bool poisoning_ = false;
  size_t unpoisoned_size_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(SharedMemory);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_SHARED_MEMORY_H_
