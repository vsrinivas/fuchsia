// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_FORMATTER_H_
#define GARNET_BIN_IQUERY_FORMATTER_H_

#include <fuchsia/inspect/cpp/fidl.h>
#include <string>
#include <vector>

namespace iquery {

class Formatter {
 public:
  virtual ~Formatter() = default;
  virtual std::string FormatFind(
      const std::vector<std::string>& find_results) = 0;
  virtual std::string FormatLs(
      const std::vector<fuchsia::inspect::Object>& ls_results) = 0;
  virtual std::string FormatCat(
      const std::vector<fuchsia::inspect::Object>& objects) = 0;
};

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_FORMATTER_H_
