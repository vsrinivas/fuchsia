// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/network_wrapper/fake_network_wrapper.h"

#include <utility>

#include <lib/async/cpp/task.h>

#include "lib/callback/cancellable_helper.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/functional/make_copyable.h"

namespace network_wrapper {

namespace http = ::fuchsia::net::oldhttp;

FakeNetworkWrapper::FakeNetworkWrapper(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

FakeNetworkWrapper::~FakeNetworkWrapper() {}

http::URLRequest* FakeNetworkWrapper::GetRequest() {
  return request_received_.get();
}

void FakeNetworkWrapper::ResetRequest() { request_received_.reset(); }

void FakeNetworkWrapper::SetResponse(http::URLResponse response) {
  response_to_return_ = fidl::MakeOptional(std::move(response));
}

void FakeNetworkWrapper::SetSocketResponse(zx::socket body,
                                           uint32_t status_code) {
  http::URLResponse server_response;
  server_response.body = http::URLBody::New();
  server_response.body->set_stream(std::move(body));
  server_response.status_code = status_code;
  SetResponse(std::move(server_response));
}

void FakeNetworkWrapper::SetStringResponse(const std::string& body,
                                           uint32_t status_code) {
  SetSocketResponse(fsl::WriteStringToSocket(body), status_code);
}

fxl::RefPtr<callback::Cancellable> FakeNetworkWrapper::Request(
    fit::function<http::URLRequest()> request_factory,
    fit::function<void(http::URLResponse)> callback) {
  std::unique_ptr<bool> cancelled = std::make_unique<bool>(false);

  bool* cancelled_ptr = cancelled.get();
  auto cancellable = callback::CancellableImpl::Create(fxl::MakeCopyable(
      [cancelled = std::move(cancelled)] { *cancelled = true; }));
  if (!response_to_return_) {
    return cancellable;
  }

  async::PostTask(dispatcher_,
                  [this, cancelled_ptr,
                   callback = cancellable->WrapCallback(std::move(callback)),
                   request_factory = std::move(request_factory)] {
                    if (!*cancelled_ptr) {
                      request_received_ = fidl::MakeOptional(request_factory());
                      callback(std::move(*response_to_return_));
                      response_to_return_.reset();
                    }
                  });
  return cancellable;
}

}  // namespace network_wrapper
