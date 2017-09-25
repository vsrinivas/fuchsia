// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_NETWORK_NO_NETWORK_SERVICE_H_
#define PERIDOT_BIN_LEDGER_NETWORK_NO_NETWORK_SERVICE_H_

#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "peridot/bin/ledger/network/network_service.h"

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

#endif  // PERIDOT_BIN_LEDGER_NETWORK_NO_NETWORK_SERVICE_H_
