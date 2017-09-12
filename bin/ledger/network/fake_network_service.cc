// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/network/fake_network_service.h"

#include <utility>

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/functional/make_copyable.h"

namespace ledger {

FakeNetworkService::FakeNetworkService(fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

FakeNetworkService::~FakeNetworkService() {}

network::URLRequest* FakeNetworkService::GetRequest() {
  return request_received_.get();
}

void FakeNetworkService::ResetRequest() {
  request_received_.reset();
}

void FakeNetworkService::SetResponse(network::URLResponsePtr response) {
  response_to_return_ = std::move(response);
}

void FakeNetworkService::SetSocketResponse(mx::socket body,
                                           uint32_t status_code) {
  network::URLResponsePtr server_response = network::URLResponse::New();
  server_response->body = network::URLBody::New();
  server_response->body->set_stream(std::move(body));
  server_response->status_code = status_code;
  SetResponse(std::move(server_response));
}

void FakeNetworkService::SetStringResponse(const std::string& body,
                                           uint32_t status_code) {
  SetSocketResponse(fsl::WriteStringToSocket(body), status_code);
}

fxl::RefPtr<callback::Cancellable> FakeNetworkService::Request(
    std::function<network::URLRequestPtr()> request_factory,
    std::function<void(network::URLResponsePtr)> callback) {
  std::unique_ptr<bool> cancelled = std::make_unique<bool>(false);

  bool* cancelled_ptr = cancelled.get();
  auto cancellable = callback::CancellableImpl::Create(fxl::MakeCopyable(
      [cancelled = std::move(cancelled)] { *cancelled = true; }));
  if (!response_to_return_) {
    return cancellable;
  }

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
