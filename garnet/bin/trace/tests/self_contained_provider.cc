// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This shared library contains a self-contained trace-provider.
// That is libtrace-engine.so is contained within our library using
// libtrace-engine-static.a.

#include "garnet/bin/trace/tests/self_contained_provider.h"

#include <assert.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <memory>

#include <trace-provider/provider.h>
#include <trace/event.h>

#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "src/lib/fxl/logging.h"

namespace tracing {
namespace test {

static const char kName[] = "self-contained-provider";

static int SelfContainedProviderThread(void* arg) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  std::unique_ptr<trace::TraceProvider> provider;
  if (!CreateProviderSynchronouslyAndWait(loop, kName, &provider)) {
    return false;
  }

  WriteTestEvents(kNumSimpleTestEvents);

  loop.RunUntilIdle();

  return 0;
}

__EXPORT bool StartSelfContainedProvider(thrd_t* out_thread) {
  if (thrd_create_with_name(out_thread, SelfContainedProviderThread, nullptr, kName) !=
      thrd_success) {
    FXL_LOG(ERROR) << "Error creating " << kName << " thread";
    return false;
  }
  return true;
}

}  // namespace test
}  // namespace tracing
