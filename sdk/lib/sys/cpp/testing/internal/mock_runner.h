// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_INTERNAL_MOCK_RUNNER_H_
#define LIB_SYS_CPP_TESTING_INTERNAL_MOCK_RUNNER_H_

#include <fuchsia/realm/builder/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>

#include <map>

#include <src/lib/fxl/macros.h>

namespace sys::testing::internal {

class MockRunner {
 public:
  MockRunner() = default;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(MockRunner);

  void Register(std::string mock_id, MockComponent* mock);

  void Bind(fidl::InterfaceHandle<fuchsia::realm::builder::FrameworkIntermediary> handle,
            async_dispatcher_t* dispatcher);

 private:
  bool Contains(std::string mock_id) const;

  std::map<std::string, MockComponent*> mocks_;
  fuchsia::realm::builder::FrameworkIntermediaryPtr framework_intermediary_proxy_;
};

}  // namespace sys::testing::internal

#endif  // LIB_SYS_CPP_TESTING_INTERNAL_MOCK_RUNNER_H_
