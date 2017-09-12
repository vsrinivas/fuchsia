// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_CLOUD_SERVER_FAKE_CLOUD_URL_LOADER_H_
#define APPS_LEDGER_SRC_TEST_CLOUD_SERVER_FAKE_CLOUD_URL_LOADER_H_

#include <unordered_map>

#include "apps/ledger/src/test/cloud_server/firebase_server.h"
#include "apps/ledger/src/test/cloud_server/gcs_server.h"
#include "lib/fxl/macros.h"
#include "lib/network/fidl/network_service.fidl.h"

namespace ledger {

// Implementation of |URLLoader| that simulate Firebase and GCS
// servers.
class FakeCloudURLLoader : public network::URLLoader {
 public:
  FakeCloudURLLoader();
  ~FakeCloudURLLoader() override;

  // URLLoader
  void Start(network::URLRequestPtr request,
             const StartCallback& callback) override;
  void FollowRedirect(const FollowRedirectCallback& callback) override;
  void QueryStatus(const QueryStatusCallback& callback) override;

 private:
  std::unordered_map<std::string, FirebaseServer> firebase_servers_;
  std::unordered_map<std::string, GcsServer> gcs_servers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeCloudURLLoader);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_TEST_CLOUD_SERVER_FAKE_CLOUD_URL_LOADER_H_
