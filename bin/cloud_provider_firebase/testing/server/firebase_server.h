// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_FIREBASE_SERVER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_FIREBASE_SERVER_H_

#include <functional>
#include <memory>

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/url/gurl.h>
#include <rapidjson/document.h>

#include "peridot/bin/cloud_provider_firebase/testing/server/server.h"
#include "peridot/lib/convert/collection_view.h"

namespace ledger {

// Implementation of a google Firebase Real-time Database server. This
// implementation is partial and only handles the part of the API that the
// Ledger application exercises.
// In particular, the only query supported is 'startAt' with an integer value.
class FirebaseServer : public Server {
 public:
  using Path = std::vector<std::string>;
  using PathView = convert::CollectionView<Path>;

  FirebaseServer();
  ~FirebaseServer() override;

 private:
  // |Listeners| is a recursive structure anchored in a specific path of the
  // database, and mapping names to the Listener anchored in the path
  // constructed from the path of the current listener and the name.
  // Each listener contains all the current watchers of the Database registered
  // for its path.
  class Listeners;

  // Server implementation.
  void HandleGet(::fuchsia::net::oldhttp::URLRequest request,
                 fit::function<void(::fuchsia::net::oldhttp::URLResponse)>
                     callback) override;
  void HandlePatch(::fuchsia::net::oldhttp::URLRequest request,
                   fit::function<void(::fuchsia::net::oldhttp::URLResponse)>
                       callback) override;
  void HandlePut(::fuchsia::net::oldhttp::URLRequest request,
                 fit::function<void(::fuchsia::net::oldhttp::URLResponse)>
                     callback) override;
  void HandleGetStream(::fuchsia::net::oldhttp::URLRequest request,
                       fit::function<void(::fuchsia::net::oldhttp::URLResponse)>
                           callback) override;

  // Returns the serialized json string pointed by |url|.
  std::string GetSerializedValueForURL(const url::GURL& url);
  // Returns the data at the given path. If |create| is true, create the
  // necessary object and returns the empty object at the given |path|.
  rapidjson::Value* GetValueAtPath(PathView path, bool create = false);

  // The document containing the current state of the database.
  rapidjson::Document document_;
  // The watcher for the root path, recursively containing all active watchers.
  std::unique_ptr<Listeners> listeners_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_TESTING_SERVER_FIREBASE_SERVER_H_
