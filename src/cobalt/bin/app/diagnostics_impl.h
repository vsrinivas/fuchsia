// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_DIAGNOSTICS_IMPL_H_
#define SRC_COBALT_BIN_APP_DIAGNOSTICS_IMPL_H_

#include <lib/inspect/cpp/inspect.h>

#include "src/lib/fxl/macros.h"
#include "third_party/cobalt/src/public/diagnostics_interface.h"

namespace cobalt {

// Implementation of the Diagnostics Interface for receiving diagnostic information about the
// functioning of the Cobalt Core library.
//
// In Fuchsia, the diagnostic information is published to Inspect.
class DiagnosticsImpl : public DiagnosticsInterface {
 public:
  explicit DiagnosticsImpl(inspect::Node node);
  ~DiagnosticsImpl() override = default;

  void SentObservationResult(const cobalt::util::Status& status) override;

  void ObservationStoreUpdated(const std::map<ReportSpec, uint64_t>& num_obs_per_report,
                               int64_t store_byte_count, int64_t max_store_bytes) override;

 private:
  // Root inspect node to create all child nodes under.
  inspect::Node node_;

  // Inspect data for sending observations to Clearcut.
  inspect::Node send_observations_;
  inspect::IntProperty send_observations_successes_;
  inspect::IntProperty send_observations_errors_;
  inspect::IntProperty last_successful_send_time_;
  inspect::IntProperty last_send_error_time_;
  inspect::IntProperty last_send_error_code_;
  inspect::StringProperty last_send_error_message_;
  inspect::StringProperty last_send_error_details_;

  // Inspect data for stored observations.
  inspect::Node stored_observations_;
  inspect::IntProperty stored_observations_total_;
  inspect::IntProperty stored_observations_byte_count_;
  inspect::IntProperty stored_observations_byte_count_limit_;
  std::map<std::string, inspect::IntProperty> stored_observations_per_report_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DiagnosticsImpl);
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_DIAGNOSTICS_IMPL_H_
