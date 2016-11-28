// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_FIREBASE_WATCH_CLIENT_H_
#define APPS_LEDGER_SRC_FIREBASE_WATCH_CLIENT_H_

#include <rapidjson/document.h>

namespace firebase {

class WatchClient {
 public:
  virtual ~WatchClient() {}

  // See
  // https://firebase.google.com/docs/database/rest/retrieve-data#section-rest-streaming
  virtual void OnPut(const std::string& path, const rapidjson::Value& value) {}
  virtual void OnPatch(const std::string& path, const rapidjson::Value& value) {
  }
  virtual void OnCancel() {}
  virtual void OnAuthRevoked(const std::string& reason) {}

  // Called when a Firebase event of incorrect format is received. Such
  // notification is ignored, further events continue to be processed after this
  // call. (but the client might choose to close the stream themselves)
  virtual void OnMalformedEvent() {}

  // Called when the stream of events can't be established, or is interrupted,
  // or the server closes the connection. No further calls will be made on this
  // WatchClient.
  virtual void OnConnectionError() {}
};

}  // namespace firebase

#endif  // APPS_LEDGER_SRC_FIREBASE_WATCH_CLIENT_H_
