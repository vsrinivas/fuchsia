// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_FORMATTERS_TEXT_H_
#define GARNET_BIN_IQUERY_FORMATTERS_TEXT_H_

#include <string>
#include <vector>
#include "../formatter.h"

namespace iquery {

class TextFormatter : public Formatter {
 public:
  std::string FormatFind(const std::vector<std::string>& find_results) override;
  std::string FormatLs(
      const std::vector<fuchsia::inspect::Object>& ls_results) override;
  std::string FormatCat(
      const std::vector<fuchsia::inspect::Object>& objects) override;
};

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_FORMATTERS_TEXT_H_
