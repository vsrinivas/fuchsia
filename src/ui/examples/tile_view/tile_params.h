// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_TILE_VIEW_TILE_PARAMS_H_
#define SRC_UI_EXAMPLES_TILE_VIEW_TILE_PARAMS_H_

#include <string>
#include <vector>

#include "src/lib/fxl/command_line.h"

namespace examples {

struct TileParams {
  enum class OrientationMode {
    kHorizontal,
    kVertical,
  };

  bool Parse(const fxl::CommandLine& command_line);

  OrientationMode orientation_mode = OrientationMode::kHorizontal;

  std::vector<std::string> view_urls;
};

}  // namespace examples

#endif  // SRC_UI_EXAMPLES_TILE_VIEW_TILE_PARAMS_H_
