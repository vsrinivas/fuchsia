// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>

#include <memory>
#include <unordered_map>

// clang-format off
#include "src/ui/lib/glm_workaround/glm_workaround.h"
// clang-format on

#include <glm/mat3x3.hpp>

#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/transform_graph.h"
#include "src/ui/scenic/lib/flatland/transform_handle.h"

namespace flatland {

// TODO(fxbug.dev/45932): find the appropriate name for this struct.
//
// A collection of data local to a particular Flatland instance representing the most recent commit
// of that instance's presented state. Because the UberStruct represents a snapshot of the local
// state of a Flatland instance, it must be stateless. It should contain only data and no
// references to external resources.
struct UberStruct {
  using InstanceMap = std::unordered_map<TransformHandle::InstanceId, std::shared_ptr<UberStruct>>;

  // The local topology of this Flatland instance.
  TransformGraph::TopologyVector local_topology;

  // The LinkProperties for each child link of this Flatland instance. Entries in this map will
  // have children that are in different Flatland instances.
  using LinkPropertiesMap =
      std::unordered_map<TransformHandle, fuchsia::ui::scenic::internal::LinkProperties>;
  LinkPropertiesMap link_properties;

  // The local (i.e. relative to the parent) geometric transformation matrix of each
  // TransformHandle. Handles with no entry indicate an identity matrix.
  std::unordered_map<TransformHandle, glm::mat3> local_matrices;

  // The images associated with each TransformHandle.
  std::unordered_map<TransformHandle, ImageMetadata> images;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_H_
