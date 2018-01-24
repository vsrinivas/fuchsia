// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/cobalt/cobalt.h"

#include <set>

#include "lib/app/cpp/connect.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/cobalt/fidl/cobalt.fidl.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/backoff/exponential_backoff.h"
#include "peridot/lib/callback/waiter.h"

namespace cobalt {

CobaltContext::CobaltContext(fxl::RefPtr<fxl::TaskRunner> task_runner,
                             app::ApplicationContext* app_context,
                             int32_t project_id,
                             int32_t metric_id,
                             int32_t encoding_id)
    : task_runner_(std::move(task_runner)),
      app_context_(app_context),
      project_id_(project_id),
      metric_id_(metric_id),
      encoding_id_(encoding_id) {
  ConnectToCobaltApplication();
}

CobaltContext::~CobaltContext() {
  if (!events_in_transit_.empty() || !events_to_send_.empty()) {
    FXL_LOG(WARNING) << "Disconnecting connection to cobalt with event still "
                        "pending... Events will be lost.";
  }
}

void CobaltContext::ReportEvent(uint32_t event) {
  if (task_runner_->RunsTasksOnCurrentThread()) {
    ReportEventOnMainThread(event);
    return;
  }

  // Hop to the main thread, and go back to the global object dispatcher.
  task_runner_->PostTask(
      [event, this]() { ::cobalt::ReportEvent(event, this); });
}

void CobaltContext::ConnectToCobaltApplication() {
  auto encoder_factory =
      app_context_->ConnectToEnvironmentService<CobaltEncoderFactory>();
  encoder_factory->GetEncoder(project_id_, encoder_.NewRequest());
  encoder_.set_error_handler([this] { OnConnectionError(); });

  SendEvents();
}

void CobaltContext::OnConnectionError() {
  FXL_LOG(ERROR) << "Connection to cobalt failed. Reconnecting after a delay.";

  events_to_send_.insert(events_in_transit_.begin(), events_in_transit_.end());
  events_in_transit_.clear();
  encoder_.Unbind();
  task_runner_->PostDelayedTask([this] { ConnectToCobaltApplication(); },
                                backoff_.GetNext());
}

void CobaltContext::ReportEventOnMainThread(uint32_t event) {
  events_to_send_.insert(event);
  if (!encoder_ || !events_in_transit_.empty()) {
    return;
  }

  SendEvents();
}

void CobaltContext::SendEvents() {
  FXL_DCHECK(events_in_transit_.empty());

  if (events_to_send_.empty()) {
    return;
  }

  events_in_transit_ = std::move(events_to_send_);
  events_to_send_.clear();

  auto waiter = callback::CompletionWaiter::Create();
  for (auto event : events_in_transit_) {
    auto callback = waiter->NewCallback();
    encoder_->AddIndexObservation(
        metric_id_, encoding_id_, static_cast<uint32_t>(event),
        [this, event, callback = std::move(callback)](Status status) {
          auto cleanup = fxl::MakeAutoCall(callback);

          switch (status) {
            case Status::INVALID_ARGUMENTS:
            case Status::FAILED_PRECONDITION:
              FXL_DCHECK(false) << "Unexpected status: " << status;
            case Status::OBSERVATION_TOO_BIG:  // fall through
              // Log the failure.
              FXL_LOG(WARNING) << "Cobalt rejected event: " << event
                               << " with status: " << status;
            case cobalt::Status::OK:  // fall through
              // Remove the event from the set of
              // events to send.
              events_in_transit_.erase(event);
              break;
            case Status::INTERNAL_ERROR:
            case Status::SEND_FAILED:
            case Status::TEMPORARILY_FULL:
              // Keep the event for re-queueing.
              break;
          }
        });
  }
  waiter->Finalize([this]() {
    // No transient errors.
    if (events_in_transit_.empty()) {
      backoff_.Reset();
      // Send any event received while |events_in_transit_| was not empty.
      SendEvents();
      return;
    }

    // A transient error happened, retry after a delay.
    task_runner_->PostDelayedTask(
        [this]() {
          events_to_send_.insert(events_in_transit_.begin(),
                                 events_in_transit_.end());
          events_in_transit_.clear();
          SendEvents();
        },
        backoff_.GetNext());
  });
}

fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* app_context,
    int32_t project_id,
    int32_t metric_id,
    int32_t encoding_id,
    CobaltContext** cobalt_context) {
  FXL_DCHECK(!*cobalt_context);
  auto context = std::make_unique<CobaltContext>(
      std::move(task_runner), app_context, project_id, metric_id, encoding_id);
  *cobalt_context = context.get();
  return fxl::MakeAutoCall<fxl::Closure>(fxl::MakeCopyable(
      [context = std::move(context), cobalt_context]() mutable {
        context.reset();
        *cobalt_context = nullptr;
      }));
}

void ReportEvent(uint32_t event, CobaltContext* cobalt_context) {
  if (cobalt_context) {
    cobalt_context->ReportEvent(event);
  }
}

}  // namespace cobalt
