// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_TESTING_VIEWS_COORDINATE_TEST_VIEW_H_
#define GARNET_TESTING_VIEWS_COORDINATE_TEST_VIEW_H_

#include "garnet/testing/views/background_view.h"

namespace scenic {

// Draws the following coordinate test pattern in a view:
//
// ___________________________________
// |                |                |
// |     BLACK      |        RED     |
// |           _____|_____           |
// |___________|  GREEN  |___________|
// |           |_________|           |
// |                |                |
// |      BLUE      |     MAGENTA    |
// |________________|________________|
//
class CoordinateTestView : public BackgroundView {
 public:
  CoordinateTestView(ViewContext context, const std::string& debug_name = "RotatedSquareView");

 private:
  // |BackgroundView|
  void Draw(float cx, float cy, float sx, float sy) override;
};

}  // namespace scenic
#endif  // GARNET_TESTING_VIEWS_COORDINATE_TEST_VIEW_H_
