// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_ERRORS_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_ERRORS_H_

namespace feedback {

// Defines common errors that occur throughout //src/developer/feedback.
enum class Error {
  kNotSet,
  // TODO(49922): Remove kDefault. This value is temporary to allow the enum to be used without
  // specifying the exact error that occurred.
  kDefault,
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_ERRORS_H_
