// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester.h"

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/dispatcher.h>
#include <lib/inspect_deprecated/query/discover.h>
#include <lib/zx/time.h>

#include <memory>

#include "gather_cpu.h"
#include "gather_inspectable.h"
#include "gather_introspection.h"
#include "gather_memory.h"
#include "gather_tasks.h"
#include "src/lib/fxl/logging.h"

namespace harvester {

std::ostream& operator<<(std::ostream& out, const DockyardProxyStatus& status) {
  switch (status) {
    case DockyardProxyStatus::OK:
      return out << "OK (0)";
    case DockyardProxyStatus::ERROR:
      return out << "ERROR (-1)";
  }
  FXL_NOTREACHED();
  return out;
}

Harvester::Harvester(zx_handle_t root_resource, async_dispatcher_t* dispatcher,
                     std::unique_ptr<DockyardProxy> dockyard_proxy)
    : root_resource_(root_resource),
      dispatcher_(dispatcher),
      dockyard_proxy_(std::move(dockyard_proxy)) {}

void Harvester::GatherData() {
  zx::time now = async::Now(dispatcher_);

  gather_cpu_.PostUpdate(dispatcher_, now, zx::msec(100));
  gather_inspectable_.PostUpdate(dispatcher_, now, zx::sec(3));
  gather_introspection_.PostUpdate(dispatcher_, now, zx::sec(10));
  gather_memory_.PostUpdate(dispatcher_, now, zx::msec(100));
  gather_tasks_.PostUpdate(dispatcher_, now, zx::sec(2));
}

}  // namespace harvester
