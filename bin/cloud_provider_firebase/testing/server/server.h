// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_SERVER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_SERVER_H_

#include <map>

#include <fuchsia/cpp/network.h>
#include "lib/fxl/macros.h"

namespace ledger {

// Base implementation for simulating a cloud server.
class Server {
 public:
  Server();
  virtual ~Server();

  // Serves the given request.
  void Serve(network::URLRequest request,
             std::function<void(network::URLResponse)> callback);

 protected:
  enum class ResponseCode { kOk = 200, kUnauthorized = 401, kNotFound = 404 };

  virtual void HandleGet(network::URLRequest request,
                         std::function<void(network::URLResponse)> callback);
  virtual void HandleGetStream(
      network::URLRequest request,
      std::function<void(network::URLResponse)> callback);
  virtual void HandlePatch(network::URLRequest request,
                           std::function<void(network::URLResponse)> callback);
  virtual void HandlePost(network::URLRequest request,
                          std::function<void(network::URLResponse)> callback);
  virtual void HandlePut(network::URLRequest request,
                         std::function<void(network::URLResponse)> callback);

  network::URLResponse BuildResponse(
      const std::string& url,
      ResponseCode code,
      zx::socket body,
      const std::map<std::string, std::string>& headers);

  network::URLResponse BuildResponse(const std::string& url,
                                     ResponseCode code,
                                     std::string body);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_SERVER_H_
