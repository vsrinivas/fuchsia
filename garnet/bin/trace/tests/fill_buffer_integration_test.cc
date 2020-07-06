// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <memory>

#include "garnet/bin/trace/tests/basic_integration_tests.h"

namespace tracing::test {

namespace {

const char kFillBufferProviderName[] = "fill-buffer";

bool RunFillBufferTest(size_t buffer_size_in_mb, const std::string& buffering_mode) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  // If we're streaming then we need intermediate buffer saving to be acted on while we're
  // writing the buffer. So run the provider loop in the background.
  loop.StartThread();

  std::unique_ptr<trace::TraceProvider> provider;
  bool already_started;
  if (!CreateProviderSynchronously(loop, kFillBufferProviderName, &provider, &already_started)) {
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
      FX_LOGS(ERROR) << "Provider " << kFillBufferProviderName
                     << " failed waiting for tracing to start";
      return false;
    }
  }

  // Generate at least 4MB of test records.
  // This stress tests streaming mode buffer saving (with buffer size of 1MB).
  constexpr size_t kMinNumBuffersFilled = 4;

  FillBuffer(kMinNumBuffersFilled, buffer_size_in_mb);

  loop.Quit();
  loop.JoinThreads();
  // The loop is no longer running at this point. This is ok as the engine doesn't need the loop
  // to finish writing to the buffer: Tracing will be terminated when |provider| goes out of
  // scope. But this is something to be aware of.

  return true;
}

bool VerifyFillBufferTest(size_t buffer_size_in_mb, const std::string& buffering_mode,
                          const std::string& test_output_file) {
  const tracing::BufferingModeSpec* mode_spec = tracing::LookupBufferingMode(buffering_mode);
  if (mode_spec == nullptr) {
    FX_LOGS(ERROR) << "Bad buffering mode: " << buffering_mode;
    return false;
  }
  return VerifyFullBuffer(test_output_file, mode_spec->mode, buffer_size_in_mb);
}

}  // namespace

const IntegrationTest kFillBufferIntegrationTest = {
    kFillBufferProviderName,
    &RunFillBufferTest,     // for run command
    &VerifyFillBufferTest,  // for verify command
};

}  // namespace tracing::test
