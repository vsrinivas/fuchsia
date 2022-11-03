// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl-async-2/fidl_struct.h"

#include <fidl/fidl.types.test/cpp/fidl.h>
#include <fidl/types/test/c/fidl.h>
#include <inttypes.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/zx/event.h>

#include <type_traits>

#include <zxtest/zxtest.h>

namespace {

template <typename C, typename Enable = void>
struct HasCopyAsLlcpp : std::false_type {};
template <typename C>
struct HasCopyAsLlcpp<
    C, typename std::enable_if<std::is_same<decltype(std::declval<C>().CopyAsLlcpp()),
                                            typename C::llcpp_type>::value>::type>
    : std::true_type {};

}  // namespace

using namespace ::fidl_types_test;

TEST(FidlStruct, CopyableStruct) {
  constexpr int32_t kNewFieldValue = 12;
  using Struct = FidlStruct<fidl_types_test_CopyableStruct, wire::CopyableStruct>;
  Struct s(Struct::Default);
  const Struct& sc = s;
  EXPECT_EQ(0, s->x);
  s->x = kNewFieldValue;
  EXPECT_EQ(kNewFieldValue, s->x);
  static_assert(HasCopyAsLlcpp<Struct>::value);
  EXPECT_EQ(kNewFieldValue, s.CopyAsLlcpp().x);
  EXPECT_EQ(kNewFieldValue, s.BorrowAsLlcpp().x);
  EXPECT_EQ(kNewFieldValue, sc.BorrowAsLlcpp().x);
  fidl_types_test_CopyableStruct* ps = s.get();
  const fidl_types_test_CopyableStruct* cps = s.get();
  EXPECT_EQ(kNewFieldValue, ps->x);
  EXPECT_EQ(kNewFieldValue, cps->x);
  EXPECT_EQ(kNewFieldValue, Struct::BorrowAsLlcpp(ps)->x);
  EXPECT_EQ(kNewFieldValue, Struct::BorrowAsLlcpp(cps)->x);
  EXPECT_EQ(kNewFieldValue, s.TakeAsLlcpp().x);
  // TakeAsLlcpp() moved it out.
  EXPECT_FALSE(!!s);
}

TEST(FidlStruct, StructWithArrays) {
  constexpr int32_t kNewFieldValue = 12;
  using Struct = FidlStruct<fidl_types_test_StructWithArrays, wire::StructWithArrays>;
  Struct s(Struct::Default);
  const Struct& sc = s;
  EXPECT_EQ(0, s->x);
  s->x = kNewFieldValue;
  EXPECT_EQ(kNewFieldValue, s->x);
  static_assert(HasCopyAsLlcpp<Struct>::value);
  EXPECT_EQ(kNewFieldValue, s.CopyAsLlcpp().x);
  EXPECT_EQ(kNewFieldValue, s.BorrowAsLlcpp().x);
  EXPECT_EQ(kNewFieldValue, sc.BorrowAsLlcpp().x);
  fidl_types_test_StructWithArrays* ps = s.get();
  const fidl_types_test_StructWithArrays* cps = s.get();
  EXPECT_EQ(kNewFieldValue, ps->x);
  EXPECT_EQ(kNewFieldValue, cps->x);
  EXPECT_EQ(kNewFieldValue, Struct::BorrowAsLlcpp(ps)->x);
  EXPECT_EQ(kNewFieldValue, Struct::BorrowAsLlcpp(cps)->x);
  EXPECT_EQ(kNewFieldValue, s.TakeAsLlcpp().x);
  // TakeAsLlcpp() moved it out.
  EXPECT_FALSE(!!s);
}

TEST(FidlStruct, StructWithSubStruct) {
  constexpr int32_t kNewFieldValue = 12;
  using Struct = FidlStruct<fidl_types_test_StructWithSubStruct, wire::StructWithSubStruct>;
  Struct s(Struct::Default);
  const Struct& sc = s;
  EXPECT_EQ(0, s->s.x);
  s->s.x = kNewFieldValue;
  EXPECT_EQ(kNewFieldValue, s->s.x);
  static_assert(HasCopyAsLlcpp<Struct>::value);
  EXPECT_EQ(kNewFieldValue, s.CopyAsLlcpp().s.x);
  EXPECT_EQ(kNewFieldValue, s.BorrowAsLlcpp().s.x);
  EXPECT_EQ(kNewFieldValue, sc.BorrowAsLlcpp().s.x);
  fidl_types_test_StructWithSubStruct* ps = s.get();
  const fidl_types_test_StructWithSubStruct* cps = s.get();
  EXPECT_EQ(kNewFieldValue, ps->s.x);
  EXPECT_EQ(kNewFieldValue, cps->s.x);
  EXPECT_EQ(kNewFieldValue, Struct::BorrowAsLlcpp(ps)->s.x);
  EXPECT_EQ(kNewFieldValue, Struct::BorrowAsLlcpp(cps)->s.x);
  EXPECT_EQ(kNewFieldValue, s.TakeAsLlcpp().s.x);
  // TakeAsLlcpp() moved it out.
  EXPECT_FALSE(!!s);
}

TEST(FidlStruct, EmptyStruct) {
  using Struct = FidlStruct<fidl_types_test_EmptyStruct, wire::EmptyStruct>;
  Struct s(Struct::Default);
  const Struct& sc = s;
  static_assert(HasCopyAsLlcpp<Struct>::value);
  (void)s.CopyAsLlcpp();
  (void)s.BorrowAsLlcpp();
  (void)sc.BorrowAsLlcpp();
  fidl_types_test_EmptyStruct* ps = s.get();
  const fidl_types_test_EmptyStruct* cps = s.get();
  (void)Struct::BorrowAsLlcpp(ps);
  (void)Struct::BorrowAsLlcpp(cps);
  (void)s.TakeAsLlcpp();
  // TakeAsLlcpp() moved it out.
  EXPECT_FALSE(!!s);
}

// If this builds, it passes.
TEST(FidlStruct, Aliases) {
  using Struct = FidlStruct<fidl_types_test_EmptyStruct, wire::EmptyStruct>;
  static_assert(std::is_same<Struct::c_type, fidl_types_test_EmptyStruct>::value);
  static_assert(std::is_same<Struct::llcpp_type, wire::EmptyStruct>::value);
}
