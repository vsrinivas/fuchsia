// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_STATUS_H_
#define SRC_DEVELOPER_DEBUG_SHARED_STATUS_H_

#include <optional>
#include <string>

#if defined(__Fuchsia__)
#include <zircon/status.h>
#endif

namespace debug {

class Status;

#if defined(__Fuchsia__)

// If there is an error and no message is given, the ZX_* constant will be queried and used.
// If given, the message will be used for most display purposes instead of the platform value, so
// if the value is important and you use a custom message, it should be manually included.
//
// These are split rather than using default values to avoid creating empty std::strings in the
// common case.
Status ZxStatus(zx_status_t s);
Status ZxStatus(zx_status_t s, std::string msg);

#else

// As with the ZxStatus version above, this will automatically use the strerror() strinf if no
// message is given.
Status ErrnoStatus(int en);
Status ErrnoStatus(int en, std::string msg);

#endif

// A cross-platform status value. MOst code will want to use one of the below platform-specific
// helper functions.
class Status {
 public:
  // No error. For error construction, use one of the helpers above.
  Status() = default;

  bool ok() const { return !platform_error_; }
  bool has_error() const { return !!platform_error_; }

  // These assume the error is set.
  int64_t platform_error() const { return *platform_error_; }
  const std::string& message() const { return message_; }

 private:
#if defined(__Fuchsia__)
  friend Status ZxStatus(zx_status_t s);
  friend Status ZxStatus(zx_status_t s, std::string msg);
#else
  friend Status ErrnoStatus(int en);
  friend Status ErrnoStatus(int en, std::string msg);
#endif

  // Only ZxStatus() and ErrnoStatus() can create this object.
  explicit Status(int64_t pe) : platform_error_(pe) {}
  Status(int64_t pe, std::string msg) : platform_error_(pe), message_(std::move(msg)) {}

  std::optional<int64_t> platform_error_;
  std::string message_;
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_STATUS_H_
