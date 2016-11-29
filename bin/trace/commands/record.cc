// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>

#include "apps/tracing/src/trace/commands/record.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tracing {

bool Record::Options::Setup(const ftl::CommandLine& command_line) {
  size_t index = 0;
  // --categories=<cat1>,<cat2>,...
  if (command_line.HasOption("categories", &index)) {
    categories =
        ftl::SplitStringCopy(command_line.options()[index].value, ",",
                             ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);
  }

  // --output-file=<file>
  if (command_line.HasOption("output-file", &index)) {
    output_file_name = command_line.options()[index].value;
  }

  // --duration=<seconds>
  if (command_line.HasOption("duration", &index)) {
    uint64_t seconds;
    if (!ftl::StringToNumberWithError(command_line.options()[index].value,
                                      &seconds)) {
      FTL_LOG(ERROR) << "Failed to parse command-line option duration: "
                     << command_line.options()[index].value;
      return false;
    }
    duration = ftl::TimeDelta::FromSeconds(seconds);
  }

  // --detach
  detach = command_line.HasOption("detach");

  // --decouple
  decouple = command_line.HasOption("decouple");

  // --buffer-size=<megabytes>
  if (command_line.HasOption("buffer-size", &index)) {
    uint32_t megabytes;
    if (!ftl::StringToNumberWithError(command_line.options()[index].value,
                                      &megabytes)) {
      FTL_LOG(ERROR) << "Failed to parse command-line option buffer-size: "
                     << command_line.options()[index].value;
      return false;
    }
    buffer_size_megabytes_hint = megabytes;
  }

  // <command> <args...>
  const auto& positional_args = command_line.positional_args();
  if (!positional_args.empty()) {
    launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = fidl::String::From(positional_args[0]);
    launch_info->arguments =
        fidl::Array<fidl::String>::From(std::vector<std::string>(
            positional_args.begin() + 1, positional_args.end()));
  }

  return true;
}

Command::Info Record::Describe() {
  return Command::Info{
      [](modular::ApplicationContext* context) {
        return std::make_unique<Record>(context);
      },
      "record",
      "starts tracing and records data",
      {{"output-file=[/tmp/trace.json]", "Trace data is stored in this file"},
       {"duration=[10s]",
        "Trace will be active for this long after the session has been "
        "started"},
       {"categories=[\"\"]", "Categories that should be enabled for tracing"},
       {"detach=[false]",
        "Don't stop the traced program when tracing finished"},
       {"decouple=[false]", "Don't stop tracing when the traced program exits"},
       {"buffer-size=[4]",
        "Maximum size of trace buffer for each provider in megabytes"},
       {"[command args]",
        "Run program before starting trace. The program is terminated when "
        "tracing ends unless --detach is specified"}}};
}

Record::Record(modular::ApplicationContext* context)
    : CommandWithTraceController(context), weak_ptr_factory_(this) {}

void Record::Run(const ftl::CommandLine& command_line) {
  if (!options_.Setup(command_line)) {
    err() << "Error parsing options from command line - aborting" << std::endl;
    exit(1);
  }

  std::ofstream out_file(options_.output_file_name,
                         std::ios_base::out | std::ios_base::trunc);
  if (!out_file.is_open()) {
    err() << "Failed to open " << options_.output_file_name << " for writing"
          << std::endl;
    exit(1);
  }

  exporter_.reset(new ChromiumExporter(std::move(out_file)));
  tracer_.reset(new Tracer(trace_controller().get()));

  tracing_ = true;

  auto trace_options = TraceOptions::New();
  trace_options->categories =
      fidl::Array<fidl::String>::From(options_.categories);
  trace_options->buffer_size_megabytes_hint =
      options_.buffer_size_megabytes_hint;

  tracer_->Start(
      std::move(trace_options),
      [this](const reader::Record& record) { exporter_->ExportRecord(record); },
      [](std::string error) { err() << error; },
      [this] {
        if (options_.launch_info)
          LaunchApp();
        StartTimer();
      },
      [this] { DoneTrace(); });
}

void Record::StopTrace() {
  if (tracing_) {
    out() << "Stopping trace..." << std::endl;
    tracing_ = false;
    tracer_->Stop();
  }
}

void Record::DoneTrace() {
  tracer_.reset();
  exporter_.reset();

  out() << "Trace file written to " << options_.output_file_name << std::endl;
  mtl::MessageLoop::GetCurrent()->QuitNow();
}

void Record::LaunchApp() {
  out() << "Launching " << options_.launch_info->url;
  context()->launcher()->CreateApplication(std::move(options_.launch_info),
                                           GetProxy(&application_controller_));
  application_controller_.set_connection_error_handler([this] {
    out() << "Application terminated";
    if (!options_.decouple)
      StopTrace();
  });
  if (options_.detach)
    application_controller_->Detach();
}

void Record::StartTimer() {
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak)
          weak->StopTrace();
      },
      options_.duration);
  out() << "Starting trace; will stop in " << options_.duration.ToSecondsF()
        << " seconds..." << std::endl;
}

}  // namespace tracing
