// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/global_matrix_data.h"

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/allocation/id.h"

namespace flatland {
namespace test {

TEST(GlobalMatrixDataTest, ClearsEmptyRectangles) {
  // Add 2 empty, 1 non empty and 1 empty rectangles.
  const escher::Rectangle2D empty_rectangle(
      glm::vec2(0, 0), glm::vec2(0, 0),
      {glm::vec2(0, 0), glm::vec2(0, 0), glm::vec2(0, 0), glm::vec2(0, 0)});
  const escher::Rectangle2D non_empty_rectangle(
      glm::vec2(0, 0), glm::vec2(1, 1),
      {glm::vec2(0, 0), glm::vec2(0, 0), glm::vec2(0, 0), glm::vec2(0, 0)});
  std::vector<escher::Rectangle2D> rectangles(2, empty_rectangle);
  rectangles.push_back(non_empty_rectangle);
  rectangles.push_back(empty_rectangle);

  // Add 2 empty, 1 non empty and 1 empty images.
  allocation::ImageMetadata empty_image;
  const allocation::GlobalImageId kImageId = 5;
  allocation::ImageMetadata non_empty_image = {.identifier = kImageId};
  std::vector<allocation::ImageMetadata> images(2, empty_image);
  images.push_back(non_empty_image);
  images.push_back(empty_image);

  EXPECT_EQ(rectangles.size(), images.size());
  ClearEmptyRectangles(&rectangles, &images);
  EXPECT_EQ(rectangles.size(), images.size());
  EXPECT_EQ(1u, rectangles.size());
  EXPECT_EQ(images[0].identifier, kImageId);
}

}  // namespace test
}  // namespace flatland
