// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_DEPRECATED_QUERY_TEXT_FORMATTER_H_
#define LIB_INSPECT_DEPRECATED_QUERY_TEXT_FORMATTER_H_

#include "formatter.h"

namespace inspect_deprecated {

class TextFormatter : public Formatter {
 public:
  struct Options {
    // The number of spaces used to indent nested values.
    size_t indent = 2;
  };

  TextFormatter(Options options, PathFormat path_format = PathFormat::NONE)
      : Formatter(path_format), options_(std::move(options)) {}
  ~TextFormatter() = default;

  std::string FormatSourceLocations(
      const std::vector<inspect_deprecated::Source>& sources) const override;

  std::string FormatChildListing(
      const std::vector<inspect_deprecated::Source>& sources) const override;

  std::string FormatSourcesRecursive(
      const std::vector<inspect_deprecated::Source>& sources) const override;

  std::string FormatHealth(const std::vector<inspect_deprecated::Source>& sources) const override;

 private:
  Options options_;
};

}  // namespace inspect_deprecated

#endif  // LIB_INSPECT_DEPRECATED_QUERY_TEXT_FORMATTER_H_
