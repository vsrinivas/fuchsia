// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETWORK_WRAPPER_FAKE_NETWORK_WRAPPER_H_
#define LIB_NETWORK_WRAPPER_FAKE_NETWORK_WRAPPER_H_

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "lib/fxl/macros.h"
#include "lib/network_wrapper/network_wrapper.h"

namespace network_wrapper {

class FakeNetworkWrapper : public NetworkWrapper {
 public:
  explicit FakeNetworkWrapper(async_dispatcher_t* dispatcher);
  ~FakeNetworkWrapper() override;

  ::fuchsia::net::oldhttp::URLRequest* GetRequest();
  void ResetRequest();

  void SetResponse(::fuchsia::net::oldhttp::URLResponse response);

  void SetSocketResponse(zx::socket body, uint32_t status_code);

  void SetStringResponse(const std::string& body, uint32_t status_code);

 private:
  // NetworkWrapper
  fxl::RefPtr<callback::Cancellable> Request(
      fit::function<::fuchsia::net::oldhttp::URLRequest()> request_factory,
      fit::function<void(::fuchsia::net::oldhttp::URLResponse)> callback)
      override;

  std::unique_ptr<::fuchsia::net::oldhttp::URLRequest> request_received_;
  std::unique_ptr<::fuchsia::net::oldhttp::URLResponse> response_to_return_;
  async_dispatcher_t* const dispatcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkWrapper);
};

}  // namespace network_wrapper

#endif  // LIB_NETWORK_WRAPPER_FAKE_NETWORK_WRAPPER_H_
