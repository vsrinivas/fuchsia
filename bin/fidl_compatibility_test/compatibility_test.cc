// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>
#include <algorithm>
#include <cstdlib>
#include <random>
#include <vector>

#include "garnet/public/lib/fidl/compatibility_test/echo_client_app.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/fxl/strings/utf_codecs.h"

using fidl::test::compatibility::Struct;
using fidl::VectorPtr;
using std::string;

namespace {
// Want a size small enough that it doesn't get too big to transmit but
// large enough to exercise interesting code paths.
constexpr uint8_t kArbitraryVectorSize = 3;
// This is used as a literal constant in compatibility_test_service.fidl.
constexpr uint8_t kArbitraryConstant = 2;

constexpr char kServersEnvVarName[] = "FIDL_COMPATIBILITY_TEST_SERVERS";
constexpr char kUsage[] = (
    "Usage:\n  FIDL_COMPATIBILITY_TEST_SERVERS=foo_server,bar_server "
    "fidl_compatibility_test\n"
    "You must set the environment variable FIDL_COMPATIBILITY_TEST_SERVERS to a"
    "comma-separated list of server URLs when running this test.");

zx::handle Handle() {
  zx_handle_t raw_event;
  const zx_status_t status = zx_event_create(0u, &raw_event);
  // Can't use gtest ASSERT_EQ because we're in a non-void function.
  ZX_ASSERT_MSG(status == ZX_OK, "status = %d", status);
  return zx::handle(raw_event);
}

::testing::AssertionResult HandlesEq(const zx::object_base& a,
                                     const zx::object_base& b) {
  if (a.is_valid() != b.is_valid()) {
    return ::testing::AssertionFailure()
           << "Handles are not equally valid :" << a.is_valid() << " vs "
           << b.is_valid();
  }
  if (!a.is_valid()) {
    return ::testing::AssertionSuccess() << "Both handles invalid";
  }
  zx_info_handle_basic_t a_info, b_info;
  zx_status_t status = zx_object_get_info(
      a.get(), ZX_INFO_HANDLE_BASIC, &a_info, sizeof(a_info), nullptr, nullptr);
  if (ZX_OK != status) {
    return ::testing::AssertionFailure()
           << "zx_object_get_info(a) returned " << status;
  }
  status = zx_object_get_info(b.get(), ZX_INFO_HANDLE_BASIC, &b_info,
                              sizeof(b_info), nullptr, nullptr);
  if (ZX_OK != status) {
    return ::testing::AssertionFailure()
           << "zx_object_get_info(b) returned " << status;
  }
  if (a_info.koid != b_info.koid) {
    return ::testing::AssertionFailure()
           << std::endl
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
      EXPECT_TRUE(HandlesEq(a.arrays_2d.handle_handle[i][j],
                            b.arrays_2d.handle_handle[i][j]));
    }
  }
  // vectors
  EXPECT_EQ(a.vectors.b_0.get(), b.vectors.b_0.get());
  EXPECT_EQ(a.vectors.i8_0.get(), b.vectors.i8_0.get());
  EXPECT_EQ(a.vectors.i16_0.get(), b.vectors.i16_0.get());
  EXPECT_EQ(a.vectors.i32_0.get(), b.vectors.i32_0.get());
  EXPECT_EQ(a.vectors.i64_0.get(), b.vectors.i64_0.get());
  EXPECT_EQ(a.vectors.u8_0.get(), b.vectors.u8_0.get());
  EXPECT_EQ(a.vectors.u16_0.get(), b.vectors.u16_0.get());
  EXPECT_EQ(a.vectors.u32_0.get(), b.vectors.u32_0.get());
  EXPECT_EQ(a.vectors.u64_0.get(), b.vectors.u64_0.get());
  EXPECT_EQ(a.vectors.f32_0.get(), b.vectors.f32_0.get());
  EXPECT_EQ(a.vectors.f64_0.get(), b.vectors.f64_0.get());
  for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
    EXPECT_TRUE(
        HandlesEq(a.vectors.handle_0.get()[i], b.vectors.handle_0.get()[i]));
  }

  for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
    EXPECT_EQ(a.vectors.b_1.get()[i].get(), b.vectors.b_1.get()[i].get());
    EXPECT_EQ(a.vectors.i8_1.get()[i].get(), b.vectors.i8_1.get()[i].get());
    EXPECT_EQ(a.vectors.i16_1.get()[i].get(), b.vectors.i16_1.get()[i].get());
    EXPECT_EQ(a.vectors.i32_1.get()[i].get(), b.vectors.i32_1.get()[i].get());
    EXPECT_EQ(a.vectors.i64_1.get()[i].get(), b.vectors.i64_1.get()[i].get());
    EXPECT_EQ(a.vectors.u8_1.get()[i].get(), b.vectors.u8_1.get()[i].get());
    EXPECT_EQ(a.vectors.u16_1.get()[i].get(), b.vectors.u16_1.get()[i].get());
    EXPECT_EQ(a.vectors.u32_1.get()[i].get(), b.vectors.u32_1.get()[i].get());
    EXPECT_EQ(a.vectors.u64_1.get()[i].get(), b.vectors.u64_1.get()[i].get());
    EXPECT_EQ(a.vectors.f32_1.get()[i].get(), b.vectors.f32_1.get()[i].get());
    EXPECT_EQ(a.vectors.f64_1.get()[i].get(), b.vectors.f64_1.get()[i].get());
    for (uint8_t j = 0; j < kArbitraryConstant; ++j) {
      EXPECT_TRUE(HandlesEq(a.vectors.handle_1.get()[i].get()[j],
                            b.vectors.handle_1.get()[i].get()[j]));
    }
  }

  EXPECT_EQ(a.vectors.b_sized_0.get(), b.vectors.b_sized_0.get());
  EXPECT_EQ(a.vectors.i8_sized_0.get(), b.vectors.i8_sized_0.get());
  EXPECT_EQ(a.vectors.i16_sized_0.get(), b.vectors.i16_sized_0.get());
  EXPECT_EQ(a.vectors.i32_sized_0.get(), b.vectors.i32_sized_0.get());
  EXPECT_EQ(a.vectors.i64_sized_0.get(), b.vectors.i64_sized_0.get());
  EXPECT_EQ(a.vectors.u8_sized_0.get(), b.vectors.u8_sized_0.get());
  EXPECT_EQ(a.vectors.u16_sized_0.get(), b.vectors.u16_sized_0.get());
  EXPECT_EQ(a.vectors.u32_sized_0.get(), b.vectors.u32_sized_0.get());
  EXPECT_EQ(a.vectors.u64_sized_0.get(), b.vectors.u64_sized_0.get());
  EXPECT_EQ(a.vectors.f32_sized_0.get(), b.vectors.f32_sized_0.get());
  EXPECT_EQ(a.vectors.f64_sized_0.get(), b.vectors.f64_sized_0.get());
  EXPECT_TRUE(HandlesEq(a.vectors.handle_sized_0.get()[0],
                        b.vectors.handle_sized_0.get()[0]));

  EXPECT_EQ(a.vectors.b_sized_1.get(), b.vectors.b_sized_1.get());
  EXPECT_EQ(a.vectors.i8_sized_1.get(), b.vectors.i8_sized_1.get());
  EXPECT_EQ(a.vectors.i16_sized_1.get(), b.vectors.i16_sized_1.get());
  EXPECT_EQ(a.vectors.i32_sized_1.get(), b.vectors.i32_sized_1.get());
  EXPECT_EQ(a.vectors.i64_sized_1.get(), b.vectors.i64_sized_1.get());
  EXPECT_EQ(a.vectors.u8_sized_1.get(), b.vectors.u8_sized_1.get());
  EXPECT_EQ(a.vectors.u16_sized_1.get(), b.vectors.u16_sized_1.get());
  EXPECT_EQ(a.vectors.u32_sized_1.get(), b.vectors.u32_sized_1.get());
  EXPECT_EQ(a.vectors.u64_sized_1.get(), b.vectors.u64_sized_1.get());
  EXPECT_EQ(a.vectors.f32_sized_1.get(), b.vectors.f32_sized_1.get());
  EXPECT_EQ(a.vectors.f64_sized_1.get(), b.vectors.f64_sized_1.get());
  for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
    EXPECT_TRUE(HandlesEq(a.vectors.handle_sized_1.get()[i],
                          b.vectors.handle_sized_1.get()[i]));
  }

  for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
    EXPECT_EQ(a.vectors.b_sized_2.get()[i].get(),
              b.vectors.b_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.i8_sized_2.get()[i].get(),
              b.vectors.i8_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.i16_sized_2.get()[i].get(),
              b.vectors.i16_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.i32_sized_2.get()[i].get(),
              b.vectors.i32_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.i64_sized_2.get()[i].get(),
              b.vectors.i64_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.u8_sized_2.get()[i].get(),
              b.vectors.u8_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.u16_sized_2.get()[i].get(),
              b.vectors.u16_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.u32_sized_2.get()[i].get(),
              b.vectors.u32_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.u64_sized_2.get()[i].get(),
              b.vectors.u64_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.f32_sized_2.get()[i].get(),
              b.vectors.f32_sized_2.get()[i].get());
    EXPECT_EQ(a.vectors.f64_sized_2.get()[i].get(),
              b.vectors.f64_sized_2.get()[i].get());
    for (uint8_t j = 0; j < kArbitraryConstant; ++j) {
      EXPECT_TRUE(HandlesEq(a.vectors.handle_sized_2.get()[i].get()[j],
                            b.vectors.handle_sized_2.get()[i].get()[j]));
    }
  }

  EXPECT_EQ(a.vectors.b_nullable_0.is_null(), b.vectors.b_nullable_0.is_null());
  EXPECT_EQ(a.vectors.i8_nullable_0.is_null(),
            b.vectors.i8_nullable_0.is_null());
  EXPECT_EQ(a.vectors.i16_nullable_0.is_null(),
            b.vectors.i16_nullable_0.is_null());
  EXPECT_EQ(a.vectors.i32_nullable_0.is_null(),
            b.vectors.i32_nullable_0.is_null());
  EXPECT_EQ(a.vectors.i64_nullable_0.is_null(),
            b.vectors.i64_nullable_0.is_null());
  EXPECT_EQ(a.vectors.u8_nullable_0.is_null(),
            b.vectors.u8_nullable_0.is_null());
  EXPECT_EQ(a.vectors.u16_nullable_0.is_null(),
            b.vectors.u16_nullable_0.is_null());
  EXPECT_EQ(a.vectors.u32_nullable_0.is_null(),
            b.vectors.u32_nullable_0.is_null());
  EXPECT_EQ(a.vectors.u64_nullable_0.is_null(),
            b.vectors.u64_nullable_0.is_null());
  EXPECT_EQ(a.vectors.f32_nullable_0.is_null(),
            b.vectors.f32_nullable_0.is_null());
  EXPECT_EQ(a.vectors.f64_nullable_0.is_null(),
            b.vectors.f64_nullable_0.is_null());
  EXPECT_EQ(a.vectors.handle_nullable_0.is_null(),
            b.vectors.handle_nullable_0.is_null());

  EXPECT_EQ(a.vectors.b_nullable_1.is_null(), b.vectors.b_nullable_1.is_null());
  EXPECT_EQ(a.vectors.i8_nullable_1.is_null(),
            b.vectors.i8_nullable_1.is_null());
  EXPECT_EQ(a.vectors.i16_nullable_1.is_null(),
            b.vectors.i16_nullable_1.is_null());
  EXPECT_EQ(a.vectors.i32_nullable_1.is_null(),
            b.vectors.i32_nullable_1.is_null());
  EXPECT_EQ(a.vectors.i64_nullable_1.is_null(),
            b.vectors.i64_nullable_1.is_null());
  EXPECT_EQ(a.vectors.u8_nullable_1.is_null(),
            b.vectors.u8_nullable_1.is_null());
  EXPECT_EQ(a.vectors.u16_nullable_1.is_null(),
            b.vectors.u16_nullable_1.is_null());
  EXPECT_EQ(a.vectors.u32_nullable_1.is_null(),
            b.vectors.u32_nullable_1.is_null());
  EXPECT_EQ(a.vectors.u64_nullable_1.is_null(),
            b.vectors.u64_nullable_1.is_null());
  EXPECT_EQ(a.vectors.f32_nullable_1.is_null(),
            b.vectors.f32_nullable_1.is_null());
  EXPECT_EQ(a.vectors.f64_nullable_1.is_null(),
            b.vectors.f64_nullable_1.is_null());
  EXPECT_EQ(a.vectors.handle_nullable_1.is_null(),
            b.vectors.handle_nullable_1.is_null());

  for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
    EXPECT_EQ(a.vectors.i8_nullable_1.get()[i].get(),
              b.vectors.i8_nullable_1.get()[i].get());
  }

  EXPECT_EQ(a.vectors.b_nullable_sized_0.is_null(),
            b.vectors.b_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.i8_nullable_sized_0.is_null(),
            b.vectors.i8_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.i16_nullable_sized_0.is_null(),
            b.vectors.i16_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.i32_nullable_sized_0.is_null(),
            b.vectors.i32_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.i64_nullable_sized_0.is_null(),
            b.vectors.i64_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.u8_nullable_sized_0.is_null(),
            b.vectors.u8_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.u16_nullable_sized_0.is_null(),
            b.vectors.u16_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.u32_nullable_sized_0.is_null(),
            b.vectors.u32_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.u64_nullable_sized_0.is_null(),
            b.vectors.u64_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.f32_nullable_sized_0.is_null(),
            b.vectors.f32_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.f64_nullable_sized_0.is_null(),
            b.vectors.f64_nullable_sized_0.is_null());
  EXPECT_EQ(a.vectors.handle_nullable_sized_0.is_null(),
            b.vectors.handle_nullable_sized_0.is_null());

  EXPECT_EQ(a.vectors.i16_nullable_sized_0.get(),
            b.vectors.i16_nullable_sized_0.get());

  EXPECT_EQ(a.vectors.b_nullable_sized_1.is_null(),
            b.vectors.b_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.i8_nullable_sized_1.is_null(),
            b.vectors.i8_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.i16_nullable_sized_1.is_null(),
            b.vectors.i16_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.i32_nullable_sized_1.is_null(),
            b.vectors.i32_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.i64_nullable_sized_1.is_null(),
            b.vectors.i64_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.u8_nullable_sized_1.is_null(),
            b.vectors.u8_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.u16_nullable_sized_1.is_null(),
            b.vectors.u16_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.u32_nullable_sized_1.is_null(),
            b.vectors.u32_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.u64_nullable_sized_1.is_null(),
            b.vectors.u64_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.f32_nullable_sized_1.is_null(),
            b.vectors.f32_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.f64_nullable_sized_1.is_null(),
            b.vectors.f64_nullable_sized_1.is_null());
  EXPECT_EQ(a.vectors.handle_nullable_sized_1.is_null(),
            b.vectors.handle_nullable_sized_1.is_null());

  EXPECT_EQ(a.vectors.f64_nullable_sized_1.get(),
            b.vectors.f64_nullable_sized_1.get());

  EXPECT_EQ(a.vectors.b_nullable_sized_2.is_null(),
            b.vectors.b_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.i8_nullable_sized_2.is_null(),
            b.vectors.i8_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.i16_nullable_sized_2.is_null(),
            b.vectors.i16_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.i32_nullable_sized_2.is_null(),
            b.vectors.i32_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.i64_nullable_sized_2.is_null(),
            b.vectors.i64_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.u8_nullable_sized_2.is_null(),
            b.vectors.u8_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.u16_nullable_sized_2.is_null(),
            b.vectors.u16_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.u32_nullable_sized_2.is_null(),
            b.vectors.u32_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.u64_nullable_sized_2.is_null(),
            b.vectors.u64_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.f32_nullable_sized_2.is_null(),
            b.vectors.f32_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.f64_nullable_sized_2.is_null(),
            b.vectors.f64_nullable_sized_2.is_null());
  EXPECT_EQ(a.vectors.handle_nullable_sized_2.is_null(),
            b.vectors.handle_nullable_sized_2.is_null());

  for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
    for (uint8_t j = 0; j < kArbitraryConstant; ++j) {
      EXPECT_TRUE(
          HandlesEq(a.vectors.handle_nullable_sized_2.get()[i].get()[j],
                    b.vectors.handle_nullable_sized_2.get()[i].get()[j]));
    }
  }

  // handles
  EXPECT_TRUE(HandlesEq(a.handles.handle_handle, b.handles.handle_handle));
  EXPECT_TRUE(HandlesEq(a.handles.process_handle, b.handles.process_handle));
  EXPECT_TRUE(HandlesEq(a.handles.thread_handle, b.handles.thread_handle));
  EXPECT_TRUE(HandlesEq(a.handles.vmo_handle, b.handles.vmo_handle));
  EXPECT_TRUE(HandlesEq(a.handles.event_handle, b.handles.event_handle));
  EXPECT_TRUE(HandlesEq(a.handles.port_handle, b.handles.port_handle));
  EXPECT_TRUE(HandlesEq(a.handles.log_handle, b.handles.log_handle));
  EXPECT_TRUE(HandlesEq(a.handles.socket_handle, b.handles.socket_handle));
  EXPECT_TRUE(
      HandlesEq(a.handles.eventpair_handle, b.handles.eventpair_handle));
  EXPECT_TRUE(HandlesEq(a.handles.job_handle, b.handles.job_handle));
  EXPECT_TRUE(HandlesEq(a.handles.vmar_handle, b.handles.vmar_handle));
  EXPECT_TRUE(HandlesEq(a.handles.fifo_handle, b.handles.fifo_handle));
  EXPECT_TRUE(HandlesEq(a.handles.timer_handle, b.handles.timer_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_handle_handle,
                        b.handles.nullable_handle_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_process_handle,
                        b.handles.nullable_process_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_thread_handle,
                        b.handles.nullable_thread_handle));
  EXPECT_TRUE(
      HandlesEq(a.handles.nullable_vmo_handle, b.handles.nullable_vmo_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_channel_handle,
                        b.handles.nullable_channel_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_event_handle,
                        b.handles.nullable_event_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_port_handle,
                        b.handles.nullable_port_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_interrupt_handle,
                        b.handles.nullable_interrupt_handle));
  EXPECT_TRUE(
      HandlesEq(a.handles.nullable_log_handle, b.handles.nullable_log_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_socket_handle,
                        b.handles.nullable_socket_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_eventpair_handle,
                        b.handles.nullable_eventpair_handle));
  EXPECT_TRUE(
      HandlesEq(a.handles.nullable_job_handle, b.handles.nullable_job_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_vmar_handle,
                        b.handles.nullable_vmar_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_fifo_handle,
                        b.handles.nullable_fifo_handle));
  EXPECT_TRUE(HandlesEq(a.handles.nullable_timer_handle,
                        b.handles.nullable_timer_handle));

  // strings
  EXPECT_EQ(a.strings.s, b.strings.s);
  EXPECT_EQ(a.strings.size_0_s, b.strings.size_0_s);
  EXPECT_EQ(a.strings.size_1_s, b.strings.size_1_s);
  EXPECT_EQ(a.strings.nullable_size_0_s.get(),
            b.strings.nullable_size_0_s.get());
  EXPECT_EQ(a.strings.nullable_size_1_s.is_null(),
            b.strings.nullable_size_1_s.is_null());

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

  // structs
  EXPECT_EQ(a.structs.s.s, b.structs.s.s);
  EXPECT_EQ(a.structs.nullable_s, b.structs.nullable_s);

  // unions
  EXPECT_EQ(a.unions.u.is_s(), b.unions.u.is_s());
  EXPECT_EQ(a.unions.u.s(), b.unions.u.s());
  EXPECT_EQ(a.unions.nullable_u->is_b(), b.unions.nullable_u->is_b());
  EXPECT_EQ(a.unions.nullable_u->b(), b.unions.nullable_u->b());

  // bool
  EXPECT_EQ(a.b, b.b);
}

std::string RandomUTF8(size_t count) {
  // Prepare randomness.
  std::default_random_engine rand_engine;
  // Using randomness to avoid having to come up with varied values by hand.
  // Seed deterministically so that this function's outputs are predictable.
  rand_engine.seed(count);
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
    FXL_CHECK(random_string.size() <= count);
  } while (random_string.size() < count);
  return random_string;
}

void Initialize(Struct* s) {
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
  std::string random_string =
      RandomUTF8(fidl::test::compatibility::strings_size);
  std::string random_short_string = RandomUTF8(kArbitraryConstant);

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
  s->vectors.b_0 = VectorPtr<bool>(
      std::vector<bool>(kArbitraryVectorSize, bool_distribution(rand_engine)));
  s->vectors.i8_0 = VectorPtr<int8_t>(std::vector<int8_t>(
      kArbitraryVectorSize, int8_distribution(rand_engine)));
  s->vectors.i16_0 = VectorPtr<int16_t>(std::vector<int16_t>(
      kArbitraryVectorSize, int16_distribution(rand_engine)));
  s->vectors.i32_0 = VectorPtr<int32_t>(std::vector<int32_t>(
      kArbitraryVectorSize, int32_distribution(rand_engine)));
  s->vectors.i64_0 = VectorPtr<int64_t>(std::vector<int64_t>(
      kArbitraryVectorSize, int64_distribution(rand_engine)));
  s->vectors.u8_0 = VectorPtr<uint8_t>(std::vector<uint8_t>(
      kArbitraryVectorSize, uint8_distribution(rand_engine)));
  s->vectors.u16_0 = VectorPtr<uint16_t>(std::vector<uint16_t>(
      kArbitraryVectorSize, uint16_distribution(rand_engine)));
  s->vectors.u32_0 = VectorPtr<uint32_t>(std::vector<uint32_t>(
      kArbitraryVectorSize, uint32_distribution(rand_engine)));
  s->vectors.u64_0 = VectorPtr<uint64_t>(std::vector<uint64_t>(
      kArbitraryVectorSize, uint64_distribution(rand_engine)));
  s->vectors.f32_0 = VectorPtr<float>(std::vector<float>(
      kArbitraryVectorSize, float_distribution(rand_engine)));
  s->vectors.f64_0 = VectorPtr<double>(std::vector<double>(
      kArbitraryVectorSize, double_distribution(rand_engine)));

  {
    std::vector<zx::handle> underlying_vec;
    for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
      underlying_vec.emplace_back(Handle());
    }
    s->vectors.handle_0 = VectorPtr<zx::handle>(std::move(underlying_vec));
  }

  {
    std::vector<VectorPtr<bool>> bool_outer_vector;
    std::vector<VectorPtr<int8_t>> int8_outer_vector;
    std::vector<VectorPtr<int16_t>> int16_outer_vector;
    std::vector<VectorPtr<int32_t>> int32_outer_vector;
    std::vector<VectorPtr<int64_t>> int64_outer_vector;
    std::vector<VectorPtr<uint8_t>> uint8_outer_vector;
    std::vector<VectorPtr<uint16_t>> uint16_outer_vector;
    std::vector<VectorPtr<uint32_t>> uint32_outer_vector;
    std::vector<VectorPtr<uint64_t>> uint64_outer_vector;
    std::vector<VectorPtr<float>> float_outer_vector;
    std::vector<VectorPtr<double>> double_outer_vector;
    std::vector<VectorPtr<zx::handle>> handle_outer_vector;
    for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
      bool_outer_vector.emplace_back(VectorPtr<bool>(std::vector<bool>(
          kArbitraryConstant, bool_distribution(rand_engine))));
      int8_outer_vector.emplace_back(VectorPtr<int8_t>(std::vector<int8_t>(
          kArbitraryConstant, int8_distribution(rand_engine))));
      int16_outer_vector.emplace_back(VectorPtr<int16_t>(std::vector<int16_t>(
          kArbitraryConstant, int16_distribution(rand_engine))));
      int32_outer_vector.emplace_back(VectorPtr<int32_t>(std::vector<int32_t>(
          kArbitraryConstant, int32_distribution(rand_engine))));
      int64_outer_vector.emplace_back(VectorPtr<int64_t>(std::vector<int64_t>(
          kArbitraryConstant, int64_distribution(rand_engine))));
      uint8_outer_vector.emplace_back(VectorPtr<uint8_t>(std::vector<uint8_t>(
          kArbitraryConstant, uint8_distribution(rand_engine))));
      uint16_outer_vector.emplace_back(
          VectorPtr<uint16_t>(std::vector<uint16_t>(
              kArbitraryConstant, uint16_distribution(rand_engine))));
      uint32_outer_vector.emplace_back(
          VectorPtr<uint32_t>(std::vector<uint32_t>(
              kArbitraryConstant, uint32_distribution(rand_engine))));
      uint64_outer_vector.emplace_back(
          VectorPtr<uint64_t>(std::vector<uint64_t>(
              kArbitraryConstant, uint64_distribution(rand_engine))));
      float_outer_vector.emplace_back(VectorPtr<float>(std::vector<float>(
          kArbitraryConstant, float_distribution(rand_engine))));
      double_outer_vector.emplace_back(VectorPtr<double>(std::vector<double>(
          kArbitraryConstant, double_distribution(rand_engine))));
      std::vector<zx::handle> handle_inner_vector;
      for (uint8_t i = 0; i < kArbitraryConstant; ++i) {
        handle_inner_vector.emplace_back(Handle());
      }
      handle_outer_vector.emplace_back(
          VectorPtr<zx::handle>(std::move(handle_inner_vector)));
    }
    s->vectors.b_1 = VectorPtr<VectorPtr<bool>>(std::move(bool_outer_vector));
    s->vectors.i8_1 =
        VectorPtr<VectorPtr<int8_t>>(std::move(int8_outer_vector));
    s->vectors.i16_1 =
        VectorPtr<VectorPtr<int16_t>>(std::move(int16_outer_vector));
    s->vectors.i32_1 =
        VectorPtr<VectorPtr<int32_t>>(std::move(int32_outer_vector));
    s->vectors.i64_1 =
        VectorPtr<VectorPtr<int64_t>>(std::move(int64_outer_vector));
    s->vectors.u8_1 =
        VectorPtr<VectorPtr<uint8_t>>(std::move(uint8_outer_vector));
    s->vectors.u16_1 =
        VectorPtr<VectorPtr<uint16_t>>(std::move(uint16_outer_vector));
    s->vectors.u32_1 =
        VectorPtr<VectorPtr<uint32_t>>(std::move(uint32_outer_vector));
    s->vectors.u64_1 =
        VectorPtr<VectorPtr<uint64_t>>(std::move(uint64_outer_vector));
    s->vectors.f32_1 =
        VectorPtr<VectorPtr<float>>(std::move(float_outer_vector));
    s->vectors.f64_1 =
        VectorPtr<VectorPtr<double>>(std::move(double_outer_vector));
    s->vectors.handle_1 =
        VectorPtr<VectorPtr<zx::handle>>(std::move(handle_outer_vector));
  }

  s->vectors.b_sized_0 =
      VectorPtr<bool>(std::vector<bool>{bool_distribution(rand_engine)});
  s->vectors.i8_sized_0 =
      VectorPtr<int8_t>(std::vector<int8_t>{int8_distribution(rand_engine)});
  s->vectors.i16_sized_0 =
      VectorPtr<int16_t>(std::vector<int16_t>{int16_distribution(rand_engine)});
  s->vectors.i32_sized_0 =
      VectorPtr<int32_t>(std::vector<int32_t>{int32_distribution(rand_engine)});
  s->vectors.i64_sized_0 =
      VectorPtr<int64_t>(std::vector<int64_t>{int64_distribution(rand_engine)});
  s->vectors.u8_sized_0 =
      VectorPtr<uint8_t>(std::vector<uint8_t>{uint8_distribution(rand_engine)});
  s->vectors.u16_sized_0 = VectorPtr<uint16_t>(
      std::vector<uint16_t>{uint16_distribution(rand_engine)});
  s->vectors.u32_sized_0 = VectorPtr<uint32_t>(
      std::vector<uint32_t>{uint32_distribution(rand_engine)});
  s->vectors.u64_sized_0 = VectorPtr<uint64_t>(
      std::vector<uint64_t>{uint64_distribution(rand_engine)});
  s->vectors.f32_sized_0 =
      VectorPtr<float>(std::vector<float>{float_distribution(rand_engine)});
  s->vectors.f64_sized_0 =
      VectorPtr<double>(std::vector<double>{double_distribution(rand_engine)});

  {
    std::vector<zx::handle> underlying_vec;
    underlying_vec.emplace_back(Handle());
    s->vectors.handle_sized_0 =
        VectorPtr<zx::handle>(std::move(underlying_vec));
  }

  s->vectors.b_sized_1 = VectorPtr<bool>(
      std::vector<bool>(fidl::test::compatibility::vectors_size,
                        bool_distribution(rand_engine)));
  s->vectors.i8_sized_1 = VectorPtr<int8_t>(
      std::vector<int8_t>(fidl::test::compatibility::vectors_size,
                          int8_distribution(rand_engine)));
  s->vectors.i16_sized_1 = VectorPtr<int16_t>(
      std::vector<int16_t>(fidl::test::compatibility::vectors_size,
                           int16_distribution(rand_engine)));
  s->vectors.i32_sized_1 = VectorPtr<int32_t>(
      std::vector<int32_t>(fidl::test::compatibility::vectors_size,
                           int32_distribution(rand_engine)));
  s->vectors.i64_sized_1 = VectorPtr<int64_t>(
      std::vector<int64_t>(fidl::test::compatibility::vectors_size,
                           int64_distribution(rand_engine)));
  s->vectors.u8_sized_1 = VectorPtr<uint8_t>(
      std::vector<uint8_t>(fidl::test::compatibility::vectors_size,
                           uint8_distribution(rand_engine)));
  s->vectors.u16_sized_1 = VectorPtr<uint16_t>(
      std::vector<uint16_t>(fidl::test::compatibility::vectors_size,
                            uint16_distribution(rand_engine)));
  s->vectors.u32_sized_1 = VectorPtr<uint32_t>(
      std::vector<uint32_t>(fidl::test::compatibility::vectors_size,
                            uint32_distribution(rand_engine)));
  s->vectors.u64_sized_1 = VectorPtr<uint64_t>(
      std::vector<uint64_t>(fidl::test::compatibility::vectors_size,
                            uint64_distribution(rand_engine)));
  s->vectors.f32_sized_1 = VectorPtr<float>(
      std::vector<float>(fidl::test::compatibility::vectors_size,
                         float_distribution(rand_engine)));
  s->vectors.f64_sized_1 = VectorPtr<double>(
      std::vector<double>(fidl::test::compatibility::vectors_size,
                          double_distribution(rand_engine)));
  {
    std::vector<zx::handle> underlying_vec;
    for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
      underlying_vec.emplace_back(Handle());
    }
    s->vectors.handle_sized_1 =
        VectorPtr<zx::handle>(std::move(underlying_vec));
  }
  {
    std::vector<VectorPtr<bool>> bool_outer_vector;
    std::vector<VectorPtr<int8_t>> int8_outer_vector;
    std::vector<VectorPtr<int16_t>> int16_outer_vector;
    std::vector<VectorPtr<int32_t>> int32_outer_vector;
    std::vector<VectorPtr<int64_t>> int64_outer_vector;
    std::vector<VectorPtr<uint8_t>> uint8_outer_vector;
    std::vector<VectorPtr<uint16_t>> uint16_outer_vector;
    std::vector<VectorPtr<uint32_t>> uint32_outer_vector;
    std::vector<VectorPtr<uint64_t>> uint64_outer_vector;
    std::vector<VectorPtr<float>> float_outer_vector;
    std::vector<VectorPtr<double>> double_outer_vector;
    std::vector<VectorPtr<zx::handle>> handle_outer_vector;
    for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
      bool_outer_vector.emplace_back(VectorPtr<bool>(std::vector<bool>(
          kArbitraryConstant, bool_distribution(rand_engine))));
      int8_outer_vector.emplace_back(VectorPtr<int8_t>(std::vector<int8_t>(
          kArbitraryConstant, int8_distribution(rand_engine))));
      int16_outer_vector.emplace_back(VectorPtr<int16_t>(std::vector<int16_t>(
          kArbitraryConstant, int16_distribution(rand_engine))));
      int32_outer_vector.emplace_back(VectorPtr<int32_t>(std::vector<int32_t>(
          kArbitraryConstant, int32_distribution(rand_engine))));
      int64_outer_vector.emplace_back(VectorPtr<int64_t>(std::vector<int64_t>(
          kArbitraryConstant, int64_distribution(rand_engine))));
      uint8_outer_vector.emplace_back(VectorPtr<uint8_t>(std::vector<uint8_t>(
          kArbitraryConstant, uint8_distribution(rand_engine))));
      uint16_outer_vector.emplace_back(
          VectorPtr<uint16_t>(std::vector<uint16_t>(
              kArbitraryConstant, uint16_distribution(rand_engine))));
      uint32_outer_vector.emplace_back(
          VectorPtr<uint32_t>(std::vector<uint32_t>(
              kArbitraryConstant, uint32_distribution(rand_engine))));
      uint64_outer_vector.emplace_back(
          VectorPtr<uint64_t>(std::vector<uint64_t>(
              kArbitraryConstant, uint64_distribution(rand_engine))));
      float_outer_vector.emplace_back(VectorPtr<float>(std::vector<float>(
          kArbitraryConstant, float_distribution(rand_engine))));
      double_outer_vector.emplace_back(VectorPtr<double>(std::vector<double>(
          kArbitraryConstant, double_distribution(rand_engine))));
      std::vector<zx::handle> handle_inner_vector;
      for (uint8_t i = 0; i < kArbitraryConstant; ++i) {
        handle_inner_vector.emplace_back(Handle());
      }
      handle_outer_vector.emplace_back(
          VectorPtr<zx::handle>(std::move(handle_inner_vector)));
    }
    s->vectors.b_sized_2 =
        VectorPtr<VectorPtr<bool>>(std::move(bool_outer_vector));
    s->vectors.i8_sized_2 =
        VectorPtr<VectorPtr<int8_t>>(std::move(int8_outer_vector));
    s->vectors.i16_sized_2 =
        VectorPtr<VectorPtr<int16_t>>(std::move(int16_outer_vector));
    s->vectors.i32_sized_2 =
        VectorPtr<VectorPtr<int32_t>>(std::move(int32_outer_vector));
    s->vectors.i64_sized_2 =
        VectorPtr<VectorPtr<int64_t>>(std::move(int64_outer_vector));
    s->vectors.u8_sized_2 =
        VectorPtr<VectorPtr<uint8_t>>(std::move(uint8_outer_vector));
    s->vectors.u16_sized_2 =
        VectorPtr<VectorPtr<uint16_t>>(std::move(uint16_outer_vector));
    s->vectors.u32_sized_2 =
        VectorPtr<VectorPtr<uint32_t>>(std::move(uint32_outer_vector));
    s->vectors.u64_sized_2 =
        VectorPtr<VectorPtr<uint64_t>>(std::move(uint64_outer_vector));
    s->vectors.f32_sized_2 =
        VectorPtr<VectorPtr<float>>(std::move(float_outer_vector));
    s->vectors.f64_sized_2 =
        VectorPtr<VectorPtr<double>>(std::move(double_outer_vector));
    s->vectors.handle_sized_2 =
        VectorPtr<VectorPtr<zx::handle>>(std::move(handle_outer_vector));
  }

  // intentionally leave most of the nullable vectors as null, just set one
  // from each category.
  s->vectors.b_nullable_0 =
      VectorPtr<bool>(std::vector<bool>{bool_distribution(rand_engine)});
  {
    std::vector<VectorPtr<int8_t>> int8_outer_vector;
    for (uint8_t i = 0; i < kArbitraryVectorSize; ++i) {
      int8_outer_vector.emplace_back(VectorPtr<int8_t>(std::vector<int8_t>(
          kArbitraryConstant, int8_distribution(rand_engine))));
    }
    s->vectors.i8_nullable_1 =
        VectorPtr<VectorPtr<int8_t>>(std::move(int8_outer_vector));
  }
  s->vectors.i16_nullable_sized_0 =
      VectorPtr<int16_t>(std::vector<int16_t>{int16_distribution(rand_engine)});
  s->vectors.f64_nullable_sized_1 = VectorPtr<double>(
      std::vector<double>(fidl::test::compatibility::vectors_size,
                          double_distribution(rand_engine)));
  {
    std::vector<VectorPtr<zx::handle>> handle_outer_vector;
    for (uint32_t i = 0; i < fidl::test::compatibility::vectors_size; ++i) {
      std::vector<zx::handle> handle_inner_vector;
      for (uint8_t i = 0; i < kArbitraryConstant; ++i) {
        handle_inner_vector.emplace_back(Handle());
      }
      handle_outer_vector.emplace_back(
          VectorPtr<zx::handle>(std::move(handle_inner_vector)));
    }
    s->vectors.handle_nullable_sized_2 =
        VectorPtr<VectorPtr<zx::handle>>(std::move(handle_outer_vector));
  }

  // handles
  s->handles.handle_handle = Handle();

  ASSERT_EQ(ZX_OK, zx::process::self()->duplicate(ZX_RIGHT_SAME_RIGHTS,
                                                  &s->handles.process_handle));
  ASSERT_EQ(ZX_OK, zx::thread::create(
      *zx::unowned_process(zx::process::self()), "dummy", 5u, 0u,
      &s->handles.thread_handle));
  ASSERT_EQ(ZX_OK, zx::vmo::create(0u, 0u, &s->handles.vmo_handle));
  ASSERT_EQ(ZX_OK, zx::event::create(0u, &s->handles.event_handle));
  ASSERT_EQ(ZX_OK, zx::port::create(0u, &s->handles.port_handle));
  ASSERT_EQ(ZX_OK, zx::log::create(0u, &s->handles.log_handle));

  zx::socket socket1;
  ASSERT_EQ(ZX_OK, zx::socket::create(0u, &s->handles.socket_handle, &socket1));

  zx::eventpair eventpair1;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0u, &s->handles.eventpair_handle,
                                         &eventpair1));

  ASSERT_EQ(ZX_OK,
            zx::job::create(*zx::job::default_job(), 0u, &s->handles.job_handle));

  uintptr_t vmar_addr;
  ASSERT_EQ(ZX_OK, zx::vmar::root_self()->allocate(
                       0u, getpagesize(), ZX_VM_FLAG_CAN_MAP_READ,
                       &s->handles.vmar_handle, &vmar_addr));

  zx::fifo fifo1;
  ASSERT_EQ(ZX_OK,
            zx::fifo::create(1u, 1u, 0u, &s->handles.fifo_handle, &fifo1));

  ASSERT_EQ(ZX_OK, zx::timer::create(0u, ZX_CLOCK_MONOTONIC,
                                     &s->handles.timer_handle));

  // For the nullable ones, just set one of them.
  s->handles.nullable_handle_handle = Handle();

  // strings
  s->strings.s = fidl::StringPtr(random_string);
  s->strings.size_0_s = fidl::StringPtr(random_short_string);
  s->strings.size_1_s = fidl::StringPtr(random_string);
  s->strings.nullable_size_0_s = fidl::StringPtr(random_short_string);

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

  // structs
  s->structs.s.s = fidl::StringPtr(random_string);

  // unions
  s->unions.u.set_s(fidl::StringPtr(random_string));
  s->unions.nullable_u = fidl::test::compatibility::this_is_a_union::New();
  s->unions.nullable_u->set_b(bool_distribution(rand_engine));

  // bool
  s->b = bool_distribution(rand_engine);
}

class CompatibilityTest
    : public ::testing::TestWithParam<std::tuple<std::string, std::string>> {
 protected:
  void SetUp() override {
    proxy_url_ = ::testing::get<0>(GetParam());
    server_url_ = ::testing::get<1>(GetParam());
    // The FIDL support lib requires async_get_default_dispatcher() to return non-null.
    loop_.reset(new async::Loop(&kAsyncLoopConfigMakeDefault));
  }
  std::string proxy_url_;
  std::string server_url_;
  std::unique_ptr<async::Loop> loop_;
};

TEST_P(CompatibilityTest, EchoStruct) {
  RecordProperty("proxy_url", proxy_url_);
  RecordProperty("server_url", server_url_);
  Struct sent;
  Initialize(&sent);
  fidl::test::compatibility::EchoClientApp app;
  app.Start(proxy_url_);

  Struct sent_clone;
  sent.Clone(&sent_clone);
  Struct resp_clone;
  bool called_back = false;
  app.echo()->EchoStruct(
      std::move(sent), server_url_,
      [this, &resp_clone, &called_back](Struct resp) {
        ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
        called_back = true;
        loop_->Quit();
      });

  loop_->Run();
  ASSERT_TRUE(called_back);
  ExpectEq(sent_clone, resp_clone);
}

TEST_P(CompatibilityTest, EchoStructNoRetVal) {
  RecordProperty("proxy_url", proxy_url_);
  RecordProperty("server_url", server_url_);
  Struct sent;
  Initialize(&sent);
  fidl::test::compatibility::EchoClientApp app;
  app.Start(proxy_url_);

  Struct sent_clone;
  sent.Clone(&sent_clone);
  std::mutex m;
  std::condition_variable cv;
  fidl::test::compatibility::Struct resp_clone;
  bool event_received = false;
  app.echo().events().EchoEvent = [this, &resp_clone,
                                   &event_received](Struct resp) {
    resp.Clone(&resp_clone);
    event_received = true;
    loop_->Quit();
  };
  app.echo()->EchoStructNoRetVal(std::move(sent), server_url_);
  loop_->Run();
  ASSERT_TRUE(event_received);
  ExpectEq(sent_clone, resp_clone);
}

// It'd be better to take these on the command-line but googletest doesn't
// support instantiating tests after main() is called.
std::vector<std::string> ServerURLsFromEnv() {
  const char* servers_raw = getenv(kServersEnvVarName);
  FXL_CHECK(servers_raw != nullptr) << kUsage;
  std::vector<std::string> servers = fxl::SplitStringCopy(
      fxl::StringView(servers_raw, strlen(servers_raw)), ",",
      fxl::WhiteSpaceHandling::kTrimWhitespace,
      fxl::SplitResult::kSplitWantNonEmpty);
  FXL_CHECK(!servers.empty()) << kUsage;
  return servers;
}

}  // namespace

INSTANTIATE_TEST_CASE_P(
    CompatibilityTest, CompatibilityTest,
    ::testing::Combine(::testing::ValuesIn(ServerURLsFromEnv()),
                       ::testing::ValuesIn(ServerURLsFromEnv())));

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
