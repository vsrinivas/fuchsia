// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/wire.h>
#include <lib/fidl/cpp/wire/decoded_value.h>

#ifdef __Fuchsia__
#include "src/lib/fidl/llcpp/tests/types_test_utils.h"
#endif  // __Fuchsia__

#include <gtest/gtest.h>

namespace {

TEST(DecodedValue, AdoptValue) {
  test_types::wire::CopyableStruct v;
  ::fidl::DecodedValue<test_types::wire::CopyableStruct> decoded(&v);
  EXPECT_EQ(&v, &decoded.value());
  EXPECT_EQ(&v, decoded.pointer());
  EXPECT_EQ(&v.x, &decoded->x);

  const ::fidl::DecodedValue<test_types::wire::CopyableStruct>& const_decoded = decoded;
  EXPECT_EQ(&v, &const_decoded.value());
  EXPECT_EQ(&v, const_decoded.pointer());
  EXPECT_EQ(&v.x, &const_decoded->x);

  decoded.Release();
  EXPECT_EQ(nullptr, decoded.pointer());
}

#ifdef __Fuchsia__

TEST(DecodedValue, AdoptResource) {
  llcpp_types_test_utils::HandleChecker handle_checker;
  zx::event h;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &h));
  handle_checker.AddEvent(h);
  test_types::wire::HandleStruct v{std::move(h)};

  { ::fidl::DecodedValue<test_types::wire::HandleStruct> decoded(&v); }

  handle_checker.CheckEvents();
  (void)v.h.release();
}

TEST(DecodedValue, LeakResource) {
  zx::event h;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &h));
  zx::unowned_event unowned_h{h};
  test_types::wire::HandleStruct v{std::move(h)};

  {
    ::fidl::DecodedValue<test_types::wire::HandleStruct> decoded(&v);
    decoded.Release();
  }

  (void)v.h.release();
  zx_info_handle_count_t info = {};
  ASSERT_EQ(ZX_OK,
            unowned_h->get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(1u, info.handle_count);
  h.reset(unowned_h->get());
}

#endif

}  // namespace
