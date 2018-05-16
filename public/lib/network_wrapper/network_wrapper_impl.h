// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_
#define LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_

#include <network/cpp/fidl.h>

#include "lib/backoff/backoff.h"
#include "lib/callback/auto_cleanable.h"
#include "lib/callback/scoped_task_runner.h"
#include "lib/network_wrapper/network_wrapper.h"

namespace network_wrapper {

class NetworkWrapperImpl : public NetworkWrapper {
 public:
  NetworkWrapperImpl(
      async_t* async, std::unique_ptr<backoff::Backoff> backoff,
      std::function<network::NetworkServicePtr()> network_service_factory);
  ~NetworkWrapperImpl() override;

  fxl::RefPtr<callback::Cancellable> Request(
      std::function<network::URLRequest()> request_factory,
      std::function<void(network::URLResponse)> callback) override;

 private:
  class RunningRequest;

  network::NetworkService* GetNetworkService();

  void RetryGetNetworkService();

  std::unique_ptr<backoff::Backoff> backoff_;
  bool in_backoff_ = false;
  std::function<network::NetworkServicePtr()> network_service_factory_;
  network::NetworkServicePtr network_service_;
  callback::AutoCleanableSet<RunningRequest> running_requests_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace network_wrapper

#endif  // LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_
