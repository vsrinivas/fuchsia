// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/sys/cpp/testing/startup_context_for_test.h"

#include <lib/fdio/directory.h>
#include <zircon/processargs.h>

namespace sys {
namespace testing {

StartupContextForTest::StartupContextForTest(
    std::shared_ptr<ServiceDirectoryForTest> svc,
    fuchsia::io::DirectoryPtr directory_ptr, async_dispatcher_t* dispatcher)
    : StartupContext(svc, directory_ptr.NewRequest(dispatcher).TakeChannel(),
                     dispatcher),
      outgoing_directory_ptr_(std::move(directory_ptr)),
      fake_svc_(svc) {
  fdio_service_connect_at(
      outgoing_directory_ptr_.channel().get(), "public",
      public_directory_ptr_.NewRequest().TakeChannel().release());
}

StartupContextForTest::~StartupContextForTest() = default;

std::unique_ptr<StartupContextForTest> StartupContextForTest::Create(
    async_dispatcher_t* dispatcher) {
  // remove this handle from namespace so that no one is using it.
  zx_take_startup_handle(PA_DIRECTORY_REQUEST);

  fuchsia::io::DirectoryPtr directory_ptr;
  return std::make_unique<StartupContextForTest>(
      ServiceDirectoryForTest::Create(dispatcher), std::move(directory_ptr),
      dispatcher);
}

}  // namespace testing
}  // namespace sys
