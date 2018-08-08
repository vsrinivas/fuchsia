// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fdio/spawn.h>
#include <zx/time.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "garnet/bin/trace/commands/record.h"
#include "garnet/bin/trace/results_export.h"
#include "garnet/bin/trace/results_output.h"
#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/fxl/strings/string_number_conversions.h"

namespace tracing {

namespace {

// Command line options.
const char kSpecFile[] = "spec-file";
const char kCategories[] = "categories";
const char kAppendArgs[] = "append-args";
const char kOutputFile[] = "output-file";
const char kDuration[] = "duration";
const char kDetach[] = "detach";
const char kDecouple[] = "decouple";
const char kLaunchpad[] = "launchpad";
const char kBufferSize[] = "buffer-size";
const char kBufferingMode[] = "buffering-mode";
const char kBenchmarkResultsFile[] = "benchmark-results-file";
const char kTestSuite[] = "test-suite";

const struct {
  const char* name;
  fuchsia::tracing::BufferingMode mode;
} kBufferingModes[] = {
    {"oneshot", fuchsia::tracing::BufferingMode::ONESHOT},
    {"circular", fuchsia::tracing::BufferingMode::CIRCULAR},
    {"streaming", fuchsia::tracing::BufferingMode::STREAMING},
};

zx_handle_t Launch(const std::vector<std::string>& args) {
  zx_handle_t subprocess = ZX_HANDLE_INVALID;
  if (!args.size())
    return subprocess;

  std::vector<const char*> raw_args;
  for (const auto& item : args) {
    raw_args.push_back(item.c_str());
  }
  raw_args.push_back(nullptr);

  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                  raw_args[0], raw_args.data(), &subprocess);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Subprocess launch failed: \"" << status
                   << "\" Did you provide the full path to the tool?";
  }

  return subprocess;
}

bool WaitForExit(zx_handle_t process, int* exit_code) {
  zx_signals_t signals_observed = 0;
  zx_status_t status = zx_object_wait_one(process, ZX_TASK_TERMINATED,
                                          ZX_TIME_INFINITE, &signals_observed);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_wait_one failed, status: " << status;
    return false;
  }

  zx_info_process_t proc_info;
  status = zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info,
                              sizeof(proc_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_get_info failed, status: " << status;
    return false;
  }

  *exit_code = proc_info.return_code;
  return true;
}

}  // namespace

bool Record::Options::Setup(const fxl::CommandLine& command_line) {
  const std::unordered_set<std::string> known_options = {kSpecFile,
                                                         kCategories,
                                                         kAppendArgs,
                                                         kOutputFile,
                                                         kDuration,
                                                         kDetach,
                                                         kDecouple,
                                                         kLaunchpad,
                                                         kBufferSize,
                                                         kBufferingMode,
                                                         kBenchmarkResultsFile,
                                                         kTestSuite};

  for (auto& option : command_line.options()) {
    if (known_options.count(option.name) == 0) {
      FXL_LOG(ERROR) << "Unknown option: " << option.name;
      return false;
    }
  }

  size_t index = 0;
  // Read the spec file first. Arguments passed on the command line override the
  // spec.
  // --spec-file=<file>
  if (command_line.HasOption(kSpecFile, &index)) {
    std::string spec_file_path = command_line.options()[index].value;
    if (!files::IsFile(spec_file_path)) {
      FXL_LOG(ERROR) << spec_file_path << " is not a file";
      return false;
    }

    std::string content;
    if (!files::ReadFileToString(spec_file_path, &content)) {
      FXL_LOG(ERROR) << "Can't read " << spec_file_path;
      return false;
    }

    Spec spec;
    if (!DecodeSpec(content, &spec)) {
      FXL_LOG(ERROR) << "Can't decode " << spec_file_path;
      return false;
    }
    app = std::move(spec.app);
    args = std::move(spec.args);
    categories = std::move(spec.categories);
    duration = std::move(spec.duration);
    measurements = std::move(spec.measurements);
    test_suite = std::move(spec.test_suite_name);
  }

  // --categories=<cat1>,<cat2>,...
  if (command_line.HasOption(kCategories, &index)) {
    categories =
        fxl::SplitStringCopy(command_line.options()[index].value, ",",
                             fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  }

  // --append-args=<arg1>,<arg2>,...
  if (command_line.HasOption(kAppendArgs, &index)) {
    auto append_args =
        fxl::SplitStringCopy(command_line.options()[index].value, ",",
                             fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
    std::move(std::begin(append_args), std::end(append_args),
              std::back_inserter(args));
  }

  // --output-file=<file>
  if (command_line.HasOption(kOutputFile, &index)) {
    output_file_name = command_line.options()[index].value;
  }

  // --duration=<seconds>
  if (command_line.HasOption(kDuration, &index)) {
    uint64_t seconds;
    if (!fxl::StringToNumberWithError(command_line.options()[index].value,
                                      &seconds)) {
      FXL_LOG(ERROR) << "Failed to parse command-line option duration: "
                     << command_line.options()[index].value;
      return false;
    }
    duration = fxl::TimeDelta::FromSeconds(seconds);
  }

  // --detach
  detach = command_line.HasOption(kDetach);

  // --decouple
  decouple = command_line.HasOption(kDecouple);

  // --launchpad
  launchpad = command_line.HasOption(kLaunchpad);

  // --buffer-size=<megabytes>
  if (command_line.HasOption(kBufferSize, &index)) {
    uint32_t megabytes;
    if (!fxl::StringToNumberWithError(command_line.options()[index].value,
                                      &megabytes)) {
      FXL_LOG(ERROR) << "Failed to parse command-line option buffer-size: "
                     << command_line.options()[index].value;
      return false;
    }
    buffer_size_megabytes_hint = megabytes;
  }

  // --buffering-mode=oneshot|circular|streaming
  if (command_line.HasOption(kBufferingMode, &index)) {
    bool found = false;
    for (const auto& mode : kBufferingModes) {
      if (command_line.options()[index].value == mode.name) {
        buffering_mode = mode.mode;
        found = true;
        break;
      }
    }
    if (!found) {
      FXL_LOG(ERROR) << "Failed to parse command-line option " << kBufferingMode
                     << ": " << command_line.options()[index].value;
      return false;
    }
  }

  // --benchmark-results-file=<file>
  if (command_line.HasOption(kBenchmarkResultsFile, &index)) {
    benchmark_results_file = command_line.options()[index].value;
  }

  // --test-suite=<test-suite-name>
  if (command_line.HasOption(kTestSuite, &index)) {
    test_suite = command_line.options()[index].value;
  }

  // <command> <args...>
  const auto& positional_args = command_line.positional_args();
  if (!positional_args.empty()) {
    if (!app.empty() || !args.empty()) {
      FXL_LOG(WARNING) << "The app and args passed on the command line"
                       << "override those from the tspec file.";
    }
    app = positional_args[0];
    args = std::vector<std::string>(positional_args.begin() + 1,
                                    positional_args.end());
  }

  return true;
}

Command::Info Record::Describe() {
  return Command::Info{
      [](component::StartupContext* context) {
        return std::make_unique<Record>(context);
      },
      "record",
      "starts tracing and records data",
      {{"spec-file=[none]", "Tracing specification file"},
       {"output-file=[/data/trace.json]", "Trace data is stored in this file"},
       {"duration=[10s]",
        "Trace will be active for this long after the session has been "
        "started"},
       {"categories=[\"\"]", "Categories that should be enabled for tracing"},
       {"append-args=[\"\"]",
        "Additional args for the app being traced, appended to those from the "
        "spec file, if any"},
       {"detach=[false]",
        "Don't stop the traced program when tracing finished"},
       {"decouple=[false]", "Don't stop tracing when the traced program exits"},
       {"launchpad=[false]",
        "Use launchpad to run a legacy app. Detach will have no effect when "
        "using this option"},
       {"buffer-size=[4]",
        "Maximum size of trace buffer for each provider in megabytes"},
       {"buffering-mode=oneshot|circular|streaming",
        "The buffering mode to use"},
       {"benchmark-results-file=[none]",
        "Destination for exported benchmark results"},
       {"test-suite=[none]",
        "Test suite name to put into the exported benchmark results file. "
        "This is used by the Catapult dashboard. This argument is required if "
        "the results are uploaded to the Catapult dashboard (using "
        "bin/catapult_converter)"},
       {"[command args]",
        "Run program before starting trace. The program is terminated when "
        "tracing ends unless --detach is specified"}}};
}

Record::Record(component::StartupContext* context)
    : CommandWithTraceController(context), weak_ptr_factory_(this) {}

void Record::Start(const fxl::CommandLine& command_line) {
  if (!options_.Setup(command_line)) {
    FXL_LOG(ERROR) << "Error parsing options from command line - aborting";
    Done(1);
    return;
  }

  std::ofstream out_file(options_.output_file_name,
                         std::ios_base::out | std::ios_base::trunc);
  if (!out_file.is_open()) {
    FXL_LOG(ERROR) << "Failed to open " << options_.output_file_name
                   << " for writing";
    Done(1);
    return;
  }

  exporter_.reset(new ChromiumExporter(std::move(out_file)));
  tracer_.reset(new Tracer(trace_controller().get()));
  if (!options_.measurements.duration.empty()) {
    aggregate_events_ = true;
    measure_duration_.reset(
        new measure::MeasureDuration(options_.measurements.duration));
  }
  if (!options_.measurements.time_between.empty()) {
    aggregate_events_ = true;
    measure_time_between_.reset(
        new measure::MeasureTimeBetween(options_.measurements.time_between));
  }
  if (!options_.measurements.argument_value.empty()) {
    aggregate_events_ = true;
    measure_argument_value_.reset(new measure::MeasureArgumentValue(
        options_.measurements.argument_value));
  }

  tracing_ = true;

  fuchsia::tracing::TraceOptions trace_options;
  trace_options.categories =
      fxl::To<fidl::VectorPtr<fidl::StringPtr>>(options_.categories);
  trace_options.buffer_size_megabytes_hint =
      options_.buffer_size_megabytes_hint;
  // TODO(dje): start_timeout_milliseconds
  trace_options.buffering_mode = options_.buffering_mode;

  tracer_->Start(
      std::move(trace_options),
      [this](trace::Record record) {
        exporter_->ExportRecord(record);

        if (aggregate_events_ && record.type() == trace::RecordType::kEvent) {
          events_.push_back(fbl::move(record));
        }
      },
      [](fbl::String error) { FXL_LOG(ERROR) << error.c_str(); },
      [this] {
        if (!options_.app.empty())
          options_.launchpad ? LaunchTool() : LaunchApp();
        StartTimer();
      },
      [this] { DoneTrace(); });
}

void Record::StopTrace(int32_t return_code) {
  if (tracing_) {
    out() << "Stopping trace..." << std::endl;
    tracing_ = false;
    return_code_ = return_code;
    tracer_->Stop();
  }
}

void Record::ProcessMeasurements() {
  if (!events_.empty()) {
    std::sort(std::begin(events_), std::end(events_),
              [](const trace::Record& e1, const trace::Record& e2) {
                return e1.GetEvent().timestamp < e2.GetEvent().timestamp;
              });
  }

  for (const auto& event : events_) {
    if (measure_duration_) {
      measure_duration_->Process(event.GetEvent());
    }
    if (measure_time_between_) {
      measure_time_between_->Process(event.GetEvent());
    }
    if (measure_argument_value_) {
      measure_argument_value_->Process(event.GetEvent());
    }
  }

  std::unordered_map<uint64_t, std::vector<trace_ticks_t>> ticks;
  if (measure_duration_) {
    ticks.insert(measure_duration_->results().begin(),
                 measure_duration_->results().end());
  }
  if (measure_time_between_) {
    ticks.insert(measure_time_between_->results().begin(),
                 measure_time_between_->results().end());
  }
  if (measure_argument_value_) {
    ticks.insert(measure_argument_value_->results().begin(),
                 measure_argument_value_->results().end());
  }

  uint64_t ticks_per_second = zx_ticks_per_second();
  FXL_DCHECK(ticks_per_second);
  std::vector<measure::Result> results =
      measure::ComputeResults(options_.measurements, ticks, ticks_per_second);

  // Fail and quit if any of the measurements has empty results. This is so that
  // we can notice when benchmarks break (e.g. in CQ or on perfbots).
  bool errored = false;
  for (auto& result : results) {
    if (result.samples.empty()) {
      FXL_LOG(ERROR) << "No results for measurement \"" << result.label
                     << "\".";
      errored = true;
    }
  }
  OutputResults(out(), results);
  if (errored) {
    FXL_LOG(ERROR) << "One or more measurements had empty results. Quitting.";
    Done(1);
    return;
  }

  if (!options_.benchmark_results_file.empty()) {
    for (auto& result : results) {
      result.test_suite = options_.test_suite;
    }
    if (!ExportResults(options_.benchmark_results_file, results)) {
      FXL_LOG(ERROR) << "Failed to write benchmark results to "
                     << options_.benchmark_results_file;
      Done(1);
      return;
    }
    out() << "Benchmark results written to " << options_.benchmark_results_file
          << std::endl;
  }

  Done(return_code_);
}

void Record::DoneTrace() {
  tracer_.reset();
  exporter_.reset();

  out() << "Trace file written to " << options_.output_file_name << std::endl;

  if (measure_duration_ || measure_time_between_ || measure_argument_value_) {
    ProcessMeasurements();
  } else {
    Done(return_code_);
  }
}

void Record::LaunchApp() {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = fidl::StringPtr(options_.app);
  launch_info.arguments =
      fxl::To<fidl::VectorPtr<fidl::StringPtr>>(options_.args);

  out() << "Launching " << launch_info.url << std::endl;
  context()->launcher()->CreateComponent(std::move(launch_info),
                                         component_controller_.NewRequest());
  component_controller_.set_error_handler([this] {
    out() << "Application terminated" << std::endl;
    if (!options_.decouple)
      // The trace might have been already stopped by the |Wait()| callback. In
      // that case, |StopTrace| below does nothing.
      StopTrace(-1);
  });
  component_controller_->Wait([this](int32_t return_code) {
    out() << "Application exited with return code " << return_code << std::endl;
    if (!options_.decouple)
      StopTrace(return_code);
  });
  if (options_.detach) {
    component_controller_->Detach();
  }
}

void Record::LaunchTool() {
  std::vector<std::string> all_args = {options_.app};
  all_args.insert(all_args.end(), options_.args.begin(), options_.args.end());

  if (FXL_VLOG_IS_ON(1)) {
    std::stringstream command;
    for (size_t i = 0; i < all_args.size(); ++i) {
      if (i > 0)
        command << " ";
      command << all_args[i];
    }
    FXL_VLOG(1) << "Launching: " << command.str();
  } else {
    out() << "Launching: " << options_.app << std::endl;
  }

  zx_handle_t process = Launch(all_args);
  if (process == ZX_HANDLE_INVALID) {
    StopTrace(-1);
    FXL_LOG(ERROR) << "Unable to launch " << options_.app;
    return;
  }

  int exit_code = -1;
  if (!WaitForExit(process, &exit_code))
    FXL_LOG(ERROR) << "Unable to get return code";

  out() << "Application exited with return code " << exit_code << std::endl;
  if (!options_.decouple)
    StopTrace(exit_code);
}

void Record::StartTimer() {
  async::PostDelayedTask(async_get_default_dispatcher(),
                         [weak = weak_ptr_factory_.GetWeakPtr()] {
                           if (weak)
                             weak->StopTrace(0);
                         },
                         zx::nsec(options_.duration.ToNanoseconds()));
  out() << "Starting trace; will stop in " << options_.duration.ToSecondsF()
        << " seconds..." << std::endl;
}

}  // namespace tracing
