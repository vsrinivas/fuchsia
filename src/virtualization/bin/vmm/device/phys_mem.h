// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_

#include <lib/zx/vmo.h>

#include "src/lib/syslog/cpp/logger.h"

class PhysMem {
 public:
  zx_status_t Init(zx::vmo vmo);

  ~PhysMem();

  const zx::vmo& vmo() const { return vmo_; }
  size_t size() const { return vmo_size_; }

  template <typename T>
  T* as(zx_vaddr_t off, size_t len = sizeof(T)) const {
    FX_CHECK(off + len >= off && off + len <= vmo_size_)
        << "Region is outside of guest physical memory";
    return reinterpret_cast<T*>(addr_ + off);
  }

  template <typename T>
  zx_vaddr_t offset(T* ptr, size_t len = sizeof(T)) const {
    zx_vaddr_t off = reinterpret_cast<zx_vaddr_t>(ptr);
    FX_CHECK(off + len >= off && off + len >= addr_ && (off + len - addr_ <= vmo_size_))
        << "Pointer is not contained within guest physical memory";
    return off - addr_;
  }

 protected:
  zx::vmo vmo_;
  size_t vmo_size_ = 0;
  zx_vaddr_t addr_ = 0;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_PHYS_MEM_H_
