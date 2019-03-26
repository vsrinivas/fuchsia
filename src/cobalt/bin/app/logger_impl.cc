#include "src/cobalt/bin/app/logger_impl.h"

#include "src/cobalt/bin/app/utils.h"

namespace cobalt {

LoggerImpl::LoggerImpl(std::unique_ptr<logger::ProjectContext> project_context,
                       logger::Encoder* encoder,
                       logger::EventAggregator* event_aggregator,
                       logger::ObservationWriter* observation_writer,
                       TimerManager* timer_manager,
                       logger::LoggerInterface* internal_logger)
    : logger_(std::move(project_context), encoder, event_aggregator,
              observation_writer, internal_logger),
      timer_manager_(timer_manager) {}

void LoggerImpl::LogEvent(
    uint32_t metric_id, uint32_t event_code,
    fuchsia::cobalt::LoggerBase::LogEventCallback callback) {
  callback(ToCobaltStatus(logger_.LogEvent(metric_id, event_code)));
}

void LoggerImpl::LogEventCount(
    uint32_t metric_id, uint32_t event_code, std::string component,
    int64_t period_duration_micros, int64_t count,
    fuchsia::cobalt::LoggerBase::LogEventCountCallback callback) {
  callback(ToCobaltStatus(logger_.LogEventCount(
      metric_id, event_code, component, period_duration_micros, count)));
}

void LoggerImpl::LogElapsedTime(
    uint32_t metric_id, uint32_t event_code, std::string component,
    int64_t elapsed_micros,
    fuchsia::cobalt::LoggerBase::LogElapsedTimeCallback callback) {
  callback(ToCobaltStatus(logger_.LogElapsedTime(metric_id, event_code,
                                                 component, elapsed_micros)));
}

void LoggerImpl::LogFrameRate(
    uint32_t metric_id, uint32_t event_code, std::string component, float fps,
    fuchsia::cobalt::LoggerBase::LogFrameRateCallback callback) {
  callback(ToCobaltStatus(
      logger_.LogFrameRate(metric_id, event_code, component, fps)));
}

void LoggerImpl::LogMemoryUsage(
    uint32_t metric_id, uint32_t event_code, std::string component,
    int64_t bytes,
    fuchsia::cobalt::LoggerBase::LogMemoryUsageCallback callback) {
  callback(ToCobaltStatus(
      logger_.LogMemoryUsage(metric_id, event_code, component, bytes)));
}

void LoggerImpl::LogString(
    uint32_t metric_id, std::string s,
    fuchsia::cobalt::LoggerBase::LogStringCallback callback) {
  callback(ToCobaltStatus(logger_.LogString(metric_id, s)));
}

void LoggerImpl::LogIntHistogram(
    uint32_t metric_id, uint32_t event_code, std::string component,
    std::vector<fuchsia::cobalt::HistogramBucket> histogram,
    fuchsia::cobalt::Logger::LogIntHistogramCallback callback) {
  logger::HistogramPtr histogram_ptr(
      new google::protobuf::RepeatedPtrField<HistogramBucket>());
  for (auto it = histogram.begin(); histogram.end() != it; it++) {
    auto bucket = histogram_ptr->Add();
    bucket->set_index((*it).index);
    bucket->set_count((*it).count);
  }
  callback(ToCobaltStatus(logger_.LogIntHistogram(
      metric_id, event_code, component, std::move(histogram_ptr))));
}

void LoggerImpl::LogIntHistogram(
    uint32_t metric_id, uint32_t event_code, std::string component,
    std::vector<uint32_t> bucket_indices, std::vector<uint64_t> bucket_counts,
    fuchsia::cobalt::LoggerSimple::LogIntHistogramCallback callback) {
  if (bucket_indices.size() != bucket_counts.size()) {
    FXL_LOG(ERROR) << "[" << metric_id
                   << "]: bucket_indices.size() != bucket_counts.size().";
    callback(Status::INVALID_ARGUMENTS);
    return;
  }
  logger::HistogramPtr histogram_ptr(
      new google::protobuf::RepeatedPtrField<HistogramBucket>());
  for (auto i = 0; i < bucket_indices.size(); i++) {
    auto bucket = histogram_ptr->Add();
    bucket->set_index(bucket_indices.at(i));
    bucket->set_count(bucket_counts.at(i));
  }

  callback(ToCobaltStatus(logger_.LogIntHistogram(
      metric_id, event_code, component, std::move(histogram_ptr))));
}

void LoggerImpl::LogCustomEvent(
    uint32_t metric_id,
    std::vector<fuchsia::cobalt::CustomEventValue> event_values,
    fuchsia::cobalt::Logger::LogCustomEventCallback callback) {
  logger::EventValuesPtr inner_event_values(
      new google::protobuf::Map<std::string, CustomDimensionValue>());
  for (auto it = event_values.begin(); event_values.end() != it; it++) {
    CustomDimensionValue value;
    if (it->value.is_string_value()) {
      value.set_string_value(it->value.string_value());
    } else if (it->value.is_int_value()) {
      value.set_int_value(it->value.int_value());
    } else if (it->value.is_double_value()) {
      value.set_double_value(it->value.double_value());
    } else if (it->value.is_index_value()) {
      value.set_index_value(it->value.index_value());
    }

    auto pair = google::protobuf::MapPair(it->dimension_name, value);
    inner_event_values->insert(pair);
  }
  callback(ToCobaltStatus(
      logger_.LogCustomEvent(metric_id, std::move(inner_event_values))));
}

template <class CB>
void LoggerImpl::AddTimerObservationIfReady(
    std::unique_ptr<TimerVal> timer_val_ptr, CB callback) {
  if (!TimerManager::isReady(timer_val_ptr)) {
    // TimerManager has not received both StartTimer and EndTimer calls. Return
    // OK status and wait for the other call.
    callback(Status::OK);
    return;
  }

  callback(ToCobaltStatus(logger_.LogElapsedTime(
      timer_val_ptr->metric_id, timer_val_ptr->event_code,
      timer_val_ptr->component,
      timer_val_ptr->end_timestamp - timer_val_ptr->start_timestamp)));
}

void LoggerImpl::StartTimer(
    uint32_t metric_id, uint32_t event_code, std::string component,
    std::string timer_id, uint64_t timestamp, uint32_t timeout_s,
    fuchsia::cobalt::LoggerBase::StartTimerCallback callback) {
  std::unique_ptr<TimerVal> timer_val_ptr;
  auto status = timer_manager_->GetTimerValWithStart(
      metric_id, event_code, component, 0, timer_id, timestamp, timeout_s,
      &timer_val_ptr);

  if (status != Status::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

void LoggerImpl::EndTimer(
    std::string timer_id, uint64_t timestamp, uint32_t timeout_s,
    fuchsia::cobalt::LoggerBase::EndTimerCallback callback) {
  std::unique_ptr<TimerVal> timer_val_ptr;
  auto status = timer_manager_->GetTimerValWithEnd(timer_id, timestamp,
                                                   timeout_s, &timer_val_ptr);

  if (status != Status::OK) {
    callback(status);
    return;
  }

  AddTimerObservationIfReady(std::move(timer_val_ptr), std::move(callback));
}

using fuchsia::cobalt::EventPayload;
void LoggerImpl::LogCobaltEvent(
    fuchsia::cobalt::CobaltEvent event,
    fuchsia::cobalt::Logger::LogCobaltEventCallback callback) {
  switch (event.payload.Which()) {
    case EventPayload::Tag::kEventCount:
      callback(ToCobaltStatus(logger_.LogEventCount(
          event.metric_id, event.event_codes, event.component,
          event.payload.event_count().period_duration_micros,
          event.payload.event_count().count)));
      return;

    case EventPayload::Tag::kElapsedMicros:
      callback(ToCobaltStatus(logger_.LogElapsedTime(
          event.metric_id, event.event_codes, event.component,
          event.payload.elapsed_micros())));
      return;

    case EventPayload::Tag::kFps:
      callback(ToCobaltStatus(
          logger_.LogFrameRate(event.metric_id, event.event_codes,
                               event.component, event.payload.fps())));
      return;

    case EventPayload::Tag::kMemoryBytesUsed:
      callback(ToCobaltStatus(logger_.LogMemoryUsage(
          event.metric_id, event.event_codes, event.component,
          event.payload.memory_bytes_used())));
      return;

    case EventPayload::Tag::kStringEvent:
      callback(ToCobaltStatus(
          logger_.LogString(event.metric_id, event.payload.string_event())));
      return;

    case EventPayload::Tag::kIntHistogram: {
      auto histogram = std::move(event.payload.int_histogram());
      logger::HistogramPtr histogram_ptr(
          new google::protobuf::RepeatedPtrField<HistogramBucket>());
      for (auto it = histogram.begin(); histogram.end() != it; it++) {
        auto bucket = histogram_ptr->Add();
        bucket->set_index((*it).index);
        bucket->set_count((*it).count);
      }
      callback(ToCobaltStatus(
          logger_.LogIntHistogram(event.metric_id, event.event_codes,
                                  event.component, std::move(histogram_ptr))));
      return;
    }

    default:
      callback(Status::INVALID_ARGUMENTS);
      return;
  }
}

void LoggerImpl::LogCobaltEvents(
    std::vector<fuchsia::cobalt::CobaltEvent> events,
    fuchsia::cobalt::Logger::LogCobaltEventCallback callback) {
  auto failures = 0;

  auto end = std::make_move_iterator(events.end());

  for (auto it = std::make_move_iterator(events.begin()); it != end; it++) {
    LogCobaltEvent(std::move(*it), [failures](Status status) mutable {
      if (status != Status::OK) {
        failures += 1;
      }
    });
  }

  if (failures == 0) {
    callback(Status::OK);
  } else {
    callback(Status::INTERNAL_ERROR);
  }
}

}  // namespace cobalt
