// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include <gflags/gflags.h>

#include "channels.h"
#include "round_trips.h"
#include "test_runner.h"

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

// Run the tests in a way that is suitable for running on the bots via
// runtests.
void RunFastTests() {
  for (auto& pair : *g_tests) {
    // Log in a format similar to gtest's output.
    printf("[ RUN      ] %s\n", pair.first.c_str());
    TestCaseInterface* test = pair.second();
    // Run the test a small number of times to ensure that doing multiple
    // runs works OK.
    for (int i = 0; i < 5; ++i)
      test->Run();
    delete test;
    printf("[       OK ] %s\n", pair.first.c_str());
  }
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

  if (run_gbenchmark) {
    benchmark::RunSpecifiedBenchmarks();
  } else {
    RunFastTests();
  }
  return 0;
}

}  // namespace fbenchmark
