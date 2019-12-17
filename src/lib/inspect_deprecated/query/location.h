// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_INSPECT_DEPRECATED_QUERY_LOCATION_H_
#define SRC_LIB_INSPECT_DEPRECATED_QUERY_LOCATION_H_

#include <lib/fit/result.h>

#include <regex>
#include <string>
#include <vector>

namespace inspect_deprecated {

// Returns a regex that matches Inspect file names.
std::regex inspect_file_regex();

// Description of how to reach a particular InspectHierarchy.
struct Location {
  // The file type that the Location contains.
  enum class Type {
    // The wrapped file implements fuchsia.inspect.Inspect.
    INSPECT_FIDL,
    // The wrapped file contains data stored in the Inspect File Format.
    // This includes VMOs and actual files.
    INSPECT_FILE_FORMAT,
  };

  // Parses a string path as a Location without consulting the file
  // system.
  //
  // Returns the parsed location on success.
  static fit::result<Location, std::string> Parse(const std::string& path);

  // Compares this Location with another.
  bool operator==(const Location& other) const;

  // The type of the Location, which gives information on how to obtain
  // the stored data.
  Type type;

  // The directory containing the inspect entry point.
  std::string directory_path;

  // The file name for the inspect entry point in the file.
  std::string file_name;

  // The path components for a particular InspectHierarchy within the inspect
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
  //   /hub/r/sys/1/c/component.cmx/2/out/diagnostics/fuchsia.inspect.Inspect
  //   /hub/r/sys/1/c/component.cmx/2/out/diagnostics/root.inspect
  std::string AbsoluteFilePath() const;

  // Gets the simplified relative file path to the object entry point. The
  // returned path is simplified in that "<directory_path>" is shorthand for
  // "<directory_path>/fuchsia.inspect.Inspect".
  //
  // Example:
  //   ./objects
  //   ./objects/root.inspect
  std::string SimplifiedFilePath() const;

  // Gets the path to the node inside the hierarchy referenced by this
  // location. The returned path is simplified using relevant shorthands.
  // The suffix may refer to a node nested within the root, and it is
  // appended to the inspect components properly.
  //
  // Example:
  //   ./objects#child/node
  //   ./objects/root.inspect#child/node
  std::string NodePath(const std::vector<std::string>& suffix = {}) const;
};

// Get a printable representation of the Location.:
std::ostream& operator<<(std::ostream& os, const Location& location);

}  // namespace inspect_deprecated

#endif  // SRC_LIB_INSPECT_DEPRECATED_QUERY_LOCATION_H_
