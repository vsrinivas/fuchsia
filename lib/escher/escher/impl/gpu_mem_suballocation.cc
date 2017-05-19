// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/gpu_mem_suballocation.h"

namespace escher {
namespace impl {

GpuMemSuballocation::GpuMemSuballocation(GpuMemPtr mem,
                                         vk::DeviceSize offset,
                                         vk::DeviceSize size)
    : GpuMem(mem->base(), mem->offset() + offset, size), mem_(std::move(mem)) {}

GpuMemSuballocation::~GpuMemSuballocation() {
  mem_->OnAllocationDestroyed(size(), offset());
}

}  // namespace impl
}  // namespace escher
