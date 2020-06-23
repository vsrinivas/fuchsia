// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_FIT_TIMEOUT_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_FIT_TIMEOUT_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <initializer_list>
#include <optional>

namespace forensics {
namespace fit {

// Couples a timeout and an action to optionally take when the timeout occurs.
struct Timeout {
  Timeout() = default;
  explicit Timeout(zx::duration value) : value(value) {}
  Timeout(zx::duration value, ::fit::closure action) : value(value), action(std::move(action)) {}

  zx::duration value;
  std::optional<::fit::closure> action;
};

}  // namespace fit
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_FIT_TIMEOUT_H_
