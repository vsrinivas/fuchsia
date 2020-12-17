// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_listener.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "dockyard_proxy.h"
#include "harvester.h"
#include "src/developer/system_monitor/lib/dockyard/dockyard.h"
#include "src/lib/fsl/vmo/strings.h"

namespace harvester {

LogListener::LogListener(const std::shared_ptr<sys::ServiceDirectory>& services)
    : services_(services) {}

fit::promise<> LogListener::Listen(
    std::function<void(std::vector<const std::string>)> content_callback) {
  // Manages a completer/consumer for notifiying callers when listener closes.
  fit::bridge<> bridge;

  fuchsia::diagnostics::ArchiveAccessorPtr archive;
  services_->Connect(archive.NewRequest());

  // Configure Diagnostics stream for structured logs.
  stream_parameters_.set_data_type(fuchsia::diagnostics::DataType::LOGS);
  stream_parameters_.set_stream_mode(
      fuchsia::diagnostics::StreamMode::SNAPSHOT_THEN_SUBSCRIBE);
  stream_parameters_.set_format(fuchsia::diagnostics::Format::JSON);

  archive->StreamDiagnostics(std::move(stream_parameters_),
                             iterator_.NewRequest());

  GetLogData(content_callback, std::move(bridge.completer));
  return bridge.consumer.promise_or(fit::error());
}

void LogListener::GetLogData(
    std::function<void(std::vector<const std::string>)> content_callback,
    fit::completer<>&& completer) {
  iterator_->GetNext(
      [this, content_callback = std::move(content_callback),
       completer = std::move(completer)](auto result) mutable {
        if (result.is_err()) {
          FX_LOGS(WARNING) << "Failed to retrieve next log batch:"
                           << std::to_string(static_cast<size_t>(result.err()));
          completer.complete_error();
          return;
        }

        std::vector<fuchsia::diagnostics::FormattedContent> batch =
            std::move(result.response().batch);

        // "An empty vector implies that the data hierarchy has been fully
        // iterated".
        // https://fuchsia.dev/reference/fidl/fuchsia.diagnostics#fuchsia.diagnostics/BatchIterator.GetNext
        if (batch.empty()) {
          FX_LOGS(INFO) << "Done getting logs.";
          completer.complete_ok();
          return;
        }

        std::vector<const std::string> logs;

        for (const auto& chunk : batch) {
          if (!chunk.is_json()) {
            FX_LOGS(WARNING) << "Missing JSON log chunk, skipping";
            continue;
          }

          std::string json;
          if (!fsl::StringFromVmo(chunk.json(), &json)) {
            FX_LOGS(WARNING)
                << "Failed to convert Log data chunk to string, skipping";
            continue;
          }

          logs.emplace_back(json);
        }
        content_callback(logs);
        GetLogData(std::move(content_callback), std::move(completer));
      });
}

}  // namespace harvester
