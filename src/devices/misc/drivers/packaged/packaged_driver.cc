// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/inspect/component/cpp/component.h>

#include <optional>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fio = fuchsia_io;

namespace {

class PackagedDriver : public driver::DriverBase {
 public:
  PackagedDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher dispatcher)
      : driver::DriverBase("packaged", std::move(start_args), std::move(dispatcher)) {}

  zx::status<> Start() override {
    exposed_inspector_.emplace(
        inspect::ComponentInspector(context().outgoing()->component(), async_dispatcher()));
    auto& root = exposed_inspector_->root();
    root.RecordString("hello", "world");

    FDF_SLOG(DEBUG, "Debug world");
    FDF_SLOG(INFO, "Hello world", KV("The answer is", 42));
    return zx::ok();
  }

 private:
  std::optional<inspect::ComponentInspector> exposed_inspector_ = std::nullopt;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<PackagedDriver>);
