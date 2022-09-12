// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fitx/result.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>

#include "lib/syslog/cpp/macros.h"
#include "src/virtualization/bin/vmm/vmm.h"

using ::fuchsia::virtualization::GuestError;

int main(int argc, char** argv) {
  syslog::SetTags({"vmm"});

  auto loop = std::make_shared<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  auto device_loop = std::make_shared<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop->dispatcher());
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  fuchsia::virtualization::GuestConfig cfg;
  fuchsia::virtualization::GuestConfigProviderSyncPtr cfg_provider;
  context->svc()->Connect(cfg_provider.NewRequest());
  zx_status_t status = cfg_provider->Get(&cfg);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "No launch info provided";
    return status;
  }

  // Note that Initialize and StartPrimaryVcpu will be invoked by different GuestLifecycle
  // FIDL calls once this has been migrated to use a VmmController.
  vmm::Vmm vmm(loop, device_loop);
  fitx::result<GuestError> result = vmm.Initialize(std::move(cfg), context.get());
  if (!result.is_ok()) {
    return static_cast<int32_t>(result.error_value());
  }

  result = vmm.StartPrimaryVcpu();
  if (!result.is_ok()) {
    return static_cast<int32_t>(result.error_value());
  }

  // Start the dispatch thread for communicating with the out of process
  // devices.
  status = device_loop->StartThread("device-worker");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create async worker";
    return status;
  }

  status = loop->Run();
  device_loop->Shutdown();

  return status;
}
