// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/fidl.h>
#include <fidl/fuchsia.offers.test/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/promise.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/service_client.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

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
                                                       fdf::UnownedDispatcher dispatcher,
                                                       fidl::WireSharedClient<fdf::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto driver = std::make_unique<LeafDriver>(dispatcher->async_dispatcher(), std::move(node),
                                               std::move(ns), std::move(logger));
    zx_status_t status = driver->Run();
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx_status_t Run() {
    auto handshake = driver::Connect<ft::Service::Device>(ns_);
    if (handshake.is_error()) {
      return handshake.status_value();
    }
    handshake_.Bind(*std::move(handshake), dispatcher_);

    auto waiter = ns_.Connect<ft::Waiter>();
    if (waiter.is_error()) {
      return waiter.status_value();
    }
    waiter_.Bind(*std::move(waiter), dispatcher_);

    // Start the driver.
    auto task = CallDo()
                    .and_then(fit::bind_member(this, &LeafDriver::CallAck))
                    .or_else(fit::bind_member(this, &LeafDriver::UnbindNode))
                    .wrap_with(scope_);
    executor_.schedule_task(std::move(task));
    return ZX_OK;
  }

  promise<void, zx_status_t> CallDo() {
    fpromise::bridge<void, zx_status_t> bridge;
    auto callback =
        [completer = std::move(bridge.completer)](fidl::Result<ft::Handshake::Do>& result) mutable {
          if (!result.is_ok()) {
            completer.complete_error(result.error_value().status());
            return;
          }
          completer.complete_ok();
        };
    handshake_->Do().ThenExactlyOnce(std::move(callback));
    return bridge.consumer.promise();
  }

  result<void, zx_status_t> CallAck() {
    __UNUSED auto result = waiter_->Ack();
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

  fidl::Client<ft::Handshake> handshake_;
  fidl::Client<ft::Waiter> waiter_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(LeafDriver);
