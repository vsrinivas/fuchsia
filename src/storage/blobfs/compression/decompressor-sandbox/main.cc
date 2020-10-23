// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/blobfs/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>

#include "src/storage/blobfs/compression/decompressor-sandbox/decompressor-impl.h"

int main(int argc, const char** argv) {
  syslog::LogSettings settings = {.min_log_level = syslog::LOG_ERROR};

  async::Loop trace_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx_status_t status = trace_loop.StartThread();
  if (status != ZX_OK)
    exit(1);
  trace::TraceProviderWithFdio trace_provider(trace_loop.dispatcher());

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  blobfs::DecompressorImpl impl;
  fidl::Binding<fuchsia::blobfs::internal::DecompressorCreator> binding(&impl);
  fidl::InterfaceRequestHandler<fuchsia::blobfs::internal::DecompressorCreator> handler =
      [&](fidl::InterfaceRequest<fuchsia::blobfs::internal::DecompressorCreator> request) {
        binding.Bind(std::move(request));
      };

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService<fuchsia::blobfs::internal::DecompressorCreator>(
      std::move(handler));

  return loop.Run();
}
