// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/cloud_provider_impl.h"

#include "apps/ledger/src/cloud_provider/impl/encoding.h"
#include "apps/ledger/src/cloud_provider/impl/timestamp_conversions.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/status.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

namespace cloud_provider {

CloudProviderImpl::CloudProviderImpl(firebase::Firebase* firebase,
                                     const AppId& app_id)
    : firebase_(firebase), app_id_(app_id) {}

CloudProviderImpl::~CloudProviderImpl() {}

void CloudProviderImpl::AddCommit(const PageId& page_id,
                                  const Commit& commit,
                                  const std::function<void(Status)>& callback) {
  std::string encoded_commit;
  bool ok = EncodeCommit(commit, &encoded_commit);
  FTL_DCHECK(ok);

  firebase_->Put(GetLocation(page_id) + "/" + firebase::EncodeKey(commit.id),
                 encoded_commit, [callback](firebase::Status status) {
                   if (status == firebase::Status::OK) {
                     callback(Status::OK);
                   } else {
                     callback(Status::UNKNOWN_ERROR);
                   }
                 });
}

void CloudProviderImpl::WatchCommits(const PageId& page_id,
                                     const std::string& min_timestamp,
                                     CommitWatcher* watcher) {
  watchers_[watcher].reset(new WatchClientImpl(firebase_, GetLocation(page_id),
                                               GetTimestampQuery(min_timestamp),
                                               watcher));
}

void CloudProviderImpl::UnwatchCommits(CommitWatcher* watcher) {
  watchers_.erase(watcher);
}

void CloudProviderImpl::GetCommits(
    const PageId& page_id,
    const std::string& min_timestamp,
    std::function<void(Status, const std::vector<Record>&)> callback) {
  firebase_->Get(
      GetLocation(page_id), GetTimestampQuery(min_timestamp),
      [callback](firebase::Status status, const rapidjson::Value& value) {
        if (status != firebase::Status::OK) {
          callback(Status::UNKNOWN_ERROR, std::vector<Record>());
          return;
        }
        if (!value.IsObject()) {
          callback(Status::UNKNOWN_ERROR, std::vector<Record>());
          return;
        }
        std::vector<Record> records;
        if (!DecodeMultipleCommitsFromValue(value, &records)) {
          callback(Status::UNKNOWN_ERROR, std::vector<Record>());
          return;
        }
        callback(Status::OK, records);
      });
}

void CloudProviderImpl::AddObject(ObjectIdView object_id,
                                  mx::vmo data,
                                  std::function<void(Status)> callback) {
  FTL_NOTIMPLEMENTED();
}

void CloudProviderImpl::GetObject(
    ObjectIdView object_id,
    std::function<void(Status status,
                       uint64_t size,
                       mx::datapipe_consumer data)> callback) {
  FTL_NOTIMPLEMENTED();
}

std::string CloudProviderImpl::GetLocation(const PageId& page_id) {
  return firebase::EncodeKey(app_id_) + "/" + firebase::EncodeKey(page_id);
}

std::string CloudProviderImpl::GetTimestampQuery(
    const std::string& min_timestamp) {
  if (min_timestamp.empty()) {
    return "";
  }

  return "orderBy=\"timestamp\"&startAt=" +
         ftl::NumberToString(BytesToServerTimestamp(min_timestamp));
}

}  // namespace cloud_provider
