// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PROC_TESTS_CHROMIUMOS_SYSCALLS_SYSCALL_MATCHERS_H_
#define SRC_PROC_TESTS_CHROMIUMOS_SYSCALLS_SYSCALL_MATCHERS_H_

// A modified version of gvisor's syscall matchers from test/util/test_util.h.

#include <string.h>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace internal {

template <typename E>
class SyscallSuccessMatcher {
 public:
  explicit SyscallSuccessMatcher(E expected) : expected_(::std::move(expected)) {}

  template <typename T>
  operator ::testing::Matcher<T>() const {
    // E is one of three things:
    // - T, or a type losslessly and implicitly convertible to T.
    // - A monomorphic Matcher<T>.
    // - A polymorphic matcher.
    // SafeMatcherCast handles any of the above correctly.
    //
    // Similarly, gMock will invoke this conversion operator to obtain a
    // monomorphic matcher (this is how polymorphic matchers are implemented).
    return ::testing::MakeMatcher(new Impl<T>(::testing::SafeMatcherCast<T>(expected_)));
  }

 private:
  template <typename T>
  class Impl : public ::testing::MatcherInterface<T> {
   public:
    explicit Impl(::testing::Matcher<T> matcher) : matcher_(::std::move(matcher)) {}

    bool MatchAndExplain(T const& rv,
                         ::testing::MatchResultListener* const listener) const override {
      if (rv == static_cast<decltype(rv)>(-1) && errno != 0) {
        *listener << "with errno " << strerror(errno);
        return false;
      }
      bool match = matcher_.MatchAndExplain(rv, listener);
      return match;
    }

    void DescribeTo(::std::ostream* const os) const override { matcher_.DescribeTo(os); }

    void DescribeNegationTo(::std::ostream* const os) const override {
      matcher_.DescribeNegationTo(os);
    }

   private:
    ::testing::Matcher<T> matcher_;
  };

 private:
  E expected_;
};

// A polymorphic matcher equivalent to ::testing::internal::AnyMatcher, except
// not in namespace ::testing::internal, and describing SyscallSucceeds()'s
// match constraints (which are enforced by SyscallSuccessMatcher::Impl).
class AnySuccessValueMatcher {
 public:
  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakeMatcher(new Impl<T>());
  }

 private:
  template <typename T>
  class Impl : public ::testing::MatcherInterface<T> {
   public:
    bool MatchAndExplain(T const& rv,
                         ::testing::MatchResultListener* const listener) const override {
      return true;
    }

    void DescribeTo(::std::ostream* const os) const override { *os << "not -1 (success)"; }

    void DescribeNegationTo(::std::ostream* const os) const override { *os << "-1 (failure)"; }
  };
};

class SyscallFailureMatcher {
 public:
  explicit SyscallFailureMatcher(::testing::Matcher<int> errno_matcher)
      : errno_matcher_(std::move(errno_matcher)) {}

  template <typename T>
  bool MatchAndExplain(T const& rv, ::testing::MatchResultListener* const listener) const {
    if (rv != static_cast<decltype(rv)>(-1)) {
      return false;
    }
    int actual_errno = errno;
    *listener << "with errno " << strerror(actual_errno);
    bool match = errno_matcher_.MatchAndExplain(actual_errno, listener);
    return match;
  }

  void DescribeTo(::std::ostream* const os) const {
    *os << "-1 (failure), with errno ";
    errno_matcher_.DescribeTo(os);
  }

  void DescribeNegationTo(::std::ostream* const os) const {
    *os << "not -1 (success), with errno ";
    errno_matcher_.DescribeNegationTo(os);
  }

 private:
  ::testing::Matcher<int> errno_matcher_;
};

class SpecificErrnoMatcher : public ::testing::MatcherInterface<int> {
 public:
  explicit SpecificErrnoMatcher(int const expected) : expected_(expected) {}

  bool MatchAndExplain(int const actual_errno,
                       ::testing::MatchResultListener* const listener) const override {
    return actual_errno == expected_;
  }

  void DescribeTo(::std::ostream* const os) const override { *os << strerror(expected_); }

  void DescribeNegationTo(::std::ostream* const os) const override {
    *os << "not " << strerror(expected_);
  }

 private:
  int const expected_;
};

inline ::testing::Matcher<int> SpecificErrno(int const expected) {
  return ::testing::MakeMatcher(new SpecificErrnoMatcher(expected));
}

}  // namespace internal

template <typename E>
inline internal::SyscallSuccessMatcher<E> SyscallSucceedsWithValue(E expected) {
  return internal::SyscallSuccessMatcher<E>(::std::move(expected));
}

inline internal::SyscallSuccessMatcher<internal::AnySuccessValueMatcher> SyscallSucceeds() {
  return SyscallSucceedsWithValue(internal::AnySuccessValueMatcher());
}

inline ::testing::PolymorphicMatcher<internal::SyscallFailureMatcher> SyscallFailsWithErrno(
    ::testing::Matcher<int> expected) {
  return ::testing::MakePolymorphicMatcher(internal::SyscallFailureMatcher(::std::move(expected)));
}

// Overload taking an int so that SyscallFailsWithErrno(<specific errno>) uses
// internal::SpecificErrno (which stringifies the errno) rather than
// ::testing::Eq (which doesn't).
inline ::testing::PolymorphicMatcher<internal::SyscallFailureMatcher> SyscallFailsWithErrno(
    int const expected) {
  return SyscallFailsWithErrno(internal::SpecificErrno(expected));
}

inline ::testing::PolymorphicMatcher<internal::SyscallFailureMatcher> SyscallFails() {
  return SyscallFailsWithErrno(::testing::Gt(0));
}

#endif  // SRC_PROC_TESTS_CHROMIUMOS_SYSCALLS_SYSCALL_MATCHERS_H_
