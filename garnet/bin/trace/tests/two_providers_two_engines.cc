// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This testcase has two providers, one uses libtrace-engine.so, and the
// second uses libtrace-engine-static. We should get valid traces from both
// providers.

#include <assert.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>
#include <stdlib.h>

#include <memory>

#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "garnet/bin/trace/tests/self_contained_provider.h"

namespace tracing::test {

namespace {

const char kTwoProvidersTwoEnginesProviderName[] = "two-providers-two-engines";

bool RunTwoProvidersTwoEnginesTest(size_t buffer_size_in_mb, const std::string& buffering_mode) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  std::unique_ptr<trace::TraceProvider> provider1;
  if (!CreateProviderSynchronouslyAndWait(loop, "provider1", &provider1)) {
    return false;
  }

  thrd_t provider2_thread;
  if (!StartSelfContainedProvider(&provider2_thread)) {
    FX_LOGS(ERROR) << "Failed to create provider2";
    return EXIT_FAILURE;
  }

  WriteTestEvents(kNumSimpleTestEvents);

  thrd_join(provider2_thread, nullptr);

  return true;
}

bool VerifyTwoProvidersTwoEnginesTest(size_t buffer_size_in_mb, const std::string& buffering_mode,
                                      const std::string& test_output_file) {
  size_t num_events;
  if (!VerifyTestEventsFromJson(test_output_file, &num_events)) {
    return false;
  }

  // Both providers copy the "simple" test.
  size_t num_expected_events = 2 * kNumSimpleTestEvents;
  if (num_events != num_expected_events) {
    FX_LOGS(ERROR) << "Incorrect number of events present, got " << num_events << ", expected "
                   << num_expected_events;
    return false;
  }

  return true;
}

const IntegrationTest kTwoProvidersTwoEnginesIntegrationTest = {
    kTwoProvidersTwoEnginesProviderName,
    &RunTwoProvidersTwoEnginesTest,     // for run command
    &VerifyTwoProvidersTwoEnginesTest,  // for verify command
};
}  // namespace

const IntegrationTest* LookupTest(const std::string& test_name) {
  if (test_name == kTwoProvidersTwoEnginesProviderName) {
    return &kTwoProvidersTwoEnginesIntegrationTest;
  }
  return nullptr;
}

}  // namespace tracing::test
