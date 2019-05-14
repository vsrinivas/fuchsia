// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#ifdef SPN_TARGET_IMAGE_DUMP

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char const * argv[])
{
  FILE * fp = fopen("target.bin", "wb");  // emit "<vendor>_<arch>.bin" instead?

  fwrite(&SPN_TARGET_IMAGE_NAME, 1, sizeof(SPN_TARGET_IMAGE_NAME), fp);

  uint8_t const * modules = SPN_TARGET_IMAGE_NAME.modules;
  size_t          modsize = (modules[0] << 24) | (modules[1] << 16) | (modules[2] << 8) | modules[3];

  while (modsize > 0)
    {
      // fprintf(stderr,"%zu\n",modsize);
      modsize += sizeof(uint32_t);
      fwrite(modules, 1, modsize, fp);
      modules += modsize;
      modsize = (modules[0] << 24) | (modules[1] << 16) | (modules[2] << 8) | modules[3];
    }

  fclose(fp);

  return EXIT_SUCCESS;
}

#endif

//
//
//
