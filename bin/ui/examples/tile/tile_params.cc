// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/tile/tile_params.h"

namespace examples {

TileParams::TileParams() {}

TileParams::~TileParams() {}

bool TileParams::Parse(const ftl::CommandLine& command_line) {
  std::string value;

  // Parse --version.
  if (command_line.GetOptionValue("version", &value)) {
    if (value == "any")
      version_mode = TileParams::VersionMode::kAny;
    else if (value == "exact")
      version_mode = TileParams::VersionMode::kExact;
    else
      return false;
  }

  // Parse --combinator.
  if (command_line.GetOptionValue("combinator", &value)) {
    if (value == "merge")
      combinator_mode = TileParams::CombinatorMode::kMerge;
    else if (value == "prune")
      combinator_mode = TileParams::CombinatorMode::kPrune;
    else if (value == "flash")
      combinator_mode = TileParams::CombinatorMode::kFallbackFlash;
    else if (value == "dim")
      combinator_mode = TileParams::CombinatorMode::kFallbackDim;
    else
      return false;
  }

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
