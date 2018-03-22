// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TEST_RUNNER_CPP_TEST_RUNNER_STORE_IMPL_H_
#define LIB_TEST_RUNNER_CPP_TEST_RUNNER_STORE_IMPL_H_

#include <map>
#include <queue>

#include "lib/test_runner/fidl/test_runner.fidl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fxl/macros.h"

namespace test_runner {

class TestRunnerStoreImpl : public TestRunnerStore {
 public:
  TestRunnerStoreImpl();
  void AddBinding(f1dl::InterfaceRequest<TestRunnerStore> req);

 private:
  // |TestRunnerStore|
  void Get(const f1dl::StringPtr& key, const GetCallback& cb) override;
  // |TestRunnerStore|
  void Put(const f1dl::StringPtr& key,
           const f1dl::StringPtr& value,
           const PutCallback& cb) override;

  void MaybeNotify(const std::string& key);

  std::map<f1dl::StringPtr, std::queue<GetCallback>> get_queue_;
  std::map<f1dl::StringPtr, std::queue<std::string>> store_;
  f1dl::BindingSet<TestRunnerStore> binding_set_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestRunnerStoreImpl);
};

}  // namespace test_runner

#endif  // LIB_TEST_RUNNER_CPP_TEST_RUNNER_STORE_IMPL_H_
