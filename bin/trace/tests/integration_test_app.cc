// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains several "tests" that exercise tracing functionality.
// Each test is composed of two pieces: a runner and a verifier.
// Each test is spawned by trace_system_test twice: once to run the runner
// and once to run the verifier. When run as a "runner" this program is
// actually spawned by "trace record". When run as a "verifier", this program
// is invoked directly by trace_system_test.
// See |kUsageString| for usage instructions.

#include <fstream>
#include <iostream>
#include <stdlib.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/spawn.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/log_settings.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <trace/event.h>
#include <trace-provider/provider.h>
#include <zircon/status.h>

#include "garnet/bin/trace/spec.h"

const char kUsageString[] = {
  "Test runner usage:\n"
  "  integration_test_app [options] run tspec-file\n"
  "\n"
  "Test verifier usage:\n"
  "  integration_test_app [options] verify tspec-file trace-output-file\n"
  "\n"
  "Options:\n"
  "  --quiet[=LEVEL]    set quietness level (opposite of verbose)\n"
  "  --verbose[=LEVEL]  set debug verbosity level\n"
  "  --log-file=FILE    write log output to FILE\n"
};

// The name of the trace events member in the json output file.
const char kTraceEventsMemberName[] = "traceEvents";

// The name of the category member in the json output file.
const char kCategoryMemberName[] = "cat";

// The name of the event name member in the json output file.
const char kEventNameMemberName[] = "name";

// Category for events we generate.
#define CATEGORY_NAME "trace:test"

// Name to use in instant events.
#define INSTANT_EVENT_NAME "instant"

// Approximate size in bytes of the records we emit for the tests.
// We assume strings and thread references are not inlined. If they are that's
// ok. The point is this value is the minimum size of the record we're going to
// emit. If the record is larger then the trace will be larger, which is ok.
// If it's smaller we risk not stress-testing things enough.
// header-word(8) + ticks(8) + 3 arguments (= 3 * (8 + 8)) = 64
constexpr size_t kApproxRecordSize = 64;

using TestRunner = bool (const tracing::Spec& spec,
                         async_dispatcher_t* dispatcher);
using TestVerifier = bool (const tracing::Spec& spec,
                           const std::string& test_output_file);

struct TestFunctions {
  const char* name;
  TestRunner* run;
  TestVerifier* verify;
};

static bool RunFillBufferTest(const tracing::Spec& spec,
                              async_dispatcher_t* dispatcher) {
  trace::TraceProvider provider(dispatcher);
  FXL_DCHECK(provider.is_valid());
  // Until we have synchronous registration, give registration time to happen.
  zx::nanosleep(zx::deadline_after(zx::sec(1)));

  // Generate at least 4MB of test records.
  // This stress tests streaming mode buffer saving (with buffer size of 1MB).
  constexpr size_t kMinNumBuffersFilled = 4;

  FXL_DCHECK(spec.buffer_size_in_mb);
  size_t buffer_size = *spec.buffer_size_in_mb * 1024 * 1024;
  size_t kNumIterations = buffer_size / kApproxRecordSize;

  for (size_t i = 0; i < kMinNumBuffersFilled; ++i) {
    if (i > 0) {
      // The buffer is roughly full at this point.
      // Give TraceManager some time to catch up (but not too much time).
      zx::nanosleep(zx::deadline_after(zx::sec(1)));
    }
    for (size_t j = 0; j < kNumIterations; ++j) {
      TRACE_INSTANT(CATEGORY_NAME, INSTANT_EVENT_NAME, TRACE_SCOPE_PROCESS,
                    "arg1", 1, "arg2", 2, "arg3", 3);
    }
  }

  return true;
}

static bool VerifyFillBufferTest(const tracing::Spec& spec,
                                 const std::string& test_output_file) {
  // We don't know how many records got dropped, but we can count them,
  // verify they are what we expect.
  std::ifstream in(test_output_file);
  rapidjson::IStreamWrapper isw(in);
  rapidjson::Document document;

  if (!document.ParseStream(isw).IsObject()) {
    FXL_LOG(ERROR) << "Failed to parse JSON object from: " << test_output_file;
    if (document.HasParseError()) {
      FXL_LOG(ERROR) << "Parse error "
                     << GetParseError_En(document.GetParseError()) << " ("
                     << document.GetErrorOffset() << ")";
    }
    return false;
  }

  auto events_it = document.FindMember(kTraceEventsMemberName);
  if (events_it == document.MemberEnd()) {
    FXL_LOG(ERROR) << "Member not found: " << kTraceEventsMemberName;
    return false;
  }
  const auto& value = events_it->value;
  if (!value.IsArray()) {
    FXL_LOG(ERROR) << "Trace events is not an array";
    return false;
  }

  const auto& array = value.GetArray();
  for (size_t i = 0; i < array.Size(); ++i) {
    if (!array[i].IsObject()) {
      FXL_LOG(ERROR) << "Event " << i << " is not an object";
      return false;
    }

    const auto& event = array[i];
    auto cat_it = event.FindMember(kCategoryMemberName);
    if (cat_it == event.MemberEnd()) {
      FXL_LOG(ERROR) << "Category not present in event";
      return false;
    }
    const auto& category_name = cat_it->value;
    if (!category_name.IsString()) {
      FXL_LOG(ERROR) << "Category name is not a string";
      return false;
    }
    if (strcmp(category_name.GetString(), CATEGORY_NAME) != 0) {
      FXL_LOG(ERROR) << "Expected category not present in event, got: "
                     << category_name.GetString();
      return false;
    }

    auto name_it = event.FindMember(kEventNameMemberName);
    if (name_it == event.MemberEnd()) {
      FXL_LOG(ERROR) << "Event name not present in event";
      return false;
    }
    const auto& event_name = name_it->value;
    if (!event_name.IsString()) {
      FXL_LOG(ERROR) << "Event name is not a string";
      return false;
    }
    if (strcmp(event_name.GetString(), INSTANT_EVENT_NAME) != 0) {
      FXL_LOG(ERROR) << "Expected event not present in event, got: "
                     << event_name.GetString();
      return false;
    }
  }

  FXL_VLOG(1) << array.Size() << " trace events present";

  double percentage_buffer_filled;
  FXL_DCHECK(spec.buffering_mode);
  if (*spec.buffering_mode == "streaming") {
    // We should have saved at least one buffer's worth of events.
    percentage_buffer_filled = 1.0;
  } else {
    // We should have saved at least 80% buffer's worth of events.
    // This is conservative to avoid having a flaky test.
    percentage_buffer_filled = 0.8;
  }

  FXL_DCHECK(spec.buffer_size_in_mb);
  size_t buffer_size = *spec.buffer_size_in_mb * 1024 * 1024;
  size_t min_event_count =
    (percentage_buffer_filled * buffer_size) / kApproxRecordSize;
  if (array.Size() < min_event_count) {
    FXL_LOG(ERROR) << "Insufficient number of events present, got "
                   << array.Size() << ", expected at least "
                   << min_event_count;
    return false;
  }

  return true;
}

// At the moment we only have a basic test that fills the buffer several times
// over. This is useful for stress testing  all three buffering modes via
// tspec files.
// TODO(dje): Later we can add tests that emit different kinds of trace
// records or whatever.
const TestFunctions kTestFunctions[] = {
  {
    "fill-buffer",
    &RunFillBufferTest,
    &VerifyFillBufferTest,
  },
};

static const TestFunctions* LookupTest(const std::string& test_name) {
  for (const auto& test : kTestFunctions) {
    if (test.name == test_name)
      return &test;
  }
  return nullptr;
}

static int RunTest(const tracing::Spec& spec, TestRunner* run) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  loop.StartThread("provider-thread", nullptr);

  bool success = run(spec, loop.dispatcher());

  loop.Quit();
  loop.JoinThreads();
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int VerifyTest(const tracing::Spec& spec, TestVerifier* verify,
                      const std::string& test_output_file) {
  if (!verify(spec, test_output_file))
    return EXIT_FAILURE;
  return EXIT_SUCCESS;
}

static void PrintUsageString() { std::cout << kUsageString << std::endl; }

int main(int argc, char *argv[]) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);

  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  auto args = cl.positional_args();

  if (args.size() == 0) {
    FXL_LOG(ERROR) << "Missing command";
    return EXIT_FAILURE;
  }

  const std::string& command = args[0];
  if (command == "run") {
    if (args.size() != 2) {
      FXL_LOG(ERROR) << "Wrong number of arguments to run invocation";
      return EXIT_FAILURE;
    }
  } else if (command == "verify") {
    if (args.size() != 3) {
      FXL_LOG(ERROR) << "Wrong number of arguments to verify invocation";
      return EXIT_FAILURE;
    }
  } else {
    FXL_LOG(ERROR) << "Unknown command: " << command;
    return EXIT_FAILURE;
  }

  const std::string& spec_file_path = args[1];
  std::string spec_file_contents;
  if (!files::ReadFileToString(spec_file_path, &spec_file_contents)) {
    FXL_LOG(ERROR) << "Can't read test spec: " << spec_file_path;
    return EXIT_FAILURE;
  }

  tracing::Spec spec;
  if (!tracing::DecodeSpec(spec_file_contents, &spec)) {
    FXL_LOG(ERROR) << "Error decoding test spec: " << spec_file_path;
    return EXIT_FAILURE;
  }

  FXL_DCHECK(spec.test_name);
  auto test_name = *spec.test_name;

  auto test = LookupTest(test_name);
  if (test == nullptr) {
    FXL_LOG(ERROR) << "Unknown test name: " << test_name;
    return EXIT_FAILURE;
  }

  if (command == "run") {
    FXL_VLOG(1) << "Running subprogram for test " << spec_file_path
                << ":\"" << test_name << "\"";
    return RunTest(spec, test->run);
  } else {
    FXL_VLOG(1) << "Verifying test " << spec_file_path
                << ":\"" << test_name << "\"";
    return VerifyTest(spec, test->verify, args[2]);
  }
}
