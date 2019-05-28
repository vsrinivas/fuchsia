// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_QUERY_FORMATTER_H_
#define LIB_INSPECT_QUERY_FORMATTER_H_

#include "source.h"

namespace inspect {

class Formatter {
 public:
  // Configure the formatting of paths for this formatter.
  enum class PathFormat {
    // Do not include paths in output, only list the names of Nodes.
    NONE,

    // Include the full path to each Node in the output.
    FULL,

    // Include the absolute path to each Node in the output.
    ABSOLUTE,
  };

  Formatter(PathFormat path_format = PathFormat::NONE)
      : path_format_(path_format) {}
  virtual ~Formatter() = default;

  // Formats the locations of all nodes under the given sources recursively.
  virtual std::string FormatSourceLocations(
      const std::vector<inspect::Source>& sources) const = 0;

  // Formats the names of the children of the given sources in the desired
  // string format.
  virtual std::string FormatChildListing(
      const std::vector<inspect::Source>& sources) const = 0;

  // Recursively formats all hierarchies in the list of sources.
  virtual std::string FormatSourcesRecursive(
      const std::vector<inspect::Source>& sources) const = 0;

  virtual std::string FormatHealth(
      const std::vector<inspect::Source>& sources) const = 0;

 protected:
  using Path = std::vector<std::string>;

  // Formats the path to the given node or the name of the node depending on the
  // configured path format.
  std::string FormatPathOrName(const inspect::Location& location,
                               const Path& path_from_location,
                               const std::string& node_name) const;

  // Formats either the absolute or relative path to the node depending on the
  // path format.
  //
  // This differs from FormatPathOrName in that it never returns the name of the
  // node even if PathFormat is NONE.
  std::string FormatPath(const inspect::Location& location,
                         const Path& path_from_location) const;

 private:
  PathFormat path_format_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_QUERY_FORMATTER_H_
