// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_CHECK_VULKAN_SUPPORT_H_
#define SRC_UI_LIB_ESCHER_UTIL_CHECK_VULKAN_SUPPORT_H_

namespace escher {

// This attempts to create a VkInstance and then a VkDevice, and returns true
// if successful and false otherwise.  These are both destroyed before the
// function returns.  Therefore, this shouldn't be called from production code
// where fast startup time is an issue.
bool VulkanIsSupported();

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_CHECK_VULKAN_SUPPORT_H_
