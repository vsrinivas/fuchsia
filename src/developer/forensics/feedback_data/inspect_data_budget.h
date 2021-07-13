// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_INSPECT_DATA_BUDGET_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_INSPECT_DATA_BUDGET_H_

#include <lib/inspect/cpp/vmo/types.h>

#include <cstddef>
#include <list>
#include <optional>

#include "src/developer/forensics/utils/archive.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/inspect_node_manager.h"

namespace forensics {
namespace feedback_data {

// Predicts the uncompressed inspect data size budget in order to keep the snapshot's size below
// 2.0 MB. If the file 'limit_data_flag_path' does not exist, prediction is disabled.
class InspectDataBudget {
 public:
  InspectDataBudget(bool limit_data, InspectNodeManager* node, cobalt::Logger* cobalt);

  void UpdateBudget(const std::map<std::string, ArchiveFileStats>& file_size_stats);

  // Returns the predicted uncompressed data size for inspect.
  std::optional<size_t> SizeInBytes() const { return data_budget_; }

 private:
  std::optional<size_t> data_budget_;
  bool limit_data_flag_;

  // For Inspect.
  InspectNodeManager* inspect_node_;
  inspect::StringProperty inspect_budget_enabled_;
  inspect::UintProperty inspect_min_budget_;
  inspect::UintProperty inspect_max_budget_;
  inspect::UintProperty inspect_target_size_;
  std::list<inspect::UintArray> inspect_last_ten_readings_;
  size_t next_reading_idx_ = 0;

  cobalt::Logger* cobalt_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_INSPECT_DATA_BUDGET_H_
