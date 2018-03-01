// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_
#define GARNET_LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_

#include "garnet/lib/backoff/backoff.h"
#include "garnet/lib/callback/auto_cleanable.h"
#include "garnet/lib/callback/scoped_task_runner.h"
#include "garnet/lib/network_wrapper/network_wrapper.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/network/fidl/network_service.fidl.h"

namespace network_wrapper {

class NetworkWrapperImpl : public NetworkWrapper {
 public:
  NetworkWrapperImpl(
      fxl::RefPtr<fxl::TaskRunner> task_runner,
      std::unique_ptr<backoff::Backoff> backoff,
      std::function<network::NetworkServicePtr()> network_service_factory);
  ~NetworkWrapperImpl() override;

  fxl::RefPtr<callback::Cancellable> Request(
      std::function<network::URLRequestPtr()> request_factory,
      std::function<void(network::URLResponsePtr)> callback) override;

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

#endif  // GARNET_LIB_NETWORK_WRAPPER_NETWORK_WRAPPER_IMPL_H_
