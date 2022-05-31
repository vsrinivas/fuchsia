// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//
#include <stdlib.h>

#include "common/vk/assert.h"
#include "radix_sort_vk_bench.h"

//
// See `rs_usage()` for parameter ordering.
//
int
main(int argc, char const * argv[])
{
  //
  // Parse arguments.
  //
  struct radix_sort_vk_bench_info info;  // no need to zero-initialize

  radix_sort_vk_bench_parse(argc, argv, &info);

  //
  // Execute benchmark.
  //
  int const result = radix_sort_vk_bench(&info);

  return result;
}

//
//
//
