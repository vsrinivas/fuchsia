// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_SHARED_MEMORY_H_
#define SRC_SYS_FUZZING_COMMON_SHARED_MEMORY_H_

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"

namespace fuzzing {

// This class can be used to share VMOs mapped into multiple processes.
//
// For example, to share a fixed length data, one process may create an object using:
//   SharedMemory sender;
//   sender.Mirror(data, size);
//
// To share variable-length data, |Mirror| can be replaced with |Reserve|, e.g.:
//   SharedMemory sender;
//   sender.Reserve(capacity);
//
// In either case, the process can created a shared VMO from the object:
//   zx::vmo vmo;
//   sender.Share(&vmo);
//
// It can then send it to another process via FIDL, which can link it:
//   SharedMemory receiver;
//   receiver.Link(std::move(vmo));
//
// At this point, the sender can |Update| the fixed length data, or |Write| to the variable length
// data. In the latter case, the receiver needs to |Read| before accessing the data with |data| and
// |size|. Reading and writing this size is not guaranteed to be atomic, so callers should use some
// other method to coordinate when the size changes, e.g. with an |AsyncEventPair|.
//
class SharedMemory final {
 public:
  SharedMemory() = default;
  SharedMemory(SharedMemory&& other) { *this = std::move(other); }
  SharedMemory& operator=(SharedMemory&& other) noexcept;
  ~SharedMemory();

  uint8_t* data() { return data_; }
  size_t size() { return size_; }

  // Resets this object, then creates a VMO of at least |capacity| bytes, maps it. The size of the
  // shared memory is recorded in the buffer itself, making it compatible with |Resize| and |Write|.
  __WARN_UNUSED_RESULT zx_status_t Reserve(size_t capacity);

  // Resets and configures the object so subsequent calls to |Update| copy the region of memory
  // described by |data| and |size|. This region of memory MUST remain valid until this object is
  // destroyed or reset. The primary use of this method is to share compiler-provided
  // instrumentation across processes.
  __WARN_UNUSED_RESULT zx_status_t Mirror(void* data, size_t size);

  // Returns a buffer containing a duplicate of the VMO backing this memory region via |out|,
  // suitable for sending to another process. The size is written to the ZX_PROP_VMO_CONTENT_SIZE
  // property of the VMO, as in |fuchsia.debugdata.Publisher|.
  __WARN_UNUSED_RESULT zx_status_t Share(zx::vmo* out) const;

  // Resets this object, then takes ownership of the |vmo| and maps it. The VMO must have been
  // |Share|d from an object that was |Reserve|d or |Mirror|ed.
  __WARN_UNUSED_RESULT zx_status_t Link(zx::vmo vmo);

  // Refreshes the valid region of the mapped memory. Callers should use this method before calling
  // |size()|.
  __WARN_UNUSED_RESULT zx_status_t Read();

  // Replaces the contents of the VMO with the given |data|.
  __WARN_UNUSED_RESULT zx_status_t Write(const void* data, size_t size);

  // Copies data from the mirrored memory region into this object. Must only be called on an object
  // that was |Mirror|ed.
  void Update();

  // Zeros the |Mirror|ed memory, if any.
  void Clear();

 private:
  // Unmaps the VMO (if mapped) and closes the VMO handle.
  void Reset();

  // Creates a new VMO with at least the given |capacity|.
  zx_status_t Create(size_t capacity);

  // Maps the current VMO into this process' address space.
  zx_status_t Map();

  // Sets the size for the valid region of the mapped memory.
  zx_status_t Resize(size_t size);

  // If AddressSanitizer is available, this will unpoison memory up to the first ASAN alignment
  // boundary following the first |size| bytes. See also
  // https://github.com/google/sanitizers/wiki/AddressSanitizerManualPoisoning.
  void Unpoison(size_t size);

  // The mapped VMO backing this object.
  zx::vmo vmo_;

  // Memory region published by |Mirror|ed objects.
  void* mirror_ = nullptr;

  // Memory region shared by the mapped VMO.
  uint8_t* data_ = 0;

  // Size of the mapped region that is valid.
  size_t size_ = 0;

  // Total size of the VMO mapped into memory.
  size_t mapped_size_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(SharedMemory);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_SHARED_MEMORY_H_
