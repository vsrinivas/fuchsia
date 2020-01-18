// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_COMMANDS_RECORD_H_
#define GARNET_BIN_TRACE_COMMANDS_RECORD_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/process.h>

#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/trace/cmd_utils.h"
#include "garnet/bin/trace/command.h"
#include "garnet/bin/trace/spec.h"
#include "garnet/bin/trace/tracer.h"
#include "garnet/bin/trace/utils.h"
#include "garnet/lib/measure/argument_value.h"
#include "garnet/lib/measure/duration.h"
#include "garnet/lib/measure/measurements.h"
#include "garnet/lib/measure/time_between.h"
#include "garnet/lib/trace_converters/chromium_exporter.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/time/time_delta.h"

namespace tracing {

class RecordCommand : public CommandWithController {
 public:
  struct Options {
    bool Setup(const fxl::CommandLine&);

    std::string test_name;
    std::string app;
    std::vector<std::string> args;
    std::vector<std::string> categories = {};
    fxl::TimeDelta duration = fxl::TimeDelta::FromSeconds(kDefaultDurationSeconds);
    bool detach = false;
    bool decouple = false;
    bool spawn = false;
    bool return_child_result = true;
    std::optional<std::string> environment_name;
    uint32_t buffer_size_megabytes = kDefaultBufferSizeMegabytes;
    std::vector<ProviderSpec> provider_specs;
    controller::BufferingMode buffering_mode = kDefaultBufferingMode;
    bool binary = false;
    bool compress = false;
    std::string output_file_name = kDefaultOutputFileName;
    std::string benchmark_results_file;
    std::string test_suite;
    measure::Measurements measurements;
  };

  static Info Describe();

  explicit RecordCommand(sys::ComponentContext* context);

 protected:
  void Start(const fxl::CommandLine& command_line) override;

 private:
  void TerminateTrace(int32_t return_code);
  void ProcessMeasurements();
  void DoneTrace();
  void LaunchComponentApp();
  void LaunchSpawnedApp();
  void StartTimer();
  void OnSpawnedAppExit(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
  void KillSpawnedApp();

  async_dispatcher_t* dispatcher_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  fuchsia::sys::EnvironmentControllerPtr environment_controller_;
  zx::process spawned_app_;
  async::WaitMethod<RecordCommand, &RecordCommand::OnSpawnedAppExit> wait_spawned_app_;

  std::unique_ptr<std::ostream> binary_out_;
  // TODO(PT-113): Remove |exporter_|.
  std::unique_ptr<ChromiumExporter> exporter_;

  std::unique_ptr<Tracer> tracer_;

  // Aggregate events if there are any measurements to be performed, so that we
  // can sort them by timestamp and process in order.
  bool aggregate_events_ = false;
  // This is actually a vector of trace::Record::Event, but it isn't
  // copyable so we record the entire Record here (which also isn't copyable
  // but it is movable).
  std::vector<trace::Record> events_;
  std::unique_ptr<measure::MeasureDuration> measure_duration_;
  std::unique_ptr<measure::MeasureTimeBetween> measure_time_between_;
  std::unique_ptr<measure::MeasureArgumentValue> measure_argument_value_;

  bool tracing_ = false;
  int32_t return_code_ = 0;
  Options options_;

  fxl::WeakPtrFactory<RecordCommand> weak_ptr_factory_;
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_COMMANDS_RECORD_H_
