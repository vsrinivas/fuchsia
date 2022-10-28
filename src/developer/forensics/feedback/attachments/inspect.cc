// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/attachments/inspect.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace forensics::feedback {

Inspect::Inspect(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                 std::unique_ptr<backoff::Backoff> backoff,
                 feedback_data::InspectDataBudget* data_budget)
    : dispatcher_(dispatcher),
      services_(std::move(services)),
      backoff_(std::move(backoff)),
      data_budget_(data_budget) {
  archive_accessor_.set_error_handler([this](const zx_status_t status) {
    FX_LOGS(WARNING) << "Lost connection to " << feedback_data::kArchiveAccessorName;
    auto self = ptr_factory_.GetWeakPtr();
    async::PostDelayedTask(
        dispatcher_,
        [self] {
          if (self) {
            self->services_->Connect(self->archive_accessor_.NewRequest(self->dispatcher_),
                                     feedback_data::kArchiveAccessorName);
          }
        },
        backoff_->GetNext());
  });

  services_->Connect(archive_accessor_.NewRequest(dispatcher_),
                     feedback_data::kArchiveAccessorName);
}

namespace {

// Manages collecting Inspect data and calling |complete| with done.
class InspectCollector {
 public:
  InspectCollector(::fit::callback<void(std::optional<Error>)> complete);

  void Run();

  ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> NewRequest(
      async_dispatcher_t* dispatcher);
  const std::vector<std::string>& Inspect() const;

 private:
  ::fit::callback<void(std::optional<Error>)> complete_;
  fuchsia::diagnostics::BatchIteratorPtr batch_iterator_;
  std::vector<std::string> inspect_;
};

InspectCollector::InspectCollector(::fit::callback<void(std::optional<Error>)> complete)
    : complete_(std::move(complete)) {
  batch_iterator_.set_error_handler([this](const zx_status_t status) mutable {
    if (complete_ != nullptr) {
      FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.diagnostics.BatchIterator";
      complete_(Error::kConnectionError);
    }
  });
}

::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> InspectCollector::NewRequest(
    async_dispatcher_t* dispatcher) {
  return batch_iterator_.NewRequest(dispatcher);
}

const std::vector<std::string>& InspectCollector::Inspect() const { return inspect_; }

void InspectCollector::Run() {
  if (complete_ == nullptr) {
    return;
  }

  FX_CHECK(batch_iterator_.is_bound())
      << "Attempting to collect Inspect without BatchIterator connection";

  batch_iterator_->GetNext([this](auto result) mutable {
    // Inspect collection has completed elsewhere, stop.
    if (complete_ == nullptr) {
      return;
    }

    // Get the next batch of Inspect data when done.
    auto get_next = ::fit::defer([this] { Run(); });

    // Try again if the result was an error.
    if (result.is_err()) {
      FX_LOGS(WARNING) << "Failed to retrieve Inspect batch: "
                       << static_cast<uint64_t>(result.err());
      return;
    }

    const auto& batch = result.response().batch;

    // All of the Inspect data has been collected.
    if (batch.empty()) {
      complete_(std::nullopt);
      get_next.cancel();
      return;
    }

    for (const auto& chunk : batch) {
      if (!chunk.is_json()) {
        FX_LOGS(WARNING) << "Invalid JSON Inspect chunk, skipping";
        continue;
      }

      if (std::string json; fsl::StringFromVmo(chunk.json(), &json)) {
        inspect_.push_back(std::move(json));
      } else {
        FX_LOGS(WARNING) << "Failed to convert Inspect chunk to string, skipping";
      }
    }
  });
}

}  // namespace

::fpromise::promise<AttachmentValue> Inspect::Get(const zx::duration timeout) {
  return Get(internal_ticket_--, timeout);
}

::fpromise::promise<AttachmentValue> Inspect::Get(const uint64_t ticket,
                                                  const zx::duration timeout) {
  FX_CHECK(completers_.count(ticket) == 0) << "Ticket used twice: " << ticket;

  if (!archive_accessor_.is_bound()) {
    return ::fpromise::make_ok_promise(AttachmentValue(Error::kConnectionError));
  }

  ::fpromise::bridge<void, Error> bridge;

  // Construct a promise and an object that can be used to complete the promise with a value at a
  // later point in time.
  auto consume = bridge.consumer.promise_or(::fpromise::error(Error::kLogicError));
  ::fit::callback<void(std::optional<Error>)> complete =
      [completer = std::move(bridge.completer)](std::optional<Error> error) mutable {
        if (error.has_value()) {
          completer.complete_error(*error);
        } else {
          completer.complete_ok();
        }
      };

  auto collector = std::make_unique<InspectCollector>(complete.share());

  completers_[ticket] = std::move(complete);

  fuchsia::diagnostics::StreamParameters params;
  params.set_data_type(fuchsia::diagnostics::DataType::INSPECT)
      .set_format(fuchsia::diagnostics::Format::JSON)
      .set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT)
      .set_client_selector_configuration(
          fuchsia::diagnostics::ClientSelectorConfiguration::WithSelectAll(true));

  if (const auto budget = data_budget_->SizeInBytes(); budget.has_value()) {
    fuchsia::diagnostics::PerformanceConfiguration performance;
    performance.set_max_aggregate_content_size_bytes(*budget);
    params.set_performance_configuration(std::move(performance));
  }

  archive_accessor_->StreamDiagnostics(std::move(params), collector->NewRequest(dispatcher_));

  auto self = ptr_factory_.GetWeakPtr();

  collector->Run();
  async::PostDelayedTask(
      dispatcher_,
      [self, ticket]() mutable {
        if (self) {
          self->ForceCompletion(ticket, Error::kTimeout);
        }
      },
      timeout);

  // Keep |collector| alive until Inspect collection has completed (for any reason).
  return consume.then([self, ticket, collector = std::move(collector)](
                          const ::fpromise::result<void, Error>& result) mutable
                      -> ::fpromise::result<AttachmentValue> {
    auto& inspect = collector->Inspect();
    if (inspect.empty()) {
      FX_LOGS(WARNING) << "Inspect data was empty";
      return ::fpromise::ok(result.is_ok() ? Error::kMissingValue : result.error());
    }

    if (self) {
      self->completers_.erase(ticket);
    }

    std::string joined_data = "[\n";
    joined_data += fxl::JoinStrings(inspect, ",\n");
    joined_data += "\n]";

    AttachmentValue value = (result.is_ok())
                                ? AttachmentValue(std::move(joined_data))
                                : AttachmentValue(std::move(joined_data), result.error());

    return ::fpromise::ok(std::move(value));
  });
}

void Inspect::ForceCompletion(const uint64_t ticket, const Error error) {
  if (completers_.count(ticket) != 0 && completers_[ticket] != nullptr) {
    completers_[ticket](error);
  }
}

}  // namespace forensics::feedback
