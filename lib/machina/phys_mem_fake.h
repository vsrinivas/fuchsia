// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_PHYS_MEM_FAKE_H_
#define GARNET_LIB_MACHINA_PHYS_MEM_FAKE_H_

#include <hypervisor/phys_mem.h>

class PhysMemFake : public PhysMem {
 public:
  PhysMemFake() {
    vmo_size_ = SIZE_MAX;
    addr_ = 0;
  }

  PhysMemFake(uintptr_t addr, size_t size) {
    vmo_size_ = size;
    addr_ = addr;
  }
};

#endif  // GARNET_LIB_MACHINA_PHYS_MEM_FAKE_H_
