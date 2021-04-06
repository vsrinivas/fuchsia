// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_FIDL_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_FIDL_H_

#include <fuchsia/systemmonitor/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include "dockyard_proxy_grpc.h"
#include "fuchsia_clock.h"
#include "harvester.h"

namespace harvester::fidl {

class HarvesterImpl : public fuchsia::systemmonitor::Harvester {
 public:
  HarvesterImpl(async_dispatcher_t* fast_dispatcher,
                async_dispatcher_t* slow_dispatcher,
                std::shared_ptr<harvester::FuchsiaClock> clock)
      : fast_dispatcher_(fast_dispatcher),
        slow_dispatcher_(fast_dispatcher),
        clock_(clock) {}
  void ConnectGrpc(zx::socket socket, ConnectGrpcCallback cb) override;

 private:
  std::unique_ptr<harvester::FuchsiaClock> GetClock();

  async_dispatcher_t* fast_dispatcher_;
  async_dispatcher_t* slow_dispatcher_;
  std::vector<std::unique_ptr<harvester::Harvester>> harvesters_;
  std::shared_ptr<harvester::FuchsiaClock> clock_;
};

}  // namespace harvester::fidl

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_FIDL_H_
