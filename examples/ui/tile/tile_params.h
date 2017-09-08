// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_TILE_TILE_PARAMS_H_
#define APPS_MOZART_EXAMPLES_TILE_TILE_PARAMS_H_

#include <vector>

#include "lib/ftl/command_line.h"

namespace examples {

struct TileParams {
  enum class OrientationMode {
    kHorizontal,
    kVertical,
  };

  TileParams();
  ~TileParams();

  bool Parse(const ftl::CommandLine& command_line);

  OrientationMode orientation_mode = OrientationMode::kHorizontal;

  std::vector<std::string> view_urls;
};

}  // namespace examples

#endif  // APPS_MOZART_EXAMPLES_TILE_TILE_PARAMS_H_
