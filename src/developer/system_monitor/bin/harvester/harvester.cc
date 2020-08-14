// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester.h"

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>

#include "gather_channels.h"
#include "gather_cpu.h"
#include "gather_inspectable.h"
#include "gather_introspection.h"
#include "gather_memory.h"
#include "gather_memory_digest.h"
#include "gather_processes_and_memory.h"
#include "gather_tasks.h"
#include "gather_threads_and_cpu.h"

namespace harvester {

Harvester::Harvester(zx_handle_t root_resource,
                     std::unique_ptr<DockyardProxy> dockyard_proxy)
    : root_resource_(root_resource),
      dockyard_proxy_(std::move(dockyard_proxy)) {}

void Harvester::GatherDeviceProperties() {
  FX_VLOGS(1) << "Harvester::GatherDeviceProperties";
  gather_cpu_.GatherDeviceProperties();
  // TODO(fxbug.dev/40872): re-enable once we need this data.
  // gather_inspectable_.GatherDeviceProperties();
  // gather_introspection_.GatherDeviceProperties();
  gather_memory_.GatherDeviceProperties();
  // Temporarily turn of digest and memory summary gathering (until after
  // dog food release).
  // gather_memory_digest_.GatherDeviceProperties();
  gather_tasks_.GatherDeviceProperties();
}

void Harvester::GatherFastData(async_dispatcher_t* dispatcher) {
  FX_VLOGS(1) << "Harvester::GatherFastData";
  zx::time now = async::Now(dispatcher);
  gather_threads_and_cpu_.PostUpdate(dispatcher, now, zx::msec(100));
}

void Harvester::GatherSlowData(async_dispatcher_t* dispatcher) {
  FX_VLOGS(1) << "Harvester::GatherSlowData";
  zx::time now = async::Now(dispatcher);

  // TODO(fxbug.dev/40872): re-enable once we need this data.
  // gather_inspectable_.PostUpdate(dispatcher, now, zx::sec(3));
  // gather_introspection_.PostUpdate(dispatcher, now, zx::sec(10));

  // Temporarily turn off digest and memory summary gathering (until after
  // dog food).
  // gather_memory_digest_.PostUpdate(dispatcher, now, zx::msec(500));

  gather_channels_.PostUpdate(dispatcher, now, zx::sec(1));
  gather_processes_and_memory_.PostUpdate(dispatcher, now, zx::sec(2));
}

}  // namespace harvester
