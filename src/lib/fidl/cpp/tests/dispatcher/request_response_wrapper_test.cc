// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpp/types/test/cpp/fidl_v2.h>
#include <lib/stdcompat/type_traits.h>

#include <zxtest/zxtest.h>

TEST(Response, DefaultConstruction) {
  fidl::Response<fidl_llcpp_types_test::Baz::Foo> response;
  EXPECT_EQ(0, response->res().bar());
}

TEST(Response, FromPayload) {
  fidl_llcpp_types_test::FooResponse res{{.bar = 42}};
  fidl_llcpp_types_test::BazFooTopResponse payload{{.res = std::move(res)}};
  fidl::Response<fidl_llcpp_types_test::Baz::Foo> response{std::move(payload)};
  EXPECT_EQ(42, response->res().bar());
}

TEST(Response, Operators) {
  fidl::Response<fidl_llcpp_types_test::Baz::Foo> response;

  auto& star = *response;
  static_assert(cpp17::is_same_v<cpp20::remove_cvref_t<decltype(star)>,
                                 fidl_llcpp_types_test::BazFooTopResponse>);

  auto* arrow = response.operator->();
  static_assert(cpp17::is_same_v<cpp20::remove_cvref_t<decltype(arrow)>,
                                 fidl_llcpp_types_test::BazFooTopResponse*>);
}

// TODO(fxbug.dev/60240): Add tests for |fidl::Request| when that is introduced.
