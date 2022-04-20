// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/log_source.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/log_source.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/diagnostics/accessor2logger/log_message.h"

namespace forensics::feedback_data {

LogSource::LogSource(async_dispatcher_t* dispatcher,
                     std::shared_ptr<sys::ServiceDirectory> services, LogSink* sink,
                     std::unique_ptr<backoff::Backoff> backoff)
    : dispatcher_(dispatcher),
      services_(std::move(services)),
      sink_(sink),
      backoff_(std::move(backoff)) {
  FX_CHECK(dispatcher_ != nullptr);
  FX_CHECK(services_ != nullptr);
  FX_CHECK(sink_ != nullptr);

  // |backoff_| can only be null if we know a reconnection won't occur.
  if (sink_->SafeAfterInterruption()) {
    FX_CHECK(backoff_ != nullptr);
  }

  archive_accessor_.set_error_handler([this](const zx_status_t status) {
    FX_LOGS(WARNING) << "Lost connection to " << kArchiveAccessorName;

    // The batch iterator and archive accessor connections are not expected to close. Ensure both
    // are unbound at the same time to simplify reconnections.
    batch_iterator_.Unbind();

    OnDisconnect();
  });

  batch_iterator_.set_error_handler([this](const zx_status_t status) {
    FX_LOGS(WARNING) << "Lost connection to fuchsia.diagnostics.BatchIterator";

    // The batch iterator and archive accessor connections are not expected to close. Ensure both
    // are unbound at the same time to simplify reconnections.
    archive_accessor_.Unbind();

    OnDisconnect();
  });
}

void LogSource::Start() {
  services_->Connect(archive_accessor_.NewRequest(dispatcher_), kArchiveAccessorName);

  fuchsia::diagnostics::StreamParameters params;
  params.set_data_type(fuchsia::diagnostics::DataType::LOGS)
      .set_format(fuchsia::diagnostics::Format::JSON)
      .set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT_THEN_SUBSCRIBE)
      .set_client_selector_configuration(
          fuchsia::diagnostics::ClientSelectorConfiguration::WithSelectAll(true));

  archive_accessor_->StreamDiagnostics(std::move(params), batch_iterator_.NewRequest(dispatcher_));
  GetNext();
}

void LogSource::OnDisconnect() {
  sink_->NotifyInterruption();
  if (!sink_->SafeAfterInterruption()) {
    return;
  }

  async::PostDelayedTask(
      dispatcher_,
      [self = ptr_factory_.GetWeakPtr()] {
        if (self) {
          self->Start();
        }
      },
      backoff_->GetNext());
}

void LogSource::GetNext() {
  using diagnostics::accessor2logger::ConvertFormattedContentToLogMessages;

  batch_iterator_->GetNext([this](auto result) {
    auto get_next = ::fit::defer([this] { GetNext(); });
    if (result.is_err()) {
      return;
    }

    for (auto& content : result.response().batch) {
      auto formatted = ConvertFormattedContentToLogMessages(std::move(content));
      if (formatted.is_error()) {
        sink_->Add(::fpromise::error(std::move(formatted.error())));
        continue;
      }

      for (auto& message : formatted.value()) {
        sink_->Add(std::move(message));
      }
    }
  });
}

void LogSource::Stop() {
  batch_iterator_.Unbind();
  archive_accessor_.Unbind();
}

}  // namespace forensics::feedback_data
