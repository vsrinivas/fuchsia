// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_LOG_FORMAT_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_LOG_FORMAT_H_

#include <fuchsia/logger/cpp/fidl.h>

#include <string>

namespace feedback {

// Format a log message as a string.
std::string Format(const fuchsia::logger::LogMessage& message);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_LOG_FORMAT_H_
