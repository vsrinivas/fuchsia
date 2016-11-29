// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/watch_client_impl.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "apps/ledger/src/cloud_provider/impl/encoding.h"
#include "apps/ledger/src/cloud_provider/impl/timestamp_conversions.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"

namespace cloud_provider {

WatchClientImpl::WatchClientImpl(firebase::Firebase* firebase,
                                 const std::string& firebase_key,
                                 const std::string& query,
                                 CommitWatcher* commit_watcher)
    : firebase_(firebase), commit_watcher_(commit_watcher) {
  firebase_->Watch(firebase_key, query, this);
}

WatchClientImpl::~WatchClientImpl() {
  if (!errored_) {
    firebase_->UnWatch(this);
  }
}

void WatchClientImpl::OnPut(const std::string& path,
                            const rapidjson::Value& value) {
  if (errored_) {
    return;
  }

  if (path == "/" && value.IsNull()) {
    // If there are no matching commits, the first response after setting up the
    // watcher is null. Don't panic.
    return;
  }

  if (!value.IsObject()) {
    HandleDecodingError(path, value, "received data is not a dictionary");
    return;
  }

  if (path == "/") {
    // The initial put event contains multiple commits.
    std::vector<Record> records;
    if (!DecodeMultipleCommitsFromValue(value, &records)) {
      HandleDecodingError(path, value,
                          "failed to decode a collection of commits");
      return;
    }
    for (auto& record : records) {
      commit_watcher_->OnRemoteCommit(std::move(record.commit),
                                      std::move(record.timestamp));
    }
    return;
  }

  if (path.empty() || path.front() != '/') {
    HandleDecodingError(path, value, "invalid path");
    return;
  }

  std::unique_ptr<Record> record;
  if (!DecodeCommitFromValue(value, &record)) {
    HandleDecodingError(path, value, "failed to decode the commit");
    return;
  }

  commit_watcher_->OnRemoteCommit(std::move(record->commit),
                                  std::move(record->timestamp));
}

void WatchClientImpl::OnMalformedEvent() {
  // Firebase already prints out debug info before calling here.
  HandleError();
  commit_watcher_->OnMalformedNotification();
}

void WatchClientImpl::OnConnectionError() {
  // Firebase already prints out debug info before calling here.
  HandleError();
  commit_watcher_->OnConnectionError();
}

void WatchClientImpl::HandleDecodingError(const std::string& path,
                                          const rapidjson::Value& value,
                                          const char error_description[]) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);

  FTL_LOG(ERROR) << "Error processing received commits: " << error_description;
  FTL_LOG(ERROR) << "Path: " << path;
  FTL_LOG(ERROR) << "Content: " << buffer.GetString();

  HandleError();
  commit_watcher_->OnMalformedNotification();
}

void WatchClientImpl::HandleError() {
  FTL_DCHECK(!errored_);
  errored_ = true;
  firebase_->UnWatch(this);
}

}  // namespace cloud_provider
