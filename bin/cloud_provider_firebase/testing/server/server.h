// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_SERVER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_SERVER_H_

#include <map>

#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include "lib/fxl/macros.h"

namespace ledger {

// Base implementation for simulating a cloud server.
class Server {
 public:
  Server();
  virtual ~Server();

  // Serves the given request.
  void Serve(
      ::fuchsia::net::oldhttp::URLRequest request,
      std::function<void(::fuchsia::net::oldhttp::URLResponse)> callback);

 protected:
  enum class ResponseCode { kOk = 200, kUnauthorized = 401, kNotFound = 404 };

  virtual void HandleGet(
      ::fuchsia::net::oldhttp::URLRequest request,
      std::function<void(::fuchsia::net::oldhttp::URLResponse)> callback);
  virtual void HandleGetStream(
      ::fuchsia::net::oldhttp::URLRequest request,
      std::function<void(::fuchsia::net::oldhttp::URLResponse)> callback);
  virtual void HandlePatch(
      ::fuchsia::net::oldhttp::URLRequest request,
      std::function<void(::fuchsia::net::oldhttp::URLResponse)> callback);
  virtual void HandlePost(
      ::fuchsia::net::oldhttp::URLRequest request,
      std::function<void(::fuchsia::net::oldhttp::URLResponse)> callback);
  virtual void HandlePut(
      ::fuchsia::net::oldhttp::URLRequest request,
      std::function<void(::fuchsia::net::oldhttp::URLResponse)> callback);

  ::fuchsia::net::oldhttp::URLResponse BuildResponse(
      const std::string& url, ResponseCode code, zx::socket body,
      const std::map<std::string, std::string>& headers);

  ::fuchsia::net::oldhttp::URLResponse BuildResponse(const std::string& url,
                                                     ResponseCode code,
                                                     std::string body);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_SERVER_H_
