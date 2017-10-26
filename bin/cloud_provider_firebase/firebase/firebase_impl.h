// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_FIREBASE_FIREBASE_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_FIREBASE_FIREBASE_IMPL_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "peridot/bin/cloud_provider_firebase/firebase/event_stream.h"
#include "peridot/bin/cloud_provider_firebase/firebase/firebase.h"
#include "peridot/bin/cloud_provider_firebase/firebase/status.h"
#include "peridot/bin/cloud_provider_firebase/firebase/watch_client.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"
#include "peridot/bin/ledger/callback/cancellable.h"
#include "peridot/bin/ledger/glue/socket/socket_drainer_client.h"
#include "peridot/bin/ledger/network/network_service.h"

#include <rapidjson/document.h>

namespace firebase {

class FirebaseImpl : public Firebase {
 public:
  // |db_id| is the identifier of the Firebase Realtime Database instance. E.g.,
  // if the database is hosted at https://example.firebaseio.com/, its
  // identifier is "example".
  //
  // |prefix| is a url prefix against which all requests will be made, without a
  // leading or trailing slash. (possible with slashes inside) If empty,
  // requests will be made against root of the database.
  FirebaseImpl(ledger::NetworkService* network_service,
               const std::string& db_id,
               const std::string& prefix);
  ~FirebaseImpl() override;

  // Firebase:
  void Get(const std::string& key,
           const std::vector<std::string>& query_params,
           std::function<void(Status status, const rapidjson::Value& value)>
               callback) override;
  void Put(const std::string& key,
           const std::vector<std::string>& query_params,
           const std::string& data,
           std::function<void(Status status)> callback) override;
  void Patch(const std::string& key,
             const std::vector<std::string>& query_params,
             const std::string& data,
             std::function<void(Status status)> callback) override;
  void Delete(const std::string& key,
              const std::vector<std::string>& query_params,
              std::function<void(Status status)> callback) override;
  void Watch(const std::string& key,
             const std::vector<std::string>& query_params,
             WatchClient* watch_client) override;
  void UnWatch(WatchClient* watch_client) override;

  const std::string& api_url() { return api_url_; }

 private:
  std::string BuildApiUrl(const std::string& db_id, const std::string& prefix);

  std::string BuildRequestUrl(
      const std::string& key,
      const std::vector<std::string>& query_params) const;

  void Request(
      const std::string& url,
      const std::string& method,
      const std::string& message,
      const std::function<void(Status status, std::string response)>& callback);

  void OnResponse(
      const std::function<void(Status status, std::string response)>& callback,
      network::URLResponsePtr response);

  void OnStream(WatchClient* watch_client, network::URLResponsePtr response);

  void OnStreamComplete(WatchClient* watch_client);

  void OnStreamEvent(WatchClient* watch_client,
                     Status status,
                     const std::string& event,
                     const std::string& payload);

  void HandleMalformedEvent(WatchClient* watch_client,
                            const std::string& event,
                            const std::string& payload,
                            const char error_description[]);

  ledger::NetworkService* const network_service_;
  // Api url against which requests are made, without a trailing slash.
  const std::string api_url_;

  callback::CancellableContainer requests_;
  callback::AutoCleanableSet<glue::SocketDrainerClient> drainers_;

  struct WatchData;
  std::map<WatchClient*, std::unique_ptr<WatchData>> watch_data_;
};

}  // namespace firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_FIREBASE_FIREBASE_IMPL_H_
