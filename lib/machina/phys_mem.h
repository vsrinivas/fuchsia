// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_PHYS_MEM_H_
#define GARNET_LIB_MACHINA_PHYS_MEM_H_

#include <zircon/types.h>
#include <zx/vmo.h>

namespace machina {

class PhysMem {
 public:
  zx_status_t Init(size_t mem_size);

  ~PhysMem();

  zx_handle_t vmo() const { return vmo_.get(); }
  uintptr_t addr() const { return addr_; }
  size_t size() const { return vmo_size_; }

 protected:
  zx::vmo vmo_;
  size_t vmo_size_ = 0;
  uintptr_t addr_ = 0;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_PHYS_MEM_H_
