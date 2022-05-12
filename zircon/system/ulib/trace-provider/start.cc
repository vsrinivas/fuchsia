// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sync/completion.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace-provider/start.h>
#include <lib/trace/observer.h>
#include <lib/zx/process.h>

#include <memory>
#include <thread>

namespace {

bool CreateProvider(async_dispatcher_t* dispatcher,
                    std::unique_ptr<trace::TraceProviderWithFdio>* out_provider,
                    bool* out_manager_is_tracing_already) {
  // Get the current process's name.  This is based on what
  // trace_provider_create_with_fdio() does.
  zx::unowned<zx::process> process = zx::process::self();
  char process_name[ZX_MAX_NAME_LEN];
  if (process->get_property(ZX_PROP_NAME, process_name, sizeof(process_name)) != ZX_OK) {
    return false;
  }

  return trace::TraceProviderWithFdio::CreateSynchronously(dispatcher, process_name, out_provider,
                                                           out_manager_is_tracing_already);
}

// Implements a thread that runs a TraceProvider.  It signals |completion|
// when its setup is complete.
void TraceProviderThread(sync_completion_t* completion) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  std::unique_ptr<trace::TraceProviderWithFdio> provider;
  bool manager_is_tracing_already;
  if (!CreateProvider(loop.dispatcher(), &provider, &manager_is_tracing_already)) {
    sync_completion_signal(completion);
    return;
  }

  trace::TraceObserver trace_observer;
  if (manager_is_tracing_already) {
    // Tracing is already enabled in the trace manager.  Wait for our
    // process's setup of tracing to complete.
    trace_observer.Start(loop.dispatcher(), [&] {
      // This callback may get called multiple times, but we must only
      // signal |completion| once because signalling it will cause it to be
      // deallocated.
      if (completion) {
        sync_completion_signal(completion);
        completion = nullptr;
      }
      // We would like to unregister the TraceObserver by doing
      // trace_observer.Stop(), but that triggers a ZX_ERR_BAD_HANDLE in
      // the event loop.
    });
  } else {
    // Tracing is not currently enabled in the trace manager, so no
    // further setup is required.
    sync_completion_signal(completion);
  }
  loop.Run();
}

}  // namespace

void trace_provider_start() {
  sync_completion_t completion;
  std::thread thread(TraceProviderThread, &completion);
  thread.detach();
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
}
