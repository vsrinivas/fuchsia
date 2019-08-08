// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_trace.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <zircon/syscalls.h>

#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace magma {

#if MAGMA_ENABLE_TRACING

static std::unique_ptr<ZirconPlatformTrace> g_platform_trace;

ZirconPlatformTrace::ZirconPlatformTrace() : loop_(&kAsyncLoopConfigNoAttachToThread) {}

ZirconPlatformTrace::~ZirconPlatformTrace() {
  if (trace_provider_) {
    async::PostTask(loop_.dispatcher(), [this]() {
      // trace_provider_.reset() needs to run on loop_'s dispatcher or else its teardown can be
      // racy and crash.
      trace_provider_.reset();
      // Run Quit() in the loop to ensure this task executes before JoinThreads() returns and the
      // destructor finishes.
      loop_.Quit();
    });
  } else {
    loop_.Quit();
  }
  loop_.JoinThreads();
}

bool ZirconPlatformTrace::Initialize() {
  zx_status_t status = loop_.StartThread();
  if (status != ZX_OK)
    return DRETF(false, "Failed to start async loop");
  trace_provider_ = std::make_unique<trace::TraceProviderWithFdio>(loop_.dispatcher());
  return true;
}

PlatformTrace* PlatformTrace::Get() {
  if (!g_platform_trace)
    g_platform_trace = std::make_unique<ZirconPlatformTrace>();
  return g_platform_trace.get();
}

// static
std::unique_ptr<PlatformTrace> PlatformTrace::CreateForTesting() {
  return std::make_unique<ZirconPlatformTrace>();
}

ZirconPlatformTraceObserver::ZirconPlatformTraceObserver()
    : loop_(&kAsyncLoopConfigNoAttachToThread) {}

bool ZirconPlatformTraceObserver::Initialize() {
  zx_status_t status = loop_.StartThread();
  if (status != ZX_OK)
    return DRETF(false, "Failed to start async loop");
  return true;
}

void ZirconPlatformTraceObserver::SetObserver(fit::function<void(bool)> callback) {
  observer_.Stop();
  enabled_ = false;

  observer_.Start(loop_.dispatcher(), [this, callback = std::move(callback)] {
    bool enabled = trace_state() == TRACE_STARTED;
    if (this->enabled_ != enabled) {
      this->enabled_ = enabled;
      callback(enabled);
    }
  });
}

// static
std::unique_ptr<PlatformTraceObserver> PlatformTraceObserver::Create() {
  auto observer = std::make_unique<ZirconPlatformTraceObserver>();
  if (!observer->Initialize())
    return nullptr;
  return observer;
}

#else

PlatformTrace* PlatformTrace::Get() { return nullptr; }

// static
std::unique_ptr<PlatformTrace> PlatformTrace::CreateForTesting() { return nullptr; }

#endif

// static
uint64_t PlatformTrace::GetCurrentTicks() { return zx_ticks_get(); }

}  // namespace magma
