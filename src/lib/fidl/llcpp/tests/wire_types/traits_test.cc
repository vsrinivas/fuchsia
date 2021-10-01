// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/llcpp/traits.h"

#include <fidl/fidl.llcpp.types.test/cpp/wire.h>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#include <lib/zx/vmo.h>
#endif

#include <lib/fidl/llcpp/array.h>

#include "gtest/gtest.h"

namespace test = fidl_llcpp_types_test;

// There's no actual code in here, but if it compiles, success.
TEST(Traits, NotConst) {
  static_assert(!fidl::IsFidlObject<uint32_t>::value);
  static_assert(fidl::IsFidlObject<test::wire::CopyableStruct>::value);
  static_assert(fidl::IsFidlObject<test::wire::MoveOnlyStruct>::value);
  static_assert(fidl::IsFidlObject<test::wire::SampleTable>::value);
  static_assert(fidl::IsFidlObject<test::wire::TestUnion>::value);

  static_assert(!fidl::IsTable<uint32_t>::value);
  static_assert(!fidl::IsTable<test::wire::CopyableStruct>::value);
  static_assert(!fidl::IsTable<test::wire::MoveOnlyStruct>::value);
  static_assert(fidl::IsTable<test::wire::SampleTable>::value);
  static_assert(!fidl::IsTable<test::wire::TestUnion>::value);

  static_assert(!fidl::IsUnion<uint32_t>::value);
  static_assert(!fidl::IsUnion<test::wire::CopyableStruct>::value);
  static_assert(!fidl::IsUnion<test::wire::MoveOnlyStruct>::value);
  static_assert(!fidl::IsUnion<test::wire::SampleTable>::value);
  static_assert(fidl::IsUnion<test::wire::TestUnion>::value);

  static_assert(!fidl::IsStruct<uint32_t>::value);
  static_assert(fidl::IsStruct<test::wire::CopyableStruct>::value);
  static_assert(fidl::IsStruct<test::wire::MoveOnlyStruct>::value);
  static_assert(!fidl::IsStruct<test::wire::SampleTable>::value);
  static_assert(!fidl::IsStruct<test::wire::TestUnion>::value);

  static_assert(!fidl::IsStringView<uint32_t>::value);
  static_assert(fidl::IsStringView<fidl::StringView>::value);

  static_assert(!fidl::IsVectorView<uint32_t>::value);
  static_assert(fidl::IsVectorView<fidl::VectorView<uint32_t>>::value);
}

TEST(Traits, Const) {
  static_assert(!fidl::IsFidlObject<const uint32_t>::value);
  static_assert(fidl::IsFidlObject<const test::wire::CopyableStruct>::value);
  static_assert(fidl::IsFidlObject<const test::wire::MoveOnlyStruct>::value);
  static_assert(fidl::IsFidlObject<const test::wire::SampleTable>::value);

  static_assert(!fidl::IsTable<const uint32_t>::value);
  static_assert(!fidl::IsTable<const test::wire::CopyableStruct>::value);
  static_assert(!fidl::IsTable<const test::wire::MoveOnlyStruct>::value);
  static_assert(fidl::IsTable<const test::wire::SampleTable>::value);

  static_assert(!fidl::IsStruct<const uint32_t>::value);
  static_assert(fidl::IsStruct<const test::wire::CopyableStruct>::value);
  static_assert(fidl::IsStruct<const test::wire::MoveOnlyStruct>::value);
  static_assert(!fidl::IsStruct<const test::wire::SampleTable>::value);

  static_assert(!fidl::IsStringView<const uint32_t>::value);
  static_assert(fidl::IsStringView<const fidl::StringView>::value);

  static_assert(!fidl::IsVectorView<const uint32_t>::value);
  static_assert(fidl::IsVectorView<const fidl::VectorView<uint32_t>>::value);
}

TEST(Traits, IsFidlType) {
  struct NotAFidlType {};
  static_assert(fidl::IsFidlType<uint32_t>::value);
  static_assert(fidl::IsFidlType<test::wire::CopyableStruct>::value);
  static_assert(fidl::IsFidlType<test::wire::MoveOnlyStruct>::value);
  static_assert(fidl::IsFidlType<test::wire::EmptyStruct>::value);
  static_assert(fidl::IsFidlType<test::wire::SampleTable>::value);
  static_assert(fidl::IsFidlType<test::wire::StrictBits>::value);
  static_assert(fidl::IsFidlType<test::wire::FlexibleBits>::value);
  static_assert(fidl::IsFidlType<test::wire::StrictEnum>::value);
  static_assert(fidl::IsFidlType<test::wire::FlexibleEnum>::value);
  static_assert(!fidl::IsFidlType<NotAFidlType>::value);
}

TEST(Traits, ContainsHandle) {
  static_assert(!fidl::ContainsHandle<uint32_t>::value);
  static_assert(!fidl::ContainsHandle<fidl::Array<uint32_t, 3>>::value);
  static_assert(!fidl::ContainsHandle<test::wire::CopyableStruct>::value);
  static_assert(fidl::ContainsHandle<test::wire::MoveOnlyStruct>::value);
  static_assert(!fidl::ContainsHandle<test::wire::TestResourceTable>::value);
  static_assert(fidl::ContainsHandle<test::wire::TestHandleTable>::value);
  static_assert(fidl::ContainsHandle<test::wire::TestXUnion>::value);
  static_assert(fidl::ContainsHandle<test::wire::TestUnion>::value);
  static_assert(!fidl::ContainsHandle<test::wire::TestStrictXUnion>::value);

#ifdef __Fuchsia__
  static_assert(fidl::ContainsHandle<zx::handle>::value);
  static_assert(fidl::ContainsHandle<zx::vmo>::value);
  static_assert(fidl::ContainsHandle<fidl::Array<zx::vmo, 3>>::value);
#endif  // __Fuchsia__
}
