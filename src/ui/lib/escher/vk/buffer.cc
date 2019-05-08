// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/buffer.h"

namespace escher {

const ResourceTypeInfo Buffer::kTypeInfo("Buffer", ResourceType::kResource,
                                         ResourceType::kWaitableResource,
                                         ResourceType::kBuffer);

Buffer::Buffer(ResourceManager* manager, vk::Buffer buffer, vk::DeviceSize size,
               uint8_t* host_ptr)
    : WaitableResource(manager),
      buffer_(buffer),
      size_(size),
      host_ptr_(host_ptr) {}

}  // namespace escher
