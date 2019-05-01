// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_QUERY_JSON_FORMATTER_H_
#define LIB_INSPECT_QUERY_JSON_FORMATTER_H_

#include "formatter.h"

namespace inspect {
class JsonFormatter : public Formatter {
 public:
  struct Options {
    // The number of spaces used to indent nested values.
    // If indent is 0, the output will be kept compact on a single line.
    size_t indent = 4;
  };

  JsonFormatter(Options options, PathFormat path_format = PathFormat::NONE)
      : Formatter(path_format), options_(std::move(options)) {}
  ~JsonFormatter() = default;

  std::string FormatSourceLocations(
      const std::vector<inspect::Source>& sources) const override;

  std::string FormatChildListing(
      const std::vector<inspect::Source>& sources) const override;

  std::string FormatSourcesRecursive(
      const std::vector<inspect::Source>& sources) const override;

 private:
  template <typename WriterType>
  void InternalFormatSourceLocations(
      WriterType& writer, const std::vector<inspect::Source>& sources) const;

  template <typename WriterType>
  void InternalFormatChildListing(
      WriterType& writer, const std::vector<inspect::Source>& sources) const;

  template <typename WriterType>
  void InternalFormatSourcesRecursive(
      WriterType& writer, const std::vector<inspect::Source>& sources) const;

  Options options_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_QUERY_JSON_FORMATTER_H_
