// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_UNIFORM_ALLOCATION_H_
#define SRC_UI_LIB_ESCHER_RENDERER_UNIFORM_ALLOCATION_H_

#include <vulkan/vulkan.hpp>

#include "src/ui/lib/escher/forward_declarations.h"

namespace escher {

// Represents a sub-allocation from within a Vulkan uniform buffer.  The valid
// lifetime of this allocation is defined by the allocator that this is obtained
// from.
struct UniformAllocation {
  Buffer* buffer = nullptr;
  vk::DeviceSize offset = 0;
  vk::DeviceSize size = 0;
  // Host-accessible pointer to offset region of the buffer's memory.
  void* host_ptr = nullptr;

  // Convenient way to refer to the host-accessible memory as a typed reference.
  template <typename T>
  T& as_ref() {
    FXL_DCHECK(size >= sizeof(T));
    return *static_cast<T*>(host_ptr);
  }
  // Convenient way to refer to the host-accessible memory as a typed pointer.
  template <typename T>
  T* as_ptr() {
    FXL_DCHECK(size >= sizeof(T));
    return static_cast<T*>(host_ptr);
  }
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_UNIFORM_ALLOCATION_H_
