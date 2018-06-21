// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_WATCH_CLIENT_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_WATCH_CLIENT_IMPL_H_

#include <rapidjson/document.h>

#include <vector>

#include "peridot/bin/cloud_provider_firebase/page_handler/public/commit_watcher.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/public/record.h"
#include "peridot/lib/firebase/firebase.h"
#include "peridot/lib/firebase/watch_client.h"

namespace cloud_provider_firebase {

// Relay between Firebase and a CommitWatcher corresponding to particular
// WatchCommits() request.
class WatchClientImpl : public firebase::WatchClient {
 public:
  WatchClientImpl(firebase::Firebase* firebase, const std::string& firebase_key,
                  const std::vector<std::string>& query_params,
                  CommitWatcher* commit_watcher);
  ~WatchClientImpl() override;

  // firebase::WatchClient:
  void OnPut(const std::string& path, const rapidjson::Value& value) override;
  void OnPatch(const std::string& path, const rapidjson::Value& value) override;
  void OnCancel() override;
  void OnAuthRevoked(const std::string& reason) override;
  void OnMalformedEvent() override;
  void OnConnectionError() override;

 private:
  void Handle(const std::string& path, const rapidjson::Value& value);

  void ProcessRecord(Record record);

  void CommitBatch();

  void HandleDecodingError(const std::string& path,
                           const rapidjson::Value& value,
                           const char error_description[]);
  void HandleError();

  firebase::Firebase* const firebase_;
  CommitWatcher* const commit_watcher_;
  bool errored_ = false;
  // Commits of the current pending batch.
  std::vector<Record> batch_;
  // Timestamp of the current pending batch.
  std::string batch_timestamp_;
  // Total size of the current pending batch.
  size_t batch_size_;
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_WATCH_CLIENT_IMPL_H_
