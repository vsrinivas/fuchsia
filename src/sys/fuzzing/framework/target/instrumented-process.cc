// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/cpp/fidl.h>
#include <lib/zx/eventpair.h>
#include <zircon/time.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

using fuchsia::debugdata::Publisher;

// This class extends |Process| by automatically connecting in a public default constructor. The
// class is instantiated as a singleton below, and lives as long as the process. All other
// fuzzing-related code executed in the target runs as result of the singleton's constructor.
class InstrumentedProcess final {
 public:
  InstrumentedProcess() {
    Process::InstallHooks();
    context_ = ComponentContext::CreateAuxillary();
    process_ = std::make_unique<Process>(context_->executor());
    fidl::InterfaceHandle<Publisher> publisher;
    if (auto status = context_->Connect(publisher.NewRequest()); status != ZX_OK) {
      FX_LOGS(FATAL) << "Failed to connect to publisher: " << zx_status_get_string(status);
    }
    zx::eventpair send, recv;
    if (auto status = zx::eventpair::create(0, &send, &recv); status != ZX_OK) {
      FX_LOGS(FATAL) << "Failed to create eventpair: " << zx_status_get_string(status);
    }

    // NOTE!!! The fuzzing framework piggy-backs on the special handling for
    // `fuchsia.debugdata.Publisher` with regards to component environments' `debug` field,
    // component lifecycle events, and the component security policy exceptions.
    //
    // However, with the test_manager's `fuzz_env` environment, `fuchsia.debugdata.Publisher` is
    // provided by the `fuzz_coverage` component. This component will recast the connection to
    // `fuchsia.fuzzer.CoverageDataCollector` in order to provide enhanced capabilities like process
    // monitoring. Should either end be replace with an implementation that uses
    // `fuchsia.debugdata.Publisher` without recasting, the ordinals of the FIDL messages will not
    // match and the connection will be closed on error.
    //
    // The corresponding protocol recast happens here, before the handle is passed off to the
    // process.
    fidl::InterfaceHandle<CoverageDataCollector> collector(publisher.TakeChannel());

    context_->ScheduleTask(process_->Connect(std::move(collector), std::move(send)));
    if (auto status = context_->Run(); status != ZX_OK) {
      FX_LOGS(FATAL) << "Failed to start component context: " << zx_status_get_string(status);
    }
    if (auto status =
            recv.wait_one(kSync | ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite(), nullptr);
        status != ZX_OK) {
      FX_LOGS(FATAL) << "Failed to wait on eventpair: " << zx_status_get_string(status);
    }
  }

  ~InstrumentedProcess() { FX_NOTREACHED(); }

 private:
  std::unique_ptr<ComponentContext> context_;
  std::unique_ptr<Process> process_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(InstrumentedProcess);
};

namespace {

// The weakly linked symbols should be examined as late as possible, in order to guarantee all of
// the module constructors execute first. To achieve this, the singleton's constructor uses a
// priority attribute to ensure it is run just before |main|.
[[gnu::init_priority(0xffff)]] InstrumentedProcess gInstrumented;

}  // namespace
}  // namespace fuzzing
