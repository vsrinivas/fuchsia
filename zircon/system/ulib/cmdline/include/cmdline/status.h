// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CMDLINE_STATUS_H_
#define CMDLINE_STATUS_H_

#include <string>

namespace cmdline {

// Return an object of this class from an otherwise void function to indicate
// if the function executed successfully, or in the case of an error, to
// provide an error message with additional detail. The message should be
// worded for consumption by end-users of an application, such as to print
// an error message from a command line tool, or perhaps a label field or
// dialog window in a UI.
class Status {
 public:
  // Return |Status::Ok()| to indicate the function did not fail.
  // |Status| objects are copyable and assignable, so you can create a default
  // |Status| and later assign a |Status::Error(with_message)|.
  // For example:
  //
  //   Status status = Status::Ok();
  //   if (it_failed) {
  //     status = Status::Error("It didn't work.");
  //   }
  //   cleanup();
  //   return status;
  static Status Ok() { return Status(); }

  // Return |Status::Error(error_message)| to indicate the function failed,
  // and provide a message explaining the error, suitable for an end user.
  static Status Error(const std::string& error_message) {
    return Status(error_message.size() > 0 ? error_message : "There was an error.");
  }

  // Returns |true| if constructed with a non-empty message, indicating the
  // outcome of the call was an error.
  bool has_error() const { return error_message_.size() > 0; }

  // Returns |true| if constructed with no arguments, or an empty string,
  // indicating the function completed successfully.
  bool ok() const { return error_message_.size() == 0; }

  // Returns an non-empty string if the outcome of the call was an error.
  std::string error_message() const { return error_message_; }

 private:
  Status() = default;

  Status(const std::string& error_message) : error_message_(error_message) {}

  std::string error_message_;
};

}  // namespace cmdline

#endif  // CMDLINE_STATUS_H_
