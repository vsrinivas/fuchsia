// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_TEST_UTILS_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_TEST_UTILS_H_

#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/driver/test/c/fidl.h>
#include <fuchsia/hardware/power/statecontrol/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/fidl/txn_header.h>
#include <string.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <fbl/algorithm.h>
#include <mock-boot-arguments/server.h>

#include "coordinator.h"
#include "devfs.h"
#include "driver_host.h"
#include "fdio.h"

constexpr char kSystemDriverPath[] = "/pkg/driver/platform-bus.so";

class DummyFsProvider : public FsProvider {
 public:
  ~DummyFsProvider() {}
  zx::channel CloneFs(const char* path) override { return zx::channel(); }
};

CoordinatorConfig DefaultConfig(async_dispatcher_t* bootargs_dispatcher,
                                mock_boot_arguments::Server* boot_args,
                                fuchsia_boot::Arguments::SyncClient* client);
void InitializeCoordinator(Coordinator* coordinator);

void CheckBindDriverReceived(const zx::channel& remote, const char* expected_driver);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COORDINATOR_TEST_UTILS_H_
