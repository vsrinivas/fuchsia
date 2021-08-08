// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//
#include <stdlib.h>

#include "bench_vk.h"

//
//
//
int
main(int argc, char const * argv[])
{
  if (argc == 1)
    {
      bench_vk_usage(argv);
    }

  int const result = bench_vk(argc, argv);

  if (result == EXIT_FAILURE)
    {
      bench_vk_usage(argv);
    }

  return result;
}

//
//
//
