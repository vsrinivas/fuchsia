// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/scene/object.h"

#include <gtest/gtest.h>

namespace {

using namespace escher;

TEST(Object, BoundingBox) {
  auto circle = Object::NewCircle(vec3{200, 200, 100}, 100, MaterialPtr());
  EXPECT_EQ(BoundingBox({100, 100, 100}, {300, 300, 100}), circle.bounding_box());

  auto rect = Object::NewRect({100, 100, 100}, {150, 250}, MaterialPtr());
  EXPECT_EQ(BoundingBox({100, 100, 100}, {250, 350, 100}), rect.bounding_box());
}

}  // namespace
