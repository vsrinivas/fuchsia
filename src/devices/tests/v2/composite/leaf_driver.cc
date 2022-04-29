// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.composite.test/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/promise.h>
#include <lib/driver2/record_cpp.h>
#include <lib/fpromise/scope.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_composite_test;

using fpromise::ok;
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
    driver->Run();
    return zx::ok(std::move(driver));
  }

 private:
  void Run() {
    // Start the driver.
    auto task = driver::Connect<ft::Waiter>(ns_, dispatcher_)
                    .and_then(fit::bind_member(this, &LeafDriver::DoWork))
                    .or_else(fit::bind_member(this, &LeafDriver::UnbindNode))
                    .wrap_with(scope_);
    executor_.schedule_task(std::move(task));
  }

  zx::status<uint32_t> ConnectToDeviceAndGetNumber(std::string path) {
    auto device = ns_.Connect<ft::Device>(path);
    if (device.status_value() != ZX_OK) {
      FDF_LOG(ERROR, "Failed to connect to %s: %s", path.data(), device.status_string());
      return device.take_error();
    }

    auto result = fidl::WireCall(*device)->GetNumber();
    if (result.status() != ZX_OK) {
      FDF_LOG(ERROR, "Failed to call number on %s: %s", path.data(), result.lossy_description());
      return zx::error(result.status());
    }
    return zx::ok(result->number);
  }

  result<void, zx_status_t> DoWork(const fidl::WireSharedClient<ft::Waiter>& waiter) {
    // Check the left device.
    auto number = ConnectToDeviceAndGetNumber("/fuchsia.composite.test.Service/left/device");
    if (number.is_error()) {
      __UNUSED auto result = waiter->Ack(number.error_value());
      return ok();
    }
    if (*number != 1) {
      FDF_LOG(ERROR, "Wrong number for left: expecting 1, saw %d", *number);
      __UNUSED auto result = waiter->Ack(ZX_ERR_INTERNAL);
      return ok();
    }

    // Check the right device.
    number = ConnectToDeviceAndGetNumber("/fuchsia.composite.test.Service/right/device");
    if (number.is_error()) {
      __UNUSED auto result = waiter->Ack(number.error_value());
      return ok();
    }
    if (*number != 2) {
      FDF_LOG(ERROR, "Wrong number for right: expecting 2, saw %d", *number);
      __UNUSED auto result = waiter->Ack(ZX_ERR_INTERNAL);
      return ok();
    }

    // Check the default device (which is the left device).
    number = ConnectToDeviceAndGetNumber("/fuchsia.composite.test.Service/default/device");
    if (number.is_error()) {
      __UNUSED auto result = waiter->Ack(number.error_value());
      return ok();
    }
    if (*number != 1) {
      FDF_LOG(ERROR, "Wrong number for default: expecting 1, saw %d", *number);
      __UNUSED auto result = waiter->Ack(ZX_ERR_INTERNAL);
      return ok();
    }

    __UNUSED auto result = waiter->Ack(ZX_OK);
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
