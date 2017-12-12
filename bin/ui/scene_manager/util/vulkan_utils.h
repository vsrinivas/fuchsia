// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_UTIL_VULKAN_UTILS_H_
#define GARNET_BIN_UI_SCENE_MANAGER_UTIL_VULKAN_UTILS_H_

#include <vulkan/vulkan.hpp>

namespace scene_manager {

vk::SurfaceKHR CreateVulkanMagmaSurface(const vk::Instance& instance);

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_UTIL_VULKAN_UTILS_H_
