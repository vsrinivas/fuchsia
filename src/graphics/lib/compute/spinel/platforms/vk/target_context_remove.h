// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan.h>

//
//
//

#include "allocator_host.h"

//
//
//

struct spn_vk_context
{
  VkDevice                       device;
  VkAllocationCallbacks  const * allocator;

  struct spn_allocator_host_perm host_perm;
};

//
//
//
