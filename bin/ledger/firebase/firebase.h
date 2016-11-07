// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_FIREBASE_FIREBASE_H_
#define APPS_LEDGER_SRC_FIREBASE_FIREBASE_H_

#include <functional>
#include <string>

#include "apps/ledger/src/firebase/status.h"
#include "apps/ledger/src/firebase/watch_client.h"
#include "lib/ftl/macros.h"

#include <rapidjson/document.h>

namespace firebase {

class Firebase {
 public:
  Firebase() {}
  virtual ~Firebase() {}

  // Retrieves JSON representation of the data under the given path. |query|
  // allows to optionally filter the data being returned and can be empty, see
  // https://firebase.google.com/docs/database/rest/retrieve-data.
  //
  // TODO(ppi): support response Content-Length header, see
  //            https://github.com/domokit/ledger/issues/8
  virtual void Get(
      const std::string& key,
      const std::string& query,
      const std::function<void(Status status, const rapidjson::Value& value)>&
          callback) = 0;

  // Overwrites the data under the given path. Data needs to be a valid JSON
  // object or JSON primitive value.
  // https://firebase.google.com/docs/database/rest/save-data
  virtual void Put(const std::string& key,
                   const std::string& data,
                   const std::function<void(Status status)>& callback) = 0;

  // Deletes the data under the given path.
  virtual void Delete(const std::string& key,
                      const std::function<void(Status status)>& callback) = 0;

  // Registers the given |watch_client| to receive notifications about changes
  // under the given |key|. |query| allows to optionally filter the data being
  // returned and can be empty, see
  // https://firebase.google.com/docs/database/rest/retrieve-data.
  virtual void Watch(const std::string& key,
                     const std::string& query,
                     WatchClient* watch_client) = 0;

  // Unregists the given |watch_client|. No calls on the client will be made
  // after this method returns.
  virtual void UnWatch(WatchClient* watch_client) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Firebase);
};

}  // namespace firebase

#endif  // APPS_LEDGER_SRC_FIREBASE_FIREBASE_H_
