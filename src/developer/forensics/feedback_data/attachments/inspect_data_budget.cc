// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/inspect_data_budget.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>

#include "src/developer/forensics/feedback_data/constants.h"

namespace forensics::feedback_data {
namespace {
const size_t kTargetZipSizeInBytes = 1024 * 1024;
const size_t kStartingInspectDataBudgetInBytes = 20 * 1024 * 1024;

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

  // Closed-loop control system for inspect size budget.
  //
  // Summary: The controller that dictates the inspect budget size is shown below. The controller
  // increases the inspect budget when the Archive size is too small, maintains the budget when the
  // Archive size has the desired size, and decreases the budget when the Archive size is too big.
  // If the desired compress size value is viable, and the inspect data is the only changing
  // variable, then the controller should make the Archive size approach the desired value on every
  // iteration (every time we make a snapshot).
  //
  // Description: The controller adapts its output Y[n] so that the difference D[n] between the
  // input and the Archive size W[n] approach 0, i.e. the archive size W[n] approaches the desired
  // input size I[n] = 1.0 Mb.
  //
  // Constraints:
  //   * Trimmed inspect data compression is unknown
  //   * Inspect data is finite
  //   * Inspect size cannot be negative
  //
  // Controller:
  // D[n] = I[n] - W[n-1]
  // Y[n] = V[n-1] + D[n] * kMinZipRatio
  //
  // Diagram:
  //                              Other files -----------------------|
  //                                                                 |
  //                D[n]                Y[n]             V[n]        v    W[n]
  // I[n] -->(+ -) ------> Controller -------> Inspect ---------> Archive-------|
  //            ^              ^--------- z^-1 -------------|                   |
  //            |                                                               |
  //            |---------------------- z^-1 -----------------------------------|
  //                        W[n-1]
  //
  // Note: Y[n] uses V[n-1] and NOT Y[n-1] to increase stability and speed. This is because
  // inspect data is capped so Y[n] could be unbounded if it took Y[n-1] instead.

  const size_t previous_inspect_size = file_size_stats.at(inspect_filename).raw_bytes;
  size_t previous_zip_size = 0;
  for (const auto& [name, size] : file_size_stats) {
    previous_zip_size += size.compressed_bytes;
  }

  // Closed-loop control for data_budget_ and prevent underflow.
  //
  // Note: To avoid instability because there is no guarantee that trimmed data has the average
  // compression ratio of the inspect file, we use the lowest compression ratio value for inspect's
  // data (which is equal to 4/3 for random data in Base64 format).
  double diff = static_cast<double>(static_cast<int>(kTargetZipSizeInBytes) -
                                    static_cast<int>(previous_zip_size)) *
                (4.0 / 3.0);
  int budget_size = static_cast<int>(previous_inspect_size) + static_cast<int>(diff);
  data_budget_ = budget_size <= 0 ? 0 : budget_size;

  // TODO(fxbug.dev/64072): Add key size variables to inspect for debugging purposes.
}

}  // namespace forensics::feedback_data
