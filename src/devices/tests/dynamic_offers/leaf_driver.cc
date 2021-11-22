// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.offers.test/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>

#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/promise.h"
#include "src/devices/lib/driver2/record.h"

namespace fdf = fuchsia_driver_framework;
namespace ft = fuchsia_offers_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class LeafDriver {
 public:
  explicit LeafDriver(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher), executor_(dispatcher) {}

  zx::status<> Start(fdf::wire::DriverStartArgs* start_args) {
    // Bind the node.
    node_.Bind(std::move(start_args->node()), dispatcher_);

    // Create the namespace.
    auto ns = driver::Namespace::Create(start_args->ns());
    if (ns.is_error()) {
      return ns.take_error();
    }
    ns_ = std::move(*ns);

    // Create the logger.
    auto logger = driver::Logger::Create(ns_, dispatcher_, "leaf");
    if (logger.is_error()) {
      return logger.take_error();
    }
    logger_ = std::move(*logger);

    // Start the driver.
    auto start_driver = driver::Connect<ft::Handshake>(ns_, dispatcher_)
                            .and_then(fit::bind_member(this, &LeafDriver::CallDo))
                            .and_then(driver::Connect<ft::Waiter>(ns_, dispatcher_))
                            .and_then(fit::bind_member(this, &LeafDriver::CallAck))
                            .or_else(fit::bind_member(this, &LeafDriver::UnbindNode))
                            .wrap_with(scope_);
    executor_.schedule_task(std::move(start_driver));
    return zx::ok();
  }

 private:
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

zx_status_t DriverStart(fidl_incoming_msg_t* msg, async_dispatcher_t* dispatcher, void** driver) {
  fidl::DecodedMessage<fdf::wire::DriverStartArgs> decoded(msg);
  if (!decoded.ok()) {
    return decoded.status();
  }

  auto root_driver = std::make_unique<LeafDriver>(dispatcher);
  auto start = root_driver->Start(decoded.PrimaryObject());
  if (start.is_error()) {
    return start.error_value();
  }

  *driver = root_driver.release();
  return ZX_OK;
}

zx_status_t DriverStop(void* driver) {
  delete static_cast<LeafDriver*>(driver);
  return ZX_OK;
}

}  // namespace

FUCHSIA_DRIVER_RECORD_V1(.start = DriverStart, .stop = DriverStop);
