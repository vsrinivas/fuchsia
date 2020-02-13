// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_INTEGRATION_TESTS_SANDBOX_NAMESPACE_TEST_H_
#define SRC_SYS_APPMGR_INTEGRATION_TESTS_SANDBOX_NAMESPACE_TEST_H_

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <memory>
#include <string>

#include "gtest/gtest.h"

class NamespaceTest : public sys::testing::TestWithEnvironment {
 protected:
  NamespaceTest() : component_context_(sys::ComponentContext::Create()) {}

  // Connects to a service provided by the environment.
  template <typename Interface>
  void ConnectToService(fidl::InterfaceRequest<Interface> request,
                        const std::string& interface_name = Interface::Name_) {
    component_context_->svc()->Connect(std::move(request), interface_name);
  }

  // Returns whether path exists.
  bool Exists(const char* path);

  // Expect that a path exists, and fail with a descriptive message
  void ExpectExists(const char* path);

  // Expect that a path does not exist, and fail with a descriptive message
  void ExpectDoesNotExist(const char* path);

  // Expect that the given path can be opened with the specified file/directory rights. All
  // filesystem rights bits can be checked: OPEN_RIGHT_READABLE, _WRITABLE, _EXECUTABLE, and _ADMIN.
  void ExpectPathSupportsRights(const char* path, uint32_t rights);

  // Expect that the given path can be opened with the specified file/directory rights, but no
  // greater. All filesystem rights bits can be checked: OPEN_RIGHT_READABLE, _WRITABLE,
  // _EXECUTABLE, and _ADMIN.
  void ExpectPathSupportsStrictRights(const char* path, uint32_t rights,
                                      bool require_access_denied = true);

 private:
  std::unique_ptr<sys::ComponentContext> component_context_;
};

#endif  // SRC_SYS_APPMGR_INTEGRATION_TESTS_SANDBOX_NAMESPACE_TEST_H_
