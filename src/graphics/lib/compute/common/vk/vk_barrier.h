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

void
vk_barrier_compute_w_to_compute_r(VkCommandBuffer cb);

void
vk_barrier_compute_w_to_transfer_r(VkCommandBuffer cb);

//
//
//
