// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/inspect_data_budget.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <filesystem>

#include "src/developer/forensics/feedback_data/constants.h"

namespace forensics::feedback_data {
namespace {
// We target a 2MB final ZIP file. We give a budget between 4MB and 20MB for Inspect data, starting
// at 20MB.
const size_t kTargetZipSizeInBytes = 2 * 1024 * 1024;
const size_t kMinInspectDataBudgetInBytes = 4 * 1024 * 1024;
const size_t kMaxInspectDataBudgetInBytes = 20 * 1024 * 1024;
const size_t kStartingInspectDataBudgetInBytes = kMaxInspectDataBudgetInBytes;

}  // namespace

InspectDataBudget::InspectDataBudget(const char* limit_data_flag_path) {
  limit_data_flag_ = std::filesystem::exists(limit_data_flag_path);
  data_budget_ = limit_data_flag_ ? std::make_optional<size_t>(kStartingInspectDataBudgetInBytes)
                                  : std::nullopt;
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
}

}  // namespace forensics::feedback_data
