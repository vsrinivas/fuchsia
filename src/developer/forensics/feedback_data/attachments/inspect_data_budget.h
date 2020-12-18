// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_DATA_BUDGET_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_DATA_BUDGET_H_

#include <cstddef>
#include <optional>

#include "src/developer/forensics/utils/archive.h"

namespace forensics {
namespace feedback_data {

// Predicts the uncompressed inspect data size budget in order to keep the snapshot's size below
// 1.0 MB. If the file 'limit_data_flag_path' does not exist, prediction is disabled.
class InspectDataBudget {
 public:
  explicit InspectDataBudget(const char* limit_data_flag_path);

  void UpdateBudget(const std::map<std::string, ArchiveFileStats>& file_size_stats);

  // Returns the predicted uncompressed data size for inspect.
  std::optional<size_t> SizeInBytes() const { return data_budget_; }

 private:
  std::optional<size_t> data_budget_;
  bool limit_data_flag_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_ATTACHMENTS_INSPECT_DATA_BUDGET_H_
