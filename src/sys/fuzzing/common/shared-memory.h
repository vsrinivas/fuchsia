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
  SharedMemory& operator=(SharedMemory&& other);
  ~SharedMemory();

  const zx::vmo& vmo() const { return vmo_; }
  zx_vaddr_t addr() const { return addr_; }
  size_t capacity() const { return capacity_; }
  bool is_mapped() const { return addr_ != 0; }

  // Describes the memory region, e.g. like inline 8-bit counters (uint8_t) for
  // __sanitizer_cov_inline_8bit_counters_init or PC tables (uintptr_t) for
  // __sanitizer_cov_pc_tables_init.
  template <typename T = void>
  T* begin() const {
    return reinterpret_cast<T*>(Begin());
  }
  template <typename T = void>
  T* end() const {
    return reinterpret_cast<T*>(End());
  }

  // Describes the memory region like a fuzzer test input, e.g. for LLVMFuzzerTestOneInput.
  uint8_t* data() const { return begin<uint8_t>(); }
  size_t size() const { return GetSize(); }

  // Resets this object, then creates a VMO of at least |capacity| bytes, maps it, and returns a
  // duplicate handle in |out|. If |inline_size| is true, this object can be used to send or receive
  // variable-length data as described in the class description.
  void Create(size_t capacity, Buffer* out, bool inline_size = false);

  // Like |Create|, but determines the capacity and initial contents automatically from the memory
  // region described by |begin| and |end|. |begin| MUST be less than |end|, i.e. the region must be
  // valid and non-empty. This method cannot be used to send variable-length data. The pointers are
  // saved and used by |Update|; they MUST remain valid until |Reset| is called.
  void Share(const void* begin, const void* end, Buffer* out);

  // Resets this object, then takes ownership of the VMO handle in |buffer| and maps it. If
  // |inline_size| is true, this object can be used to send or receive variable-length data as
  // described in the class description.
  void Link(Buffer buffer, bool inline_size = false);

  // Writes data to the VMO. If unmapped, returns ZX_ERR_BAD_STATE. If the data is truncated due to
  // insufficient remaining capacity, writes as much as it can and returns ZX_ERR_BUFFER_TOO_SMALL.
  zx_status_t Write(const void* data, size_t size);

  // If this object was |Share|d, copies the data from the original memory region to this objects
  // shared memory; otherwise, does nothing.
  void Update();

  // Sets the amount valid date to 0.
  void Clear();

  // Unmaps and resets the VMO if mapped.
  void Reset();

 private:
  // Manages the (possibly inlined) size.
  size_t GetSize() const;
  void SetSize(size_t size);

  // Gets memory region pointers, accounting for the possibly inlined header.
  void* Begin() const;
  void* End() const;

  zx::vmo vmo_;
  zx_vaddr_t addr_ = 0;
  size_t capacity_ = 0;
  const void* source_ = nullptr;
  size_t size_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(SharedMemory);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_SHARED_MEMORY_H_
