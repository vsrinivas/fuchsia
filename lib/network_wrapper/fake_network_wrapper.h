// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_NETWORK_FAKE_NETWORK_WRAPPER_H_
#define GARNET_LIB_NETWORK_FAKE_NETWORK_WRAPPER_H_

#include "garnet/lib/network_wrapper/network_wrapper.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include <fuchsia/cpp/network.h>

namespace network_wrapper {

class FakeNetworkWrapper : public NetworkWrapper {
 public:
  explicit FakeNetworkWrapper(fxl::RefPtr<fxl::TaskRunner> task_runner);
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

  network::URLRequest request_received_;
  network::URLResponse response_to_return_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkWrapper);
};

}  // namespace network_wrapper

#endif  // GARNET_LIB_NETWORK_FAKE_NETWORK_WRAPPER_H_
