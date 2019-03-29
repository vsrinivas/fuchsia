// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do some simple tracing and verification.
// The big part of the test is that this works at all in the presence of
// a provider that provides two of them.

#include <src/lib/fxl/logging.h>
#include <lib/zx/time.h>
#include <trace-provider/provider.h>
#include <zircon/status.h>

#include "garnet/bin/trace/tests/basic_integration_tests.h"

static bool RunSimpleTest(const tracing::Spec& spec) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

  fbl::unique_ptr<trace::TraceProvider> provider;
  if (!CreateProviderSynchronously(loop, "simple", &provider)) {
    return false;
  }

  WriteTestEvents(kNumSimpleTestEvents);

  loop.RunUntilIdle();
  return true;
}

static bool VerifySimpleTest(const tracing::Spec& spec,
                             const std::string& test_output_file) {
  size_t num_events;
  if (!VerifyTestEvents(test_output_file, &num_events)) {
    return false;
  }

  if (num_events != kNumSimpleTestEvents) {
    FXL_LOG(ERROR) << "Incorrect number of events present, got " << num_events
                   << ", expected " << kNumSimpleTestEvents;
    return false;
  }

  return true;
}

const IntegrationTest kSimpleIntegrationTest = {
    "simple",
    &RunSimpleTest,
    &VerifySimpleTest,
};
