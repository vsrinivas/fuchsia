// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>
#include <lib/zx/time.h>
#include <trace-provider/provider.h>
#include <zircon/status.h>

#include "garnet/bin/trace/spec.h"
#include "garnet/bin/trace/tests/integration_tests.h"

bool RunFillBufferTest(const tracing::Spec& spec,
                       async_dispatcher_t* dispatcher) {
  trace::TraceProvider provider(dispatcher);
  FXL_DCHECK(provider.is_valid());
  // Until we have synchronous registration, give registration time to happen.
  zx::nanosleep(zx::deadline_after(zx::sec(1)));

  // Generate at least 4MB of test records.
  // This stress tests streaming mode buffer saving (with buffer size of 1MB).
  constexpr size_t kMinNumBuffersFilled = 4;

  FillBuffer(kMinNumBuffersFilled, *spec.buffer_size_in_mb);
  return true;
}

bool VerifyFillBufferTest(const tracing::Spec& spec,
                          const std::string& test_output_file) {
  tracing::BufferingMode buffering_mode;
  if (!tracing::GetBufferingMode(*spec.buffering_mode, &buffering_mode)) {
    FXL_LOG(ERROR) << "Bad buffering mode: " << *spec.buffering_mode;
    return false;
  }
  return VerifyFullBuffer(test_output_file, buffering_mode,
                          *spec.buffer_size_in_mb);
}

const IntegrationTest kFillBufferIntegrationTest = {
  "fill-buffer",
  &RunFillBufferTest,
  &VerifyFillBufferTest,
};
