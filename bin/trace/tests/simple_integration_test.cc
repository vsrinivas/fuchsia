// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do some simple tracing and verification.
// The big part of the test is that this works at all in the presence of
// a provider that provides two of them.

#include <lib/fxl/logging.h>
#include <lib/zx/time.h>
#include <trace-provider/provider.h>
#include <zircon/status.h>

#include "garnet/bin/trace/tests/integration_tests.h"

bool RunSimpleTest(const tracing::Spec& spec,
                   async_dispatcher_t* dispatcher) {
  fbl::unique_ptr<trace::TraceProvider> provider;
  bool already_started;
  if (!trace::TraceProvider::CreateSynchronously(dispatcher, "simple-test",
                                                 &provider,
                                                 &already_started)) {
    FXL_LOG(ERROR) << "Failed to create provider";
    return false;
  }
  if (already_started) {
    if (!WaitForTracingToStart(kStartTimeout)) {
      FXL_LOG(ERROR) << "Provider failed waiting for tracing to start";
      return false;
    }
  }

  WriteTestEvents(kNumSimpleTestEvents);
  return true;
}

bool VerifySimpleTest(const tracing::Spec& spec,
                      const std::string& test_output_file) {
  size_t num_events;
  if (!VerifyTestEvents(test_output_file, &num_events)) {
    return false;
  }

  if (num_events != kNumSimpleTestEvents) {
    FXL_LOG(ERROR) << "Incorrect number of events present, got "
                   << num_events << ", expected " << kNumSimpleTestEvents;
    return false;
  }

  return true;
}

const IntegrationTest kSimpleIntegrationTest = {
  "simple",
  &RunSimpleTest,
  &VerifySimpleTest,
};
