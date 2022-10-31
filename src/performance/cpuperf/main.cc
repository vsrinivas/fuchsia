// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <limits>

#include "print_tallies.h"
#include "session_result_spec.h"
#include "session_spec.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/performance/lib/perfmon/controller.h"
#include "src/performance/lib/perfmon/events.h"

const char kUsageString[] =
    "Usage: cpuperf [options]\n"
    "\n"
    "Options:\n"
    "  --spec-file=FILE   Use the cpuperf specification data in FILE\n"
    "  --help             Show this help message and exit\n"
    "  --list-events      Print the list of supported events\n"
    "  --describe-event=EVENT  Print a description of EVENT\n"
    "                     Event is specified as group:name\n"
    "\n"
    "Logging options:\n"
    "  --quiet[=LEVEL]    Set quietness level (opposite of verbose)\n"
    "  --verbose[=LEVEL]  Set debug verbosity level\n"
    "  --log-file=FILE    Write log output to FILE.\n"
    "Quiet supersedes verbose if both are specified.\n"
    "Defined log levels:\n"
    "-n - verbosity level n\n"
    " 0 - INFO - this is the default level\n"
    " 1 - WARNING\n"
    " 2 - ERROR\n"
    " 3 - FATAL\n";

static void PrintUsageString(FILE* f) { fputs(kUsageString, f); }

static bool GetSessionSpecFromArgv(const fxl::CommandLine& cl, cpuperf::SessionSpec* out_spec) {
  std::string arg;

  if (cl.GetOptionValue("spec-file", &arg)) {
    std::string content;
    if (!files::ReadFileToString(arg, &content)) {
      FX_LOGS(ERROR) << "Can't read spec file \"" << arg << "\"";
      return false;
    }
    if (!cpuperf::DecodeSessionSpec(content, out_spec)) {
      return false;
    }
  }

  return true;
}

static void DescribeEvent(FILE* f, const perfmon::EventDetails* details) {
  if (details->description[0] != '\0') {
    fprintf(f, "%s: %s\n", details->name, details->description);
  } else {
    // Print some kind of description for consistency.
    // The output in some sessions (e.g., emacs) gets colorized due to
    // the presence of colons, and it's harder to read without consistency.
    // Printing "missing description" will help encourage adding one. :-)
    fprintf(f, "%s: <missing description>\n", details->name);
  }
}

static void DescribeEvent(FILE* f, perfmon::ModelEventManager* model_event_manager,
                          const std::string& full_name) {
  std::vector<std::string> parts =
      fxl::SplitStringCopy(full_name, ":", fxl::kTrimWhitespace, fxl::kSplitWantAll);
  if (parts.size() != 2) {
    FX_LOGS(ERROR) << "Usage: cpuperf --describe-event=group:name";
    exit(EXIT_FAILURE);
  }

  const perfmon::EventDetails* details;
  if (!model_event_manager->LookupEventByName(parts[0].c_str(), parts[1].c_str(), &details)) {
    FX_LOGS(ERROR) << "Unknown event: " << full_name;
    exit(EXIT_FAILURE);
  }

  DescribeEvent(f, details);
}

static void PrintEventList(FILE* f, perfmon::ModelEventManager* model_event_manager) {
  perfmon::ModelEventManager::GroupTable groups = model_event_manager->GetAllGroups();

  for (auto& group : groups) {
    std::sort(group.events.begin(), group.events.end(),
              [](const perfmon::EventDetails*& a, const perfmon::EventDetails*& b) {
                return strcmp(a->name, b->name) < 0;
              });
    fprintf(f, "\nGroup %s\n", group.group_name.c_str());
    for (const auto& event : group.events) {
      DescribeEvent(f, event);
    }
  }
}

static void SaveTrace(const cpuperf::SessionResultSpec& result_spec,
                      perfmon::Controller* controller, size_t iter) {
  std::unique_ptr<perfmon::Reader> reader = controller->GetReader();
  if (!reader) {
    return;
  }

  FX_VLOGS(1) << "Saving results of iteration " << iter;

  for (size_t trace = 0; trace < result_spec.num_traces; ++trace) {
    if (reader->SetTrace(trace) != perfmon::ReaderStatus::kOk) {
      // If we can't set the trace to this one it's unlikely we can continue.
      return;
    }

    auto buffer = reinterpret_cast<const char*>(reader->GetCurrentTraceBuffer());
    FX_DCHECK(buffer);
    size_t size = reader->GetCurrentTraceSize();
    FX_DCHECK(size > 0);
    std::string output_file_path = result_spec.GetTraceFilePath(iter, trace);
    if (!files::WriteFile(output_file_path, buffer, size)) {
      FX_LOGS(ERROR) << "Error saving trace data to: " << output_file_path;
      // If writing this one fails, it's unlikely we can continue.
      return;
    }
  }

  // Print a summary of this run.
  // In tally mode this is noise, but if verbosity is on sure.
  if (controller->config().GetMode() != perfmon::CollectionMode::kTally || FX_VLOG_IS_ON(1)) {
    FX_VLOGS(1) << "Iteration " << iter << " summary";
    for (size_t trace = 0; trace < result_spec.num_traces; ++trace) {
      std::string path = result_spec.GetTraceFilePath(iter, trace);
      uint64_t size;
      if (files::GetFileSize(path, &size)) {
        FX_VLOGS(1) << path << ": " << size << " bytes";
      } else {
        FX_VLOGS(1) << path << ": unknown size";
      }
    }
  }
}

static bool RunSession(const cpuperf::SessionSpec& spec,
                       const perfmon::ModelEventManager* model_event_manager,
                       perfmon::Controller* controller) {
  cpuperf::SessionResultSpec result_spec{spec.config_name, spec.model_name, spec.num_iterations,
                                         controller->num_traces(), spec.output_path_prefix};

  for (size_t iter = 0; iter < spec.num_iterations; ++iter) {
    if (!controller->Start()) {
      return false;
    }

    zx::nanosleep(zx::deadline_after(spec.duration));

    controller->Stop();

    // Save the trace, even if printing results, for testing purposes.
    if (result_spec.save_results()) {
      SaveTrace(result_spec, controller, iter);
    }

    if (controller->config().GetMode() == perfmon::CollectionMode::kTally) {
      PrintTallyResults(stdout, spec, result_spec, model_event_manager, controller);
    }
  }

  if (result_spec.save_results()) {
    if (!cpuperf::WriteSessionResultSpec(spec.session_result_spec_path, result_spec)) {
      return false;
    }
  }

  return true;
}

static bool GetBufferSizeInPages(uint32_t size_in_mb, uint32_t* out_num_pages) {
  const uint64_t kPagesPerMb = 1024 * 1024 / perfmon::Controller::kPageSize;
  const uint64_t kMaxSizeInMb = std::numeric_limits<uint32_t>::max() / kPagesPerMb;
  if (size_in_mb > kMaxSizeInMb) {
    return false;
  }
  *out_num_pages = size_in_mb * kPagesPerMb;
  return true;
}

int main(int argc, char* argv[]) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString(stdout);
    return EXIT_SUCCESS;
  }

  if (cl.HasOption("list-events", nullptr) || cl.HasOption("describe-event", nullptr)) {
    // For list-events and describe-event, just support the default model
    // for now.
    std::unique_ptr<perfmon::ModelEventManager> model_event_manager =
        perfmon::ModelEventManager::Create(perfmon::GetDefaultModelName());
    FX_CHECK(model_event_manager);

    if (cl.HasOption("list-events", nullptr)) {
      PrintEventList(stdout, model_event_manager.get());
      return EXIT_SUCCESS;
    }

    std::string arg;
    if (cl.GetOptionValue("describe-event", &arg)) {
      DescribeEvent(stdout, model_event_manager.get(), arg);
      return EXIT_SUCCESS;
    }
  }

  // TODO(dje): dump-arch option
  // TODO(dje): Command line options for parts of the spec.

  cpuperf::SessionSpec spec;
  if (!GetSessionSpecFromArgv(cl, &spec)) {
    return EXIT_FAILURE;
  }

  if (spec.perfmon_config.GetEventCount() == 0) {
    FX_LOGS(ERROR) << "No events specified";
    return EXIT_FAILURE;
  }

  // The provided buffer size is in MB, the controller takes the buffer size
  // in pages.
  uint32_t buffer_size_in_pages;
  if (!GetBufferSizeInPages(spec.buffer_size_in_mb, &buffer_size_in_pages)) {
    FX_LOGS(ERROR) << "Buffer size too large";
    exit(EXIT_FAILURE);
  }

  std::unique_ptr<perfmon::Controller> controller;
  if (!perfmon::Controller::Create(buffer_size_in_pages, spec.perfmon_config, &controller)) {
    return EXIT_FAILURE;
  }

  FX_LOGS(INFO) << "cpuperf control program starting";
  FX_LOGS(INFO) << spec.num_iterations << " iteration(s), " << spec.duration.to_secs()
                << " second(s) per iteration";

  bool success = RunSession(spec, spec.model_event_manager.get(), controller.get());

  if (!success) {
    FX_LOGS(INFO) << "cpuperf exiting with error";
    return EXIT_FAILURE;
  }

  FX_LOGS(INFO) << "cpuperf control program exiting";
  return EXIT_SUCCESS;
}
