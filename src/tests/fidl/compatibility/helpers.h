// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A compilation of helpers utilities to support running and validating the compatibility tests.

#ifndef SRC_TESTS_FIDL_COMPATIBILITY_HELPERS_H_
#define SRC_TESTS_FIDL_COMPATIBILITY_HELPERS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>

#include <map>
#include <random>
#include <vector>

#include <gtest/gtest.h>
#include <re2/re2.h>
#include <src/lib/fxl/strings/utf_codecs.h>
#include <src/tests/fidl/compatibility/hlcpp_client_app.h>

using fidl::test::compatibility::this_is_a_struct;
using fidl::test::compatibility::this_is_a_table;
using fidl::test::compatibility::this_is_a_union;
using fidl::test::compatibility::this_is_a_xunion;

namespace fidl_test_compatibility_helpers {

// Want a size small enough that it doesn't get too big to transmit but
// large enough to exercise interesting code paths.
constexpr uint8_t kArbitraryVectorSize = 3;

// This is used as a literal constant in compatibility_test_service.fidl.
constexpr uint8_t kArbitraryConstant = 2;

// A predicate function that returns true if the specified proxy + server pair should be tested for
// a given test run.
using AllowImplPair =
    std::function<bool(const std::string& proxy_url, const std::string& server_url)>;

// A simple list of implementations to be tested.
using Impls = std::vector<std::string>;

// A summary of findings to be printed as human-readable output.
using Summary = std::map<std::string, bool>;

// A test setup and executing function.
using TestBody = std::function<void(async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                                    const std::string& server_url, const std::string& proxy_url)>;

// Returns an |AllowImplPair| predicate that returns false if ANY of the provided list of
// substrings is found in the implementation list.
AllowImplPair Exclude(std::initializer_list<const char*> substrings);

// Get the short name of the language binding being tested, like "rust" or "cpp".
std::string ExtractShortName(const std::string& pkg_url);

// Run a test for all possible proxy + server combinations.
void ForAllImpls(Impls impls, TestBody body);

// Only test some proxy + server combinations, using an |AllowImplPair| predicate function to
// determine whether or not the particular proxy + server combination should be executed.
void ForSomeImpls(Impls impls, AllowImplPair allow, TestBody body);

// Parse the input args to build a list of binding implementations being tested. Returns false if no
// viable implementation names are found in the passed in command line arguments.
bool GetImplsUnderTest(int argc, char** argv, Impls* out_impls);

// Mint a simple handle for test-case building purposes.
zx::handle Handle();

// Compare two handles for equality.
::testing::AssertionResult HandlesEq(const zx::object_base& a, const zx::object_base& b);

// Prints a summary of the tests performed, and their results, to the terminal.
void PrintSummary(const Summary& summary);

// Random UTF8 string generator, with a byte (not character!) length of |count|.
std::string RandomUTF8(size_t count, std::default_random_engine& rand_engine);

// A generic class for generating random data for a FIDL type.
class DataGenerator {
 public:
  DataGenerator(int seed) : rand_engine_(seed) {}

  template <typename T>
  T choose(T a, T b) {
    if (next<bool>()) {
      return a;
    } else {
      return b;
    }
  }

  // Note: uniform_int_distribution is undefined behavior for integral types
  // smaller than short.
  template <typename T>
  struct UniformDistType {
    using Type = T;
  };
  template <>
  struct UniformDistType<bool> {
    using Type = uint16_t;
  };
  template <>
  struct UniformDistType<uint8_t> {
    using Type = uint16_t;
  };
  template <>
  struct UniformDistType<int8_t> {
    using Type = int16_t;
  };

  template <typename T>
  std::enable_if_t<std::is_integral_v<T>, T> next() {
    return static_cast<T>(std::uniform_int_distribution<typename UniformDistType<T>::Type>(
        0, std::numeric_limits<T>::max())(rand_engine_));
  }

  template <typename T>
  std::enable_if_t<std::is_floating_point_v<T>, T> next() {
    return std::uniform_real_distribution<T>{}(rand_engine_);
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, std::string>, T> next(size_t count = kArbitraryConstant) {
    std::string random_string;
    random_string.reserve(count);
    do {
      // Generate a random 32 bit unsigned int to use a the code point.
      uint32_t code_point = next<uint32_t>();
      // Mask the random number so that it can be encoded into the number of
      // bytes remaining.
      size_t remaining = count - random_string.size();
      if (remaining == 1) {
        code_point &= 0x7F;
      } else if (remaining == 2) {
        code_point &= 0x7FF;
      } else if (remaining == 3) {
        code_point &= 0xFFFF;
      } else {
        // Mask to fall within the general range of code points.
        code_point &= 0x1FFFFF;
      }
      // Check that it's really a valid code point, otherwise try again.
      if (!fxl::IsValidCodepoint(code_point)) {
        continue;
      }
      // Add the character to the random string.
      fxl::WriteUnicodeCharacter(code_point, &random_string);
      FX_CHECK(random_string.size() <= count);
    } while (random_string.size() < count);
    return random_string;
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, fidl::StringPtr>, T> next(size_t count = kArbitraryConstant) {
    return nullable<fidl::StringPtr>(fidl::StringPtr(), [this, count]() -> fidl::StringPtr {
      return fidl::StringPtr(next<std::string>(count));
    });
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, zx::handle>, T> next(bool nullable = false) {
    if (!nullable || next<bool>()) {
      zx_handle_t raw_event;
      const zx_status_t status = zx_event_create(0u, &raw_event);
      // Can't use gtest ASSERT_EQ because we're in a non-void function.
      ZX_ASSERT_MSG(status == ZX_OK, "status = %d", status);
      return zx::handle(raw_event);
    } else {
      return zx::handle(0);
    }
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, this_is_a_struct>, T> next() {
    this_is_a_struct value{};
    value.s = next<std::string>();
    return value;
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, std::unique_ptr<this_is_a_struct>>, T> next() {
    return nullable<std::unique_ptr<this_is_a_struct>>(
        nullptr, [this]() { return std::make_unique<this_is_a_struct>(next<this_is_a_struct>()); });
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, this_is_a_table>, T> next() {
    this_is_a_table value{};
    value.set_s(next<std::string>());
    return value;
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, this_is_a_union>, T> next() {
    this_is_a_union value{};
    if (next<bool>()) {
      value.set_b(next<bool>());
    } else {
      value.set_s(next<std::string>());
    }
    return value;
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, std::unique_ptr<this_is_a_union>>, T> next() {
    return nullable<std::unique_ptr<this_is_a_union>>(
        nullptr, [this]() { return std::make_unique<this_is_a_union>(next<this_is_a_union>()); });
  }

  template <typename T>
  std::enable_if_t<std::is_same_v<T, this_is_a_xunion>, T> next() {
    this_is_a_xunion value{};
    if (next<bool>()) {
      value.set_b(next<bool>());
    } else {
      value.set_s(next<std::string>());
    }
    return value;
  }

 private:
  std::default_random_engine rand_engine_;
  template <typename T>
  T nullable(T null_value, std::function<T(void)> generate_value) {
    if (next<bool>()) {
      return generate_value();
    } else {
      return null_value;
    }
  }
};

}  // namespace fidl_test_compatibility_helpers

#endif  // SRC_TESTS_FIDL_COMPATIBILITY_HELPERS_H_
