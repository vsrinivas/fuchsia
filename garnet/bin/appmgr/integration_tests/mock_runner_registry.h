// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_REGISTRY_H_
#define GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_REGISTRY_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <test/component/mockrunner/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"

namespace component {
namespace testing {

namespace mockrunner = test::component::mockrunner;

class MockRunnerWrapper {
 public:
  const mockrunner::MockRunnerPtr& runner_ptr() const { return runner_; }

  const std::vector<mockrunner::ComponentInfo>& components() const {
    return components_;
  }

  MockRunnerWrapper(mockrunner::MockRunnerPtr runner)
      : runner_(std::move(runner)) {
    runner_.events().OnComponentCreated =
        [this](mockrunner::ComponentInfo info) { components_.push_back(info); };

    runner_.events().OnComponentKilled = [this](uint64_t id) {
      for (auto it = components_.begin(); it != components_.end(); it++) {
        if (it->unique_id == id) {
          components_.erase(it);
          break;
        }
      }
    };
  }

 private:
  mockrunner::MockRunnerPtr runner_;
  std::vector<mockrunner::ComponentInfo> components_;
};

// we only have one mock runner so this only supports one. It can be extended to
// support more if required.
class MockRunnerRegistry : public mockrunner::MockRunnerRegistry {
 public:
  MockRunnerRegistry() : connect_count_(0), dead_runner_count_(0) {}

  fidl::InterfaceRequestHandler<mockrunner::MockRunnerRegistry> GetHandler() {
    return bindings_.GetHandler(this);
  }

  int connect_count() const { return connect_count_; }
  int dead_runner_count() const { return dead_runner_count_; }
  const MockRunnerWrapper* runner() const { return runner_.get(); }

  void Register(
      ::fidl::InterfaceHandle<mockrunner::MockRunner> runner) override;

 private:
  std::unique_ptr<MockRunnerWrapper> runner_;
  int connect_count_;
  int dead_runner_count_;
  fidl::BindingSet<mockrunner::MockRunnerRegistry> bindings_;
};

}  // namespace testing
}  // namespace component

#endif  // GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_REGISTRY_H_
