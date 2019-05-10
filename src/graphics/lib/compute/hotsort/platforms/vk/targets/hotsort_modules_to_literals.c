// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

//
// hotsort_modules_to_literals:
//
//   Write a concatenated array of post-processed SPIR-V modules to a
//   file as an array of hex-encoded 32-bit C literals.
//
// Arguments:
//
//   <output file> <spir-v module:1> ... <spir-v module M>
//
// Operation:
//
//   1. For each SPIR-V module:
//     1. Open and find length
//     2. Reallocate incrementing by length plus dword
//     3. Store length
//     4. Store module
//     5. Close module
//   2. Open and write out literals to output file
//   3. Close output file
//
// SPIR-V modules are encoded with this layout:
//
//   DWORD 0   - N : number of dwords in SPIR-V module
//   DWORD 1   - module[0]
//   DWORD N+1 - module[N-1]
//

#define HS_LITERALS_PER_LINE  6

//
//
//

int
main(int argc, char const * argv[])
{
  // This tool will typically be passed ~20 files but if there isn't
  // at least one file then fail.
  if (argc < 3) {
    return EXIT_FAILURE;
  }

  //
  // layout buffer is reallocated for each module
  //
  uint32_t * layout      = NULL;
  size_t     layout_size = 0;
  uint32_t   layout_next = 0;

  //
  // load and process all modules
  //
  uint32_t const module_count = argc - 2;

  for (uint32_t ii=0; ii<module_count; ii++)
    {
      FILE * module = fopen(argv[2+ii],"rb");

      if (module == NULL) {
        return EXIT_FAILURE;
      }

      if (fseek(module,0L,SEEK_END) != 0) {
        return EXIT_FAILURE;
      }

      long const module_bytes = ftell(module);

      if (module_bytes == EOF) {
        return EXIT_FAILURE;
      }

      rewind(module);

      // "length + module size"
      layout_size += sizeof(uint32_t) + module_bytes;

      layout = realloc(layout,layout_size);

      if (layout == NULL) {
        return EXIT_FAILURE;
      }

      // store dwords
      uint32_t const module_dwords = (uint32_t)(module_bytes / sizeof(uint32_t));

      layout[layout_next++] = module_dwords;

      // load module
      if (fread(layout+layout_next,1,module_bytes,module) != (size_t)module_bytes) {
        return EXIT_FAILURE;
      }

      // close module
      if (fclose(module) != 0) {
        return EXIT_FAILURE;
      }

      // move to next module
      layout_next += module_dwords;
    }

  //
  // store
  //
  FILE * file = fopen(argv[1],"wb");

  if (file == NULL) {
    return EXIT_FAILURE;
  }

  uint32_t literals = 0;

  for (uint32_t ii=0; ii<layout_next; ii++)
    {
      fprintf(file,"0x%08X",layout[ii]);

      if ((++literals % HS_LITERALS_PER_LINE) != 0) {
        fprintf(file,", ");
      } else {
        fprintf(file,",\n");
      }
    }

  fprintf(file,"\n");

  if (ferror(file) || fclose(file) != 0) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

//
//
//
