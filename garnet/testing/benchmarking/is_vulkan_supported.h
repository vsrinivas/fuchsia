// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_TESTING_BENCHMARKING_IS_VULKAN_SUPPORTED_H_
#define GARNET_TESTING_BENCHMARKING_IS_VULKAN_SUPPORTED_H_

namespace benchmarking {

// Determine whether Vulkan is supported or not by subprocessing
// `vulkan_is_supported`.
bool IsVulkanSupported();

}  // namespace benchmarking

#endif  // GARNET_TESTING_BENCHMARKING_IS_VULKAN_SUPPORTED_H_