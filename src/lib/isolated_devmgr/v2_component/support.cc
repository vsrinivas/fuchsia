// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To get drivermanager to run in a test environment, we need to fake boot-arguments & root-job.

#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/kernel/c/fidl.h>
#include <fuchsia/kernel/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <mock-boot-arguments/server.h>

static zx_status_t RootJobGet(void* ctx, fidl_txn_t* txn) {
  zx_handle_t out;
  zx_status_t status = zx_handle_duplicate(zx_job_default(), ZX_RIGHT_SAME_RIGHTS, &out);
  if (status != ZX_OK) {
    return status;
  }
  return fuchsia_kernel_RootJobGet_reply(txn, out);
}

constexpr fuchsia_kernel_RootJob_ops kRootJobOps = {
    .Get = RootJobGet,
};

int main(void) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  mock_boot_arguments::Server boot_arguments;
  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>(
          [&boot_arguments](zx::channel request, async_dispatcher_t* dispatcher) {
            fidl::BindSingleInFlightOnly(dispatcher, std::move(request), &boot_arguments);
          }),
      llcpp::fuchsia::boot::Arguments::Name);

  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>([](zx::channel request, async_dispatcher_t* dispatcher) {
        auto root_job_dispatch =
            reinterpret_cast<fidl_dispatch_t*>(fuchsia_kernel_RootJob_dispatch);
        fidl_bind(dispatcher, request.release(), root_job_dispatch, nullptr, &kRootJobOps);
      }),
      llcpp::fuchsia::kernel::RootJob::Name);

  loop.Run();
  return 0;
}
