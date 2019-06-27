// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>

#include "gtest/gtest.h"

namespace fidl {
namespace {

// Confirms that a protocol can have a method that returns a protocol whose
// type name is the same as the method's. Prior to a change which added the
// "class" modifier to the generated proxy's method signature, the identifier
// was ambiguous and the generated header did not compile.
TEST(Collision, Compiles) {
  zx::channel ch;
  fidl::InterfaceHandle<class test::misc::NameCollision> collision;
  test::misc::ReturnsCollision_SyncProxy proxy(std::move(ch));
  proxy.NameCollision(&collision);
}

}  // namespace
}  // namespace fidl
