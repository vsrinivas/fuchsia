// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/tile/tile_params.h"

namespace examples {

TileParams::TileParams() {}

TileParams::~TileParams() {}

bool TileParams::Parse(const ftl::CommandLine& command_line) {
  std::string value;

  // Parse --horizontal and --vertical.
  if (command_line.HasOption("horizontal"))
    orientation_mode = TileParams::OrientationMode::kHorizontal;
  else if (command_line.HasOption("vertical"))
    orientation_mode = TileParams::OrientationMode::kVertical;

  // Remaining positional arguments are views.
  view_urls = command_line.positional_args();
  return !view_urls.empty();
}

}  // namespace examples
