// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/test_runner_store_impl.h"

namespace test_runner {

TestRunnerStoreImpl::TestRunnerStoreImpl() = default;

void TestRunnerStoreImpl::AddBinding(
    fidl::InterfaceRequest<TestRunnerStore> req) {
  binding_set_.AddBinding(this, std::move(req));
}

void TestRunnerStoreImpl::Get(const fidl::String& key, const GetCallback& cb) {
  get_queue_[key].push(cb);
  MaybeNotify(key);
}

void TestRunnerStoreImpl::Put(const fidl::String& key,
                              const fidl::String& value,
                              const PutCallback& cb) {
  store_[key].push(value);
  MaybeNotify(key);
  cb();
}

void TestRunnerStoreImpl::MaybeNotify(const std::string& key) {
  auto store_it = store_.find(key);
  if (store_it == store_.end())
    return;

  auto get_queue_it = get_queue_.find(key);
  if (get_queue_it != get_queue_.end()) {
    get_queue_it->second.front()(store_it->second.front());
    get_queue_it->second.pop();
    store_it->second.pop();
  }

  if (get_queue_it->second.empty()) {
    get_queue_.erase(key);
  }

  if (store_it->second.empty()) {
    store_.erase(key);
  }
}

}  // namespace test_runner
