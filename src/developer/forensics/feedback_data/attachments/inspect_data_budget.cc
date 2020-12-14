// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/inspect_data_budget.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>

namespace forensics::feedback_data {
namespace {
const size_t kDefaultInspectDataBudgetInBytes = 20 * 1024 * 1024;
}

InspectDataBudget::InspectDataBudget(const char* limit_data_flag_path) {
  data_budget_ = std::filesystem::exists(limit_data_flag_path)
                     ? std::make_optional<size_t>(kDefaultInspectDataBudgetInBytes)
                     : std::nullopt;
}

}  // namespace forensics::feedback_data
