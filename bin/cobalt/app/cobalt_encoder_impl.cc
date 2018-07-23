// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_encoder_impl.h"

#include "garnet/bin/cobalt/app/utils.h"

namespace cobalt {
namespace encoder {

using cobalt::TimerManager;
using cobalt::TimerVal;

CobaltEncoderImpl::CobaltEncoderImpl(
    std::unique_ptr<ProjectContext> project_context, ClientSecret client_secret,
    ObservationStoreDispatcher* store_dispatcher,
    util::EncryptedMessageMaker* encrypt_to_analyzer,
    ShippingDispatcher* shipping_dispatcher, const SystemData* system_data,
    TimerManager* timer_manager)
    : encoder_(std::move(project_context), std::move(client_secret),
               system_data),
      store_dispatcher_(store_dispatcher),
      encrypt_to_analyzer_(encrypt_to_analyzer),
      shipping_dispatcher_(shipping_dispatcher),
      timer_manager_(timer_manager) {}

template <class CB>
void CobaltEncoderImpl::AddEncodedObservation(Encoder::Result* result,
                                              CB callback) {
  switch (result->status) {
    case Encoder::kOK:
      break;
    case Encoder::kInsufficientBuildLevel:
      FXL_LOG(WARNING)
          << "Cobalt metric reporting attempt with insufficient build level";
      callback(Status::OK);
      return;
    case Encoder::kInvalidArguments:
      callback(Status::INVALID_ARGUMENTS);
      return;
    case Encoder::kInvalidConfig:
    case Encoder::kEncodingFailed:
      callback(Status::INTERNAL_ERROR);
      FXL_LOG(WARNING) << "Cobalt internal error: " << result->status;
      return;
  }

  auto message = std::make_unique<EncryptedMessage>();
  if (!encrypt_to_analyzer_->Encrypt(*(result->observation), message.get())) {
    FXL_LOG(WARNING)
        << "Cobalt internal error. Unable to encrypt observations.";
    callback(Status::INTERNAL_ERROR);
  }
  // AddEncryptedObservation returns a StatusOr<ObservationStore::StoreStatus>.
  auto result_or = store_dispatcher_->AddEncryptedObservation(
      std::move(message), std::move(result->metadata));

  // If the StatusOr is not ok(), that means there was no configured store for
  // the metadata's backend.
  if (!result_or.ok()) {
    callback(Status::INTERNAL_ERROR);
  }

  // Unpack the inner StoreStatus and convert it to a cobalt Status.
  Status status = ToCobaltStatus(result_or.ConsumeValueOrDie());
  shipping_dispatcher_->NotifyObservationsAdded();
  callback(status);
}
void CobaltEncoderImpl::AddStringObservation(
    uint32_t metric_id, uint32_t encoding_id, fidl::StringPtr observation,
    AddStringObservationCallback callback) {
  auto result = encoder_.EncodeString(metric_id, encoding_id, observation);
  AddEncodedObservation(&result, std::move(callback));
}

void CobaltEncoderImpl::AddIntObservation(uint32_t metric_id,
                                          uint32_t encoding_id,
                                          const int64_t observation,
                                          AddIntObservationCallback callback) {
  auto result = encoder_.EncodeInt(metric_id, encoding_id, observation);
  AddEncodedObservation(&result, std::move(callback));
}

void CobaltEncoderImpl::AddDoubleObservation(
    uint32_t metric_id, uint32_t encoding_id, const double observation,
    AddDoubleObservationCallback callback) {
  auto result = encoder_.EncodeDouble(metric_id, encoding_id, observation);
  AddEncodedObservation(&result, std::move(callback));
}

void CobaltEncoderImpl::AddIndexObservation(
    uint32_t metric_id, uint32_t encoding_id, uint32_t index,
    AddIndexObservationCallback callback) {
  auto result = encoder_.EncodeIndex(metric_id, encoding_id, index);
  AddEncodedObservation(&result, std::move(callback));
}

void CobaltEncoderImpl::AddObservation(uint32_t metric_id, uint32_t encoding_id,
                                       fuchsia::cobalt::Value observation,
                                       AddObservationCallback callback) {
  switch (observation.Which()) {
    case fuchsia::cobalt::Value::Tag::kStringValue: {
      AddStringObservation(metric_id, encoding_id, observation.string_value(),
                           std::move(callback));
      break;
    }
    case fuchsia::cobalt::Value::Tag::kIntValue: {
      AddIntObservation(metric_id, encoding_id, observation.int_value(),
                        std::move(callback));
      break;
    }
    case fuchsia::cobalt::Value::Tag::kDoubleValue: {
      AddDoubleObservation(metric_id, encoding_id, observation.double_value(),
                           std::move(callback));
      break;
    }
    case fuchsia::cobalt::Value::Tag::kIndexValue: {
      AddIndexObservation(metric_id, encoding_id, observation.index_value(),
                          std::move(callback));
      break;
    }
    case fuchsia::cobalt::Value::Tag::kIntBucketDistribution: {
      AddIntBucketDistribution(metric_id, encoding_id,
                               std::move(observation.int_bucket_distribution()),
                               std::move(callback));
      break;
    }
    default:
      callback(Status::INVALID_ARGUMENTS);
      FXL_LOG(ERROR) << "Cobalt: Unrecognized value type in observation.";
      return;
  }
}

void CobaltEncoderImpl::AddMultipartObservation(
    uint32_t metric_id,
    fidl::VectorPtr<fuchsia::cobalt::ObservationValue> observation,
    AddMultipartObservationCallback callback) {
  Encoder::Value value;
  for (const auto& obs_val : *observation) {
    switch (obs_val.value.Which()) {
      case fuchsia::cobalt::Value::Tag::kStringValue: {
        value.AddStringPart(obs_val.encoding_id, obs_val.name,
                            obs_val.value.string_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kIntValue: {
        value.AddIntPart(obs_val.encoding_id, obs_val.name,
                         obs_val.value.int_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kDoubleValue: {
        value.AddDoublePart(obs_val.encoding_id, obs_val.name,
                            obs_val.value.double_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kIndexValue: {
        value.AddIndexPart(obs_val.encoding_id, obs_val.name,
                           obs_val.value.index_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kIntBucketDistribution: {
        std::map<uint32_t, uint64_t> distribution_map;
        for (auto it = obs_val.value.int_bucket_distribution()->begin();
             obs_val.value.int_bucket_distribution()->end() != it; it++) {
          distribution_map[(*it).index] = (*it).count;
        }
        value.AddIntBucketDistributionPart(obs_val.encoding_id, obs_val.name,
                                           distribution_map);
        break;
      }
      default:
        callback(Status::INVALID_ARGUMENTS);
        FXL_LOG(ERROR)
            << "Cobalt: Unrecognized value type for observation part "
            << obs_val.name;
        return;
    }
  }
  auto result = encoder_.Encode(metric_id, value);
  AddEncodedObservation(&result, std::move(callback));
}

void CobaltEncoderImpl::AddIntBucketDistribution(
    uint32_t metric_id, uint32_t encoding_id,
    fidl::VectorPtr<fuchsia::cobalt::BucketDistributionEntry> distribution,
    AddIntBucketDistributionCallback callback) {
  std::map<uint32_t, uint64_t> distribution_map;
  for (auto it = distribution->begin(); distribution->end() != it; it++) {
    distribution_map[(*it).index] = (*it).count;
  }
  auto result = encoder_.EncodeIntBucketDistribution(metric_id, encoding_id,
                                                     distribution_map);
  AddEncodedObservation(&result, std::move(callback));
}

template <class CB>
void CobaltEncoderImpl::AddTimerObservationIfReady(
    std::unique_ptr<TimerVal> timer_val_ptr, CB callback) {
  if (!TimerManager::isReady(timer_val_ptr)) {
    // TimerManager has not received both StartTimer and EndTimer calls. Return
    // OK status and wait for the other call.
    callback(Status::OK);
    return;
  }

  if (TimerManager::isMultipart(timer_val_ptr)) {
    fuchsia::cobalt::ObservationValue value;
    value.name = std::move(timer_val_ptr->part_name);
    value.encoding_id = timer_val_ptr->encoding_id;
    value.value.set_int_value(timer_val_ptr->end_timestamp -
                              timer_val_ptr->start_timestamp);

    timer_val_ptr->observation.push_back(std::move(value));
    AddMultipartObservation(timer_val_ptr->metric_id,
                            std::move(timer_val_ptr->observation),
                            std::move(callback));
  } else {
    AddIntObservation(
        timer_val_ptr->metric_id, timer_val_ptr->encoding_id,
        timer_val_ptr->end_timestamp - timer_val_ptr->start_timestamp,
        std::move(callback));
  }
}

void CobaltEncoderImpl::StartTimer(uint32_t metric_id, uint32_t encoding_id,
                                   fidl::StringPtr timer_id, uint64_t timestamp,
                                   uint32_t timeout_s,
                                   StartTimerCallback callback) {
  std::unique_ptr<TimerVal> timer_val_ptr;
  auto status = timer_manager_->GetTimerValWithStart(metric_id, encoding_id,
                                                     timer_id.get(), timestamp,
                                                     timeout_s, &timer_val_ptr);

  if (status != Status::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

void CobaltEncoderImpl::EndTimer(fidl::StringPtr timer_id, uint64_t timestamp,
                                 uint32_t timeout_s,
                                 EndTimerCallback callback) {
  std::unique_ptr<TimerVal> timer_val_ptr;
  auto status = timer_manager_->GetTimerValWithEnd(timer_id.get(), timestamp,
                                                   timeout_s, &timer_val_ptr);

  if (status != Status::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

void CobaltEncoderImpl::EndTimerMultiPart(
    fidl::StringPtr timer_id, uint64_t timestamp, fidl::StringPtr part_name,
    fidl::VectorPtr<fuchsia::cobalt::ObservationValue> observation,
    uint32_t timeout_s, EndTimerMultiPartCallback callback) {
  std::unique_ptr<TimerVal> timer_val_ptr;
  auto status = timer_manager_->GetTimerValWithEnd(
      timer_id.get(), timestamp, timeout_s, part_name.get(),
      std::move(observation), &timer_val_ptr);

  if (status != Status::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

void CobaltEncoderImpl::SendObservations(SendObservationsCallback callback) {
  callback(Status::OK);
}

}  // namespace encoder
}  // namespace cobalt
