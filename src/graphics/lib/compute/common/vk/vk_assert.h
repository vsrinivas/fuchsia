// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan.h>
#include <stdbool.h>

//
//
//

char const *
vk_get_result_string(VkResult const result);

VkResult
vk_assert(VkResult     const result,
          char const * const file,
          int          const line,
          bool         const is_abort);

//
//
//

#define vk(...)    vk_assert((vk##__VA_ARGS__), __FILE__, __LINE__, true);
#define vk_ok(err) vk_assert(err,               __FILE__, __LINE__, true);

//
//
//
