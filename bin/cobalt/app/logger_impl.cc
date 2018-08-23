// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/logger_impl.h"

#include "garnet/bin/cobalt/app/utils.h"

#include <type_traits>

namespace cobalt {

using cobalt::TimerManager;
using cobalt::TimerVal;
using fuchsia::cobalt::Status2;

namespace {
Status2 ToStatus2(Status s) {
  switch (s) {
    case Status::OK:
      return Status2::OK;
    case Status::INVALID_ARGUMENTS:
      return Status2::INVALID_ARGUMENTS;
    case Status::OBSERVATION_TOO_BIG:
      return Status2::EVENT_TOO_BIG;
    case Status::TEMPORARILY_FULL:
      return Status2::BUFFER_FULL;
    default:
      return Status2::INTERNAL_ERROR;
  }
}

// Returns a tuple of the names of the three MetricParts used to report a
// Metric with at most one int/float part, one string part and one index part.
// The 0th item will be the name of the int/float part, the 1st item will be the
// name of the string part for the component name, and the 2nd item will be the
// name of the index part that is for the event type index. If the metric is not
// found or the MetricParts do not fit the expected types, a tuple with empty
// strings will be returned.
std::tuple<std::string, std::string, std::string> ThreePartMetricPartNames(
    const Metric* metric) {
  if (!metric || metric->parts_size() > 3) {
    return std::make_tuple("", "", "");
  }
  std::string number_part, component_name_part, index_part;
  for (auto pair : metric->parts()) {
    switch (pair.second.data_type()) {
      case MetricPart::INT:
      case MetricPart::DOUBLE: {
        if (!number_part.empty()) {
          return std::make_tuple("", "", "");
        }
        number_part = pair.first;
        break;
      }
      case MetricPart::STRING: {
        if (!component_name_part.empty()) {
          return std::make_tuple("", "", "");
        }
        component_name_part = pair.first;
        break;
      }
      case MetricPart::INDEX: {
        if (!index_part.empty()) {
          return std::make_tuple("", "", "");
        }
        index_part = pair.first;
        break;
      }
      default:
        return std::make_tuple("", "", "");
    }
  }
  return std::make_tuple(number_part, component_name_part, index_part);
}
}  // namespace

LoggerImpl::LoggerImpl(std::unique_ptr<encoder::ProjectContext> project_context,
                       encoder::ClientSecret client_secret,
                       encoder::ObservationStoreDispatcher* store_dispatcher,
                       util::EncryptedMessageMaker* encrypt_to_analyzer,
                       encoder::ShippingDispatcher* shipping_dispatcher,
                       const encoder::SystemData* system_data,
                       TimerManager* timer_manager)
    : encoder_(std::move(project_context), std::move(client_secret),
               system_data),
      store_dispatcher_(store_dispatcher),
      encrypt_to_analyzer_(encrypt_to_analyzer),
      shipping_dispatcher_(shipping_dispatcher),
      timer_manager_(timer_manager) {}

template <class ValueType, class CB>
void LoggerImpl::LogThreePartMetric(const std::string& value_part_name,
                                    fidl::StringPtr metric_name,
                                    uint32_t event_type_index,
                                    fidl::StringPtr component, ValueType value,
                                    CB callback, bool value_part_required) {
  uint32_t metric_id = encoder_.MetricId(metric_name);
  if (metric_id == 0) {
    FXL_LOG(ERROR) << "Metric " << metric_name << " does not exist.";
    callback(Status2::INVALID_ARGUMENTS);
    return;
  }

  auto encodings = encoder_.DefaultEncodingsForMetric(metric_id);

  std::string value_part, component_name_part, index_part;
  std::tie(value_part, component_name_part, index_part) =
      ThreePartMetricPartNames(encoder_.GetMetric(metric_id));
  cobalt::encoder::Encoder::Value new_value;

  // LogElapsedTime, LogFrameRate and LogMemoryUsage can be logged to a metric
  // with just a single part while LogEventCount cannot (the user should use
  // LogEvent instead)
  if (encodings.size() == 1 && value_part_required) {
    if (event_type_index != 0 || !component->empty()) {
      FXL_LOG(ERROR)
          << "Metric " << metric_name << " is a single part metric so only "
          << value_part_name
          << " must be provided "
             "(event_type_index must be 0 and component must be empty).";
      callback(Status2::INVALID_ARGUMENTS);
      return;
    }
    if (std::is_same<ValueType, int64_t>::value) {
      new_value.AddIntPart(encodings.begin()->second, "", value);
    } else if (std::is_same<ValueType, float>::value) {
      new_value.AddDoublePart(encodings.begin()->second, "", value);
    }
  } else if (encodings.size() == 2 || encodings.size() == 3) {
    if (!value_part.empty()) {
      if (std::is_same<ValueType, int64_t>::value ||
          std::is_same<ValueType, uint32_t>::value) {
        new_value.AddIntPart(encodings[value_part], value_part, value);
      } else if (std::is_same<ValueType, float>::value) {
        new_value.AddDoublePart(encodings[value_part], value_part, value);
      }
    } else if (value_part_required) {
      FXL_LOG(ERROR) << "Metric " << metric_name
                     << " must have a numeric part to be a valid "
                     << value_part_name << " metric.";
      callback(Status2::INVALID_ARGUMENTS);
      return;
    }

    if (!component_name_part.empty()) {
      new_value.AddStringPart(encodings[component_name_part],
                              component_name_part, component);
    } else if (component_name_part.empty() && !component->empty()) {
      FXL_LOG(ERROR) << "Metric " << metric_name
                     << " is a two part metric with no string part so "
                        "component must be empty";
      callback(Status2::INVALID_ARGUMENTS);
      return;
    }

    if (!index_part.empty()) {
      new_value.AddIndexPart(encodings[index_part], index_part,
                             event_type_index);
    } else if (index_part.empty() && event_type_index != 0) {
      FXL_LOG(ERROR) << "Metric " << metric_name
                     << " is a two part metric with no index part so "
                        "event_type_index must be 0";
      callback(Status2::INVALID_ARGUMENTS);
      return;
    }
  } else {
    FXL_LOG(ERROR) << "Metric " << metric_name << " is not a valid "
                   << value_part_name << " metric.";
    callback(Status2::INVALID_ARGUMENTS);
    return;
  }

  auto result = encoder_.Encode(metric_id, new_value);
  AddEncodedObservation(&result, std::move(callback));
}

// Duplicated from cobalt_encoder_impl.cc
template <class CB>
void LoggerImpl::AddEncodedObservation(cobalt::encoder::Encoder::Result* result,
                                       CB callback) {
  switch (result->status) {
    case cobalt::encoder::Encoder::kOK:
      break;
    case cobalt::encoder::Encoder::kInsufficientBuildLevel:
      FXL_LOG(WARNING)
          << "Cobalt metric reporting attempt with insufficient build level";
      callback(Status2::OK);
      return;
    case cobalt::encoder::Encoder::kInvalidArguments:
      callback(Status2::INVALID_ARGUMENTS);
      return;
    case cobalt::encoder::Encoder::kInvalidConfig:
    case cobalt::encoder::Encoder::kEncodingFailed:
      callback(Status2::INTERNAL_ERROR);
      FXL_LOG(WARNING) << "Cobalt internal error: " << result->status;
      return;
  }

  auto message = std::make_unique<EncryptedMessage>();
  if (!encrypt_to_analyzer_->Encrypt(*(result->observation), message.get())) {
    FXL_LOG(WARNING)
        << "Cobalt internal error. Unable to encrypt observations.";
    callback(Status2::INTERNAL_ERROR);
  }
  // AddEncryptedObservation returns a StatusOr<ObservationStore::StoreStatus>.
  auto result_or = store_dispatcher_->AddEncryptedObservation(
      std::move(message), std::move(result->metadata));

  // If the StatusOr is not ok(), that means there was no configured store for
  // the metadata's backend.
  if (!result_or.ok()) {
    callback(Status2::INTERNAL_ERROR);
  }

  // Unpack the inner StoreStatus and convert it to a cobalt Status.
  Status2 status = ToStatus2(ToCobaltStatus(result_or.ConsumeValueOrDie()));
  shipping_dispatcher_->NotifyObservationsAdded();
  callback(status);
}

bool LoggerImpl::GetSinglePartMetricInfo(const fidl::StringPtr& metric_name,
                                         uint32_t* metric_id,
                                         uint32_t* encoding_id) {
  *metric_id = encoder_.MetricId(metric_name);
  if (*metric_id == 0) {
    FXL_LOG(ERROR) << "Metric " << metric_name << " does not exist.";
    return false;
  }
  auto encodings = encoder_.DefaultEncodingsForMetric(*metric_id);
  if (encodings.size() != 1) {
    FXL_LOG(ERROR) << "Expected Metric " << *metric_id
                   << " to only have a single part.";
    return false;
  }

  *encoding_id = encodings.begin()->second;
  return true;
}

void LoggerImpl::LogEvent(fidl::StringPtr metric_name,
                          uint32_t event_type_index,
                          LogEventCallback callback) {
  uint32_t metric_id, encoding_id;
  if (!GetSinglePartMetricInfo(metric_name, &metric_id, &encoding_id)) {
    callback(Status2::INVALID_ARGUMENTS);
    return;
  }

  auto result = encoder_.EncodeIndex(metric_id, encoding_id, event_type_index);
  AddEncodedObservation(&result, std::move(callback));
}

void LoggerImpl::LogEventCount(fidl::StringPtr metric_name,
                               uint32_t event_type_index,
                               fidl::StringPtr component,
                               int64_t period_duration_micros, uint32_t count,
                               LogEventCountCallback callback) {
  LogThreePartMetric("event count", metric_name, event_type_index, component,
                     count, std::move(callback), false);
}

void LoggerImpl::LogElapsedTime(fidl::StringPtr metric_name,
                                uint32_t event_type_index,
                                fidl::StringPtr component,
                                int64_t elapsed_micros,
                                LogElapsedTimeCallback callback) {
  LogThreePartMetric("elapsed time", metric_name, event_type_index, component,
                     elapsed_micros, std::move(callback), true);
}

void LoggerImpl::LogFrameRate(fidl::StringPtr metric_name,
                              uint32_t event_type_index,
                              fidl::StringPtr component, float fps,
                              LogFrameRateCallback callback) {
  LogThreePartMetric("frame rate", metric_name, event_type_index, component,
                     fps, std::move(callback), true);
}

void LoggerImpl::LogMemoryUsage(fidl::StringPtr metric_name,
                                uint32_t event_type_index,
                                fidl::StringPtr component, int64_t bytes,
                                LogMemoryUsageCallback callback) {
  LogThreePartMetric("memory usage", metric_name, event_type_index, component,
                     bytes, std::move(callback), true);
}

void LoggerImpl::LogString(fidl::StringPtr metric_name, fidl::StringPtr s,
                           LogStringCallback callback) {
  uint32_t metric_id, encoding_id;
  if (!GetSinglePartMetricInfo(metric_name, &metric_id, &encoding_id)) {
    callback(Status2::INVALID_ARGUMENTS);
  }

  auto result = encoder_.EncodeString(metric_id, encoding_id, s);
  AddEncodedObservation(&result, std::move(callback));
}

template <class CB>
void LoggerImpl::AddTimerObservationIfReady(
    std::unique_ptr<TimerVal> timer_val_ptr, CB callback) {
  if (!TimerManager::isReady(timer_val_ptr)) {
    // TimerManager has not received both StartTimer and EndTimer calls. Return
    // OK status and wait for the other call.
    callback(Status2::OK);
    return;
  }

  auto result = encoder_.EncodeInt(
      timer_val_ptr->metric_id, timer_val_ptr->encoding_id,
      timer_val_ptr->end_timestamp - timer_val_ptr->start_timestamp);
  AddEncodedObservation(&result, std::move(callback));
}

void LoggerImpl::StartTimer(fidl::StringPtr metric_name,
                            uint32_t event_type_index,
                            fidl::StringPtr component, fidl::StringPtr timer_id,
                            uint64_t timestamp, uint32_t timeout_s,
                            StartTimerCallback callback) {
  if (event_type_index != 0 || !component->empty()) {
    FXL_LOG(ERROR) << "event_type_index and component are not currently "
                      "consumed. Pass in 0 and empty string respectively.";
    callback(Status2::INVALID_ARGUMENTS);
  }
  std::unique_ptr<TimerVal> timer_val_ptr;
  uint32_t metric_id, encoding_id;
  if (!GetSinglePartMetricInfo(metric_name, &metric_id, &encoding_id)) {
    callback(Status2::INVALID_ARGUMENTS);
  }
  auto status = ToStatus2(timer_manager_->GetTimerValWithStart(
      metric_id, encoding_id, timer_id.get(), timestamp, timeout_s,
      &timer_val_ptr));

  if (status != Status2::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

void LoggerImpl::EndTimer(fidl::StringPtr timer_id, uint64_t timestamp,
                          uint32_t timeout_s, EndTimerCallback callback) {
  std::unique_ptr<TimerVal> timer_val_ptr;
  auto status = ToStatus2(timer_manager_->GetTimerValWithEnd(
      timer_id.get(), timestamp, timeout_s, &timer_val_ptr));

  if (status != Status2::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

void LoggerExtImpl::LogIntHistogram(
    fidl::StringPtr metric_name, uint32_t event_type_index,
    fidl::StringPtr component,
    fidl::VectorPtr<fuchsia::cobalt::HistogramBucket> histogram,
    LogIntHistogramCallback callback) {
  FXL_LOG(ERROR) << "Not yet implemented";
  callback(Status2::INTERNAL_ERROR);
}

void LoggerExtImpl::LogCustomEvent(
    fidl::StringPtr metric_name,
    fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> event_values,
    LogCustomEventCallback callback) {
  uint32_t metric_id = encoder_.MetricId(metric_name);
  auto encodings = encoder_.DefaultEncodingsForMetric(metric_id);
  cobalt::encoder::Encoder::Value value;
  for (const auto& event_val : *event_values) {
    switch (event_val.value.Which()) {
      case fuchsia::cobalt::Value::Tag::kStringValue: {
        value.AddStringPart(encodings[event_val.dimension_name],
                            event_val.dimension_name,
                            event_val.value.string_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kIntValue: {
        value.AddIntPart(encodings[event_val.dimension_name],
                         event_val.dimension_name, event_val.value.int_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kDoubleValue: {
        value.AddDoublePart(encodings[event_val.dimension_name],
                            event_val.dimension_name,
                            event_val.value.double_value());
        break;
      }
      case fuchsia::cobalt::Value::Tag::kIndexValue: {
        value.AddIndexPart(encodings[event_val.dimension_name],
                           event_val.dimension_name,
                           event_val.value.index_value());
        break;
      }
      default:
        callback(Status2::INVALID_ARGUMENTS);
        FXL_LOG(ERROR)
            << "Cobalt: Unrecognized value type for observation part "
            << event_val.dimension_name;
        return;
    }
  }
  auto result = encoder_.Encode(metric_id, value);
  AddEncodedObservation(&result, std::move(callback));
}

void LoggerSimpleImpl::LogIntHistogram(fidl::StringPtr metric_name,
                                       uint32_t event_type_index,
                                       fidl::StringPtr component,
                                       fidl::VectorPtr<uint32_t> bucket_indices,
                                       fidl::VectorPtr<uint64_t> bucket_counts,
                                       LogIntHistogramCallback callback) {
  FXL_LOG(ERROR) << "Not yet implemented";
  callback(Status2::INTERNAL_ERROR);
}

void LoggerSimpleImpl::LogCustomEvent(fidl::StringPtr metric_name,
                                      fidl::StringPtr json_string,
                                      LogCustomEventCallback callback) {
  FXL_LOG(ERROR) << "Not yet implemented";
  callback(Status2::INTERNAL_ERROR);
}

}  // namespace cobalt
