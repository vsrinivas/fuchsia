// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ERR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ERR_H_

#include <string>

namespace zxdb {

// Most errors are general but in some cases we need to programmatically know a particular error.
// These errors are listed here.
enum class ErrType {
  kNone,            // No error.
  kGeneral,         // Unspecified error type.
  kCanceled,        // The operation was explicitly canceled.
  kNoConnection,    // There is no connection to the debug agent.
  kCorruptMessage,  // Data was corrupted between us and the debug agent.
  kClientApi,       // An invalid client API call.
  kInput,           // Some problem getting input from the user (parse error, etc.).
  kOptimizedOut,    // Not available because of optimization in the debugged program.
  kUnsupported      // The answer is probably knowable but the debugger doesn't support it yet.
};

class Err {
 public:
  // Indicates no error.
  Err();

  // Indicates an error of the given type with an optional error message.
  Err(ErrType type, const std::string& msg = std::string());

  // Produces a "general" error with the given message.
  explicit Err(const std::string& msg);

  [[gnu::format(printf, 2, 3)]] explicit Err(const char* fmt, ...);

  ~Err();

  bool has_error() const { return type_ != ErrType::kNone; }
  bool ok() const { return type_ == ErrType::kNone; }

  ErrType type() const { return type_; }
  const std::string& msg() const { return msg_; }

  // Equality operator is provided for tests.
  bool operator==(const Err& other) const;

 private:
  ErrType type_ = ErrType::kNone;
  std::string msg_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ERR_H_
