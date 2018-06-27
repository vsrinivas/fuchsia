// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/cobalt/cobalt.h"

#include <set>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>

#include "lib/app/cpp/connect.h"
#include "lib/backoff/exponential_backoff.h"
#include "lib/callback/waiter.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

using fuchsia::cobalt::CobaltEncoderFactory;
using fuchsia::cobalt::ObservationValue;
using fuchsia::cobalt::Status;
using fuchsia::cobalt::Value;

namespace cobalt {

namespace {
fidl::VectorPtr<ObservationValue> CloneObservationValues(
    const fidl::VectorPtr<ObservationValue>& other) {
  fidl::VectorPtr<ObservationValue> result;
  zx_status_t status = fidl::Clone(other, &result);
  FXL_DCHECK(status == ZX_OK);
  return result;
}
}  // namespace

CobaltObservation::CobaltObservation(uint32_t metric_id, uint32_t encoding_id,
                                     Value value)
    : metric_id_(metric_id) {
  FXL_DCHECK(value.is_string_value() || value.is_int_value() ||
             value.is_double_value() || value.is_index_value() ||
             value.is_int_bucket_distribution());
  parts_.push_back(ObservationValue());
  parts_->at(0).value = std::move(value);
  parts_->at(0).encoding_id = encoding_id;
}

CobaltObservation::~CobaltObservation() = default;

CobaltObservation::CobaltObservation(uint32_t metric_id,
                                     fidl::VectorPtr<ObservationValue> parts)
    : metric_id_(metric_id), parts_(std::move(parts)) {}

CobaltObservation::CobaltObservation(const CobaltObservation& rhs)
    : CobaltObservation(rhs.metric_id_, CloneObservationValues(rhs.parts_)) {}

CobaltObservation::CobaltObservation(CobaltObservation&& rhs)
    : CobaltObservation(rhs.metric_id_, std::move(rhs.parts_)) {}

void CobaltObservation::Report(fuchsia::cobalt::CobaltEncoderPtr& encoder,
                               fit::function<void(Status)> callback) && {
  if (parts_->size() == 1) {
    encoder->AddObservation(metric_id_, parts_->at(0).encoding_id,
                            std::move(parts_->at(0).value),
                            std::move(callback));
  } else {
    encoder->AddMultipartObservation(metric_id_, std::move(parts_),
                                     std::move(callback));
  }
}

std::string CobaltObservation::ValueRepr() {
  std::ostringstream stream;
  stream << "[";
  for (auto& observation_value : *parts_) {
    const Value& value = observation_value.value;
    switch (value.Which()) {
      case Value::Tag::Invalid: {
        stream << "unknown";
        break;
      }
      case Value::Tag::kStringValue: {
        stream << value.string_value();
        break;
      }
      case Value::Tag::kDoubleValue: {
        stream << value.double_value();
        break;
      }
      case Value::Tag::kIntValue: {
        stream << value.int_value();
        break;
      }
      case Value::Tag::kIndexValue: {
        stream << value.index_value();
        break;
      }
      case Value::Tag::kIntBucketDistribution: {
        stream << "bucket of size " << value.int_bucket_distribution()->size();
        break;
      }
    }
    stream << ",";
  }
  stream << "]";
  return stream.str();
}

bool CobaltObservation::operator<(const CobaltObservation& rhs) const {
  if (metric_id_ != rhs.metric_id_) {
    return metric_id_ < rhs.metric_id_;
  }
  if (parts_->size() < rhs.parts_->size()) {
    return true;
  }
  for (uint64_t i = 0; i < parts_->size(); i++) {
    if (!CompareObservationValueLess(parts_->at(i), rhs.parts_->at(i))) {
      return false;
    }
  }
  return true;
}

bool CobaltObservation::CompareObservationValueLess(
    const ObservationValue& observation_value,
    const ObservationValue& rhs_observation_value) const {
  if (observation_value.encoding_id != observation_value.encoding_id) {
    return observation_value.encoding_id < rhs_observation_value.encoding_id;
  }
  const Value& value = observation_value.value;
  const Value& rhs_value = rhs_observation_value.value;
  if (value.Which() != rhs_value.Which()) {
    return value.Which() < rhs_value.Which();
  }
  switch (value.Which()) {
    case Value::Tag::Invalid:
      return false;
    case Value::Tag::kDoubleValue:
      return value.double_value() < rhs_value.double_value();
    case Value::Tag::kIntValue:
      return value.int_value() < rhs_value.int_value();
    case Value::Tag::kIndexValue:
      return value.index_value() < rhs_value.index_value();
    case Value::Tag::kStringValue:
      return value.string_value() < rhs_value.string_value();
    case Value::Tag::kIntBucketDistribution: {
      if (value.int_bucket_distribution()->size() ==
          rhs_value.int_bucket_distribution()->size()) {
        auto i = value.int_bucket_distribution()->begin();
        auto j = rhs_value.int_bucket_distribution()->begin();
        while (i != value.int_bucket_distribution()->end()) {
          if ((*i).index != (*j).index) {
            return (*i).index < (*j).index;
          }
          if ((*i).count != (*j).count) {
            return (*i).count < (*j).count;
          }
          ++i;
          ++j;
        }
        return false;
      }
      return value.int_bucket_distribution()->size() <
             rhs_value.int_bucket_distribution()->size();
    }
  }
}

CobaltObservation& CobaltObservation::operator=(const CobaltObservation& rhs) {
  if (this != &rhs) {
    metric_id_ = rhs.metric_id_;
    parts_ = CloneObservationValues(rhs.parts_);
  }
  return *this;
}

CobaltObservation& CobaltObservation::operator=(CobaltObservation&& rhs) {
  if (this != &rhs) {
    metric_id_ = rhs.metric_id_;
    parts_ = std::move(rhs.parts_);
  }
  return *this;
}

namespace {
class CobaltContextImpl : public CobaltContext {
 public:
  CobaltContextImpl(async_t* async, fuchsia::sys::StartupContext* context,
                    int32_t project_id);
  ~CobaltContextImpl();

  void ReportObservation(CobaltObservation observation) override;

 private:
  void ConnectToCobaltApplication();
  void OnConnectionError();
  void ReportObservationOnMainThread(CobaltObservation observation);
  void SendObservations();
  void AddObservationCallback(CobaltObservation observation, Status status);

  backoff::ExponentialBackoff backoff_;
  async_t* const async_;
  fuchsia::sys::StartupContext* context_;
  fuchsia::cobalt::CobaltEncoderPtr encoder_;
  const int32_t project_id_;

  std::multiset<CobaltObservation> observations_to_send_;
  std::multiset<CobaltObservation> observations_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltContextImpl);
};

CobaltContextImpl::CobaltContextImpl(async_t* async,
                                     fuchsia::sys::StartupContext* context,
                                     int32_t project_id)
    : async_(async), context_(context), project_id_(project_id) {
  ConnectToCobaltApplication();
}

CobaltContextImpl::~CobaltContextImpl() {
  if (!observations_in_transit_.empty() || !observations_to_send_.empty()) {
    FXL_LOG(WARNING) << "Disconnecting connection to cobalt with observation "
                        "still pending... Observations will be lost.";
  }
}

void CobaltContextImpl::ReportObservation(CobaltObservation observation) {
  if (async_ == async_get_default()) {
    ReportObservationOnMainThread(std::move(observation));
    return;
  }

  // Hop to the main thread, and go back to the global object dispatcher.
  async::PostTask(async_, [observation = std::move(observation), this]() {
    ::cobalt::ReportObservation(observation, this);
  });
}

void CobaltContextImpl::ConnectToCobaltApplication() {
  auto encoder_factory =
      context_->ConnectToEnvironmentService<CobaltEncoderFactory>();
  encoder_factory->GetEncoder(project_id_, encoder_.NewRequest());
  encoder_.set_error_handler([this] { OnConnectionError(); });

  SendObservations();
}

void CobaltContextImpl::OnConnectionError() {
  FXL_LOG(ERROR) << "Connection to cobalt failed. Reconnecting after a delay.";

  observations_to_send_.insert(observations_in_transit_.begin(),
                               observations_in_transit_.end());
  observations_in_transit_.clear();
  encoder_.Unbind();
  async::PostDelayedTask(async_, [this] { ConnectToCobaltApplication(); },
                         backoff_.GetNext());
}

void CobaltContextImpl::ReportObservationOnMainThread(
    CobaltObservation observation) {
  observations_to_send_.insert(observation);
  if (!encoder_ || !observations_in_transit_.empty()) {
    return;
  }

  SendObservations();
}

void CobaltContextImpl::SendObservations() {
  FXL_DCHECK(observations_in_transit_.empty());

  if (observations_to_send_.empty()) {
    return;
  }

  observations_in_transit_ = std::move(observations_to_send_);
  observations_to_send_.clear();

  auto waiter = fxl::MakeRefCounted<callback::CompletionWaiter>();
  for (auto observation : observations_in_transit_) {
    auto callback = waiter->NewCallback();
    std::move(observation)
        .Report(encoder_, [this, observation,
                           callback = std::move(callback)](Status status) {
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
    async::PostDelayedTask(async_,
                           [this]() {
                             observations_to_send_.insert(
                                 observations_in_transit_.begin(),
                                 observations_in_transit_.end());
                             observations_in_transit_.clear();
                             SendObservations();
                           },
                           backoff_.GetNext());
  });
}

void CobaltContextImpl::AddObservationCallback(CobaltObservation observation,
                                               Status status) {
  switch (status) {
    case Status::INVALID_ARGUMENTS:
    case Status::FAILED_PRECONDITION:
      FXL_DCHECK(false) << "Unexpected status: " << status;
    case Status::OBSERVATION_TOO_BIG:  // fall through
      // Log the failure.
      FXL_LOG(WARNING) << "Cobalt rejected obsevation for metric: "
                       << observation.metric_id()
                       << " with value: " << observation.ValueRepr()
                       << " with status: " << status;
    case Status::OK:  // fall through
      // Remove the observation from the set of observations to send.
      observations_in_transit_.erase(observation);
      break;
    case Status::INTERNAL_ERROR:
    case Status::SEND_FAILED:
    case Status::TEMPORARILY_FULL:
      // Keep the observation for re-queueing.
      break;
  }
}
}  // namespace

std::unique_ptr<CobaltContext> MakeCobaltContext(
    async_t* async, fuchsia::sys::StartupContext* context, int32_t project_id) {
  return std::make_unique<CobaltContextImpl>(async, context, project_id);
}

fxl::AutoCall<fit::closure> InitializeCobalt(
    async_t* async, fuchsia::sys::StartupContext* startup_context,
    int32_t project_id, CobaltContext** cobalt_context) {
  FXL_DCHECK(!*cobalt_context);
  auto context = MakeCobaltContext(async, startup_context, project_id);
  *cobalt_context = context.get();
  return fxl::MakeAutoCall<fit::closure>(fxl::MakeCopyable(
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
