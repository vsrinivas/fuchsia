// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_SHARED_MEMORY_H_
#define SRC_LIB_FUZZING_FIDL_SHARED_MEMORY_H_

#include <lib/fxl/macros.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

namespace fuzzing {

// This small utility class can be used to share VMOs mapped into multiple processes. For example,
// for a given |max_size|, a FIDL client of some service might do the following (error-checking
// omitted):
//
//   SharedMemory shmem;
//   shmem.Create(max_size);
//   fuchsia.mem.Buffer buffer;
//   shmem.Share(&buffer.vmo);
//
// The service might then do (again, error-checking omitted):
//
//   SharedMemory shmem;
//   shmem.Link(buffer.vmo);
//
class SharedMemory {
 public:
  SharedMemory();
  SharedMemory(SharedMemory &&other);
  virtual ~SharedMemory();

  bool is_mapped() const { return addr_ != 0; }
  const zx::vmo &vmo() const { return vmo_; }
  zx_vaddr_t addr() const { return addr_; }
  size_t len() const { return len_; }

  // Create a VMO of at least |len| bytes and map it. This method can be called at most once, and is
  // exclusive with |Link|.
  virtual zx_status_t Create(size_t len);

  // Duplicate the underlying VMO and return it via |out|. This is typically called after |Create|,
  // but it does not need to be called immediately. For example, the VMO may be created and mapped
  // early, and shared only later when a connection is established.
  virtual zx_status_t Share(zx::vmo *out_vmo);

  // Duplicates |vmo| and maps it. This method can be called at most once, and is exclusive with
  // |Create|. |len| must be less than or equal to the VMO's size.
  virtual zx_status_t Link(const zx::vmo &vmo, size_t len);

  // Unmaps and resets the VMO if mapped.
  void Reset();

 private:
  zx::vmo vmo_;
  zx_vaddr_t addr_;
  size_t len_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SharedMemory);
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_SHARED_MEMORY_H_
