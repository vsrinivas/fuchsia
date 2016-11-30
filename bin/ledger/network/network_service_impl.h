// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_IMPL_H_
#define APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_IMPL_H_

#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/network/network_service.h"
#include "apps/network/services/network_service.fidl.h"

namespace ledger {

class NetworkServiceImpl : public NetworkService {
 public:
  NetworkServiceImpl(
      std::function<network::NetworkServicePtr()> network_service_factory);
  ~NetworkServiceImpl() override;

  ftl::RefPtr<callback::Cancellable> Request(
      std::function<network::URLRequestPtr()> request_factory,
      std::function<void(network::URLResponsePtr)> callback) override;

 private:
  class RunningRequest;

  network::NetworkService* GetNetworkService();

  std::function<network::NetworkServicePtr()> network_service_factory_;
  network::NetworkServicePtr network_service_;
  callback::AutoCleanableSet<RunningRequest> running_requests_;
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_IMPL_H_
