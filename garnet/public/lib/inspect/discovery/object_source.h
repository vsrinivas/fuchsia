// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_DISCOVERY_OBJECT_SOURCE_H_
#define GARNET_PUBLIC_LIB_DISCOVERY_OBJECT_SOURCE_H_

#include <vector>

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/inspect/reader.h>
//#include "garnet/bin/iquery/utils.h"

namespace inspect {

// Description of how to reach a particular inspect ObjectHierarchy.
struct ObjectLocation {
  enum class Type {
    INSPECT_FIDL,  // The wrapped file implements fuchsia.inspect.Inspect.
    INSPECT_VMO,   // The wrapped file is an Inspect VMO file.
  };

  // The type of the ObjectLocation, which gives information on how to obtain
  // the stored data.
  Type type;

  // The directory containing the inspect entry point.
  std::string directory_path;

  // The file name for the inspect entry point in the file.
  std::string file_name;

  // The path components for a particular ObjectHierarchy within the inspect
  // entry point.
  std::vector<std::string> inspect_path_components;

  // Gets the relative file path to the object entry point.
  //
  // Example:
  //   ./objects/fuchsia.inspect.Inspect
  //   ./objects/root.inspect
  std::string RelativeFilePath() const;

  // Gets the absolute file path to the object entry point. The returned path is
  // appropriate for use in Open calls.
  //
  // Example:
  //   /hub/r/sys/1/c/component.cmx/2/out/objects/fuchsia.inspect.Inspect
  //   /hub/r/sys/1/c/component.cmx/2/out/objects/root.inspect
  std::string AbsoluteFilePath() const;

  // Gets the simplified relative file path to the object entry point. The
  // returned path is simplified in that "<directory_path>" is shorthand for
  // "<directory_path>/fuchsia.inspect.Inspect".
  //
  // Example:
  //   ./objects
  //   ./objects/root.inspect
  std::string SimplifiedFilePath() const;

  // Gets the path to the object inside the hierarchy referenced by this
  // location. The returned path is simplified using relevant shorthands.
  // The suffix may refer to an object nested within the root, and it is
  // appended to the inspect components properly.
  //
  // Example:
  //   ./objects#child/object
  //   ./objects/root.inspect#child/object
  std::string ObjectPath(const std::vector<std::string>& suffix = {}) const;
};

// An ObjectSource represents a particular object hierarchy reachable through
// the file system. It consists of an ObjectLocation describing how to navigate
// to the desired hierarchy and within the hierarchy itself.
class ObjectSource {
 public:
  using Visitor = fit::function<void(const std::vector<std::string>&,
                                     const inspect::ObjectHierarchy&)>;

  // Construct a new source consisting of an inspectable file path and path
  // components for the element to inspect within the hierarchy.
  // The hierarchy will be populated by the given ObjectReader.
  static fit::promise<ObjectSource> Make(ObjectLocation root_location,
                                         inspect::ObjectReader root_reader,
                                         int depth = -1);

  static fit::promise<ObjectSource> Make(ObjectLocation root_location,
                                         fuchsia::io::FilePtr file_ptr,
                                         int depth = -1);

  // Format the relative path to the root object hierarchy followed by the given
  // list of path components.
  std::string FormatRelativePath(
      const std::vector<std::string>& suffix = {}) const;

  // Return a pointer to the root object hierarchy.
  const inspect::ObjectHierarchy& GetRootHierarchy() const {
    return hierarchy_;
  }

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

  // The location of the root object accessible through the file system.
  ObjectLocation location_;

  // The requested portion of the hierarchy for this object.
  inspect::ObjectHierarchy hierarchy_;
};

fit::promise<ObjectSource> MakeObjectPromiseFromLocation(
    ObjectLocation location, int depth);
fit::result<ObjectLocation> ParseToLocation(const std::string& path);
std::vector<ObjectLocation> SyncFindPaths(const std::string& path);

}  // namespace inspect

#endif  // GARNET_PUBLIC_LIB_DISCOVERY_OBJECT_SOURCE_H_
