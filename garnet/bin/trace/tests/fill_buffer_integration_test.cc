// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>
#include <lib/zx/time.h>
#include <trace-provider/provider.h>
#include <zircon/status.h>

#include "garnet/bin/trace/tests/basic_integration_tests.h"

static bool RunFillBufferTest(const tracing::Spec& spec) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

  fbl::unique_ptr<trace::TraceProvider> provider;
  if (!CreateProviderSynchronously(loop, "fill-buffer", &provider)) {
    return false;
  }

  // Generate at least 4MB of test records.
  // This stress tests streaming mode buffer saving (with buffer size of 1MB).
  constexpr size_t kMinNumBuffersFilled = 4;

  FillBuffer(kMinNumBuffersFilled, *spec.buffer_size_in_mb);
  return true;
}

static bool VerifyFillBufferTest(const tracing::Spec& spec,
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
