// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/archive_accessor_ptr.h"

#include <lib/async/cpp/task.h>
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
                                 fuchsia::diagnostics::StreamMode stream_mode)
    : archive_(dispatcher, services, kArchiveAccessorName) {
  // Setup stream parameters
  stream_parameters_.set_data_type(data_type);
  stream_parameters_.set_stream_mode(stream_mode);
  stream_parameters_.set_format(fuchsia::diagnostics::Format::JSON);
  stream_parameters_.set_client_selector_configuration(
      fuchsia::diagnostics::ClientSelectorConfiguration::WithSelectAll(true));

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
    const std::function<void(const fuchsia::diagnostics::FormattedContent&)>&
        write_formatted_content) {
  // We start the Inspect data collection.
  archive_->StreamDiagnostics(std::move(stream_parameters_), snapshot_iterator_.NewRequest());
  AppendNextBatch(write_formatted_content);
}

::fit::promise<void, Error> ArchiveAccessor::WaitForDone(fit::Timeout timeout) {
  return archive_.WaitForDone(std::move(timeout));
}

void ArchiveAccessor::AppendNextBatch(
    const std::function<void(const fuchsia::diagnostics::FormattedContent&)>&
        write_formatted_content) {
  snapshot_iterator_->GetNext([this, write_formatted_content](auto result) {
    if (archive_.IsAlreadyDone()) {
      return;
    }

    if (result.is_err()) {
      FX_LOGS(WARNING) << "Failed to retrieve next Inspect batch: " << result.err();
      // TODO(fxbug.dev/51658): don't complete the flow on an error. The API says we should continue
      // making calls instead.
      archive_.CompleteError(Error::kBadValue);
      return;
    }

    const std::vector<fuchsia::diagnostics::FormattedContent>& batch = result.response().batch;
    if (batch.empty()) {  // We have gotten all the Inspect data.
      archive_.CompleteOk();
      return;
    }

    for (const auto& chunk : batch) {
      if (!chunk.is_json()) {
        FX_LOGS(WARNING) << "Missing JSON Inspect chunk, skipping";
        continue;
      }
      write_formatted_content(chunk);
    }

    AppendNextBatch(write_formatted_content);
  });
}

}  // namespace feedback_data
}  // namespace forensics
