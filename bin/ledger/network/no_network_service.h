// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_NETWORK_NO_NETWORK_SERVICE_H_
#define APPS_LEDGER_SRC_NETWORK_NO_NETWORK_SERVICE_H_

#include "apps/ledger/src/network/network_service.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"

namespace ledger {

class NoNetworkService : public NetworkService {
 public:
  explicit NoNetworkService(fxl::RefPtr<fxl::TaskRunner> task_runner);
  ~NoNetworkService() override;

  // NetworkService:
  fxl::RefPtr<callback::Cancellable> Request(
      std::function<network::URLRequestPtr()> request_factory,
      std::function<void(network::URLResponsePtr)> callback) override;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NoNetworkService);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_NETWORK_NO_NETWORK_SERVICE_H_
