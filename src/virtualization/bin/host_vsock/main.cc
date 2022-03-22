// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/virtualization/bin/host_vsock/guest_vsock_endpoint.h"
#include "src/virtualization/bin/host_vsock/host_vsock_endpoint.h"

int main(int argc, char** argv) {
  syslog::SetTags({"host_vsock"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  fuchsia::virtualization::GuestVsockEndpointPtr vm_guest_endpoint;
  context->svc()->Connect(vm_guest_endpoint.NewRequest());

  // TODO(fxbug.dev/72386): Revisit after CFv2 migration is complete and refactor Guest/Host
  // endpoint API. This component only supports connections between a single host endpoint and a
  // single guest endpoint. Multiple guests are not supported.
  std::unique_ptr<GuestVsockEndpoint> local_guest_endpoint;
  HostVsockEndpoint host_vsock_endpoint(loop.dispatcher(), [&local_guest_endpoint](uint32_t cid) {
    GuestVsockEndpoint* res = nullptr;
    if (cid == fuchsia::virtualization::DEFAULT_GUEST_CID) {
      res = local_guest_endpoint.get();
    }
    return res;
  });

  local_guest_endpoint =
      std::make_unique<GuestVsockEndpoint>(fuchsia::virtualization::DEFAULT_GUEST_CID,
                                           std::move(vm_guest_endpoint), &host_vsock_endpoint);

  zx_status_t status = context->outgoing()->AddPublicService(host_vsock_endpoint.GetHandler());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to add host vsock public service";
    loop.Quit();
  }

  loop.Run();
}
