// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TEST_RUNNER_CPP_TEST_RUNNER_STORE_IMPL_H_
#define LIB_TEST_RUNNER_CPP_TEST_RUNNER_STORE_IMPL_H_

#include <map>
#include <queue>

#include <test_runner/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fxl/macros.h"

namespace test_runner {

class TestRunnerStoreImpl : public TestRunnerStore {
 public:
  TestRunnerStoreImpl();
  void AddBinding(fidl::InterfaceRequest<TestRunnerStore> req);

 private:
  // |TestRunnerStore|
  void Get(fidl::StringPtr key, GetCallback cb) override;
  // |TestRunnerStore|
  void Put(fidl::StringPtr key,
           fidl::StringPtr value,
           PutCallback cb) override;

  void MaybeNotify(const std::string& key);

  std::map<fidl::StringPtr, std::queue<GetCallback>> get_queue_;
  std::map<fidl::StringPtr, std::queue<std::string>> store_;
  fidl::BindingSet<TestRunnerStore> binding_set_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestRunnerStoreImpl);
};

}  // namespace test_runner

#endif  // LIB_TEST_RUNNER_CPP_TEST_RUNNER_STORE_IMPL_H_
