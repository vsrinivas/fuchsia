// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "source.h"

#include <fbl/array.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <src/lib/files/file.h>
#include <src/lib/fxl/strings/substitute.h>
#include <stdlib.h>
#include <string.h>

#include <stack>

namespace inspect_deprecated {

namespace {
fit::promise<inspect_deprecated::ObjectReader> OpenPathInsideRoot(
    inspect_deprecated::ObjectReader reader, std::vector<std::string> path_components,
    size_t index = 0) {
  if (index >= path_components.size()) {
    return fit::make_promise(
        [reader = std::move(reader)]() -> fit::result<inspect_deprecated::ObjectReader> {
          return fit::ok(reader);
        });
  }

  return reader.OpenChild(path_components[index])
      .and_then([reader = std::move(reader), path_components = std::move(path_components),
                 index](inspect_deprecated::ObjectReader& child) {
        return OpenPathInsideRoot(child, path_components, index + 1);
      });
}

// Finds the object hierarchy in a file-like object referenced by its
// full |path|. |info| is passed by value as VMO read moves the vmo object from
// inside it.
fit::result<ObjectHierarchy> ReadFromFilePtr(const std::string& path, fuchsia::io::NodeInfo info) {
  if (info.is_file()) {
    // The fbl::Array below will take ownership of buf.first.
    std::pair<uint8_t*, intptr_t> buf = files::ReadFileToBytes(path);
    if (buf.first == nullptr) {
      return fit::error();
    }
    // fbl::Array takes ownership of the file path, but uses delete[] instead of
    // delete.  To avoid the error ASan would give us if we simply transferred
    // ownership, we copy the array to something that can use delete[].
    uint8_t* new_buf = new uint8_t[buf.second];
    memcpy(new_buf, buf.first, buf.second);
    free(buf.first);
    return inspect_deprecated::ReadFromBuffer(fbl::Array(new_buf, buf.second));
  }
  return inspect_deprecated::ReadFromVmo(info.vmofile().vmo);
}

}  // namespace

fit::promise<Source, std::string> Source::MakeFromFidl(Location location,
                                                       inspect_deprecated::ObjectReader root_reader,
                                                       int depth) {
  return OpenPathInsideRoot(std::move(root_reader), location.inspect_path_components)
      .then([depth](fit::result<inspect_deprecated::ObjectReader>& reader)
                -> fit::promise<inspect_deprecated::ObjectHierarchy> {
        if (!reader.is_ok()) {
          return fit::make_promise(
              []() -> fit::result<inspect_deprecated::ObjectHierarchy> { return fit::error(); });
        }
        return inspect_deprecated::ReadFromFidl(reader.take_value(), depth);
      })
      .then(
          [location = std::move(location)](fit::result<inspect_deprecated::ObjectHierarchy>& result)
              -> fit::result<Source, std::string> {
            if (!result.is_ok()) {
              return fit::error(fxl::Substitute("Failed to read $0", location.NodePath()));
            }

            return fit::ok(Source(std::move(location), result.take_value()));
          });
}

fit::promise<Source, std::string> Source::MakeFromVmo(Location root_location,
                                                      fuchsia::io::FilePtr file_ptr, int depth) {
  fit::bridge<fuchsia::io::NodeInfo> vmo_read_bridge;
  file_ptr->Describe(vmo_read_bridge.completer.bind());

  return vmo_read_bridge.consumer.promise_or(fit::error())
      .then([depth, file_ptr = std::move(file_ptr), root_location = std::move(root_location)](
                fit::result<fuchsia::io::NodeInfo>& result) -> fit::result<Source, std::string> {
        if (!result.is_ok()) {
          return fit::error(
              fxl::Substitute("Failed to describe file: $0", root_location.AbsoluteFilePath()));
        }

        fuchsia::io::NodeInfo info = result.take_value();
        fit::result<ObjectHierarchy> read_result =
            ReadFromFilePtr(root_location.AbsoluteFilePath(), std::move(info));
        if (!read_result.is_ok()) {
          return fit::error(fxl::Substitute("Failed reading the VMO as an Inspect VMO or file: $0",
                                            root_location.AbsoluteFilePath()));
        }

        ObjectHierarchy hierarchy_root = read_result.take_value();
        ObjectHierarchy* hierarchy = &hierarchy_root;

        // Navigate within the hierarchy to the correct location.
        for (const auto& path_component : root_location.inspect_path_components) {
          auto child = std::find_if(hierarchy->children().begin(), hierarchy->children().end(),
                                    [&path_component](inspect_deprecated::ObjectHierarchy& obj) {
                                      return obj.node().name() == path_component;
                                    });
          if (child == hierarchy->children().end()) {
            return fit::error(fxl::Substitute("Could not find child named $0", path_component));
          }
          hierarchy = &(*child);
        }

        if (depth >= 0) {
          // If we have a specific depth requirement, prune the hierarchy tree
          // to the requested depth. Reading the VMO is all or nothing, so we
          // require post-processing to implement specific depth cutoffs.

          // Stack of ObjectHierarchies along with an associated depth.
          // Hierarchies at the max depth will have their children pruned, while
          // hierarchies at a lower depth simply push their children onto the
          // stack.
          std::stack<std::pair<inspect_deprecated::ObjectHierarchy*, int>> object_depth_stack;

          object_depth_stack.push(std::make_pair(hierarchy, 0));
          while (object_depth_stack.size() > 0) {
            auto pair = object_depth_stack.top();
            object_depth_stack.pop();

            if (pair.second == depth) {
              pair.first->children().clear();
            } else {
              for (auto& child : pair.first->children()) {
                object_depth_stack.push(std::make_pair(&child, pair.second + 1));
              }
            }
          }
        }

        return fit::ok(Source(std::move(root_location), std::move(*hierarchy)));
      });
}

void Source::VisitObjectsInHierarchyRecursively(const Visitor& visitor,
                                                const inspect_deprecated::ObjectHierarchy& current,
                                                std::vector<std::string>* path) const {
  visitor(*path, current);

  for (const auto& child : current.children()) {
    path->push_back(child.node().name());
    VisitObjectsInHierarchyRecursively(visitor, child, path);
    path->pop_back();
  }
}

void Source::VisitObjectsInHierarchy(Visitor visitor) const {
  std::vector<std::string> path;
  VisitObjectsInHierarchyRecursively(visitor, GetHierarchy(), &path);
}

void Source::SortHierarchy() {
  std::stack<inspect_deprecated::ObjectHierarchy*> hierarchies_to_sort;
  hierarchies_to_sort.push(&hierarchy_);
  while (hierarchies_to_sort.size() > 0) {
    auto* hierarchy = hierarchies_to_sort.top();
    hierarchies_to_sort.pop();
    hierarchy->Sort();
    for (auto& child : hierarchy->children()) {
      hierarchies_to_sort.push(&child);
    }
  }
}

}  // namespace inspect_deprecated
