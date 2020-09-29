// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vulkan/vulkan.h>

#define __EXPORT __attribute__((__visibility__("default")))

typedef VkResult(VKAPI_PTR* PFN_vkOpenInNamespaceAddr)(const char* pName, uint32_t handle);

//
// These are the four entry points that may be exported by a Vulkan ICD.
//

__EXPORT VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
  return VK_SUCCESS;
}

__EXPORT void vk_icdGetInstanceProcAddr(VkInstance instance, const char* name) {}

__EXPORT void vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* name) {}

__EXPORT void vk_icdInitializeOpenInNamespaceCallback(PFN_vkOpenInNamespaceAddr callback) {}
