// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/fd.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>

#include "lib/fidl/cpp/synchronous_interface_ptr.h"

namespace driver_test_realm {

using namespace sys::testing;

void Setup(sys::testing::Realm::Builder& realm_builder) {
  realm_builder.AddComponent(Moniker{"driver_test_realm"},
                             Component{.source = ComponentUrl{"#meta/driver_test_realm.cm"}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.logger.LogSink"},
                                         .source = {AboveRoot()},
                                         .targets = {Moniker{"driver_test_realm"}}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.process.Launcher"},
                                         .source = {AboveRoot()},
                                         .targets = {Moniker{"driver_test_realm"}}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.sys.Launcher"},
                                         .source = {AboveRoot()},
                                         .targets = {Moniker{"driver_test_realm"}}});
  realm_builder.AddRoute(
      CapabilityRoute{.capability = Protocol{"fuchsia.driver.development.DriverDevelopment"},
                      .source = {Moniker{"driver_test_realm"}},
                      .targets = {AboveRoot()}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.driver.test.Realm"},
                                         .source = {Moniker{"driver_test_realm"}},
                                         .targets = {AboveRoot()}});
  realm_builder.AddRoute(
      CapabilityRoute{.capability = Directory{"dev", "dev", fuchsia::io2::Operations::CONNECT},
                      .source = {Moniker{"driver_test_realm"}},
                      .targets = {AboveRoot()}});
}

}  // namespace driver_test_realm
