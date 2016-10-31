// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_mem.h"

#include "escher/impl/gpu_allocator.h"

namespace escher {
namespace impl {

GpuMem::GpuMem(GpuMemSlab* slab, vk::DeviceSize offset, vk::DeviceSize size)
    : slab_(slab), offset_(offset), size_(size) {
  slab_->AddRef();
}

GpuMem::~GpuMem() {
  slab_->FreeMem(offset_, size_);
}

}  // namespace impl
}  // namespace escher
