// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_PHYS_MEM_H_
#define GARNET_LIB_MACHINA_PHYS_MEM_H_

#include <zx/vmo.h>
#include <type_traits>

#include "garnet/public/lib/fxl/logging.h"

namespace machina {

class PhysMem {
 public:
  zx_status_t Init(size_t mem_size);

  ~PhysMem();

  const zx::vmo& vmo() const { return vmo_; }
  uintptr_t addr() const { return addr_; }
  size_t size() const { return vmo_size_; }

  template <typename T>
  T* as(uintptr_t off) const {
    FXL_DCHECK(off + sizeof(T) <= vmo_size_)
        << "Region is outside of guest physical memory";
    return reinterpret_cast<T*>(addr_ + off);
  }

  void* ptr(uintptr_t off, size_t len) const {
    FXL_DCHECK(off + len <= vmo_size_)
        << "Region is outside of guest physical memory";
    return reinterpret_cast<void*>(addr_ + off);
  }

 protected:
  zx::vmo vmo_;
  size_t vmo_size_ = 0;
  uintptr_t addr_ = 0;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_PHYS_MEM_H_
