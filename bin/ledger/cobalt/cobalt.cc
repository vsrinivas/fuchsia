// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cobalt/cobalt.h"

#include <set>

#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/callback/waiter.h"
#include "lib/app/cpp/connect.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/cobalt/fidl/cobalt.fidl.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace ledger {
namespace {
constexpr int32_t kLedgerCobaltProjectId = 100;
constexpr int32_t kCobaltMetricId = 2;
constexpr int32_t kCobaltEncodingId = 2;

class CobaltContext {
 public:
  CobaltContext(fxl::RefPtr<fxl::TaskRunner> task_runner,
                app::ApplicationContext* app_context);
  ~CobaltContext();

  void ReportEvent(CobaltEvent event);

 private:
  void ConnectToCobaltApplication();
  void OnConnectionError();
  void ReportEventOnMainThread(CobaltEvent event);
  void SendEvents();

  backoff::ExponentialBackoff backoff_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  app::ApplicationContext* app_context_;
  cobalt::CobaltEncoderPtr encoder_;

  std::multiset<CobaltEvent> events_to_send_;
  std::multiset<CobaltEvent> events_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltContext);
};

CobaltContext::CobaltContext(fxl::RefPtr<fxl::TaskRunner> task_runner,
                             app::ApplicationContext* app_context)
    : task_runner_(std::move(task_runner)), app_context_(app_context) {
  ConnectToCobaltApplication();
}

CobaltContext::~CobaltContext() {
  if (!events_in_transit_.empty() || !events_to_send_.empty()) {
    FXL_LOG(WARNING) << "Disconnecting connection to cobalt with event still "
                        "pending... Events will be lost.";
  }
}

void CobaltContext::ReportEvent(CobaltEvent event) {
  if (task_runner_->RunsTasksOnCurrentThread()) {
    ReportEventOnMainThread(event);
    return;
  }

  // Hop to the main thread, and go back to the global object dispatcher.
  task_runner_->PostTask([event]() { ::ledger::ReportEvent(event); });
}

void CobaltContext::ConnectToCobaltApplication() {
  auto encoder_factory =
      app_context_->ConnectToEnvironmentService<cobalt::CobaltEncoderFactory>();
  encoder_factory->GetEncoder(kLedgerCobaltProjectId, encoder_.NewRequest());
  encoder_.set_connection_error_handler([this] { OnConnectionError(); });

  SendEvents();
}

void CobaltContext::OnConnectionError() {
  FXL_LOG(ERROR) << "Connection to cobalt failed. Reconnecting after a delay.";

  events_to_send_.insert(events_in_transit_.begin(),
                         events_in_transit_.end());
  events_in_transit_.clear();
  encoder_.reset();
  task_runner_->PostDelayedTask([this] { ConnectToCobaltApplication(); },
                                backoff_.GetNext());
}

void CobaltContext::ReportEventOnMainThread(CobaltEvent event) {
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
        kCobaltMetricId, kCobaltEncodingId, static_cast<uint32_t>(event),
        [ this, event, callback = std::move(callback) ](cobalt::Status status) {
          auto cleanup = fxl::MakeAutoCall(callback);

          switch (status) {
            case cobalt::Status::INVALID_ARGUMENTS:
            case cobalt::Status::FAILED_PRECONDITION:
              FXL_DCHECK(false) << "Unexpected status: " << status;
            case cobalt::Status::OBSERVATION_TOO_BIG:  // fall through
              // Log the failure.
              FXL_LOG(WARNING)
                  << "Cobalt rejected event: " << static_cast<uint32_t>(event)
                  << " with status: " << status;
            case cobalt::Status::OK:  // fall through
              // Remove the event from the set of
              // events to send.
              events_in_transit_.erase(event);
              break;
            case cobalt::Status::INTERNAL_ERROR:
            case cobalt::Status::SEND_FAILED:
            case cobalt::Status::TEMPORARILY_FULL:
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

CobaltContext* g_cobalt_context = nullptr;

}  // namespace

fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* app_context) {
  FXL_DCHECK(!g_cobalt_context);
  auto context =
      std::make_unique<CobaltContext>(std::move(task_runner), app_context);
  g_cobalt_context = context.get();
  return fxl::MakeAutoCall<fxl::Closure>(
      fxl::MakeCopyable([context = std::move(context)]() mutable {
        context.reset();
        g_cobalt_context = nullptr;
      }));
}

void ReportEvent(CobaltEvent event) {
  if (g_cobalt_context) {
    g_cobalt_context->ReportEvent(event);
  }
}

}  // namespace ledger
