// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/commands/record.h"

#include <errno.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <string.h>
#include <zircon/status.h>

#include <fstream>
#include <string>
#include <unordered_set>

#include "garnet/bin/trace/results_export.h"
#include "garnet/bin/trace/results_output.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/lib/fxl/strings/trim.h"

namespace tracing {

namespace {

// Command line options.
const char kSpecFile[] = "spec-file";
const char kCategories[] = "categories";
const char kAppendArgs[] = "append-args";
const char kOutputFile[] = "output-file";
const char kBinary[] = "binary";
const char kCompress[] = "compress";
const char kDuration[] = "duration";
const char kDetach[] = "detach";
const char kDecouple[] = "decouple";
const char kSpawn[] = "spawn";
const char kEnvironmentName[] = "environment-name";
const char kReturnChildResult[] = "return-child-result";
const char kBufferSize[] = "buffer-size";
const char kProviderBufferSize[] = "provider-buffer-size";
const char kBufferingMode[] = "buffering-mode";
const char kBenchmarkResultsFile[] = "benchmark-results-file";
const char kTestSuite[] = "test-suite";
const char kTrigger[] = "trigger";

zx_status_t Spawn(const std::vector<std::string>& args, zx::process* subprocess) {
  FX_DCHECK(args.size() > 0);

  std::vector<const char*> raw_args;
  for (const auto& item : args) {
    raw_args.push_back(item.c_str());
  }
  raw_args.push_back(nullptr);

  return fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, raw_args[0], raw_args.data(),
                    subprocess->reset_and_get_address());
}

void CheckCommandLineOverride(const char* name, bool present_in_spec) {
  if (present_in_spec) {
    FX_LOGS(WARNING) << "The " << name << " passed on the command line"
                     << "override value(s) from the tspec file.";
  }
}

// Helper so call sites don't have to type !!object_as_unique_ptr.
template <typename T>
void CheckCommandLineOverride(const char* name, const T& object) {
  CheckCommandLineOverride(name, !!object);
}

}  // namespace

bool RecordCommand::Options::Setup(const fxl::CommandLine& command_line) {
  const std::unordered_set<std::string> known_options = {kSpecFile,        kCategories,
                                                         kAppendArgs,      kOutputFile,
                                                         kBinary,          kCompress,
                                                         kDuration,        kDetach,
                                                         kDecouple,        kSpawn,
                                                         kEnvironmentName, kReturnChildResult,
                                                         kBufferSize,      kProviderBufferSize,
                                                         kBufferingMode,   kBenchmarkResultsFile,
                                                         kTestSuite,       kTrigger};

  for (auto& option : command_line.options()) {
    if (known_options.count(option.name) == 0) {
      FX_LOGS(ERROR) << "Unknown option: " << option.name;
      return false;
    }
  }

  Spec spec{};
  size_t index = 0;
  // Read the spec file first. Arguments passed on the command line override the
  // spec.
  // --spec-file=<file>
  if (command_line.HasOption(kSpecFile, &index)) {
    std::string spec_file_path = command_line.options()[index].value;
    if (!files::IsFile(spec_file_path)) {
      FX_LOGS(ERROR) << spec_file_path << " is not a file";
      return false;
    }

    std::string content;
    if (!files::ReadFileToString(spec_file_path, &content)) {
      FX_LOGS(ERROR) << "Can't read " << spec_file_path;
      return false;
    }

    if (!DecodeSpec(content, &spec)) {
      FX_LOGS(ERROR) << "Can't decode " << spec_file_path;
      return false;
    }

    if (spec.test_name)
      test_name = *spec.test_name;
    if (spec.app)
      app = *spec.app;
    if (spec.args)
      args = *spec.args;
    if (spec.spawn)
      spawn = *spec.spawn;
    if (spec.environment_name)
      environment_name = *spec.environment_name;
    if (spec.categories)
      categories = *spec.categories;
    if (spec.buffering_mode) {
      const BufferingModeSpec* mode_spec = LookupBufferingMode(*spec.buffering_mode);
      if (mode_spec == nullptr) {
        FX_LOGS(ERROR) << "Unknown spec parameter buffering-mode: " << spec.buffering_mode;
        return false;
      }
      buffering_mode = TranslateBufferingMode(mode_spec->mode);
    }
    if (spec.buffer_size_in_mb)
      buffer_size_megabytes = *spec.buffer_size_in_mb;
    if (spec.provider_specs)
      provider_specs = *spec.provider_specs;
    if (spec.duration)
      duration = *spec.duration;
    if (spec.measurements)
      measurements = *spec.measurements;
    if (spec.test_suite_name)
      test_suite = *spec.test_suite_name;
    if (spec.trigger_specs)
      trigger_specs = std::move(*spec.trigger_specs);
  }

  // --categories=<cat1>,<cat2>,...
  if (command_line.HasOption(kCategories, &index)) {
    categories = fxl::SplitStringCopy(command_line.options()[index].value, ",",
                                      fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
    CheckCommandLineOverride("categories", spec.categories);
  }

  // --append-args=<arg1>,<arg2>,...
  // This option may be repeated, all args are added in order.
  // These arguments are added after either of spec.args or the command line
  // positional args.
  std::vector<std::string> append_args;
  if (command_line.HasOption(kAppendArgs, nullptr)) {
    auto all_append_args = command_line.GetOptionValues(kAppendArgs);
    for (const auto& arg : all_append_args) {
      auto args = fxl::SplitStringCopy(arg, ",", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
      std::move(std::begin(args), std::end(args), std::back_inserter(append_args));
    }
  }

  // --binary
  if (ParseBooleanOption(command_line, kBinary, &binary) == OptionStatus::ERROR) {
    return false;
  }
  if (binary) {
    output_file_name = kDefaultBinaryOutputFileName;
  }

  // --compress
  if (ParseBooleanOption(command_line, kCompress, &compress) == OptionStatus::ERROR) {
    return false;
  }
  if (compress) {
    output_file_name += ".gz";
  }

  // --output-file=<file>
  if (command_line.HasOption(kOutputFile, &index)) {
    output_file_name = command_line.options()[index].value;
  }

  // --duration=<seconds>
  if (command_line.HasOption(kDuration, &index)) {
    uint64_t seconds;
    if (!fxl::StringToNumberWithError(command_line.options()[index].value, &seconds)) {
      FX_LOGS(ERROR) << "Failed to parse command-line option " << kDuration << ": "
                     << command_line.options()[index].value;
      return false;
    }
    duration = zx::sec(seconds);
    CheckCommandLineOverride("duration", spec.duration);
  }

  // --detach
  if (ParseBooleanOption(command_line, kDetach, &detach) == OptionStatus::ERROR) {
    return false;
  }

  // --decouple
  if (ParseBooleanOption(command_line, kDecouple, &decouple) == OptionStatus::ERROR) {
    return false;
  }

  // --spawn
  {
    bool spawn_value = false;
    OptionStatus spawn_status = ParseBooleanOption(command_line, kSpawn, &spawn_value);
    if (spawn_status == OptionStatus::ERROR) {
      return false;
    }
    bool have_spawn = spawn_status == OptionStatus::PRESENT;
    if (have_spawn) {
      spawn = spawn_value;
      CheckCommandLineOverride("spawn", spec.spawn);
    }
  }

  // --environment-name
  if (command_line.HasOption(kEnvironmentName, &index)) {
    environment_name = command_line.options()[index].value;
    CheckCommandLineOverride(kEnvironmentName, spec.environment_name);
  }

  // --return-child-result=<flag>
  if (ParseBooleanOption(command_line, kReturnChildResult, &return_child_result) ==
      OptionStatus::ERROR) {
    return false;
  }

  // --buffer-size=<megabytes>
  if (command_line.HasOption(kBufferSize, &index)) {
    if (!ParseBufferSize(command_line.options()[index].value, &buffer_size_megabytes)) {
      return false;
    }
    CheckCommandLineOverride("buffer-size", spec.buffer_size_in_mb);
  }

  // --provider-buffer-size=<name:megabytes>
  if (command_line.HasOption(kProviderBufferSize)) {
    std::vector<fxl::StringView> args = command_line.GetOptionValues(kProviderBufferSize);
    if (!ParseProviderBufferSize(args, &provider_specs)) {
      return false;
    }
    CheckCommandLineOverride("provider-specs", spec.provider_specs);
  }

  // --buffering-mode=oneshot|circular|streaming
  if (command_line.HasOption(kBufferingMode, &index)) {
    BufferingMode mode;
    if (!ParseBufferingMode(command_line.options()[index].value, &mode)) {
      return false;
    }
    buffering_mode = TranslateBufferingMode(mode);
    CheckCommandLineOverride("buffering-mode", spec.buffering_mode);
  }

  // --benchmark-results-file=<file>
  if (command_line.HasOption(kBenchmarkResultsFile, &index)) {
    benchmark_results_file = command_line.options()[index].value;
  }

  // --test-suite=<test-suite-name>
  if (command_line.HasOption(kTestSuite, &index)) {
    test_suite = command_line.options()[index].value;
    CheckCommandLineOverride("test-suite-name", spec.test_suite_name);
  }

  // --trigger=<alert>:<action>
  if (command_line.HasOption(kTrigger)) {
    std::vector<fxl::StringView> args = command_line.GetOptionValues(kTrigger);
    if (!ParseTriggers(args, &trigger_specs)) {
      return false;
    }
    CheckCommandLineOverride("trigger", spec.trigger_specs);
  }

  // <command> <args...>
  const auto& positional_args = command_line.positional_args();
  if (!positional_args.empty()) {
    app = positional_args[0];
    args = std::vector<std::string>(positional_args.begin() + 1, positional_args.end());
    CheckCommandLineOverride("app,args", spec.app || spec.args);
  }

  // Now that we've processed positional args we can append --append-args args.
  std::move(std::begin(append_args), std::end(append_args), std::back_inserter(args));

  return true;
}

Command::Info RecordCommand::Describe() {
  return Command::Info{
      [](sys::ComponentContext* context) { return std::make_unique<RecordCommand>(context); },
      "record",
      "starts tracing and records data",
      {{"spec-file=[none]", "Tracing specification file"},
       {"output-file=[/tmp/trace.json]",
        "Trace data is stored in this file. "
        "If the output file is \"tcp:TCP-ADDRESS\" then the output is streamed "
        "to that address. TCP support is generally only used by traceutil."},
       {"binary=[false]",
        "Output the binary trace rather than converting to JSON. "
        "If this is set, then the default output location will be "
        "/tmp/trace.fxt"},
       {"compress=[false]",
        "Compress trace output. This option is ignored "
        "when streaming over a TCP socket."},
       {"duration=[10]",
        "Trace will be active for this many seconds after the session has been "
        "started. The provided value must be integral."},
       {"categories=[\"\"]", "Categories that should be enabled for tracing"},
       {"append-args=[\"\"]",
        "Additional args for the app being traced, appended to those from the "
        "spec file, if any. The value is a comma-separated list of arguments "
        "to pass. This option may be repeated, arguments are added in order."},
       {"detach=[false]", "Don't stop the traced program when tracing finished"},
       {"decouple=[false]", "Don't stop tracing when the traced program exits"},
       {"spawn=[false]", "Use fdio_spawn to run a legacy app."},
       {"environment-name=[none]",
        "Create a nested environment with the given name and run the app being traced under it."},
       {"return-child-result=[true]",
        "Return with the same return code as the child. "
        "Only valid when a child program is passed."},
       {"buffer-size=[4]", "Maximum size of trace buffer for each provider in megabytes"},
       {"provider-buffer-size=[provider-name:buffer-size]",
        "Specify the buffer size that \"provider-name\" will use. "
        "May be specified multiple times, once per provider."},
       {"buffering-mode=oneshot|circular|streaming", "The buffering mode to use"},
       {"benchmark-results-file=[none]", "Destination for exported benchmark results"},
       {"test-suite=[none]",
        "Test suite name to put into the exported benchmark results file. "
        "This is used by the Catapult dashboard. This argument is required if "
        "the results are uploaded to the Catapult dashboard (using "
        "bin/catapult_converter)"},
       {"trigger=<alert>:<action>",
        "Specifies an action to take when an alert with "
        "the specified name is received. Multiple alert/action rules may be "
        "specified using multiple --trigger arguments. The only action currently "
        "supported is 'stop'. This action causes the session to be stopped and "
        "results to be captured"},
       {"[command args]",
        "Run program after starting trace. The program is terminated when "
        "tracing ends unless --detach is specified"}}};
}

RecordCommand::RecordCommand(sys::ComponentContext* context)
    : CommandWithController(context),
      dispatcher_(async_get_default_dispatcher()),
      wait_spawned_app_(this),
      weak_ptr_factory_(this) {
  wait_spawned_app_.set_trigger(ZX_PROCESS_TERMINATED);
}

void RecordCommand::Start(const fxl::CommandLine& command_line) {
  if (!options_.Setup(command_line)) {
    FX_LOGS(ERROR) << "Error parsing options from command line - aborting";
    Done(EXIT_FAILURE);
    return;
  }

  std::unique_ptr<std::ostream> out_stream =
      OpenOutputStream(options_.output_file_name, options_.compress);
  if (!out_stream) {
    FX_LOGS(ERROR) << "Failed to open " << options_.output_file_name << " for writing";
    Done(EXIT_FAILURE);
    return;
  }

  Tracer::BytesConsumer bytes_consumer;
  Tracer::RecordConsumer record_consumer;
  Tracer::ErrorHandler error_handler;
  if (options_.binary) {
    binary_out_ = std::move(out_stream);

    bytes_consumer = [this](const unsigned char* buffer, size_t n_bytes) {
      binary_out_->write(reinterpret_cast<const char*>(buffer), n_bytes);
    };
    record_consumer = [](trace::Record record) {};
    error_handler = [](fbl::String error) {};
  } else {
    exporter_.reset(new ChromiumExporter(std::move(out_stream)));

    bytes_consumer = [](const unsigned char* buffer, size_t n_bytes) {};
    record_consumer = [this](trace::Record record) {
      exporter_->ExportRecord(record);

      if (aggregate_events_ && record.type() == trace::RecordType::kEvent) {
        events_.push_back(std::move(record));
      }
    };
    error_handler = [](fbl::String error) { FX_LOGS(ERROR) << error.c_str(); };
  }

  tracer_.reset(new Tracer(controller().get()));

  if (!options_.measurements.duration.empty()) {
    aggregate_events_ = true;
    measure_duration_.reset(new measure::MeasureDuration(options_.measurements.duration));
  }
  if (!options_.measurements.time_between.empty()) {
    aggregate_events_ = true;
    measure_time_between_.reset(
        new measure::MeasureTimeBetween(options_.measurements.time_between));
  }
  if (!options_.measurements.argument_value.empty()) {
    aggregate_events_ = true;
    measure_argument_value_.reset(
        new measure::MeasureArgumentValue(options_.measurements.argument_value));
  }

  tracing_ = true;

  controller::TraceConfig trace_config;
  trace_config.set_categories(options_.categories);
  trace_config.set_buffer_size_megabytes_hint(options_.buffer_size_megabytes);
  // TODO(dje): start_timeout_milliseconds
  trace_config.set_buffering_mode(options_.buffering_mode);
  trace_config.set_provider_specs(TranslateProviderSpecs(options_.provider_specs));

  tracer_->Initialize(
      std::move(trace_config), options_.binary, std::move(bytes_consumer),
      std::move(record_consumer), std::move(error_handler),
      [this] { DoneTrace(); },  // TODO(37435): For now preserve existing behaviour.
      [this] { DoneTrace(); }, fit::bind_member(this, &RecordCommand::OnAlert));

  tracer_->Start([this](controller::Controller_StartTracing_Result result) {
    if (result.is_err()) {
      FX_LOGS(ERROR) << "Unable to start trace: " << StartErrorCodeToString(result.err());
      tracing_ = false;
      Done(EXIT_FAILURE);
      return;
    }
    if (!options_.app.empty()) {
      options_.spawn ? LaunchSpawnedApp() : LaunchComponentApp();
    }
    StartTimer();
  });
}

void RecordCommand::TerminateTrace(int32_t return_code) {
  if (tracing_) {
    out() << "Terminating trace..." << std::endl;
    tracing_ = false;
    return_code_ = return_code;
    tracer_->Terminate();
    if (spawned_app_ && !options_.detach) {
      KillSpawnedApp();
    }
  }
}

void RecordCommand::ProcessMeasurements() {
  FX_DCHECK(!tracing_);

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
    ticks.insert(measure_duration_->results().begin(), measure_duration_->results().end());
  }
  if (measure_time_between_) {
    ticks.insert(measure_time_between_->results().begin(), measure_time_between_->results().end());
  }
  if (measure_argument_value_) {
    ticks.insert(measure_argument_value_->results().begin(),
                 measure_argument_value_->results().end());
  }

  uint64_t ticks_per_second = zx_ticks_per_second();
  FX_DCHECK(ticks_per_second);
  std::vector<measure::Result> results =
      measure::ComputeResults(options_.measurements, ticks, ticks_per_second);

  // Fail and quit if any of the measurements has empty results. This is so that
  // we can notice when benchmarks break (e.g. in CQ or on perfbots).
  bool errored = false;
  for (auto& result : results) {
    if (result.values.empty()) {
      FX_LOGS(ERROR) << "No results for measurement \"" << result.label << "\".";
      errored = true;
    }
  }
  OutputResults(out(), results);
  if (errored) {
    FX_LOGS(ERROR) << "One or more measurements had empty results. Quitting.";
    Done(EXIT_FAILURE);
    return;
  }

  if (!options_.benchmark_results_file.empty()) {
    for (auto& result : results) {
      result.test_suite = options_.test_suite;
    }
    if (!ExportResults(options_.benchmark_results_file, results)) {
      FX_LOGS(ERROR) << "Failed to write benchmark results to " << options_.benchmark_results_file;
      Done(EXIT_FAILURE);
      return;
    }
    out() << "Benchmark results written to " << options_.benchmark_results_file << std::endl;
  }

  Done(return_code_);
}

void RecordCommand::DoneTrace() {
  FX_DCHECK(!tracing_);

  tracer_.reset();
  exporter_.reset();

  out() << "Trace file written to " << options_.output_file_name << std::endl;

  if (measure_duration_ || measure_time_between_ || measure_argument_value_) {
    ProcessMeasurements();
  } else {
    Done(return_code_);
  }
}

// Quote elements of |args| as necessary to ensure the result can be correctly
// parsed by readers. But also do so minimally to maintain the S/N ratio.
// This is just a log message so the result doesn't need to be executable,
// this fact to avoid handling various complicated cases like one arg
// containing a mix of spaces, single quotes, and double quotes.
static std::string JoinArgsForLogging(const std::vector<std::string>& args) {
  std::string result;

  for (const auto& arg : args) {
    if (result.size() > 0) {
      result += " ";
    }
    if (arg.size() == 0) {
      result += "\"\"";
    } else if (arg.find(' ') != arg.npos) {
      result += "{" + arg + "}";
    } else {
      result += arg;
    }
  }

  return result;
}

void RecordCommand::LaunchComponentApp() {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = options_.app;
  launch_info.arguments = options_.args;

  // Include the arguments here for when invoked by traceutil: It's useful to
  // see how the passed command+args ended up after shell processing.
  FX_LOGS(INFO) << "Launching: " << launch_info.url << " " << JoinArgsForLogging(options_.args);

  fuchsia::sys::LauncherPtr launcher;
  if (options_.environment_name.has_value()) {
    fuchsia::sys::EnvironmentPtr environment;
    context()->svc()->Connect(environment.NewRequest());
    fuchsia::sys::EnvironmentPtr nested_environment;
    environment->CreateNestedEnvironment(
        nested_environment.NewRequest(), environment_controller_.NewRequest(),
        options_.environment_name.value(), nullptr,
        fuchsia::sys::EnvironmentOptions{/*inherit_parent_services*/ true,
                                         /*allow_parent_runners*/ true,
                                         /*kill_on_oom*/ true,
                                         /*delete_storage_on_death*/ true});

    nested_environment->GetLauncher(launcher.NewRequest());
  } else {
    context()->svc()->Connect(launcher.NewRequest());
  }
  launcher->CreateComponent(std::move(launch_info), component_controller_.NewRequest());

  component_controller_.set_error_handler([this](zx_status_t error) {
    out() << "Error launching component: " << error << "/" << zx_status_get_string(error)
          << std::endl;
    if (!options_.decouple)
      // The trace might have been already stopped by the |Wait()| callback. In
      // that case, |TerminateTrace| below does nothing.
      TerminateTrace(EXIT_FAILURE);
  });
  component_controller_.events().OnTerminated =
      [this](int64_t return_code, fuchsia::sys::TerminationReason termination_reason) {
        out() << "Application exited with return code " << return_code << std::endl;
        // Disable the error handler, the application has terminated. We can see things like
        // PEER_CLOSED for channels here which we don't care about any more.
        component_controller_.set_error_handler([](zx_status_t error) {});
        if (!options_.decouple) {
          if (options_.return_child_result) {
            TerminateTrace(return_code);
          } else {
            TerminateTrace(EXIT_SUCCESS);
          }
        }
      };
  if (options_.detach) {
    component_controller_->Detach();
  }
}

void RecordCommand::LaunchSpawnedApp() {
  std::vector<std::string> all_args = {options_.app};
  all_args.insert(all_args.end(), options_.args.begin(), options_.args.end());

  // Include the arguments here for when invoked by traceutil: It's useful to
  // see how the passed command+args ended up after shell processing.
  FX_LOGS(INFO) << "Spawning: " << JoinArgsForLogging(all_args);

  zx::process subprocess;
  zx_status_t status = Spawn(all_args, &subprocess);
  if (status != ZX_OK) {
    TerminateTrace(EXIT_FAILURE);
    FX_LOGS(ERROR) << "Subprocess launch failed: \"" << status
                   << "\" Did you provide the full path to the tool?";
    return;
  }

  spawned_app_ = std::move(subprocess);

  wait_spawned_app_.set_object(spawned_app_.get());
  status = wait_spawned_app_.Begin(dispatcher_);
  FX_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;
}

void RecordCommand::OnSpawnedAppExit(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                     zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to wait for spawned app: status=" << status;
    TerminateTrace(EXIT_FAILURE);
    return;
  }

  if (signal->observed & ZX_PROCESS_TERMINATED) {
    zx_info_process_t proc_info;
    [[maybe_unused]] zx_status_t info_status =
        spawned_app_.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr);
    FX_DCHECK(info_status == ZX_OK);

    out() << "Application exited with return code " << proc_info.return_code << std::endl;
    if (!options_.decouple) {
      if (options_.return_child_result) {
        TerminateTrace(proc_info.return_code);
      } else {
        TerminateTrace(EXIT_SUCCESS);
      }
    }
  } else {
    FX_NOTREACHED();
  }
}

void RecordCommand::KillSpawnedApp() {
  FX_DCHECK(spawned_app_);

  // If already dead this is a no-op.
  [[maybe_unused]] zx_status_t status = spawned_app_.kill();
  FX_DCHECK(status == ZX_OK);

  wait_spawned_app_.Cancel();
  wait_spawned_app_.set_object(ZX_HANDLE_INVALID);
}

void RecordCommand::OnAlert(std::string alert_name) {
  auto iter = options_.trigger_specs.find(alert_name);
  if (iter == options_.trigger_specs.end()) {
    // No action specified for alert. This is expected.
    return;
  }

  switch (iter->second) {
    case Action::kStop:
      TerminateTrace(EXIT_SUCCESS);
      break;
  }
}

void RecordCommand::StartTimer() {
  async::PostDelayedTask(
      dispatcher_,
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak)
          weak->TerminateTrace(EXIT_SUCCESS);
      },
      options_.duration);
  out() << "Starting trace; will stop in " << options_.duration.to_nsecs() / 1000000000.0
        << " seconds..." << std::endl;
}

}  // namespace tracing
