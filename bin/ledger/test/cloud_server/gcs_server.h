// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_CLOUD_SERVER_GCS_SERVER_H_
#define PERIDOT_BIN_LEDGER_TEST_CLOUD_SERVER_GCS_SERVER_H_

#include <functional>
#include <map>

#include "lib/network/fidl/network_service.fidl.h"
#include "peridot/bin/ledger/test/cloud_server/server.h"

namespace ledger {

// Implementation of a google cloud storage server. This implementation is
// partial and only handles the part of the API that the Ledger application
// exercises.
class GcsServer : public Server {
 public:
  GcsServer();
  ~GcsServer() override;

 private:
  void HandleGet(
      network::URLRequestPtr request,
      std::function<void(network::URLResponsePtr)> callback) override;
  void HandlePost(
      network::URLRequestPtr request,
      std::function<void(network::URLResponsePtr)> callback) override;

  std::map<std::string, std::string> data_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TEST_CLOUD_SERVER_GCS_SERVER_H_
