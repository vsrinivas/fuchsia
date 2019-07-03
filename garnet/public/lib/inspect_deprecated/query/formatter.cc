// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "formatter.h"

#include "src/lib/files/path.h"

namespace inspect {

std::string Formatter::FormatPathOrName(const inspect::Location& location,
                                        const Path& path_from_location,
                                        const std::string& node_name) const {
  switch (path_format_) {
    case PathFormat::NONE:
      return node_name;
    case PathFormat::FULL:
      return location.NodePath(path_from_location);
    case PathFormat::ABSOLUTE:
      return files::AbsolutePath(location.NodePath(path_from_location));
  }
}

std::string Formatter::FormatPath(const inspect::Location& location,
                                  const Path& path_from_location) const {
  switch (path_format_) {
    case PathFormat::ABSOLUTE:
      return files::AbsolutePath(location.NodePath(path_from_location));
    default:
      return location.NodePath(path_from_location);
  }
}

}  // namespace inspect
