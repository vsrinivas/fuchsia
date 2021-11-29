// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.offers.test/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>

#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/lib/driver2/promise.h"
#include "src/devices/lib/driver2/record_cpp.h"

namespace fdf = fuchsia_driver_framework;
namespace ft = fuchsia_offers_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class LeafDriver {
 public:
  LeafDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger)
      : dispatcher_(dispatcher),
        executor_(dispatcher),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {}

  static constexpr const char* Name() { return "leaf"; }

  static zx::status<std::unique_ptr<LeafDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                       async_dispatcher_t* dispatcher,
                                                       fidl::WireSharedClient<fdf::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto driver =
        std::make_unique<LeafDriver>(dispatcher, std::move(node), std::move(ns), std::move(logger));
    driver->Run();
    return zx::ok(std::move(driver));
  }

 private:
  void Run() {
    // Start the driver.
    auto task = driver::Connect<ft::Handshake>(ns_, dispatcher_)
                    .and_then(fit::bind_member(this, &LeafDriver::CallDo))
                    .and_then(driver::Connect<ft::Waiter>(ns_, dispatcher_))
                    .and_then(fit::bind_member(this, &LeafDriver::CallAck))
                    .or_else(fit::bind_member(this, &LeafDriver::UnbindNode))
                    .wrap_with(scope_);
    executor_.schedule_task(std::move(task));
  }

  promise<void, zx_status_t> CallDo(const fidl::WireSharedClient<ft::Handshake>& handshake) {
    fpromise::bridge<void, zx_status_t> bridge;
    auto callback = [completer = std::move(bridge.completer)](
                        fidl::WireUnownedResult<ft::Handshake::Do>& result) mutable {
      if (!result.ok()) {
        completer.complete_error(result.status());
        return;
      }
      completer.complete_ok();
    };
    handshake->Do(std::move(callback));
    return bridge.consumer.promise_or(error(ZX_ERR_UNAVAILABLE));
  }

  result<void, zx_status_t> CallAck(const fidl::WireSharedClient<ft::Waiter>& waiter) {
    waiter->Ack();
    return ok();
  }

  result<> UnbindNode(const zx_status_t& status) {
    FDF_LOG(ERROR, "Failed to start leaf driver: %s", zx_status_get_string(status));
    node_.AsyncTeardown();
    return ok();
  }

  async_dispatcher_t* const dispatcher_;
  async::Executor executor_;

  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(LeafDriver);
