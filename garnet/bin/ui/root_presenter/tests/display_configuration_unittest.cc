// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/displays/display_configuration.h"

#include "garnet/bin/ui/root_presenter/displays/display_model.h"
#include "gtest/gtest.h"

namespace root_presenter {
namespace display_configuration {
namespace {

TEST(DisplayConfiguration, Basic_2160x1440) {
  DisplayModel model;
  InitializeModelForDisplay(2160, 1440, &model);

  EXPECT_EQ(model.display_info().density_in_px_per_mm, 8.5f);
  EXPECT_EQ(model.environment_info().usage,
            fuchsia::ui::policy::DisplayUsage::kClose);
}

TEST(DisplayConfiguration, Basic_2400x1600) {
  DisplayModel model;
  InitializeModelForDisplay(2400, 1600, &model);

  EXPECT_EQ(model.display_info().density_in_px_per_mm, 9.252f);
  EXPECT_EQ(model.environment_info().usage,
            fuchsia::ui::policy::DisplayUsage::kClose);
}

TEST(DisplayConfiguration, Basic_3840x2160) {
  DisplayModel model;
  InitializeModelForDisplay(3840, 2160, &model);

  EXPECT_EQ(model.display_info().density_in_px_per_mm, 7.323761f);
  EXPECT_EQ(model.environment_info().usage,
            fuchsia::ui::policy::DisplayUsage::kNear);
}

TEST(DisplayConfiguration, Basic_1920x1200) {
  DisplayModel model;
  InitializeModelForDisplay(1920, 1200, &model);

  EXPECT_EQ(model.display_info().density_in_px_per_mm, 4.16f);
  EXPECT_EQ(model.environment_info().usage,
            fuchsia::ui::policy::DisplayUsage::kNear);
}
}  // namespace
}  // namespace display_configuration
}  // namespace root_presenter
