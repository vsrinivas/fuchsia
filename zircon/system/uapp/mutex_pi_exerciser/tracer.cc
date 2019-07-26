// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdarg.h>
#include <trace/event.h>

#include "tracer.h"
#include "utils.h"

zx_status_t Tracer::Start() {
  zx_status_t res;

  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);

  res = loop_->StartThread();
  if (res != ZX_OK) {
    fprintf(stderr, "Failed to start trace thread (res = %d)\n", res);
    return res;
  }

  trace_provider_ = std::make_unique<trace::TraceProviderWithFdio>(loop_->dispatcher());

  // Wait for the trace subsystem to start up and be ready to go.
  printf("Waiting up to 5 seconds for tracing to start.\n");
  res = WaitFor([]() { return trace_state() == TRACE_STARTED; }, zx::sec(5));
  if (res == ZX_ERR_TIMED_OUT) {
    fprintf(stderr, "Timeout waiting for tracing to start\n");
  }

  return res;
}

void Tracer::Trace(trace_scope_t scope, const char* fmt, ...) {
  char buffer[256];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);

  TRACE_INSTANT("app", "mutex_pi_exerciser", scope, "msg", TA_STRING(buffer));
}
