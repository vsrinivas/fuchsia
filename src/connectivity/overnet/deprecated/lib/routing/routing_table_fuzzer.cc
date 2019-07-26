// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/overnet/routingtablefuzzer/cpp/fidl.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/overnet/protocol/formatting.h"
#include "src/connectivity/overnet/deprecated/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/deprecated/lib/protocol/fidl.h"
#include "src/connectivity/overnet/deprecated/lib/routing/routing_table.h"
#include "src/connectivity/overnet/deprecated/lib/testing/test_timer.h"

using namespace overnet;

namespace {

class RoutingTableFuzzer {
 public:
  RoutingTableFuzzer(bool log_stuff) : logging_(log_stuff ? new Logging(&timer_) : nullptr) {}

  void Run(fuchsia::overnet::routingtablefuzzer::RoutingTableFuzzPlan plan) {
    using namespace fuchsia::overnet::routingtablefuzzer;
    for (const auto& action : plan.actions) {
      switch (action.Which()) {
        case RoutingTableAction::Tag::kStepTime:
          timer_.Step(action.step_time());
          break;
        case RoutingTableAction::Tag::kUpdateNode:
          // Ignores failures in order to verify that the next input doesn't
          // crash.
          routing_table_.ProcessUpdate({fidl::Clone(action.update_node())}, {}, false);
          break;
        case RoutingTableAction::Tag::kUpdateLink:
          // Ignores failures in order to verify that the next input doesn't
          // crash.
          routing_table_.ProcessUpdate({}, {fidl::Clone(action.update_link())}, false);
          break;
        case RoutingTableAction::Tag::kUpdateFlush:
          // Ignores failures in order to verify that the next input doesn't
          // crash.
          routing_table_.ProcessUpdate({}, {}, true);
          break;
        case RoutingTableAction::Tag::kUnknown:
          break;
      }
    }
  }

 private:
  TestTimer timer_;
  struct Logging {
    Logging(Timer* timer) : tracer(timer) {}
    TraceCout tracer;
    ScopedRenderer set_tracer{&tracer};
  };
  std::unique_ptr<Logging> logging_;
  RoutingTable routing_table_{NodeId(1), &timer_, false};
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (auto buffer = Decode<fuchsia::overnet::routingtablefuzzer::RoutingTableFuzzPlan>(
          Slice::FromCopiedBuffer(data, size));
      buffer.is_ok()) {
    RoutingTableFuzzer(false).Run(std::move(*buffer));
  }
  return 0;
}
