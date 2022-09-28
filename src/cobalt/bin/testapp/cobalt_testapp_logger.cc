// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <map>
#include <string>

#include <rapidjson/document.h>
#include <src/lib/fsl/vmo/strings.h>

#include "src/cobalt/bin/utils/status_utils.h"

namespace cobalt::testapp {

using ::cobalt::StatusToString;

bool CobaltTestAppLogger::LogOccurrence(uint32_t metric_id, std::vector<uint32_t> indices,
                                        uint64_t count, ExperimentArm arm) {
  fuchsia::metrics::MetricEventLogger_LogOccurrence_Result result;
  fuchsia::metrics::MetricEventLoggerSyncPtr* metric_event_logger;
  switch (arm) {
    case kExperiment:
      metric_event_logger = &experimental_metric_event_logger_;
      break;
    case kControl:
      metric_event_logger = &control_metric_event_logger_;
      break;
    default:
      metric_event_logger = &metric_event_logger_;
  };
  (*metric_event_logger)->LogOccurrence(metric_id, count, indices, &result);
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogOccurrence() => " << ResultToString(std::move(result));
    return false;
  }
  FX_VLOGS(1) << "LogOccurrence(" << count << ") => OK";
  return true;
}

bool CobaltTestAppLogger::LogInteger(uint32_t metric_id, std::vector<uint32_t> indices,
                                     int64_t value) {
  fuchsia::metrics::MetricEventLogger_LogInteger_Result result;
  metric_event_logger_->LogInteger(metric_id, value, indices, &result);
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogInteger() => " << ResultToString(std::move(result));
    return false;
  }
  FX_VLOGS(1) << "LogInteger(" << value << ") => OK";
  return true;
}

bool CobaltTestAppLogger::LogIntegerHistogram(uint32_t metric_id, std::vector<uint32_t> indices,
                                              const std::map<uint32_t, uint64_t>& histogram_map) {
  fuchsia::metrics::MetricEventLogger_LogIntegerHistogram_Result result;
  std::vector<fuchsia::metrics::HistogramBucket> histogram;
  for (auto it = histogram_map.begin(); histogram_map.end() != it; it++) {
    fuchsia::metrics::HistogramBucket entry;
    entry.index = it->first;
    entry.count = it->second;
    histogram.push_back(std::move(entry));
  }

  metric_event_logger_->LogIntegerHistogram(metric_id, std::move(histogram), indices, &result);
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogString() => " << ResultToString(std::move(result));
    return false;
  }
  FX_VLOGS(1) << "LogIntegerHistogram() => OK";
  return true;
}

bool CobaltTestAppLogger::LogString(uint32_t metric_id, std::vector<uint32_t> indices,
                                    const std::string& string_value) {
  fuchsia::metrics::MetricEventLogger_LogString_Result result;
  metric_event_logger_->LogString(metric_id, string_value, indices, &result);
  if (result.is_err()) {
    FX_LOGS(ERROR) << "LogString() => " << ResultToString(std::move(result));
    return false;
  }
  FX_VLOGS(1) << "LogString(" << string_value << ") => OK";

  return true;
}

bool CobaltTestAppLogger::CheckForSuccessfulSend() {
  if (!use_network_) {
    FX_LOGS(INFO) << "Not using the network because --no_network_for_testing "
                     "was passed.";
    return true;
  }

  bool send_success = false;
  FX_VLOGS(1) << "Invoking RequestSendSoon() now...";
  (*cobalt_controller_)->RequestSendSoon(&send_success);
  FX_VLOGS(1) << "RequestSendSoon => " << send_success;
  return send_success;
}

std::string CobaltTestAppLogger::GetInspectJson() const {
  fuchsia::diagnostics::BatchIteratorSyncPtr iterator;
  fuchsia::diagnostics::StreamParameters stream_parameters;
  stream_parameters.set_data_type(fuchsia::diagnostics::DataType::INSPECT);
  stream_parameters.set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT);
  stream_parameters.set_format(fuchsia::diagnostics::Format::JSON);

  {
    std::vector<fuchsia::diagnostics::SelectorArgument> args;
    args.emplace_back();
    args[0].set_raw_selector(cobalt_under_test_moniker_ + ":root");

    fuchsia::diagnostics::ClientSelectorConfiguration client_selector_config;
    client_selector_config.set_selectors(std::move(args));
    stream_parameters.set_client_selector_configuration(std::move(client_selector_config));
  }
  (*inspect_archive_)->StreamDiagnostics(std::move(stream_parameters), iterator.NewRequest());

  fuchsia::diagnostics::BatchIterator_GetNext_Result out_result;
  zx_status_t status = iterator->GetNext(&out_result);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get the Inspect diagnostics data: " << status;
    return "";
  }
  if (out_result.is_err()) {
    FX_LOGS(ERROR) << "Inspect diagnostics Reader Error returned: "
                   << (out_result.err() == fuchsia::diagnostics::ReaderError::IO);
    return "";
  }
  if (out_result.response().batch.empty()) {
    FX_LOGS(ERROR) << "Inspect diagnostics returned empty response.";
    return "";
  }
  // Should be at most one component.
  ZX_ASSERT(out_result.response().batch.size() <= 1);
  if (!out_result.response().batch.empty()) {
    std::string json;
    fsl::StringFromVmo(out_result.response().batch[0].json(), &json);
    return json;
  }
  return "";
}

}  // namespace cobalt::testapp
