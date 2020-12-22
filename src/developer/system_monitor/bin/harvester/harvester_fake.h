// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_HARVESTER_FAKE_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_HARVESTER_FAKE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <iostream>
#include <string>

#include "dockyard_proxy.h"
#include "harvester.h"
#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

namespace harvester {

class Harvester;

class HarvesterFake : public Harvester {
 public:
  HarvesterFake(zx_handle_t info_resource,
                std::unique_ptr<DockyardProxy> dockyard_proxy)
      : Harvester(info_resource, /*dispatcher=*/nullptr,
                  std::move(dockyard_proxy)) {}

  void GatherData() {}

  void SetUpdatePeriod(dockyard::DockyardId dockyard_id,
                       zx::duration update_period);
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_HARVESTER_FAKE_H_
