// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include "scoped_svg.h"
#include "svg_print.h"

int
main(int argc, char ** argv)
{
  if (argc < 2)
    {
      fprintf(stderr, "This program takes the path of an SVG document as input!\n");
      return EXIT_FAILURE;
    }
  const char * input_file = argv[1];

  ScopedSvg svg = ScopedSvg::parseFile(input_file);

  svg_print_stdout(svg.get());
  return EXIT_SUCCESS;
}
