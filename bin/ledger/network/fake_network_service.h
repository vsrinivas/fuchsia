// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_NETWORK_FAKE_NETWORK_SERVICE_H_
#define PERIDOT_BIN_LEDGER_NETWORK_FAKE_NETWORK_SERVICE_H_

#include "peridot/bin/ledger/network/network_service.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/network/fidl/network_service.fidl.h"

namespace ledger {

class FakeNetworkService : public NetworkService {
 public:
  explicit FakeNetworkService(fxl::RefPtr<fxl::TaskRunner> task_runner);
  ~FakeNetworkService() override;

  network::URLRequest* GetRequest();
  void ResetRequest();

  void SetResponse(network::URLResponsePtr response);

  void SetSocketResponse(zx::socket body, uint32_t status_code);

  void SetStringResponse(const std::string& body, uint32_t status_code);

 private:
  // NetworkService
  fxl::RefPtr<callback::Cancellable> Request(
      std::function<network::URLRequestPtr()> request_factory,
      std::function<void(network::URLResponsePtr)> callback) override;

  network::URLRequestPtr request_received_;
  network::URLResponsePtr response_to_return_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeNetworkService);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_NETWORK_FAKE_NETWORK_SERVICE_H_
