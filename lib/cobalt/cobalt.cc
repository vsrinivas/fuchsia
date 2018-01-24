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

CobaltObservation::CobaltObservation(uint32_t metric_id,
                                     uint32_t encoding_id, ValuePtr value) :
    encoding_id_(encoding_id), metric_id_(metric_id), value_(value.Clone()) {
  FXL_DCHECK(!value.is_null() && (
      value->is_string_value() || value->is_int_value() ||
      value->is_double_value() || value->is_index_value() ||
      value->is_int_bucket_distribution()));
}

CobaltObservation::CobaltObservation(const CobaltObservation& rhs) :
  CobaltObservation(rhs.metric_id_, rhs.encoding_id_, rhs.value_.Clone()) {}

CobaltObservation::CobaltObservation(CobaltObservation&& rhs) :
  CobaltObservation(rhs.metric_id_, rhs.encoding_id_, rhs.value_.Clone()) {}

CobaltObservation::~CobaltObservation() {
  value_.reset();
}

std::string CobaltObservation::ValueRepr() {
  switch (value_->which()) {
    case Value::Tag::__UNKNOWN__:
      return "unknown";
    case Value::Tag::STRING_VALUE:
      return value_->get_string_value();
    case Value::Tag::DOUBLE_VALUE:
      return std::to_string(value_->get_double_value());
    case Value::Tag::INT_VALUE:
      return std::to_string(value_->get_int_value());
    case Value::Tag::INDEX_VALUE:
      return std::to_string(value_->get_index_value());
    case Value::Tag::INT_BUCKET_DISTRIBUTION:
      std::ostringstream stream;
      stream << "bucket of size ";
      stream << value_->get_int_bucket_distribution().size();
      return stream.str();
  }
}

bool CobaltObservation::operator<(const CobaltObservation& rhs) const {
  if (metric_id_ != rhs.metric_id_) {
    return metric_id_ < rhs.metric_id_;
  }
  if (encoding_id_ != rhs.encoding_id_) {
    return encoding_id_ < rhs.encoding_id_;
  }
  if (value_->which() != rhs.value_->which()) {
    return value_->which() < rhs.value_->which();
  }
  switch (value_->which()) {
    case Value::Tag::__UNKNOWN__:
      return false;
    case Value::Tag::DOUBLE_VALUE:
      return value_->get_double_value() < rhs.value_->get_double_value();
    case Value::Tag::INT_VALUE:
      return value_->get_int_value() < rhs.value_->get_int_value();
    case Value::Tag::INDEX_VALUE:
      return value_->get_index_value() < rhs.value_->get_index_value();
    case Value::Tag::STRING_VALUE:
      return value_->get_string_value() < rhs.value_->get_string_value();
    case Value::Tag::INT_BUCKET_DISTRIBUTION: {
      if (value_->get_int_bucket_distribution().size() ==
          rhs.value_->get_int_bucket_distribution().size()) {
        auto i = value_->get_int_bucket_distribution().cbegin();
        auto j = rhs.value_->get_int_bucket_distribution().cbegin();
        while (i != value_->get_int_bucket_distribution().cend()) {
          if (i.GetKey() != j.GetKey()) {
            return i.GetKey() < j.GetKey();
          }
          if (i.GetValue() != j.GetValue()) {
            return i.GetValue() < j.GetValue();
          }
          ++i;
          ++j;
        }
        return false;
      }
      return value_->get_int_bucket_distribution().size() <
          rhs.value_->get_int_bucket_distribution().size();
    }
  }
}

CobaltObservation& CobaltObservation::operator=(const CobaltObservation& rhs) {
  if (this != &rhs) {
    encoding_id_ = rhs.encoding_id_;
    metric_id_ = rhs.metric_id_;
    value_ = rhs.value_.Clone();
  }
  return *this;
}

CobaltObservation& CobaltObservation::operator=(CobaltObservation&& rhs) {
  if (this != &rhs) {
    encoding_id_ = rhs.encoding_id_;
    metric_id_ = rhs.metric_id_;
    value_ = std::move(rhs.value_);
  }
  return *this;
}

CobaltContext::CobaltContext(fxl::RefPtr<fxl::TaskRunner> task_runner,
                             app::ApplicationContext* app_context,
                             int32_t project_id)
    : task_runner_(std::move(task_runner)), app_context_(app_context),
      project_id_(project_id) {
  ConnectToCobaltApplication();
}

CobaltContext::~CobaltContext() {
  if (!observations_in_transit_.empty() || !observations_to_send_.empty()) {
    FXL_LOG(WARNING) << "Disconnecting connection to cobalt with observation "
                        "still pending... Observations will be lost.";
  }
}

void CobaltContext::ReportObservation(CobaltObservation observation) {
  if (task_runner_->RunsTasksOnCurrentThread()) {
    ReportObservationOnMainThread(std::move(observation));
    return;
  }

  // Hop to the main thread, and go back to the global object dispatcher.
  task_runner_->PostTask([observation = std::move(observation), this]() {
      ::cobalt::ReportObservation(observation, this); });
}

void CobaltContext::ConnectToCobaltApplication() {
  auto encoder_factory =
      app_context_->ConnectToEnvironmentService<CobaltEncoderFactory>();
  encoder_factory->GetEncoder(project_id_, encoder_.NewRequest());
  encoder_.set_error_handler([this] { OnConnectionError(); });

  SendObservations();
}

void CobaltContext::OnConnectionError() {
  FXL_LOG(ERROR) << "Connection to cobalt failed. Reconnecting after a delay.";

  observations_to_send_.insert(observations_in_transit_.begin(),
                               observations_in_transit_.end());
  observations_in_transit_.clear();
  encoder_.Unbind();
  task_runner_->PostDelayedTask([this] { ConnectToCobaltApplication(); },
                                backoff_.GetNext());
}

void CobaltContext::ReportObservationOnMainThread(
    CobaltObservation observation) {
  observations_to_send_.insert(observation);
  if (!encoder_ || !observations_in_transit_.empty()) {
    return;
  }

  SendObservations();
}

void CobaltContext::SendObservations() {
  FXL_DCHECK(observations_in_transit_.empty());

  if (observations_to_send_.empty()) {
    return;
  }

  observations_in_transit_ = std::move(observations_to_send_);
  observations_to_send_.clear();

  auto waiter = callback::CompletionWaiter::Create();
  for (auto observation : observations_in_transit_) {
    auto callback = waiter->NewCallback();
    encoder_->AddObservation(
        observation.metric_id(), observation.encoding_id(),
        observation.value().Clone(),
        [this, observation, callback = std::move(callback)](Status status) {
          AddObservationCallback(observation, status);
          callback();
        });
  }
  waiter->Finalize([this]() {
    // No transient errors.
    if (observations_in_transit_.empty()) {
      backoff_.Reset();
      // Send any observation received while |observations_in_transit_| was not
      // empty.
      SendObservations();
      return;
    }

    // A transient error happened, retry after a delay.
    // TODO(miguelfrde): issue if we delete the context while a retry is in
    // flight.
    task_runner_->PostDelayedTask(
        [this]() {
          observations_to_send_.insert(observations_in_transit_.begin(),
                                 observations_in_transit_.end());
          observations_in_transit_.clear();
          SendObservations();
        },
        backoff_.GetNext());
  });
}

void CobaltContext::AddObservationCallback(CobaltObservation observation,
                                           cobalt::Status status) {
  switch (status) {
    case cobalt::Status::INVALID_ARGUMENTS:
    case cobalt::Status::FAILED_PRECONDITION:
      FXL_DCHECK(false) << "Unexpected status: " << status;
    case cobalt::Status::OBSERVATION_TOO_BIG:  // fall through
      // Log the failure.
      FXL_LOG(WARNING)
          << "Cobalt rejected obsevation for metric: "
          << observation.metric_id()
          << " with encoding: " << observation.encoding_id()
          << " and value: " << observation.ValueRepr()
          << " with status: " << status;
    case cobalt::Status::OK:  // fall through
      // Remove the observation from the set of observations to send.
      observations_in_transit_.erase(observation);
      break;
    case cobalt::Status::INTERNAL_ERROR:
    case cobalt::Status::SEND_FAILED:
    case cobalt::Status::TEMPORARILY_FULL:
      // Keep the observation for re-queueing.
      break;
  }
}

fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* app_context,
    int32_t project_id,
    CobaltContext** cobalt_context) {
  FXL_DCHECK(!*cobalt_context);
  auto context = std::make_unique<CobaltContext>(std::move(task_runner),
                                                 app_context, project_id);
  *cobalt_context = context.get();
  return fxl::MakeAutoCall<fxl::Closure>(fxl::MakeCopyable(
      [context = std::move(context), cobalt_context]() mutable {
        context.reset();
        *cobalt_context = nullptr;
      }));
}

void ReportObservation(CobaltObservation observation,
                       CobaltContext* cobalt_context) {
  if (cobalt_context) {
    cobalt_context->ReportObservation(observation);
  }
}

}  // namespace cobalt
