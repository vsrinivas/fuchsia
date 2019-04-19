// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_CRASHPAD_AGENT_SCOPED_UNLINK_H_
#define SRC_DEVELOPER_CRASHPAD_AGENT_SCOPED_UNLINK_H_

#include <unistd.h>

#include <string>

#include "src/lib/fxl/macros.h"

namespace fuchsia {
namespace crash {

class ScopedUnlink {
 public:
  ScopedUnlink() = default;
  explicit ScopedUnlink(const std::string& filename) : filename_(filename) {}
  ~ScopedUnlink() {
    if (is_valid()) {
      unlink(filename_.c_str());
    }
  }

  ScopedUnlink(ScopedUnlink&&) = default;
  ScopedUnlink& operator=(ScopedUnlink&&) = default;

  bool is_valid() const { return !filename_.empty(); }
  const std::string& get() const { return filename_; }

 private:
  std::string filename_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedUnlink);
};

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_CRASHPAD_AGENT_SCOPED_UNLINK_H_
