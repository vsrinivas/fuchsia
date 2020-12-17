// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"

#include <lib/fit/result.h>
#include <lib/fostr/fidl/fuchsia/diagnostics/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <vector>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/developer/forensics/utils/fit/timeout.h"

namespace forensics {
namespace feedback_data {

ArchiveAccessor::ArchiveAccessor(async_dispatcher_t* dispatcher,
                                 std::shared_ptr<sys::ServiceDirectory> services,
                                 fuchsia::diagnostics::DataType data_type,
                                 fuchsia::diagnostics::StreamMode stream_mode,
                                 std::optional<size_t> data_budget)
    : archive_(dispatcher, services, kArchiveAccessorName) {
  // Setup stream parameters
  stream_parameters_.set_data_type(data_type);
  stream_parameters_.set_stream_mode(stream_mode);
  stream_parameters_.set_format(fuchsia::diagnostics::Format::JSON);
  stream_parameters_.set_client_selector_configuration(
      fuchsia::diagnostics::ClientSelectorConfiguration::WithSelectAll(true));

  if (data_budget) {
    fuchsia::diagnostics::PerformanceConfiguration performance;
    performance.set_max_aggregate_content_size_bytes(data_budget.value());
    stream_parameters_.set_performance_configuration(std::move(performance));
  }

  // We set up the connection and all the error handlers.
  snapshot_iterator_.set_error_handler([this](zx_status_t status) {
    if (archive_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.diagnostics.BatchIterator";
    archive_.CompleteError(Error::kConnectionError);
  });
}

void ArchiveAccessor::Collect(
    std::function<void(fuchsia::diagnostics::FormattedContent)> write_formatted_content) {
  // We start the Diagnostics data collection.
  archive_->StreamDiagnostics(std::move(stream_parameters_), snapshot_iterator_.NewRequest());
  AppendNextBatch(std::move(write_formatted_content));
}

::fit::promise<void, Error> ArchiveAccessor::WaitForDone(fit::Timeout timeout) {
  return archive_.WaitForDone(std::move(timeout));
}

void ArchiveAccessor::AppendNextBatch(
    std::function<void(fuchsia::diagnostics::FormattedContent)> write_formatted_content) {
  snapshot_iterator_->GetNext(
      [this, write_formatted_content = std::move(write_formatted_content)](auto result) {
        if (archive_.IsAlreadyDone()) {
          return;
        }

        if (result.is_err()) {
          FX_LOGS(WARNING) << "Failed to retrieve next Diagnostics batch: " << result.err();
          // TODO(fxbug.dev/51658): don't complete the flow on an error. The API says we should
          // continue making calls instead.
          archive_.CompleteError(Error::kBadValue);
          return;
        }

        std::vector<fuchsia::diagnostics::FormattedContent>& batch = result.response().batch;
        if (batch.empty()) {  // We have gotten all the Diagnostics data.
          archive_.CompleteOk();
          return;
        }

        for (auto& chunk : batch) {
          write_formatted_content(std::move(chunk));
        }

        AppendNextBatch(std::move(write_formatted_content));
      });
}

}  // namespace feedback_data
}  // namespace forensics
