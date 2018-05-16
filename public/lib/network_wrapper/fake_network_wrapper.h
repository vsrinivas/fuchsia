// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETWORK_WRAPPER_FAKE_NETWORK_WRAPPER_H_
#define LIB_NETWORK_WRAPPER_FAKE_NETWORK_WRAPPER_H_

#include <network/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "lib/fxl/macros.h"
#include "lib/network_wrapper/network_wrapper.h"

namespace network_wrapper {

class FakeNetworkWrapper : public NetworkWrapper {
 public:
  explicit FakeNetworkWrapper(async_t* async);
  ~FakeNetworkWrapper() override;

  network::URLRequest* GetRequest();
  void ResetRequest();

  void SetResponse(network::URLResponse response);

  void SetSocketResponse(zx::socket body, uint32_t status_code);

  void SetStringResponse(const std::string& body, uint32_t status_code);

 private:
  // NetworkWrapper
  fxl::RefPtr<callback::Cancellable> Request(
      std::function<network::URLRequest()> request_factory,
      std::function<void(network::URLResponse)> callback) override;

  std::unique_ptr<network::URLRequest> request_received_;
  std::unique_ptr<network::URLResponse> response_to_return_;
  async_t* const async_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkWrapper);
};

}  // namespace network_wrapper

#endif  // LIB_NETWORK_WRAPPER_FAKE_NETWORK_WRAPPER_H_
