// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.runtime.test/cpp/fidl.h>
#include <lib/driver2/driver2_cpp.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_runtime_test;

namespace {

class LeafDriver : public driver::DriverBase {
 public:
  LeafDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("leaf", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::status<> Start() override {
    // Test we can block on the dispatcher thread.
    ZX_ASSERT(ZX_OK == DoHandshakeSynchronously());

    auto waiter = context().incoming()->Connect<ft::Waiter>();
    if (waiter.is_error()) {
      node().reset();
      return waiter.take_error();
    }

    const fidl::WireSharedClient<ft::Waiter> client(std::move(waiter.value()), dispatcher());
    auto result = client.sync()->Ack();
    if (!result.ok()) {
      node().reset();
      return zx::error(result.error().status());
    }

    return zx::ok();
  }

 private:
  zx_status_t DoHandshakeSynchronously() {
    ZX_ASSERT((*driver_dispatcher()->options() & FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS) ==
              FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS);

    auto result = context().incoming()->Connect<ft::Handshake>();
    if (result.is_error()) {
      return result.status_value();
    }
    const fidl::WireSharedClient<ft::Handshake> client(std::move(*result), dispatcher());
    return client.sync()->Do().status();
  }
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<LeafDriver>);
