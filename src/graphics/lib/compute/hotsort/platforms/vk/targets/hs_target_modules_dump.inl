// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef HS_DUMP

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

//
// Dump 'struct hotsort_vk_target' to file name argv[1]
//

int
main(int argc, char const * argv[])
{
  FILE * file = fopen(argv[1], "wb");

  if (file == NULL)
    {
      return EXIT_FAILURE;
    }

  size_t const size_config = sizeof(HS_TARGET_NAME->config);

  if (fwrite(&HS_TARGET_NAME->config, 1, size_config, file) != size_config)
    {
      return EXIT_FAILURE;
    }

  uint32_t const * modules = HS_TARGET_NAME->modules;
  uint32_t         dwords  = modules[0];

  while (dwords > 0)
    {
      dwords += 1;

      size_t const size_module = dwords * sizeof(*modules);

      if (fwrite(modules, 1, size_module, file) != size_module)
        {
          return EXIT_FAILURE;
        }

      modules += dwords;
      dwords = modules[0];
    }

  if (fclose(file) != 0)
    {
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}

#endif

//
//
//
