// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.interop.test/cpp/wire.h>
#include <lib/driver2/driver2_cpp.h>

namespace ft = fuchsia_interop_test;

namespace {

class LeafDriver : public driver::DriverBase {
 public:
  LeafDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("leaf", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::status<> Start() override {
    auto waiter = context().incoming()->Connect<ft::Waiter>();
    if (waiter.is_error()) {
      node().reset();
      return waiter.take_error();
    }
    const fidl::WireSharedClient<ft::Waiter> client{std::move(waiter.value()), dispatcher()};
    auto result = client.sync()->Ack();
    if (!result.ok()) {
      node().reset();
      return zx::error(result.error().status());
    }

    return zx::ok();
  }
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<LeafDriver>);
