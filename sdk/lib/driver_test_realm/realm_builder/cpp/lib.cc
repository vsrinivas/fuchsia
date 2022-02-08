// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/fd.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>

#include "lib/fidl/cpp/synchronous_interface_ptr.h"

namespace driver_test_realm {

using namespace component_testing;

void Setup(component_testing::RealmBuilder& realm_builder) {
  realm_builder.AddChild("driver_test_realm", "#meta/driver_test_realm.cm");
  realm_builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.logger.LogSink"}},
                               .source = {ParentRef()},
                               .targets = {ChildRef{"driver_test_realm"}}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.process.Launcher"}},
                               .source = {ParentRef()},
                               .targets = {ChildRef{"driver_test_realm"}}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.sys.Launcher"}},
                               .source = {ParentRef()},
                               .targets = {ChildRef{"driver_test_realm"}}});
  realm_builder.AddRoute(
      Route{.capabilities = {Protocol{"fuchsia.driver.development.DriverDevelopment"}},
            .source = {ChildRef{"driver_test_realm"}},
            .targets = {ParentRef()}});
  realm_builder.AddRoute(Route{.capabilities = {Protocol{"fuchsia.driver.test.Realm"}},
                               .source = {ChildRef{"driver_test_realm"}},
                               .targets = {ParentRef()}});
  realm_builder.AddRoute(
      Route{.capabilities = {Directory{.name = "dev", .rights = fuchsia::io::RW_STAR_DIR}},
            .source = {ChildRef{"driver_test_realm"}},
            .targets = {ParentRef()}});
}

}  // namespace driver_test_realm
