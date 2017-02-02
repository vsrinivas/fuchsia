// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/test_runner/test_runner_store_impl.h"

namespace modular {
namespace testing {

TestRunnerStoreImpl::TestRunnerStoreImpl() = default;

void TestRunnerStoreImpl::AddBinding(
    fidl::InterfaceRequest<TestRunnerStore> req) {
  binding_set_.AddBinding(this, std::move(req));
}

void TestRunnerStoreImpl::Get(const fidl::String& key, const GetCallback& cb) {
  auto it = store_.find(key);
  cb(it == store_.end() ? nullptr : it->second);
}

void TestRunnerStoreImpl::Put(const fidl::String& key,
                              const fidl::String& value,
                              const PutCallback& cb) {
  store_[key] = value;
  cb();
}

}  // namespace testing
}  // namespace modular
