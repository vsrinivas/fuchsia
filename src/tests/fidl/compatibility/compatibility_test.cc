// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <random>
#include <regex>
#include <vector>

#include <fidl/test/compatibility/cpp/fidl.h>
#include <gtest/gtest.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/utf_codecs.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/tests/fidl/compatibility/hlcpp_client_app.h"

using fidl::VectorPtr;
using fidl::test::compatibility::AllTypesTable;
using fidl::test::compatibility::AllTypesXunion;
using fidl::test::compatibility::ArraysStruct;
using fidl::test::compatibility::Echo_EchoArraysWithError_Response;
using fidl::test::compatibility::Echo_EchoArraysWithError_Result;
using fidl::test::compatibility::Echo_EchoStructWithError_Response;
using fidl::test::compatibility::Echo_EchoStructWithError_Result;
using fidl::test::compatibility::Echo_EchoTableWithError_Response;
using fidl::test::compatibility::Echo_EchoTableWithError_Result;
using fidl::test::compatibility::Echo_EchoVectorsWithError_Response;
using fidl::test::compatibility::Echo_EchoVectorsWithError_Result;
using fidl::test::compatibility::Echo_EchoXunionsWithError_Response;
using fidl::test::compatibility::Echo_EchoXunionsWithError_Result;
using fidl::test::compatibility::RespondWith;
using fidl::test::compatibility::Struct;
using fidl::test::compatibility::this_is_a_struct;
using fidl::test::compatibility::this_is_a_table;
using fidl::test::compatibility::this_is_a_union;
using fidl::test::compatibility::this_is_a_xunion;
using fidl::test::compatibility::VectorsStruct;
using std::string;

namespace {

// Want a size small enough that it doesn't get too big to transmit but
// large enough to exercise interesting code paths.
constexpr uint8_t kArbitraryVectorSize = 3;
// This is used as a literal constant in compatibility_test_service.fidl.
constexpr uint8_t kArbitraryConstant = 2;

constexpr char kUsage[] = ("Usage:\n  fidl_compatibility_test foo_server bar_server\n");

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

  template <typename T>
  std::enable_if_t<std::is_integral_v<T>, T> next() {
    return std::uniform_int_distribution<T>{}(rand_engine_);
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

zx::handle Handle() {
  zx_handle_t raw_event;
  const zx_status_t status = zx_event_create(0u, &raw_event);
  // Can't use gtest ASSERT_EQ because we're in a non-void function.
  ZX_ASSERT_MSG(status == ZX_OK, "status = %d", status);
  return zx::handle(raw_event);
}

::testing::AssertionResult HandlesEq(const zx::object_base& a, const zx::object_base& b) {
  if (a.is_valid() != b.is_valid()) {
    return ::testing::AssertionFailure()
           << "Handles are not equally valid :" << a.is_valid() << " vs " << b.is_valid();
  }
  if (!a.is_valid()) {
    return ::testing::AssertionSuccess() << "Both handles invalid";
  }
  zx_info_handle_basic_t a_info, b_info;
  zx_status_t status =
      zx_object_get_info(a.get(), ZX_INFO_HANDLE_BASIC, &a_info, sizeof(a_info), nullptr, nullptr);
  if (ZX_OK != status) {
    return ::testing::AssertionFailure() << "zx_object_get_info(a) returned " << status;
  }
  status =
      zx_object_get_info(b.get(), ZX_INFO_HANDLE_BASIC, &b_info, sizeof(b_info), nullptr, nullptr);
  if (ZX_OK != status) {
    return ::testing::AssertionFailure() << "zx_object_get_info(b) returned " << status;
  }
  if (a_info.koid != b_info.koid) {
    return ::testing::AssertionFailure() << std::endl
                                         << "a_info.koid is: " << a_info.koid << std::endl
                                         << "b_info.koid is: " << b_info.koid;
  }
  return ::testing::AssertionSuccess();
}

void ExpectEq(const Struct& a, const Struct& b) {
  // primitive types
  EXPECT_EQ(a.primitive_types.b, b.primitive_types.b);
  EXPECT_EQ(a.primitive_types.i8, b.primitive_types.i8);
  EXPECT_EQ(a.primitive_types.i16, b.primitive_types.i16);
  EXPECT_EQ(a.primitive_types.i32, b.primitive_types.i32);
  EXPECT_EQ(a.primitive_types.i64, b.primitive_types.i64);
  EXPECT_EQ(a.primitive_types.u8, b.primitive_types.u8);
  EXPECT_EQ(a.primitive_types.u16, b.primitive_types.u16);
  EXPECT_EQ(a.primitive_types.u32, b.primitive_types.u32);
  EXPECT_EQ(a.primitive_types.u64, b.primitive_types.u64);
  EXPECT_EQ(a.primitive_types.f32, b.primitive_types.f32);
  EXPECT_EQ(a.primitive_types.f64, b.primitive_types.f64);

  // arrays
  EXPECT_EQ(a.arrays.b_0[0], b.arrays.b_0[0]);
  EXPECT_EQ(a.arrays.i8_0[0], b.arrays.i8_0[0]);
  EXPECT_EQ(a.arrays.i16_0[0], b.arrays.i16_0[0]);
  EXPECT_EQ(a.arrays.i32_0[0], b.arrays.i32_0[0]);
  EXPECT_EQ(a.arrays.i64_0[0], b.arrays.i64_0[0]);
  EXPECT_EQ(a.arrays.u8_0[0], b.arrays.u8_0[0]);
  EXPECT_EQ(a.arrays.u16_0[0], b.arrays.u16_0[0]);
  EXPECT_EQ(a.arrays.u32_0[0], b.arrays.u32_0[0]);
  EXPECT_EQ(a.arrays.u64_0[0], b.arrays.u64_0[0]);
  EXPECT_EQ(a.arrays.f32_0[0], b.arrays.f32_0[0]);
  EXPECT_EQ(a.arrays.f64_0[0], b.arrays.f64_0[0]);
  EXPECT_TRUE(HandlesEq(a.arrays.handle_0[0], b.arrays.handle_0[0]));
  for (uint32_t i = 0; i < fidl::test::compatibility::arrays_size; ++i) {
    EXPECT_EQ(a.arrays.b_1[i], b.arrays.b_1[i]);
    EXPECT_EQ(a.arrays.i8_1[i], b.arrays.i8_1[i]);
    EXPECT_EQ(a.arrays.i16_1[i], b.arrays.i16_1[i]);
    EXPECT_EQ(a.arrays.i32_1[i], b.arrays.i32_1[i]);
    EXPECT_EQ(a.arrays.i64_1[i], b.arrays.i64_1[i]);
    EXPECT_EQ(a.arrays.u8_1[i], b.arrays.u8_1[i]);
    EXPECT_EQ(a.arrays.u16_1[i], b.arrays.u16_1[i]);
    EXPECT_EQ(a.arrays.u32_1[i], b.arrays.u32_1[i]);
    EXPECT_EQ(a.arrays.u64_1[i], b.arrays.u64_1[i]);
    EXPECT_EQ(a.arrays.f32_1[i], b.arrays.f32_1[i]);
    EXPECT_EQ(a.arrays.f64_1[i], b.arrays.f64_1[i]);
    EXPECT_TRUE(HandlesEq(a.arrays.handle_1[i], b.arrays.handle_1[i]));
  }
  // arrays_2d
  for (uint32_t i = 0; i < fidl::test::compatibility::arrays_size; ++i) {
    for (uint32_t j = 0; j < kArbitraryConstant; ++j) {
      EXPECT_EQ(a.arrays_2d.b[i][j], b.arrays_2d.b[i][j]);
      EXPECT_EQ(a.arrays_2d.i8[i][j], b.arrays_2d.i8[i][j]);
      EXPECT_EQ(a.arrays_2d.i16[i][j], b.arrays_2d.i16[i][j]);
      EXPECT_EQ(a.arrays_2d.i32[i][j], b.arrays_2d.i32[i][j]);
      EXPECT_EQ(a.arrays_2d.i64[i][j], b.arrays_2d.i64[i][j]);
      EXPECT_EQ(a.arrays_2d.u8[i][j], b.arrays_2d.u8[i][j]);
      EXPECT_EQ(a.arrays_2d.u16[i][j], b.arrays_2d.u16[i][j]);
      EXPECT_EQ(a.arrays_2d.u32[i][j], b.arrays_2d.u32[i][j]);
      EXPECT_EQ(a.arrays_2d.u64[i][j], b.arrays_2d.u64[i][j]);
      EXPECT_EQ(a.arrays_2d.f32[i][j], b.arrays_2d.f32[i][j]);
      EXPECT_EQ(a.arrays_2d.f64[i][j], b.arrays_2d.f64[i][j]);
      EXPECT_TRUE(HandlesEq(a.arrays_2d.handle_handle[i][j], b.arrays_2d.handle_handle[i][j]));
    }
  }
  // vectors
  EXPECT_EQ(a.vectors.b_0, b.vectors.b_0);
  EXPECT_EQ(a.vectors.i8_0, b.vectors.i8_0);
  EXPECT_EQ(a.vectors.i16_0, b.vectors.i16_0);
  EXPECT_EQ(a.vectors.i32_0, b.vectors.i32_0);
  EXPECT_EQ(a.vectors.i64_0, b.vectors.i64_0);
  EXPECT_EQ(a.vectors.u8_0, b.vectors.u8_0);
  EXPECT_EQ(a.vectors.u16_0, b.vectors.u16_0);
  EXPECT_EQ(a.vectors.u32_0, b.vectors.u32_0);
  EXPECT_EQ(a.vectors.u64_0, b.vectors.u64_0);
  EXPECT_EQ(a.vectors.f32_0, b.vectors.f32_0);
  EXPECT_EQ(a.vectors.f64_0, b.vectors.f64_0);
  for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
    EXPECT_TRUE(HandlesEq(a.vectors.handle_0[i], b.vectors.handle_0[i]));
  }

  for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
    EXPECT_EQ(a.vectors.b_1[i], b.vectors.b_1[i]);
    EXPECT_EQ(a.vectors.i8_1[i], b.vectors.i8_1[i]);
    EXPECT_EQ(a.vectors.i16_1[i], b.vectors.i16_1[i]);
    EXPECT_EQ(a.vectors.i32_1[i], b.vectors.i32_1[i]);
    EXPECT_EQ(a.vectors.i64_1[i], b.vectors.i64_1[i]);
    EXPECT_EQ(a.vectors.u8_1[i], b.vectors.u8_1[i]);
    EXPECT_EQ(a.vectors.u16_1[i], b.vectors.u16_1[i]);
    EXPECT_EQ(a.vectors.u32_1[i], b.vectors.u32_1[i]);
    EXPECT_EQ(a.vectors.u64_1[i], b.vectors.u64_1[i]);
    EXPECT_EQ(a.vectors.f32_1[i], b.vectors.f32_1[i]);
    EXPECT_EQ(a.vectors.f64_1[i], b.vectors.f64_1[i]);
    for (uint8_t j = 0; j < kArbitraryConstant; ++j) {
      EXPECT_TRUE(HandlesEq(a.vectors.handle_1[i][j], b.vectors.handle_1[i][j]));
    }
  }

  EXPECT_EQ(a.vectors.b_sized_0, b.vectors.b_sized_0);
  EXPECT_EQ(a.vectors.i8_sized_0, b.vectors.i8_sized_0);
  EXPECT_EQ(a.vectors.i16_sized_0, b.vectors.i16_sized_0);
  EXPECT_EQ(a.vectors.i32_sized_0, b.vectors.i32_sized_0);
  EXPECT_EQ(a.vectors.i64_sized_0, b.vectors.i64_sized_0);
  EXPECT_EQ(a.vectors.u8_sized_0, b.vectors.u8_sized_0);
  EXPECT_EQ(a.vectors.u16_sized_0, b.vectors.u16_sized_0);
  EXPECT_EQ(a.vectors.u32_sized_0, b.vectors.u32_sized_0);
  EXPECT_EQ(a.vectors.u64_sized_0, b.vectors.u64_sized_0);
  EXPECT_EQ(a.vectors.f32_sized_0, b.vectors.f32_sized_0);
  EXPECT_EQ(a.vectors.f64_sized_0, b.vectors.f64_sized_0);
  EXPECT_TRUE(HandlesEq(a.vectors.handle_sized_0[0], b.vectors.handle_sized_0[0]));

  EXPECT_EQ(a.vectors.b_sized_1, b.vectors.b_sized_1);
  EXPECT_EQ(a.vectors.i8_sized_1, b.vectors.i8_sized_1);
  EXPECT_EQ(a.vectors.i16_sized_1, b.vectors.i16_sized_1);
  EXPECT_EQ(a.vectors.i32_sized_1, b.vectors.i32_sized_1);
  EXPECT_EQ(a.vectors.i64_sized_1, b.vectors.i64_sized_1);
  EXPECT_EQ(a.vectors.u8_sized_1, b.vectors.u8_sized_1);
  EXPECT_EQ(a.vectors.u16_sized_1, b.vectors.u16_sized_1);
  EXPECT_EQ(a.vectors.u32_sized_1, b.vectors.u32_sized_1);
  EXPECT_EQ(a.vectors.u64_sized_1, b.vectors.u64_sized_1);
  EXPECT_EQ(a.vectors.f32_sized_1, b.vectors.f32_sized_1);
  EXPECT_EQ(a.vectors.f64_sized_1, b.vectors.f64_sized_1);
  for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
    EXPECT_TRUE(HandlesEq(a.vectors.handle_sized_1[i], b.vectors.handle_sized_1[i]));
  }

  for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
    EXPECT_EQ(a.vectors.b_sized_2[i], b.vectors.b_sized_2[i]);
    EXPECT_EQ(a.vectors.i8_sized_2[i], b.vectors.i8_sized_2[i]);
    EXPECT_EQ(a.vectors.i16_sized_2[i], b.vectors.i16_sized_2[i]);
    EXPECT_EQ(a.vectors.i32_sized_2[i], b.vectors.i32_sized_2[i]);
    EXPECT_EQ(a.vectors.i64_sized_2[i], b.vectors.i64_sized_2[i]);
    EXPECT_EQ(a.vectors.u8_sized_2[i], b.vectors.u8_sized_2[i]);
    EXPECT_EQ(a.vectors.u16_sized_2[i], b.vectors.u16_sized_2[i]);
    EXPECT_EQ(a.vectors.u32_sized_2[i], b.vectors.u32_sized_2[i]);
    EXPECT_EQ(a.vectors.u64_sized_2[i], b.vectors.u64_sized_2[i]);
    EXPECT_EQ(a.vectors.f32_sized_2[i], b.vectors.f32_sized_2[i]);
    EXPECT_EQ(a.vectors.f64_sized_2[i], b.vectors.f64_sized_2[i]);
    for (uint8_t j = 0; j < kArbitraryConstant; ++j) {
      EXPECT_TRUE(HandlesEq(a.vectors.handle_sized_2[i][j], b.vectors.handle_sized_2[i][j]));
    }
  }

  EXPECT_EQ(a.vectors.b_nullable_0.has_value(), b.vectors.b_nullable_0.has_value());
  EXPECT_EQ(a.vectors.i8_nullable_0.has_value(), b.vectors.i8_nullable_0.has_value());
  EXPECT_EQ(a.vectors.i16_nullable_0.has_value(), b.vectors.i16_nullable_0.has_value());
  EXPECT_EQ(a.vectors.i32_nullable_0.has_value(), b.vectors.i32_nullable_0.has_value());
  EXPECT_EQ(a.vectors.i64_nullable_0.has_value(), b.vectors.i64_nullable_0.has_value());
  EXPECT_EQ(a.vectors.u8_nullable_0.has_value(), b.vectors.u8_nullable_0.has_value());
  EXPECT_EQ(a.vectors.u16_nullable_0.has_value(), b.vectors.u16_nullable_0.has_value());
  EXPECT_EQ(a.vectors.u32_nullable_0.has_value(), b.vectors.u32_nullable_0.has_value());
  EXPECT_EQ(a.vectors.u64_nullable_0.has_value(), b.vectors.u64_nullable_0.has_value());
  EXPECT_EQ(a.vectors.f32_nullable_0.has_value(), b.vectors.f32_nullable_0.has_value());
  EXPECT_EQ(a.vectors.f64_nullable_0.has_value(), b.vectors.f64_nullable_0.has_value());
  EXPECT_EQ(a.vectors.handle_nullable_0.has_value(), b.vectors.handle_nullable_0.has_value());

  EXPECT_EQ(a.vectors.b_nullable_1.has_value(), b.vectors.b_nullable_1.has_value());
  EXPECT_EQ(a.vectors.i8_nullable_1.has_value(), b.vectors.i8_nullable_1.has_value());
  EXPECT_EQ(a.vectors.i16_nullable_1.has_value(), b.vectors.i16_nullable_1.has_value());
  EXPECT_EQ(a.vectors.i32_nullable_1.has_value(), b.vectors.i32_nullable_1.has_value());
  EXPECT_EQ(a.vectors.i64_nullable_1.has_value(), b.vectors.i64_nullable_1.has_value());
  EXPECT_EQ(a.vectors.u8_nullable_1.has_value(), b.vectors.u8_nullable_1.has_value());
  EXPECT_EQ(a.vectors.u16_nullable_1.has_value(), b.vectors.u16_nullable_1.has_value());
  EXPECT_EQ(a.vectors.u32_nullable_1.has_value(), b.vectors.u32_nullable_1.has_value());
  EXPECT_EQ(a.vectors.u64_nullable_1.has_value(), b.vectors.u64_nullable_1.has_value());
  EXPECT_EQ(a.vectors.f32_nullable_1.has_value(), b.vectors.f32_nullable_1.has_value());
  EXPECT_EQ(a.vectors.f64_nullable_1.has_value(), b.vectors.f64_nullable_1.has_value());
  EXPECT_EQ(a.vectors.handle_nullable_1.has_value(), b.vectors.handle_nullable_1.has_value());

  ASSERT_TRUE(a.vectors.i8_nullable_1.has_value());
  ASSERT_TRUE(b.vectors.i8_nullable_1.has_value());
  for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
    EXPECT_EQ(a.vectors.i8_nullable_1->at(i), b.vectors.i8_nullable_1->at(i));
  }

  EXPECT_EQ(a.vectors.b_nullable_sized_0.has_value(), b.vectors.b_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.i8_nullable_sized_0.has_value(), b.vectors.i8_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.i16_nullable_sized_0.has_value(), b.vectors.i16_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.i32_nullable_sized_0.has_value(), b.vectors.i32_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.i64_nullable_sized_0.has_value(), b.vectors.i64_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.u8_nullable_sized_0.has_value(), b.vectors.u8_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.u16_nullable_sized_0.has_value(), b.vectors.u16_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.u32_nullable_sized_0.has_value(), b.vectors.u32_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.u64_nullable_sized_0.has_value(), b.vectors.u64_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.f32_nullable_sized_0.has_value(), b.vectors.f32_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.f64_nullable_sized_0.has_value(), b.vectors.f64_nullable_sized_0.has_value());
  EXPECT_EQ(a.vectors.handle_nullable_sized_0.has_value(),
            b.vectors.handle_nullable_sized_0.has_value());

  if (a.vectors.i16_nullable_sized_0.has_value()) {
    EXPECT_EQ(a.vectors.i16_nullable_sized_0.value(), b.vectors.i16_nullable_sized_0.value());
  }

  EXPECT_EQ(a.vectors.b_nullable_sized_1.has_value(), b.vectors.b_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.i8_nullable_sized_1.has_value(), b.vectors.i8_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.i16_nullable_sized_1.has_value(), b.vectors.i16_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.i32_nullable_sized_1.has_value(), b.vectors.i32_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.i64_nullable_sized_1.has_value(), b.vectors.i64_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.u8_nullable_sized_1.has_value(), b.vectors.u8_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.u16_nullable_sized_1.has_value(), b.vectors.u16_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.u32_nullable_sized_1.has_value(), b.vectors.u32_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.u64_nullable_sized_1.has_value(), b.vectors.u64_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.f32_nullable_sized_1.has_value(), b.vectors.f32_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.f64_nullable_sized_1.has_value(), b.vectors.f64_nullable_sized_1.has_value());
  EXPECT_EQ(a.vectors.handle_nullable_sized_1.has_value(),
            b.vectors.handle_nullable_sized_1.has_value());

  EXPECT_EQ(a.vectors.f64_nullable_sized_1.has_value(), b.vectors.f64_nullable_sized_1.has_value());
  if (a.vectors.f64_nullable_sized_1.has_value()) {
    EXPECT_EQ(a.vectors.f64_nullable_sized_1.value(), b.vectors.f64_nullable_sized_1.value());
  }

  EXPECT_EQ(a.vectors.b_nullable_sized_2.has_value(), b.vectors.b_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.i8_nullable_sized_2.has_value(), b.vectors.i8_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.i16_nullable_sized_2.has_value(), b.vectors.i16_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.i32_nullable_sized_2.has_value(), b.vectors.i32_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.i64_nullable_sized_2.has_value(), b.vectors.i64_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.u8_nullable_sized_2.has_value(), b.vectors.u8_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.u16_nullable_sized_2.has_value(), b.vectors.u16_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.u32_nullable_sized_2.has_value(), b.vectors.u32_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.u64_nullable_sized_2.has_value(), b.vectors.u64_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.f32_nullable_sized_2.has_value(), b.vectors.f32_nullable_sized_2.has_value());
  EXPECT_EQ(a.vectors.f64_nullable_sized_2.has_value(), b.vectors.f64_nullable_sized_2.has_value());
  EXPECT_TRUE(a.vectors.handle_nullable_sized_2.has_value());
  EXPECT_TRUE(b.vectors.handle_nullable_sized_2.has_value());

  for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
    for (uint8_t j = 0; j < kArbitraryConstant; ++j) {
      EXPECT_TRUE(HandlesEq(a.vectors.handle_nullable_sized_2.value()[i][j],
                            b.vectors.handle_nullable_sized_2.value()[i][j]));
    }
  }

  // handles
  EXPECT_TRUE(HandlesEq(a.handles.handle_handle, b.handles.handle_handle));
  EXPECT_TRUE(HandlesEq(a.handles.process_handle, b.handles.process_handle));
  EXPECT_TRUE(HandlesEq(a.handles.thread_handle, b.handles.thread_handle));
  EXPECT_TRUE(HandlesEq(a.handles.vmo_handle, b.handles.vmo_handle));
  EXPECT_TRUE(HandlesEq(a.handles.event_handle, b.handles.event_handle));
  EXPECT_TRUE(HandlesEq(a.handles.port_handle, b.handles.port_handle));
  EXPECT_TRUE(HandlesEq(a.handles.socket_handle, b.handles.socket_handle));
  EXPECT_TRUE(HandlesEq(a.handles.eventpair_handle, b.handles.eventpair_handle));
  EXPECT_TRUE(HandlesEq(a.handles.job_handle, b.handles.job_handle));
  EXPECT_TRUE(HandlesEq(a.handles.vmar_handle, b.handles.vmar_handle));
  EXPECT_TRUE(HandlesEq(a.handles.fifo_handle, b.handles.fifo_handle));
  EXPECT_TRUE(HandlesEq(a.handles.timer_handle, b.handles.timer_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_handle_handle, b.handles.nullable_handle_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_process_handle, b.handles.nullable_process_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_thread_handle, b.handles.nullable_thread_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_vmo_handle, b.handles.nullable_vmo_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_channel_handle, b.handles.nullable_channel_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_event_handle, b.handles.nullable_event_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_port_handle, b.handles.nullable_port_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_interrupt_handle, b.handles.nullable_interrupt_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_log_handle, b.handles.nullable_log_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_socket_handle, b.handles.nullable_socket_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_eventpair_handle, b.handles.nullable_eventpair_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_job_handle, b.handles.nullable_job_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_vmar_handle, b.handles.nullable_vmar_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_fifo_handle, b.handles.nullable_fifo_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_timer_handle, b.handles.nullable_timer_handle));

  // strings
  EXPECT_EQ(a.strings.s, b.strings.s);
  EXPECT_EQ(a.strings.size_0_s, b.strings.size_0_s);
  EXPECT_EQ(a.strings.size_1_s, b.strings.size_1_s);
  EXPECT_EQ(a.strings.nullable_size_0_s.has_value(), b.strings.nullable_size_0_s.has_value());
  if (a.strings.nullable_size_0_s.has_value() && b.strings.nullable_size_0_s.has_value()) {
    EXPECT_EQ(a.strings.nullable_size_0_s.value(), b.strings.nullable_size_0_s.value());
  }
  EXPECT_EQ(a.strings.nullable_size_1_s.has_value(), b.strings.nullable_size_1_s.has_value());

  // enums
  EXPECT_EQ(a.default_enum, b.default_enum);
  EXPECT_EQ(a.i8_enum, b.i8_enum);
  EXPECT_EQ(a.i16_enum, b.i16_enum);
  EXPECT_EQ(a.i32_enum, b.i32_enum);
  EXPECT_EQ(a.i64_enum, b.i64_enum);
  EXPECT_EQ(a.u8_enum, b.u8_enum);
  EXPECT_EQ(a.u16_enum, b.u16_enum);
  EXPECT_EQ(a.u32_enum, b.u32_enum);
  EXPECT_EQ(a.u64_enum, b.u64_enum);

  // bits
  EXPECT_EQ(a.default_bits, b.default_bits);
  EXPECT_EQ(a.u8_bits, b.u8_bits);
  EXPECT_EQ(a.u16_bits, b.u16_bits);
  EXPECT_EQ(a.u32_bits, b.u32_bits);
  EXPECT_EQ(a.u64_bits, b.u64_bits);

  // structs
  EXPECT_EQ(a.structs.s.s, b.structs.s.s);
  EXPECT_EQ(a.structs.nullable_s, b.structs.nullable_s);

  // empty structs
  EXPECT_TRUE(fidl::Equals(a.structs.es, b.structs.es));
  EXPECT_EQ(a.structs.es.__reserved, 0u);

  // unions
  EXPECT_EQ(a.unions.u.is_s(), b.unions.u.is_s());
  EXPECT_EQ(a.unions.u.s(), b.unions.u.s());
  EXPECT_EQ(a.unions.nullable_u->is_b(), b.unions.nullable_u->is_b());
  EXPECT_EQ(a.unions.nullable_u->b(), b.unions.nullable_u->b());

  // tables and xunions
  EXPECT_TRUE(fidl::Equals(a.table, b.table));
  EXPECT_TRUE(fidl::Equals(a.xunion_, b.xunion_));

  // bool
  EXPECT_EQ(a.b, b.b);
}

std::string RandomUTF8(size_t count, std::default_random_engine& rand_engine) {
  std::uniform_int_distribution<uint32_t> uint32_distribution;
  std::string random_string;
  random_string.reserve(count);
  do {
    // Generate a random 32 bit unsigned int to use a the code point.
    uint32_t code_point = uint32_distribution(rand_engine);
    // Mask the random number so that it can be encoded into the number of bytes
    // remaining.
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

void InitializeStruct(Struct* s) {
  // Prepare randomness.
  std::default_random_engine rand_engine;
  // Using randomness to avoid having to come up with varied values by hand.
  // Seed deterministically so that this function's outputs are predictable.
  rand_engine.seed(42);
  std::uniform_int_distribution<bool> bool_distribution;
  std::uniform_int_distribution<int8_t> int8_distribution;
  std::uniform_int_distribution<int16_t> int16_distribution;
  std::uniform_int_distribution<int32_t> int32_distribution;
  std::uniform_int_distribution<int64_t> int64_distribution;
  std::uniform_int_distribution<uint8_t> uint8_distribution;
  std::uniform_int_distribution<uint16_t> uint16_distribution;
  std::uniform_int_distribution<uint32_t> uint32_distribution;
  std::uniform_int_distribution<uint64_t> uint64_distribution;
  std::uniform_real_distribution<float> float_distribution;
  std::uniform_real_distribution<double> double_distribution;
  std::string random_string = RandomUTF8(fidl::test::compatibility::strings_size, rand_engine);
  std::string random_short_string = RandomUTF8(kArbitraryConstant, rand_engine);

  // primitive_types
  s->primitive_types.b = bool_distribution(rand_engine);
  s->primitive_types.i8 = int8_distribution(rand_engine);
  s->primitive_types.i16 = int16_distribution(rand_engine);
  s->primitive_types.i32 = int32_distribution(rand_engine);
  s->primitive_types.i64 = int64_distribution(rand_engine);
  s->primitive_types.u8 = uint8_distribution(rand_engine);
  s->primitive_types.u16 = uint16_distribution(rand_engine);
  s->primitive_types.u32 = uint32_distribution(rand_engine);
  s->primitive_types.u64 = uint64_distribution(rand_engine);
  s->primitive_types.f32 = float_distribution(rand_engine);
  s->primitive_types.f64 = double_distribution(rand_engine);

  // arrays
  s->arrays.b_0[0] = bool_distribution(rand_engine);
  s->arrays.i8_0[0] = int8_distribution(rand_engine);
  s->arrays.i16_0[0] = int16_distribution(rand_engine);
  s->arrays.i32_0[0] = int32_distribution(rand_engine);
  s->arrays.i64_0[0] = int64_distribution(rand_engine);
  s->arrays.u8_0[0] = uint8_distribution(rand_engine);
  s->arrays.u16_0[0] = uint16_distribution(rand_engine);
  s->arrays.u32_0[0] = uint32_distribution(rand_engine);
  s->arrays.u64_0[0] = uint64_distribution(rand_engine);
  s->arrays.f32_0[0] = float_distribution(rand_engine);
  s->arrays.f64_0[0] = double_distribution(rand_engine);
  s->arrays.handle_0[0] = Handle();

  for (uint32_t i = 0; i < fidl::test::compatibility::arrays_size; ++i) {
    s->arrays.b_1[i] = bool_distribution(rand_engine);
    s->arrays.i8_1[i] = int8_distribution(rand_engine);
    s->arrays.i16_1[i] = int16_distribution(rand_engine);
    s->arrays.i32_1[i] = int32_distribution(rand_engine);
    s->arrays.i64_1[i] = int64_distribution(rand_engine);
    s->arrays.u8_1[i] = uint8_distribution(rand_engine);
    s->arrays.u16_1[i] = uint16_distribution(rand_engine);
    s->arrays.u32_1[i] = uint32_distribution(rand_engine);
    s->arrays.u64_1[i] = uint64_distribution(rand_engine);
    s->arrays.f32_1[i] = float_distribution(rand_engine);
    s->arrays.f64_1[i] = double_distribution(rand_engine);
    s->arrays.handle_1[i] = Handle();
  }

  // arrays_2d
  for (uint32_t i = 0; i < fidl::test::compatibility::arrays_size; ++i) {
    for (uint32_t j = 0; j < kArbitraryConstant; ++j) {
      s->arrays_2d.b[i][j] = bool_distribution(rand_engine);
      s->arrays_2d.i8[i][j] = int8_distribution(rand_engine);
      s->arrays_2d.i16[i][j] = int16_distribution(rand_engine);
      s->arrays_2d.i32[i][j] = int32_distribution(rand_engine);
      s->arrays_2d.i64[i][j] = int64_distribution(rand_engine);
      s->arrays_2d.u8[i][j] = uint8_distribution(rand_engine);
      s->arrays_2d.u16[i][j] = uint16_distribution(rand_engine);
      s->arrays_2d.u32[i][j] = uint32_distribution(rand_engine);
      s->arrays_2d.u64[i][j] = uint64_distribution(rand_engine);
      s->arrays_2d.f32[i][j] = float_distribution(rand_engine);
      s->arrays_2d.f64[i][j] = double_distribution(rand_engine);
      s->arrays_2d.handle_handle[i][j] = Handle();
    }
  }

  // vectors
  s->vectors.b_0 = std::vector<bool>(kArbitraryVectorSize, bool_distribution(rand_engine));
  s->vectors.i8_0 = std::vector<int8_t>(kArbitraryVectorSize, int8_distribution(rand_engine));
  s->vectors.i16_0 = std::vector<int16_t>(kArbitraryVectorSize, int16_distribution(rand_engine));
  s->vectors.i32_0 = std::vector<int32_t>(kArbitraryVectorSize, int32_distribution(rand_engine));
  s->vectors.i64_0 = std::vector<int64_t>(kArbitraryVectorSize, int64_distribution(rand_engine));
  s->vectors.u8_0 = std::vector<uint8_t>(kArbitraryVectorSize, uint8_distribution(rand_engine));
  s->vectors.u16_0 = std::vector<uint16_t>(kArbitraryVectorSize, uint16_distribution(rand_engine));
  s->vectors.u32_0 = std::vector<uint32_t>(kArbitraryVectorSize, uint32_distribution(rand_engine));
  s->vectors.u64_0 = std::vector<uint64_t>(kArbitraryVectorSize, uint64_distribution(rand_engine));
  s->vectors.f32_0 = std::vector<float>(kArbitraryVectorSize, float_distribution(rand_engine));
  s->vectors.f64_0 = std::vector<double>(kArbitraryVectorSize, double_distribution(rand_engine));

  {
    std::vector<zx::handle> underlying_vec;
    for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
      underlying_vec.emplace_back(Handle());
    }
    s->vectors.handle_0 = std::vector<zx::handle>(std::move(underlying_vec));
  }

  {
    std::vector<std::vector<bool>> bool_outer_vector;
    std::vector<std::vector<int8_t>> int8_outer_vector;
    std::vector<std::vector<int16_t>> int16_outer_vector;
    std::vector<std::vector<int32_t>> int32_outer_vector;
    std::vector<std::vector<int64_t>> int64_outer_vector;
    std::vector<std::vector<uint8_t>> uint8_outer_vector;
    std::vector<std::vector<uint16_t>> uint16_outer_vector;
    std::vector<std::vector<uint32_t>> uint32_outer_vector;
    std::vector<std::vector<uint64_t>> uint64_outer_vector;
    std::vector<std::vector<float>> float_outer_vector;
    std::vector<std::vector<double>> double_outer_vector;
    std::vector<std::vector<zx::handle>> handle_outer_vector;
    for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
      bool_outer_vector.emplace_back(
          std::vector<bool>(std::vector<bool>(kArbitraryConstant, bool_distribution(rand_engine))));
      int8_outer_vector.emplace_back(std::vector<int8_t>(
          std::vector<int8_t>(kArbitraryConstant, int8_distribution(rand_engine))));
      int16_outer_vector.emplace_back(std::vector<int16_t>(
          std::vector<int16_t>(kArbitraryConstant, int16_distribution(rand_engine))));
      int32_outer_vector.emplace_back(std::vector<int32_t>(
          std::vector<int32_t>(kArbitraryConstant, int32_distribution(rand_engine))));
      int64_outer_vector.emplace_back(std::vector<int64_t>(
          std::vector<int64_t>(kArbitraryConstant, int64_distribution(rand_engine))));
      uint8_outer_vector.emplace_back(std::vector<uint8_t>(
          std::vector<uint8_t>(kArbitraryConstant, uint8_distribution(rand_engine))));
      uint16_outer_vector.emplace_back(std::vector<uint16_t>(
          std::vector<uint16_t>(kArbitraryConstant, uint16_distribution(rand_engine))));
      uint32_outer_vector.emplace_back(std::vector<uint32_t>(
          std::vector<uint32_t>(kArbitraryConstant, uint32_distribution(rand_engine))));
      uint64_outer_vector.emplace_back(std::vector<uint64_t>(
          std::vector<uint64_t>(kArbitraryConstant, uint64_distribution(rand_engine))));
      float_outer_vector.emplace_back(std::vector<float>(
          std::vector<float>(kArbitraryConstant, float_distribution(rand_engine))));
      double_outer_vector.emplace_back(std::vector<double>(
          std::vector<double>(kArbitraryConstant, double_distribution(rand_engine))));
      std::vector<zx::handle> handle_inner_vector;
      for (uint8_t i = 0; i < kArbitraryConstant; ++i) {
        handle_inner_vector.emplace_back(Handle());
      }
      handle_outer_vector.emplace_back(std::vector<zx::handle>(std::move(handle_inner_vector)));
    }
    s->vectors.b_1 = std::vector<std::vector<bool>>(std::move(bool_outer_vector));
    s->vectors.i8_1 = std::vector<std::vector<int8_t>>(std::move(int8_outer_vector));
    s->vectors.i16_1 = std::vector<std::vector<int16_t>>(std::move(int16_outer_vector));
    s->vectors.i32_1 = std::vector<std::vector<int32_t>>(std::move(int32_outer_vector));
    s->vectors.i64_1 = std::vector<std::vector<int64_t>>(std::move(int64_outer_vector));
    s->vectors.u8_1 = std::vector<std::vector<uint8_t>>(std::move(uint8_outer_vector));
    s->vectors.u16_1 = std::vector<std::vector<uint16_t>>(std::move(uint16_outer_vector));
    s->vectors.u32_1 = std::vector<std::vector<uint32_t>>(std::move(uint32_outer_vector));
    s->vectors.u64_1 = std::vector<std::vector<uint64_t>>(std::move(uint64_outer_vector));
    s->vectors.f32_1 = std::vector<std::vector<float>>(std::move(float_outer_vector));
    s->vectors.f64_1 = std::vector<std::vector<double>>(std::move(double_outer_vector));
    s->vectors.handle_1 = std::vector<std::vector<zx::handle>>(std::move(handle_outer_vector));
  }

  s->vectors.b_sized_0 = std::vector<bool>{bool_distribution(rand_engine)};
  s->vectors.i8_sized_0 = std::vector<int8_t>{int8_distribution(rand_engine)};
  s->vectors.i16_sized_0 = std::vector<int16_t>{int16_distribution(rand_engine)};
  s->vectors.i32_sized_0 = std::vector<int32_t>{int32_distribution(rand_engine)};
  s->vectors.i64_sized_0 = std::vector<int64_t>{int64_distribution(rand_engine)};
  s->vectors.u8_sized_0 = std::vector<uint8_t>{uint8_distribution(rand_engine)};
  s->vectors.u16_sized_0 = std::vector<uint16_t>{uint16_distribution(rand_engine)};
  s->vectors.u32_sized_0 = std::vector<uint32_t>{uint32_distribution(rand_engine)};
  s->vectors.u64_sized_0 = std::vector<uint64_t>{uint64_distribution(rand_engine)};
  s->vectors.f32_sized_0 = std::vector<float>{float_distribution(rand_engine)};
  s->vectors.f64_sized_0 = std::vector<double>{double_distribution(rand_engine)};

  {
    std::vector<zx::handle> underlying_vec;
    underlying_vec.emplace_back(Handle());
    s->vectors.handle_sized_0 = std::vector<zx::handle>(std::move(underlying_vec));
  }

  s->vectors.b_sized_1 =
      std::vector<bool>(fidl::test::compatibility::vectors_size, bool_distribution(rand_engine));
  s->vectors.i8_sized_1 =
      std::vector<int8_t>(fidl::test::compatibility::vectors_size, int8_distribution(rand_engine));
  s->vectors.i16_sized_1 = std::vector<int16_t>(fidl::test::compatibility::vectors_size,
                                                int16_distribution(rand_engine));
  s->vectors.i32_sized_1 = std::vector<int32_t>(fidl::test::compatibility::vectors_size,
                                                int32_distribution(rand_engine));
  s->vectors.i64_sized_1 = std::vector<int64_t>(fidl::test::compatibility::vectors_size,
                                                int64_distribution(rand_engine));
  s->vectors.u8_sized_1 = std::vector<uint8_t>(fidl::test::compatibility::vectors_size,
                                               uint8_distribution(rand_engine));
  s->vectors.u16_sized_1 = std::vector<uint16_t>(fidl::test::compatibility::vectors_size,
                                                 uint16_distribution(rand_engine));
  s->vectors.u32_sized_1 = std::vector<uint32_t>(fidl::test::compatibility::vectors_size,
                                                 uint32_distribution(rand_engine));
  s->vectors.u64_sized_1 = std::vector<uint64_t>(fidl::test::compatibility::vectors_size,
                                                 uint64_distribution(rand_engine));
  s->vectors.f32_sized_1 =
      std::vector<float>(fidl::test::compatibility::vectors_size, float_distribution(rand_engine));
  s->vectors.f64_sized_1 = std::vector<double>(fidl::test::compatibility::vectors_size,
                                               double_distribution(rand_engine));
  {
    std::vector<zx::handle> underlying_vec;
    for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
      underlying_vec.emplace_back(Handle());
    }
    s->vectors.handle_sized_1 = std::vector<zx::handle>(std::move(underlying_vec));
  }
  {
    std::vector<std::vector<bool>> bool_outer_vector;
    std::vector<std::vector<int8_t>> int8_outer_vector;
    std::vector<std::vector<int16_t>> int16_outer_vector;
    std::vector<std::vector<int32_t>> int32_outer_vector;
    std::vector<std::vector<int64_t>> int64_outer_vector;
    std::vector<std::vector<uint8_t>> uint8_outer_vector;
    std::vector<std::vector<uint16_t>> uint16_outer_vector;
    std::vector<std::vector<uint32_t>> uint32_outer_vector;
    std::vector<std::vector<uint64_t>> uint64_outer_vector;
    std::vector<std::vector<float>> float_outer_vector;
    std::vector<std::vector<double>> double_outer_vector;
    std::vector<std::vector<zx::handle>> handle_outer_vector;
    for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
      bool_outer_vector.emplace_back(
          std::vector<bool>(std::vector<bool>(kArbitraryConstant, bool_distribution(rand_engine))));
      int8_outer_vector.emplace_back(std::vector<int8_t>(
          std::vector<int8_t>(kArbitraryConstant, int8_distribution(rand_engine))));
      int16_outer_vector.emplace_back(std::vector<int16_t>(
          std::vector<int16_t>(kArbitraryConstant, int16_distribution(rand_engine))));
      int32_outer_vector.emplace_back(std::vector<int32_t>(
          std::vector<int32_t>(kArbitraryConstant, int32_distribution(rand_engine))));
      int64_outer_vector.emplace_back(std::vector<int64_t>(
          std::vector<int64_t>(kArbitraryConstant, int64_distribution(rand_engine))));
      uint8_outer_vector.emplace_back(std::vector<uint8_t>(
          std::vector<uint8_t>(kArbitraryConstant, uint8_distribution(rand_engine))));
      uint16_outer_vector.emplace_back(std::vector<uint16_t>(
          std::vector<uint16_t>(kArbitraryConstant, uint16_distribution(rand_engine))));
      uint32_outer_vector.emplace_back(std::vector<uint32_t>(
          std::vector<uint32_t>(kArbitraryConstant, uint32_distribution(rand_engine))));
      uint64_outer_vector.emplace_back(std::vector<uint64_t>(
          std::vector<uint64_t>(kArbitraryConstant, uint64_distribution(rand_engine))));
      float_outer_vector.emplace_back(std::vector<float>(
          std::vector<float>(kArbitraryConstant, float_distribution(rand_engine))));
      double_outer_vector.emplace_back(std::vector<double>(
          std::vector<double>(kArbitraryConstant, double_distribution(rand_engine))));
      std::vector<zx::handle> handle_inner_vector;
      for (uint8_t i = 0; i < kArbitraryConstant; ++i) {
        handle_inner_vector.emplace_back(Handle());
      }
      handle_outer_vector.emplace_back(std::vector<zx::handle>(std::move(handle_inner_vector)));
    }
    s->vectors.b_sized_2 = std::vector<std::vector<bool>>(std::move(bool_outer_vector));
    s->vectors.i8_sized_2 = std::vector<std::vector<int8_t>>(std::move(int8_outer_vector));
    s->vectors.i16_sized_2 = std::vector<std::vector<int16_t>>(std::move(int16_outer_vector));
    s->vectors.i32_sized_2 = std::vector<std::vector<int32_t>>(std::move(int32_outer_vector));
    s->vectors.i64_sized_2 = std::vector<std::vector<int64_t>>(std::move(int64_outer_vector));
    s->vectors.u8_sized_2 = std::vector<std::vector<uint8_t>>(std::move(uint8_outer_vector));
    s->vectors.u16_sized_2 = std::vector<std::vector<uint16_t>>(std::move(uint16_outer_vector));
    s->vectors.u32_sized_2 = std::vector<std::vector<uint32_t>>(std::move(uint32_outer_vector));
    s->vectors.u64_sized_2 = std::vector<std::vector<uint64_t>>(std::move(uint64_outer_vector));
    s->vectors.f32_sized_2 = std::vector<std::vector<float>>(std::move(float_outer_vector));
    s->vectors.f64_sized_2 = std::vector<std::vector<double>>(std::move(double_outer_vector));
    s->vectors.handle_sized_2 =
        std::vector<std::vector<zx::handle>>(std::move(handle_outer_vector));
  }

  // intentionally leave most of the nullable vectors as null, just set one
  // from each category.
  s->vectors.b_nullable_0 = VectorPtr<bool>(std::vector<bool>{bool_distribution(rand_engine)});
  {
    std::vector<std::vector<int8_t>> int8_outer_vector;
    for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
      int8_outer_vector.emplace_back(std::vector<int8_t>(
          std::vector<int8_t>(kArbitraryConstant, int8_distribution(rand_engine))));
    }
    s->vectors.i8_nullable_1 = VectorPtr<std::vector<int8_t>>(std::move(int8_outer_vector));
  }
  s->vectors.i16_nullable_sized_0 =
      VectorPtr<int16_t>(std::vector<int16_t>{int16_distribution(rand_engine)});
  s->vectors.f64_nullable_sized_1 = VectorPtr<double>(std::vector<double>(
      fidl::test::compatibility::vectors_size, double_distribution(rand_engine)));
  {
    std::vector<std::vector<zx::handle>> handle_outer_vector;
    for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
      std::vector<zx::handle> handle_inner_vector;
      for (uint8_t i = 0; i < kArbitraryConstant; ++i) {
        handle_inner_vector.emplace_back(Handle());
      }
      handle_outer_vector.emplace_back(std::vector<zx::handle>(std::move(handle_inner_vector)));
    }
    s->vectors.handle_nullable_sized_2 =
        VectorPtr<std::vector<zx::handle>>(std::move(handle_outer_vector));
  }

  // handles
  s->handles.handle_handle = Handle();

  ASSERT_EQ(ZX_OK,
            zx::process::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &s->handles.process_handle));
  ASSERT_EQ(ZX_OK, zx::thread::create(*zx::unowned_process(zx::process::self()), "dummy", 5u, 0u,
                                      &s->handles.thread_handle));
  ASSERT_EQ(ZX_OK, zx::vmo::create(0u, 0u, &s->handles.vmo_handle));
  ASSERT_EQ(ZX_OK, zx::event::create(0u, &s->handles.event_handle));
  ASSERT_EQ(ZX_OK, zx::port::create(0u, &s->handles.port_handle));

  zx::socket socket1;
  ASSERT_EQ(ZX_OK, zx::socket::create(0u, &s->handles.socket_handle, &socket1));

  zx::eventpair eventpair1;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0u, &s->handles.eventpair_handle, &eventpair1));

  ASSERT_EQ(ZX_OK, zx::job::create(*zx::job::default_job(), 0u, &s->handles.job_handle));

  uintptr_t vmar_addr;
  ASSERT_EQ(ZX_OK, zx::vmar::root_self()->allocate2(ZX_VM_CAN_MAP_READ, 0u, getpagesize(),
                                                    &s->handles.vmar_handle, &vmar_addr));

  zx::fifo fifo1;
  ASSERT_EQ(ZX_OK, zx::fifo::create(1u, 1u, 0u, &s->handles.fifo_handle, &fifo1));

  ASSERT_EQ(ZX_OK, zx::timer::create(0u, ZX_CLOCK_MONOTONIC, &s->handles.timer_handle));

  // For the nullable ones, just set one of them.
  s->handles.nullable_handle_handle = Handle();

  // strings
  s->strings.s = random_string;
  s->strings.size_0_s = random_short_string;
  s->strings.size_1_s = random_string;
  s->strings.nullable_size_0_s = random_short_string;

  // enums
  s->default_enum = fidl::test::compatibility::default_enum::kOne;
  s->i8_enum = fidl::test::compatibility::i8_enum::kNegativeOne;
  s->i16_enum = fidl::test::compatibility::i16_enum::kNegativeOne;
  s->i32_enum = fidl::test::compatibility::i32_enum::kNegativeOne;
  s->i64_enum = fidl::test::compatibility::i64_enum::kNegativeOne;
  s->u8_enum = fidl::test::compatibility::u8_enum::kOne;
  s->u16_enum = fidl::test::compatibility::u16_enum::kTwo;
  s->u32_enum = fidl::test::compatibility::u32_enum::kThree;
  s->u64_enum = fidl::test::compatibility::u64_enum::kFour;

  // bits
  s->default_bits = fidl::test::compatibility::default_bits::kOne;
  s->u8_bits = fidl::test::compatibility::u8_bits::kOne;
  s->u16_bits = fidl::test::compatibility::u16_bits::kTwo;
  s->u32_bits = fidl::test::compatibility::u32_bits::kThree;
  s->u64_bits = fidl::test::compatibility::u64_bits::kFour;

  // structs
  s->structs.s.s = random_string;

  // unions
  s->unions.u.set_s(random_string);
  s->unions.nullable_u = this_is_a_union::New();
  s->unions.nullable_u->set_b(bool_distribution(rand_engine));

  s->table.set_s(random_string);
  s->xunion_.set_s(random_string);

  // bool
  s->b = bool_distribution(rand_engine);
}

void InitializeArraysStruct(ArraysStruct* value, DataGenerator& gen) {
  for (uint32_t i = 0; i < fidl::test::compatibility::arrays_size; i++) {
    value->bools[i] = gen.next<bool>();
    value->int8s[i] = gen.next<int8_t>();
    value->int16s[i] = gen.next<int16_t>();
    value->int32s[i] = gen.next<int32_t>();
    value->uint8s[i] = gen.next<uint8_t>();
    value->uint16s[i] = gen.next<uint16_t>();
    value->uint32s[i] = gen.next<uint32_t>();
    value->uint64s[i] = gen.next<uint64_t>();
    value->float32s[i] = gen.next<float>();
    value->float64s[i] = gen.next<double>();

    value->enums[i] = gen.choose(fidl::test::compatibility::default_enum::kOne,
                                 fidl::test::compatibility::default_enum::kZero);
    value->bits[i] = gen.choose(fidl::test::compatibility::default_bits::kOne,
                                fidl::test::compatibility::default_bits::kTwo);

    value->handles[i] = gen.next<zx::handle>();
    value->nullable_handles[i] = gen.next<zx::handle>(true);

    value->strings[i] = gen.next<std::string>();
    value->nullable_strings[i] = gen.next<fidl::StringPtr>();

    value->structs[i] = gen.next<this_is_a_struct>();
    value->nullable_structs[i] = gen.next<std::unique_ptr<this_is_a_struct>>();
    if (gen.next<bool>()) {
      value->nullable_structs[i] = std::make_unique<this_is_a_struct>();
      value->nullable_structs[i]->s = gen.next<std::string>();
    }

    value->unions[i] = gen.next<this_is_a_union>();
    value->nullable_unions[i] = gen.next<std::unique_ptr<this_is_a_union>>();

    for (size_t j = 0; j < fidl::test::compatibility::arrays_size; j++) {
      value->arrays[i][j] = gen.next<uint32_t>();
      value->vectors[i].push_back(gen.next<uint32_t>());
    }

    if (gen.next<bool>()) {
      value->nullable_vectors[i].emplace();
      for (size_t j = 0; j < fidl::test::compatibility::arrays_size; j++) {
        value->nullable_vectors[i]->push_back(gen.next<uint32_t>());
      }
    }

    value->tables[i] = gen.next<this_is_a_table>();
    value->xunions[i] = gen.next<this_is_a_xunion>();
  }
}

void ExpectArraysStructEq(const ArraysStruct& a, const ArraysStruct& b) {
  EXPECT_TRUE(fidl::Equals(a.bools, b.bools));
  EXPECT_TRUE(fidl::Equals(a.int8s, b.int8s));
  EXPECT_TRUE(fidl::Equals(a.int16s, b.int16s));
  EXPECT_TRUE(fidl::Equals(a.int32s, b.int32s));
  EXPECT_TRUE(fidl::Equals(a.int64s, b.int64s));
  EXPECT_TRUE(fidl::Equals(a.uint8s, b.uint8s));
  EXPECT_TRUE(fidl::Equals(a.uint16s, b.uint16s));
  EXPECT_TRUE(fidl::Equals(a.uint32s, b.uint32s));
  EXPECT_TRUE(fidl::Equals(a.uint64s, b.uint64s));
  EXPECT_TRUE(fidl::Equals(a.float32s, b.float32s));
  EXPECT_TRUE(fidl::Equals(a.float64s, b.float64s));
  EXPECT_TRUE(fidl::Equals(a.enums, b.enums));
  EXPECT_TRUE(fidl::Equals(a.bits, b.bits));
  EXPECT_EQ(a.handles.size(), b.handles.size());
  EXPECT_EQ(a.nullable_handles.size(), b.nullable_handles.size());
  EXPECT_EQ(a.handles.size(), a.nullable_handles.size());
  for (size_t i = 0; i < a.handles.size(); i++) {
    EXPECT_TRUE(HandlesEq(a.handles[i], b.handles[i]));
    EXPECT_TRUE(HandlesEq(a.nullable_handles[i], b.nullable_handles[i]));
  }
  EXPECT_TRUE(fidl::Equals(a.strings, b.strings));
  EXPECT_TRUE(fidl::Equals(a.nullable_strings, b.nullable_strings));
  EXPECT_TRUE(fidl::Equals(a.structs, b.structs));
  EXPECT_TRUE(fidl::Equals(a.nullable_structs, b.nullable_structs));
  EXPECT_TRUE(fidl::Equals(a.unions, b.unions));
  EXPECT_TRUE(fidl::Equals(a.nullable_unions, b.nullable_unions));
  EXPECT_TRUE(fidl::Equals(a.arrays, b.arrays));
  EXPECT_TRUE(fidl::Equals(a.vectors, b.vectors));
  EXPECT_TRUE(fidl::Equals(a.nullable_vectors, b.nullable_vectors));
  EXPECT_TRUE(fidl::Equals(a.tables, b.tables));
  EXPECT_TRUE(fidl::Equals(a.xunions, b.xunions));
}

void InitializeVectorsStruct(VectorsStruct* value, DataGenerator& gen) {
  for (uint32_t i = 0; i < fidl::test::compatibility::arrays_size; i++) {
    value->bools.push_back(gen.next<bool>());
    value->int8s.push_back(gen.next<int8_t>());
    value->int16s.push_back(gen.next<int16_t>());
    value->int32s.push_back(gen.next<int32_t>());
    value->int64s.push_back(gen.next<int64_t>());
    value->uint8s.push_back(gen.next<uint8_t>());
    value->uint16s.push_back(gen.next<uint16_t>());
    value->uint32s.push_back(gen.next<uint32_t>());
    value->uint64s.push_back(gen.next<uint64_t>());
    value->float32s.push_back(gen.next<float>());
    value->float64s.push_back(gen.next<double>());

    value->enums.push_back(gen.choose(fidl::test::compatibility::default_enum::kOne,
                                      fidl::test::compatibility::default_enum::kZero));
    value->bits.push_back(gen.choose(fidl::test::compatibility::default_bits::kOne,
                                     fidl::test::compatibility::default_bits::kTwo));

    value->handles.push_back(gen.next<zx::handle>());
    value->nullable_handles.push_back(gen.next<zx::handle>(true));

    value->strings.push_back(gen.next<std::string>());
    value->nullable_strings.push_back(gen.next<fidl::StringPtr>());

    value->structs.push_back(this_is_a_struct{});
    value->structs[i].s = gen.next<std::string>();
    if (gen.next<bool>()) {
      value->nullable_structs.push_back(std::make_unique<this_is_a_struct>());
      value->nullable_structs.back()->s = gen.next<std::string>();
    }

    value->unions.push_back(gen.next<this_is_a_union>());
    value->nullable_unions.push_back(gen.next<std::unique_ptr<this_is_a_union>>());

    value->arrays.push_back(std::array<uint32_t, fidl::test::compatibility::vectors_size>{});
    value->vectors.push_back(std::vector<uint32_t>{});
    for (size_t j = 0; j < fidl::test::compatibility::vectors_size; j++) {
      value->arrays.back()[j] = gen.next<uint32_t>();
      value->vectors.back().push_back(gen.next<uint32_t>());
    }

    value->nullable_vectors.push_back(fidl::VectorPtr<uint32_t>());
    if (gen.next<bool>()) {
      value->nullable_vectors.back().emplace();
      for (size_t j = 0; j < fidl::test::compatibility::vectors_size; j++) {
        value->nullable_vectors.back()->push_back(gen.next<uint32_t>());
      }
    } else {
      value->nullable_vectors.back().reset();
    }

    value->tables.push_back(gen.next<this_is_a_table>());
    value->xunions.push_back(gen.next<this_is_a_xunion>());
  }
}

void ExpectVectorsStructEq(const VectorsStruct& a, const VectorsStruct& b) {
  EXPECT_TRUE(fidl::Equals(a.bools, b.bools));
  EXPECT_TRUE(fidl::Equals(a.int8s, b.int8s));
  EXPECT_TRUE(fidl::Equals(a.int16s, b.int16s));
  EXPECT_TRUE(fidl::Equals(a.int32s, b.int32s));
  EXPECT_TRUE(fidl::Equals(a.int64s, b.int64s));
  EXPECT_TRUE(fidl::Equals(a.uint8s, b.uint8s));
  EXPECT_TRUE(fidl::Equals(a.uint16s, b.uint16s));
  EXPECT_TRUE(fidl::Equals(a.uint32s, b.uint32s));
  EXPECT_TRUE(fidl::Equals(a.uint64s, b.uint64s));
  EXPECT_TRUE(fidl::Equals(a.float32s, b.float32s));
  EXPECT_TRUE(fidl::Equals(a.float64s, b.float64s));
  EXPECT_TRUE(fidl::Equals(a.enums, b.enums));
  EXPECT_TRUE(fidl::Equals(a.bits, b.bits));
  EXPECT_EQ(a.handles.size(), b.handles.size());
  EXPECT_EQ(a.nullable_handles.size(), b.nullable_handles.size());
  EXPECT_EQ(a.handles.size(), a.nullable_handles.size());
  for (size_t i = 0; i < a.handles.size(); i++) {
    EXPECT_TRUE(HandlesEq(a.handles[i], b.handles[i]));
    EXPECT_TRUE(HandlesEq(a.nullable_handles[i], b.nullable_handles[i]));
  }
  EXPECT_TRUE(fidl::Equals(a.strings, b.strings));
  EXPECT_TRUE(fidl::Equals(a.nullable_strings, b.nullable_strings));
  EXPECT_TRUE(fidl::Equals(a.structs, b.structs));
  EXPECT_TRUE(fidl::Equals(a.nullable_structs, b.nullable_structs));
  EXPECT_TRUE(fidl::Equals(a.unions, b.unions));
  EXPECT_TRUE(fidl::Equals(a.nullable_unions, b.nullable_unions));
  EXPECT_TRUE(fidl::Equals(a.arrays, b.arrays));
  EXPECT_TRUE(fidl::Equals(a.vectors, b.vectors));
  EXPECT_TRUE(fidl::Equals(a.nullable_vectors, b.nullable_vectors));
  EXPECT_TRUE(fidl::Equals(a.tables, b.tables));
  EXPECT_TRUE(fidl::Equals(a.xunions, b.xunions));
}

void InitializeAllTypesTable(AllTypesTable* value, DataGenerator& gen) {
  value->set_bool_member(gen.next<bool>());
  value->set_int8_member(gen.next<int8_t>());
  value->set_int16_member(gen.next<int16_t>());
  value->set_int32_member(gen.next<int32_t>());
  value->set_int64_member(gen.next<int64_t>());
  value->set_uint8_member(gen.next<uint8_t>());
  value->set_uint16_member(gen.next<uint16_t>());
  value->set_uint32_member(gen.next<uint32_t>());
  value->set_uint64_member(gen.next<uint64_t>());
  value->set_float32_member(gen.next<float>());
  value->set_float64_member(gen.next<double>());
  value->set_enum_member(gen.choose(fidl::test::compatibility::default_enum::kOne,
                                    fidl::test::compatibility::default_enum::kZero));
  value->set_bits_member(gen.choose(fidl::test::compatibility::default_bits::kOne,
                                    fidl::test::compatibility::default_bits::kTwo));
  value->set_handle_member(gen.next<zx::handle>());
  value->set_string_member(gen.next<std::string>());
  value->set_struct_member(gen.next<this_is_a_struct>());
  value->set_union_member(gen.next<this_is_a_union>());

  std::array<uint32_t, fidl::test::compatibility::arrays_size> array;
  for (size_t i = 0; i < array.size(); i++) {
    array[i] = gen.next<uint32_t>();
  }
  value->set_array_member(array);

  std::vector<uint32_t> vector;
  for (size_t i = 0; i < kArbitraryVectorSize; i++) {
    vector.push_back(gen.next<uint32_t>());
  }
  value->set_vector_member(vector);

  value->set_table_member(gen.next<this_is_a_table>());
  value->set_xunion_member(gen.next<this_is_a_xunion>());
}

void ExpectAllTypesTableEq(const AllTypesTable& a, const AllTypesTable& b) {
  EXPECT_TRUE(fidl::Equals(a.bool_member(), b.bool_member()));
  EXPECT_TRUE(fidl::Equals(a.int8_member(), b.int8_member()));
  EXPECT_TRUE(fidl::Equals(a.int16_member(), b.int16_member()));
  EXPECT_TRUE(fidl::Equals(a.int32_member(), b.int32_member()));
  EXPECT_TRUE(fidl::Equals(a.int64_member(), b.int64_member()));
  EXPECT_TRUE(fidl::Equals(a.uint8_member(), b.uint8_member()));
  EXPECT_TRUE(fidl::Equals(a.uint16_member(), b.uint16_member()));
  EXPECT_TRUE(fidl::Equals(a.uint32_member(), b.uint32_member()));
  EXPECT_TRUE(fidl::Equals(a.uint64_member(), b.uint64_member()));
  EXPECT_TRUE(fidl::Equals(a.float32_member(), b.float32_member()));
  EXPECT_TRUE(fidl::Equals(a.float64_member(), b.float64_member()));
  EXPECT_TRUE(fidl::Equals(a.enum_member(), b.enum_member()));
  EXPECT_TRUE(fidl::Equals(a.bits_member(), b.bits_member()));
  EXPECT_TRUE(HandlesEq(a.handle_member(), b.handle_member()));
  EXPECT_TRUE(fidl::Equals(a.string_member(), b.string_member()));
  EXPECT_TRUE(fidl::Equals(a.struct_member(), b.struct_member()));
  EXPECT_TRUE(fidl::Equals(a.union_member(), b.union_member()));
  EXPECT_TRUE(fidl::Equals(a.array_member(), b.array_member()));
  EXPECT_TRUE(fidl::Equals(a.vector_member(), b.vector_member()));
  EXPECT_TRUE(fidl::Equals(a.table_member(), b.table_member()));
  EXPECT_TRUE(fidl::Equals(a.xunion_member(), b.xunion_member()));
}

void InitializeAllTypesXunions(std::vector<AllTypesXunion>* value, DataGenerator& gen) {
  for (size_t i = 1; true; i++) {
    AllTypesXunion xu{};
    switch (i) {
      case 1:
        xu.set_bool_member(gen.next<bool>());
        break;
      case 2:
        xu.set_int8_member(gen.next<int8_t>());
        break;
      case 3:
        xu.set_int16_member(gen.next<int16_t>());
        break;
      case 4:
        xu.set_int32_member(gen.next<int32_t>());
        break;
      case 5:
        xu.set_int64_member(gen.next<int64_t>());
        break;
      case 6:
        xu.set_uint8_member(gen.next<uint8_t>());
        break;
      case 7:
        xu.set_uint16_member(gen.next<uint16_t>());
        break;
      case 8:
        xu.set_uint32_member(gen.next<uint32_t>());
        break;
      case 9:
        xu.set_uint64_member(gen.next<uint64_t>());
        break;
      case 10:
        xu.set_float32_member(gen.next<float>());
        break;
      case 11:
        xu.set_float64_member(gen.next<double>());
        break;
      case 12:
        xu.set_enum_member(gen.choose(fidl::test::compatibility::default_enum::kOne,
                                      fidl::test::compatibility::default_enum::kZero));
        break;
      case 13:
        xu.set_bits_member(gen.choose(fidl::test::compatibility::default_bits::kOne,
                                      fidl::test::compatibility::default_bits::kTwo));
        break;
      case 14:
        xu.set_handle_member(gen.next<zx::handle>());
        break;
      case 15:
        xu.set_string_member(gen.next<std::string>());
        break;
      case 16:
        xu.set_struct_member(gen.next<this_is_a_struct>());
        break;
      case 17:
        xu.set_union_member(gen.next<this_is_a_union>());
        break;
      default:
        EXPECT_EQ(i, 18UL);
        return;
    }
    value->push_back(std::move(xu));
  }
}

void ExpectAllTypesXunionsEq(const std::vector<AllTypesXunion>& a,
                             const std::vector<AllTypesXunion>& b) {
  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].is_handle_member()) {
      EXPECT_TRUE(b[i].is_handle_member());
      EXPECT_TRUE(HandlesEq(a[i].handle_member(), b[i].handle_member()));
    } else {
      EXPECT_TRUE(fidl::Equals(a[i], b[i]));
    }
  }
}

class CompatibilityTest : public ::testing::TestWithParam<std::tuple<std::string, std::string>> {
 protected:
  void SetUp() override {
    proxy_url_ = ::testing::get<0>(GetParam());
    server_url_ = ::testing::get<1>(GetParam());
    // The FIDL support lib requires async_get_default_dispatcher() to return
    // non-null.
    loop_.reset(new async::Loop(&kAsyncLoopConfigAttachToCurrentThread));
  }
  std::string proxy_url_;
  std::string server_url_;
  std::unique_ptr<async::Loop> loop_;
};

std::vector<std::string> servers;
std::map<std::string, bool> summary;

std::string ExtractShortName(const std::string& pkg_url) {
  std::regex r("(meta/fidl_compatibility_test_server_)(.*)(\\.cmx)");
  std::smatch match;
  std::regex_search(pkg_url, match, r);
  return match.str(2);
}

using TestBody = std::function<void(async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                                    const std::string& server_url, const std::string& proxy_url)>;
using AllowServer = std::function<bool(const std::string& server_url)>;

void ForSomeServers(AllowServer allow, TestBody body) {
  for (auto const& proxy_url : servers) {
    if (!allow(proxy_url)) {
      continue;
    }
    for (auto const& server_url : servers) {
      if (!allow(server_url)) {
        continue;
      }
      std::cerr << proxy_url << " <-> " << server_url << std::endl;
      async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
      fidl::test::compatibility::EchoClientApp proxy;
      bool test_completed = false;
      proxy.echo().set_error_handler([&proxy_url, &loop, &test_completed](zx_status_t status) {
        if (!test_completed) {
          loop.Quit();
          FAIL() << "Connection to " << proxy_url << " failed unexpectedly: " << status;
        }
      });
      proxy.Start(proxy_url);

      body(loop, proxy.echo(), server_url, proxy_url);
      test_completed = true;
    }
  }
}

void ForAllServers(TestBody body) {
  ForSomeServers([](const std::string& _) { return true; }, body);
}

[[maybe_unused]] AllowServer Exclude(std::initializer_list<const char*> substrings) {
  return [substrings](const std::string& server_url) {
    for (auto substring : substrings) {
      if (server_url.find(substring) != std::string::npos) {
        return false;
      }
    }
    return true;
  };
}

TEST(Struct, EchoStruct) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (struct)"] =
        false;
    Struct sent;
    InitializeStruct(&sent);

    Struct sent_clone;
    sent.Clone(&sent_clone);
    Struct resp_clone;
    bool called_back = false;
    proxy->EchoStruct(std::move(sent), server_url, [&loop, &resp_clone, &called_back](Struct resp) {
      ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
      called_back = true;
      loop.Quit();
    });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (struct)"] =
        true;
  });
}

TEST(Struct, EchoStructWithErrorSuccessCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct result success)"] = false;
    Struct sent;
    InitializeStruct(&sent);
    auto err = fidl::test::compatibility::default_enum::kOne;

    Struct sent_clone;
    sent.Clone(&sent_clone);

    Struct resp_clone;
    bool called_back = false;
    proxy->EchoStructWithError(
        std::move(sent), err, server_url, RespondWith::SUCCESS,
        [&loop, &resp_clone, &called_back](Echo_EchoStructWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().value.Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct result success)"] = true;
  });
}

TEST(Struct, EchoStructWithErrorErrorCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct result error)"] = false;
    Struct sent;
    InitializeStruct(&sent);
    auto err = fidl::test::compatibility::default_enum::kOne;

    bool called_back = false;
    proxy->EchoStructWithError(std::move(sent), err, server_url, RespondWith::ERR,
                               [&loop, &err, &called_back](Echo_EchoStructWithError_Result resp) {
                                 ASSERT_TRUE(resp.is_err());
                                 ASSERT_EQ(err, resp.err());
                                 called_back = true;
                                 loop.Quit();
                               });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct result error)"] = true;
  });
}

TEST(Struct, EchoStructNoRetval) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct_no_ret)"] = false;
    Struct sent;
    InitializeStruct(&sent);

    Struct sent_clone;
    sent.Clone(&sent_clone);
    fidl::test::compatibility::Struct resp_clone;
    bool event_received = false;
    proxy.events().EchoEvent = [&loop, &resp_clone, &event_received](Struct resp) {
      resp.Clone(&resp_clone);
      event_received = true;
      loop.Quit();
    };
    proxy->EchoStructNoRetVal(std::move(sent), server_url);
    loop.Run();
    ASSERT_TRUE(event_received);
    ExpectEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct_no_ret)"] = true;
  });
}

TEST(Array, EchoArrays) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (array)"] =
        false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    ArraysStruct sent;
    InitializeArraysStruct(&sent, generator);

    ArraysStruct sent_clone;
    sent.Clone(&sent_clone);
    ArraysStruct resp_clone;
    bool called_back = false;
    proxy->EchoArrays(std::move(sent), server_url,
                      [&loop, &resp_clone, &called_back](ArraysStruct resp) {
                        ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
                        called_back = true;
                        loop.Quit();
                      });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectArraysStructEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (array)"] =
        true;
  });
}

TEST(Array, EchoArraysWithErrorSuccessCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (array result success)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    ArraysStruct sent;
    InitializeArraysStruct(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    ArraysStruct sent_clone;
    sent.Clone(&sent_clone);
    ArraysStruct resp_clone;
    bool called_back = false;
    proxy->EchoArraysWithError(
        std::move(sent), err, server_url, RespondWith::SUCCESS,
        [&loop, &resp_clone, &called_back](Echo_EchoArraysWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().value.Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectArraysStructEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (array result success)"] = true;
  });
}

TEST(Array, EchoArraysWithErrorErrorCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (array result error)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    ArraysStruct sent;
    InitializeArraysStruct(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    bool called_back = false;
    proxy->EchoArraysWithError(std::move(sent), err, server_url, RespondWith::ERR,
                               [&loop, &err, &called_back](Echo_EchoArraysWithError_Result resp) {
                                 ASSERT_TRUE(resp.is_err());
                                 ASSERT_EQ(err, resp.err());
                                 called_back = true;
                                 loop.Quit();
                               });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (array result error)"] = true;
  });
}

TEST(Vector, EchoVectors) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (vector)"] =
        false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    VectorsStruct sent;
    InitializeVectorsStruct(&sent, generator);

    VectorsStruct sent_clone;
    sent.Clone(&sent_clone);
    VectorsStruct resp_clone;
    bool called_back = false;
    proxy->EchoVectors(std::move(sent), server_url,
                       [&loop, &resp_clone, &called_back](VectorsStruct resp) {
                         ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
                         called_back = true;
                         loop.Quit();
                       });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectVectorsStructEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (vector)"] =
        true;
  });
}

TEST(Vector, EchoVectorsWithErrorSuccessCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (vector result success)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    VectorsStruct sent;
    InitializeVectorsStruct(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    VectorsStruct sent_clone;
    sent.Clone(&sent_clone);
    VectorsStruct resp_clone;
    bool called_back = false;
    proxy->EchoVectorsWithError(
        std::move(sent), err, server_url, RespondWith::SUCCESS,
        [&loop, &resp_clone, &called_back](Echo_EchoVectorsWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().value.Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectVectorsStructEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (vector result success)"] = true;
  });
}

TEST(Vector, EchoVectorsWithErrorErrorCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (vector result error)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    VectorsStruct sent;
    InitializeVectorsStruct(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    bool called_back = false;
    proxy->EchoVectorsWithError(std::move(sent), err, server_url, RespondWith::ERR,
                                [&loop, &err, &called_back](Echo_EchoVectorsWithError_Result resp) {
                                  ASSERT_TRUE(resp.is_err());
                                  ASSERT_EQ(err, resp.err());
                                  called_back = true;
                                  loop.Quit();
                                });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (vector result error)"] = true;
  });
}

TEST(Table, EchoTable) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
        false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    AllTypesTable sent;
    InitializeAllTypesTable(&sent, generator);

    AllTypesTable sent_clone;
    sent.Clone(&sent_clone);
    AllTypesTable resp_clone;
    bool called_back = false;
    proxy->EchoTable(std::move(sent), server_url,
                     [&loop, &resp_clone, &called_back](AllTypesTable resp) {
                       ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
                       called_back = true;
                       loop.Quit();
                     });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectAllTypesTableEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (table)"] =
        true;
  });
}

TEST(Table, EchoTableWithErrorSuccessCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (table result success)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    AllTypesTable sent;
    InitializeAllTypesTable(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    AllTypesTable sent_clone;
    sent.Clone(&sent_clone);
    AllTypesTable resp_clone;
    bool called_back = false;
    proxy->EchoTableWithError(
        std::move(sent), err, server_url, RespondWith::SUCCESS,
        [&loop, &resp_clone, &called_back](Echo_EchoTableWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().value.Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectAllTypesTableEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (table result success)"] = true;
  });
}

TEST(Table, EchoTableWithErrorErrorCase) {
  ForAllServers(
      // See: fxbug.dev/7966
      [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
         const std::string& server_url, const std::string& proxy_url) {
        summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
                " (table result error)"] = false;
        // Using randomness to avoid having to come up with varied values by
        // hand. Seed deterministically so that this function's outputs are
        // predictable.
        DataGenerator generator(0xF1D7);

        AllTypesTable sent;
        InitializeAllTypesTable(&sent, generator);
        auto err = fidl::test::compatibility::default_enum::kOne;

        bool called_back = false;
        proxy->EchoTableWithError(std::move(sent), err, server_url, RespondWith::ERR,
                                  [&loop, &err, &called_back](Echo_EchoTableWithError_Result resp) {
                                    ASSERT_TRUE(resp.is_err());
                                    ASSERT_EQ(err, resp.err());
                                    called_back = true;
                                    loop.Quit();
                                  });

        loop.Run();
        ASSERT_TRUE(called_back);
        summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
                " (table result error)"] = true;
      });
}

TEST(Union, EchoUnions) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (xunion)"] =
        false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    std::vector<AllTypesXunion> sent;
    InitializeAllTypesXunions(&sent, generator);

    std::vector<AllTypesXunion> sent_clone;
    fidl::Clone(sent, &sent_clone);
    std::vector<AllTypesXunion> resp_clone;
    bool called_back = false;
    proxy->EchoXunions(std::move(sent), server_url,
                       [&loop, &resp_clone, &called_back](std::vector<AllTypesXunion> resp) {
                         ASSERT_EQ(ZX_OK, fidl::Clone(resp, &resp_clone));
                         called_back = true;
                         loop.Quit();
                       });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectAllTypesXunionsEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (xunion)"] =
        true;
  });
}

TEST(Union, EchoUnionsWithErrorSuccessCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (xunion result success)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0x1234);

    std::vector<AllTypesXunion> sent;
    InitializeAllTypesXunions(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    std::vector<AllTypesXunion> sent_clone;
    fidl::Clone(sent, &sent_clone);
    std::vector<AllTypesXunion> resp_clone;
    bool called_back = false;
    proxy->EchoXunionsWithError(
        std::move(sent), err, server_url, RespondWith::SUCCESS,
        [&loop, &resp_clone, &called_back](Echo_EchoXunionsWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, fidl::Clone(resp.response().value, &resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    ExpectAllTypesXunionsEq(sent_clone, resp_clone);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (xunion result success)"] = true;
  });
}

TEST(Union, EchoUnionsWithErrorErrorCase) {
  ForAllServers([](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                   const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (xunion result error)"] = false;
    // Using randomness to avoid having to come up with varied values by
    // hand. Seed deterministically so that this function's outputs are
    // predictable.
    DataGenerator generator(0xF1D7);

    std::vector<AllTypesXunion> sent;
    InitializeAllTypesXunions(&sent, generator);
    auto err = fidl::test::compatibility::default_enum::kOne;

    bool called_back = false;
    proxy->EchoXunionsWithError(std::move(sent), err, server_url, RespondWith::ERR,
                                [&loop, &err, &called_back](Echo_EchoXunionsWithError_Result resp) {
                                  ASSERT_TRUE(resp.is_err());
                                  ASSERT_EQ(err, resp.err());
                                  called_back = true;
                                  loop.Quit();
                                });

    loop.Run();
    ASSERT_TRUE(called_back);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (xunion result error)"] = true;
  });
}

}  // namespace

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  for (int i = 1; i < argc; i++) {
    std::string server(argv[i]);
    std::string package_url;
    if (server.rfind("fuchsia-pkg://", 0) == 0) {
      package_url = server;
    } else {
      package_url = "fuchsia-pkg://fuchsia.com/fidl-compatibility-test#meta/" +
                    std::string(argv[i]) + "-server.cmx";
    }
    servers.push_back(package_url);
  }

  FX_CHECK(!servers.empty()) << kUsage;

  int r = RUN_ALL_TESTS();
  std::cout << std::endl;
  std::cout << "========================= Interop Summary ======================" << std::endl;

  for (std::pair<std::string, bool> element : summary) {
    if (element.second) {
      std::cout << "[PASS]";
    } else {
      std::cout << "[FAIL]";
    }
    std::cout << " " << element.first << std::endl;
  }

  std::cout << std::endl;
  std::cout << std::endl;
  return r;
}
