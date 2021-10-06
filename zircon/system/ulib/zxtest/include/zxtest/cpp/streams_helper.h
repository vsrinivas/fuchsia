// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_CPP_STREAMS_HELPER_H_
#define ZXTEST_CPP_STREAMS_HELPER_H_

#include <iostream>
#include <optional>
#include <sstream>

#include <zxtest/base/assertion.h>
#include <zxtest/base/types.h>

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_status_value, status_value, zx_status_t (C::*)() const);
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_status, status, zx_status_t (C::*)() const);

#define _RETURN_TAG_true return Tag()&
#define _RETURN_TAG_false
#define _RETURN_TAG(val) _RETURN_TAG_##val

template <typename T>
zx_status_t GetStatus(const T& status) {
  if constexpr (has_status_value_v<T>) {
    return status.status_value();
  } else if constexpr (has_status_v<T>) {
    return status.status();
  } else {
    return status;
  }
}

struct Tag {};

class StreamableBase {
 public:
  StreamableBase(const zxtest::SourceLocation location)
      : stream_(std::stringstream("")), location_(location) {}

  // Lower precedence operator that returns void, such that the following
  // expressions are valid in functions that return void:
  //
  //  return Tag{} & StreamableBase{};
  //  return Tag{} & StreamableBase{} << "Stream operators are higher precedence than &";
  //
  friend void operator&(Tag, const StreamableBase&) {}

  template <typename T>
  StreamableBase& operator<<(T t) {
    stream_ << t;
    return *this;
  }

 protected:
  std::stringstream stream_;
  const zxtest::SourceLocation location_;
};

class StreamableFail : public StreamableBase {
 public:
  StreamableFail(const zxtest::SourceLocation location, bool is_fatal)
      : StreamableBase(location), is_fatal_(is_fatal) {}

  virtual ~StreamableFail() {
    zxtest::Runner::GetInstance()->NotifyAssertion(
        zxtest::Assertion(stream_.str(), location_, is_fatal_));
  }

 private:
  bool is_fatal_ = false;
};

class StreamableAssertion : public StreamableBase {
 public:
  template <typename Actual, typename Expected, typename CompareOp, typename PrintActual,
            typename PrintExpected>
  StreamableAssertion(const Actual& actual, const Expected& expected, const char* actual_symbol,
                      const char* expected_symbol, const zxtest::SourceLocation location,
                      bool is_fatal, const CompareOp& compare, const PrintActual& print_actual,
                      const PrintExpected& print_expected)
      : StreamableBase(location),
        actual_symbol_(actual_symbol),
        expected_symbol_(expected_symbol),
        is_fatal_(is_fatal) {
    actual_value_ = print_actual(actual);
    expected_value_ = print_expected(expected);
    is_triggered_ = !compare(actual, expected);
  }

  ~StreamableAssertion() {
    if (is_triggered_) {
      zxtest::Runner::GetInstance()->NotifyAssertion(
          zxtest::Assertion(stream_.str(), expected_symbol_.value(), expected_value_.value(),
                            actual_symbol_.value(), actual_value_.value(), location_, is_fatal_));
    }
  }

  bool IsTriggered() { return is_triggered_; }

 private:
  std::optional<fbl::String> actual_value_;
  std::optional<fbl::String> expected_value_;
  std::optional<const char*> actual_symbol_;
  std::optional<const char*> expected_symbol_;
  bool is_fatal_ = false;
  bool is_triggered_ = true;
};

class StreamableSkip : public StreamableBase {
 public:
  StreamableSkip(const zxtest::SourceLocation location) : StreamableBase(location) {}

  ~StreamableSkip() {
    zxtest::Message message(stream_.str(), location_);
    zxtest::Runner::GetInstance()->SkipCurrent(message);
  }
};

#endif  // ZXTEST_CPP_STREAMS_HELPER_H_
