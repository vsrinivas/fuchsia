// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/fidl.h>
#include <lib/stdcompat/type_traits.h>

#include <zxtest/zxtest.h>

TEST(Response, DefaultConstruction) {
  fidl::Response<test_types::Baz::Foo> response;
  EXPECT_EQ(0, response->res().bar());
}

TEST(Response, FromPayload) {
  test_types::FooResponse res{{.bar = 42}};
  test_types::BazFooTopResponse payload{{.res = std::move(res)}};
  fidl::Response<test_types::Baz::Foo> response{std::move(payload)};
  EXPECT_EQ(42, response->res().bar());
}

TEST(Response, Operators) {
  fidl::Response<test_types::Baz::Foo> response;

  auto& star = *response;
  static_assert(
      cpp17::is_same_v<cpp20::remove_cvref_t<decltype(star)>, test_types::BazFooTopResponse>);

  auto* arrow = response.operator->();
  static_assert(
      cpp17::is_same_v<cpp20::remove_cvref_t<decltype(arrow)>, test_types::BazFooTopResponse*>);
}

// TODO(fxbug.dev/60240): Add tests for |fidl::Request| when that is introduced.
