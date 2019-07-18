// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTEL_HDA_UTILS_STATUS_H_
#define INTEL_HDA_UTILS_STATUS_H_

#include <fbl/string.h>
#include <zircon/types.h>

#include <string_view>
#include <variant>

namespace audio::intel_hda {

class [[nodiscard]] Status {
 public:
  // Create a Status with ZX_OK code, and no message.
  Status() : code_(ZX_OK) {}

  // Create a Status with the given code.
  explicit Status(zx_status_t code) : code_(code) {}

  // Create a Status with the given message.
  //
  // In the case of the "const char*" overload, we assume that
  // message will outlive this object.
  Status(zx_status_t code, const char* message);
  Status(zx_status_t code, fbl::String message);

  // Copy/move constructors/operators.
  Status(const Status& other) = default;
  Status& operator=(const Status& other) = default;
  Status(Status && other) = default;
  Status& operator=(Status&& other) = default;

  // Return true if the status is ZX_OK.
  [[nodiscard]] bool ok() const { return code_ == ZX_OK; }

  // Return the error code.
  [[nodiscard]] zx_status_t code() const { return code_; }

  // Return the message.
  [[nodiscard]] const fbl::String& message() const { return message_; }

  // Return a human-readable string containing both the code
  // and message, such as "Could not connect (ZX_ERR_ACCESS_DENIED)".
  [[nodiscard]] fbl::String ToString() const;

 private:
  // Error code.
  zx_status_t code_;

  // Supplied error message.
  fbl::String message_;
};

// More readable alias for Status() or Status(ZX_OK).
inline Status OkStatus() { return Status(); }

// Add a string to the beginning of a Status error message.
//
// The call:
//
//   PrependStatus("This is a prefix", Status(ZX_ERR_ACCESS_DENIED, "Denied"))
//
// will have a message of the form "This is a prefix: Denied".
Status PrependMessage(const fbl::String& prefix, const Status& status);

}  // namespace audio::intel_hda

#endif  // INTEL_HDA_UTILS_STATUS_H_
