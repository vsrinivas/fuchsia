// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/inspect_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <lib/fostr/fidl/fuchsia/diagnostics/formatting.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<AttachmentValue> CollectInspectData(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services,
                                                 zx::duration timeout, Cobalt* cobalt) {
  std::unique_ptr<Inspect> inspect = std::make_unique<Inspect>(dispatcher, services, cobalt);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto inspect_data = inspect->Collect(timeout);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(inspect_data),
                                         /*args=*/std::move(inspect));
}

Inspect::Inspect(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                 Cobalt* cobalt)
    : services_(services), cobalt_(cobalt), bridge_(dispatcher, "Inspect data collection") {}

fit::promise<AttachmentValue> Inspect::Collect(zx::duration timeout) {
  FXL_CHECK(!has_called_collect_) << "Collect() is not intended to be called twice";
  has_called_collect_ = true;

  // We set up the connection and all the error handlers.
  SetUp();

  // We start the Inspect data collection.
  StreamInspectSnapshot();

  // We wait on one way to finish the flow, joining whichever data has been collected.
  return bridge_
      .WaitForDone(timeout,
                   /*if_timeout=*/[this] { cobalt_->LogOccurrence(TimedOutData::kInspect); })
      .then([this](fit::result<>& result) -> fit::result<AttachmentValue> {
        if (!result.is_ok()) {
          FX_LOGS(WARNING)
              << "Inspect data collection was interrupted - Inspect data may be partial or missing";
        }

        if (inspect_data_.empty()) {
          FX_LOGS(WARNING) << "Empty Inspect data";
          return fit::error();
        }

        std::string joined_data = "[\n";
        joined_data += fxl::JoinStrings(inspect_data_, ",\n");
        joined_data += "\n]";

        return fit::ok(joined_data);
      });
}

void Inspect::SetUp() {
  archive_ = services_->Connect<fuchsia::diagnostics::Archive>();
  archive_.set_error_handler([this](zx_status_t status) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.diagnostics.Archive";
    bridge_.CompleteError();
  });

  snapshot_iterator_.set_error_handler([this](zx_status_t status) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.diagnostics.BatchIterator";
    bridge_.CompleteError();
  });
}

void Inspect::StreamInspectSnapshot() {
  fuchsia::diagnostics::StreamParameters stream_parameters;
  stream_parameters.set_data_type(fuchsia::diagnostics::DataType::INSPECT);
  stream_parameters.set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT);
  stream_parameters.set_format(fuchsia::diagnostics::Format::JSON);
  archive_->StreamDiagnostics(snapshot_iterator_.NewRequest(), std::move(stream_parameters));
  AppendNextInspectBatch();
}

void Inspect::AppendNextInspectBatch() {
  snapshot_iterator_->GetNext([this](auto result) {
    if (bridge_.IsAlreadyDone()) {
      return;
    }

    if (result.is_err()) {
      FX_LOGS(ERROR) << "Failed to retrieve next Inspect batch: " << result.err();
      bridge_.CompleteError();
      return;
    }

    std::vector<fuchsia::diagnostics::FormattedContent> batch = std::move(result.response().batch);
    if (batch.empty()) {  // We have gotten all the Inspect data.
      bridge_.CompleteOk();
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

}  // namespace feedback
