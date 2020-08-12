// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/inspect_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <lib/fostr/fidl/fuchsia/diagnostics/formatting.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/fit/promise.h"
#include "src/developer/forensics/utils/fit/timeout.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics {
namespace feedback_data {

::fit::promise<AttachmentValue> CollectInspectData(async_dispatcher_t* dispatcher,
                                                   std::shared_ptr<sys::ServiceDirectory> services,
                                                   fit::Timeout timeout) {
  std::unique_ptr<Inspect> inspect = std::make_unique<Inspect>(dispatcher, services);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto inspect_data = inspect->Collect(std::move(timeout));
  return fit::ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(inspect_data),
                                              /*args=*/std::move(inspect));
}

Inspect::Inspect(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services)
    : archive_(dispatcher, services, kArchiveAccessorName) {}

::fit::promise<AttachmentValue> Inspect::Collect(fit::Timeout timeout) {
  // We set up the connection and all the error handlers.
  SetUp();

  // We start the Inspect data collection.
  StreamInspectSnapshot();

  // We wait on one way to finish the flow, joining whichever data has been collected.
  return archive_.WaitForDone(std::move(timeout))
      .then([this](::fit::result<void, Error>& result) -> ::fit::result<AttachmentValue> {
        if (inspect_data_.empty()) {
          FX_LOGS(WARNING) << "Empty Inspect data";
          AttachmentValue value = (result.is_ok()) ? AttachmentValue(Error::kMissingValue)
                                                   : AttachmentValue(result.error());
          return ::fit::ok(std::move(value));
        }

        std::string joined_data = "[\n";
        joined_data += fxl::JoinStrings(inspect_data_, ",\n");
        joined_data += "\n]";

        AttachmentValue value = (result.is_ok())
                                    ? AttachmentValue(std::move(joined_data))
                                    : AttachmentValue(std::move(joined_data), result.error());

        return ::fit::ok(std::move(value));
      });
}

void Inspect::SetUp() {
  snapshot_iterator_.set_error_handler([this](zx_status_t status) {
    if (archive_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.diagnostics.BatchIterator";
    archive_.CompleteError(Error::kConnectionError);
  });
}

void Inspect::StreamInspectSnapshot() {
  fuchsia::diagnostics::StreamParameters stream_parameters;
  stream_parameters.set_data_type(fuchsia::diagnostics::DataType::INSPECT);
  stream_parameters.set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT);
  stream_parameters.set_format(fuchsia::diagnostics::Format::JSON);
  stream_parameters.set_client_selector_configuration(
      fuchsia::diagnostics::ClientSelectorConfiguration::WithSelectAll(true));
  archive_->StreamDiagnostics(std::move(stream_parameters), snapshot_iterator_.NewRequest());
  AppendNextInspectBatch();
}

void Inspect::AppendNextInspectBatch() {
  snapshot_iterator_->GetNext([this](auto result) {
    if (archive_.IsAlreadyDone()) {
      return;
    }

    if (result.is_err()) {
      FX_LOGS(ERROR) << "Failed to retrieve next Inspect batch: " << result.err();
      // TODO(fxbug.dev/51658): don't complete the flow on an error. The API says we should continue
      // making calls instead.
      archive_.CompleteError(Error::kBadValue);
      return;
    }

    std::vector<fuchsia::diagnostics::FormattedContent> batch = std::move(result.response().batch);
    if (batch.empty()) {  // We have gotten all the Inspect data.
      archive_.CompleteOk();
      return;
    }

    for (const auto& chunk : batch) {
      if (!chunk.is_json()) {
        FX_LOGS(WARNING) << "Missing JSON Inspect chunk, skipping";
        continue;
      }

      std::string json;
      if (!fsl::StringFromVmo(chunk.json(), &json)) {
        FX_LOGS(WARNING) << "Failed to convert Inspect data chunk to string, skipping";
        continue;
      }
      inspect_data_.push_back(json);
    }
    AppendNextInspectBatch();
  });
}

}  // namespace feedback_data
}  // namespace forensics
