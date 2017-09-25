// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_NETWORK_NETWORK_SERVICE_IMPL_H_
#define PERIDOT_BIN_LEDGER_NETWORK_NETWORK_SERVICE_IMPL_H_

#include "peridot/bin/ledger/backoff/exponential_backoff.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"
#include "peridot/bin/ledger/callback/scoped_task_runner.h"
#include "peridot/bin/ledger/network/network_service.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/network/fidl/network_service.fidl.h"

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

  backoff::ExponentialBackoff backoff_;
  bool in_backoff_ = false;
  std::function<network::NetworkServicePtr()> network_service_factory_;
  network::NetworkServicePtr network_service_;
  callback::AutoCleanableSet<RunningRequest> running_requests_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_NETWORK_NETWORK_SERVICE_IMPL_H_
