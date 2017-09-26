// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_TILE_TILE_PARAMS_H_
#define GARNET_EXAMPLES_UI_TILE_TILE_PARAMS_H_

#include <vector>

#include "lib/fxl/command_line.h"

namespace examples {

struct TileParams {
  enum class OrientationMode {
    kHorizontal,
    kVertical,
  };

  TileParams();
  ~TileParams();

  bool Parse(const fxl::CommandLine& command_line);

  OrientationMode orientation_mode = OrientationMode::kHorizontal;

  std::vector<std::string> view_urls;
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_TILE_TILE_PARAMS_H_
