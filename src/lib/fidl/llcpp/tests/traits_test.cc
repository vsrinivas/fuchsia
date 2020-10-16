// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/llcpp/traits.h"

#include <fidl/llcpp/types/test/llcpp/fidl.h>

#include "gtest/gtest.h"

namespace test = llcpp::fidl::llcpp::types::test;

// There's no actual code in here, but if it compiles, success.
TEST(Traits, NotConst) {
  static_assert(!fidl::IsFidlObject<uint32_t>::value);
  static_assert(fidl::IsFidlObject<test::CopyableStruct>::value);
  static_assert(fidl::IsFidlObject<test::MoveOnlyStruct>::value);
  static_assert(fidl::IsFidlObject<test::SampleTable>::value);
  static_assert(fidl::IsFidlObject<test::TestUnion>::value);

  static_assert(!fidl::IsTable<uint32_t>::value);
  static_assert(!fidl::IsTable<test::CopyableStruct>::value);
  static_assert(!fidl::IsTable<test::MoveOnlyStruct>::value);
  static_assert(fidl::IsTable<test::SampleTable>::value);
  static_assert(!fidl::IsTable<test::TestUnion>::value);

  static_assert(!fidl::IsUnion<uint32_t>::value);
  static_assert(!fidl::IsUnion<test::CopyableStruct>::value);
  static_assert(!fidl::IsUnion<test::MoveOnlyStruct>::value);
  static_assert(!fidl::IsUnion<test::SampleTable>::value);
  static_assert(fidl::IsUnion<test::TestUnion>::value);

  static_assert(!fidl::IsStruct<uint32_t>::value);
  static_assert(fidl::IsStruct<test::CopyableStruct>::value);
  static_assert(fidl::IsStruct<test::MoveOnlyStruct>::value);
  static_assert(!fidl::IsStruct<test::SampleTable>::value);
  static_assert(!fidl::IsStruct<test::TestUnion>::value);

  static_assert(!fidl::IsTableBuilder<uint32_t>::value);
  static_assert(!fidl::IsTableBuilder<test::CopyableStruct>::value);
  static_assert(!fidl::IsTableBuilder<test::MoveOnlyStruct>::value);
  static_assert(!fidl::IsTableBuilder<test::SampleTable>::value);
  static_assert(fidl::IsTableBuilder<test::SampleTable::Builder>::value);
  static_assert(!fidl::IsTableBuilder<test::TestUnion>::value);

  static_assert(!fidl::IsStringView<uint32_t>::value);
  static_assert(fidl::IsStringView<fidl::StringView>::value);

  static_assert(!fidl::IsVectorView<uint32_t>::value);
  static_assert(fidl::IsVectorView<fidl::VectorView<uint32_t>>::value);
}

TEST(Traits, Const) {
  static_assert(!fidl::IsFidlObject<const uint32_t>::value);
  static_assert(fidl::IsFidlObject<const test::CopyableStruct>::value);
  static_assert(fidl::IsFidlObject<const test::MoveOnlyStruct>::value);
  static_assert(fidl::IsFidlObject<const test::SampleTable>::value);

  static_assert(!fidl::IsTable<const uint32_t>::value);
  static_assert(!fidl::IsTable<const test::CopyableStruct>::value);
  static_assert(!fidl::IsTable<const test::MoveOnlyStruct>::value);
  static_assert(fidl::IsTable<const test::SampleTable>::value);

  static_assert(!fidl::IsStruct<const uint32_t>::value);
  static_assert(fidl::IsStruct<const test::CopyableStruct>::value);
  static_assert(fidl::IsStruct<const test::MoveOnlyStruct>::value);
  static_assert(!fidl::IsStruct<const test::SampleTable>::value);

  static_assert(!fidl::IsTableBuilder<const uint32_t>::value);
  static_assert(!fidl::IsTableBuilder<const test::CopyableStruct>::value);
  static_assert(!fidl::IsTableBuilder<const test::MoveOnlyStruct>::value);
  static_assert(!fidl::IsTableBuilder<const test::SampleTable>::value);
  static_assert(fidl::IsTableBuilder<const test::SampleTable::Builder>::value);

  static_assert(!fidl::IsStringView<const uint32_t>::value);
  static_assert(fidl::IsStringView<const fidl::StringView>::value);

  static_assert(!fidl::IsVectorView<const uint32_t>::value);
  static_assert(fidl::IsVectorView<const fidl::VectorView<uint32_t>>::value);
}

TEST(Traits, IsFidlType) {
  struct NotAFidlType {};
  static_assert(fidl::IsFidlType<uint32_t>::value);
  static_assert(fidl::IsFidlType<test::CopyableStruct>::value);
  static_assert(fidl::IsFidlType<test::MoveOnlyStruct>::value);
  static_assert(fidl::IsFidlType<test::EmptyStruct>::value);
  static_assert(fidl::IsFidlType<test::SampleTable>::value);
  static_assert(fidl::IsFidlType<test::StrictBits>::value);
  static_assert(fidl::IsFidlType<test::FlexibleBits>::value);
  static_assert(fidl::IsFidlType<test::StrictEnum>::value);
  static_assert(fidl::IsFidlType<test::FlexibleEnum>::value);
  static_assert(!fidl::IsFidlType<NotAFidlType>::value);
}
