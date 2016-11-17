// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/network/fake_network_service.h"
#include "apps/ledger/src/callback/cancellable_helper.h"
#include "lib/ftl/functional/make_copyable.h"

namespace ledger {

FakeNetworkService::FakeNetworkService(ftl::RefPtr<ftl::TaskRunner> task_runner)
    : task_runner_(task_runner) {}

FakeNetworkService::~FakeNetworkService() {}

ftl::RefPtr<callback::Cancellable> FakeNetworkService::Request(
    std::function<network::URLRequestPtr()>&& request_factory,
    std::function<void(network::URLResponsePtr)>&& callback) {
  std::unique_ptr<bool> cancelled = std::make_unique<bool>(false);

  bool* cancelled_ptr = cancelled.get();
  auto cancellable = callback::CancellableImpl::Create(ftl::MakeCopyable(
      [cancelled = std::move(cancelled)] { *cancelled = true; }));
  task_runner_->PostTask([
    this, cancelled_ptr, callback = cancellable->WrapCallback(callback),
    request_factory = std::move(request_factory)
  ] {
    if (!*cancelled_ptr) {
      request_received_ = request_factory();
      callback(std::move(response_to_return_));
    }
  });
  return cancellable;
}

}  // namespace ledger
