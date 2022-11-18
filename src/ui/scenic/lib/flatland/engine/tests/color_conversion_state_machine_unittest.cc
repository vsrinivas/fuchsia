// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/color_conversion_state_machine.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/flatland/engine/tests/common.h"

namespace flatland {
namespace test {

// If the state machine has never received any color conversion data at all,
// then it should return nullopt data.
TEST(ColorConversionStateMachineTest, NoDataTest) {
  ColorConversionStateMachine cm;

  auto return_data = cm.GetDataToApply();
  EXPECT_EQ(return_data, std::nullopt);
}

// If the data provided to the state machine is the default data, it should return
// nullopt when asked to return the data.
TEST(ColorConversionStateMachineTest, DefaultDataTest) {
  ColorConversionStateMachine cm;
  cm.SetData(ColorConversionData(/*default*/));

  auto return_data = cm.GetDataToApply();
  EXPECT_EQ(return_data, std::nullopt);
}

// If the state machine has received non-default values, but nothing has ever
// been applied, we should get real values back.
TEST(ColorConversionStateMachineTest, ReceivedValidDataTest) {
  // The actual values here don't matter except that they're non-default.
  ColorConversionData data = {
      .coefficients = {1, 2, 3, 4, 5, 6, 7, 8, 9},
      .preoffsets = {1, 2, 3},
      .postoffsets = {9, 8, 7},
  };

  ColorConversionStateMachine cm;
  cm.SetData(data);

  auto return_data = cm.GetDataToApply();
  EXPECT_NE(return_data, std::nullopt);
  EXPECT_EQ(*return_data, data);

  // If we revert back to default data here after having supplied real data,
  // it should nullopt again since we don't have a successfully applied config.
  {
    cm.SetData(ColorConversionData(/*default*/));

    auto return_data = cm.GetDataToApply();
    EXPECT_EQ(return_data, std::nullopt);
  }
}

// Here we test what happens once the color conversion values have been successfully
// applied to the display controller. At this point, since the display controller is
// stateful, no further CC calls should be required until there is a change. So the
// data returned for direct-scanout and GPU afterwards should both be nullopt.
TEST(ColorConversionStateMachineTest, DirectScanoutAppliedSuccessfully) {
  // The actual values here don't matter except that they're non-default.
  ColorConversionData data = {
      .coefficients = {1, 2, 9, 4, 5, 7, 7, 8, 11},
      .preoffsets = {1, 2, 3},
      .postoffsets = {9, 8, 7},
  };

  ColorConversionStateMachine cm;
  cm.SetData(data);
  cm.SetApplyConfigSucceeded();

  auto return_data = cm.GetDataToApply();
  EXPECT_EQ(return_data, std::nullopt);

  bool gpu_reset_required = cm.GpuRequiresDisplayClearing();
  EXPECT_FALSE(gpu_reset_required);
}

// Check that after successfully applying a config and then changing
// the color correction data to make sure we get the correct results.
TEST(ColorConversionStateMachine, DataResetAfterSuccessfulApplication) {
  // The actual values here don't matter except that they're non-default.
  ColorConversionData data = {
      .coefficients = {1, 2, 9, 4, 5, 7, 7, 8, 11},
      .preoffsets = {1, 2, 3},
      .postoffsets = {9, 8, 7},
  };

  ColorConversionStateMachine cm;

  // Set the data and successfully apply it to the display controller.
  cm.SetData(data);
  cm.SetApplyConfigSucceeded();

  // Trying to get the data after successfully applying it should result
  // in a nullopt.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_EQ(return_data, std::nullopt);
  }

  // Set the same data again.
  cm.SetData(data);

  // If for some reason the same data gets set multiple times,
  // it should continue to nullopt.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_EQ(return_data, std::nullopt);
  }

  // Set the data back to the default data.
  cm.SetData(ColorConversionData());

  // In this case it should _NOT_ nullopt,
  // even though it nullopted in the tests above since we have to undo the color
  // correction that was previously applied.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_NE(return_data, std::nullopt);
    EXPECT_EQ(*return_data, ColorConversionData());

    // Since we are doing a reset but the display controller has not successfully
    // applied a new config, we need the GPU path to reset before continuing.
    bool gpu_reset_required = cm.GpuRequiresDisplayClearing();
    EXPECT_TRUE(gpu_reset_required);
  }

  // Now let's successfully apply the config.
  cm.SetApplyConfigSucceeded();

  // The requirement for a reset should go away now, and we have null configs again.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_EQ(return_data, std::nullopt);

    // The GPU path should not require a reset here.
    bool gpu_reset_required = cm.GpuRequiresDisplayClearing();
    EXPECT_FALSE(gpu_reset_required);
  }
}

// Test that if we clear the display from the GPU path, we get the desired result.
TEST(ColorConversionStateMachine, GpuClearDisplayTest) {
  ColorConversionData data = {
      .coefficients = {1, 2, 9, 4, 5, 7, 7, 8, 11},
      .preoffsets = {1, 2, 3},
      .postoffsets = {9, 8, 7},
  };

  // Set the data and apply the config.
  ColorConversionStateMachine cm;
  cm.SetData(data);
  cm.SetApplyConfigSucceeded();

  // Change the data.
  data.preoffsets = {9, 9, 9};
  cm.SetData(data);

  // The GPU should now say it needs to clear the display.
  bool should_clear = cm.GpuRequiresDisplayClearing();
  EXPECT_TRUE(should_clear);

  // Now we clear the display.
  cm.DisplayCleared();

  // We should no longer have to clear.
  should_clear = cm.GpuRequiresDisplayClearing();
  EXPECT_FALSE(should_clear);

  // The data we get back should be the new data.
  auto return_data = cm.GetDataToApply();
  EXPECT_NE(return_data, std::nullopt);
  EXPECT_EQ(*return_data, data);
}

// Test a complicated scenario to make sure the logic is working as expected.
TEST(ColorConversionStateMachine, StressTest) {
  // The actual values here don't matter except that they're non-default.
  ColorConversionData data = {
      .coefficients = {1, 2, 9, 4, 5, 7, 7, 8, 11},
      .preoffsets = {1, 2, 3},
      .postoffsets = {9, 8, 7},
  };

  ColorConversionStateMachine cm;

  // Set some data.
  cm.SetData(data);

  // Data should read back the same. No GPU reset required.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_NE(return_data, std::nullopt);
    EXPECT_EQ(*return_data, data);

    bool gpu_reset_required = cm.GpuRequiresDisplayClearing();
    EXPECT_FALSE(gpu_reset_required);
  }

  // Apply the data successfully to the display controller.
  cm.SetApplyConfigSucceeded();

  // Subsequent data should be null. Still shouldn't require
  // a GPU reset.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_EQ(return_data, std::nullopt);

    bool gpu_reset_required = cm.GpuRequiresDisplayClearing();
    EXPECT_FALSE(gpu_reset_required);
  }

  // Change the data
  data.coefficients = {5, 5, 5, 5, 5, 5, 5, 5, 5};
  cm.SetData(data);

  // The data should be updated. Now we will need a GPU
  // reset since the data differs from the applied data.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_NE(return_data, std::nullopt);
    EXPECT_EQ(*return_data, data);

    bool gpu_reset_required = cm.GpuRequiresDisplayClearing();
    EXPECT_TRUE(gpu_reset_required);
  }

  // Clear the display.
  cm.DisplayCleared();

  // Now we shouldn't have GPU clearing required, but we should have
  // data returning again.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_NE(return_data, std::nullopt);
    EXPECT_EQ(*return_data, data);

    bool gpu_reset_required = cm.GpuRequiresDisplayClearing();
    EXPECT_FALSE(gpu_reset_required);
  }

  // Reset the data back to default/identity values.
  cm.SetData(ColorConversionData());

  // The data should be nullopt.
  {
    auto return_data = cm.GetDataToApply();
    EXPECT_EQ(return_data, std::nullopt);
  }
}

}  // namespace test
}  // namespace flatland
