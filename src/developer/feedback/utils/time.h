// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_TIME_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_TIME_H_

#include <lib/zx/time.h>

#include <optional>
#include <string>

namespace feedback {

// Formats the provided duration as WdXhYmZs e.g., 1d14h7m32s
std::optional<std::string> FormatDuration(zx::duration duration);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_TIME_H_
