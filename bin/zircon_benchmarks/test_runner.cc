// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex.h>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include <gflags/gflags.h>
#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"
#include "third_party/rapidjson/rapidjson/ostreamwrapper.h"
#include "third_party/rapidjson/rapidjson/writer.h"

#include "channels.h"
#include "round_trips.h"
#include "test_runner.h"

DEFINE_string(fbenchmark_out, "", "Filename to write results to");
DEFINE_uint32(fbenchmark_runs, 1000,
              "Number of times to run each test (default is 1000)");
// Note that an empty regular expression matches any string.
DEFINE_string(fbenchmark_filter,
              "",
              "Regular expression that specifies a subset of tests to run.  "
              "By default, all the tests are run");

// Command line arguments used internally for launching subprocesses.
DEFINE_uint32(channel_read, 0, "Launch a process to read from a channel");
DEFINE_uint32(channel_write, 0, "Launch a process to write to a channel");
DEFINE_string(subprocess, "", "Launch a process to run the named function");

namespace fbenchmark {
namespace {

typedef std::vector<std::pair<std::string, std::function<TestCaseInterface*()>>>
    TestList;
// g_tests needs to be POD because this list is populated by constructors.
// We don't want g_tests to have a constructor that might get run after
// items have been added to the list, because that would clobber the list.
TestList* g_tests;

bool RunTests(uint32_t run_count,
              std::ostream* stream,
              const char* regex_string) {
  // Compile the regular expression.
  regex_t regex;
  int err = regcomp(&regex, regex_string, REG_EXTENDED);
  if (err != 0) {
    char msg[100];
    msg[0] = '\0';
    regerror(err, &regex, msg, sizeof(msg));
    fprintf(stderr, "Compiling the regular expression \"%s\" failed: %s\n",
            regex_string, msg);
    return false;
  }

  rapidjson::OStreamWrapper stream_wrapper(*stream);
  rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream_wrapper);

  double nanoseconds_per_tick = 1e9 / zx_ticks_per_second();

  uint64_t* time_points = new uint64_t[run_count + 1];
  writer.StartArray();

  bool found_match = false;
  for (auto& pair : *g_tests) {
    const char* test_name = pair.first.c_str();
    bool matched_regex = regexec(&regex, test_name, 0, nullptr, 0) == 0;
    if (!matched_regex)
      continue;
    found_match = true;

    // Log in a format similar to gtest's output.
    printf("[ RUN      ] %s\n", test_name);

    TestCaseInterface* test_instance = pair.second();

    time_points[0] = zx_ticks_get();
    for (uint32_t idx = 0; idx < run_count; ++idx) {
      test_instance->Run();
      time_points[idx + 1] = zx_ticks_get();
    }

    delete test_instance;

    printf("[       OK ] %s\n", test_name);

    writer.StartObject();
    writer.Key("label");
    writer.String(pair.first.c_str());
    writer.Key("unit");
    writer.String("ns");
    writer.Key("samples");
    writer.StartArray();
    writer.StartObject();
    writer.Key("values");
    writer.StartArray();
    for (uint32_t idx = 0; idx < run_count; ++idx) {
      uint64_t time_taken = time_points[idx + 1] - time_points[idx];
      writer.Double(time_taken * nanoseconds_per_tick);
    }
    writer.EndArray();
    writer.EndObject();
    writer.EndArray();
    writer.EndObject();
  }

  writer.EndArray();
  delete[] time_points;

  regfree(&regex);
  if (!found_match) {
    // Report an error so that this doesn't fail silently if the regex is
    // wrong.
    fprintf(stderr, "The regular expression \"%s\" did not match any tests\n",
            regex_string);
    return false;
  }
  return true;
}

// Run the tests in a way that is suitable for running on the bots via
// runtests.
void RunFastTests() {
  // Run each test a small number of times to ensure that doing multiple
  // runs works OK.
  uint32_t run_count = 5;
  std::ofstream null_stream;
  FXL_CHECK(RunTests(run_count, &null_stream, ""));
}

}  // namespace

TestCaseInterface::~TestCaseInterface() {}

void RegisterTestFactory(const char* name,
                         std::function<TestCaseInterface*()> factory_func) {
  if (!g_tests)
    g_tests = new TestList;
  g_tests->push_back(std::make_pair(name, factory_func));
}

int BenchmarksMain(int argc, char** argv, bool run_gbenchmark) {
  benchmark::Initialize(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_channel_read > 0) {
    return channel_read(FLAGS_channel_read);
  } else if (FLAGS_channel_write > 0) {
    return channel_write(FLAGS_channel_write);
  } else if (FLAGS_subprocess != "") {
    RunSubprocess(FLAGS_subprocess.c_str());
    return 0;
  }

  if (FLAGS_fbenchmark_out != "") {
    std::ofstream stream(FLAGS_fbenchmark_out);
    bool success = RunTests(FLAGS_fbenchmark_runs, &stream,
                            FLAGS_fbenchmark_filter.c_str());
    stream.close();
    return success ? 0 : 1;
  } else if (run_gbenchmark) {
    benchmark::RunSpecifiedBenchmarks();
  } else {
    RunFastTests();
  }
  return 0;
}

}  // namespace fbenchmark
