// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scene_dumper.h"

#include <stack>

#include <sdk/lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/allocation/id.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"

namespace {

constexpr char kIndentation[] = " | ";

inline void IndentLine(size_t current_indentation_level, std::ostream& output) {
  for (size_t i = 0; i < current_indentation_level; i++) {
    output << kIndentation;
  }
}

// Dumps the connected topology by outputting information on the current node and then iteratively
// dumping for direct children nodes. The topology vector is organized in a preordered depth-first
// order.
void DumpTopology(const flatland::UberStruct::InstanceMap& snapshot,
                  const flatland::GlobalTopologyData& topology_data, std::ostream& output) {
  output << "Topology:\n";
  std::stack<size_t> indentation_levels;
  for (size_t transform_index = 0; transform_index < topology_data.topology_vector.size();
       transform_index++) {
    auto& transform = topology_data.topology_vector[transform_index];
    const auto children = topology_data.child_counts[transform_index];
    auto current_indentation_level = indentation_levels.size();

    // Print indented, no-parentheses transform.
    IndentLine(current_indentation_level, output);
    output << transform.GetInstanceId() << ":" << transform.GetTransformId();

    // If the transform has children, print the pipe.
    if (children > 0) {
      output << "-|";
    }

    // Every time we cross a viewport/view boundary, print out the `debug_name` of the view's
    // Flatland instance.
    const auto uber_struct_it = snapshot.find(transform.GetInstanceId());
    if (uber_struct_it != snapshot.end() && transform.GetTransformId() == 0 &&
        !uber_struct_it->second->debug_name.empty()) {
      output << " <-- (" << uber_struct_it->second->debug_name << ")";
    }

    // Newline.
    output << '\n';

    // Adjust indentation for newline.
    if (children > 0) {
      indentation_levels.push(children);
    } else {
      while (!indentation_levels.empty()) {
        auto& current_indentation_level_children = indentation_levels.top();
        current_indentation_level_children--;
        if (current_indentation_level_children == 0) {
          indentation_levels.pop();
        } else {
          break;
        }
      }
    }
  }
}

// Dumps the complete topology by outputting information on the current node and then iteratively
// dumping for direct children nodes.
//
// Instances which are not present in the main topology will still appear in this dump.
void DumpAllInstances(const flatland::UberStruct::InstanceMap& snapshot,
                      const flatland::GlobalTopologyData& topology_data, std::ostream& output) {
  output << "All Instances:\n";
  for (auto& [instance_id, uber_struct] : snapshot) {
    output << "Instance " << instance_id << " (" << uber_struct->debug_name << "):\n";

    std::stack<size_t> indentation_levels;
    for (size_t transform_index = 0; transform_index < uber_struct->local_topology.size();
         transform_index++) {
      auto& transform = uber_struct->local_topology[transform_index];
      const auto children = topology_data.child_counts[transform_index];
      auto current_indentation_level = indentation_levels.size();

      // Print indented, no-parentheses transform.
      IndentLine(current_indentation_level, output);
      output << transform.handle.GetInstanceId() << ":" << transform.handle.GetTransformId();

      // If the transform has children, print the pipe.
      if (children > 0) {
        output << "-|";
      }

      // Newline.
      output << '\n';

      // Adjust indentation for newline.
      if (children > 0) {
        indentation_levels.push(children);
      } else {
        while (!indentation_levels.empty()) {
          auto& current_indentation_level_children = indentation_levels.top();
          current_indentation_level_children--;
          if (current_indentation_level_children == 0) {
            indentation_levels.pop();
          } else {
            break;
          }
        }
      }
    }
  }
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
  DumpTopology(snapshot, topology_data, output);
  output << '\n';
  DumpAllInstances(snapshot, topology_data, output);
  output << '\n';
  DumpImages(topology_data, images, image_indices, image_rectangles, output);
  output << "\n============ END SCENE DUMP ======================";
}

}  // namespace flatland
