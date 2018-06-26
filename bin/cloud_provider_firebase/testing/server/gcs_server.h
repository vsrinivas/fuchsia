// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_GCS_SERVER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_GCS_SERVER_H_

#include <functional>
#include <map>

#include <fuchsia/net/oldhttp/cpp/fidl.h>

#include "peridot/bin/cloud_provider_firebase/testing/server/server.h"

namespace ledger {

// Implementation of a google cloud storage server. This implementation is
// partial and only handles the part of the API that the Ledger application
// exercises.
class GcsServer : public Server {
 public:
  GcsServer();
  ~GcsServer() override;

 private:
  void HandleGet(::fuchsia::net::oldhttp::URLRequest request,
                 std::function<void(::fuchsia::net::oldhttp::URLResponse)>
                     callback) override;
  void HandlePost(::fuchsia::net::oldhttp::URLRequest request,
                  std::function<void(::fuchsia::net::oldhttp::URLResponse)>
                      callback) override;

  std::map<std::string, std::string> data_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_GCS_SERVER_H_
