// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/commands/record.h"

#include <errno.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/time.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <third_party/zlib/contrib/iostream3/zfstream.h>
#include <zircon/status.h>

#include <fstream>
#include <string>
#include <unordered_set>

#include "garnet/bin/trace/results_export.h"
#include "garnet/bin/trace/results_output.h"
#include "lib/fsl/types/type_converters.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/lib/fxl/strings/trim.h"

namespace tracing {

namespace {

// Result of ParseBooleanOption.
enum class OptionStatus {
  PRESENT,
  NOT_PRESENT,
  ERROR,
};

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
const char kReturnChildResult[] = "return-child-result";
const char kBufferSize[] = "buffer-size";
const char kProviderBufferSize[] = "provider-buffer-size";
const char kBufferingMode[] = "buffering-mode";
const char kBenchmarkResultsFile[] = "benchmark-results-file";
const char kTestSuite[] = "test-suite";

const char kTcpPrefix[] = "tcp:";

const struct {
  const char* name;
  fuchsia::tracing::controller::BufferingMode mode;
} kBufferingModes[] = {
    {"oneshot", fuchsia::tracing::controller::BufferingMode::ONESHOT},
    {"circular", fuchsia::tracing::controller::BufferingMode::CIRCULAR},
    {"streaming", fuchsia::tracing::controller::BufferingMode::STREAMING},
};

static bool BeginsWith(fxl::StringView str, fxl::StringView prefix,
                       fxl::StringView* arg) {
  size_t prefix_size = prefix.size();
  if (str.size() < prefix_size)
    return false;
  if (str.substr(0, prefix_size) != prefix)
    return false;
  *arg = str.substr(prefix_size);
  return true;
}

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

bool WaitForExit(zx_handle_t process, int* return_code) {
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

  *return_code = proc_info.return_code;
  return true;
}

bool LookupBufferingMode(
    const std::string& mode_name,
    fuchsia::tracing::controller::BufferingMode* out_mode) {
  for (const auto& mode : kBufferingModes) {
    if (mode_name == mode.name) {
      *out_mode = mode.mode;
      return true;
    }
  }
  return false;
}

OptionStatus ParseBooleanOption(const fxl::CommandLine& command_line,
                                const char* name, bool* out_value) {
  std::string arg;
  bool have_option = command_line.GetOptionValue(fxl::StringView(name), &arg);

  if (!have_option) {
    return OptionStatus::NOT_PRESENT;
  }

  if (arg == "" || arg == "true") {
    *out_value = true;
  } else if (arg == "false") {
    *out_value = false;
  } else {
    FXL_LOG(ERROR) << "Bad value for --" << name
                   << " option, pass true or false";
    return OptionStatus::ERROR;
  }

  return OptionStatus::PRESENT;
}

void CheckCommandLineOverride(const char* name, bool present_in_spec) {
  if (present_in_spec) {
    FXL_LOG(WARNING) << "The " << name << " passed on the command line"
                     << "override value(s) from the tspec file.";
  }
}

// Helper so call sites don't have to type !!object_as_unique_ptr.
template <typename T>
void CheckCommandLineOverride(const char* name, const T& object) {
  CheckCommandLineOverride(name, !!object);
}

bool CheckBufferSize(uint32_t megabytes) {
  if (megabytes < kMinBufferSizeMegabytes ||
      megabytes > kMaxBufferSizeMegabytes) {
    FXL_LOG(ERROR) << "Buffer size not between " << kMinBufferSizeMegabytes
                   << "," << kMaxBufferSizeMegabytes << ": " << megabytes;
    return false;
  }
  return true;
}

}  // namespace

bool Record::Options::Setup(const fxl::CommandLine& command_line) {
  const std::unordered_set<std::string> known_options = {
      kSpecFile,             kCategories,         kAppendArgs,
      kOutputFile,           kBinary,             kCompress,
      kDuration,             kDetach,             kDecouple,
      kSpawn,                kReturnChildResult,
      kBufferSize,           kProviderBufferSize, kBufferingMode,
      kBenchmarkResultsFile, kTestSuite};

  for (auto& option : command_line.options()) {
    if (known_options.count(option.name) == 0) {
      FXL_LOG(ERROR) << "Unknown option: " << option.name;
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
      FXL_LOG(ERROR) << spec_file_path << " is not a file";
      return false;
    }

    std::string content;
    if (!files::ReadFileToString(spec_file_path, &content)) {
      FXL_LOG(ERROR) << "Can't read " << spec_file_path;
      return false;
    }

    if (!DecodeSpec(content, &spec)) {
      FXL_LOG(ERROR) << "Can't decode " << spec_file_path;
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
    if (spec.categories)
      categories = *spec.categories;
    if (spec.buffering_mode) {
      if (!LookupBufferingMode(*spec.buffering_mode, &buffering_mode)) {
        FXL_LOG(ERROR) << "Unknown spec parameter buffering-mode: "
                       << spec.buffering_mode;
        return false;
      }
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
  }

  // --categories=<cat1>,<cat2>,...
  if (command_line.HasOption(kCategories, &index)) {
    categories =
        fxl::SplitStringCopy(command_line.options()[index].value, ",",
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
      auto args = fxl::SplitStringCopy(arg, ",", fxl::kTrimWhitespace,
                                       fxl::kSplitWantNonEmpty);
      std::move(std::begin(args), std::end(args),
                std::back_inserter(append_args));
    }
  }

  // --binary
  if (ParseBooleanOption(command_line, kBinary, &binary) ==
          OptionStatus::ERROR) {
    return false;
  }
  if (binary) {
    output_file_name = kDefaultBinaryOutputFileName;
  }

  // --compress
  if (ParseBooleanOption(command_line, kCompress, &compress) ==
          OptionStatus::ERROR) {
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
    if (!fxl::StringToNumberWithError(command_line.options()[index].value,
                                      &seconds)) {
      FXL_LOG(ERROR) << "Failed to parse command-line option " << kDuration
                     << ": " << command_line.options()[index].value;
      return false;
    }
    duration = fxl::TimeDelta::FromSeconds(seconds);
    CheckCommandLineOverride("duration", spec.duration);
  }

  // --detach
  if (ParseBooleanOption(command_line, kDetach, &detach) ==
          OptionStatus::ERROR) {
    return false;
  }

  // --decouple
  if (ParseBooleanOption(command_line, kDecouple, &decouple) ==
          OptionStatus::ERROR) {
    return false;
  }

  // --spawn
  {
    bool spawn_value = false;
    OptionStatus spawn_status =
        ParseBooleanOption(command_line, kSpawn, &spawn_value);
    if (spawn_status == OptionStatus::ERROR) {
      return false;
    }
    bool have_spawn = spawn_status == OptionStatus::PRESENT;
    if (have_spawn) {
      spawn = spawn_value;
      CheckCommandLineOverride("spawn", spec.spawn);
    }
  }

  // --return-child-result=<flag>
  if (ParseBooleanOption(command_line, kReturnChildResult,
                         &return_child_result) ==
          OptionStatus::ERROR) {
    return false;
  }

  // --buffer-size=<megabytes>
  if (command_line.HasOption(kBufferSize, &index)) {
    uint32_t megabytes;
    if (!fxl::StringToNumberWithError(command_line.options()[index].value,
                                      &megabytes)) {
      FXL_LOG(ERROR) << "Failed to parse command-line option " << kBufferSize
                     << ": " << command_line.options()[index].value;
      return false;
    }
    if (!CheckBufferSize(megabytes)) {
      return false;
    }
    buffer_size_megabytes = megabytes;
    CheckCommandLineOverride("buffer-size", spec.buffer_size_in_mb);
  }

  // --provider-buffer-size=<name:megabytes>
  if (command_line.HasOption(kProviderBufferSize)) {
    std::vector<fxl::StringView> args =
        command_line.GetOptionValues(kProviderBufferSize);
    for (const auto& arg : args) {
      size_t colon = arg.rfind(':');
      if (colon == arg.npos) {
        FXL_LOG(ERROR) << "Syntax error in " << kProviderBufferSize
                       << ": should be provider-name:buffer_size_in_mb";
        return false;
      }
      uint32_t megabytes;
      if (!fxl::StringToNumberWithError(arg.substr(colon + 1), &megabytes)) {
        FXL_LOG(ERROR) << "Failed to parse buffer size: " << arg;
        return false;
      }
      if (!CheckBufferSize(megabytes)) {
        return false;
      }
      // We can't verify the provider name here, all we can do is pass it on.
      std::string name = arg.substr(0, colon).ToString();
      provider_specs.emplace_back(ProviderSpec{name, megabytes});
      CheckCommandLineOverride("provider-specs", spec.provider_specs);
    }
  }

  // --buffering-mode=oneshot|circular|streaming
  if (command_line.HasOption(kBufferingMode, &index)) {
    if (!LookupBufferingMode(command_line.options()[index].value,
                             &buffering_mode)) {
      FXL_LOG(ERROR) << "Failed to parse command-line option " << kBufferingMode
                     << ": " << command_line.options()[index].value;
      return false;
    }
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

  // <command> <args...>
  const auto& positional_args = command_line.positional_args();
  if (!positional_args.empty()) {
    app = positional_args[0];
    args = std::vector<std::string>(positional_args.begin() + 1,
                                    positional_args.end());
    CheckCommandLineOverride("app,args", spec.app || spec.args);
  }

  // Now that we've processed positional args we can append --append-args args.
  std::move(std::begin(append_args), std::end(append_args),
            std::back_inserter(args));

  return true;
}

Command::Info Record::Describe() {
  return Command::Info{
      [](sys::ComponentContext* context) {
        return std::make_unique<Record>(context);
      },
      "record",
      "starts tracing and records data",
      {{"spec-file=[none]", "Tracing specification file"},
       {"output-file=[/data/trace.json]",
        "Trace data is stored in this file. "
        "If the output file is \"tcp:TCP-ADDRESS\" then the output is streamed "
        "to that address. This option is generally only used by traceutil."},
       {"binary=[false]",
        "Output the binary trace rather than converting to JSON. "
        "If this is set, then the default output location will be "
        "/data/trace.fxt"},
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
       {"detach=[false]",
        "Don't stop the traced program when tracing finished"},
       {"decouple=[false]", "Don't stop tracing when the traced program exits"},
       {"spawn=[false]",
        "Use fdio_spawn to run a legacy app. Detach will have no effect when "
        "using this option."},
       {"return-child-result=[true]",
        "Return with the same return code as the child. "
        "Only valid when a child program is passed."},
       {"buffer-size=[4]",
        "Maximum size of trace buffer for each provider in megabytes"},
       {"provider-buffer-size=[provider-name:buffer-size]",
        "Specify the buffer size that \"provider-name\" will use. "
        "May be specified multiple times, once per provider."},
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
        "Run program after starting trace. The program is terminated when "
        "tracing ends unless --detach is specified"}}};
}

Record::Record(sys::ComponentContext* context)
    : CommandWithController(context), weak_ptr_factory_(this) {}

static bool TcpAddrFromString(fxl::StringView address, fxl::StringView port,
                              addrinfo* out_addr) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

  addrinfo* addrinfos;
  int errcode = getaddrinfo(address.ToString().c_str(), port.ToString().c_str(),
                            &hints, &addrinfos);
  if (errcode != 0) {
    FXL_LOG(ERROR) << "Failed to getaddrinfo for address " << address << ":"
                   << port << ": " << gai_strerror(errcode);
    return false;
  }
  if (addrinfos == nullptr) {
    FXL_LOG(ERROR) << "No matching addresses found for " << address << ":"
                   << port;
    return false;
  }

  *out_addr = *addrinfos;
  freeaddrinfo(addrinfos);
  return true;
}

static std::unique_ptr<std::ostream> ConnectToTraceSaver(
    fxl::StringView address) {
  FXL_LOG(INFO) << "Connecting to " << address;

  size_t colon = address.rfind(':');
  if (colon == address.npos) {
    FXL_LOG(ERROR) << "TCP address is missing port: " << address;
    return nullptr;
  }

  fxl::StringView ip_addr_str(address.substr(0, colon));
  fxl::StringView port_str(address.substr(colon + 1));

  // [::1] -> ::1
  ip_addr_str = fxl::TrimString(ip_addr_str, "[]");

  addrinfo tcp_addr;
  if (!TcpAddrFromString(ip_addr_str, port_str, &tcp_addr)) {
    return nullptr;
  }

  fxl::UniqueFD fd(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
  if (!fd.is_valid()) {
    FXL_LOG(ERROR) << "Failed to create socket: " << strerror(errno);
    return nullptr;
  }

  if (connect(fd.get(), tcp_addr.ai_addr, tcp_addr.ai_addrlen) < 0) {
    FXL_LOG(ERROR) << "Failed to connect: " << strerror(errno);
    return nullptr;
  }

  auto ofstream = std::make_unique<std::ofstream>();
  ofstream->__open(fd.release(), std::ios_base::out);
  FXL_DCHECK(ofstream->is_open());
  return ofstream;
}

static std::unique_ptr<std::ostream> OpenOutputStream(
    const std::string& output_file_name, bool compress) {
  std::unique_ptr<std::ostream> out_stream;
  fxl::StringView address;
  if (BeginsWith(output_file_name, kTcpPrefix, &address)) {
    out_stream = ConnectToTraceSaver(address);
  } else if (compress) {
    // TODO(dje): Compressing a network stream is not supported.
    auto gzstream = std::make_unique<gzofstream>(
        output_file_name.c_str(), std::ios_base::out | std::ios_base::trunc);
    if (gzstream->is_open()) {
      out_stream = std::move(gzstream);
    }
  } else {
    auto ofstream = std::make_unique<std::ofstream>(
        output_file_name, std::ios_base::out | std::ios_base::trunc);
    if (ofstream->is_open()) {
      out_stream = std::move(ofstream);
    }
  }
  return out_stream;
}

void Record::Start(const fxl::CommandLine& command_line) {
  if (!options_.Setup(command_line)) {
    FXL_LOG(ERROR) << "Error parsing options from command line - aborting";
    Done(1);
    return;
  }

  std::unique_ptr<std::ostream> out_stream =
      OpenOutputStream(options_.output_file_name, options_.compress);
  if (!out_stream) {
    FXL_LOG(ERROR) << "Failed to open " << options_.output_file_name
                   << " for writing";
    Done(1);
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
    error_handler = [](fbl::String error) { FXL_LOG(ERROR) << error.c_str(); };
  }

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

  fuchsia::tracing::controller::TraceOptions trace_options;
  trace_options.set_categories(options_.categories);
  trace_options.set_buffer_size_megabytes_hint(options_.buffer_size_megabytes);
  // TODO(dje): start_timeout_milliseconds
  trace_options.set_buffering_mode(options_.buffering_mode);

  // Uniquify the list, with later entries overriding earlier entries.
  std::map<std::string, uint32_t> provider_specs;
  for (const auto& it : options_.provider_specs) {
    provider_specs[it.name] = it.buffer_size_in_mb;
  }
  std::vector<fuchsia::tracing::controller::ProviderSpec>
      uniquified_provider_specs;
  for (const auto& it : provider_specs) {
    fuchsia::tracing::controller::ProviderSpec spec;
    spec.set_name(it.first);
    spec.set_buffer_size_megabytes_hint(it.second);
    uniquified_provider_specs.push_back(std::move(spec));
  }
  trace_options.set_provider_specs(std::move(uniquified_provider_specs));

  tracer_->Start(
      std::move(trace_options), options_.binary, std::move(bytes_consumer),
      std::move(record_consumer), std::move(error_handler),
      [this] {
        if (!options_.app.empty())
          options_.spawn ? LaunchTool() : LaunchApp();
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
    if (result.values.empty()) {
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

// Quote elements of |args| as necessary to ensure the result can be correctly
// parsed by readers. But also do so minimally to maintain the S/N ratio.
// This is just a log message so the result doesn't need to be executable,
// this fact to avoid handling various complicated cases like one arg
// containing a mix of spaces, single quotes, and double quotes.
static std::string JoinArgsForLogging(
    const std::vector<std::string>& args) {
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

void Record::LaunchApp() {
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = fidl::StringPtr(options_.app);
  launch_info.arguments = fidl::To<fidl::VectorPtr<std::string>>(options_.args);

  // Include the arguments here for when invoked by traceutil: It's useful to
  // see how the passed command+args ended up after shell processing.
  FXL_LOG(INFO) << "Launching: " << launch_info.url << " "
                << JoinArgsForLogging(options_.args);

  fuchsia::sys::LauncherPtr launcher;
  context()->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info),
                            component_controller_.NewRequest());

  component_controller_.set_error_handler([this](zx_status_t error) {
    out() << "Error launching component: " << error << "/"
          << zx_status_get_string(error) << std::endl;
    if (!options_.decouple)
      // The trace might have been already stopped by the |Wait()| callback. In
      // that case, |StopTrace| below does nothing.
      StopTrace(-1);
  });
  component_controller_.events().OnTerminated =
      [this](int64_t return_code,
             fuchsia::sys::TerminationReason termination_reason) {
        out() << "Application exited with return code " << return_code
              << std::endl;
        if (!options_.decouple) {
          if (options_.return_child_result) {
            StopTrace(return_code);
          } else {
            StopTrace(0);
          }
        }
      };
  if (options_.detach) {
    component_controller_->Detach();
  }
}

void Record::LaunchTool() {
  std::vector<std::string> all_args = {options_.app};
  all_args.insert(all_args.end(), options_.args.begin(), options_.args.end());

  // Include the arguments here for when invoked by traceutil: It's useful to
  // see how the passed command+args ended up after shell processing.
  FXL_LOG(INFO) << "Spawning: " << JoinArgsForLogging(all_args);

  zx_handle_t process = Launch(all_args);
  if (process == ZX_HANDLE_INVALID) {
    StopTrace(-1);
    FXL_LOG(ERROR) << "Unable to launch " << options_.app;
    return;
  }

  int return_code = -1;
  if (!WaitForExit(process, &return_code))
    FXL_LOG(ERROR) << "Unable to get return code";

  out() << "Application exited with return code " << return_code << std::endl;
  if (!options_.decouple) {
    if (options_.return_child_result) {
      StopTrace(return_code);
    } else {
      StopTrace(0);
    }
  }
}

void Record::StartTimer() {
  async::PostDelayedTask(
      async_get_default_dispatcher(),
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak)
          weak->StopTrace(0);
      },
      zx::nsec(options_.duration.ToNanoseconds()));
  out() << "Starting trace; will stop in " << options_.duration.ToSecondsF()
        << " seconds..." << std::endl;
}

}  // namespace tracing
