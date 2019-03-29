// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This testcase has two providers, one uses libtrace-engine.so, and the
// second uses libtrace-engine-static. We should get valid traces from both
// providers.

#include <assert.h>
#include <stdlib.h>

#include <lib/async-loop/cpp/loop.h>
#include <src/lib/fxl/logging.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

#include "integration_test_utils.h"
#include "self_contained_provider.h"

#define TEST_NAME "two-providers-two-engines"

static bool RunTwoProvidersTwoEnginesTest(const tracing::Spec& spec) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

  fbl::unique_ptr<trace::TraceProvider> provider1;
  if (!CreateProviderSynchronously(loop, "provider1", &provider1)) {
    return false;
  }

  thrd_t provider2_thread;
  if (!StartSelfContainedProvider(&provider2_thread)) {
    FXL_LOG(ERROR) << "Failed to create provider2";
    return EXIT_FAILURE;
  }

  WriteTestEvents(kNumSimpleTestEvents);

  thrd_join(provider2_thread, nullptr);

  return true;
}

static bool VerifyTwoProvidersTwoEnginesTest(
    const tracing::Spec& spec, const std::string& test_output_file) {
  size_t num_events;
  if (!VerifyTestEvents(test_output_file, &num_events)) {
    return false;
  }

  // Both providers copy the "simple" test.
  size_t num_expected_events = 2 * kNumSimpleTestEvents;
  if (num_events != num_expected_events) {
    FXL_LOG(ERROR) << "Incorrect number of events present, got " << num_events
                   << ", expected " << num_expected_events;
    return false;
  }

  return true;
}

const IntegrationTest kTwoProvidersTwoEnginesIntegrationTest = {
    TEST_NAME,
    &RunTwoProvidersTwoEnginesTest,
    &VerifyTwoProvidersTwoEnginesTest,
};

const IntegrationTest* LookupTest(const std::string& test_name) {
  if (test_name == TEST_NAME) {
    return &kTwoProvidersTwoEnginesIntegrationTest;
  }
  return nullptr;
}
