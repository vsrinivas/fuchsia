// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// vk_mem_alloc.h is a header-only implementation of a memory allocator. In
// order to generate the code for the class, the header must be included with
// the following macro defined.
#define VMA_IMPLEMENTATION
#include "src/ui/lib/escher/third_party/VulkanMemoryAllocator/vk_mem_alloc.h"
#undef VMA_IMPLEMENTATION
