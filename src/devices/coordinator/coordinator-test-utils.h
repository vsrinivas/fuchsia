// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_COORDINATOR_COORDINATOR_TEST_UTILS_H_
#define SRC_DEVICES_COORDINATOR_COORDINATOR_TEST_UTILS_H_

#include <fuchsia/driver/test/c/fidl.h>
#include <fuchsia/hardware/power/statecontrol/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/fidl/txn_header.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <fbl/algorithm.h>

#include "coordinator.h"
#include "devfs.h"
#include "devhost.h"
#include "fdio.h"

constexpr char kSystemDriverPath[] = "/boot/driver/platform-bus.so";

class DummyFsProvider : public devmgr::FsProvider {
  ~DummyFsProvider() {}
  zx::channel CloneFs(const char* path) override { return zx::channel(); }
};

void CreateBootArgs(const char* config, size_t size, devmgr::BootArgs* boot_args);
devmgr::CoordinatorConfig DefaultConfig(async_dispatcher_t* dispatcher,
                                        devmgr::BootArgs* boot_args);
void InitializeCoordinator(devmgr::Coordinator* coordinator);

void CheckBindDriverReceived(const zx::channel& remote, const char* expected_driver);

#endif  // SRC_DEVICES_COORDINATOR_COORDINATOR_TEST_UTILS_H_
