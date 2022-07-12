// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/cpp/fidl.h>
#include <fidl/test/imported/cpp/fidl.h>

#include <gtest/gtest.h>
#include <src/lib/fxl/test/test_settings.h>
#include <src/tests/fidl/compatibility/helpers.h>
#include <src/tests/fidl/compatibility/hlcpp_client_app.h>

using fidl::VectorPtr;

using fidl::test::compatibility::Echo_EchoStructWithError_Result;
using fidl::test::compatibility::RespondWith;
using fidl::test::compatibility::Struct;
using fidl::test::compatibility::this_is_a_union;

using fidl_test_compatibility_helpers::ExtractShortName;
using fidl_test_compatibility_helpers::ForAllServers;
using fidl_test_compatibility_helpers::GetServersUnderTest;
using fidl_test_compatibility_helpers::Handle;
using fidl_test_compatibility_helpers::HandlesEq;
using fidl_test_compatibility_helpers::kArbitraryConstant;
using fidl_test_compatibility_helpers::kArbitraryVectorSize;
using fidl_test_compatibility_helpers::PrintSummary;
using fidl_test_compatibility_helpers::RandomUTF8;
using fidl_test_compatibility_helpers::Servers;
using fidl_test_compatibility_helpers::Summary;

namespace {

void InitializeStruct(Struct* s) {
  // Prepare randomness.
  std::default_random_engine rand_engine;
  // Using randomness to avoid having to come up with varied values by hand.
  // Seed deterministically so that this function's outputs are predictable.
  rand_engine.seed(42);
  std::bernoulli_distribution bool_distribution;
  std::uniform_int_distribution<int16_t> int8_distribution(0, std::numeric_limits<int8_t>::max());
  std::uniform_int_distribution<int16_t> int16_distribution;
  std::uniform_int_distribution<int32_t> int32_distribution;
  std::uniform_int_distribution<int64_t> int64_distribution;
  std::uniform_int_distribution<uint16_t> uint8_distribution(0,
                                                             std::numeric_limits<uint8_t>::max());
  std::uniform_int_distribution<uint16_t> uint16_distribution;
  std::uniform_int_distribution<uint32_t> uint32_distribution;
  std::uniform_int_distribution<uint64_t> uint64_distribution;
  std::uniform_real_distribution<float> float_distribution;
  std::uniform_real_distribution<double> double_distribution;
  std::string random_string = RandomUTF8(fidl::test::compatibility::strings_size, rand_engine);
  std::string random_short_string = RandomUTF8(kArbitraryConstant, rand_engine);

  // primitive_types
  s->primitive_types.b = bool_distribution(rand_engine);
  s->primitive_types.i8 = static_cast<int8_t>(int8_distribution(rand_engine));
  s->primitive_types.i16 = int16_distribution(rand_engine);
  s->primitive_types.i32 = int32_distribution(rand_engine);
  s->primitive_types.i64 = int64_distribution(rand_engine);
  s->primitive_types.u8 = static_cast<uint8_t>(uint8_distribution(rand_engine));
  s->primitive_types.u16 = uint16_distribution(rand_engine);
  s->primitive_types.u32 = uint32_distribution(rand_engine);
  s->primitive_types.u64 = uint64_distribution(rand_engine);
  s->primitive_types.f32 = float_distribution(rand_engine);
  s->primitive_types.f64 = double_distribution(rand_engine);

  // arrays
  s->arrays.b_0[0] = bool_distribution(rand_engine);
  s->arrays.i8_0[0] = static_cast<int8_t>(int8_distribution(rand_engine));
  s->arrays.i16_0[0] = int16_distribution(rand_engine);
  s->arrays.i32_0[0] = int32_distribution(rand_engine);
  s->arrays.i64_0[0] = int64_distribution(rand_engine);
  s->arrays.u8_0[0] = static_cast<uint8_t>(uint8_distribution(rand_engine));
  s->arrays.u16_0[0] = uint16_distribution(rand_engine);
  s->arrays.u32_0[0] = uint32_distribution(rand_engine);
  s->arrays.u64_0[0] = uint64_distribution(rand_engine);
  s->arrays.f32_0[0] = float_distribution(rand_engine);
  s->arrays.f64_0[0] = double_distribution(rand_engine);
  s->arrays.handle_0[0] = Handle();

  for (uint32_t i = 0; i < fidl::test::compatibility::arrays_size; ++i) {
    s->arrays.b_1[i] = bool_distribution(rand_engine);
    s->arrays.i8_1[i] = static_cast<int8_t>(int8_distribution(rand_engine));
    s->arrays.i16_1[i] = int16_distribution(rand_engine);
    s->arrays.i32_1[i] = int32_distribution(rand_engine);
    s->arrays.i64_1[i] = int64_distribution(rand_engine);
    s->arrays.u8_1[i] = static_cast<uint8_t>(uint8_distribution(rand_engine));
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
      s->arrays_2d.i8[i][j] = static_cast<int8_t>(int8_distribution(rand_engine));
      s->arrays_2d.i16[i][j] = int16_distribution(rand_engine);
      s->arrays_2d.i32[i][j] = int32_distribution(rand_engine);
      s->arrays_2d.i64[i][j] = int64_distribution(rand_engine);
      s->arrays_2d.u8[i][j] = static_cast<uint8_t>(uint8_distribution(rand_engine));
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
  s->vectors.i8_0 = std::vector<int8_t>(kArbitraryVectorSize,
                                        static_cast<int8_t>(int8_distribution(rand_engine)));
  s->vectors.i16_0 = std::vector<int16_t>(kArbitraryVectorSize, int16_distribution(rand_engine));
  s->vectors.i32_0 = std::vector<int32_t>(kArbitraryVectorSize, int32_distribution(rand_engine));
  s->vectors.i64_0 = std::vector<int64_t>(kArbitraryVectorSize, int64_distribution(rand_engine));
  s->vectors.u8_0 = std::vector<uint8_t>(kArbitraryVectorSize,
                                         static_cast<uint8_t>(uint8_distribution(rand_engine)));
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
      int8_outer_vector.emplace_back(std::vector<int8_t>(std::vector<int8_t>(
          kArbitraryConstant, static_cast<int8_t>(int8_distribution(rand_engine)))));
      int16_outer_vector.emplace_back(std::vector<int16_t>(
          std::vector<int16_t>(kArbitraryConstant, int16_distribution(rand_engine))));
      int32_outer_vector.emplace_back(std::vector<int32_t>(
          std::vector<int32_t>(kArbitraryConstant, int32_distribution(rand_engine))));
      int64_outer_vector.emplace_back(std::vector<int64_t>(
          std::vector<int64_t>(kArbitraryConstant, int64_distribution(rand_engine))));
      uint8_outer_vector.emplace_back(std::vector<uint8_t>(std::vector<uint8_t>(
          kArbitraryConstant, static_cast<uint8_t>(uint8_distribution(rand_engine)))));
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
  s->vectors.i8_sized_0 = std::vector<int8_t>{static_cast<int8_t>(int8_distribution(rand_engine))};
  s->vectors.i16_sized_0 = std::vector<int16_t>{int16_distribution(rand_engine)};
  s->vectors.i32_sized_0 = std::vector<int32_t>{int32_distribution(rand_engine)};
  s->vectors.i64_sized_0 = std::vector<int64_t>{int64_distribution(rand_engine)};
  s->vectors.u8_sized_0 =
      std::vector<uint8_t>{static_cast<uint8_t>(uint8_distribution(rand_engine))};
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
  s->vectors.i8_sized_1 = std::vector<int8_t>(fidl::test::compatibility::vectors_size,
                                              static_cast<int8_t>(int8_distribution(rand_engine)));
  s->vectors.i16_sized_1 = std::vector<int16_t>(fidl::test::compatibility::vectors_size,
                                                int16_distribution(rand_engine));
  s->vectors.i32_sized_1 = std::vector<int32_t>(fidl::test::compatibility::vectors_size,
                                                int32_distribution(rand_engine));
  s->vectors.i64_sized_1 = std::vector<int64_t>(fidl::test::compatibility::vectors_size,
                                                int64_distribution(rand_engine));
  s->vectors.u8_sized_1 =
      std::vector<uint8_t>(fidl::test::compatibility::vectors_size,
                           static_cast<uint8_t>(uint8_distribution(rand_engine)));
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
      int8_outer_vector.emplace_back(std::vector<int8_t>(std::vector<int8_t>(
          kArbitraryConstant, static_cast<int8_t>(int8_distribution(rand_engine)))));
      int16_outer_vector.emplace_back(std::vector<int16_t>(
          std::vector<int16_t>(kArbitraryConstant, int16_distribution(rand_engine))));
      int32_outer_vector.emplace_back(std::vector<int32_t>(
          std::vector<int32_t>(kArbitraryConstant, int32_distribution(rand_engine))));
      int64_outer_vector.emplace_back(std::vector<int64_t>(
          std::vector<int64_t>(kArbitraryConstant, int64_distribution(rand_engine))));
      uint8_outer_vector.emplace_back(std::vector<uint8_t>(std::vector<uint8_t>(
          kArbitraryConstant, static_cast<uint8_t>(uint8_distribution(rand_engine)))));
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
      int8_outer_vector.emplace_back(std::vector<int8_t>(std::vector<int8_t>(
          kArbitraryConstant, static_cast<int8_t>(int8_distribution(rand_engine)))));
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
  ASSERT_EQ(ZX_OK, zx::vmar::root_self()->allocate(ZX_VM_CAN_MAP_READ, 0u, getpagesize(),
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
  s->unions.nullable_u = std::make_unique<this_is_a_union>();
  s->unions.nullable_u->set_b(bool_distribution(rand_engine));

  s->table.set_s(random_string);
  s->xunion_.set_s(random_string);

  // bool
  s->b = bool_distribution(rand_engine);
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

Servers servers;
Summary summary;

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

TEST(Struct, EchoStruct) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
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
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
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
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
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
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
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

TEST(Struct, EchoNamedStruct) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (struct)"] =
        false;
    fidl::test::imported::SimpleStruct sent;
    sent.f1 = true;
    sent.f2 = 1;

    fidl::test::imported::SimpleStruct sent_clone;
    sent.Clone(&sent_clone);
    fidl::test::imported::SimpleStruct resp_clone;
    bool called_back = false;
    proxy->EchoNamedStruct(
        std::move(sent), server_url,
        [&loop, &resp_clone, &called_back](fidl::test::imported::SimpleStruct resp) {
          ASSERT_EQ(ZX_OK, resp.Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    EXPECT_EQ(sent_clone.f1, resp_clone.f1);
    EXPECT_EQ(sent_clone.f2, resp_clone.f2);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) + " (struct)"] =
        true;
  });
}

TEST(Struct, EchoNamedStructWithErrorSuccessCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct result success)"] = false;
    fidl::test::imported::SimpleStruct sent;
    sent.f1 = false;
    sent.f2 = 2;
    const uint32_t err = 12;

    fidl::test::imported::SimpleStruct sent_clone;
    sent.Clone(&sent_clone);

    fidl::test::imported::SimpleStruct resp_clone;
    bool called_back = false;
    proxy->EchoNamedStructWithError(
        std::move(sent), err, server_url, fidl::test::imported::WantResponse::SUCCESS,
        [&loop, &resp_clone,
         &called_back](fidl::test::compatibility::Echo_EchoNamedStructWithError_Result resp) {
          ASSERT_TRUE(resp.is_response());
          ASSERT_EQ(ZX_OK, resp.response().value.Clone(&resp_clone));
          called_back = true;
          loop.Quit();
        });

    loop.Run();
    ASSERT_TRUE(called_back);
    EXPECT_EQ(sent_clone.f1, resp_clone.f1);
    EXPECT_EQ(sent_clone.f2, resp_clone.f2);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct result success)"] = true;
  });
}

TEST(Struct, EchoNamedStructWithErrorErrorCase) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string& proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct result error)"] = false;
    fidl::test::imported::SimpleStruct sent;
    sent.f1 = true;
    sent.f2 = 3;
    const uint32_t err = 13;

    bool called_back = false;
    proxy->EchoNamedStructWithError(
        std::move(sent), err, server_url, fidl::test::imported::WantResponse::ERR,
        [&loop, &err,
         &called_back](fidl::test::compatibility::Echo_EchoNamedStructWithError_Result resp) {
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

TEST(Struct, EchoNamedStructNoRetval) {
  ForAllServers(servers, [](async::Loop& loop, fidl::test::compatibility::EchoPtr& proxy,
                            const std::string& server_url, const std::string proxy_url) {
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct_no_ret)"] = false;
    fidl::test::imported::SimpleStruct sent;
    sent.f1 = false;
    sent.f2 = 4;

    fidl::test::imported::SimpleStruct sent_clone;
    sent.Clone(&sent_clone);
    fidl::test::imported::SimpleStruct resp_clone;
    bool event_received = false;
    proxy.events().OnEchoNamedEvent = [&loop, &resp_clone,
                                       &event_received](fidl::test::imported::SimpleStruct resp) {
      resp.Clone(&resp_clone);
      event_received = true;
      loop.Quit();
    };
    proxy->EchoNamedStructNoRetVal(std::move(sent), server_url);
    loop.Run();
    ASSERT_TRUE(event_received);
    EXPECT_EQ(sent_clone.f1, resp_clone.f1);
    EXPECT_EQ(sent_clone.f2, resp_clone.f2);
    summary[ExtractShortName(proxy_url) + " <-> " + ExtractShortName(server_url) +
            " (struct_no_ret)"] = true;
  });
}

}  // namespace

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  GetServersUnderTest(argc, argv, &servers);

  int r = RUN_ALL_TESTS();
  PrintSummary(summary);
  return r;
}
