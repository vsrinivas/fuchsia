// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_INTEGRATION_TESTS_SCENIC_REALM_BUILDER_H_
#define SRC_UI_SCENIC_INTEGRATION_TESTS_SCENIC_REALM_BUILDER_H_

#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

namespace integration_tests {
using ProtocolName = std::string;
using SubRealmUrl = std::string;

// Helper class for building a scenic subrealm. The scenic subrealm consists of a scenic component
// and a fake display provider component. This class sets up the component topology and routes
// protocols between the test manager and its child components.
//
// The realm builder library is used to construct a realm during runtime with a topology as follows:
//    test_manager
//        |
//   <Test component>
//        |
//   <realm root>
//        |
// ------------------
//     /     \ Scenic subrealm
//  Scenic    Hdcp
class ScenicRealmBuilder {
 public:
  ScenicRealmBuilder(const SubRealmUrl& url);

  // Routes |protocol| from the scenic subrealm to the test fixtures component. Should be used only
  // for the protocols which are required by the test component.
  ScenicRealmBuilder& AddScenicSubRealmProtocol(const ProtocolName& protocol);

  // Builds the realm with the provided components and routes and returns the realm root.
  std::unique_ptr<sys::testing::experimental::RealmRoot> Build();

 private:
  // Adds child components in the scenic subrealm and routes required protocols from the
  // test_manager to the subrealm. |url| refers to the package url for the scenic subrealm.
  ScenicRealmBuilder& Init(const SubRealmUrl& url);

  sys::testing::experimental::RealmBuilder realm_builder_;
};

}  // namespace integration_tests

#endif  // SRC_UI_SCENIC_INTEGRATION_TESTS_SCENIC_REALM_BUILDER_H_
