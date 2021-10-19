// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_MOCK_RUNNER_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_MOCK_RUNNER_H_

#include <fuchsia/component/test/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <map>

namespace sys::testing::internal {

class MockRunner final {
 public:
  MockRunner() = default;

  MockRunner(MockRunner&& other) = delete;
  MockRunner& operator=(MockRunner&& other) = delete;

  MockRunner(MockRunner& other) = delete;
  MockRunner& operator=(MockRunner& other) = delete;

  void Register(std::string mock_id, MockComponent* mock);

  void Bind(fidl::InterfaceHandle<fuchsia::component::test::RealmBuilder> handle,
            async_dispatcher_t* dispatcher);

 private:
  bool Contains(std::string mock_id) const;

  std::map<std::string, MockComponent*> mocks_;
  fuchsia::component::test::RealmBuilderPtr realm_builder_proxy_;
};

}  // namespace sys::testing::internal

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_INTERNAL_MOCK_RUNNER_H_
