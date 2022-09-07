// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/collision/cpp/fidl.h>
#include <zxtest/zxtest.h>

namespace fidl {
namespace {

// Confirms that a protocol can have a method that returns a protocol whose
// type name is the same as the method's. Prior to a change which added the
// "class" modifier to the generated proxy's method signature, the identifier
// was ambiguous and the generated header did not compile.
TEST(Collision, ReturnTypeCompiles) {
  zx::channel ch;
  fidl::InterfaceHandle<class test::collision::NameCollision> collision;
  test::collision::ReturnsCollision_SyncProxy proxy(std::move(ch));
  proxy.NameCollision(&collision);
}

// Confirms that a protocol can have a method that takes in a nullable union
// whose type name is the same as the method's.
TEST(Collision, NullableUnionCompiles) {
  zx::channel ch;
  fidl::InterfaceHandle<class test::collision::NameCollision> collision;
  test::collision::ReturnsCollision_SyncProxy proxy(std::move(ch));
  auto nullable_union = std::make_unique<test::collision::NullableUnionCollision>();
  nullable_union->set_foo(42);
  proxy.NullableUnionCollision(std::move(nullable_union));
}

}  // namespace
}  // namespace fidl
