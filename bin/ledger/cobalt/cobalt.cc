// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cobalt/cobalt.h"

#include <set>

#include "application/lib/app/connect.h"
#include "application/services/application_environment.fidl.h"
#include "apps/cobalt_client/services/cobalt.fidl.h"
#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/callback/waiter.h"
#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace ledger {
namespace {
constexpr int32_t kLedgerCobaltProjectId = 100;
constexpr int32_t kCobaltMetricId = 2;
constexpr int32_t kCobaltEncodingId = 2;

class CobaltContext {
 public:
  CobaltContext(ftl::RefPtr<ftl::TaskRunner> task_runner,
                app::ApplicationContext* app_context);
  ~CobaltContext();

  void ReportEvent(CobaltEvent event);

 private:
  void ConnectToCobaltApplication();
  void OnConnectionError();
  void ReportEventOnMainThread(CobaltEvent event);
  void SendEvents();

  backoff::ExponentialBackoff backoff_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  app::ApplicationContext* app_context_;
  cobalt::CobaltEncoderPtr encoder_;

  std::multiset<CobaltEvent> events_to_send_;
  std::multiset<CobaltEvent> events_in_transit_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CobaltContext);
};

CobaltContext::CobaltContext(ftl::RefPtr<ftl::TaskRunner> task_runner,
                             app::ApplicationContext* app_context)
    : task_runner_(std::move(task_runner)), app_context_(app_context) {
  ConnectToCobaltApplication();
}

CobaltContext::~CobaltContext() {
  if (!events_in_transit_.empty() || !events_to_send_.empty()) {
    FTL_LOG(WARNING) << "Disconnecting connection to cobalt with event still "
                        "pending... Events will be lost.";
  }
}

void CobaltContext::ReportEvent(CobaltEvent event) {
  task_runner_->PostTask([this, event]() { ReportEventOnMainThread(event); });
}

void CobaltContext::ConnectToCobaltApplication() {
  auto encoder_factory =
      app_context_->ConnectToEnvironmentService<cobalt::CobaltEncoderFactory>();
  encoder_factory->GetEncoder(kLedgerCobaltProjectId, encoder_.NewRequest());
  encoder_.set_connection_error_handler([this] { OnConnectionError(); });

  SendEvents();
}

void CobaltContext::OnConnectionError() {
  FTL_LOG(ERROR) << "Connection to cobalt failed. Reconnecting after a delay.";

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
  FTL_DCHECK(events_in_transit_.empty());

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
          auto cleanup = ftl::MakeAutoCall(callback);

          switch (status) {
            case cobalt::Status::INVALID_ARGUMENTS:
            case cobalt::Status::FAILED_PRECONDITION:
              FTL_DCHECK(false) << "Unexpected status: " << status;
            case cobalt::Status::OBSERVATION_TOO_BIG:  // fall through
              // Log the failure.
              FTL_LOG(WARNING)
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

ftl::AutoCall<ftl::Closure> InitializeCobalt(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    app::ApplicationContext* app_context) {
  FTL_DCHECK(!g_cobalt_context);
  auto context =
      std::make_unique<CobaltContext>(std::move(task_runner), app_context);
  g_cobalt_context = context.get();
  return ftl::MakeAutoCall<ftl::Closure>(
      ftl::MakeCopyable([context = std::move(context)]() mutable {
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
