// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scene_dumper.h"

#include <sdk/lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/allocation/id.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"

namespace {

constexpr char kIndentation[] = "  |  ";

inline void IndentLine(int current_indentation_level, std::ostream& output) {
  for (int i = 0; i < current_indentation_level; i++) {
    output << kIndentation;
  }
}

// Dumps the topology by outputting information on the current node and then recursively dumping for
// direct children nodes. The topology vector is organized in a preordered depth-first order.
//
// The return value is the index of the next direct child for the current recursive iteration (as
// direct child nodes are not contiguous). Once completed, the return index will equal
// topology_vector.size().
size_t DumpTopology(const flatland::UberStruct::InstanceMap& snapshot,
                    const flatland::GlobalTopologyData& topology_data, size_t current_index,
                    int current_indentation_level, std::ostream& output) {
  if (current_index >= topology_data.topology_vector.size()) {
    return current_index;
  }

  // If the current transform has a debug name, print it on its own line.
  const auto& transform = topology_data.topology_vector[current_index];
  const auto uber_struct_it = snapshot.find(transform.GetInstanceId());
  if (uber_struct_it != snapshot.end() && !uber_struct_it->second->debug_name.empty()) {
    IndentLine(current_indentation_level, output);
    output << '(' << uber_struct_it->second->debug_name << ")\n";
  }

  IndentLine(current_indentation_level, output);
  output << transform;

  // Dump children and sibling transforms.
  const auto remaining = topology_data.child_counts[current_index];
  if (remaining) {
    output << "--|";
  }
  output << '\n';
  current_indentation_level++;
  current_index++;
  for (size_t i = 0; i < remaining; i++) {
    FX_DCHECK(current_index < topology_data.topology_vector.size());
    current_index =
        DumpTopology(snapshot, topology_data, current_index, current_indentation_level, output);
  }
  return current_index;
}

void DumpImages(const flatland::GlobalTopologyData& topology_data,
                const flatland::GlobalImageVector& images,
                const flatland::GlobalIndexVector& image_indices,
                const flatland::GlobalRectangleVector& image_rectangles, std::ostream& output) {
  output << "\nFrame display-list contains " << images.size() << " images and image-rectangles.";
  FX_DCHECK(images.size() == image_rectangles.size());
  FX_DCHECK(images.size() == image_indices.size());
  for (size_t i = 0; i < images.size(); i++) {
    auto& image = images[i];
    output << "\n        image: " << image;
    output << "\n        transform: " << topology_data.topology_vector[image_indices[i]];
    output << "\n        rect: " << image_rectangles[i];
  }
}

}  // namespace

namespace flatland {

void DumpScene(const UberStruct::InstanceMap& snapshot,
               const flatland::GlobalTopologyData& topology_data,
               const flatland::GlobalImageVector& images,
               const flatland::GlobalIndexVector& image_indices,
               const flatland::GlobalRectangleVector& image_rectangles, std::ostream& output) {
  output << "\n========== BEGIN SCENE DUMP ======================\n";
  output << "Topology:\n";
  const auto vector_index = DumpTopology(snapshot, topology_data, /*current_index=*/0,
                                         /*current_indentation_level=*/0, output);
  FX_DCHECK(vector_index == topology_data.topology_vector.size());

  output << '\n';
  DumpImages(topology_data, images, image_indices, image_rectangles, output);
  output << "\n============ END SCENE DUMP ======================";
}

}  // namespace flatland
