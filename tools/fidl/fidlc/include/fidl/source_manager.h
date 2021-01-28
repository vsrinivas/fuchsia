// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SOURCE_MANAGER_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SOURCE_MANAGER_H_

#include <memory>
#include <string_view>
#include <vector>

#include "fidl/source_file.h"

namespace fidl {

class SourceManager {
 public:
  // Returns whether the filename was successfully read.
  bool CreateSource(std::string_view filename);
  void AddSourceFile(std::unique_ptr<SourceFile> file);

  const std::vector<std::unique_ptr<SourceFile>>& sources() const { return sources_; }

 private:
  std::vector<std::unique_ptr<SourceFile>> sources_;
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_SOURCE_MANAGER_H_
