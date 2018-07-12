// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_UNIFORM_ALLOCATION_H_
#define LIB_ESCHER_RENDERER_UNIFORM_ALLOCATION_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/forward_declarations.h"

namespace escher {

// Represents a sub-allocation from within a Vulkan uniform buffer.  The valid
// lifetime of this allocation is defined by the allocator that this is obtained
// from.
struct UniformAllocation {
  Buffer* buffer;
  vk::DeviceSize offset;
  vk::DeviceSize size;
  // Host-accessible pointer to offset region of the buffer's memory.
  void* host_ptr;

  // Convenient way to refer to the host-accessible memory as a typed reference.
  template <typename T>
  T& as_ref() {
    FXL_DCHECK(size >= sizeof(T));
    return *static_cast<T*>(host_ptr);
  }
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_UNIFORM_ALLOCATION_H_
