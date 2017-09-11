// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_H_
#define APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_H_

#include "apps/ledger/src/callback/cancellable.h"
#include "apps/network/services/url_request.fidl.h"
#include "apps/network/services/url_response.fidl.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace ledger {

// Abstraction for the network service. It will reconnect to the network service
// application in case of disconnection, as well as handle 307 and 308
// redirections.
class NetworkService {
 public:
  NetworkService() {}
  virtual ~NetworkService() {}

  // Starts a url network request.
  virtual fxl::RefPtr<callback::Cancellable> Request(
      std::function<network::URLRequestPtr()> request_factory,
      std::function<void(network::URLResponsePtr)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(NetworkService);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_NETWORK_NETWORK_SERVICE_H_
