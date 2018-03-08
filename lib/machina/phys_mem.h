// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_PHYS_MEM_H_
#define GARNET_LIB_MACHINA_PHYS_MEM_H_

#include <zx/vmo.h>
#include <type_traits>

#include "garnet/public/lib/fxl/logging.h"

namespace machina {

template <typename T>
struct type_size {
  static constexpr size_t value = sizeof(T);
};

template <>
struct type_size<void> {
  static constexpr size_t value = 1;
};

class PhysMem {
 public:
  zx_status_t Init(size_t mem_size);

  ~PhysMem();

  const zx::vmo& vmo() const { return vmo_; }
  uintptr_t addr() const { return addr_; }
  size_t size() const { return vmo_size_; }

  template <typename T>
  T* as(uintptr_t off) const {
    FXL_DCHECK(off + type_size<T>::value <= vmo_size_)
        << "Offset is outside of guest physical memory";
    return reinterpret_cast<T*>(addr_ + off);
  }

 protected:
  zx::vmo vmo_;
  size_t vmo_size_ = 0;
  uintptr_t addr_ = 0;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_PHYS_MEM_H_
