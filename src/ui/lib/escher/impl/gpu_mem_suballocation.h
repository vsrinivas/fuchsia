// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_GPU_MEM_SUBALLOCATION_H_
#define SRC_UI_LIB_ESCHER_IMPL_GPU_MEM_SUBALLOCATION_H_

#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace escher {
namespace impl {

// Helper class for GpuMem::Suballocate(), which returns an instance of this
// class. When this instance is destroyed, it releases its strong reference on
// the backing memory object.
class GpuMemSuballocation final : public GpuMem {
 private:
  friend class ::escher::GpuMem;
  GpuMemSuballocation(GpuMemPtr mem, vk::DeviceSize size,
                      vk::DeviceSize offset);

  // The memory that this was sub-allocated from.
  GpuMemPtr mem_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_GPU_MEM_SUBALLOCATION_H_
