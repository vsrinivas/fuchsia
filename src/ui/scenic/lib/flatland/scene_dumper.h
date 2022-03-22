// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_SCENE_DUMPER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_SCENE_DUMPER_H_

#include <ostream>

#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"

namespace flatland {

// Dumps information about a flatland scene to an output stream.
void DumpScene(const flatland::UberStruct::InstanceMap& snapshot,
               const flatland::GlobalTopologyData& topology_data,
               const flatland::GlobalImageVector& images,
               const flatland::GlobalIndexVector& image_indices,
               const flatland::GlobalRectangleVector& image_rectangles, std::ostream& output);

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_SCENE_DUMPER_H_
