// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_INTEGRATION_TESTS_SCENIC_REALM_BUILDER_H_
#define SRC_UI_SCENIC_INTEGRATION_TESTS_SCENIC_REALM_BUILDER_H_

#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

namespace integration_tests {

using ProtocolName = std::string;

// Helper class for building a scenic realm. The scenic realm consists of three
// components:
//   * Scenic
//   * Mock Cobalt
//   * Fake Display Provider
// This class sets up the component topology and routes protocols between the test
// manager and its child components.
//
// The realm builder library is used to construct a realm during runtime with a
// topology as follows:
//       test_manager
//            |
//     <test component>
//            |
//       <realm root>
//            |          <-Test realm
// ----------------------------
//     /      |     \    <-Scenic realm
//  Scenic  Cobalt  Hdcp
class ScenicRealmBuilder {
 public:
  ScenicRealmBuilder();

  // Routes |protocol| from the realm root returned by |Build()| to the test fixtures
  // component. Should be used only for the protocols which are required by the test component.
  ScenicRealmBuilder& AddRealmProtocol(const ProtocolName& protocol);

  // Builds the realm with the provided components and routes and returns the realm root.
  sys::testing::experimental::RealmRoot Build();

 private:
  // Adds child components in the scenic realm and routes required protocols from the
  // test_manager to the realm.
  ScenicRealmBuilder& Init();

  sys::testing::experimental::RealmBuilder realm_builder_;
};

}  // namespace integration_tests

#endif  // SRC_UI_SCENIC_INTEGRATION_TESTS_SCENIC_REALM_BUILDER_H_
