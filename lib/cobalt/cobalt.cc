// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/cobalt/cobalt.h"

#include <set>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/callback/waiter.h>
#include <lib/component/cpp/connect.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/function.h>
#include <lib/fxl/functional/auto_call.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

using fuchsia::cobalt::EncoderFactory;
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

fxl::AutoCall<fit::closure> InitializeCobalt(
    std::unique_ptr<CobaltContext> context, CobaltContext** cobalt_context) {
  FXL_DCHECK(!*cobalt_context);
  *cobalt_context = context.get();
  return fxl::MakeAutoCall<fit::closure>(
      [context = std::move(context), cobalt_context]() mutable {
        context.reset();
        *cobalt_context = nullptr;
      });
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

void CobaltObservation::Report(fuchsia::cobalt::EncoderPtr* encoder,
                               fit::function<void(Status)> callback) && {
  if (parts_->size() == 1) {
    (*encoder)->AddObservation(metric_id_, parts_->at(0).encoding_id,
                               std::move(parts_->at(0).value),
                               std::move(callback));
  } else {
    (*encoder)->AddMultipartObservation(metric_id_, std::move(parts_),
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
  CobaltContextImpl(async_dispatcher_t* dispatcher,
                    component::StartupContext* context, int32_t project_id);
  CobaltContextImpl(async_dispatcher_t* dispatcher,
                    component::StartupContext* context, fsl::SizedVmo config);
  ~CobaltContextImpl() override;

  void ReportObservation(CobaltObservation observation) override;

 private:
  void ConnectToCobaltApplication();
  void OnConnectionError();
  void ReportObservationOnMainThread(CobaltObservation observation);
  void SendObservations();
  void AddObservationCallback(CobaltObservation observation, Status status);

  backoff::ExponentialBackoff backoff_;
  async_dispatcher_t* const dispatcher_;
  component::StartupContext* context_;
  fuchsia::cobalt::EncoderPtr encoder_;
  const int32_t project_id_ = 0;
  const fsl::SizedVmo config_;

  std::multiset<CobaltObservation> observations_to_send_;
  std::multiset<CobaltObservation> observations_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltContextImpl);
};

CobaltContextImpl::CobaltContextImpl(async_dispatcher_t* dispatcher,
                                     component::StartupContext* context,
                                     int32_t project_id)
    : dispatcher_(dispatcher), context_(context), project_id_(project_id) {
  ConnectToCobaltApplication();
}

CobaltContextImpl::CobaltContextImpl(async_dispatcher_t* dispatcher,
                                     component::StartupContext* context,
                                     fsl::SizedVmo config)
    : dispatcher_(dispatcher), context_(context), config_(std::move(config)) {
  ConnectToCobaltApplication();
}

CobaltContextImpl::~CobaltContextImpl() {
  if (!observations_in_transit_.empty() || !observations_to_send_.empty()) {
    FXL_LOG(WARNING) << "Disconnecting connection to cobalt with observation "
                        "still pending... Observations will be lost.";
  }
}

void CobaltContextImpl::ReportObservation(CobaltObservation observation) {
  if (dispatcher_ == async_get_default_dispatcher()) {
    ReportObservationOnMainThread(std::move(observation));
    return;
  }

  // Hop to the main thread, and go back to the global object dispatcher.
  async::PostTask(dispatcher_, [observation = std::move(observation), this]() {
    ::cobalt::ReportObservation(observation, this);
  });
}

void CobaltContextImpl::ConnectToCobaltApplication() {
  auto encoder_factory =
      context_->ConnectToEnvironmentService<EncoderFactory>();

  if (project_id_ > 0) {
    encoder_factory->GetEncoder(project_id_, encoder_.NewRequest());
    encoder_.set_error_handler([this] { OnConnectionError(); });
    SendObservations();
  } else {
    fsl::SizedVmo config_vmo;
    FXL_CHECK(config_.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                                &config_vmo) == ZX_OK)
        << "Could not clone config VMO";

    fuchsia::cobalt::ProjectProfile profile;
    fuchsia::mem::Buffer buf = std::move(config_vmo).ToTransport();
    profile.config.vmo = std::move(buf.vmo);
    profile.config.size = buf.size;

    encoder_factory->GetEncoderForProject(
        std::move(profile), encoder_.NewRequest(), [this](Status status) {
          if (status == Status::OK) {
            if (encoder_) {
              encoder_.set_error_handler([this] { OnConnectionError(); });
              SendObservations();
            } else {
              OnConnectionError();
            }
          } else {
            FXL_LOG(ERROR)
                << "GetEncoderForProject() received invalid arguments";
          }
        });
  }
}

void CobaltContextImpl::OnConnectionError() {
  FXL_LOG(ERROR) << "Connection to cobalt failed. Reconnecting after a delay.";

  observations_to_send_.insert(observations_in_transit_.begin(),
                               observations_in_transit_.end());
  observations_in_transit_.clear();
  encoder_.Unbind();
  async::PostDelayedTask(dispatcher_, [this] { ConnectToCobaltApplication(); },
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
        .Report(&encoder_, [this, observation,
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
    async::PostDelayedTask(dispatcher_,
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
      FXL_DCHECK(false) << "Unexpected status: " << fidl::ToUnderlying(status);
    case Status::OBSERVATION_TOO_BIG:  // fall through
      // Log the failure.
      FXL_LOG(WARNING) << "Cobalt rejected obsevation for metric: "
                       << observation.metric_id()
                       << " with value: " << observation.ValueRepr()
                       << " with status: " << fidl::ToUnderlying(status);
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
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    int32_t project_id) {
  return std::make_unique<CobaltContextImpl>(dispatcher, context, project_id);
}

std::unique_ptr<CobaltContext> MakeCobaltContext(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    fsl::SizedVmo config) {
  return std::make_unique<CobaltContextImpl>(dispatcher, context,
                                             std::move(config));
}

fxl::AutoCall<fit::closure> InitializeCobalt(
    async_dispatcher_t* dispatcher, component::StartupContext* startup_context,
    int32_t project_id, CobaltContext** cobalt_context) {
  return InitializeCobalt(
      MakeCobaltContext(dispatcher, startup_context, project_id),
      cobalt_context);
}

fxl::AutoCall<fit::closure> InitializeCobalt(
    async_dispatcher_t* dispatcher, component::StartupContext* startup_context,
    fsl::SizedVmo config, CobaltContext** cobalt_context) {
  return InitializeCobalt(
      MakeCobaltContext(dispatcher, startup_context, std::move(config)),
      cobalt_context);
}

void ReportObservation(CobaltObservation observation,
                       CobaltContext* cobalt_context) {
  if (cobalt_context) {
    cobalt_context->ReportObservation(observation);
  }
}

}  // namespace cobalt
