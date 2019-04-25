// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_QUERY_SOURCE_H_
#define LIB_INSPECT_QUERY_SOURCE_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fit/promise.h>
#include <lib/inspect/reader.h>

#include "lib/inspect/hierarchy.h"
#include "location.h"

namespace inspect {

// A Source is the result of reading data from an Inspect Location.
//
// It wraps a Location along with the ObjectHierarchy that was read.
class Source {
 public:
  using Visitor = fit::function<void(const std::vector<std::string>&,
                                     const inspect::ObjectHierarchy&)>;

  Source(Location location, inspect::ObjectHierarchy hierarchy)
      : location_(std::move(location)), hierarchy_(std::move(hierarchy)) {}

  // Construct a new source consisting of an inspectable file path and path
  // components for the element to inspect within the hierarchy.
  // The hierarchy will be populated by the given ObjectReader.
  static fit::promise<Source, std::string> MakeFromFidl(
      Location root_location, inspect::ObjectReader root_reader,
      int depth = -1);

  // Construct a new source consisting of an inspectable file path.
  static fit::promise<Source, std::string> MakeFromVmo(
      Location root_location, fuchsia::io::FilePtr file_ptr, int depth = -1);

  // Return a reference to the location for this source.
  const Location GetLocation() const { return location_; }

  // Return a reference to the requested object hierarchy.
  const inspect::ObjectHierarchy& GetHierarchy() const { return hierarchy_; }

  // Visit each ObjectHierarchy recursively.
  // The visitor function receives a reference to the relative path within the
  // hierarchy and a reference to the hierarchy rooted at that path.
  void VisitObjectsInHierarchy(Visitor visitor) const;

  // Sort objects in the stored hierarchy by name.
  void SortHierarchy();

 private:
  void VisitObjectsInHierarchyRecursively(
      const Visitor& visitor, const inspect::ObjectHierarchy& current,
      std::vector<std::string>* path) const;

  // The location of the root tree accessible through the file system.
  Location location_;

  // The requested portion of the hierarchy for this source.
  inspect::ObjectHierarchy hierarchy_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_QUERY_SOURCE_H_
