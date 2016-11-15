// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/watch_client_impl.h"

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
  firebase_->UnWatch(this);
}

void WatchClientImpl::OnPut(const std::string& path,
                            const rapidjson::Value& value) {
  if (!value.IsObject()) {
    FTL_LOG(ERROR) << "Ignoring a malformed commit from Firebase. "
                   << "Returned data is not a dictionary.";
    return;
  }

  if (path == "/") {
    // The initial put event contains multiple commits.
    std::vector<Record> records;
    if (!DecodeMultipleCommitsFromValue(value, &records)) {
      FTL_LOG(ERROR) << "Ignoring a malformed commit from Firebase. "
                     << "Can't decode a collection of commits.";
      return;
    }
    for (size_t i = 0u; i < records.size(); i++) {
      commit_watcher_->OnRemoteCommit(records[i].commit, records[i].timestamp);
    }
    return;
  }

  if (path.empty() || path.front() != '/') {
    FTL_LOG(ERROR) << "Ignoring a malformed commit from Firebase. " << path
                   << " is not a valid path.";
    return;
  }

  std::unique_ptr<Record> record;
  if (!DecodeCommitFromValue(value, &record)) {
    FTL_LOG(ERROR) << "Ignoring a malformed commit from Firebase. "
                   << "Can't decode the commit.";
    return;
  }

  commit_watcher_->OnRemoteCommit(record->commit, record->timestamp);
}

void WatchClientImpl::OnError() {
  // TODO(ppi): add an error callback on the CommitWatcher and
  // surface this there.
  FTL_LOG(ERROR) << "Firebase client signalled an unknown error.";
}

}  // namespace cloud_provider
