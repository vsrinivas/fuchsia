// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_MODES_H_
#define GARNET_BIN_IQUERY_MODES_H_

#include <vector>

#include <lib/fit/promise.h>
#include <lib/inspect/reader.h>
#include "garnet/bin/iquery/options.h"
#include "garnet/bin/iquery/utils.h"

namespace iquery {

// Description of how to reach a particular inspect ObjectHierarchy.
struct ObjectLocation {
  // The directory containing the inspect entry point.
  std::string directory_path;

  // The file name for the inspect entry point in the file.
  std::string file_name;

  // The path components for a particular ObjectHierarchy within the inspect
  // entry point.
  std::vector<std::string> inspect_path_components;
};

// An ObjectSource represents a particular object hierarchy reachable through
// the file system. It consists of an InspectLocation describing how to navigate
// to the desired hierarchy and the hierarchy itself.
class ObjectSource {
 public:
  using Visitor = fit::function<void(const std::vector<std::string>&,
                                     const inspect::ObjectHierarchy&)>;

  enum class Type {
    INSPECT_FIDL,  // The wrapped file implements fuchsia.inspect.Inspect.
  };

  // Construct a new source consisting of an inspectable file path and path
  // components for the element to inspect within the hierarchy.
  // The hierarchy will be populated by the given ObjectReader.
  static fit::promise<ObjectSource> Make(ObjectLocation root_location,
                                         inspect::ObjectReader root_reader,
                                         int depth = -1);

  // Format the path required to refer to the root object hierarchy.
  std::string FormatRelativePath() const;

  // Format the relative path to the root object hierarchy followed by the given
  // list of path components.
  std::string FormatRelativePath(const std::vector<std::string>& suffix) const;

  // Return a pointer to the root object hierarchy.
  const inspect::ObjectHierarchy& GetRootHierarchy() const {
    return hierarchy_;
  }

  // Visit each ObjectHierarchy recursively.
  // The visitor function receives a reference to the relative path within the
  // hierarchy and a reference to the hierarchy rooted at that path.
  void VisitObjectsInHierarchy(Visitor visitor) const;

 private:
  void VisitObjectsInHierarchyRecursively(
      const Visitor& visitor, const inspect::ObjectHierarchy& current,
      std::vector<std::string>* path) const;

  // The location of the root object accessible through the file system.
  ObjectLocation location_;

  // The type of the file, which indicates how it should be interpreted.
  Type type_;

  // The requested portion of the hierarchy for this object.
  inspect::ObjectHierarchy hierarchy_;
};

fit::promise<std::vector<ObjectSource>> RunCat(const Options*);
fit::promise<std::vector<ObjectSource>> RunFind(const Options*);
fit::promise<std::vector<ObjectSource>> RunLs(const Options*);

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_MODES_H_
