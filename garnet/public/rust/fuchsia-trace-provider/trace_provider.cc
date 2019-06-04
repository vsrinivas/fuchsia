// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

void trace_provider_create_with_fdio_rust() __attribute__((visibility("default")));

// TODO(PT-63): Delete when soft-transition has completed.
void trace_provider_create_rust() __attribute__((visibility("default")));

__END_CDECLS

// The C++ trace provider API depends on libasync. Create a new thread here
// to run a libasync loop here to host that trace provider.
//
// This is intended to be a temporary solution until we have a proper rust
// trace-provider implementation.
static void trace_provider_with_fdio_thread_entry() {
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  loop.Run();
}

void trace_provider_create_with_fdio_rust() {
  new std::thread(&trace_provider_with_fdio_thread_entry);
}

// TODO(PT-63): Delete when soft-transition has completed.
static void trace_provider_thread_entry() {
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  loop.Run();
}

void trace_provider_create_rust() {
  new std::thread(&trace_provider_thread_entry);
}
