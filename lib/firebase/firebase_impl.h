// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_FIREBASE_IMPL_H_
#define PERIDOT_LIB_FIREBASE_FIREBASE_IMPL_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <lib/fit/function.h>
#include <rapidjson/document.h>

#include "lib/callback/auto_cleanable.h"
#include "lib/callback/cancellable.h"
#include "lib/network_wrapper/network_wrapper.h"
#include "peridot/lib/firebase/event_stream.h"
#include "peridot/lib/firebase/firebase.h"
#include "peridot/lib/firebase/status.h"
#include "peridot/lib/firebase/watch_client.h"
#include "peridot/lib/socket/socket_drainer_client.h"

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
  FirebaseImpl(network_wrapper::NetworkWrapper* network_wrapper,
               const std::string& db_id, const std::string& prefix);
  ~FirebaseImpl() override;

  // Firebase:
  void Get(const std::string& key, const std::vector<std::string>& query_params,
           fit::function<void(Status status,
                              std::unique_ptr<rapidjson::Value> value)>
               callback) override;
  void Put(const std::string& key, const std::vector<std::string>& query_params,
           const std::string& data,
           fit::function<void(Status status)> callback) override;
  void Patch(const std::string& key,
             const std::vector<std::string>& query_params,
             const std::string& data,
             fit::function<void(Status status)> callback) override;
  void Delete(const std::string& key,
              const std::vector<std::string>& query_params,
              fit::function<void(Status status)> callback) override;
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
      const std::string& url, const std::string& method,
      const std::string& message,
      fit::function<void(Status status, std::string response)> callback);

  void OnResponse(
      fit::function<void(Status status, std::string response)> callback,
      ::fuchsia::net::oldhttp::URLResponse response);

  void OnStream(WatchClient* watch_client,
                ::fuchsia::net::oldhttp::URLResponse response);

  void OnStreamComplete(WatchClient* watch_client);

  void OnStreamEvent(WatchClient* watch_client, Status status,
                     const std::string& event, const std::string& payload);

  void HandleMalformedEvent(WatchClient* watch_client, const std::string& event,
                            const std::string& payload,
                            const char error_description[]);

  network_wrapper::NetworkWrapper* const network_wrapper_;
  // Api url against which requests are made, without a trailing slash.
  const std::string api_url_;

  callback::CancellableContainer requests_;
  callback::AutoCleanableSet<socket::SocketDrainerClient> drainers_;

  struct WatchData;
  std::map<WatchClient*, std::unique_ptr<WatchData>> watch_data_;
};

}  // namespace firebase

#endif  // PERIDOT_LIB_FIREBASE_FIREBASE_IMPL_H_
