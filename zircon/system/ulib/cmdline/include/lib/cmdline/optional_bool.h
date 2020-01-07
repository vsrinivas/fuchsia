// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CMDLINE_OPTIONAL_BOOL_H_
#define CMDLINE_OPTIONAL_BOOL_H_

#include <cassert>
#include <optional>

namespace cmdline {
// Used by cmdline::ArgsParser to represent a boolean command line switch
// that may or may not be present on the command line.
//
// This class is an improved alternative to std::optional<bool> because it does
// not implement "operator bool()". Unfortunately, since std::optional<> does
// implement "operator bool()", it is very easy to make the mistake of using
// the |std::optional<>| in boolean expressions that should instead by using
// the wrapped boolan value.
//
// For example, the following code will execute the do_this_when_true() function
// because |my_flag| returns true, indicating the optional variable is set to
// a value:
//
//   std::optional<bool> my_flag = false;
//   if (my_flag) {
//      do_this_when_true();
//   }
//
// But the value is false! The caller should have used something like:
//
//   if (my_flag.value()) {
//
// This class does not require all of the features of std::optional, and does
// not need to be templatized since it is purely intended to support the
// potential ambiguity of std::optional<bool>.
class OptionalBool {
 public:
  OptionalBool() : has_value_(false) {}
  explicit OptionalBool(bool value) : has_value_(true), value_(value) {}

  explicit OptionalBool(const OptionalBool& rhs) = default;
  OptionalBool& operator=(const OptionalBool& rhs) = default;

  OptionalBool& operator=(std::nullopt_t) noexcept {
    has_value_ = false;
    return *this;
  }

  OptionalBool& operator=(bool rhs) {
    has_value_ = true;
    value_ = rhs;
    return *this;
  }

  bool has_value() const { return has_value_; }

  bool value() const {
    assert(has_value_);
    return value_;
  }

  bool operator*() const { return value(); }

  bool value_or(bool default_value) const {
    if (has_value_) {
      return value_;
    }
    return default_value;
  }

 private:
  bool has_value_;
  bool value_;
};

}  // namespace cmdline

#endif  // CMDLINE_OPTIONAL_BOOL_H_
