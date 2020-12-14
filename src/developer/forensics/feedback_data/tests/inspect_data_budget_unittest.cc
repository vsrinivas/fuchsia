// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/inspect_data_budget.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback_data {
namespace {

TEST(InspectDataBudgetTest, TestUnlimitedBudget) {
  InspectDataBudget inspect_data_budget("non-existent_path");
  ASSERT_FALSE(inspect_data_budget.SizeInBytes());
}

TEST(InspectDataBudgetTest, TestLimitedBudget) {
  files::ScopedTempDir tmp_dir;
  std::string limit_data_flag_path = files::JoinPath(tmp_dir.path(), "limit_inspect_data");
  ASSERT_TRUE(files::WriteFile(limit_data_flag_path, " "));

  InspectDataBudget inspect_data_budget(limit_data_flag_path.c_str());
  ASSERT_TRUE(inspect_data_budget.SizeInBytes());
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
