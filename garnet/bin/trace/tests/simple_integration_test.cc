// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do some simple tracing and verification.
// The big part of the test is that this works at all in the presence of
// a provider that provides two of them.

#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <memory>

#include "garnet/bin/trace/tests/basic_integration_tests.h"

namespace tracing {
namespace test {

static bool RunSimpleTest(const tracing::Spec& spec) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  std::unique_ptr<trace::TraceProvider> provider;
  if (!CreateProviderSynchronouslyAndWait(loop, "simple", &provider)) {
    return false;
  }

  WriteTestEvents(kNumSimpleTestEvents);

  loop.RunUntilIdle();
  return true;
}

static bool VerifySimpleTest(const tracing::Spec& spec, const std::string& test_output_file) {
  size_t num_events;
  if (!VerifyTestEventsFromJson(test_output_file, &num_events)) {
    return false;
  }

  if (num_events != kNumSimpleTestEvents) {
    FX_LOGS(ERROR) << "Incorrect number of events present, got " << num_events << ", expected "
                   << kNumSimpleTestEvents;
    return false;
  }

  return true;
}

static bool RunSimpleTest(size_t buffer_size_in_mb, const std::string& buffering_mode) {
  // TODO(52043): Implement non-tspec version of simple integration test.
  FX_LOGS(ERROR) << "Non-tspec simple integration test not yet implemented";
  return false;
}

static bool VerifySimpleTest(size_t buffer_size_in_mb, const std::string& buffering_mode,
                             const std::string& test_output_file) {
  // TODO(52043): Implement non-tspec version of simple integration test.
  FX_LOGS(ERROR) << "Non-tspec simple integration test not yet implemented";
  return false;
}

// TODO(52043): Remove tspec functionality.
const IntegrationTest kSimpleIntegrationTest = {
    "simple",
    &RunSimpleTest,     // for run command
    &VerifySimpleTest,  // for verify command
    &RunSimpleTest,     // for run_tspec command; to be removed
    &VerifySimpleTest,  // for verify_tspec command; to be removed
};

}  // namespace test
}  // namespace tracing
