// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/geometry/bounding_box.h"

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/geometry/types.h"

namespace {

using namespace escher;

TEST(BoundingBox, Transform) {
  mat4 matrix(1);

  // Test identity transform.
  BoundingBox box({1, 2, 3}, vec3{2, 4, 6});
  EXPECT_EQ(box, matrix * box);
  EXPECT_EQ(BoundingBox(), matrix * BoundingBox());

  // Test reflection around origin.
  matrix[0][0] = -1;
  matrix[1][1] = -2;
  matrix[2][2] = -1;
  EXPECT_EQ(BoundingBox({-2, -8, -6}, {-1, -4, -3}), matrix * box);
  EXPECT_EQ(BoundingBox(), matrix * BoundingBox());

  matrix = mat4(1);
  matrix[3][0] = 10;
  matrix[3][1] = 11;
  matrix[3][2] = 12;
  EXPECT_EQ(BoundingBox({11, 13, 15}, {12, 15, 18}), matrix * box);
}

TEST(BoundingBox, Contains) {
  EXPECT_FALSE(BoundingBox().Contains(BoundingBox({0.1, 0.1, 0.1}, {0.2, 0.2, 0.2})));

  EXPECT_FALSE(BoundingBox().Contains(BoundingBox()));

  EXPECT_FALSE(BoundingBox(vec3(-10, -10, -10), vec3(10, 10, 10)).Contains(BoundingBox()));

  BoundingBox unit({0, 0, 0}, {1, 1, 1});
  EXPECT_TRUE(unit.Contains(unit));
  EXPECT_TRUE(unit.Contains(BoundingBox({0, 0, 0}, {1, 1, 0.9})));
  EXPECT_TRUE(unit.Contains(BoundingBox({0, 0, 0}, {1, 0.9, 1})));
  EXPECT_TRUE(unit.Contains(BoundingBox({0, 0, 0}, {0.9, 1, 1})));
  EXPECT_TRUE(unit.Contains(BoundingBox({0.1, 0, 0}, {1, 1, 1})));
  EXPECT_TRUE(unit.Contains(BoundingBox({0, 0.1, 0}, {1, 1, 1})));
  EXPECT_TRUE(unit.Contains(BoundingBox({0, 0, 0.1}, {1, 1, 1})));

  BoundingBox out_there({1000, 1000, 1000}, {3000, 3000, 3000});
  EXPECT_TRUE(out_there.Contains(BoundingBox({1500, 1500, 1500}, {2500, 2500, 2500})));
  EXPECT_FALSE(out_there.Contains(BoundingBox({1500, 1500, 1500}, {2500, 2500, 3500})));
}

TEST(BoundingBox, IntersectEmpty) {
  EXPECT_EQ(BoundingBox(), BoundingBox().Intersect(BoundingBox()));
  BoundingBox b({0.1, 0.1, 0.1}, {0.3, 0.3, 0.3});
  EXPECT_EQ(BoundingBox(), BoundingBox().Intersect(b));
  EXPECT_NE(BoundingBox(), b);
  EXPECT_EQ(BoundingBox(), b.Intersect(BoundingBox()));
  EXPECT_EQ(BoundingBox(), b);

  // No intersection.
  b = BoundingBox({0.1, 0.1, 0.1}, {0.3, 0.3, 0.3})
          .Intersect(BoundingBox({0.35, 0.3, 0.3}, {0.4, 0.4, 0.4}));
  EXPECT_EQ(BoundingBox(), b);
  // They touch at one point, but a 0-D intersection is considered empty.
  b = BoundingBox({0.1, 0.1, 0.1}, {0.3, 0.3, 0.3})
          .Intersect(BoundingBox({0.3, 0.3, 0.3}, {0.4, 0.4, 0.4}));
  EXPECT_EQ(BoundingBox(), b);
  // They touch along an edge point, but a 1-D intersection is considered empty.
  b = BoundingBox({0.1, 0.1, 0.1}, {0.3, 0.3, 0.3})
          .Intersect(BoundingBox({0.29, 0.3, 0.3}, {0.4, 0.4, 0.4}));
  EXPECT_EQ(BoundingBox(), b);
  b = BoundingBox({0.1, 0.1, 0.1}, {0.3, 0.3, 0.3})
          .Intersect(BoundingBox({0.3, 0.29, 0.3}, {0.4, 0.4, 0.4}));
  EXPECT_EQ(BoundingBox(), b);
  b = BoundingBox({0.1, 0.1, 0.1}, {0.3, 0.3, 0.3})
          .Intersect(BoundingBox({0.3, 0.3, 0.29}, {0.4, 0.4, 0.4}));
  EXPECT_EQ(BoundingBox(), b);
  // A 2D intersection is not considered empty.
  b = BoundingBox({0.1, 0.1, 0.1}, {0.3, 0.3, 0.3})
          .Intersect(BoundingBox({0.29, 0.29, 0.3}, {0.4, 0.4, 0.4}));
  EXPECT_NE(BoundingBox(), b);
}

TEST(BoundingBox, Intersect) {
  BoundingBox box1({100, 100, 100}, {300, 300, 300});
  BoundingBox box2({200, 200, 200}, {400, 400, 400});

  {
    BoundingBox b2 = box2;
    EXPECT_EQ(BoundingBox({200, 200, 200}, {300, 300, 300}), b2.Intersect(box1));
    BoundingBox b1 = box1;
    EXPECT_EQ(BoundingBox({200, 200, 200}, {300, 300, 300}), b1.Intersect(box2));
  }
}

}  // namespace
