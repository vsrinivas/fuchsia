// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_COMMANDS_RECORD_H_
#define APPS_TRACING_SRC_TRACE_COMMANDS_RECORD_H_

#include <memory>
#include <string>
#include <vector>

#include "application/services/application_controller.fidl.h"
#include "application/services/application_launcher.fidl.h"
#include "apps/tracing/lib/measure/duration.h"
#include "apps/tracing/lib/measure/measurements.h"
#include "apps/tracing/lib/measure/time_between.h"
#include "apps/tracing/lib/trace_converters/chromium_exporter.h"
#include "apps/tracing/src/trace/command.h"
#include "apps/tracing/src/trace/results_upload.h"
#include "apps/tracing/src/trace/spec.h"
#include "apps/tracing/src/trace/tracer.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/time/time_delta.h"

namespace tracing {

class Record : public CommandWithTraceController {
 public:
  struct Options {
    bool Setup(const ftl::CommandLine&);

    std::string app;
    std::vector<std::string> args;
    std::vector<std::string> categories = {};
    ftl::TimeDelta duration = ftl::TimeDelta::FromSeconds(10);
    bool detach = false;
    bool decouple = false;
    uint32_t buffer_size_megabytes_hint = 4;
    std::string output_file_name = "/tmp/trace.json";
    measure::Measurements measurements;
    bool upload_results = false;
    UploadMetadata upload_metadata;
  };

  static Info Describe();

  explicit Record(app::ApplicationContext* context);
  void Run(const ftl::CommandLine& command_line) override;

 private:
  void StopTrace();
  void ProcessMeasurements(ftl::Closure on_done);
  void DoneTrace();
  void LaunchApp();
  void StartTimer();

  app::ApplicationControllerPtr application_controller_;
  std::unique_ptr<ChromiumExporter> exporter_;
  std::unique_ptr<Tracer> tracer_;
  // Aggregate events if there are any measurements to be performed, so that we
  // can sort them by timestamp and process in order.
  bool aggregate_events_ = false;
  std::vector<reader::Record::Event> events_;
  std::unique_ptr<measure::MeasureDuration> measure_duration_;
  std::unique_ptr<measure::MeasureTimeBetween> measure_time_between_;
  bool tracing_ = false;
  Options options_;

  ftl::WeakPtrFactory<Record> weak_ptr_factory_;
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_COMMANDS_RECORD_H_
