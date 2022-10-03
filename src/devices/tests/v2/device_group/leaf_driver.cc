// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.devicegroup.test/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver2/service_client.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_devicegroup_test;

namespace {

class LeafDriver : public driver::DriverBase {
 public:
  LeafDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("leaf", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::status<> Start() override {
    auto result = async::PostTask(dispatcher(), [&]() { RunAsync(); });
    if (result == ZX_OK) {
      return zx::ok();
    }

    return zx::error(result);
  }

  void RunAsync() {
    auto connect_result = context().incoming()->Connect<ft::Waiter>();
    if (connect_result.is_error()) {
      FDF_LOG(ERROR, "Failed to start leaf driver: %s", connect_result.status_string());
      node().reset();
      return;
    }

    const fidl::WireSharedClient<ft::Waiter> client{std::move(connect_result.value()),
                                                    dispatcher()};
    auto work_result = DoWork(client);
    if (work_result.is_error()) {
      FDF_LOG(ERROR, "DoWork was not successful: %s", work_result.status_string());
      return;
    }

    FDF_LOG(INFO, "Completed RunAsync successfully.");
  }

 private:
  zx::status<uint32_t> GetNumber(std::string_view instance) {
    auto device = driver::Connect<ft::Service::Device>(*context().incoming(), instance);
    if (device.status_value() != ZX_OK) {
      FDF_LOG(ERROR, "Failed to connect to %s: %s", instance.data(), device.status_string());
      return device.take_error();
    }

    auto result = fidl::WireCall(*device)->GetNumber();
    if (result.status() != ZX_OK) {
      FDF_LOG(ERROR, "Failed to call number on %s: %s", instance.data(),
              result.lossy_description());
      return zx::error(result.status());
    }
    return zx::ok(result.value().number);
  }

  zx::status<> DoWork(const fidl::WireSharedClient<ft::Waiter>& waiter) {
    // Check the left device.
    auto number = GetNumber("left");
    if (number.is_error()) {
      __UNUSED auto result = waiter->Ack(number.error_value());
      return zx::ok();
    }
    if (*number != 1) {
      FDF_LOG(ERROR, "Wrong number for left: expecting 1, saw %d", *number);
      __UNUSED auto result = waiter->Ack(ZX_ERR_INTERNAL);
      return zx::ok();
    }

    // Check the right device.
    number = GetNumber("right");
    if (number.is_error()) {
      __UNUSED auto result = waiter->Ack(number.error_value());
      return zx::ok();
    }
    if (*number != 2) {
      FDF_LOG(ERROR, "Wrong number for right: expecting 2, saw %d", *number);
      __UNUSED auto result = waiter->Ack(ZX_ERR_INTERNAL);
      return zx::ok();
    }

    // Check the default device (which is the left device).
    number = GetNumber("default");
    if (number.is_error()) {
      __UNUSED auto result = waiter->Ack(number.error_value());
      return zx::ok();
    }
    if (*number != 1) {
      FDF_LOG(ERROR, "Wrong number for default: expecting 1, saw %d", *number);
      __UNUSED auto result = waiter->Ack(ZX_ERR_INTERNAL);
      return zx::ok();
    }

    __UNUSED auto result = waiter->Ack(ZX_OK);
    return zx::ok();
  }
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<LeafDriver>);
