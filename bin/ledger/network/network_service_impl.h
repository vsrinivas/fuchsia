// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_IMPL_H_
#define APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_IMPL_H_

#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/network/network_service.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace ledger {

class NetworkServiceImpl : public NetworkService {
 public:
  NetworkServiceImpl(
      fxl::RefPtr<fxl::TaskRunner> task_runner,
      std::function<network::NetworkServicePtr()> network_service_factory);
  ~NetworkServiceImpl() override;

  fxl::RefPtr<callback::Cancellable> Request(
      std::function<network::URLRequestPtr()> request_factory,
      std::function<void(network::URLResponsePtr)> callback) override;

 private:
  class RunningRequest;

  network::NetworkService* GetNetworkService();

  void RetryGetNetworkService();

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  backoff::ExponentialBackoff backoff_;
  bool in_backoff_ = false;
  std::function<network::NetworkServicePtr()> network_service_factory_;
  network::NetworkServicePtr network_service_;
  callback::AutoCleanableSet<RunningRequest> running_requests_;

  // Must be the last member field.
  fxl::WeakPtrFactory<NetworkServiceImpl> weak_factory_;
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_IMPL_H_
