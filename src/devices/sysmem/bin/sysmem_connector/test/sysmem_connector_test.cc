// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "lib/fidl/cpp/interface_ptr.h"
#include "src/lib/fsl/handles/object_info.h"

// The sysmem-test in zircon covers more functionality of sysmem itself.  This
// test is only meant to verify that sysmem_connector establishes a connection
// to the sysmem driver.
TEST(SysmemConnectorTest, Connect) {
  async::Loop main_loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  std::mutex is_sync_complete_lock;
  bool is_sync_complete = false;

  fuchsia::sysmem::AllocatorPtr allocator;
  allocator.set_error_handler([](zx_status_t status) { ASSERT_EQ(status, ZX_OK); });
  component_context->svc()->Connect(allocator.NewRequest(main_loop.dispatcher()));
  allocator->SetDebugClientInfo(fsl::GetCurrentProcessName(), fsl::GetCurrentProcessKoid());

  fuchsia::sysmem::BufferCollectionTokenPtr token;
  token.set_error_handler([](zx_status_t status) { ASSERT_EQ(status, ZX_OK); });
  allocator->AllocateSharedCollection(token.NewRequest(main_loop.dispatcher()));

  token->Sync([&] {
    {  // scope lock
      std::lock_guard<std::mutex> lock(is_sync_complete_lock);
      is_sync_complete = true;
    }  // ~lock
  });

  while (true) {
    {  // scope lock
      std::lock_guard<std::mutex> lock(is_sync_complete_lock);
      if (is_sync_complete) {
        break;
      }
    }  // ~lock
    zx_status_t status = main_loop.Run(zx::deadline_after(zx::sec(30)), true);
    // If this didn't work after 30 seconds, consider that a test failure.
    ASSERT_EQ(status, ZX_OK);
  }
  ASSERT_TRUE(is_sync_complete);
  // The Sync() working means the Allocator connection was established to the
  // sysmem driver, and the driver responded.

  allocator.set_error_handler([](zx_status_t status) { ASSERT_EQ(status, ZX_ERR_CANCELED); });
  token.set_error_handler([](zx_status_t status) { ASSERT_EQ(status, ZX_ERR_CANCELED); });
}
