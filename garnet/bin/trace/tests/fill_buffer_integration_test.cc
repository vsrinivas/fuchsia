// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>
#include <zircon/status.h>

#include <memory>

#include <trace-provider/provider.h>

#include "garnet/bin/trace/tests/basic_integration_tests.h"
#include "src/lib/fxl/logging.h"

namespace tracing {
namespace test {

const char kProviderName[] = "fill-buffer";

static bool RunFillBufferTest(const tracing::Spec& spec) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  // If we're streaming then we need intermediate buffer saving to be acted on while we're
  // writing the buffer. So run the provider loop in the background.
  loop.StartThread();

  std::unique_ptr<trace::TraceProvider> provider;
  bool already_started;
  if (!CreateProviderSynchronously(loop, kProviderName, &provider, &already_started)) {
    return false;
  }

  // The program may not be being run under tracing. If it is tracing should have already started.
  // Things are a little different because the provider loop is running in the background.
  if (already_started) {
    // At this point we're registered with trace-manager, and we know tracing
    // has started. But we haven't received the Start() request yet, which
    // contains the trace buffer (as a vmo) and other things. So wait for it.
    async::Loop wait_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    if (!WaitForTracingToStart(wait_loop, kStartTimeout)) {
      FXL_LOG(ERROR) << "Provider " << kProviderName << " failed waiting for tracing to start";
      return false;
    }
  }

  // Generate at least 4MB of test records.
  // This stress tests streaming mode buffer saving (with buffer size of 1MB).
  constexpr size_t kMinNumBuffersFilled = 4;

  FillBuffer(kMinNumBuffersFilled, *spec.buffer_size_in_mb);

  loop.Quit();
  loop.JoinThreads();
  // The loop is no longer running at this point. This is ok as the engine doesn't need the loop
  // to finish writing to the buffer: Tracing will be terminated when |provider| goes out of
  // scope. But this is something to be aware of.

  return true;
}

static bool VerifyFillBufferTest(const tracing::Spec& spec, const std::string& test_output_file) {
  const tracing::BufferingModeSpec* mode_spec = tracing::LookupBufferingMode(*spec.buffering_mode);
  if (mode_spec == nullptr) {
    FXL_LOG(ERROR) << "Bad buffering mode: " << *spec.buffering_mode;
    return false;
  }
  return VerifyFullBuffer(test_output_file, mode_spec->mode, *spec.buffer_size_in_mb);
}

const IntegrationTest kFillBufferIntegrationTest = {
    "fill-buffer",
    &RunFillBufferTest,
    &VerifyFillBufferTest,
};

}  // namespace test
}  // namespace tracing
