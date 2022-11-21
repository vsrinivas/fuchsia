// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.kernel/cpp/fidl.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

namespace {

// Ask the kernel to run its unit tests.
TEST(KernelUnittests, run_kernel_unittests) {
  constexpr char command[] = "ut all";

  auto client_end = component::Connect<fuchsia_kernel::DebugBroker>();
  ASSERT_OK(client_end.status_value());

  auto result = fidl::WireCall(*client_end)->SendDebugCommand(command);
  ASSERT_OK(result.status());
}

// Run certain unit tests in loops, to shake out flakes.
TEST(KernelUnittests, repeated_run_certain_unittests) {
  constexpr std::array<std::string_view, 2> commands{"ut timer", "ut pi"};
  constexpr int kLoops = 10;

  auto client_end = component::Connect<fuchsia_kernel::DebugBroker>();
  ASSERT_OK(client_end.status_value());

  for (int i = 0; i < kLoops; i++) {
    for (auto command : commands) {
      auto result =
          fidl::WireCall(*client_end)->SendDebugCommand(fidl::StringView::FromExternal(command));
      ASSERT_OK(result.status());
    }
  }
}

}  // namespace
