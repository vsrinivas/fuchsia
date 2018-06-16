// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_ctx.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/fxl/logging.h>
#include <stdarg.h>
#include <stdio.h>

DriverCtx::DriverCtx() {
  // We don't use kAsyncLoopConfigMakeDefault here, because we don't really want
  // to be setting the default async_t for the the thread that creates the
  // DriverCtx.  We'll plumb async_t(s) explicitly instead.
  shared_fidl_loop_ = std::make_unique<async::Loop>();
  shared_fidl_loop_->StartThread("shared_fidl_thread", &shared_fidl_thread_);
}

DriverCtx::~DriverCtx() {
  assert(shared_fidl_loop_);
  shared_fidl_loop_->Quit();
  shared_fidl_loop_->JoinThreads();
  shared_fidl_loop_->Shutdown();
}

// TODO(dustingreen): Do format, printf, log, maybe some epitaphs.
void DriverCtx::FatalError(const char* format, ...) {
  // Let's not have a buffer on the stack, not because it couldn't be done
  // safely, but because we'd potentially run into stack size vs. message length
  // tradeoffs, stack expansion granularity fun, or whatever else.

  va_list args;
  va_start(args, format);
  size_t buffer_bytes = vsnprintf(nullptr, 0, format, args) + 1;
  va_end(args);

  // ~buffer never actually runs since this method never returns
  std::unique_ptr<char[]> buffer(new char[buffer_bytes]);

  va_start(args, format);
  size_t buffer_bytes_2 =
      vsnprintf(buffer.get(), buffer_bytes, format, args) + 1;
  (void)buffer_bytes_2;
  // sanity check; should match so go ahead and assert that it does.
  assert(buffer_bytes == buffer_bytes_2);
  va_end(args);

  FXL_LOG(ERROR) << "DriverCtx::FatalError(): " << buffer.get();

  // TODO(dustingreen): Send string in buffer via channel epitaphs, when
  // possible. The channel activity/failing server-side generally will race with
  // trying to send epitaph - probably requires enlisting shared_fidl_thread()
  // from here - probably a timeout here would be a good idea if so.

  // This should provide more stack dump than exit(-1) would give.
  FXL_CHECK(false) << "DriverCtx::FatalError() is fatal.";
}

// Post to async in a way that's guaranteed to run the posted work in the same
// order as the posting order (is the intent; if async::PostTask ever changes to
// not guarantee order, we'll need to work around that here).
//
// TODO(dustingreen): Determine if async::PostTask() will strictly guarantee
// ordering of posted items at least in the case of async::Loop.
void DriverCtx::PostSerial(async_t* async, fit::closure to_run) {
  // TODO(dustingreen): This should just forward to async::PostTask(async,
  // to_run) without any wrapping needed, once I figure out why attempts to use
  // async::PostTask() directly didn't work before.  Hopefully async::PostTask()
  // can just switch to fit::function soon.
  std::shared_ptr<fit::closure> shared =
      std::make_shared<fit::closure>(std::move(to_run));
  std::function<void(void)> copyable = [shared]() {
    fit::closure foo = std::move(*shared.get());
    foo();
  };
  // The std::function copyable can be passed to a fbl::Closure parameter.  We'd
  // prefer to use a move-only function all the way down when things are ready
  // for that.
  zx_status_t post_result = async::PostTask(async, copyable);
  if (post_result != ZX_OK) {
    FatalError("async::PostTask() failed - result: %d", post_result);
  }
}

void DriverCtx::PostToSharedFidl(fit::closure to_run_on_shared_fidl_thread) {
  // Switch the implementation here to fit::function when possible.
  PostSerial(shared_fidl_loop()->async(),
             std::move(to_run_on_shared_fidl_thread));
}
