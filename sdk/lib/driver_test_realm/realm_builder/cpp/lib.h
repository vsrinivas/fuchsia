// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_TEST_REALM_REALM_BUILDER_CPP_LIB_H_
#define LIB_DRIVER_TEST_REALM_REALM_BUILDER_CPP_LIB_H_

#include <lib/sys/component/cpp/testing/realm_builder.h>

namespace driver_test_realm {

// Sets up the DriverTestRealm component in the given `realm_builder`.
void Setup(component_testing::RealmBuilder& realm_builder);

}  // namespace driver_test_realm

#endif  // LIB_DRIVER_TEST_REALM_REALM_BUILDER_CPP_LIB_H_
