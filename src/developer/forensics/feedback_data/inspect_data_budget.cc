// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/inspect_data_budget.h"

#include <lib/syslog/cpp/macros.h>
#include <stdint.h>

#include <algorithm>
#include <filesystem>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics::feedback_data {
namespace {
// We target a 2MB final ZIP file. We give a budget between 4MB and 20MB for Inspect data, starting
// at 20MB.
const size_t kTargetZipSizeInBytes = 2 * 1024 * 1024;
const size_t kMinInspectDataBudgetInBytes = 4 * 1024 * 1024;
const size_t kMaxInspectDataBudgetInBytes = 20 * 1024 * 1024;
const size_t kStartingInspectDataBudgetInBytes = kMaxInspectDataBudgetInBytes;

}  // namespace

InspectDataBudget::InspectDataBudget(const bool limit_data, InspectNodeManager* node,
                                     cobalt::Logger* cobalt)
    : limit_data_flag_(limit_data), inspect_node_(node), cobalt_(cobalt) {
  inspect_budget_enabled_ =
      inspect_node_->Get("/inspect_budget")
          .CreateString("is_budget_enabled", limit_data_flag_ ? "true" : "false");

  if (!limit_data_flag_) {
    data_budget_ = std::nullopt;
    return;
  }

  data_budget_ = kStartingInspectDataBudgetInBytes;

  inspect_min_budget_ = inspect_node_->Get("/inspect_budget")
                            .CreateUint("min_input_budget_bytes", kMinInspectDataBudgetInBytes);
  inspect_max_budget_ = inspect_node_->Get("/inspect_budget")
                            .CreateUint("max_input_budget_bytes", kMaxInspectDataBudgetInBytes);
  inspect_target_size_ = inspect_node_->Get("/inspect_budget")
                             .CreateUint("target_snapshot_size_bytes", kTargetZipSizeInBytes);
}

void InspectDataBudget::UpdateBudget(
    const std::map<std::string, ArchiveFileStats>& file_size_stats) {
  std::string inspect_filename(kAttachmentInspect);

  // No-op if data limiting is disabled or the Inspect file doesn't exist in the latest archive.
  if (!limit_data_flag_ || file_size_stats.count(inspect_filename) == 0) {
    return;
  }

  size_t previous_zip_size = 0;
  for (const auto& [name, size] : file_size_stats) {
    previous_zip_size += size.compressed_bytes;
  }

  // Online algorithm; there is no guarantee the same input will give us the same output. For
  // simplicity we use only the last budget and its size output to calculate the new budget.
  //
  // Note: converges faster when the compressed portion of the inspect file is larger.
  // Note: converges when the relationship between budget and snapshot is close to linearity.
  const double ratio = (double)kTargetZipSizeInBytes / (double)previous_zip_size;
  const size_t new_budget = (size_t)((double)data_budget_.value() * ratio);
  data_budget_ = std::clamp(new_budget, kMinInspectDataBudgetInBytes, kMaxInspectDataBudgetInBytes);

  // Add new budget data to Inspect.
  auto entry =
      inspect_node_->Get("/inspect_budget/last_ten_input_budget_previous_snapshot_size_bytes")
          .CreateUintArray(std::to_string(next_reading_idx_++), 2);
  entry.Set(0, data_budget_.value());
  entry.Set(1, previous_zip_size);
  inspect_last_ten_readings_.push_back(std::move(entry));

  // We only keep the last 10 readings.
  if (inspect_last_ten_readings_.size() > 10) {
    inspect_last_ten_readings_.pop_front();
  }

  cobalt_->LogIntegerEvent(cobalt::kInspectBudgetMetricId, data_budget_.value());
}

}  // namespace forensics::feedback_data
