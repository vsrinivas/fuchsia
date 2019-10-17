// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_FIND_VALIDATION_LAYER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_FIND_VALIDATION_LAYER_H_

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

const char *
vk_find_validation_layer();

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_VK_FIND_VALIDATION_LAYER_H_
