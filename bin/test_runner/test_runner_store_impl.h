// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_STORE_IMPL_H_
#define APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_STORE_IMPL_H_

#include <map>
#include <queue>

#include "apps/modular/services/test_runner/test_runner.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/string.h"

namespace modular {
namespace testing {

class TestRunnerStoreImpl : public TestRunnerStore {
 public:
  TestRunnerStoreImpl();
  void AddBinding(fidl::InterfaceRequest<TestRunnerStore> req);

 private:
  // |TestRunnerStore|
  void Get(const fidl::String& key, const GetCallback& cb) override;
  // |TestRunnerStore|
  void Put(const fidl::String& key, const fidl::String& value,
           const PutCallback& cb) override;

  void MaybeNotify(const std::string& key);

  std::map<fidl::String, std::queue<GetCallback>> get_queue_;
  std::map<fidl::String, std::queue<std::string>> store_;
  fidl::BindingSet<TestRunnerStore> binding_set_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestRunnerStoreImpl);
};

}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_STORE_IMPL_H_
