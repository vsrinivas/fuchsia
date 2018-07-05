// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_GPU_MEM_SUBALLOCATION_H_
#define LIB_ESCHER_IMPL_GPU_MEM_SUBALLOCATION_H_

#include "lib/escher/vk/gpu_mem.h"

namespace escher {
namespace impl {

// Helper class for GpuMem::Allocate(), which returns an instance of this class.
// When the instance is destroyed, it notifies the GpuMem that it was allocated
// from.
class GpuMemSuballocation final : public GpuMem {
 public:
  ~GpuMemSuballocation() override;

 private:
  friend class ::escher::GpuMem;
  GpuMemSuballocation(GpuMemPtr mem, vk::DeviceSize size,
                      vk::DeviceSize offset);

  // The memory that this was sub-allocated from.
  GpuMemPtr mem_;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_GPU_MEM_SUBALLOCATION_H_
