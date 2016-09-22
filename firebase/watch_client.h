// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_FIREBASE_WATCH_CLIENT_H_
#define APPS_LEDGER_FIREBASE_WATCH_CLIENT_H_

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

  // Called when an error occurs when processing an event. Further events will
  // continue to be processed after this call, until OnDone() is called.
  virtual void OnError() {}

  // Called when the server closes the stream of events.
  virtual void OnDone() {}
};

}  // namespace firebase

#endif  // APPS_LEDGER_FIREBASE_WATCH_CLIENT_H_
