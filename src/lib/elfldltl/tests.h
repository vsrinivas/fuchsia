// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_TESTS_H_
#define SRC_LIB_ELFLDLTL_TESTS_H_

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/layout.h>

#include <functional>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>

#include <zxtest/zxtest.h>

template <class... Elf>
struct TestAllFormatsHelper {
  template <typename Test>
  void OneTest(Test&& test) const {
    ASSERT_NO_FATAL_FAILURE((test(Elf{}), ...));
  }

  template <typename... Test>
  void operator()(Test&&... tests) const {
    ASSERT_NO_FATAL_FAILURE((OneTest(tests), ...));
  }
};

template <typename... Test>
inline void TestAllFormats(Test&&... test) {
  elfldltl::AllFormats<TestAllFormatsHelper>{}(std::forward<Test>(test)...);
}

// This helper object is instantiated with the expected error string and its
// diag() method returns a Diagnostics object.  When the helper object goes out
// of scope, it will assert that the Diagnostics object got the expected one
// error logged.
template <typename... Args>
class ExpectedSingleError {
 public:
  explicit ExpectedSingleError(Args&&... args) : expected_(args...) {}

  auto& diag() { return diag_; }

  template <typename... Ts>
  bool operator()(Ts&&... args) {
    if constexpr (sizeof...(Args) != sizeof...(Ts)) {
      ADD_FAILURE("Expected %zu args, got %zu\n", sizeof...(Args), sizeof...(Ts));
    } else {
      Check(std::make_tuple(args...), std::make_index_sequence<sizeof...(Args)>());
    }
    return true;
  }

 private:
  template <typename T, typename T2 = void>
  struct ExpectedType {
    using type = T;
  };

  template <typename T>
  struct ExpectedType<T, std::enable_if_t<std::is_constructible_v<std::string_view, T>>> {
    using type = std::string_view;
  };

  template <typename T>
  struct ExpectedType<T, std::enable_if_t<std::is_integral_v<T>>> {
    using type = uint64_t;
  };

  template <typename Tuple, size_t... I>
  void Check(Tuple&& args, std::index_sequence<I...> seq) {
    ([&]() { EXPECT_EQ(std::get<I>(args), std::get<I>(expected_), "argument %zu", I); }(), ...);
  }

 public:
  template <typename T>
  using expected_t = typename ExpectedType<T>::type;

 private:
  // Diagnostic flags for signaling as much information as possible.
  static constexpr elfldltl::DiagnosticsFlags kFlags = {
      .multiple_errors = true,
      .warnings_are_errors = false,
      .extra_checking = true,
  };

  std::tuple<Args...> expected_;
  elfldltl::Diagnostics<std::reference_wrapper<ExpectedSingleError>> diag_{*this, kFlags};
};

template <typename... Args>
ExpectedSingleError(Args...)
    -> ExpectedSingleError<ExpectedSingleError<>::expected_t<std::decay_t<Args>>...>;

constexpr auto ExpectOkDiagnostics() {
  auto fail = [](std::string_view error, auto&&... args) {
    std::stringstream os;
    elfldltl::OstreamDiagnostics(os).FormatError(error, args...);
    std::string message = os.str();
    if (message.back() == '\n')
      message.pop_back();
    ADD_FAILURE("Expected no diagnostics, got \"%.*s\"", static_cast<int>(message.size()),
                message.data());
    return false;
    ;
  };
  return elfldltl::Diagnostics(fail, elfldltl::DiagnosticsFlags{.extra_checking = true});
}

#endif  // SRC_LIB_ELFLDLTL_TESTS_H_
