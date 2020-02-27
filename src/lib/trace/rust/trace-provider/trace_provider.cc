// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/compiler.h>

#include <thread>

#include <trace-provider/provider.h>

__BEGIN_CDECLS

void trace_provider_create_with_fdio_rust() __attribute__((visibility("default")));

__END_CDECLS

// The C++ trace provider API depends on libasync. Create a new thread here
// to run a libasync loop here to host that trace provider.
//
// This is intended to be a temporary solution until we have a proper rust
// trace-provider implementation.
static void trace_provider_with_fdio_thread_entry() {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  loop.Run();
}

// Detaches the loop cleanly for shutdown.  A thread must be either detached, or
// joined before it is destroyed.
class LoopShutdown {
 public:
  explicit LoopShutdown(std::function<void()> f) : thread_(std::make_unique<std::thread>(f)) {}
  ~LoopShutdown() { thread_->detach(); }

 private:
  std::unique_ptr<std::thread> thread_;
};

void trace_provider_create_with_fdio_rust() {
  // Keep a reference to the thread to make LSAN happy.  At the same time, ensures exactly-once
  // semantics in C++11 and beyond.
  static const auto trace_provider_thread =
      std::make_unique<LoopShutdown>(&trace_provider_with_fdio_thread_entry);
}
