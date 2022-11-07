// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/scoped-value-change.h"

#include <gtest/gtest.h>

namespace i915_tgl {

namespace {

TEST(ScopedValueChange, ConstructorChangesVariable) {
  int variable = 100;
  ScopedValueChange value_change(variable, 200);
  EXPECT_EQ(200, variable);
}

TEST(ScopedValueChange, DestructorRestoresVariable) {
  int variable = 100;
  { ScopedValueChange value_change(variable, 200); }
  EXPECT_EQ(100, variable);
}

TEST(ScopedValueChange, MoveConstructorPopulatesDestination) {
  int variable = 100;
  {
    ScopedValueChange move_source_change(variable, 200);
    { ScopedValueChange move_destination_change(std::move(move_source_change)); }
    EXPECT_EQ(100, variable)
        << "`move_destination_change` destruction did not restore the variable";
  }
}

TEST(ScopedValueChange, MoveConstructorDoesNotModifyVariable) {
  int variable = 100;
  {
    ScopedValueChange move_source_change(variable, 200);
    {
      variable = 300;
      ScopedValueChange move_destination_change(std::move(move_source_change));
      EXPECT_EQ(300, variable) << "Move constructor changed the variable";
    }
  }
}

TEST(ScopedValueChange, MoveConstructorInvalidatesMoveSource) {
  int variable = 100;
  {
    ScopedValueChange move_source_change(variable, 200);
    { ScopedValueChange move_destination_change(std::move(move_source_change)); }
    EXPECT_EQ(100, variable)
        << "`move_destination_change` destruction did not restore the variable";
    variable = 300;
  }
  EXPECT_EQ(300, variable) << "`move_source_change` destruction restored the variable";
}

TEST(ScopedValueChange, MoveAssignmentPopulatesDestination) {
  int variable1 = 101;
  int variable2 = 102;
  {
    ScopedValueChange move_source_change(variable1, 201);
    {
      ScopedValueChange move_destination_change(variable2, 202);
      move_destination_change = std::move(move_source_change);
    }
    EXPECT_EQ(101, variable1)
        << "`move_destination_change` destruction did not restore the variable";
  }
}

TEST(ScopedValueChange, MoveAssignmentDoesNotModifyVariables) {
  int variable1 = 101;
  int variable2 = 102;
  {
    ScopedValueChange move_source_change(variable1, 201);
    {
      ScopedValueChange move_destination_change(variable2, 202);

      variable1 = 103;
      variable2 = 203;
      move_destination_change = std::move(move_source_change);
      EXPECT_EQ(103, variable1) << "Move assignment changed the variable of the moved-from Change";
      EXPECT_EQ(203, variable2) << "Move assignment changed the variable of the moved-to Change";
    }
  }
}

TEST(ScopedValueChange, MoveAssignmentDoesNotDropDestinationState) {
  int variable1 = 101;
  int variable2 = 102;
  {
    ScopedValueChange move_source_change(variable1, 201);
    {
      ScopedValueChange move_destination_change(variable2, 202);
      move_destination_change = std::move(move_source_change);
    }
  }
  EXPECT_EQ(102, variable2) << "Move assignment dropped the moved-to state";
}

TEST(ScopedValueChange, MultipleChangesForSameVariable) {
  int variable = 100;
  ScopedValueChange change(variable, 200);

  EXPECT_DEATH({ ScopedValueChange change2(variable, 300); },
               "Multiple ScopedValueChange instances created");
}

TEST(ScopedValueChange, ResetRestoresOriginalValue) {
  int variable = 100;
  ScopedValueChange change(variable, 200);
  change.reset();
  EXPECT_EQ(100, variable) << "reset() did not restore the variable";
}

TEST(ScopedValueChange, ResetInvalidatesChange) {
  int variable = 100;
  {
    ScopedValueChange change(variable, 200);
    change.reset();
    variable = 300;
  }
  EXPECT_EQ(300, variable) << "Reset `change` destruction restored the variable";
}

TEST(ScopedValueChange, MoveAssignmentPopulatesResetDestination) {
  int variable1 = 101;
  int variable2 = 102;
  {
    ScopedValueChange move_source_change(variable1, 201);
    {
      ScopedValueChange move_destination_change(variable2, 202);
      move_destination_change.reset();
      variable2 = 203;
      move_destination_change = std::move(move_source_change);
    }
    EXPECT_EQ(101, variable1)
        << "`move_destination_change` destruction did not restore the variable";
  }
}

TEST(ScopedValueChange, MoveAssignmentToResetDestinationDoesNotModifyVariables) {
  int variable1 = 101;
  int variable2 = 102;
  {
    ScopedValueChange move_source_change(variable1, 201);
    {
      ScopedValueChange move_destination_change(variable2, 202);
      move_destination_change.reset();
      variable1 = 103;
      variable2 = 203;

      move_destination_change = std::move(move_source_change);
      EXPECT_EQ(103, variable1) << "Move assignment changed the variable of the moved-from Change";
      EXPECT_EQ(203, variable2) << "Move assignment changed the variable of the moved-to Change";
    }
  }
}

TEST(ScopedValueChange, MoveAssignmentDoesRestoreResetDestination) {
  int variable1 = 101;
  int variable2 = 102;
  {
    ScopedValueChange move_source_change(variable1, 201);
    {
      ScopedValueChange move_destination_change(variable2, 202);
      move_destination_change.reset();
      variable2 = 203;
      move_destination_change = std::move(move_source_change);
    }
  }
  EXPECT_EQ(203, variable2) << "Move assignment revived reset destination state";
}

}  // namespace

}  // namespace i915_tgl
