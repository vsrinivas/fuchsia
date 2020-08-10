// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CMDLINE_OPTIONAL_H_
#define CMDLINE_OPTIONAL_H_

#include <cassert>
#include <optional>

namespace cmdline {
// Used by cmdline::ArgsParser to represent a command line switches that may or
// may not be present on the command line.
//
// This class is an improved alternative to std::optional<T> because it does
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
// A similar option exists for non-bool types that may be implicitly cast to
// bool, such as ints and floats.
//
// This class does not provide all of the features of std::optional, but is
// simply intended to support the potential ambiguity of std::optional<bool>.
template <typename T>
class Optional {
 public:
  Optional() : value_() {}
  Optional(T value) : value_(value) {}  // NOLINT(google-explicit-constructor)

  // Use default copy/move constructors.
  Optional(const Optional& rhs) = default;
  Optional& operator=(const Optional& rhs) = default;
  Optional(Optional&& rhs) noexcept = default;
  Optional& operator=(Optional&& rhs) noexcept = default;

  // Allow assignment from T.
  Optional& operator=(T value) noexcept {
    value_ = value;
    return *this;
  }

  // Allow assignment from std::nullopt_t.
  Optional& operator=(std::nullopt_t /*unused*/) noexcept {
    value_.reset();
    return *this;
  }

  //
  // Basic std::optional functionality.
  //

  bool has_value() const { return value_.has_value(); }

  const T& value() const { return *value_; }
  T& value() { return *value_; }

  const T& operator*() const { return *value_; }
  T& operator*() { return *value_; }

  T value_or(T default_value) const { return value_.value_or(default_value); }

  void reset() { value_.reset(); }

  template <typename... Args>
  T& emplace(Args&&... args) {
    return value_.emplace(std::forward<Args>(args)...);
  }

 private:
  std::optional<T> value_;

  // Allow operator implementations to access private members.
  template <typename U>
  friend bool operator==(const Optional<U>& lhs, const Optional<U>& rhs);
  template <typename U>
  friend bool operator!=(const Optional<U>& lhs, const Optional<U>& rhs);
};

// Forward the IO stream operator>> to the inner value of an Optional<T>.
template <typename T>
std::istream& operator>>(std::istream& is, Optional<T>& result) {
  is >> result.emplace();
  return is;
}

template <typename T>
bool operator==(const Optional<T>& lhs, const Optional<T>& rhs) {
  return lhs.value_ == rhs.value_;
}

template <typename T>
bool operator!=(const Optional<T>& lhs, const Optional<T>& rhs) {
  return lhs.value_ != rhs.value_;
}

}  // namespace cmdline

#endif  // CMDLINE_OPTIONAL_H_
