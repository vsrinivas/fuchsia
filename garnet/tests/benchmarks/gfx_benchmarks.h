// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_TESTS_BENCHMARKS_GFX_BENCHMARKS_H_
#define GARNET_TESTS_BENCHMARKS_GFX_BENCHMARKS_H_

#include "garnet/testing/benchmarking/benchmarking.h"

// Determine whether Vulkan is supported or not by subprocessing
// `vulkan_is_supported`.
bool IsVulkanSupported();

// Add all garnet graphics benchmarks to |benchmarks_runner|.
void AddGraphicsBenchmarks(benchmarking::BenchmarksRunner* benchmarks_runner);

#endif  // GARNET_TESTS_BENCHMARKS_GFX_BENCHMARKS_H_
