// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_VMO_SHARED_VMO_H_
#define LIB_MTL_VMO_SHARED_VMO_H_

#include <mutex>

#include <mx/vmo.h>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace mtl {

// Holds a reference to a shared VMO which may be memory mapped lazily.
// Once memory-mapped, the VMO remains mapped until all references to this
// object have been released.
//
// This object is thread-safe.
class SharedVmo : public ftl::RefCountedThreadSafe<SharedVmo> {
 public:
  // Initializes a shared VMO.
  //
  // |vmo| must be a valid VMO handle.
  // If not zero, |map_flags| specifies the flags which should be passed to
  // |mx::process::map_vm| when the VMO is mapped.
  explicit SharedVmo(mx::vmo vmo, uint32_t map_flags = 0u);

  virtual ~SharedVmo();

  // Gets the underlying VMO.
  const mx::vmo& vmo() const { return vmo_; }

  // Gets the size of the VMO.
  size_t vmo_size() const { return vmo_size_; }

  // Gets the flags used for mapping the VMO.
  uint32_t map_flags() const { return map_flags_; }

  // Maps the entire VMO into memory (if not already mapped).
  // Returns the address of the mapping or nullptr if an error occurred.
  void* Map();

 private:
  mx::vmo const vmo_;
  uint32_t const map_flags_;
  size_t vmo_size_;

  std::once_flag mapping_once_flag_{};
  uintptr_t mapping_ = 0u;

  FRIEND_REF_COUNTED_THREAD_SAFE(SharedVmo);
  FTL_DISALLOW_COPY_AND_ASSIGN(SharedVmo);
};

}  // namespace mozart

#endif  // LIB_MTL_VMO_SHARED_VMO_H_
