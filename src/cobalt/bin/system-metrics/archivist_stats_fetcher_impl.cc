// Copyright 2020  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/archivist_stats_fetcher_impl.h"

#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/cobalt/bin/system-metrics/diagnostics_metrics_registry.cb.h"

namespace cobalt {

namespace {
constexpr const char kAllSelector[] = "core/archivist:root/all_archive_accessor:*";
}

ArchivistStatsFetcherImpl::ArchivistStatsFetcherImpl(async_dispatcher_t* dispatcher,
                                                     sys::ComponentContext* context)
    : ArchivistStatsFetcherImpl(dispatcher, [context] {
        return context->svc()->Connect<fuchsia::diagnostics::ArchiveAccessor>();
      }) {}

ArchivistStatsFetcherImpl::ArchivistStatsFetcherImpl(
    async_dispatcher_t* dispatcher,
    fit::function<fuchsia::diagnostics::ArchiveAccessorPtr()> connector)
    : executor_(dispatcher), connector_(std::move(connector)) {}

void ArchivistStatsFetcherImpl::FetchMetrics(MetricsCallback metrics_callback) {
  std::vector<std::string> selectors = {kAllSelector};
  auto reader =
      std::make_shared<inspect::contrib::ArchiveReader>(connector_(), std::move(selectors));
  executor_.schedule_task(reader->GetInspectSnapshot().then(
      [this, reader, metrics_callback = std::move(metrics_callback)](
          fit::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>& results) {
        if (!results.is_ok()) {
          FX_LOGS(ERROR) << "Failed to fetch data for archivist: " << results.error();
          return;
        }

        if (results.value().size() != 1) {
          FX_LOGS(ERROR) << "Expected one result, found " << results.value().size();
          return;
        }

        const rapidjson::Value& node =
            results.value()[0].GetByPath({"root", "all_archive_accessor"});
        if (node.IsNull() || !node.IsObject()) {
          FX_LOGS(ERROR) << "Could not find object at root/all_archive_accessor";
          return;
        }

        for (auto it = node.MemberBegin(); it != node.MemberEnd(); ++it) {
          if (!it->value.IsUint64()) {
            continue;
          }
          if (strcmp(it->name.GetString(), "inspect_batch_iterator_get_next_requests") == 0) {
            ProcessNewValue(
                MeasurementKey(
                    fuchsia_component_diagnostics::kBatchIteratorGetNextRequestsMetricId,
                    fuchsia_component_diagnostics::BatchIteratorGetNextRequestsEventCodes{
                        .pipeline = fuchsia_component_diagnostics::
                            BatchIteratorGetNextRequestsMetricDimensionPipeline::All,
                        .data_type = fuchsia_component_diagnostics::
                            BatchIteratorGetNextRequestsMetricDimensionDataType::Inspect}
                        .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(), "inspect_batch_iterator_get_next_errors") == 0) {
            ProcessNewValue(
                MeasurementKey(fuchsia_component_diagnostics::kBatchIteratorGetNextErrorsMetricId,
                               fuchsia_component_diagnostics::BatchIteratorGetNextErrorsEventCodes{
                                   .pipeline = fuchsia_component_diagnostics::
                                       BatchIteratorGetNextErrorsMetricDimensionPipeline::All,
                                   .data_type = fuchsia_component_diagnostics::
                                       BatchIteratorGetNextErrorsMetricDimensionDataType::Inspect}
                                   .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(), "inspect_batch_iterator_get_next_result_count") ==
                     0) {
            ProcessNewValue(
                MeasurementKey(
                    fuchsia_component_diagnostics::kBatchIteratorGetNextResultCountMetricId,
                    fuchsia_component_diagnostics::BatchIteratorGetNextResultCountEventCodes{
                        .pipeline = fuchsia_component_diagnostics::
                            BatchIteratorGetNextResultCountMetricDimensionPipeline::All,
                        .data_type = fuchsia_component_diagnostics::
                            BatchIteratorGetNextResultCountMetricDimensionDataType::Inspect}
                        .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(),
                            "inspect_batch_iterator_get_next_result_errors") == 0) {
            ProcessNewValue(
                MeasurementKey(
                    fuchsia_component_diagnostics::kBatchIteratorGetNextResultErrorsMetricId,
                    fuchsia_component_diagnostics::BatchIteratorGetNextResultErrorsEventCodes{
                        .pipeline = fuchsia_component_diagnostics::
                            BatchIteratorGetNextResultErrorsMetricDimensionPipeline::All,
                        .data_type = fuchsia_component_diagnostics::
                            BatchIteratorGetNextResultErrorsMetricDimensionDataType::Inspect}
                        .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(), "inspect_component_timeouts_count") == 0) {
            ProcessNewValue(
                MeasurementKey(fuchsia_component_diagnostics::kComponentTimeoutsCountMetricId,
                               fuchsia_component_diagnostics::ComponentTimeoutsCountEventCodes{
                                   .pipeline = fuchsia_component_diagnostics::
                                       ComponentTimeoutsCountMetricDimensionPipeline::All,
                                   .data_type = fuchsia_component_diagnostics::
                                       ComponentTimeoutsCountMetricDimensionDataType::Inspect}
                                   .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(), "archive_accessor_connections_opened") == 0) {
            ProcessNewValue(
                MeasurementKey(
                    fuchsia_component_diagnostics::kArchiveAccessorConnectionsOpenedMetricId,
                    {fuchsia_component_diagnostics::
                         ArchiveAccessorConnectionsOpenedMetricDimensionPipeline::All}),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(), "lifecycle_batch_iterator_get_next_requests") ==
                     0) {
            ProcessNewValue(
                MeasurementKey(
                    fuchsia_component_diagnostics::kBatchIteratorGetNextRequestsMetricId,
                    fuchsia_component_diagnostics::BatchIteratorGetNextRequestsEventCodes{
                        .pipeline = fuchsia_component_diagnostics::
                            BatchIteratorGetNextRequestsMetricDimensionPipeline::All,
                        .data_type = fuchsia_component_diagnostics::
                            BatchIteratorGetNextRequestsMetricDimensionDataType::Lifecycle}
                        .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(), "lifecycle_batch_iterator_get_next_errors") ==
                     0) {
            ProcessNewValue(
                MeasurementKey(fuchsia_component_diagnostics::kBatchIteratorGetNextErrorsMetricId,
                               fuchsia_component_diagnostics::BatchIteratorGetNextErrorsEventCodes{
                                   .pipeline = fuchsia_component_diagnostics::
                                       BatchIteratorGetNextErrorsMetricDimensionPipeline::All,
                                   .data_type = fuchsia_component_diagnostics::
                                       BatchIteratorGetNextErrorsMetricDimensionDataType::Lifecycle}
                                   .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(),
                            "lifecycle_batch_iterator_get_next_result_count") == 0) {
            ProcessNewValue(
                MeasurementKey(
                    fuchsia_component_diagnostics::kBatchIteratorGetNextResultCountMetricId,
                    fuchsia_component_diagnostics::BatchIteratorGetNextResultCountEventCodes{
                        .pipeline = fuchsia_component_diagnostics::
                            BatchIteratorGetNextResultCountMetricDimensionPipeline::All,
                        .data_type = fuchsia_component_diagnostics::
                            BatchIteratorGetNextResultCountMetricDimensionDataType::Lifecycle}
                        .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(),
                            "lifecycle_batch_iterator_get_next_result_errors") == 0) {
            ProcessNewValue(
                MeasurementKey(
                    fuchsia_component_diagnostics::kBatchIteratorGetNextResultErrorsMetricId,
                    fuchsia_component_diagnostics::BatchIteratorGetNextResultErrorsEventCodes{
                        .pipeline = fuchsia_component_diagnostics::
                            BatchIteratorGetNextResultErrorsMetricDimensionPipeline::All,
                        .data_type = fuchsia_component_diagnostics::
                            BatchIteratorGetNextResultErrorsMetricDimensionDataType::Lifecycle}
                        .ToVector()),
                it->value.GetUint64(), metrics_callback);
          } else if (strcmp(it->name.GetString(), "lifecycle_component_timeouts_count") == 0) {
            ProcessNewValue(
                MeasurementKey(fuchsia_component_diagnostics::kComponentTimeoutsCountMetricId,
                               fuchsia_component_diagnostics::ComponentTimeoutsCountEventCodes{
                                   .pipeline = fuchsia_component_diagnostics::
                                       ComponentTimeoutsCountMetricDimensionPipeline::All,
                                   .data_type = fuchsia_component_diagnostics::
                                       ComponentTimeoutsCountMetricDimensionDataType::Lifecycle}
                                   .ToVector()),
                it->value.GetUint64(), metrics_callback);
          }
        }
      }));
}

void ArchivistStatsFetcherImpl::ProcessNewValue(MeasurementKey key, MeasurementValue value,
                                                const MetricsCallback& callback) {
  Measurement diff = std::make_pair(key, GetDifferenceForMetric(key, value));
  if (diff.second > 0 && callback(diff)) {
    UpdatePreviousValue(key, value);
  }
}

ArchivistStatsFetcherImpl::MeasurementValue ArchivistStatsFetcherImpl::GetDifferenceForMetric(
    const MeasurementKey& key, MeasurementValue value) {
  auto it = previous_measurements_.find(key);
  MeasurementValue prev = 0;
  if (it != previous_measurements_.end()) {
    prev = it->second;
  }

  if (value > prev) {
    return value - prev;
  } else {
    // Prevent underflow.
    return 0;
  }
}

void ArchivistStatsFetcherImpl::UpdatePreviousValue(const MeasurementKey& key,
                                                    MeasurementValue value) {
  auto it = previous_measurements_.find(key);
  if (it != previous_measurements_.end()) {
    it->second = value;
  } else {
    previous_measurements_.insert(std::make_pair(key, value));
  }
}

}  // namespace cobalt
